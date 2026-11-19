.. _configure:

Configure: Set Up Boot Files
============================

.. description::

   Scan the boot partition and copy the correct boot files for your eval-board
   and carrier combination

A freshly flashed Kuiper card is generic. ``configure`` specializes it for one
hardware combination — an eval board on a carrier — by copying the right kernel
and boot files into place. It is a C++ port of ADI's ``configure-setup.sh`` and
is faithful to that script's behavior. Raspberry Pi cards boot without this step
and can skip it.

Configure is portable file work (see :ref:`ConfigurationService <core-library>`):
it operates on the already-mounted BOOT partition, which the CLI mounts for you
from ``--drive``.

----

list-projects
-------------

.. code-block:: bash

   kli list-projects --drive <dev>

Mounts the card's BOOT partition and lists the hardware projects declared in its
``*.json`` manifests. The columns are ``EVAL BOARD``, ``CARRIER``, ``PLATFORM``,
and ``ARCH``. A project is identified by the **(eval board, carrier)** pair — the
same eval board recurs across several carriers — which is exactly what
``configure`` needs as ``--project`` and ``--board``.

Manifest scanning mirrors the script: manifests are found recursively, malformed
or non-manifest JSON is skipped silently, and a valid card with no projects
yields an empty list rather than an error. Output is sorted for stable results.

----

configure
---------

.. code-block:: bash

   sudo kli configure --drive <dev> --project <name> --board <carrier> [--dry-run] [--force]

Copies the selected project's kernel and its manifest ``files[]`` into the
BOOT-partition root, where the board's bootloader looks for them. For example:

.. code-block:: bash

   sudo kli configure --drive /dev/mmcblk0 --project ad9081 --board vck190

Key behaviors:

- **All-or-nothing preflight.** Every source file (and, for Intel, the
  preloader) must exist before *anything* is copied, so a bad or mismatched
  manifest never leaves a half-configured card. A project that declares no kernel
  is rejected up front.
- **Dry run.** ``--dry-run`` prints the exact copy plan (``Would copy … -> …``)
  without touching the card — the safe way to preview what a project will do.
- **First match wins.** ``configure`` resolves ``--project`` + ``--board`` using
  the same scan as ``list-projects``, taking the first match (faithful to the
  script's ``head -n 1``); an unknown combination is ``NotFound``.

On success it lists what it copied and the project/board it configured.

----

Intel Preloader and extlinux
----------------------------

Intel-based projects need two things beyond a file copy, both handled
automatically:

- **A raw preloader write.** The Intel bootloader (preloader) is written to the
  card's unformatted bootloader partition. This is the one device operation
  ``configure`` performs, and it needs root. The bootloader partition comes from
  the :ref:`identified layout <core-library>` — never a string-composed device
  node — so if the card has no such partition, an Intel project fails with a
  clear error. ``--force`` allows a non-removable bootloader target.
- **extlinux layout.** Intel boots via ``extlinux``, so ``configure`` moves
  ``extlinux.conf`` into an ``extlinux/`` directory (mirroring the script).
  Non-Intel boards get any stale ``extlinux/`` removed, so a card reconfigured
  from Intel to another platform boots correctly.

The preloader write is wired from the CLI's composition root to
:ref:`DriveService::writePreloader <flash-pipeline>` through a ``PreloaderSink``
callback, which is what lets ``ConfigurationService`` remain 100% portable while
still performing a privileged device write. If the tool is ever used without that
sink wired in, an Intel project fails cleanly and prints the exact manual
procedure (``cp`` commands, the ``extlinux`` move, and the ``dd`` preloader
write) instead of doing anything halfway.
