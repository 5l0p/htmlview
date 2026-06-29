// HTML parser for the htmlview subset.
//
// Targets well-formed, app-authored markup — best-effort on surprises, no full
// browser error recovery. Handles void elements, self-closing syntax, implicit
// close for p/li/tr/td/th, and raw-text content for <style>/<title>.
#pragma once

#include <memory>
#include <string>

#include "dom.h"

namespace hv {

struct Document {
    std::unique_ptr<Element> root;  // synthetic "#document" root
    std::string title;              // text content of <title>, if any
    std::string css;                // concatenated text of all <style> blocks
    std::string base_dir;           // directory of the source file, for <img src>
};

Document parse_html(const std::string& src);

}  // namespace hv
