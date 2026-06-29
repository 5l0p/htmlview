// htmlview — app-specific X11/Xft/XRender/Imlib2 HTML renderer.
//
// Usage:
//   htmlview <file.html>            open a window and render
//   htmlview --dump-dom <file>      print the parsed DOM tree and exit
//
// More flags (--dump-style, --render-to) arrive with later milestones.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "css.h"
#include "dom.h"
#include "html_parser.h"
#include "style.h"
#include "view.h"

namespace {

bool read_file(const char* path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int usage() {
    std::fprintf(stderr,
                 "usage: htmlview [--dump-dom] [--dump-style] "
                 "[--render-to FILE.png] [--width N] <file.html>\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = nullptr;
    const char* render_to = nullptr;
    bool dump_dom = false, dump_style = false;
    int width = 800;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-dom") == 0) dump_dom = true;
        else if (std::strcmp(argv[i], "--dump-style") == 0) dump_style = true;
        else if (std::strcmp(argv[i], "--render-to") == 0 && i + 1 < argc) render_to = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (argv[i][0] == '-') { std::fprintf(stderr, "unknown flag: %s\n", argv[i]); return usage(); }
        else path = argv[i];
    }
    if (!path) return usage();

    std::string src;
    if (!read_file(path, src)) {
        std::fprintf(stderr, "htmlview: cannot read %s\n", path);
        return 1;
    }

    hv::Document doc = hv::parse_html(src);
    {
        std::string p = path;
        size_t slash = p.find_last_of('/');
        doc.base_dir = (slash == std::string::npos) ? "." : p.substr(0, slash);
    }

    if (dump_dom) {
        std::string out;
        hv::dump_dom(doc.root.get(), out);
        std::fputs(out.c_str(), stdout);
        std::printf("--- title: \"%s\"\n", doc.title.c_str());
        std::printf("--- css: %zu bytes\n", doc.css.size());
        return 0;
    }

    std::vector<hv::Rule> rules = hv::parse_css(doc.css);
    hv::StyleMap styles = hv::resolve_styles(doc, rules);

    if (dump_style) {
        std::printf("--- %zu CSS rules parsed\n", rules.size());
        std::string out;
        hv::dump_styles(doc.root.get(), styles, out);
        std::fputs(out.c_str(), stdout);
        return 0;
    }

    if (render_to)
        return hv::render_to_png(doc, styles, width, render_to);

    return hv::run_window(path);
}
