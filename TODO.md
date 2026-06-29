# TODO

Roadmap for `htmlview`. Roughly ordered by value for the app-specific use case.

## Next up
- [ ] **Form submission** — gather a `<form>`'s fields on submit and do
      something useful (GET query string / callback); today a button click just
      logs. Radio-button grouping; real `<select>` dropdown; textarea newlines.
- [ ] **Tab to move focus** between controls.
- [ ] **Paint-only hover recolour** — `:hover` currently re-resolves + relays
      out on link enter/leave; a recolor-without-relayout path would remove the
      hitch on large pages.

## Done
- [x] Basic forms: text inputs, checkboxes, buttons (focus, keyboard entry,
      click dispatch); textarea + select render statically.
- [x] Back/forward history (Alt+Left/Right, Backspace).
- [x] `:hover` pseudo-class (re-resolve + repaint on link enter/leave).
      `:visited` intentionally unsupported (no history).
- [x] Adjacent-sibling vertical margin collapsing.
- [x] Local link hit-testing, hand cursor on hover, click-to-navigate.
- [x] Per-side `border-*` shorthands; legacy `<!-- -->` style-comment wrapper.
- [x] Body background propagation to the canvas.

## Layout fidelity
- [ ] Table `colspan` / `rowspan` and auto min/max column sizing (currently even
      columns only).
- [ ] `width` / `margin: auto` horizontal centering of block boxes.
- [ ] Percentage lengths (`width: 50%`, etc.).
- [ ] `line-height` property (today derived purely from font metrics).
- [ ] Inline `vertical-align` (baseline/middle/top) for images and mixed fonts.

## CSS coverage
- [ ] `font` shorthand; `font-family` fallback list (try each family in order).
- [ ] `border-radius`, `border-style` rendering (only solid is drawn now).
- [ ] `background-image` (Imlib2 tiling) and `background` shorthand parsing.
- [ ] Child / sibling combinators (`>`, `+`, `~`) and attribute selectors.

## Parsing / robustness
- [ ] Larger named-entity table (currently a curated subset).
- [ ] `<tbody>` / `<thead>` / `<tfoot>` wrappers in tables.
- [ ] Graceful handling of unknown block-vs-inline display defaults.

## Rendering / perf
- [ ] Glyph fallback for missing code points (some bullets/symbols show tofu in
      fonts lacking them; shape bullets already sidestep this).
- [ ] Incremental repaint of only the exposed/dirty region.
- [ ] Image cache (images currently load twice: once for size, once to paint).

## Known limitations (intentional for v1)
- Latin/Western text only — no HarfBuzz shaping or bidi.
- No floats, positioning, or z-index.
