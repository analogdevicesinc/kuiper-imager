.. _flash:

Flash: Write an Image to a Drive
================================

.. description::

   Write a local image to an SD card or drive with progress and read-back
   verification

``flash`` writes a local image onto a card and verifies it. It is the one
destructive command — it erases the target — so it guards the operation with a
confirmation prompt and a suite of safety checks. The full mechanics are in
:ref:`flash-pipeline`; this page is the command reference.

----

The flash command
-----------------

.. code-block:: bash

   sudo kli flash --image <file> --drive <dev> [--yes] [--force] [--no-verify] [--timings]

Find the target with ``kli list-drives`` first, then flash:

.. code-block:: bash

   kli list-drives
   sudo kli flash --image kuiper.img.xz --drive /dev/sda

``--image`` is a **local file** (download first with :ref:`fetch`). It may be a
raw ``.img`` or a compressed ``.zip`` / ``.xz`` / ``.zst`` / ``.gz`` / ``.bz2``,
decompressed inline as it is written. ``--drive`` is a whole drive (see the
:ref:`whole-drive model <cli-overview>`). Flashing needs root — run with
``sudo``.

Before writing, ``flash`` prints a loud confirmation naming the drive it will
erase and waits for you to type ``y``. Declining (or Ctrl-C) aborts with exit
code 3 and touches nothing. Progress is shown per phase on stderr; on success it
prints the bytes written, whether the image was verified, the SHA-256, and a
``Next:`` hint toward :ref:`configure`.

----

Options
-------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Option
     - Effect
   * - ``--yes`` / ``-y``
     - Skip the confirmation prompt. Use in scripts, once you are certain of the
       target.
   * - ``--force``
     - Bypass the removable/system-drive guard (for example to write a
       ``/dev/loopN`` loop device). The backend still refuses anything that
       isn't a block device. This is the guard that stops you erasing the wrong
       disk — use it deliberately.
   * - ``--no-verify``
     - Skip the full read-back verification (~half the wall-clock time). The
       partition table is *still* written last and checked, so a failed flash
       never looks bootable — you only give up the whole-image integrity proof.
   * - ``--timings``
     - Print a per-phase wall-clock breakdown (prepare / write / flush / verify
       / head) to stderr after a successful flash.

----

Verification and Safety
-----------------------

Read-back SHA-256 verification is the integrity guarantee that distinguishes
``flash`` from a raw ``dd``: after writing, the device is read back and hashed to
prove that what is *on the card* matches the image. It is on by default.

Equally important, the partition table is always written **last**, after the body
is on media and verified. This means a flash that is cancelled, fails, or has
verification turned off never leaves a card that *looks* bootable but isn't — an
interrupted flash yields an inert card, not a corrupt one. ``flash`` also
re-enumerates the drive up front (never trusting cached metadata), refuses
non-removable/system drives unless ``--force``, unmounts everything on the
target, and wipes stale signatures so a smaller image can't leave a ghost
partition table behind. See :ref:`flash-pipeline` for the complete step-by-step.
