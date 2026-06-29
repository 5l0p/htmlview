# htmlview

A small, **application-specific** HTML/CSS renderer built directly on X11, Xft,
XRender and Imlib2 — no browser engine, no web stack. It targets a controlled
subset of HTML 3.2 + a pragmatic slice of CSS, for displaying app-authored
markup (which must also render correctly in a real browser).

## Build

Pure Makefile, C++17. Requires dev packages for `x11 xft xrender imlib2 fontconfig`.

```sh
make            # builds ./htmlview
make run        # builds and opens test/kitchen-sink.html
make clean
```

## Usage

```sh
htmlview <file.html>                  # open a window and render
htmlview --width 1000 <file.html>     # initial viewport width
htmlview --render-to out.png <file>   # headless: rasterise to a PNG (Imlib2)
htmlview --dump-dom   <file>          # print the parsed DOM tree
htmlview --dump-style <file>          # print resolved computed styles
```

Window keys: arrows / PageUp / PageDown and the mouse wheel scroll; `q` / `Esc` quit.

## Pipeline

```
HTML ─▶ html_parser ─▶ DOM ─┐
CSS  ─▶ css ─▶ rules ───────┴▶ style (cascade) ─▶ layout (box tree) ─▶ paint
```

| File | Responsibility |
|------|----------------|
| `dom.{h,cpp}`         | Element/Text node tree, tag enum |
| `html_parser.{h,cpp}` | Tokeniser + tree builder (void elements, implicit close, raw-text `<style>`/`<title>`, entities) |
| `css.{h,cpp}`         | Selector + declaration parser (type/class/id/descendant/grouping, specificity) |
| `style.{h,cpp}`       | UA defaults + cascade + inheritance → `ComputedStyle`; colour/length parsing |
| `font.{h,cpp}`        | Xft font cache + text measurement |
| `layout.{h,cpp}`      | Block (BFC) + inline (IFC, word wrap) + list markers + images + tables |
| `paint.{h,cpp}`       | XRender fills (bg/borders), Xft text, drawn bullets, Imlib2 images |
| `window.cpp`          | Live X11 window (scroll/resize) and headless PNG export |

## Supported subset

- **Elements:** `html head body title style`, `h1`–`h6`, `p div span pre br hr`,
  `a img`, `ul ol li`, `b i em strong`, `table tr td th`.
- **CSS properties:** `display`, `color`, `background-color`, `font-family/size/weight/style`,
  `text-align`, `text-decoration`, `list-style-type`, `margin*`, `padding*`,
  `border` + per-side `border-top/right/bottom/left` (solid), `width`, `height`.
- Tolerates the legacy `<style><!-- ... --></style>` SGML-comment wrapper.
- **CSS selectors:** type, `.class`, `#id`, descendant (`div p`), grouping (`h1, h3`),
  plus inline `style=""`. Specificity + source-order cascade with inheritance.
- **Colours:** `#rgb`, `#rrggbb`, `rgb()`, common named colours.
- **Lengths:** `px`, `em`, `pt`, unitless.

## Known limitations (by design, for v1)

- No vertical margin collapsing; no floats / positioning / z-index.
- Tables: even column widths only — no colspan/rowspan, no auto min/max sizing.
- No forms/interactivity yet (hit-testing + link `href` plumbing is in place for it).
- Latin/Western text only (Xft; no HarfBuzz shaping or bidi).
