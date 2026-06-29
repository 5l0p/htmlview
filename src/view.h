// X11 presentation: live window and headless PNG export. Both build their own
// X connection + font cache, lay the document out, and paint it.
#pragma once

#include <string>

#include "html_parser.h"
#include "style.h"

namespace hv {

// Open a live window. `target0` is the initial document: a file path in file
// mode, or a request path (e.g. "/") in CGI mode. If `cgi_program` is non-empty
// the viewer runs it (classic CGI, re-spawned per request) for navigation and
// GET form submissions instead of reading files. `refresh_seconds` > 0 reloads
// the *current* page on that interval (a page's own <meta refresh> overrides it).
int run_window(const std::string& target0, const std::string& cgi_program = "",
               int refresh_seconds = 0);

// Render the document at `width` px into a PNG via Imlib2. Returns 0 on success.
int render_to_png(const Document& doc, const StyleMap& styles, int width,
                  const std::string& path);

}  // namespace hv
