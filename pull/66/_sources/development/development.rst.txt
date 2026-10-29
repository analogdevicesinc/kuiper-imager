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

Testing
-------

There is no automated test suite yet — a ``ctest`` target is a planned addition.
The CI pipeline's smoke test is ``kli version``, which confirms the binary links
and runs. When contributing, verify changes manually against the relevant
command and note that in your commit (see :ref:`contributing`).
