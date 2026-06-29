# TODO

Roadmap for `htmlview`. Roughly ordered by value for the app-specific use case.

## Next up
- [ ] **Basic forms** (the planned phase 2): `input` (text/checkbox/button),
      focus management, keyboard entry, click dispatch. Hit-testing + link
      `href` plumbing already exist to build on.
- [ ] **Pseudo-classes** `a:hover` / `a:visited` (currently parsed but never
      matched). At minimum track hover under the pointer and repaint.
- [ ] **Vertical margin collapsing** between adjacent block boxes (today margins
      simply add, so vertical spacing is larger than a browser's).

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
