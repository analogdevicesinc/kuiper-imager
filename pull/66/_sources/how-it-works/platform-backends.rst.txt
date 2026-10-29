.. _platform-backends:

Platform Backends
=================

.. description::

   The IDriveBackend seam and the Linux implementation: raw device I/O,
   privilege, and the kernel quirks the backend absorbs

Every operating-system difference in Kuiper Imager lives behind one interface,
``IDriveBackend``. It is the bottom of the :ref:`funnel <architecture>`: the
only place that touches device nodes, syscalls, and ioctls. The portable
services above it never see a file descriptor. This page covers the interface
contract, how the factory selects an implementation, and the Linux backend —
including the kernel quirks it absorbs so the rest of the code doesn't have to.

----

The IDriveBackend Contract
--------------------------

``IDriveBackend`` exposes exactly what the flash and preloader pipelines need,
grouped into three areas:

**Enumeration and mounting**

- ``listDrives()`` — one platform call returning every whole disk with its
  partitions filled in, each tagged ``isSystem`` and ``isRemovable`` (derived
  independently — see the Linux backend below). Non-system disks are the flash
  candidates.
- ``mount(partition)`` — mount a partition and return an RAII
  :ref:`MountedPartition <core-library>` handle. If the partition is already
  mounted the handle *borrows* it (and leaves it alone on destruction);
  otherwise the backend mounts to a private temp dir and the handle unmounts and
  cleans up. A partition with no filesystem is an error.

**Raw device I/O** (one device open at a time, via ``openForWrite`` /
``openForRead``)

- ``seek`` / ``write`` / ``read`` — a byte-stream abstraction. Callers pass
  arbitrary offsets and lengths; the backend does all short-write, short-read,
  and ``EINTR`` looping internally, so ``write()`` is all-or-error and ``read()``
  fills the buffer. **All block alignment is the backend's concern** — the one
  rule for callers is that ``seek()`` offsets must be block-aligned, which the
  flash caller satisfies (it only ever seeks to 0 and the 1-MiB defer-head).
- ``deviceSize`` — capacity, the source of truth for the size guard.
- ``wipeSignatures`` — zero the first 4 MiB and last 1 MiB.
- ``flushAndSync`` — flush the staged tail, ``fsync``, and drop the page cache
  so verification reads media, not cache.
- ``rereadPartTable`` — ask the kernel to re-read the table (best-effort).

The preloader safety check needs no separate topology methods: it derives the
owning drive and the target's mount state directly from ``listDrives()`` — the
drive that lists a partition is its owner, and each partition row already carries
its own mountpoint — so no ``/dev/Xp1`` string surgery leaks into the contract.

----

Selecting a Backend
-------------------

A single factory, ``makeDriveBackend()``, returns the backend for the host
platform. It is defined once per operating system, so **only the current
platform's implementation is compiled and linked** — the Linux build never
contains Windows code, and adding an OS is one more definition of this factory
plus one ``IDriveBackend`` implementation. Nothing above the funnel changes.

----

The Linux Backend
-----------------

The Linux backend enumerates with ``lsblk`` and does raw I/O with direct
syscalls. A few of its behaviors are load-bearing and worth knowing.

**Enumeration.** ``lsblk -b -J -p`` gives byte sizes, JSON, and full device
paths. Columns are restricted to what the tool consumes. When attaching
partitions to a disk the backend reads the ``children`` array but **does not
recurse** into nested children: Kuiper cards use a simple partition table, and
recursing would pull LVM/LUKS/RAID mapper nodes into the partition list where
they don't belong. (The *system* check below is separate and does walk the full
tree.) Only ``type == "disk"`` nodes are candidates; loop, ram, zram, and optical
nodes are skipped. Every partition ``node`` is taken **verbatim** from ``lsblk``
— never composed by string surgery — the whole point of enumerating instead of
guessing (see the two-card bug in :ref:`core-library`).

**System vs removable.** Each disk carries two facts, derived **independently**
— inferring one from the other ("non-removable means system") is wrong both ways:
a built-in SD reader reads as non-removable, and a USB-booted OS lives on
removable media.

*isSystem* — "does this disk host the **running** OS?" — is the safety-critical
signal, and a system disk is **never** a flash candidate (see
:ref:`the flash safety model <flash>`). A disk is system if any node in its full
subtree (a partition, or a mapper stacked above one via LVM/LUKS/RAID) is mounted
at ``/``, under ``/boot`` / ``/usr`` / ``/var`` / ``/etc``, or is swap — **or**
its ``MAJ:MIN`` matches the device backing ``/`` (from ``stat("/")``; skipped if
that fails). The walk recurses, so a root behind stacked LVM/LUKS is still found.

*isRemovable* — "can the user pull this media?" — is advisory only; a false
negative is overridable with ``--force``. ``RM`` alone is unreliable (``0`` for a
card in a built-in reader), so a disk counts as removable if ``RM`` or
``HOTPLUG`` is set, the transport is USB, or it is an SD card in an MMC slot
(``/sys/block/<dev>/device/type`` is ``SD``, not soldered-eMMC ``MMC``).

**O_DIRECT and alignment.** Devices are opened ``O_DIRECT`` where possible, so
reads and writes bypass the page cache and go straight to media — which is what
makes read-back verification trustworthy. O_DIRECT requires the buffer, the file
offset, and the transfer length all to be block-aligned; the backend owns an
aligned staging buffer (``posix_memalign``, aligned to ``max(block size, 4096)``)
and stages/bounces every transfer through it so callers can pass any offset or
length. If a target rejects O_DIRECT (``EINVAL`` — some file-backed loop
devices), the backend transparently falls back to buffered I/O and, on the read
path, drops cached pages (``posix_fadvise(DONTNEED)``) so verification still hits
the media.

**Exclusive open and the udev race.** Devices are opened ``O_EXCL`` so the flash
fails fast if anything else holds the disk. This collides with desktop udev,
which auto-mounts removable media the instant it appears — including right after
the pipeline unmounts it. The backend beats this race with a bounded retry loop
(~25 attempts, 200 ms apart, ~5 s total): on ``EBUSY`` it re-runs the full
unmount and tries again. Unmounting is deliberately **non-lazy** — a lazy
unmount would defer the release and defeat ``O_EXCL``; a genuinely busy mount is
caught here and surfaced as ``DeviceBusy`` rather than silently deferred. A
device stacked under LVM/RAID/LUKS is refused outright with ``DeviceBusy``,
since it cannot be safely released.

**Sync and cache.** ``flushAndSync`` runs ``fsync``, then ``BLKFLSBUF`` to drop
the block device's page cache and ``posix_fadvise(DONTNEED)`` as
belt-and-suspenders for the buffered fallback — together forcing the verify pass
to re-read from media. Under O_DIRECT these are near-free no-ops.

----

Privilege
---------

Raw device access requires root. On Linux the tool opens the device directly and
expects to be run with ``sudo``; an ``EACCES`` / ``EPERM`` is mapped to
``PermissionDenied`` with a "run with sudo" hint. This "open the raw node with
elevated privilege" approach is deliberately simple and dependency-free: it
avoids taking a hard runtime dependency on ``udisks2`` / D-Bus and the policy
surface that comes with it, in exchange for requiring explicit elevation.

The other platforms follow the same shape with their own elevation mechanism,
which is why privilege sits behind ``IDriveBackend`` rather than in the portable
core:

.. list-table::
   :header-rows: 1
   :widths: 18 30 52

   * - Platform
     - Raw device
     - Elevation
   * - Linux
     - ``/dev/sdX`` opened directly
     - Run under ``sudo`` (root)
   * - macOS*
     - ``/dev/rdiskN``
     - ``authopen`` hands back an fd over ``SCM_RIGHTS``
   * - Windows*
     - ``\\.\PhysicalDriveN``
     - UAC-elevated process

\* Planned for Phase 4 — see below.

----

macOS and Windows (Planned)
---------------------------

The macOS and Windows backends exist today as **structural stubs**: they
implement ``IDriveBackend`` so the tool compiles and links everywhere, but every
operation returns ``UnsupportedPlatform``. They mark the seam the Phase 4 ports
will fill (see :ref:`roadmap`):

- **macOS** — enumeration via DiskArbitration + IOKit; raw open of
  ``/dev/rdiskN`` using a file descriptor obtained from ``authopen`` and passed
  back over ``SCM_RIGHTS``; ``DKIOCGETBLOCKCOUNT`` for size.
- **Windows** — enumeration via SetupAPI + ``IOCTL_STORAGE_QUERY_PROPERTY``; raw
  open of ``\\.\PhysicalDriveN`` from a UAC-elevated process, with
  ``DeviceIoControl`` for lock, dismount, and size.

Because they sit behind the funnel, filling them in touches nothing in
``ImageService``, ``ConfigurationService``, or either front-end.
