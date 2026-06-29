// htmlview — app-specific X11/Xft/XRender/Imlib2 HTML renderer.
//
// Usage:
//   htmlview <file.html>                 open a window and render
//   htmlview --render-to out.png <file>  headless: rasterise to a PNG
//   htmlview --dump-dom / --dump-style   print the DOM / computed styles
//   htmlview --cgi-bin "PROG" [/start]   render a CGI backend's output
//
// The headless flags (--render-to / --dump-*) compose with --cgi-bin: they act
// on the CGI program's response instead of a file.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "view.h"

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: htmlview [--dump-dom] [--dump-style] [--render-to FILE.png]\n"
                 "                [--width N] [--refresh SECONDS]\n"
                 "                [--cgi-bin PROGRAM] [PATH | START_PATH]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = nullptr;
    const char* render_to = nullptr;
    const char* cgi = nullptr;
    bool dump_dom = false, dump_style = false;
    int width = 800;
    int refresh = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-dom") == 0) dump_dom = true;
        else if (std::strcmp(argv[i], "--dump-style") == 0) dump_style = true;
        else if (std::strcmp(argv[i], "--render-to") == 0 && i + 1 < argc) render_to = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--cgi-bin") == 0 && i + 1 < argc) cgi = argv[++i];
        else if (std::strcmp(argv[i], "--refresh") == 0 && i + 1 < argc) refresh = std::atoi(argv[++i]);
        else if (argv[i][0] == '-') { std::fprintf(stderr, "unknown flag: %s\n", argv[i]); return usage(); }
        else path = argv[i];
    }

    bool headless = dump_dom || dump_style || render_to;

    if (headless) {
        // File mode needs a path; CGI mode defaults the start path to "/".
        if (!cgi && !path) return usage();
        std::string target = path ? path : "/";
        return hv::run_headless(target, cgi ? cgi : "", width,
                                render_to ? render_to : "", dump_dom, dump_style);
    }

    // Live window: CGI start path defaults to "/", file mode requires a path.
    if (cgi) return hv::run_window(path ? path : "/", cgi, refresh);
    if (!path) return usage();
    return hv::run_window(path, "", refresh);
}
