# htmlview

A small, **application-specific** HTML/CSS renderer built directly on X11, Xft,
XRender and Imlib2 — no browser engine, no web stack. It renders a practical
subset of HTML (roughly the early-core + HTML 2.0 element set, plus tables and a
CSS1-flavoured style engine) for app-authored markup that also renders correctly
in a real browser.

Beyond a static viewer, it doubles as a tiny **application runtime**: point it at
a local **CGI** program (`--cgi-bin`) and it becomes a thin client that renders
the program's HTML, follows links, submits forms (GET/POST), and displays images
the backend serves — a lightweight, dependency-free alternative to a webview.

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
htmlview --refresh N <file>           # reload the *current* page every N seconds
```

External stylesheets via `<link rel=stylesheet href>` are loaded (relative to the
document's directory, or as further requests in CGI mode). A page's
`<meta http-equiv="refresh" content="N; url=...">` is honoured and overrides
`--refresh`. `--refresh` always reloads whatever page is *currently* shown (so it
follows navigation, not just the initial page).

### CGI mode

`--cgi-bin "PROGRAM"` turns htmlview into a thin client for a local backend:
the program is run as a **classic CGI script** (re-spawned per request, via
`/bin/sh -c`), with `REQUEST_METHOD`, `PATH_INFO`, `QUERY_STRING`, etc. in the
environment. Its stdout (an optional `Content-Type`/`Status`/`Location` header
block, then the body) is rendered. Link clicks issue GET requests to the
program; form submits serialize named fields into the query string. Because each
request is a fresh process, the backend persists its own state (a file, a db).
Responses are dispatched by `Content-Type`, like a real CGI gateway: `text/html`
renders as a page, other `text/*` as `<pre>`, `image/*` is displayed, and any
other binary is saved to a temp file with an info page (the body is framed by
`Content-Length` when the script sends it). See `test/counter.cgi` and
`test/binary.cgi` for worked examples:

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
| `html_parser.{h,cpp}` | Tokeniser + tree builder (void elements, implicit close, raw-text `<style>`/`<title>`, full Latin-1 + numeric entities) |
| `css.{h,cpp}`         | Selector + declaration parser (type/class/id/descendant/grouping, `:hover`, specificity) |
| `style.{h,cpp}`       | UA defaults + cascade + inheritance → `ComputedStyle`; colour/length (`Length`: px/%/auto) parsing; `<link>` CSS gathering |
| `font.{h,cpp}`        | Xft font cache (fontconfig family fallback) + text measurement |
| `layout.{h,cpp}`      | Block flow (box model, % widths, `margin:auto`, margin collapsing) + inline flow (word wrap, sub/sup) + list markers + images + form widgets + tables (colspan/rowspan) |
| `paint.{h,cpp}`       | XRender fills, styled borders, tiled backgrounds, Xft text, drawn bullets, form controls + dropdowns, Imlib2 images |
| `cgi.{h,cpp}`         | Classic-CGI request runner (fork/exec, env, header parse, `Content-Length`) |
| `window.cpp`          | Live X11 window (scroll/resize/nav/forms), file & CGI page loaders, headless PNG export |

## Supported subset

- **Elements:** `html head body title style`, `h1`–`h6`, `p div span pre br hr`,
  `a img`, `ul ol li`, `b i em strong`, `blockquote address dl dt dd menu dir`,
  `code samp kbd var cite tt u sub sup`, `table tr td th`, and form controls
  `form input button textarea select` — text fields (incl. `password` masking),
  checkboxes, radio groups, submit/reset/plain buttons, `<select>` (click for a
  dropdown), and multi-line `textarea` are all interactive; forms submit via GET
  (query string) or POST (CGI, body on stdin). `Tab`/`Shift+Tab` cycle fields.
- **Navigation:** local links, `#fragment` anchors (`id` / `<a name>`), and
  back/forward history.
- **Entities:** the full ISO 8859-1 (Latin-1) named set, common punctuation, and
  numeric `&#nnn;` / `&#xhh;` references. `<img alt>` shown when an image fails.
- **Images:** local files (relative to the doc / `<base href>`), `data:` URLs
  (base64 or percent-encoded), and — in CGI mode — images fetched from the backend.
- **CSS properties:** `display`, `color`, `background-color`, `background-image`
  (tiled), `font-family/size/weight/style`, `text-align`, `text-decoration`,
  `list-style-type`, `margin*` (incl. `auto` for centering), `padding*`,
  `border` + per-side `border-top/right/bottom/left` with `solid/dashed/dotted/
  double` styles, `width`/`height` in px **or `%`**.
- Tolerates the legacy `<style><!-- ... --></style>` SGML-comment wrapper.
- **CSS selectors:** type, `.class`, `#id`, descendant (`div p`), grouping (`h1, h3`),
  the `:hover` pseudo-class, plus inline `style=""`. Specificity + source-order
  cascade with inheritance. (`:visited` is parsed but inert — no history.)
- **Colours:** `#rgb`, `#rrggbb`, `rgb()`, `transparent`, common named colours.
- **Lengths:** `px`, `em`, `pt`, `%`, `auto`, unitless.
- **Head elements:** `<base href>`, `<link rel=stylesheet>` (external CSS),
  `<meta http-equiv=refresh>`. Adjacent-sibling vertical margins collapse.

## Known limitations

- No floats / positioning (`relative`/`absolute`) / z-index — normal flow only.
- Tables have colspan/rowspan but **even column widths** (no auto min/max sizing,
  no `border-collapse`).
- No `line-height`, `vertical-align`, `border-radius`, or `%` margins/padding yet.
- `<select multiple>` and `<input type=file>` not supported; `type=image` is a
  plain submit.
- Latin/Western text only (Xft; no HarfBuzz shaping or bidi); no scripting.
- Fetched/`data:` images write temp files that are not cleaned up until exit.

See `TODO.md` for the roadmap (including the HTML 1.x / 2.0 compliance status).
