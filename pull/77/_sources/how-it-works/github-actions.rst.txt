.. _github-actions:

GitHub Actions CI
=================

.. description::

   The CI workflows: the cross-platform build-and-package pipeline, and the
   documentation site deploy

Kuiper Imager's continuous integration lives in GitHub Actions workflows under
``.github/workflows/``. Dev CI (``ci.yml``) builds, smoke-tests, and packages the
tool across platforms on every push and pull request against ``main``; a separate
workflow (``gh-pages.yml``) builds and publishes this documentation.

``ci.yml`` is only a thin trigger: it delegates the actual work to a **reusable
workflow**, ``_build.yml``, so the same build steps can be driven from a release
pipeline without being copy-pasted. The jobs below live in ``_build.yml``.

----

ci.yml / _build.yml — build and package
---------------------------------------

The build pipeline compiles the tool, proves each build actually runs, and
produces installable artifacts, in three jobs.

The ``build-linux`` and ``build-windows`` jobs (compile + smoke)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

One job per platform — **Linux** (``ubuntu-24.04``, preset ``ci-linux``) and
**Windows** (``windows-latest``, preset ``ci-windows``) — so no step needs an OS
guard. Qt 6.8.2 is installed
via ``install-qt-action``; Windows additionally wires up MSVC (``msvc-dev-cmd``)
and Ninja, and the C libraries (libarchive, curl, openssl) come from **vcpkg**
manifest mode through the ``ci-windows`` preset's toolchain. Each platform
configures and builds with its preset, then smoke-tests:

- **Linux** runs ``kli version``.
- **Windows** stages a **self-contained bundle** — the three executables plus
  the vcpkg C-library DLLs and, via ``windeployqt``, the Qt DLLs, the platform
  plugin, and the MSVC runtime — then runs ``kli version`` and ``kli
  list-drives`` **with ``PATH`` stripped to the system directories**. Running
  with a bare ``PATH`` proves every dependency resolves from the bundle itself,
  and ``list-drives`` exercises the real SetupAPI enumeration on the runner.

The unpackaged Linux binaries and the Windows bundle are uploaded as artifacts
(retained 14 days). ``build-linux`` serves dev CI only — a release caller skips
it (``run-linux-build: false``), since ``package-deb`` already builds and tests
Linux and the ``.deb`` is the shippable artifact. There is no macOS row yet — the
macOS :ref:`backend <platform-backends>` is a stub.

The ``package-deb`` job (Debian packages)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A matrix builds the Debian packages for **amd64** and **arm64**, each
inside a pinned ``debian:12`` container so they link the Qt the target users
have. It installs the packaging toolchain and the build-deps from
``debian/control`` (``mk-build-deps``), builds with ``dpkg-buildpackage -us -uc
-b``, then gates quality before publishing:

- ``desktop-file-validate`` checks the desktop entry.
- ``lintian --fail-on error`` lints the built ``.deb`` files.
- both packages are **installed** (dependencies only, no Recommends) and
  ``kli version`` is run, proving the packaged CLI works from a clean install.

The ``.deb`` files are uploaded per architecture (excluding ``-dbgsym``
packages). See :ref:`the dev container <development>` for building the same
packages locally.

----

gh-pages.yml — the documentation site
--------------------------------------

``gh-pages.yml`` builds and deploys this Sphinx site. It triggers only on
changes under ``docs/**`` (on ``main`` and ``v2.0``, plus pull requests). The
``build-doc`` job installs ``docs/requirements.txt`` and runs:

.. code-block:: bash

   make html SPHINXOPTS='-W --keep-going'

The ``-W`` makes **every warning an error**, so a broken cross-reference, an
orphaned page, or a stray ``.. todo::`` fails the build — which is why the docs
keep no unresolved references. ``--keep-going`` collects *all* warnings in one
run rather than stopping at the first. The generated HTML is then published by
the shared ``analogdevicesinc/doctools/gh-pages-deploy`` action on ``main`` (and
built, but not deployed, for pull requests so a doc change is checked before
merge).

----

Relationship to the image sources
---------------------------------

These workflows build the **imager itself**. They are distinct from the workflow
that builds the **Kuiper images** the tool downloads: the ``--unstable`` channel
of :ref:`list-releases <list-releases>` reads CI artifacts from the
``kuiper2_0-build.yml`` workflow in the separate ADI ``kuiper`` repository. See
:ref:`releases-and-sources` for how that channel works.
