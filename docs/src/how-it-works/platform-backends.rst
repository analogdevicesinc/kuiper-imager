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
  partitions filled in; removable, non-system disks are the flash candidates.
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

**Topology queries** (read-only lookups over the block tree)

- ``parentDisk(node)`` — the whole disk that owns a partition node, or empty if
  the node is already a whole disk.
- ``mountpointOf(node)`` — current mountpoint, or empty if not mounted.

These read-only lookups shell out to ``util-linux`` from *inside* the backend
rather than from the portable services — the funnel invariant applies even to
lookups that never open a device, so no platform assumption leaks upward.

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
paths. Columns are restricted to what the tool actually consumes. The backend
reads the ``children`` array to attach partitions to each disk but **does not
recurse** into nested children: Kuiper cards use a simple partition table, and
recursing would pull in LVM/LUKS/RAID mapper nodes that are not partitions of
the drive. Only ``type == "disk"`` nodes are candidates; loop, ram, zram, and
optical nodes are skipped. Every partition ``node`` is taken **verbatim** from
``lsblk`` — never composed by string surgery — which is the whole point of
enumerating instead of guessing (see the two-card bug in :ref:`core-library`).

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
