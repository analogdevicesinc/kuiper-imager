.. _quick-start:

GUI Walkthrough
===============

.. description::

   Prepare a bootable Kuiper card with the desktop app, from picking an image
   to safely ejecting the finished card

The Kuiper Imager desktop app turns a blank SD card into a bootable Analog
Devices system. This page walks through the whole flow. The window is a single
screen built from three stacked cards — **Image**, **Storage**, and an **action
area** — so the workflow reads top to bottom: prepare each input, then commit
the write.

Launch it from your desktop menu (*Kuiper Imager*) or by running
``kuiper-imager``. For the internals behind this screen — the threading model,
the download cache, the post-write flow — see :ref:`gui-console`.

----

1. Choose an image
------------------

The **Image** card is where you select what to write. You have two ways to get
an image:

- **Use a local file.** Pick an image you already have — a raw ``.img`` or a
  compressed ``.zip`` / ``.xz`` / ``.zst`` / ``.gz`` / ``.bz2``.
- **Download a release.** Open the release picker to browse published Kuiper
  releases (and, optionally, CI builds) and download one. Downloads are kept in
  a per-user cache, so a release you already fetched is reused instead of
  downloaded again — the picker badges cached releases, shows their on-disk
  size, and lets you delete one to reclaim space.

A small **provenance pill** on the card shows whether the current image is
*Local* or *Downloaded*, so you always know where it came from.

----

2. Choose the drive
-------------------

The **Storage** card lists the drives you can write to. Each carries tags that
tell you what it is:

- **Removable** — the kind of media you can safely pull.
- **System** — the disk hosting your running operating system. A system drive is
  shown with a red **System** tag and **cannot be selected for writing** — the
  app will not let you erase the OS it is running on.
- **Kuiper 2** — a card that already holds a flashed Kuiper image (so you can
  reconfigure it without reflashing).

Select the card you inserted. If it is not listed, reseat it and let the list
refresh.

----

3. Write
--------

The **action area** holds a **Verify** toggle and the **Write** button. Writing
erases the target, so it is deliberate:

- Leave **Verify** on (the default) to have the app read the card back after
  writing and confirm it matches the image — the integrity guarantee that sets
  Kuiper Imager apart from a plain disk copy.
- Click **Write**. A confirmation dialog names the drive it will erase; confirm
  to proceed. Writing needs elevated rights, so you are prompted once (polkit on
  Linux, UAC on Windows) for the write itself.

Progress is shown as the image is written, flushed, and verified. When it
finishes you get a success banner.

----

4. Configure the card
---------------------

Most eval-board-and-carrier combinations need the correct boot files copied onto
the card after flashing. After a successful write, the Storage card recognises
the new Kuiper 2 card and offers **Configure**.

The Configure dialog lists the hardware projects the card declares. Pick your
**eval board** and **carrier**, optionally preview the exact copy plan with a
**dry run**, then apply. Intel-based boards also need a raw preloader written and
show a short warning to that effect; this is handled for you (Intel configure is
Linux-only — see :ref:`configure`).

**Raspberry Pi cards boot without this step** and can skip Configure.

----

5. Customize an image (Linux only)
----------------------------------

If you have a *basic* local ``.img`` and want the *full* ADI stack on it, the
Image card offers **Customize** (Linux only, for a raw local image). The dialog
lists the installable features grouped into **Desktop**, **Libraries**, and
**Applications** with size estimates. Choose what you want, let the image
auto-grow to fit (or set an explicit size), and apply — the install runs in the
elevated helper with a live log. See :ref:`customize` for what each feature
installs.

----

6. Eject safely
---------------

After a write the card is left mounted so Configure can keep working. When you
are done, use **Safe eject** from the banner: it releases the card and powers it
down so the OS won't touch it again, and tells you if something is still holding
the card so you don't pull it mid-write. (Safe eject is available on Linux and
Windows.)

Your card is now ready to boot.

.. note::

   The app is designed to be self-explanatory, and richer in-app guidance
   (such as hover hints on each control) is planned. Until then, this page and
   the :ref:`CLI walkthrough <cli-overview>` are the reference for what each
   action does.
