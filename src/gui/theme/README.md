# GUI theme — Harmonic tokens → Qt

The GUI adopts ADI's **Harmonic** design language by reusing its *design tokens*
(the Stencil web components themselves don't run in Qt). Harmonic ships every
token pre-resolved to a concrete value, so there is no SCSS to compile.

## Pipeline

```
harmonic/color-tokens-{core,light,dark}.json   (vendored from the Harmonic repo)
        │  node tools/gen_theme.mjs             (run by hand, not part of the build)
        ▼
theme-light.qss  theme-dark.qss  Tokens.hpp     (generated, committed)
        │  theme.qrc → ThemeManager::applyTheme()
        ▼
QSS-skinned native widgets (e.g. widgets/HmcButton)
```

- **`harmonic/*.json`** — vendored copies of Harmonic's resolved token tables
  (`graphics/src/stories/assets/color-tokens-*.json`). Re-copy to update.
- **`tools/gen_theme.mjs`** — normalises token colours into Qt forms and emits
  the QSS + `Tokens.hpp`. No npm dependencies; just `node`.
- **`theme-*.qss` / `Tokens.hpp`** — **generated, do not edit by hand.** QSS is
  for stylable widgets; `Tokens.hpp` gives `QColor` constants for `QPainter`
  code.

## Regenerate

```sh
node src/gui/theme/tools/gen_theme.mjs
```

Then rebuild (the C++ build only reads the committed outputs; it never runs Node)
and commit the regenerated files.

## Qt vs web (handled in the generator)

- `var()` / `$vars` / nesting — pre-resolved in the JSON; QSS gets literals.
- `rgb(R% G% B% / A)` / `#fff` / `lch(…)` → Qt `#rrggbb` or `rgba(0-255,…)`.
- web `:active` → Qt `:pressed`; `.disabled` → `:disabled`; `.processing` →
  `[processing="true"]`; variants → `[kind="…"]` / `[size="…"]`.
- `outline` / `:focus-visible` are ignored by Qt → focus ring is a `border` swap.
- `box-shadow` / CSS transitions are not reproduced (flat; animations would be a
  later `QPropertyAnimation` concern).
- Fonts: QSS asks for `"Inter"`; if it isn't installed Qt falls back. Bundling
  the OFL TTFs via the qrc is a follow-up.
