.. _flash-pipeline:

The Flash Pipeline
==================

.. description::

   How a local image becomes a verified, bootable card: decompression, the
   defer-head safety model, and dual-hash verification

Flashing turns a local image file into a verified card. The pipeline
decompresses on the fly, writes the payload before the partition table, verifies
by reading the device back, and only then commits the bytes that make the card
bootable. The result is an all-or-nothing guarantee: a flash that is cancelled,
crashes, or fails verification never leaves a card that *looks* bootable but
isn't.

This page describes ``DriveService::flash()``. The raw-I/O primitives it builds
on — exclusive open, O_DIRECT, unmount, sync — belong to the platform backend
and are documented in :ref:`platform-backends`.

----

Accepted Formats
----------------

``flash()`` takes a **local file** (downloading is a separate step —
:ref:`fetch`) and decompresses it through libarchive. Accepted inputs are a raw
``.img`` or a single-stream ``.gz`` / ``.xz`` / ``.zst`` / ``.bz2``, or a
``.zip``.

Only the codecs Kuiper actually ships images in are enabled — the gzip, xz,
zstd, and bzip2 filters plus the raw and zip formats. ``support_format_all`` is
deliberately *not* used: the tar and ISO readers can mis-bid on a raw disk
image and silently mangle it. A missing codec is reported up front rather than
mid-stream.

A ``.zip`` is handled specially. The file is sniffed for the ZIP magic
(``PK\x03\x04``), then its central directory is scanned and the single largest
regular file is chosen as the image. If two or more entries are each larger than
1 MiB the archive is ambiguous and ``flash()`` refuses to guess — extract the
intended ``.img`` and flash it directly.

----

The Defer-Head Safety Model
---------------------------

**The first 1 MiB of the image — the partition table — is written last.** This
is the single most important property of the pipeline.

On every modern disk image the MBR / GPT and the protective headers live in the
first 1 MiB, and the first real partition starts at or after the 1-MiB mark. The
pipeline captures those bytes (``kDeferHead = 1 MiB``) in memory instead of
writing them, streams the entire body to the device *after* that offset, and
writes the retained head only once the body is on media and verified.

The consequence: until the very last moment there is no valid partition table on
the card, so a card that was interrupted at any earlier point is inert, not
half-bootable. Cancellation (``UserCancelled``), a write error, a decompression
failure, or a failed body verification all return with the head still unwritten
— the card is non-bootable *by construction*, and no clean-up wipe is needed on
those paths. 1 MiB is block-aligned on every real device, so this safety model
costs the pipeline no block-size knowledge of its own.

----

The Eleven Steps
----------------

``flash()`` runs a fixed sequence. Steps 1–6 prepare; step 7 writes the body;
steps 8–11 flush, verify, and commit the head.

.. list-table::
   :header-rows: 1
   :widths: 6 26 68

   * - #
     - Step
     - What happens
   * - 1
     - Guard
     - Re-enumerate drives (never trust cached metadata) and refuse a missing,
       non-removable, or system drive. ``--force`` bypasses these checks for
       loop devices and power users; the backend still refuses anything that
       isn't a block device.
   * - 2
     - Validate image
     - The source must be a regular, non-empty file.
   * - 3
     - Unmount
     - Unmount every filesystem on the target and swapoff any swap.
   * - 4
     - Open exclusive
     - Open the whole-drive node ``O_EXCL`` with a bounded EBUSY retry that
       re-races udev auto-remount (see :ref:`platform-backends`).
   * - 5
     - Capacity
     - Read the device size from the device itself (``BLKGETSIZE64``); the image
       must fit.
   * - 6
     - Wipe signatures
     - Zero the first 4 MiB and last 1 MiB so a smaller image can't leave a
       ghost partition table or backup GPT behind.
   * - 7
     - Write body
     - Decompress and stream the body to the device starting at offset 1 MiB,
       capturing the first 1 MiB (the head) in memory. Hash as we go.
   * - 8
     - Flush
     - ``fsync`` the staged body to media and drop the page cache — the honest,
       slow step.
   * - 9
     - Verify body
     - Re-read the body from the device and compare its hash **before**
       committing the head. On mismatch the head stays unwritten, so the card
       stays non-bootable. Skipped by ``--no-verify``.
   * - 10
     - Commit head
     - Seek to 0, write the retained partition table, ``fsync``, and ask the
       kernel to re-read the table.
   * - 11
     - Verify head
     - Read the head back and compare it against the retained bytes. On mismatch
       the signatures are re-wiped so a bad table is never left live.

----

Dual-Hash Verification
----------------------

The pipeline computes **two** SHA-256 digests during the write, for two
different jobs:

- **Full hash** — over the whole image in order (head + body). This is always
  computed and is the digest reported in the ``FlashSummary``; it identifies
  what was flashed.
- **Body hash** — over the body region only. It exists solely to verify the body
  in step 9, *before* the head is committed, so it is skipped entirely when
  verification is off (avoiding a redundant second hash pass over every body
  byte).

Verification is genuine read-back: step 9 re-reads the body from the device
(through O_DIRECT or a dropped page cache, so it hits the media, not a buffer)
and compares hashes; step 11 reads the just-written head back byte-for-byte. A
verified flash therefore proves that what is *on the card* matches the image —
the integrity guarantee that distinguishes Kuiper Imager from a bare ``dd``.
Hashing runs through the shared OpenSSL ``Sha256`` wrapper (see
:ref:`core-library`), fast enough that it is not the bottleneck.

----

Write Options
-------------

Two flags, both defaulting to the safe setting:

- ``force`` (``--force``) — bypass the target-safety guards (presence,
  removable/system, and, for the preloader write, the mounted-partition check).
  Intended for loopback devices and power users. The backend still refuses
  anything that isn't a block device. This is the guard that stops you erasing
  the wrong disk; use it deliberately.
- ``verify`` (on by default; ``--no-verify`` turns it off) — read the target
  back and compare. Turning it off skips only the full-body read-back — roughly
  half the wall-clock time — because the head is *still* written last and
  checked (step 11), so a failed flash never looks bootable either way.

``flash()`` also returns a ``FlashTimings`` breakdown (prepare / write /
decompress / device-write / flush / verify / head / total, all wall-clock
seconds). It is a diagnostic surface: it shows where the time actually goes and
lets any performance change be measured directly — for example, whether device
I/O lands in the write step (as it does under O_DIRECT) or in the flush step (as
buffered I/O would push it).

----

Writing the Preloader
---------------------

Intel-based Kuiper cards carry a small bootloader (preloader) on a raw,
unformatted partition. ``DriveService::writePreloader()`` handles it — a
narrower cousin of ``flash()`` invoked by :ref:`configure`.

The differences from a whole-drive flash: it writes an uncompressed blob to a
**partition** at offset 0 (a preloader must start at the partition head, so
there is no defer-head), it computes a single digest, and it adds a
mounted-target guard — a partition is a plausible live filesystem, so a mounted
one is refused unless ``--force``. It still validates, unmounts, opens
exclusively, size-checks, flushes, and verifies by read-back. Keeping this
device write in ``DriveService`` is what lets :ref:`ConfigurationService
<configure>` stay platform-free: it reaches the write through an injected
callback rather than touching a device itself.
