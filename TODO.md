# TODO

Roadmap for `htmlview`. Roughly ordered by value for the app-specific use case.

## Next up
- [ ] **CGI: persistent-coprocess option** for in-memory state (re-spawn POST
      already works).
- [ ] **CGI: backend-served images/assets** (currently HTML/CSS only).
- [ ] Real `<select>` dropdown (currently a static box).
- [ ] **Paint-only hover recolour** — `:hover` currently re-resolves + relays
      out on link enter/leave; a recolor-without-relayout path would remove the
      hitch on large pages.

## Done
- [x] Forms part 2: POST submission (CGI, body on stdin), radio-button grouping,
      password masking, reset buttons, Tab focus cycling, multi-line textarea
      editing; body-less documents lay out from the root.
- [x] `--cgi-bin`: classic-CGI backend (re-spawn per request), link navigation
      and GET form submission; in-window error page on load failure.
- [x] `font-family` fallback list (fontconfig picks first available).
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
- [x] Table `colspan` / `rowspan` (grid-occupancy model). Still even columns —
      auto min/max column sizing and `border-collapse` remain.
- [ ] `width` / `margin: auto` horizontal centering of block boxes.
- [ ] Percentage lengths (`width: 50%`, etc.).
- [ ] `line-height` property (today derived purely from font metrics).
- [ ] Inline `vertical-align` (baseline/middle/top) for images and mixed fonts.

## CSS coverage
- [ ] `font` shorthand.
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

---

# Standards-compliance roadmaps

> Version note: there was never a formally standardised **HTML 1.1** — HTML 1.x
> was the pre-standard, early-Mosaic-era document model. **HTML 2.0 (RFC 1866,
> 1995)** was the first real standard. The two sections below are scoped to the
> *gaps* relative to what htmlview already renders. Both predate CSS (CSS1 is
> 1996), so "compliance" here means the elements + their default presentation;
> our CSS engine is a bonus layer on top.

## Toward the early-core HTML document model ("HTML 1.x") — DONE

All implemented (see `test/html1x.html`):

- [x] **Block elements**: `<blockquote>` (indented), `<address>` (italic),
      `<dl>/<dt>/<dd>`, `<menu>`/`<dir>`.
- [x] **Phrase/typographic inlines**: `<code> <samp> <kbd> <var> <cite> <tt>`
      and `<u>`.
- [x] **`<sub>` / `<sup>`** — smaller font + inline baseline shift.
- [x] **Named anchors + fragment navigation**: `id="x"` / `<a name="x">` targets,
      `href="#x"` and `file#x` scroll to the anchor.
- [x] **`alt` text** rendered when an `<img>` is missing/unloadable.
- [x] **Full ISO 8859-1 entity set** + common general punctuation (shared UTF-8
      encoder; numeric `&#…;` too).

## Toward HTML 2.0 (RFC 1866) features

Beyond the core model, 2.0's distinctive additions — and our gaps:

- **Forms model** — mostly done:
  - [x] text / `password` (masked) / checkbox / `radio` (grouped) inputs,
        submit / `reset` / plain buttons, multi-line `<textarea>`, Tab focus.
  - [x] **`method=POST`** with `application/x-www-form-urlencoded` body (CGI,
        on the program's stdin); GET submits via the query string.
  - [ ] Real `<select>` (dropdown, option selection, `multiple`) — static today.
  - [ ] `<input type=image>` and `<input type=file>`.
  - [~] `<isindex>` single-field search prompt — **won't do** (obsolete; a
        one-field `<form>` covers the same need).
- [ ] **Head elements**:
  - [x] **`<link rel=stylesheet href>`** — external CSS (file dir, or CGI
        requests); honoured by the window and the render/dump paths.
  - [ ] **`<base href>`** — resolve relative URLs/links against it.
  - [x] `<meta http-equiv=refresh>` — honoured (with optional `url=`), overrides
        the `--refresh` CLI flag.
  - [ ] `<meta charset>` and other `<meta>` keys.
- [x] **Resource fetching** for `<link>` CSS and `<img src>`: `data:` URLs
      (base64 / percent-encoded) decode to temp files; CGI-mode images are
      fetched via GET. (Temp files are cached per page but leak until exit — a
      cleanup/cache-eviction pass is still TODO.)


> Out of scope for 2.0 (arrived later, already partly done here as extras):
> tables (HTML 3.0+ — we have a basic implementation), most presentational
> attributes and CSS (1996+), frames (4.0), and scripting.
