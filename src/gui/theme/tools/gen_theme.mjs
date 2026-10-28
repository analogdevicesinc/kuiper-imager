#!/usr/bin/env node
// Generates the Qt theme artifacts from Harmonic's resolved token tables.
//
//   node src/gui/theme/tools/gen_theme.mjs
//
// Inputs  (vendored, committed): ../harmonic/color-tokens-{core,light,dark}.json
// Outputs (generated, committed): ../theme-light.qss, ../theme-dark.qss, ../Tokens.hpp
//
// The C++/CMake build never runs this — regenerate by hand when Harmonic's
// tokens change, then commit the outputs. See ../README.md.
//
// Harmonic ships every token pre-resolved to a concrete value (hex or
// `rgb(R% G% B% / A)`), so there is no SCSS/`var()`/alias resolution to do here;
// we only normalise the colour syntax into forms Qt QSS / QColor accept.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const themeDir = join(here, '..');
const harmonicDir = join(themeDir, 'harmonic');

const load = (name) =>
  JSON.parse(readFileSync(join(harmonicDir, name), 'utf8'));

const core = load('color-tokens-core.json');
const light = load('color-tokens-light.json');
const dark = load('color-tokens-dark.json');

// name (without leading `$`) -> raw value string. Core is the palette base;
// the per-theme table layers the semantic tokens on top.
const tableFor = (semantic) => {
  const map = new Map();
  for (const row of [...core, ...semantic]) {
    if (!row.scss || !row.hex) continue;
    map.set(row.scss.replace(/^\$/, ''), row.hex.trim());
  }
  return map;
};

const LIGHT = tableFor(light);
const DARK = tableFor(dark);

// --- colour normalisation ---------------------------------------------------
// Returns {r,g,b,a} (a in [0,1]) from any value Harmonic emits.
function parseColor(raw) {
  let v = raw.trim();

  // #rgb / #rrggbb (Harmonic also writes the short `#fff`).
  let m = v.match(/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/);
  if (m) {
    let h = m[1];
    if (h.length === 3) h = [...h].map((c) => c + c).join('');
    return {
      r: parseInt(h.slice(0, 2), 16),
      g: parseInt(h.slice(2, 4), 16),
      b: parseInt(h.slice(4, 6), 16),
      a: 1,
    };
  }

  // Modern CSS: `rgb(R% G% B% / A)` or `rgb(R% G% B%)`, space-separated.
  m = v.match(
    /^rgba?\(\s*([\d.]+)%\s+([\d.]+)%\s+([\d.]+)%\s*(?:\/\s*([\d.]+))?\s*\)$/,
  );
  if (m) {
    const pct = (s) => Math.round((parseFloat(s) / 100) * 255);
    return {
      r: pct(m[1]),
      g: pct(m[2]),
      b: pct(m[3]),
      a: m[4] === undefined ? 1 : parseFloat(m[4]),
    };
  }

  // Legacy comma form `rgb(r,g,b)` / `rgba(r,g,b,a)`, just in case.
  m = v.match(/^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([\d.]+))?\)$/);
  if (m) {
    return {
      r: +m[1],
      g: +m[2],
      b: +m[3],
      a: m[4] === undefined ? 1 : parseFloat(m[4]),
    };
  }

  // CIE LCh (CSS Color 4): `lch(L C H / A)`, L in [0,100], H in degrees.
  m = v.match(/^lch\(\s*([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*(?:\/\s*([\d.]+))?\s*\)$/);
  if (m) {
    const [r, g, b] = lchToRgb(+m[1], +m[2], +m[3]);
    return { r, g, b, a: m[4] === undefined ? 1 : parseFloat(m[4]) };
  }

  throw new Error(`unparseable colour: ${raw}`);
}

// CIE LCh(ab) -> sRGB (0..255). Lab uses the D50 white point (CSS Color 4);
// XYZ is Bradford-adapted D50->D65, then the standard D65 XYZ->sRGB matrix.
function lchToRgb(L, C, H) {
  const h = (H * Math.PI) / 180;
  const a = C * Math.cos(h);
  const bb = C * Math.sin(h);

  // Lab -> XYZ (D50)
  const fy = (L + 16) / 116;
  const fx = fy + a / 500;
  const fz = fy - bb / 200;
  const eps = 216 / 24389;
  const kappa = 24389 / 27;
  const f3 = (t) => (t ** 3 > eps ? t ** 3 : (116 * t - 16) / kappa);
  const Xn = 0.96422, Yn = 1.0, Zn = 0.82521; // D50 reference white
  let X = f3(fx) * Xn;
  let Y = (L > kappa * eps ? fy ** 3 : L / kappa) * Yn;
  let Z = f3(fz) * Zn;

  // Bradford chromatic adaptation D50 -> D65
  const Xd = 0.9555766 * X - 0.0230393 * Y + 0.0631636 * Z;
  const Yd = -0.0282895 * X + 1.0099416 * Y + 0.0210077 * Z;
  const Zd = 0.0122982 * X - 0.0204830 * Y + 1.3299098 * Z;

  // XYZ (D65) -> linear sRGB
  let rl = 3.2404542 * Xd - 1.5371385 * Yd - 0.4985314 * Zd;
  let gl = -0.969266 * Xd + 1.8760108 * Yd + 0.041556 * Zd;
  let bl = 0.0556434 * Xd - 0.2040259 * Yd + 1.0572252 * Zd;

  const gamma = (u) =>
    u <= 0.0031308 ? 12.92 * u : 1.055 * u ** (1 / 2.4) - 0.055;
  const clamp = (u) => Math.min(255, Math.max(0, Math.round(gamma(u) * 255)));
  return [clamp(rl), clamp(gl), clamp(bl)];
}

// QSS-ready string: `#rrggbb` when opaque, else `rgba(r, g, b, a)`.
function qss(name, table) {
  const raw = table.get(name);
  if (raw === undefined) {
    console.warn(`  ! missing token: $${name} (using magenta sentinel)`);
    return '#ff00ff';
  }
  const { r, g, b, a } = parseColor(raw);
  if (a >= 1) {
    const hex = (n) => n.toString(16).padStart(2, '0');
    return `#${hex(r)}${hex(g)}${hex(b)}`;
  }
  return `rgba(${r}, ${g}, ${b}, ${a})`;
}

// --- geometry (read off graphics/src/components/button/button.scss) ---------
// Harmonic button: radius 4px; sizes md=40 / sm=32 / xs=24; body font 14px/600.
const SIZES = {
  md: { h: 40, padX: 16, font: 14 },
  sm: { h: 32, padX: 12, font: 14 },
  xs: { h: 24, padX: 8, font: 12 },
};

// --- QSS emitters -----------------------------------------------------------
function buttonBlock(t) {
  const c = (n) => qss(n, t);
  // No `-pressed` tokens exist in Harmonic's table, so :pressed reuses hover.
  const lines = [];

  lines.push(`/* --- HmcButton (QPushButton skin, keyed on dynamic props) --- */`);
  lines.push(`QPushButton[hmc="true"] {`);
  lines.push(`    border: none;`);
  lines.push(`    border-radius: 4px;`);
  lines.push(`    font-family: "Inter";`);
  lines.push(`    font-weight: 600;`);
  lines.push(`    font-size: ${SIZES.md.font}px;`);
  lines.push(`    min-height: ${SIZES.md.h}px;`);
  lines.push(`    padding: 0 ${SIZES.md.padX}px;`);
  lines.push(`}`);

  for (const [name, s] of Object.entries(SIZES)) {
    lines.push(`QPushButton[hmc="true"][size="${name}"] {`);
    lines.push(`    min-height: ${s.h}px; padding: 0 ${s.padX}px; font-size: ${s.font}px;`);
    lines.push(`}`);
  }

  // primary — filled
  lines.push(`QPushButton[hmc="true"][kind="primary"] {`);
  lines.push(`    background-color: ${c('c-action-strong-background')};`);
  lines.push(`    color: ${c('c-action-strong-content')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="primary"]:hover,`);
  lines.push(`QPushButton[hmc="true"][kind="primary"]:pressed {`);
  lines.push(`    background-color: ${c('c-action-strong-background-hover')};`);
  lines.push(`    color: ${c('c-action-strong-content-hover')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="primary"]:disabled,`);
  lines.push(`QPushButton[hmc="true"][kind="primary"][processing="true"] {`);
  lines.push(`    background-color: ${c('c-action-strong-background-disabled')};`);
  lines.push(`    color: ${c('c-action-strong-content-disabled')};`);
  lines.push(`}`);

  // secondary — outline
  lines.push(`QPushButton[hmc="true"][kind="secondary"] {`);
  lines.push(`    background-color: transparent;`);
  lines.push(`    border: 1px solid ${c('c-action-medium-border')};`);
  lines.push(`    color: ${c('c-action-medium-content')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="secondary"]:hover,`);
  lines.push(`QPushButton[hmc="true"][kind="secondary"]:pressed {`);
  lines.push(`    background-color: ${c('c-action-medium-background-hover')};`);
  lines.push(`    border: 1px solid ${c('c-action-medium-border-hover')};`);
  lines.push(`    color: ${c('c-action-medium-content-hover')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="secondary"]:disabled,`);
  lines.push(`QPushButton[hmc="true"][kind="secondary"][processing="true"] {`);
  lines.push(`    border: 1px solid ${c('c-action-medium-border-disabled')};`);
  lines.push(`    color: ${c('c-action-medium-content-disabled')};`);
  lines.push(`}`);

  // tertiary — subtle outline
  lines.push(`QPushButton[hmc="true"][kind="tertiary"] {`);
  lines.push(`    background-color: transparent;`);
  lines.push(`    border: 1px solid ${c('c-action-weak-border')};`);
  lines.push(`    color: ${c('c-action-weak-content')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="tertiary"]:hover,`);
  lines.push(`QPushButton[hmc="true"][kind="tertiary"]:pressed {`);
  lines.push(`    background-color: ${c('c-action-weak-background-hover')};`);
  lines.push(`    border: 1px solid ${c('c-action-weak-border-hover')};`);
  lines.push(`    color: ${c('c-action-weak-content-hover')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="tertiary"]:disabled,`);
  lines.push(`QPushButton[hmc="true"][kind="tertiary"][processing="true"] {`);
  lines.push(`    border: 1px solid ${c('c-action-weak-border-disabled')};`);
  lines.push(`    color: ${c('c-action-weak-content-disabled')};`);
  lines.push(`}`);

  // ghost — text only
  lines.push(`QPushButton[hmc="true"][kind="ghost"] {`);
  lines.push(`    background-color: transparent;`);
  lines.push(`    color: ${c('c-action-weakest-content')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="ghost"]:hover,`);
  lines.push(`QPushButton[hmc="true"][kind="ghost"]:pressed {`);
  lines.push(`    background-color: ${c('c-action-weakest-background-hover')};`);
  lines.push(`    color: ${c('c-action-weakest-content-hover')};`);
  lines.push(`}`);
  lines.push(`QPushButton[hmc="true"][kind="ghost"]:disabled,`);
  lines.push(`QPushButton[hmc="true"][kind="ghost"][processing="true"] {`);
  lines.push(`    color: ${c('c-action-weakest-content-disabled')};`);
  lines.push(`}`);

  // focus ring — Qt ignores `outline`, so swap the border instead.
  lines.push(`QPushButton[hmc="true"]:focus {`);
  lines.push(`    border: 2px solid ${c('c-interactive-focus')};`);
  lines.push(`}`);

  return lines.join('\n');
}

// Harmonic components beyond the button, skinned onto native widgets. Each rule
// is keyed on a dynamic property the matching Hmc* widget sets. Token names and
// geometry are taken from graphics/src/components/<name>/<name>.scss.
function componentsBlock(t) {
  const c = (n) => qss(n, t);
  const notif = (s) => ({
    bg: c(`c-notification-${s}-background`),
    fg: c(`c-notification-${s}-text`),
    border: c(`c-notification-${s}-border`),
  });
  const STATUSES = {
    info: notif('info'),
    success: notif('success'),
    warning: notif('warning'),
    danger: notif('danger'),
    highlight: notif('highlight'),
  };

  const out = [];

  // --- App header bar + hairline divider ---
  out.push(`/* --- chrome --- */`);
  out.push(`QFrame[hmcHeaderBar="true"] { background-color: ${c('c-layout-container')}; border-bottom: 1px solid ${c('c-layout-divider')}; }`);
  out.push(`QFrame[hmcDivider="true"] { background-color: ${c('c-layout-divider')}; max-height: 1px; min-height: 1px; }`);

  // --- Card (QFrame): 8px radius, 1px divider, container surface ---
  out.push(`/* --- HmcCard --- */`);
  out.push(`QFrame[hmcCard="true"] {`);
  out.push(`    background-color: ${c('c-layout-container')};`);
  out.push(`    border: 1px solid ${c('c-layout-divider')};`);
  out.push(`    border-radius: 8px;`);
  out.push(`}`);

  // --- Container header labels ---
  out.push(`/* --- HmcContainerHeader --- */`);
  out.push(`QLabel[hmcHeading="true"] { color: ${c('c-content')}; font-weight: 700; font-size: 16px; }`);
  out.push(`QLabel[hmcHeading="true"][density="low"] { font-size: 20px; font-weight: 600; }`);
  out.push(`QLabel[hmcSubtitle="true"] { color: ${c('c-content-medium')}; font-weight: 400; font-size: 14px; }`);
  out.push(`QLabel[hmcCaption="true"] { color: ${c('c-content-weak')}; font-size: 12px; }`);

  // --- Tag (QLabel pill) ---
  out.push(`/* --- HmcTag --- */`);
  for (const [name, s] of Object.entries(STATUSES)) {
    out.push(`QLabel[hmcTag="true"][status="${name}"] {`);
    out.push(`    background-color: ${s.bg}; color: ${s.fg};`);
    out.push(`    border: 1px solid ${s.border}; border-radius: 4px;`);
    out.push(`    padding: 1px 6px; font-size: 12px; font-weight: 600;`);
    out.push(`}`);
  }

  // --- Progress bar (QProgressBar), 8px track, per-status chunk ---
  out.push(`/* --- HmcProgressBar --- */`);
  out.push(`QProgressBar[hmcProgress="true"] {`);
  out.push(`    background-color: ${c('c-progress-background')};`);
  out.push(`    border: none; border-radius: 4px; max-height: 8px; min-height: 8px;`);
  out.push(`}`);
  // Harmonic progress: *-background is the (grey) track, *-content is the fill.
  out.push(`QProgressBar[hmcProgress="true"]::chunk { background-color: ${c('c-progress-primary-content')}; border-radius: 4px; }`);
  out.push(`QProgressBar[hmcProgress="true"][status="success"]::chunk { background-color: ${c('c-progress-success-content')}; }`);
  out.push(`QProgressBar[hmcProgress="true"][status="danger"]::chunk { background-color: ${c('c-progress-danger-content')}; }`);

  // --- Segmented control: bordered frame + flat checkable buttons ---
  out.push(`/* --- HmcSegmentedControl --- */`);
  out.push(`QFrame[hmcSegmented="true"] { border: 1px solid ${c('c-selection-unselected-medium-border')}; border-radius: 4px; }`);
  out.push(`QPushButton[hmcSegment="true"] {`);
  out.push(`    border: none; border-radius: 0; background-color: transparent;`);
  out.push(`    color: ${c('c-selection-unselected-medium-content')};`);
  out.push(`    padding: 0 16px; min-height: 38px; font-size: 14px; font-weight: 600;`);
  out.push(`}`);
  out.push(`QPushButton[hmcSegment="true"][pos="mid"], QPushButton[hmcSegment="true"][pos="last"] {`);
  out.push(`    border-left: 1px solid ${c('c-selection-unselected-medium-border')};`);
  out.push(`}`);
  out.push(`QPushButton[hmcSegment="true"][pos="first"] { border-top-left-radius: 3px; border-bottom-left-radius: 3px; }`);
  out.push(`QPushButton[hmcSegment="true"][pos="last"] { border-top-right-radius: 3px; border-bottom-right-radius: 3px; }`);
  out.push(`QPushButton[hmcSegment="true"]:checked {`);
  out.push(`    background-color: ${c('c-selection-selected-medium-background')};`);
  out.push(`    color: ${c('c-selection-selected-medium-content')};`);
  out.push(`}`);

  // --- Checkbox (QCheckBox) ---
  out.push(`/* --- HmcCheckBox --- */`);
  out.push(`QCheckBox[hmcCheck="true"] { color: ${c('c-content')}; spacing: 8px; font-size: 14px; }`);
  out.push(`QCheckBox[hmcCheck="true"]::indicator {`);
  out.push(`    width: 16px; height: 16px; border-radius: 4px;`);
  out.push(`    border: 1px solid ${c('c-selection-unselected-strong-border')};`);
  out.push(`    background-color: ${c('c-layout-container')};`);
  out.push(`}`);
  out.push(`QCheckBox[hmcCheck="true"]::indicator:hover { border-color: ${c('c-selection-unselected-strong-border-hover')}; }`);
  out.push(`QCheckBox[hmcCheck="true"]::indicator:checked {`);
  out.push(`    border: none; background-color: ${c('c-selection-selected-strong-background')};`);
  out.push(`    image: url(:/theme/check.svg);`);
  out.push(`}`);

  // --- Banner message (QFrame, subtle emphasis) ---
  out.push(`/* --- HmcBanner (subtle) --- */`);
  for (const [name, s] of Object.entries(STATUSES)) {
    out.push(`QFrame[hmcBanner="true"][status="${name}"] {`);
    out.push(`    background-color: ${s.bg}; border: 1px solid ${s.border}; border-radius: 8px;`);
    out.push(`}`);
    out.push(`QFrame[hmcBanner="true"][status="${name}"] QLabel { color: ${s.fg}; background: transparent; }`);
  }

  return out.join('\n');
}

function themeQss(themeName, t) {
  const c = (n) => qss(n, t);
  return `/* GENERATED by src/gui/theme/tools/gen_theme.mjs — do not edit.
   Harmonic ${themeName} theme. Regenerate: node src/gui/theme/tools/gen_theme.mjs */

/* --- app surfaces --- */
/* Base text/font only — NOT background: a universal background-color would paint
   every child (labels, rows) and hide the card surfaces. The canvas colour is set
   on the window instead, and children stay transparent unless a rule opts in. */
QWidget {
    color: ${c('c-content')};
    font-family: "Inter";
    font-size: 14px;
}
QMainWindow, QDialog { background-color: ${c('c-layout-canvas')}; }

QTableWidget, QTableView {
    background-color: ${c('c-layout-container')};
    alternate-background-color: ${c('c-layout-canvas')};
    gridline-color: ${c('c-layout-divider')};
    border: 1px solid ${c('c-layout-divider')};
    border-radius: 4px;
    selection-background-color: ${c('c-action-strong-background')};
    selection-color: ${c('c-action-strong-content')};
}
QHeaderView::section {
    background-color: ${c('c-layout-container')};
    color: ${c('c-content-weak')};
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid ${c('c-layout-divider')};
    font-weight: 600;
}
QStatusBar { color: ${c('c-content-weak')}; }
QStatusBar QLabel { color: ${c('c-content-weak')}; }

${buttonBlock(t)}

${componentsBlock(t)}
`;
}

// --- Tokens.hpp -------------------------------------------------------------
const ident = (name) =>
  name
    .replace(/^c-/, '')
    .split('-')
    .map((w, i) => (i === 0 ? w : w[0].toUpperCase() + w.slice(1)))
    .join('')
    .replace(/[^A-Za-z0-9]/g, '');

function tokensNamespace(t) {
  const out = [];
  for (const [name, raw] of [...t.entries()].sort()) {
    const { r, g, b, a } = parseColor(raw);
    out.push(`    inline const QColor ${ident(name)}{${r}, ${g}, ${b}, ${Math.round(a * 255)}};  // $${name}`);
  }
  return out.join('\n');
}

function tokensHpp() {
  return `#pragma once
// GENERATED by src/gui/theme/tools/gen_theme.mjs — do not edit.
// Harmonic colour tokens as QColor constants, for custom-painted widgets
// (QSS covers everything stylable; this is for QPainter code).
// Regenerate: node src/gui/theme/tools/gen_theme.mjs

#include <QColor>

namespace kuiper::theme::light {
${tokensNamespace(LIGHT)}
}  // namespace kuiper::theme::light

namespace kuiper::theme::dark {
${tokensNamespace(DARK)}
}  // namespace kuiper::theme::dark
`;
}

// --- write ------------------------------------------------------------------
console.log('Generating Qt theme from Harmonic tokens...');
writeFileSync(join(themeDir, 'theme-light.qss'), themeQss('light', LIGHT));
writeFileSync(join(themeDir, 'theme-dark.qss'), themeQss('dark', DARK));
writeFileSync(join(themeDir, 'Tokens.hpp'), tokensHpp());
console.log(`  theme-light.qss, theme-dark.qss, Tokens.hpp written to ${themeDir}`);
console.log(`  (${LIGHT.size} light tokens, ${DARK.size} dark tokens)`);
