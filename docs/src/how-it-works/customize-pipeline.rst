.. _customize-pipeline:

The Customize Pipeline
======================

.. description::

   How ``customize`` turns a basic image into a full one: the feature catalog,
   the chroot/qemu model, the RAII teardown, and the testable command seam

Where :ref:`flash <flash-pipeline>` writes an image to a card untouched,
``customize`` edits the image *itself*: it loop-mounts a prebuilt ``.img``,
installs the selected ADI features through apt inside the image's chroot,
writes the few asset files apt cannot, restamps the image identity, and tears
everything back down. It is a C++ port of the Kuiper build's
``tools/image-customize.sh``, driven by a data-only feature catalog instead of a
shell script.

This page describes the internals — ``ImageCustomizer`` and its Linux executor.
For the user-facing commands and flags, see :ref:`customize`.

----

Why a Port, Not a Shell-Out
---------------------------

The Kuiper build already has ``image-customize.sh``. Re-expressing it in C++
buys three things the imager cares about:

- **One testable seam.** Every privileged step — ``losetup``, ``mount``,
  ``chroot``, ``apt-get`` — is an ``argv`` run through a single ``CommandRunner``
  interface. Production uses a ``QProcess`` implementation (merged stdout/stderr,
  line streaming); tests inject a fake that records the ``argv`` sequence instead
  of executing it. This is the same "swappable impl, fake-testable" shape as
  ``IHttpClient`` and ``IDriveBackend``. A launch failure is an error; a program
  that runs and exits non-zero is *not* — it returns with a non-zero exit code
  and the caller decides what that code means.
- **Guaranteed teardown.** The script's single ``EXIT`` trap becomes an RAII
  LIFO stack (``Unwind``): each setup step pushes its own undo, and the
  destructor runs them in reverse on *every* exit path — success, error, or
  Ctrl-C. The loop device, the mounts, the staged emulator, and the swapped
  ``resolv.conf`` are always restored.
- **Shared planning.** The pure, I/O-free parts (resolving catalog ids, sizing
  the grow, building the ``chroot``/``apt`` argvs, size-string parsing) live in a
  planning module reused by the real executor and by ``--dry-run``, so the plan
  you preview is the plan that runs.

----

The Feature Catalog
-------------------

A Kuiper image is **basic** or **full** only by which ``CONFIG_*`` switches were
on at build time. The catalog (``FeatureCatalog.cpp``) is the data that closes
that gap: a hand-authored, stable-ordered table mirroring the Kuiper build, where
each ``FeatureDef`` is one installable unit — a desktop, a library, or an
application.

Each feature carries its apt package list, whether to pull recommends, any
post-install ``chroot`` shell snippets, a rough size estimate (for auto-sizing
the grow), an optional arch restriction, and any files to drop into the rootfs.
It maps directly onto the Kuiper repo:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Catalog field
     - Kuiper source
   * - package lists
     - the ``USE_ADI_REPO=y`` paths of ``stages/05.adi-tools/*/run.sh``
   * - the ``desktop`` feature
     - ``stages/04.configure-desktop-env/{01.desktop-env,02.vnc-server,03.cosmetic}``
   * - asset files
     - the ``install -m … files/<x>`` steps, vendored verbatim (see below)
   * - apt repo + key + pinning
     - ``stages/01.bootstrap`` — already on *every* image, so features install
       straight from apt

Two deviations are intentional and reviewed: ``-dbgsym`` debug-symbol packages
are omitted (a functional image does not need them), and ``adi-scripts`` is not
listed because the build installs it unconditionally, so a basic image already
has it. This table should eventually be **generated** from the Kuiper repo in CI
rather than hand-maintained.

Vendored assets
~~~~~~~~~~~~~~~

Some steps are not apt installs — systemd units, the desktop wallpaper, the menu
icon, the RPi5 ``99-vc4.conf``. Those files are copied verbatim from the Kuiper
repo into ``src/core/assets/desktop/`` and embedded into ``libkuiper`` as a Qt
resource (``kuiper_assets.qrc``), so the bytes written into the image stay
*identical* to the build instead of being hand-rewritten in C++. A feature writes
them after its apt install and before its ``chroot`` snippets, honoring each
file's arch restriction.

----

The Pipeline, Step by Step
--------------------------

``customize()`` validates cheaply first, then runs a fixed sequence. Everything
that can fail without touching the image is checked **before** the destructive
grow.

.. list-table::
   :header-rows: 1
   :widths: 6 26 68

   * - #
     - Step
     - What happens
   * - 1
     - Resolve plan
     - Resolve feature ids against the catalog (unknown id is an error) and size
       the grow. A plan needs features, a script, or both; empty on both is a
       mistake, not a no-op.
   * - 2
     - Validate script
     - If a custom script was given, confirm it exists and parses under
       ``bash -n`` — up front, so a typo fails here instead of mid-chroot with the
       image already mounted and grown. Runs even in ``--dry-run``.
   * - 3
     - Dry run (optional)
     - If ``--dry-run``, report the computed plan and return. Nothing is touched,
       as root or not.
   * - 4
     - Precheck
     - Require root (loop-mount and chroot need it) and a regular image file, and
       confirm the host tools exist (``losetup``, ``parted``, ``e2fsck``,
       ``resize2fs``, ``mount``, ``chroot``, …).
   * - 5
     - Attach + read layout
     - Loop-mount the image (``-P`` for partition nodes), settle udev, then
       enumerate partitions by ``blkid`` and classify roles through the shared
       ``identifyLayout`` — boot/root come from inspection, never a composed node
       string.
   * - 6
     - Arch + qemu
     - Determine the image arch and validate the qemu/binfmt posture (see below).
       Both happen before the grow so an unsupported host fails cheaply.
   * - 7
     - Grow (optional)
     - Enlarge the image file, ``resizepart`` the rootfs to 100%, ``e2fsck``, and
       ``resize2fs``. Only a **physically last** rootfs can grow (see note).
   * - 8
     - Mount + prepare chroot
     - Mount rootfs and boot, bind ``/dev`` ``/proc`` ``/sys`` ``/dev/pts``, stage
       the emulator if needed, and swap in a working ``resolv.conf``.
   * - 9
     - Install features
     - ``apt-get update`` once, then install each feature's packages, write its
       files, and run its ``chroot`` snippets — streamed live to the log.
   * - 10
     - Run custom script (optional)
     - Copy the host script into the rootfs ``/tmp``, run it as root in the
       prepared chroot (it gets working apt and network for free), then remove it.
       Runs last so packages it installs are reflected in the manifest.
   * - 11
     - Restamp + teardown
     - Restamp the image identity (optional), ``sync``, and let the ``Unwind``
       stack restore ``resolv.conf``, remove the emulator, unmount, and detach.

.. note::

   **"Last" means physically last — the greatest end offset, not the highest
   partition number.** A Kuiper card can carry a small bootloader partition
   numbered last but placed first; only a rootfs at the physical end of the disk
   can grow into trailing free space. The executor finds it from ``sfdisk -J``.

----

Architecture Detection and the Chroot
--------------------------------------

The Kuiper rootfs is ARM, so apt inside the chroot runs ARM binaries. Whether
that needs qemu user-mode emulation depends not on whether the *host* is ARM but
on whether the host can execute the *image's* execution state natively. The
executor reads ``KUIPER_ARCH`` from the image's ``/etc/kuiper-release`` (mounting
the rootfs read-only *before* the grow, so it unmounts cleanly leaving nothing
behind) to pick the emulator — ``arm64`` → ``qemu-aarch64``, ``armhf`` →
``qemu-arm``. An image without the marker needs ``--arch``.

``arm64`` (AArch64) and ``armhf`` (AArch32) are **different execution states**,
not interchangeable members of one "ARM" family. A host runs an image natively
only when their states match: an ``aarch64`` host runs an ``arm64`` image, and a
32-bit ARM host runs an ``armhf`` image. Every other pairing needs qemu — crucially
an **``armhf`` image on an ``aarch64`` host** (e.g. a 32-bit Kuiper customized on
an arm64 Raspberry Pi 5). Such kernels do not run 32-bit binaries at EL0 by
default — ``execve`` of a 32-bit ELF returns ``ENOEXEC`` — so relying on native
execution would fail mid-chroot. The executor therefore treats a mismatched state
exactly like an x86 host and routes it through qemu, rather than gambling on a
32-bit-userspace kernel option that may not be present.

The qemu posture is validated without touching the image:

- **Host matches the image's execution state** — native; nothing is needed.
- **Mismatched state, binfmt with the** ``F`` **(fix_binary) flag** — the default
  with ``qemu-user-binfmt`` + ``binfmt-support``. The kernel already holds the
  interpreter open, so the chroot just works; nothing is copied in.
- **Mismatched state, non-**\ ``F`` **registration** — the interpreter must be
  copied into the chroot (and removed again on teardown).
- **binfmt missing or disabled** — fail up front with the exact ``apt install`` /
  ``update-binfmts`` hint, before anything changes.

----

Restamping Image Identity
-------------------------

After the installs, ``customize`` makes the result describe itself honestly —
mirroring the build's ``stages/08.export-stage/00.image-identity``. It sets
``KUIPER_VARIANT=custom`` in both ``/etc/kuiper-release`` and the BOOT
partition's ``kuiper-release.json``, and regenerates ``/etc/kuiper-packages`` from
``dpkg-query`` so the imager's own ``readRelease()`` reports the customized image
accurately.

----

Front-Ends and the Privileged Helper
-------------------------------------

The same core ``ImageCustomizer`` drives three callers. The CLI (``kli
customize``) calls it directly. The GUI "Customize…" dialog cannot — the install
is privileged — so, exactly like the flash and the Intel preloader write, it runs
the work in the elevated ``kuiper-imager-helper`` (``pkexec``) and talks to it
over the NDJSON :ref:`helper protocol <platform-backends>`, streaming apt output
line by line into a log (apt has no honest overall fraction, so the log — paired
with an indeterminate progress bar — is the truthful readout, not a percentage).

So the three front-ends can't disagree about size grammar or the auto-grow
preview, the size parser and the auto-grow estimate live in ``kuiper::customize``
and are shared by all of them; the GUI's preview is only a preview, while the
executor computes the real grow.
