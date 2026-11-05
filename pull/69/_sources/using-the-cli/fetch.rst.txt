.. _fetch:

Fetch: List and Download Releases
=================================

.. description::

   List available Kuiper images and download one with ``list-releases`` and
   ``fetch``

Getting an image is two commands: ``list-releases`` to see what's available and
copy an identifier, then ``fetch`` to download it. Both talk to the image
sources described in :ref:`releases-and-sources`.

----

list-releases
-------------

.. code-block:: bash

   kli list-releases [--unstable] [--branch <branch>] [--limit <n>]

Lists available images as a table. The channel selects the source:

- Without ``--unstable`` the **stable** channel is queried. It is not wired up
  yet and reports cleanly that no official source is available — use
  ``--unstable`` for now.
- ``--unstable`` lists **Kuiper 2.0 CI builds** from GitHub Actions.

Options for the unstable channel:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Option
     - Effect
   * - ``--branch <name>`` / ``-b``
     - Filter builds to one branch (server-side). Only valid with ``--unstable``.
   * - ``--limit <n>``
     - Number of recent successful runs to scan (default 10, max 100).

The output columns are ``IDENTIFIER``, ``BRANCH``, ``COMMIT``, ``VARIANT``,
``ARCH``, ``SIZE(GB)``, ``EXPIRES``, and ``STATUS``. **IDENTIFIER** (for example
``gh:9607528144``) is the copy-paste key you pass to ``fetch``; the branch /
commit / variant / arch columns are what you read to pick the right row. See
:ref:`releases-and-sources` for why the identifier is the artifact id rather than
a commit-based name.

Listing needs no token, though ``$GH_TOKEN`` / ``$GITHUB_TOKEN`` (or
``gh auth token``) raises the API rate limit if you list often.

----

fetch
-----

.. code-block:: bash

   kli fetch --image <id> --output <path> [--force]

Downloads the artifact named by ``--image`` (an identifier from
``list-releases``, such as ``gh:9607528144``) to ``--output``. The bytes are
saved **verbatim** — no decompression; the downloaded ``.zip`` is exactly what
:ref:`flash` will later decompress.

``--output`` may be either:

- a **file path** to write, or
- a **directory** (an existing directory, or a path ending in ``/``), in which
  case the artifact is saved under its own canonical name.

By default ``fetch`` refuses to overwrite an existing file; ``--force`` allows
it. The download **streams to a sibling ``.part`` file and is renamed into place
only on success**, so a failed, cancelled (Ctrl-C), or interrupted fetch never
leaves a partial file at the destination. A best-effort free-space check aborts
early with ``DiskFull`` once the total size is known, and when ``--output`` is a
directory the artifact name is reduced to a bare filename so an unexpected name
can never write outside it.

On success ``fetch`` prints the bytes written, the SHA-256 of the downloaded
file, and a ``Next:`` hint suggesting the matching ``flash`` command.

----

Tokens, Rate Limits, and Expiry
-------------------------------

Unlike listing, **downloading a CI artifact requires a GitHub token** — the
download endpoint is authenticated. The token needs the ``actions:read`` scope
and is discovered from ``$GH_TOKEN``, then ``$GITHUB_TOKEN``, then
``gh auth token`` (see :ref:`cli-overview`). Without one, ``fetch`` fails with
``PermissionDenied`` and a hint on how to set it.

CI artifacts are deleted by GitHub roughly 90 days after their run. An expired
artifact lists with ``STATUS`` ``expired`` and cannot be fetched; re-run
``list-releases --unstable`` for a current identifier. A 401/403 at download
time usually means a missing scope; a 404 usually means the artifact expired
between listing and fetching.
