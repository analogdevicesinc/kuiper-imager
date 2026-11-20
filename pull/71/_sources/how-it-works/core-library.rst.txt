.. _core-library:

Core Library (libkuiper)
========================

.. description::

   The UI-agnostic core: the error model, the three services, the drive domain
   model, and the seams that keep it portable

``libkuiper`` is the heart of Kuiper Imager: a UI-agnostic library that links
Qt Core only and holds all the business logic both front-ends share. This page
covers its error model, the three services, the drive domain model, the layout
identification rules, progress and cancellation, and the injection seams that
keep most of it portable.

----

Error Model: Result<T, Error>
-----------------------------

Every fallible operation returns ``Result<T, Error>`` — an alias for C++23's
``std::expected<T, Error>``. There are no exceptions across the API and no
string-only errors: a call either yields its value or a structured ``Error``.

.. code-block:: cpp

   enum class ErrorCode {
       NetworkFailure, DiskFull, PermissionDenied, InvalidImage,
       HashMismatch, DeviceRemoved, DeviceBusy, UnsupportedPlatform,
       NotKuiper2, NotFound, UserCancelled, Unknown,
   };

   struct Error {
       ErrorCode                  code = ErrorCode::Unknown;
       std::string                message;             // human-readable
       std::string                details;             // context (path, url, ...)
       std::optional<std::string> recoverySuggestion;  // hint, when we have one
   };

The ``ErrorCode`` vocabulary is deliberately small and stable, because the CLI
maps each code onto a fixed process :ref:`exit code <cli-overview>` that scripts
can branch on. The optional ``recoverySuggestion`` is where an error carries a
concrete next step (for example, "set ``$GH_TOKEN``" or "run with ``sudo``").

----

The Three Services
------------------

.. list-table::
   :header-rows: 1
   :widths: 22 30 20 28

   * - Service
     - Commands
     - Portable?
     - Responsibility
   * - ``ImageService``
     - ``list-releases``, ``fetch``
     - Yes
     - Obtain an image artifact (see :ref:`releases-and-sources`)
   * - ``DriveService``
     - ``list-drives``, ``flash`` (+ ``mount`` / preloader write)
     - No — the platform funnel
     - Operate on a physical drive and its partitions
   * - ``ConfigurationService``
     - ``list-projects``, ``configure``
     - Yes
     - Configure the Kuiper install on a card

Each service is cohesive — one clear responsibility — and only
``DriveService`` is platform-specific. ``DriveService::flash()`` writes a local
image (raw or ``.zip``/``.xz``/``.zst``/``.gz``) to a whole-drive node and
verifies it by read-back; the mechanics are in :ref:`flash-pipeline`.

``ConfigurationService`` is a port of the ADI ``configure-setup.sh`` script and
is **path-based by design**: it operates on an already-mounted BOOT partition
and holds no ``DriveService`` reference at all. Its parsing and file-copy logic
is plain ``std::filesystem`` plus Qt JSON, identical on every OS. The one
platform operation it needs — the Intel preloader's raw partition write — is
supplied through an injected ``PreloaderSink`` callback wired to
``DriveService::writePreloader`` by the front-end. This keeps the service
portable while still funnelling the one device write through DriveService. See
:ref:`configure` for the user-facing behavior.

----

Domain Model: Drive and Partition
---------------------------------

A ``Drive`` is one whole removable medium (``/dev/sda``, ``/dev/mmcblk0``) — the
single identifier every command takes. It carries its own ``Partition`` list,
filled by one ``listDrives()`` enumeration; the user never names a partition.

.. code-block:: cpp

   struct Partition {         // pure enumeration facts — no roles, no composed names
       std::string   node;    // "/dev/mmcblk0p1" — verbatim from the backend
       int           number;  // trailing integer of the node name (display hint only)
       std::uint64_t sizeBytes;
       std::string   fsType;  // "vfat" | "ext4" | "" (unformatted = preloader)
       std::string   label;   // a hint — NOT unique across cards
       std::string   mountpoint;   // "" if not mounted
   };

Two rules in this struct are load-bearing. First, ``node`` is taken **verbatim**
from platform enumeration and never composed by string surgery. Second,
``label`` is only a hint and is *not* unique across cards. Both exist to fix a
real bug: with two Kuiper cards inserted, udev disambiguates the colliding
filesystem labels (``BOOT``/``BOOT1``, ``rootfs``/``rootfs1``), so a label no
longer identifies which physical card is which — and the old
``${disk}p3``-style composed node could point at the wrong medium entirely.
Anchoring on the whole-drive node the user supplied, and reading partitions back
from that drive, sidesteps the collision.

----

Layout Identification
---------------------

Partition **roles are assigned by inspection, never by index**. ``Layout`` is a
pure, platform-free function that classifies a ``Drive``'s partitions into the
three a Kuiper card can carry:

.. code-block:: cpp

   struct KuiperLayout {
       std::optional<Partition> boot;        // vfat, holds the manifests
       std::optional<Partition> root;        // ext4 rootfs
       std::optional<Partition> bootloader;  // unformatted, small (Intel only)
   };
   Result<KuiperLayout> identifyLayout(const Drive&);

The ranked rules are:

- **boot** — a ``vfat`` partition, preferring ``label`` containing ``BOOT``
  (case-insensitive), else the first ``vfat``.
- **root** — an ``ext4`` partition, preferring ``label`` containing ``rootfs``.
- **bootloader** — unformatted *and* unmounted; the smallest if several (a
  large unformatted area is data, not the tiny preloader slot). Optional —
  Raspberry Pi and other non-Intel cards have none.

Label matching is a case-insensitive **substring** test, not equality, exactly
because tool- and image-dependent casing (``BOOT`` vs ``boot``) and udev's
collision suffix (``BOOT1``) would otherwise miss. Identification works from the
enumerated facts of the partitions on the *named* drive, so colliding labels
across two cards can never redirect it to the wrong medium. The function is pure
and returns copies, so a ``KuiperLayout`` never dangles into the ``Drive`` it
came from.

----

Progress and Cancellation
-------------------------

Long-running operations report progress through a callback and poll a
cancellation token between chunks, so the core stays free of Qt signals:

.. code-block:: cpp

   struct Progress {
       enum class Phase { Preparing, Downloading, Writing,
                          Finalizing, Verifying, Configuring, Done };
       Phase         phase;
       std::uint64_t bytesDone, bytesTotal;  // bytesTotal 0 if unknown
       double        fraction;               // [0,1], monotonic within a phase
       std::string   message;
   };
   using ProgressFn = std::function<void(const Progress&)>;
   struct CancelToken { std::function<bool()> cancelled; };  // polled between chunks

``bytesTotal`` is ``0`` when the total is not yet known, and ``fraction`` never
moves backwards within a phase. Cancellation is cooperative: when the token asks
to stop, the operation aborts cleanly and returns ``UserCancelled`` — and for a
flash, the device is left non-bootable by construction (see the
:ref:`defer-head model <flash-pipeline>`). Front-ends turn these reports into a
progress bar; an empty callback or token means "no reporting" / "never
cancelled".

----

The Seams
---------

Services take their dependencies by injection, so each is unit-testable with a
fake — the same "one facade, swappable implementations" shape throughout:

- **IDriveBackend** — the platform seam behind ``DriveService`` (see
  :ref:`platform-backends`).
- **ReleaseProvider** — the per-source seam behind ``ImageService`` (see
  :ref:`releases-and-sources`).
- **IHttpClient** — the HTTP seam. ``get()`` buffers a small body (the JSON API
  responses for ``list-releases``); ``download()`` streams an arbitrarily large
  body to a sink without buffering (the multi-GB ``fetch``), reporting progress
  and honoring cancellation. A sink or progress abort surfaces as
  ``UserCancelled``, a transport failure as ``NetworkFailure``.
- **PreloaderSink** — the one-call callback through which
  ``ConfigurationService`` reaches the Intel preloader write without holding a
  drive reference.

Hashing runs through a small header-only ``Sha256`` wrapper over OpenSSL's EVP
interface, shared by both the flash path (``DriveService``) and the fetch path
(``ImageService``). Using ``libcrypto`` rather than ``QCryptographicHash`` is a
deliberate performance choice: libcrypto uses the CPU's SHA extensions where
present — roughly an order of magnitude faster than Qt's software hash — which
turns hashing from a bottleneck into noise on the flash path. The digest is
computed inline while writing; see :ref:`flash-pipeline`.
