# TODO

Roadmap for `htmlview`. Roughly ordered by value for the app-specific use case.

## Next up
- [ ] **`line-height` + inline `vertical-align`** (the next layout-fidelity step;
      see *Layout fidelity*).
- [ ] **Table auto column sizing + `border-collapse`** (still even columns).
- [ ] **`%` margins/padding** and `border-radius` (see *Layout fidelity* / *CSS*).
- [ ] **CGI: persistent-coprocess option** for in-memory state (re-spawn POST
      already works).
- [ ] **Temp-file cleanup** for fetched/`data:` images (cached per page but
      currently leak until exit).
- [ ] **Paint-only hover recolour** — `:hover` currently re-resolves + relays
      out on link enter/leave; a recolor-without-relayout path would remove the
      hitch on large pages.

## Done
- [x] CSS: `%` widths (and `%`/px heights), `margin: auto` centering,
      `background-image` (tiled), per-side border styles (solid/dashed/dotted/
      double).
- [x] CGI binary mode: responses dispatched by Content-Type (html/text/image/
      other-binary), `Content-Length` framing, binary save+info page.
- [x] Image resource fetching: `data:` URLs (base64/percent) and CGI-served
      images materialised to local files; `<base href>` for relative resolution.
- [x] Real `<select>` dropdown (inline overlay, option selection); `<input
      type=image>` as an image-labelled submit.
- [x] Table `colspan` / `rowspan` (grid-occupancy model).
- [x] External `<link rel=stylesheet>` CSS; `--refresh` flag and
      `<meta http-equiv=refresh>`.
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
- [x] `%` widths (and `%`/px heights); `margin: auto` horizontal centering.
- [ ] `%` margins/padding (resolve against containing-block width).
- [ ] `line-height` property (today derived purely from font metrics).
- [ ] Inline `vertical-align` (baseline/middle/top) for images and mixed fonts.
- [ ] Floats (`float`/`clear`) and positioning (`relative`/`absolute`) — parked.

## CSS coverage
- [x] `border-style` (`solid/dashed/dotted/double`); `background-image` (tiled)
      + `url()` in the `background` shorthand.
- [ ] `font` shorthand.
- [ ] `border-radius` (rounded corners + bg clipping).
- [ ] `background-repeat` / `background-position` (currently always tiled).
- [ ] Child / sibling combinators (`>`, `+`, `~`) and attribute selectors.

## Parsing / robustness
- [ ] Larger named-entity table (currently a curated subset).
- [ ] `<tbody>` / `<thead>` / `<tfoot>` wrappers in tables.
- [ ] Graceful handling of unknown block-vs-inline display defaults.

## Rendering / perf
- [ ] Glyph fallback for missing code points (some bullets/symbols show tofu in
      fonts lacking them; shape bullets already sidestep this).
- [ ] Incremental repaint of only the exposed/dirty region.
- [ ] Decoded-image cache (resolved *paths* are cached, but each image is still
      opened twice via Imlib2 — once to size, once to paint).

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
  - [x] Real `<select>` (inline-overlay dropdown, option selection). `multiple`
        and a true popup window are still TODO.
  - [x] `<input type=image>` — minimal (image-labelled submit). `type=file`
        (needs a file chooser) is still TODO.
  - [~] `<isindex>` single-field search prompt — **won't do** (obsolete; a
        one-field `<form>` covers the same need).
- [ ] **Head elements**:
  - [x] **`<link rel=stylesheet href>`** — external CSS (file dir, or CGI
        requests); honoured by the window and the render/dump paths.
  - [x] **`<base href>`** — overrides the dir used to resolve relative
        links/srcs (file and CGI modes).
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

## Bugs

- [ ] **Out-of-bounds read in HTML parser** (`src/html_parser.cpp:33`). The parser
      accesses `s[pos + 1]` without bounds-checking; if input ends with a bare `<`,
      this reads past the end of the string (undefined behaviour). Fix: add `pos + 1 < n &&`
      guard before the access.

- [ ] **BackSpace corrupts multi-byte UTF-8** (`src/window.cpp:857`). In text inputs,
      `text.pop_back()` removes one **byte** instead of one Unicode code point. For
      multi-byte UTF-8 characters (2–4 bytes), this leaves behind invalid continuation
      bytes. Fix: walk backward from the end, skipping all bytes in the range `0x80–0xBF`,
      then erase; this removes a complete code point.

- [ ] **Form reset wipes all forms' state** (`src/window.cpp:811`). When resetting
      a `<form>`, the code calls `page.form.clear()`, which destroys widget state for
      **every** form on the page. Only the target form is re-seeded via
      `init_form_state()`, so other forms permanently lose their in-progress values.
      Fix: selectively erase only the elements that belong to the target `<form>` by
      walking its subtree and erasing those keys from the map.

- [ ] **`size_t` underflow in pre-formatted text** (`src/layout.cpp:193`, `src/paint.cpp:296`).
      When computing `nl - start` where `nl == std::string::npos`, the subtraction wraps
      to a huge `size_t`; `substr()` silently clamps it to the remaining length, so output
      happens to be correct but the code is fragile and confusing. Fix: pass `npos` directly
      to `substr()` for "rest of string" when no newline was found, or check for `npos`
      before computing the length.

- [ ] **Segfault loading images in the `make static` build**. The fully static
      binary (see `make static`) crashes on image loads that work fine in the
      dynamic build. Likely an Imlib2 loader-module issue — Imlib2 normally
      `dlopen()`s its format loaders (PNG/JPEG/etc.) from a shared-lib path at
      runtime, which doesn't exist/resolve the same way once everything is
      statically linked. Needs investigation: either force Imlib2's built-in
      loaders to be linked in directly, or find another way to make format
      loading work without dlopen in the static binary.
