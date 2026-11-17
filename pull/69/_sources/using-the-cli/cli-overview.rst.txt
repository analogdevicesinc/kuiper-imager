.. _cli-overview:

CLI Overview
============

.. description::

   How the ``kli`` command line works: the whole-drive model, output and exit
   codes, and the privilege and token requirements shared by every command

``kli`` is the command-line front-end and the scriptability surface of Kuiper
Imager. It is a thin layer over :ref:`libkuiper <core-library>`: it parses
arguments, renders progress, and translates typed results into output and exit
codes. Every command follows the conventions on this page.

.. code-block:: text

   kli version
   kli list-drives
   kli list-releases [--unstable] [--branch <branch>] [--limit <n>]
   kli list-projects --drive <dev>
   kli fetch     --image <id>   --output <path> [--force]
   kli flash     --image <file> --drive <dev>   [--yes] [--force] [--no-verify] [--timings]
   kli configure --drive <dev>  --project <name> --board <carrier> [--dry-run] [--force]
   kli help

----

The Whole-Drive Model
---------------------

Every command that touches a card takes one identifier: ``--drive``, and it is
**always a whole drive** — ``/dev/sda``, ``/dev/mmcblk0``. You never name a
partition. Kuiper Imager enumerates the drive and identifies its BOOT, rootfs,
and bootloader partitions itself, by inspection (see the :ref:`layout rules
<core-library>`).

As a convenience, a partition node such as ``/dev/sda1`` is also accepted and
resolves to its parent drive, so a mistyped partition still works instead of
erroring. This single-identifier model is what makes the tool robust against the
two-card label collision described in :ref:`core-library`.

----

Output Conventions
------------------

``kli`` produces **human-readable output only** — there is no JSON or
machine-readable mode, because nothing in the current design consumes one (see
:ref:`architecture`). Output is split by stream so it composes cleanly:

- **stdout** — results: drive tables, release listings, the final summary.
- **stderr** — everything else: progress bars, the flash confirmation prompt,
  and errors (``error [code]: message``, with an indented detail line and an
  optional ``hint:``).

Progress is redrawn in place on stderr and throttled to whole-percent changes,
so piping stdout to a file or another program leaves you with just the results.

----

Exit Codes
----------

``kli`` maps each error's :ref:`ErrorCode <core-library>` to a stable process
exit code, so scripts can branch on the *kind* of failure:

.. list-table::
   :header-rows: 1
   :widths: 12 30 58

   * - Code
     - Meaning
     - Typical cause
   * - 0
     - Success
     - The command completed.
   * - 1
     - Generic failure
     - Any error without a specific code (also: no arguments, unknown command).
   * - 2
     - Usage error
     - A missing or unexpected argument.
   * - 3
     - ``UserCancelled``
     - Ctrl-C, or declining the flash confirmation prompt.
   * - 4
     - ``PermissionDenied``
     - Raw device access without root; a rejected download token.
   * - 5
     - ``DeviceBusy``
     - A live mount / swap / LVM / RAID / LUKS holder on the target.
   * - 6
     - ``DeviceRemoved``
     - The card was pulled, or an I/O error during the operation.
   * - 7
     - ``InvalidImage``
     - The image or target isn't a usable file / block device.
   * - 8
     - ``DiskFull``
     - The image is larger than the target, or the download exceeds free space.
   * - 9
     - ``HashMismatch``
     - Read-back verification failed — the media doesn't match the image.
   * - 10
     - ``NotFound``
     - Drive, project, release, or artifact not found.

``NotKuiper2`` (a card that isn't a flashed Kuiper card) and other uncoded
errors fall through to exit code 1.

----

Privileges and Tokens
---------------------

**Root.** Flashing and the Intel preloader write require raw device access, so
``flash`` and (for Intel projects) ``configure`` must run under ``sudo``. Without
it you get ``PermissionDenied`` (exit 4) with a "run with sudo" hint. See the
:ref:`privilege model <platform-backends>`.

**GitHub token.** Downloading a CI artifact with ``fetch`` needs a GitHub token
with the ``actions:read`` scope. The token is discovered in order:

1. ``$GH_TOKEN``
2. ``$GITHUB_TOKEN``
3. ``gh auth token`` (if the GitHub CLI is installed and logged in)

Token discovery lives in the CLI, not the core, so ``libkuiper`` stays free of
environment and ``gh``-CLI assumptions. Listing releases needs **no** token — a
token there only raises the API rate limit. See :ref:`fetch` for details.
