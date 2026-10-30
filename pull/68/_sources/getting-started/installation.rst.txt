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

Debian Package
--------------

A ``.deb`` of ``kli`` is produced by the ``deb-linux`` preset, which builds a
release CLI against the **distro's** Qt (6.4+) with CPack DEB packaging enabled
(distinct from the CI build, which uses a newer Qt):

.. code-block:: bash

   cmake --preset deb-linux
   cmake --build build-deb
   cd build-deb && cpack

The resulting package installs ``kli`` and depends on the distro's Qt Core,
libarchive, OpenSSL, libcurl, and util-linux. The GUI is not part of the
package.
