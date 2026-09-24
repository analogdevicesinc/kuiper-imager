.. _development:

Development
===========

.. description::

   Set up a development environment, understand the project layout, and build
   the CLI, GUI, and packages

Development and the Linux build happen in Docker; the (planned) Windows and macOS
executables come from native CI runners rather than this container. This page
covers the repository layout, the dev environment, and the build presets.

----

Project Layout
--------------

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Path
     - Contents
   * - ``src/core``
     - ``libkuiper`` — the UI-agnostic core (Qt Core only): services, domain
       model, platform backends. The single source of truth both front-ends use.
       See :ref:`core-library`.
   * - ``src/cli``
     - ``kli`` — the command-line front-end (thin).
   * - ``src/gui``
     - ``kuiper-imager`` — the Qt Widgets GUI front-end (thin; early skeleton).
   * - ``tests/``
     - The doctest suite: in-memory fakes, portable unit tests, and the root-only
       Linux loopback integration test. See :ref:`testing` below.
   * - ``docker/``
     - The ``Dockerfile`` for the dev + Linux-build environment.
   * - ``docs/``
     - This Sphinx documentation.

Both front-ends link ``libkuiper`` and call the same ``DriveService``
in-process, so they behave identically by construction — see :ref:`architecture`.

----

Dev Environment and Presets
---------------------------

Build the dev image once, then work inside a container shell:

.. code-block:: bash

   docker compose build
   docker compose run --rm dev

The build is driven by CMake presets:

.. list-table::
   :header-rows: 1
   :widths: 18 22 60

   * - Preset
     - Build dir
     - Purpose
   * - ``dev``
     - ``build``
     - Ninja, Debug, GUI **on** — the default dev build.
   * - ``cli-only``
     - ``build-cli``
     - Ninja, Debug, GUI **off** — headless / CLI-only.
   * - ``deb-linux``
     - ``build-deb``
     - Release CLI against the distro Qt (6.4+) with CPack DEB packaging (see
       :ref:`installation`).
   * - ``ci-linux`` / ``ci-macos`` / ``ci-windows``
     - ``build-ci``
     - Release CI builds. Only ``ci-linux`` is exercised today; the macOS and
       Windows rows wait on the Phase 4 :ref:`backends <platform-backends>`.

Configure and build with, for example, ``cmake --preset dev && cmake --build
build``. The ``KUIPER_IMAGER_GUI`` option (set by the preset) toggles the GUI so
CI and headless machines can build the CLI without Qt Widgets.

To run the GUI from the container over X11, allow local connections once per
session and launch it:

.. code-block:: bash

   xhost +local:
   docker compose run --rm dev ./build/src/gui/kuiper-imager

----

.. _testing:

Testing
-------

The suite is built on `doctest <https://github.com/doctest/doctest>`_, pulled in
by ``FetchContent`` (pinned) only when ``KUIPER_IMAGER_TESTS`` is on — so it is
hermetic, needs no system package, and never touches the ``deb-linux`` build. The
``dev`` and ``ci-*`` presets enable it; ``cli-only`` and ``deb-linux`` do not.
Everything lives under ``tests/``.

**Why it is testable.** Every dependency that touches the outside world sits
behind an interface the core already injects — ``IDriveBackend`` and its
``IRawDevice`` handle for devices, ``IHttpClient`` for the network. The suite
supplies in-memory fakes for each, so the real services run end to end with no
hardware, no root, and no network:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Fake
     - Stands in for
   * - ``FakeRawDevice``
     - An open device: a byte vector with a cursor. Records a write log (to assert
       the :ref:`defer-head ordering <flash>`) and can corrupt an offset to force a
       verify mismatch.
   * - ``FakeDriveBackend``
     - The platform backend: a scripted drive list and capabilities, handing out a
       ``FakeRawDevice``.
   * - ``FakeHttpClient``
     - The HTTP client: canned metadata and artifact bytes for the fetch path.

**Two suites, two audiences:**

.. list-table::
   :header-rows: 1
   :widths: 22 20 58

   * - Target
     - Where it runs
     - Covers
   * - ``kuiper_tests``
     - Everywhere, no root
     - The write-target guard, the ``flash`` pipeline (bytes, size guard,
       verify-mismatch, defer-head ordering, cancellation, ``--no-verify``),
       capabilities, layout identification, ``ConfigurationService``, and
       ``ImageService``.
   * - ``kuiper_linux_it``
     - Linux, root only
     - The real ``LinuxDriveBackend`` / ``LinuxRawDevice`` flashing a loop device
       and reading it back — the O_DIRECT path against a real fd. Labelled
       ``integration`` so the default run skips it.

Run the portable suite with ``ctest --preset dev`` (or ``ctest --test-dir
build``). The integration test needs a loop device, so run it as root and select
it by label:

.. code-block:: bash

   sudo ctest --test-dir build -L integration --output-on-failure

CI runs both: the portable suite on every build, then the loopback test in a
separate root step. The ``kli version`` smoke test still guards that the binary
links and runs.
