.. _releases-and-sources:

Releases and Sources
====================

.. description::

   Where images come from: the ReleaseProvider seam, the GitHub Actions
   provider, and the HTTP client's behavior

``ImageService`` is the portable image-source facade behind ``list-releases``
and ``fetch``. It holds no device or platform code: it dispatches to a
per-channel ``ReleaseProvider`` for discovery and resolution, and streams the
bytes through an injected HTTP client. This page covers the provider seam, the
two channels, the GitHub Actions provider, and the HTTP behaviors that make a
fetch trustworthy.

----

The ReleaseProvider Seam
------------------------

A ``ReleaseProvider`` is one source of images — the same "one facade, swappable
implementations, fake-testable" shape as :ref:`IDriveBackend
<platform-backends>`. It has just two operations:

- ``list(query)`` — return the available :ref:`Release <core-library>` rows for
  a query.
- ``resolve(id)`` — turn a ``Release::id`` into a ``DownloadSource`` (a URL, the
  headers the source needs, and the artifact's canonical filename). It may make
  one lightweight metadata request to read the name and confirm the artifact
  hasn't expired.

``ImageService`` picks a provider by the query's channel (and, for ``fetch``, by
the identifier's scheme prefix). Adding a new source — a hosted OS list, a
mirror — is a new ``ReleaseProvider`` implementation with **no CLI change**: the
commands, output format, and identifier plumbing are already source-agnostic.

----

Channels: stable and unstable
-----------------------------

``list-releases`` takes a channel:

- **stable** (the default) — published Kuiper releases from
  ``packages.analog.com``, served by ``StableReleaseProvider`` from a hosted JSON
  manifest (cached on disk, with a compiled-in seed as the offline fallback). No
  token is involved; each image's download URL and expected SHA-256 come from the
  manifest.
- **unstable** — Kuiper 2.0 CI images, built by the ``kuiper2_0-build.yml``
  GitHub Actions workflow in the ADI ``kuiper`` repository.

An unknown channel is rejected with the list of valid ones.

----

The GitHub Actions Provider
---------------------------

The unstable channel is served by ``GitHubActionsProvider``, which reads CI
artifacts through the GitHub REST API.

**Listing.** It scans recent *successful* runs of the workflow (branch-filtered
server-side when ``--branch`` is given) and emits one ``Release`` per ``*_image``
artifact — ``*_meta`` and other artifacts are skipped. Each artifact's name is
parsed by ``variantOf``: ``kuiper_full_64_image`` becomes variant ``full``,
arch ``arm64`` (the build encodes the ARM word size as ``32``/``64``, surfaced
as ``arm32``/``arm64``); an unexpected shape falls back to the raw name with an
empty arch.

**The identifier.** A release's ``id`` is ``gh:<artifactId>`` — the GitHub
artifact id, deliberately **not** a composed ``variant+sha``. The same commit is
built by more than one successful run (a re-run, or ``push`` + ``pull_request``
on the same SHA), so ``variant+sha`` is not unique; only the artifact id is. The
``gh:`` scheme prefix also lets ``fetch`` route to the right provider once the
stable channel exists. Humans recognize a row by its branch / commit / variant /
arch columns; ``id`` is the copy-paste key for ``fetch``.

**Cost and auth.** Listing costs ``1 + N`` API requests: one for the run list,
then one per run for its artifacts (``N`` is the clamped ``--limit``, at most
100). Listing needs no authentication, but this spends the unauthenticated rate
budget quickly — hence the optional token, which only raises the rate limit for
listing. The **download** endpoint, by contrast, is authenticated, so
``resolve()`` (and therefore ``fetch``) *requires* a token with the
``actions:read`` scope. Every API request carries a ``User-Agent`` (GitHub
rejects requests without one), the ``Accept`` and API-version headers, and the
bearer token when set. CI artifacts are deleted after ~90 days; ``resolve()``
checks the ``expired`` flag and fails with a clear "re-run list-releases"
message rather than handing back a dead URL.

----

The Stable Provider
-------------------

The stable channel is served by ``StableReleaseProvider`` from a **hosted JSON
manifest** at
``https://packages.analog.com/public/raw/files/kuiper_releases.json`` — one entry
per published ``(version, variant, arch)``. A new release reaches every imager by
editing that one file; no imager update is needed. A patch may ship fewer
variants than its base release (``2026.1.1`` is 64-bit only), so the manifest
lists only what exists.

The Manifest Format
~~~~~~~~~~~~~~~~~~~

.. code-block:: json

   {
     "schemaVersion": 1,
     "generated": "2026-11-19T00:00:00Z",
     "baseUrl": "https://packages.analog.com/public/raw/files",
     "releases": [
       {
         "version": "2026.1.1", "variant": "full", "arch": "arm64",
         "file": "kuiper_full_64_image_2026.1.1.zip",
         "sizeBytes": 7700000000, "sha256": "…", "recommended": true
       }
     ]
   }

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Field
     - Meaning
   * - ``schemaVersion``
     - The format version (currently ``1``). An imager that does not recognise it
       rejects the manifest and falls back, so the shape can evolve safely.
   * - ``baseUrl``
     - The directory every image lives under. Factoring it out keeps the file
       naming convention in the manifest, not compiled into the imager — a mirror
       or host move is a one-line edit.
   * - ``releases[]``
     - One entry per image: ``version`` / ``variant`` / ``arch`` (which form the
       identifier), the ``file`` name (joined to ``baseUrl`` for the download
       URL), optional ``sizeBytes`` and ``sha256``, and the ``recommended`` flag
       that marks an image as a curated default for its architecture.

The file is authored **recommended-first**, and ``list()`` preserves document
order, so the recommended rows lead the listing. The release pipeline fills
``sha256`` (from ``sha256sum`` of each image) and ``sizeBytes``; an empty or
absent ``sha256`` simply means that image is not verified.

Caching and the Offline Seed
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``list()`` and ``resolve()`` both resolve the catalog through four steps, in
order, stopping at the first that yields a usable manifest:

#. a **fresh on-disk cache** — served with no network, within a ten-minute
   window keyed off the cache file's modification time;
#. else a **live GET** of the manifest — cached atomically on success (write to a
   ``.part`` sibling, then rename);
#. else **any cached copy**, even if stale;
#. else the **compiled-in seed** — a minimal manifest embedded via
   ``kuiper_assets.qrc`` that lists the releases known at build time.

The seed guarantees the stable channel is never empty just because the network
is down. A body is accepted at any step only if it parses, declares a known
``schemaVersion``, and has at least one usable entry — so a 404 page or a
truncated download can never poison the cache. The refresh is a plain
unconditional GET: the manifest is only a few KB, and a conditional GET
(``ETag`` / ``If-Modified-Since``) would need response headers the
``IHttpClient`` seam does not expose. The cache lives in a shared,
app-name-independent per-user cache directory (``GenericCacheLocation`` +
``/kuiper-imager``) so the CLI and GUI reuse one copy.

Identifier and Verification
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A release's ``id`` is ``adi:<version>/<variant>/<arch>`` (e.g.
``adi:2026.1.1/full/arm64``). Unlike the unstable channel — where one commit
yields several runs, forcing an opaque artifact id — this triple is unique, so
the identifier is also human-readable. ``resolve()`` finds the matching entry and
returns its download URL, its canonical filename (the last path segment of the
URL), and its ``sha256`` as the expected hash. That hash is what :ref:`fetch
<fetch>` checks the downloaded bytes against before saving them.

----

The HTTP Client
---------------

HTTP lives behind ``IHttpClient``, a two-method seam injected into
``ImageService`` and its providers (fake-testable, like every other seam):

- ``get()`` buffers a small body — the JSON API responses for ``list-releases``.
- ``download()`` streams an arbitrarily large body to a sink chunk by chunk,
  without buffering — the multi-GB ``fetch``. A sink or progress-callback abort
  surfaces as ``UserCancelled``; a transport failure as ``NetworkFailure``.

The default implementation wraps **libcurl**, with a few deliberate behaviors:

- **Authorization is never resent across a cross-host redirect.** GitHub
  redirects the artifact URL from ``api.github.com`` to blob storage; the bearer
  token must not follow it. libcurl's default already restricts this, and the
  client sets it explicitly.
- **No overall timeout on downloads.** A multi-GB image legitimately runs for
  minutes, so instead of a wall-clock cap the client uses a low-speed abort:
  if throughput stays under 1 KiB/s for 30 s the transfer fails, catching a dead
  connection without penalizing a slow-but-live one. (The buffered ``get()`` for
  API calls *does* use a normal 60 s timeout.)
- **No content-encoding is advertised on downloads.** The client wants the bytes
  on the wire to be exactly the file's bytes, so the progress total and the
  SHA-256 match the output file precisely; transparent gzip would break both.
- **libcurl's global init runs exactly once.** That init is not thread-safe, so a
  single static instance owns the init/cleanup lifecycle (relying on C++'s
  thread-safe function-local static init); individual clients are stateless and
  copy-free.

HTTP status handling is split by purpose. The API path leaves status inspection
to the caller (so a 4xx JSON error can be mapped to a typed ``Error``), while the
download path lets a ``>= 400`` become an error so a failure body is never
streamed into the output file. ``ImageService`` then maps the final status:
401/403 → ``PermissionDenied`` (token scope), 404 → ``NotFound`` (likely
expired), other non-2xx → ``NetworkFailure``. The fetch pipeline itself — atomic
``.part`` rename, the free-space guard, and path-traversal safety — is described
in :ref:`fetch`.

----

Resolving a Path Without Fetching
---------------------------------

``ImageService::artifactPath(id, outputDir)`` composes the exact path
``fetch(id, outputDir)`` *would* write to — without downloading anything. It
routes to the same provider ``fetch`` would, reads the artifact's canonical
filename from ``resolve()``, and joins it to ``outputDir`` exactly as the
into-a-directory branch of ``fetch`` does. For the stable channel this is fully
offline; for unstable it may cost the one light metadata request ``resolve()``
already makes.

It deliberately does **not** check whether the file exists — it reports a
*location*, and the caller tests that path. This is what lets the
:ref:`GUI <gui-console>` maintain its download cache: it asks where each release
would live, then detects, reuses, or deletes the file there.
