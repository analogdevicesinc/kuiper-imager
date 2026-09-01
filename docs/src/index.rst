Kuiper Imager
=============

.. description::

   Cross-platform tool to fetch, flash, and configure ADI Kuiper Linux images
   for Analog Devices hardware — CLI-first, with a GUI on top

Kuiper Imager is a tool for preparing bootable `ADI Kuiper Linux
<https://github.com/analogdevicesinc/kuiper>`__ SD cards for Analog Devices
hardware. It consolidates a fragmented,
error-prone, multi-tool process into a single application with three core
operations:

**Fetch**
   List official and CI-built Kuiper releases and download the one you want.

**Flash**
   Write an image to an SD card or drive, with progress and read-back
   verification.

**Configure**
   Scan the boot partition and copy the correct boot files for your specific
   eval-board and carrier combination — the most error-prone manual step.

Preparing a Kuiper card has meant juggling several tools and a set of
easy-to-get-wrong manual steps. Kuiper Imager replaces that with one application
and a guided **Fetch → Flash → Configure** workflow: each command finishes by
suggesting the next, so a first-time user is walked from a blank card to a
booting board, while the steps stay independent for anyone who only needs one.

It is **CLI-first**: all the logic lives in a UI-agnostic core library that the
``kli`` command-line tool and the (planned) GUI both drive in-process, so the two
front-ends behave identically. See :ref:`architecture` for how that fits
together, or :ref:`quick-start` to get going.

----

Status
------

Kuiper Imager is in early development on the ``v2.0`` branch. What works today is
the ``kli`` CLI on **Linux**:

- ``list-drives`` — enumerate removable drives
- ``flash`` — write a local image (raw or compressed) with read-back verification
- ``list-projects`` / ``configure`` — set up boot files for your eval-board
- ``list-releases --unstable`` / ``fetch`` — discover and download CI images

The GUI currently lists drives only. Windows and macOS drive backends, the GUI
wire-up, and the stable (non-CI) release source are postponed but have their
seams in place — see the :ref:`roadmap`.

----

Documentation
=============

.. toctree::
   :maxdepth: 2
   :caption: Getting Started

   getting-started/installation
   getting-started/quick-start

.. toctree::
   :maxdepth: 2
   :caption: Using the CLI

   using-the-cli/cli-overview
   using-the-cli/fetch
   using-the-cli/flash
   using-the-cli/configure

.. toctree::
   :maxdepth: 2
   :caption: How It Works

   how-it-works/architecture
   how-it-works/core-library
   how-it-works/flash-pipeline
   how-it-works/platform-backends
   how-it-works/releases-and-sources

.. toctree::
   :maxdepth: 2
   :caption: Development

   development/development
   development/contributing
   development/roadmap

.. toctree::
   :maxdepth: 2
   :caption: Reference

   reference/troubleshooting
