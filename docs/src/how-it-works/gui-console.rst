.. _gui-console:

The GUI Console
===============

.. description::

   How the Qt console window drives the core off the UI thread, tracks an
   image's provenance, reuses the download cache, and lands a write safely

The GUI is a single window, ``ConsoleWindow``, built from three stacked cards —
**Image**, **Storage**, and an action area — over the same ``libkuiper`` the CLI
uses (see :ref:`architecture`). It holds no business logic: it binds core
callbacks to progress bars and banners, keeps every blocking call off the UI
thread, and translates typed ``Result`` values into messages. This page covers
the pieces that are GUI-specific — the threaded execution model, how a selected
image carries its provenance, the download cache, and the post-write flow that
ends in safe removal.

----

The Execution Model
-------------------

Every core call that can block runs on a ``BackgroundTask`` worker, never on the
UI thread. A job writes its result into a ``shared_ptr<std::optional<Result<…>>>``
holder (``drivesOut_``, ``probeOut_``, ``releasesOut_``, ``fetchOut_``,
``ejectOut_``); the window reads the holder back in the ``finished`` slot. The
``shared_ptr`` is what makes this safe: the holder outlives the window if a job
is still running at close time, so a late completion has somewhere to land.

The one exception is the **write**, the single privileged step. It does not run
in-process: it runs in the elevated ``kuiper-imager-helper`` (``pkexec`` / UAC),
so its result lives in ``helper_->result()``, not a holder. Everything else —
enumerate, probe, list releases, fetch, eject — is an ordinary unprivileged core
call on the worker. See :ref:`platform-backends` for why only the write needs
elevation.

``busy()`` is true whenever either the worker or the helper is in flight. It
gates every ``start*`` entry point and drives ``applyState()``, so the UI can
never launch a second job over a running one. Closing the window while busy is
deferred (``closePending_``): the running job is cancelled and the window closes
once it reports back.

A selected Kuiper 2 card is held in a ``BootContext`` — the drive, its layout,
an RAII ``MountedPartition`` for BOOT, and (iff ``kuiper-release.json`` was
found) the release and project list. Dropping that context unmounts what we
mounted, which shells out to ``udisksctl`` and can block, so ``takeBootContext()``
hands it to a worker job rather than releasing it on the UI thread.

----

The Three Cards
---------------

Each card owns the one action that edits its object, so the window reads as
"prepare each input, then commit":

.. list-table::
   :header-rows: 1
   :widths: 16 44 40

   * - Card
     - Holds
     - Owns the action that edits it
   * - Image
     - The selected image, its size, and a provenance pill
     - **Customize** (Linux only) — add ADI features to a raw local ``.img``
       (see :ref:`customize-pipeline`)
   * - Storage
     - The target drive, with removable / system / Kuiper 2 tags
     - **Configure** — set up boot files on a flashed Kuiper 2 card
       (see :ref:`configure`)
   * - Action
     - Verify toggle, progress, and the result banner
     - **Write** — the single commit action below both cards

----

Image Provenance and the Download Cache
---------------------------------------

The Image card tracks *how* the current image was obtained, as an
``ImageOrigin`` (``None`` / ``Local`` / ``Downloaded``). Two things read it: the
provenance pill (``Local`` vs ``Downloaded``), and the Customize gate — Customize
is offered only for a **raw local** ``.img``, never a downloaded or compressed
one. There is no source toggle to infer the origin from; the pill is the only
cue, so it is always shown once an image is selected.

Downloads land in a per-user cache (``CacheLocation/images``). The window asks
the core where a release *would* live there with
:ref:`ImageService::artifactPath <releases-and-sources>` — which resolves the
path without downloading — and treats the release as cached when that path
exists on disk. This drives three behaviors:

- **Reuse.** Picking a release that is already cached selects the file in place
  ("Already downloaded — reused from cache") instead of fetching it again.
- **Inspect and delete.** The release picker badges cached rows "Downloaded",
  shows their real on-disk size, and offers an in-place delete that reclaims the
  file.
- **Self-heal.** If a delete inside the picker removes the file that is currently
  selected, the selection is dropped (``clearImage``) so the card never points at
  a missing file.

----

The Post-Write Flow
-------------------

A write ends in a specific sequence, because the obvious "show the banner when
the helper finishes" is wrong here.

When the helper reports success, the window immediately re-enumerates and
re-probes the just-written card (``driveList_`` is now stale) so the Storage card
can recognise it as Kuiper 2 and light up Configure. That re-probe flips on the
``busy`` UI ("Scanning drives…"), which would wipe a success banner shown in the
same frame.

So the banner is **deferred**: its title, message, and the node to eject are
stashed (``pendingWriteTitle_`` / ``pendingWriteMsg_`` / ``pendingEjectNode_``),
and ``showWriteComplete()`` renders them only once the re-probe has settled. The
re-probe itself retries on a short budget (``writeReprobeLeft_``) to cover a slow
udisks, though the helper settles the new partition table before exiting, so the
first probe almost always succeeds.

.. note::

   The settled banner offers a **Safe eject** action wherever the backend
   implements eject (Linux and Windows — not macOS). The card is left *mounted*
   after a write so Configure keeps working; Safe eject is the one explicit click
   that releases our BOOT mount (off-thread — it can block), then ejects the
   whole device through :ref:`DriveService::eject <platform-backends>` so the OS
   won't re-mount it before the card is pulled. On failure the action is
   re-offered so the user can retry once whatever held the card lets go.

----

Safety Guards
-------------

The window never offers to overwrite the disk hosting the running OS. A
``system`` drive (``Drive::isSystem``, derived in the backend) disables Write and
shows a red **System** tag that says why. The guard is enforced in two places:
``applyState()`` keeps the Write button disabled, and ``startWrite()``
re-checks the same invariant (no system disk, an image selected, not busy)
before launching — the UI state and the action never diverge.
