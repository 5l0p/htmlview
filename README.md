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
htmlview --cgi-bin "PROGRAM" [/start] # run PROGRAM as a CGI backend (see below)
```

### CGI mode

`--cgi-bin "PROGRAM"` turns htmlview into a thin client for a local backend:
the program is run as a **classic CGI script** (re-spawned per request, via
`/bin/sh -c`), with `REQUEST_METHOD`, `PATH_INFO`, `QUERY_STRING`, etc. in the
environment. Its stdout (an optional `Content-Type`/`Status`/`Location` header
block, then the body) is rendered. Link clicks issue GET requests to the
program; form submits serialize named fields into the query string. Because each
request is a fresh process, the backend persists its own state (a file, a db).
See `test/counter.cgi` for a worked example:

```sh
htmlview --cgi-bin "python3 test/counter.cgi" /
```

Window keys: arrows / PageUp / PageDown / Home / End and the mouse wheel scroll;
`Alt+Left` / `Alt+Right` (or `Backspace` / `Shift+Backspace`) go back / forward;
`q` / `Esc` quit. Clicking a link to a **local** file navigates to it (the
pointer shows a hand cursor over links); external `http(s)` links are not followed.

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
  `a img`, `ul ol li`, `b i em strong`, `blockquote address dl dt dd menu dir`,
  `code samp kbd var cite tt u sub sup`, `table tr td th`, and form controls
  `form input button textarea select` (text inputs, checkboxes, buttons are
  interactive; select is a static box; radios render as checkboxes).
- **Navigation:** local links, `#fragment` anchors (`id` / `<a name>`), and
  back/forward history.
- **Entities:** the full ISO 8859-1 (Latin-1) named set, common punctuation, and
  numeric `&#nnn;` / `&#xhh;` references. `<img alt>` shown when an image fails.
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
