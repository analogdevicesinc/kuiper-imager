.. _installation:

Installation
============

.. description::

   Get Kuiper Imager: build ``kli`` and the GUI from source, or install the
   Debian package

Kuiper Imager is in early development on the ``v2.0`` branch. Today it is
obtained by building from source on Linux — through the provided Docker
environment (recommended) or natively — or by installing the Debian package.
Windows and macOS builds are planned (see :ref:`roadmap`).

----

Prerequisites
-------------

Building from source requires:

- **Qt 6.8+** — linked dynamically (LGPLv3). The ``.deb`` build instead targets
  the distro's Qt (6.4+).
- **CMake 3.22+** and a **C++23 compiler** (GCC 12+), for real
  ``std::expected`` — the basis of the :ref:`Result type <core-library>`.
- **libarchive** — image decompression (gzip/xz/zstd/bzip2 + zip).
- **OpenSSL (libcrypto)** — hardware-accelerated SHA-256.
- **libcurl** — HTTP for ``list-releases`` and ``fetch``.
- **util-linux** (``lsblk``, ``findmnt``) at runtime for drive enumeration.

The Docker image bundles all of these, so the Docker flow needs only Docker
itself.

----

Build from Source (Docker)
--------------------------

Development and the Linux build happen in a Docker container (Ubuntu + Qt 6.8 +
toolchain). Build the image once, then get a dev shell:

.. code-block:: bash

   docker compose build          # build the dev image
   docker compose run --rm dev   # open a shell in the container

Inside the container, configure with a :ref:`CMake preset <development>` and
build:

.. code-block:: bash

   cmake --preset dev
   cmake --build build
   ./build/src/cli/kli version
   ./build/src/cli/kli list-drives

The ``dev`` preset builds both ``kli`` and the GUI. For a headless build of just
the CLI, use ``cli-only``:

.. code-block:: bash

   cmake --preset cli-only && cmake --build build-cli

----

Build from Source (Native)
--------------------------

With the prerequisites installed on the host, the same presets work without
Docker:

.. code-block:: bash

   cmake --preset dev && cmake --build build

The GUI is gated by the ``KUIPER_IMAGER_GUI`` CMake option (``ON`` in ``dev``,
``OFF`` in ``cli-only``), so a headless or CI machine without Qt Widgets can
still build the CLI. See :ref:`development` for all presets.

----

Debian Packages
---------------

The Debian packaging lives in the native ``debian/`` tree (debhelper driving the
CMake build) and produces **two** binary packages from a single build against the
**distro's** Qt (6.4+):

- ``kuiper-imager-cli`` — just ``kli``. Depends only on the distro's Qt Core, plus
  libarchive, OpenSSL, and libcurl — no GUI or desktop stack, so it suits headless
  servers, CI runners, and scripted flashing.
- ``kuiper-imager`` — the Qt Widgets GUI, the privileged write helper, the polkit
  policy, and a desktop entry + icon. Depends additionally on Qt Widgets/Network
  and ``pkexec``.

Build both with ``dpkg-buildpackage`` (ideally inside the ``debian:12`` container
so the packages link the same Qt the target users have):

.. code-block:: bash

   # install build-deps from debian/control, then build binary-only, unsigned
   mk-build-deps --install --remove --tool 'apt-get -y' debian/control
   dpkg-buildpackage -us -uc -b
   # kuiper-imager-cli_*.deb and kuiper-imager_*.deb land in the parent directory

Qt is linked dynamically against the distribution's packages (posture A): nothing
from Qt is bundled, so the ``.deb`` carries no LGPL source-availability obligation.
To reproduce the packaged build locally without ``dpkg-buildpackage``, the
``deb-linux`` CMake preset configures the same CLI + GUI + helper build.
