.. _development:

The Dev Container
=================

.. description::

   Build Kuiper Imager in the reproducible Docker environment: the dev image,
   the CMake presets, running the GUI, and the Debian packages

Development and the Linux build happen in a Docker container so every
contributor compiles against the **same** toolchain and Qt. This page covers the
dev image, the repository layout, the build presets, and how the ``.deb``
packages are produced. (The Windows executables come from a native CI runner —
see :ref:`github-actions` — and macOS is still planned.)

----

Why a container
---------------

The dev image (``docker/Dockerfile``) is Ubuntu 24.04 with the full toolchain
baked in: ``build-essential``, CMake, Ninja, the C-library headers
(``libarchive-dev``, ``libssl-dev``, ``libcurl4-openssl-dev``), the Qt runtime
dependencies, and **Qt 6.8.2** installed via ``aqtinstall`` under ``/opt/qt``.
Because the exact Qt and compiler are pinned in the image, a build in the
container is reproducible and independent of whatever is on the host.

----

Project layout
--------------

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Path
     - Contents
   * - ``src/core``
     - ``libkuiper`` — the UI-agnostic core (Qt Core only): services, domain
       model, platform backends. The single source of truth both front-ends use.
       See :ref:`core-library`.
   * - ``src/cli``
     - ``kli`` — the command-line front-end (thin).
   * - ``src/gui``
     - ``kuiper-imager`` — the Qt Widgets GUI front-end. See :ref:`gui-console`.
   * - ``src/helper``
     - ``kuiper-imager-helper`` — the small privileged write helper the GUI
       elevates (see :ref:`platform-backends`).
   * - ``docker/``
     - The ``Dockerfile`` for the dev + Linux-build environment.
   * - ``debian/``
     - The Debian packaging (debhelper driving the CMake build).
   * - ``docs/``
     - This Sphinx documentation.

Both front-ends link ``libkuiper`` and call the same ``DriveService``
in-process, so they behave identically by construction — see :ref:`architecture`.

----

Building in the container
-------------------------

Build the dev image once, then open a shell in it:

.. code-block:: bash

   docker compose build          # build the dev image
   docker compose run --rm dev   # open a shell in the container

Inside, configure with a preset and build:

.. code-block:: bash

   cmake --preset dev
   cmake --build build
   ./build/src/cli/kli version
   ./build/src/cli/kli list-drives

----

Build presets
-------------

The build is driven by CMake presets (``CMakePresets.json``). The
``KUIPER_IMAGER_GUI`` option (set by each preset) toggles the GUI so CI and
headless machines can build just the CLI without Qt Widgets; the helper
(``KUIPER_IMAGER_HELPER``) follows the GUI.

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Preset
     - Build dir
     - Purpose
   * - ``dev``
     - ``build``
     - Ninja, Debug, GUI **on** — the default dev build.
   * - ``cli-only``
     - ``build-cli``
     - Ninja, Debug, GUI **off** — headless / CLI-only.
   * - ``deb-linux``
     - ``build-deb``
     - Release CLI + GUI + helper against the distro Qt (6.4+), mirroring the
       ``debian/`` debhelper build.
   * - ``ci-linux`` / ``ci-macos`` / ``ci-windows``
     - ``build-ci``
     - Release CI builds (see :ref:`github-actions`). ``ci-windows`` pulls the C
       libraries from vcpkg; the macOS row waits on its
       :ref:`backend <platform-backends>`.

----

Building natively
-----------------

With the prerequisites installed on the host, the same presets work without
Docker. Building from source requires:

- **Qt 6.8+** (the ``.deb`` build instead targets the distro's Qt, 6.4+).
- **CMake 3.22+** and a **C++23 compiler** (GCC 12+), for real
  ``std::expected`` — the basis of the :ref:`Result type <core-library>`.
- **libarchive** (image decompression), **OpenSSL/libcrypto** (hardware SHA-256),
  **libcurl** (HTTP for ``list-releases`` / ``fetch``).
- **util-linux** (``lsblk``, ``findmnt``) at runtime for drive enumeration.

.. code-block:: bash

   cmake --preset dev && cmake --build build

----

Running the GUI from the container
----------------------------------

The compose file mounts the host X11 socket, so the GUI can display on the
host. Allow local connections once per session and launch it:

.. code-block:: bash

   xhost +local:
   docker compose run --rm dev ./build/src/gui/kuiper-imager

----

Debian packages
---------------

The ``debian/`` tree (debhelper driving CMake, hardening ``+all``) produces
**two** binary packages from one build against the **distro's** Qt (6.4+):

- ``kuiper-imager-cli`` — just ``kli``. No GUI or desktop stack, so it suits
  headless servers, CI runners, and scripted flashing.
- ``kuiper-imager`` — the GUI, the privileged write helper, the polkit policy,
  and a desktop entry + icon. On an x86 host it recommends
  ``qemu-user-static`` + ``binfmt-support`` for :ref:`customize <customize>`.

Build both with ``dpkg-buildpackage`` (ideally inside a ``debian:12`` container
so they link the Qt the target users have):

.. code-block:: bash

   # install build-deps from debian/control, then build binary-only, unsigned
   mk-build-deps --install --remove --tool 'apt-get -y' debian/control
   dpkg-buildpackage -us -uc -b
   # kuiper-imager-cli_*.deb and kuiper-imager_*.deb land in the parent directory

Qt is linked dynamically against the distribution's packages: nothing from Qt is
bundled, so the ``.deb`` carries no LGPL source-availability obligation. To
reproduce the packaged build locally without ``dpkg-buildpackage``, the
``deb-linux`` preset configures the same CLI + GUI + helper build.

----

.. _testing:

Verification
------------

The automated test suite was removed while the cross-platform backends land.
Verification today is the CI build/smoke gate (see :ref:`github-actions`) plus a
manual flash check:

- **Flash parity (Linux)** is checked by hand against a loop device: write an
  image to ``/dev/loopN`` with ``--force`` and confirm the read-back verify
  passes (the reported SHA-256 matches the source). The whole
  :ref:`flash pipeline <flash-pipeline>` — wipe, defer-head write, verify — runs
  above the backend, so this also exercises the logic the Windows backend reuses
  through the shared ``AlignedRawDevice`` engine.
- **Flash (Windows)** is validated on a real machine with an SD card, from an
  elevated prompt, once CI confirms the backend compiles and links.
