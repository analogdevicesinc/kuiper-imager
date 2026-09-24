.. _roadmap:

Roadmap
=======

.. description::

   What is built today and what is postponed, phase by phase

Kuiper Imager is developed in phases. The core and the full Linux CLI workflow
are done; the additional platform backends and the GUI are deliberately
postponed but already have their seams in place, so filling them in touches
nothing above the :ref:`funnel <architecture>`.

----

Phase Status
------------

.. list-table::
   :header-rows: 1
   :widths: 14 44 42

   * - Phase
     - Scope
     - Status
   * - 1
     - Core library + Linux flash pipeline
     - **Done**
   * - 2
     - ``configure`` (boot-file setup, Intel preloader)
     - **Done**
   * - 3a
     - ``list-releases`` (image discovery)
     - **Done**
   * - 3b
     - ``fetch`` (download to file)
     - **Done**
   * - 4
     - Windows and macOS drive backends
     - Postponed — compilable stubs (see :ref:`platform-backends`)
   * - 5
     - GUI wire-up and image customization
     - Postponed

What works today: the ``kli`` CLI on **Linux** — ``list-drives``, ``flash`` of a
local image, ``list-projects`` / ``configure``, ``list-releases --unstable``, and
``fetch``. The GUI builds but currently lists drives only.

----

Deferred Design Pieces
----------------------

A few designed-but-not-built pieces are held for later phases:

- **Streaming flash** — a download-straight-to-device seam (``ImageReader``) and
  a blob cache, so ``fetch`` and ``flash`` can fuse without an intermediate
  file. The current flash writes from a local file only.
- **The stable release source** — the official (non-CI) source for
  ``list-releases`` has not been finalized (GitHub Releases? a hosted image
  list?), so the stable channel reports cleanly as not-yet-available. See
  :ref:`releases-and-sources`.
- **The GUI privilege model** — the GUI will elevate through a privileged helper
  rather than taking a hard dependency on ``udisks2`` / D-Bus, consistent with
  the CLI's :ref:`privilege approach <platform-backends>`.
- **A lighter verify tier** — an optional sampled read-back for users who want
  faster-than-full verification without dropping to none.
