.. _installation:

Quick Setup
===========

.. description::

   What you need, how to install Kuiper Imager, and where to go next

This page gets Kuiper Imager onto your machine. Once it is installed, the
:ref:`GUI walkthrough <quick-start>` and the :ref:`CLI walkthrough
<cli-overview>` take you from a blank card to a booting board.

----

What you need
-------------

- An Analog Devices evaluation board on its carrier, or a Raspberry Pi with an
  ADI HAT — whatever you intend to boot.
- An **SD card** (or USB drive) large enough for the image. A *full* image is
  several GB; a *basic* image is smaller.
- An **SD card reader** on the computer running Kuiper Imager.
- For downloading **CI** images with :ref:`fetch`, a GitHub token (not needed
  for the stable channel, and not needed at all if you already have an image
  file). See the :ref:`CLI walkthrough <fetch>`.

Flashing and the Intel preloader write touch raw devices, so on Linux they run
under ``sudo`` (the GUI elevates only for the write itself). See the
:ref:`privilege model <platform-backends>`.

----

Install on Linux (Debian/Ubuntu)
--------------------------------

Kuiper Imager ships as **two** Debian packages, so you install only what you
need:

- **kuiper-imager** — the desktop GUI, the privileged write helper, the polkit
  policy, and a desktop entry. Install this for the graphical workflow.
- **kuiper-imager-cli** — just the ``kli`` command-line tool. It has no GUI or
  desktop dependencies, so it suits headless servers, CI runners, and scripted
  flashing.

Install the ``.deb`` files you obtained, letting ``apt`` pull in their
dependencies:

.. code-block:: bash

   sudo apt install ./kuiper-imager_*.deb        # the GUI (+ helper)
   sudo apt install ./kuiper-imager-cli_*.deb     # the kli CLI

On an x86 host the GUI package *recommends* ``qemu-user-static`` and
``binfmt-support``; these are only needed to :ref:`customize <customize>` an ARM
image on a non-ARM machine, and ``apt`` installs them by default.

To build the packages (or the tools) yourself, see :ref:`the dev container
<development>`.

----

Install on Windows
------------------

The Windows build is produced by :ref:`continuous integration <github-actions>`
as a self-contained folder (the executables plus their Qt and C-library DLLs)
that runs on a clean Windows host with nothing else installed. Unzip it and run
``kuiper-imager.exe`` for the GUI, or ``kli.exe`` from a terminal.

Flashing on Windows needs Administrator rights: the GUI prompts for elevation
(UAC) only at the moment it writes, and the CLI should be run from an elevated
prompt.

----

macOS
-----

macOS is planned but not yet implemented — the drive backend is a compilable
stub, so the tool builds but cannot image a card. See :ref:`platform-backends`
for the status.

----

Next steps
----------

- **Most users:** follow the :ref:`GUI walkthrough <quick-start>`.
- **Scripting or headless:** follow the :ref:`CLI walkthrough <cli-overview>`.
