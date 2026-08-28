# Contributing

## Comments

The full design, workflows, and CLI reference live in the docs. Comments in the
code exist for one thing the docs and the type system **can't** carry: **why the
code is shaped the way it is.** Keep them minimal and load-bearing.

### Rules

1. **Explain _why_, never _what_.** If a comment restates the code, delete it.
   The code says what it does; the comment says why it has to.
2. **Rationale, invariants, and gotchas are mandatory** wherever a choice is
   non-obvious or looks wrong-but-isn't — a safety ordering, a workaround, a
   deliberate omission. These are the comments worth writing.
3. **Public headers get a 1–3 line intent comment** per type/function: behavior
   not visible in the signature — blocking? ownership? thread-safety? a safety
   guarantee? Nothing more.
4. **No Doxygen tags** (`@param`, `@return`, …). The signature already documents
   parameters; the doc site is written by hand, not generated from code.
5. **Point to the docs, don't duplicate them.** If something is explained in
   depth in the doc site, reference it (`// see docs: flash verification`)
   rather than restate it. The _why-this-code_ stays in the code regardless.
6. **Git history is not a comment.** No commented-out code, no changelog
   comments. A `TODO` needs an owner or a linked issue, or it doesn't go in.

### Example

Bad example, restates the code, adds nothing:

```cpp
i++;  // increment i
```

Good example, explains a non-obvious safety ordering the code alone can't
justify:

```cpp
// Write the partition-table head LAST, after the body verifies, so an
// interrupted flash never leaves a card that looks bootable.
constexpr std::uint64_t kDeferHead = 1u << 20;
```

The rest of the codebase is the reference for tone and density.

## Commits

The git history is documentation held to the same standard as the code: it
records **why** a change was made, in units small enough to reason about one at
a time. A commit is the smallest thing someone will `git bisect` to, revert, or
cite in a bug report years from now — shape each one so it stands on its own.

### Rules

1. **One logical change per commit.** A commit does exactly one thing, and does
   it completely. Don't bundle a refactor with a feature, or two features under
   an "and" — if the subject needs an "and", it's two commits.
2. **Every commit builds and passes.** The history is bisectable: check out any
   commit and it compiles with its tests green. A change that only makes sense
   alongside the next one belongs _in_ the next one.
3. **Subject is `area: imperative summary`.** Areas are `core`, `cli`, `gui`,
   `build`, `ci`, `docs`. Imperative mood ("add", not "added" or "adds"), no
   trailing period, kept short (~50 chars). The subject names the real change —
   never `wip`, `fixes`, `improvements`, or `ggg`.
4. **The body explains _why_, wrapped at 72.** The diff already shows what
   changed; the body carries what it can't — the constraint, the rejected
   alternative, the invariant being preserved. Skip the body only when the
   subject is genuinely self-evident.
5. **Sign off every commit.** `Signed-off-by:` on all commits (DCO). Add
   `Assisted-By:` when the change was assisted. Trailers are consistent across
   the history, not chosen per commit.

### Example

Bad, bundles two changes and explains nothing:

```text
update drive stuff and add fetch

improvements
```

Good, one change, the subject names it and the body carries the reasoning:

```text
core: implement flash (local image to drive)

Decompress in-process and stream to the raw drive so a multi-GB image
never lands on disk twice. libarchive sniffs the container; we refuse
rather than guess when an archive holds more than one image.
```

The existing history's early commits (`core: implement flash`,
`build: add libarchive …`) are the reference for tone and depth.
