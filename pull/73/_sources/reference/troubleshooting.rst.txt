.. _troubleshooting:

Troubleshooting
===============

.. description::

   Common problems and what they mean, mapped to Kuiper Imager's error codes

Every failure prints a single ``error [Code]: message`` line to stderr — often
with a ``hint:`` — and the process exits with a :ref:`stable code <cli-overview>`
you can branch on. This page groups the failures you are most likely to see by
what you can do about them.

----

Permission Denied
-----------------

*Exit code 4 (``PermissionDenied``).* Reading and writing raw devices requires
root. Run the writing commands under ``sudo``:

.. code-block:: bash

   sudo kli flash --image ./kuiper.zip --drive /dev/sda
   sudo kli configure --drive /dev/sda --project ad9081 --board vck190

``list-drives`` and ``list-releases`` do not need elevation. See
:ref:`platform-backends` for how privilege differs per OS.

----

Device Busy
-----------

*Exit code 5 (``DeviceBusy``).* Something else is holding the drive — a live
mount, swap, an ``md`` array, LVM, or a LUKS mapping. Kuiper Imager unmounts the
target's own filesystems before writing and opens the device exclusively
(``O_EXCL``), so this means a *different* holder is in the way. Close the program
using the drive (or stop the array / deactivate the volume) and retry. Note that
opening exclusively is also what lets Kuiper Imager detect the busy state up
front instead of corrupting an in-use device.

----

Drive Not Found or Not a Kuiper Card
------------------------------------

*Exit code 10 (``NotFound``); a card that isn't a flashed Kuiper image exits 1.*
``--drive`` must be a **whole drive** exactly as ``kli list-drives`` reports it
(for example ``/dev/sda``, not ``/dev/sda1``). ``configure`` additionally needs a
card that has already been flashed: if it can't find a BOOT partition, the card
isn't a Kuiper 2.0 card yet — :ref:`flash <flash>` it first.

----

Fetch Fails with 401 or Token Errors
------------------------------------

*Typically exit code 4 or 1, with a ``hint:`` about credentials.* Downloading CI
artifacts goes through the GitHub API and needs a token with ``actions:read``.
Provide one of:

.. code-block:: bash

   export GH_TOKEN=ghp_...        # or GITHUB_TOKEN
   gh auth login                  # or log in with the GitHub CLI

A ``404`` on an identifier that used to work usually means the artifact
**expired** — CI artifacts are retained for a limited time. Re-run
``kli list-releases --unstable`` for a current identifier. See :ref:`fetch`.

----

Hash Mismatch
-------------

*Exit code 9 (``HashMismatch``).* After writing, Kuiper Imager reads the media
back and compares hashes; this code means what's on the card does not match what
was written — a failing card or a bad connection. The partition table is left
wiped so a corrupt card cannot appear bootable. Try a different card or reader,
then reflash. You can skip verification with ``--no-verify`` (see :ref:`flash`),
but only when you have another reason to trust the media.

----

The GUI Only Lists Drives
-------------------------

The Qt GUI is an early skeleton — today it enumerates drives and nothing more.
Use the ``kli`` CLI for ``fetch``, ``flash``, and ``configure``. Wiring the GUI to
the core is a postponed phase; see :ref:`roadmap`.
