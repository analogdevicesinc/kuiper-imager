.. _contributing:

Contributing
============

.. description::

   How to contribute to Kuiper Imager: the standards for comments and commits

The authoritative contribution guide is `CONTRIBUTING.md
<https://github.com/analogdevicesinc/kuiper-imager/blob/main/CONTRIBUTING.md>`__
in the repository root. This page summarizes its two central standards; the file
itself is the source of truth.

----

Comments
--------

**This documentation is the home for design, workflows, and CLI reference.** Code
comments are not a second copy of it. The rule is to explain *why*, not *what*:

- Keep comments that carry load-bearing rationale — a safety ordering or
  invariant, a workaround for a specific kernel / libcurl / udev quirk, the
  meaning of a magic constant, ownership or thread-safety that isn't visible in
  the signature.
- Drop comments that restate the code or narrate architecture; that material
  belongs here in the docs. Where a removed narrative is genuinely useful at a
  seam, leave a one-line pointer (``// see docs: <topic>``) rather than the prose.
- Public headers keep a short (1–3 line) intent comment per type or function.
- No commented-out code; a ``TODO`` needs an owner.

If you find yourself explaining *how the system works* in a comment, write it in
the relevant :ref:`How It Works <architecture>` page instead and link to it.

----

Commits
-------

- **One logical change per commit**, and **every commit builds and passes** — the
  history stays bisectable.
- Subject line: ``area: imperative summary`` (for example ``cli: add the fetch
  command``).
- Body explains *why*, wrapped at 72 columns.
- Sign off with the DCO (``Signed-off-by:``) and include the ``Co-Authored-By:``
  trailer where applicable.

See `CONTRIBUTING.md
<https://github.com/analogdevicesinc/kuiper-imager/blob/main/CONTRIBUTING.md>`__
for the complete rules and examples.
