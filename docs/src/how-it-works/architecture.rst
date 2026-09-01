.. _architecture:

Architecture Overview
=====================

.. description::

   The layered design of Kuiper Imager: thin front-ends over a portable core,
   with all platform code behind a single funnel

Kuiper Imager is built as four layers with dependencies pointing **inward
only** (the Clean Architecture dependency rule): the front-ends depend on the
core, and the core depends on neither front-end. Everything that differs
between operating systems is confined to one place, so the portable majority of
the tool — downloading, decompressing, parsing boot-file manifests, copying
files into a mounted partition — is written once and never forked per platform.

----

The Four Layers
---------------

.. code-block:: text

   Front-ends (thin, no business logic)
     kli (CLI)                 Kuiper Imager (GUI)
       arg parsing               Qt Widgets
       human-readable output     binds core callbacks to bars/tables
       exit codes
                 both link libkuiper directly
   ─────────────────────────────────────────────────────────────
   Core library  libkuiper  (UI-agnostic, Qt Core only)
     ImageService     DriveService        ConfigurationService
      (portable)      (PLATFORM FUNNEL)      (portable)
     list-releases    list-drives          list-projects
     fetch            flash / mount        configure
                      Result<T, Error> everywhere · progress via callbacks
   ──────────────────── only DriveService crosses this line ─────
   Platform abstraction  (one interface, three implementations)
     IDriveBackend — enumerate · raw I/O · mount · privilege
       Linux backend      Windows backend*     macOS backend*
                                        (*planned, Phase 4)

The top layer holds two interchangeable front-ends. ``kli`` is the
command-line tool; the Qt Widgets GUI is a second front-end that builds today
but is not yet wired up. Neither contains business logic — they parse input,
render progress, and translate results for a human.

The core, ``libkuiper``, links **Qt Core only** — never Qt Widgets or QML. Its
public types use ``std::string`` rather than ``QString`` so the API stays
portable and minimally Qt-coupled. Every operation is a service that returns a
typed :ref:`Result\<T, Error\> <core-library>` and reports progress through a
plain callback, so the core never depends on Qt's signal/slot machinery.

The three services map one-to-one onto the command groups:
:ref:`ImageService <releases-and-sources>` obtains images
(:ref:`fetch <fetch>`), :ref:`DriveService <core-library>` operates on physical
drives (:ref:`flash <flash>`), and
:ref:`ConfigurationService <configure>` prepares boot files
(:ref:`configure <configure>`). Only ``DriveService`` reaches below the line
into platform code.

----

The Funnel Invariant
--------------------

**DriveService is the sole gateway to platform-specific code.** ImageService
and ConfigurationService are 100% portable. This is the single invariant the
whole design turns on, because platform divergence is exactly four
drive-centric concerns — enumerate drives, raw-write a device, mount/unmount a
partition, and elevate privilege — while everything else behaves identically on
every OS.

Below DriveService sits one interface, :ref:`IDriveBackend
<platform-backends>`, with one implementation per operating system. A factory,
``makeDriveBackend()``, returns the backend for the host platform, and only
that platform's implementation is compiled and linked. The funnel is enforced
even for read-only topology lookups (for example, resolving a partition node to
its parent disk): those shell out to ``util-linux`` from inside the backend
rather than from the portable services, so no platform assumption ever leaks
upward.

The payoff is that porting to Windows or macOS (Phase 4) touches only
DriveService and its backends — nothing in ImageService or ConfigurationService
changes. See :ref:`platform-backends` for the interface and the Linux
implementation.

----

One Core, Two Front-Ends
------------------------

Both ``kli`` and the GUI link ``libkuiper`` and call the **same**
``DriveService`` in-process. There is no subprocess boundary and no
output-parsing contract between them: progress is a direct callback and errors
are typed values. Because both front-ends drive one core, they behave
identically by construction — a fix or a new capability in the core reaches
both at once.

This is the shared-in-process-core arrangement (the same shape Raspberry Pi
Imager uses). The CLI is kept deliberately thin — it adds nothing the GUI could
not also obtain from the core — which keeps a future front-end split cheap
should process isolation ever be wanted. Consistent with this, ``kli`` produces
:ref:`human-readable output only <cli-overview>`; there is no machine-readable
mode, because nothing in the current design consumes one.

Adding an operating system means adding a drive backend behind ``IDriveBackend``
and one branch in the factory; the front-ends and the portable services are
untouched.
