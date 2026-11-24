# GUI theme

The GUI is skinned with a small, hand-authored Qt theme. It is plain QSS plus a
handful of `QColor` constants — there is no build step and no external tooling.

## Files

```
theme-light.qss  theme-dark.qss   the two stylesheets (one per mode)
Tokens.hpp                        QColor constants for QPainter code
check.svg                         checkbox tick glyph
theme.qrc                         bundles the above into the binary
ThemeManager.cpp/.hpp             loads a stylesheet and applies it app-wide
```

`theme.qrc` → `ThemeManager::applyTheme()` loads the chosen `.qss` at startup and
on theme switch.

## How it fits together

- **`theme-*.qss`** skins the stylable widgets. Most Kuiper widgets are native
  Qt widgets carrying a dynamic property (e.g. `QPushButton[kuiperButton="true"]`,
  `QFrame[kuiperCard="true"]`); the QSS keys its selectors on those properties.
  Changing a property at runtime requires a re-polish — see `util/Repolish.hpp`.
- **`Tokens.hpp`** gives the same colours as `QColor` constants for the few
  widgets that paint themselves (e.g. `KuiperSpinner`), since QSS can't reach
  `QPainter` code.

## Editing

Edit the `.qss` and `Tokens.hpp` by hand. Qt has no `var()`, so a colour used in
both modes or in painter code lives in more than one place — **keep
`theme-light.qss`, `theme-dark.qss`, and `Tokens.hpp` in sync** when you change
one. Fonts: the QSS asks for `"Inter"`; if it isn't installed Qt falls back.
