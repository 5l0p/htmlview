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

// Headless operation on `target` (a file path, or a request path in CGI mode):
// dump the DOM and/or computed styles to stdout, and/or rasterise to a PNG at
// `render_to`. `cgi_program` empty -> file mode. Returns 0 on success.
int run_headless(const std::string& target, const std::string& cgi_program, int width,
                 const std::string& render_to, bool dump_dom, bool dump_style);

}  // namespace hv
