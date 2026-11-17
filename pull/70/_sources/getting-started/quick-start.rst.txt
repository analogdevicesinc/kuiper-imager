.. _quick-start:

Quick Start
===========

.. description::

   Go from nothing to a bootable Kuiper SD card with the guided
   fetch → flash → configure workflow

Kuiper Imager turns a blank SD card into a bootable Analog Devices system in
three steps. The tool *suggests* this order — each command ends with a ``Next:``
hint toward the following one — but the steps are independent, not enforced: you
can flash an image you already have, or reconfigure a card without reflashing.

.. code-block:: text

   Fetch ─▶ Flash ─▶ Configure ─▶ Bootable SD card
            ▲                       (Raspberry Pi: skip Configure)
      Local image ┘

Everything below uses the ``kli`` CLI on Linux. ``flash`` and ``configure`` write
to raw devices, so they run under ``sudo``. See :ref:`cli-overview` for the
conventions shared by every command.

----

1. Fetch an image
-----------------

List the available Kuiper 2.0 CI builds, then download one by its identifier:

.. code-block:: bash

   kli list-releases --unstable
   kli fetch --image gh:9607528144 --output ./kuiper.zip

Copy the ``IDENTIFIER`` of the row you want into ``--image``. Downloading needs a
GitHub token — see :ref:`fetch`. Already have an image file? Skip straight to
step 2.

----

2. Flash it to a drive
----------------------

Identify the target drive, then flash the image onto it:

.. code-block:: bash

   kli list-drives
   sudo kli flash --image ./kuiper.zip --drive /dev/sda

``flash`` erases the drive, so it confirms before writing and verifies the result
by reading it back. The image may be raw or compressed. See :ref:`flash`.

----

3. Configure boot files
-----------------------

Pick the project for your hardware, then configure the card:

.. code-block:: bash

   kli list-projects --drive /dev/sda
   sudo kli configure --drive /dev/sda --project ad9081 --board vck190

This copies the correct kernel and boot files for your eval-board and carrier
combination — the most error-prone manual step, done for you. **Raspberry Pi
cards boot without this step and can skip it.** See :ref:`configure`.

The card is now bootable.
