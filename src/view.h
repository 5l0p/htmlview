// X11 presentation: live window and headless PNG export. Both build their own
// X connection + font cache, lay the document out, and paint it.
#pragma once

#include <string>

#include "html_parser.h"
#include "style.h"

namespace hv {

int run_window(const Document& doc, const StyleMap& styles, const std::string& title);

// Render the document at `width` px into a PNG via Imlib2. Returns 0 on success.
int render_to_png(const Document& doc, const StyleMap& styles, int width,
                  const std::string& path);

}  // namespace hv
