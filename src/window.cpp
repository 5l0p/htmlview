// X11 presentation layer: live window + headless PNG export.

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <Imlib2.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "css.h"
#include "font.h"
#include "html_parser.h"
#include "layout.h"
#include "paint.h"
#include "style.h"
#include "view.h"

namespace hv {

namespace {

// A loaded document plus everything derived from it. StyleMap keys point into
// doc's node tree, so the two must live and move together.
struct Page {
    Document doc;
    std::vector<Rule> rules;
    StyleMap styles;
    std::string title;
    bool has_hover = false;  // stylesheet uses :hover -> track pointer for restyle
    bool ok = false;
};

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

Page load_page(const std::string& path) {
    Page pg;
    std::string src;
    if (!read_file(path, src)) {
        std::fprintf(stderr, "htmlview: cannot read %s\n", path.c_str());
        return pg;
    }
    pg.doc = parse_html(src);
    size_t slash = path.find_last_of('/');
    pg.doc.base_dir = (slash == std::string::npos) ? "." : path.substr(0, slash);
    pg.rules = parse_css(pg.doc.css);
    pg.has_hover = stylesheet_has_hover(pg.rules);
    pg.styles = resolve_styles(pg.doc, pg.rules);
    pg.title = pg.doc.title.empty() ? path : pg.doc.title;
    pg.ok = true;
    return pg;
}

// Deepest box under (px,py) in document coords that carries a link href.
const LayoutBox* find_link(const LayoutBox* box, float px, float py) {
    const LayoutBox* found = nullptr;
    if (!box->href.empty() && px >= box->x && px < box->x + box->width &&
        py >= box->y && py < box->y + box->height)
        found = box;
    for (const auto& c : box->children)
        if (const LayoutBox* r = find_link(c.get(), px, py)) found = r;
    return found;
}

bool file_exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

}  // namespace

int run_window(const std::string& path) {
    Page page = load_page(path);
    if (!page.ok) return 1;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { std::fprintf(stderr, "htmlview: cannot open X display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Visual* visual = DefaultVisual(dpy, screen);
    Colormap cmap = DefaultColormap(dpy, screen);
    int depth = DefaultDepth(dpy, screen);
    unsigned long white = WhitePixel(dpy, screen);

    int width = 800, height = 600;
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, width, height,
                                     0, white, white);
    XStoreName(dpy, win, page.title.c_str());
    XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | KeyPressMask |
                               ButtonPressMask | PointerMotionMask);
    XMapWindow(dpy, win);

    imlib_context_set_display(dpy);
    imlib_context_set_visual(visual);
    imlib_context_set_colormap(cmap);

    Cursor hand = XCreateFontCursor(dpy, XC_hand2);
    bool hand_active = false;

    FontCache fonts(dpy, screen);
    GC gc = XCreateGC(dpy, win, 0, nullptr);

    Pixmap buffer = XCreatePixmap(dpy, win, width, height, depth);
    std::unique_ptr<LayoutBox> root;
    float scroll_y = 0, doc_height = 0;
    const Element* hovered = nullptr;

    std::vector<std::string> history{path};  // resolved paths visited
    size_t hindex = 0;                        // current position in history

    auto relayout = [&]() {
        root = layout_document(page.doc, page.styles, fonts, (float)width);
        doc_height = root ? root->height : 0;
    };
    // Re-run the cascade with the current hover state, then relayout (layout
    // boxes hold pointers into the StyleMap, so it must be rebuilt wholesale).
    auto restyle_for_hover = [&]() {
        page.styles = resolve_styles(page.doc, page.rules, hovered);
        relayout();
    };
    auto repaint = [&]() {
        Painter p(dpy, buffer, visual, cmap, fonts);
        p.paint(root.get(), width, height, 0, scroll_y);
        XCopyArea(dpy, buffer, win, gc, 0, 0, width, height, 0, 0);
    };
    auto clamp_scroll = [&]() {
        float max_scroll = std::max(0.0f, doc_height - height);
        scroll_y = std::clamp(scroll_y, 0.0f, max_scroll);
    };
    // Load `target` into the view (no history change). Returns success.
    auto load_into = [&](const std::string& target) -> bool {
        Page np = load_page(target);
        if (!np.ok) return false;
        page = std::move(np);
        XStoreName(dpy, win, page.title.c_str());
        scroll_y = 0;
        hovered = nullptr;
        relayout();
        clamp_scroll();
        repaint();
        return true;
    };
    auto navigate = [&](const std::string& href) {
        if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
            std::fprintf(stderr, "htmlview: external link not followed: %s\n", href.c_str());
            return;
        }
        std::string target = (href[0] == '/') ? href : page.doc.base_dir + "/" + href;
        if (!file_exists(target)) {
            std::fprintf(stderr, "htmlview: link target not found: %s\n", target.c_str());
            return;
        }
        if (!load_into(target)) return;
        history.resize(hindex + 1);  // drop any forward history
        history.push_back(target);
        hindex = history.size() - 1;
    };
    auto go = [&](int delta) {
        long ni = (long)hindex + delta;
        if (ni < 0 || ni >= (long)history.size()) return;
        if (load_into(history[ni])) hindex = (size_t)ni;
    };

    relayout();

    bool running = true;
    while (running) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) repaint();
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != width || ev.xconfigure.height != height) {
                    width = ev.xconfigure.width;
                    height = ev.xconfigure.height;
                    XFreePixmap(dpy, buffer);
                    buffer = XCreatePixmap(dpy, win, width, height, depth);
                    relayout();
                    clamp_scroll();
                    repaint();
                }
                break;
            case MotionNotify: {
                const LayoutBox* link = root ? find_link(root.get(), ev.xmotion.x,
                                                          ev.xmotion.y + scroll_y) : nullptr;
                bool over = link != nullptr;
                if (over != hand_active) {
                    XDefineCursor(dpy, win, over ? hand : None);
                    hand_active = over;
                }
                // Restyle on link enter/leave so :hover rules take effect.
                const Element* now = link ? link->element : nullptr;
                if (page.has_hover && now != hovered) {
                    hovered = now;
                    restyle_for_hover();
                    repaint();
                }
                break;
            }
            case ButtonPress:
                if (ev.xbutton.button == 4) { scroll_y -= 48; clamp_scroll(); repaint(); }
                else if (ev.xbutton.button == 5) { scroll_y += 48; clamp_scroll(); repaint(); }
                else if (ev.xbutton.button == 1 && root) {
                    if (const LayoutBox* link = find_link(root.get(), ev.xbutton.x,
                                                          ev.xbutton.y + scroll_y))
                        navigate(link->href);
                }
                break;
            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                bool alt = ev.xkey.state & Mod1Mask;
                bool shift = ev.xkey.state & ShiftMask;
                if (ks == XK_q || ks == XK_Escape) running = false;
                else if (alt && ks == XK_Left) go(-1);    // back
                else if (alt && ks == XK_Right) go(+1);   // forward
                else if (ks == XK_BackSpace) go(shift ? +1 : -1);
                else if (ks == XK_Down) { scroll_y += 48; clamp_scroll(); repaint(); }
                else if (ks == XK_Up) { scroll_y -= 48; clamp_scroll(); repaint(); }
                else if (ks == XK_Page_Down) { scroll_y += height - 40; clamp_scroll(); repaint(); }
                else if (ks == XK_Page_Up) { scroll_y -= height - 40; clamp_scroll(); repaint(); }
                else if (ks == XK_Home) { scroll_y = 0; repaint(); }
                else if (ks == XK_End) { scroll_y = doc_height; clamp_scroll(); repaint(); }
                break;
            }
        }
    }

    XFreeCursor(dpy, hand);
    XFreePixmap(dpy, buffer);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    fonts.clear();  // close fonts before the Display does
    XCloseDisplay(dpy);
    return 0;
}

int render_to_png(const Document& doc, const StyleMap& styles, int width,
                  const std::string& path) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { std::fprintf(stderr, "htmlview: cannot open X display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Visual* visual = DefaultVisual(dpy, screen);
    Colormap cmap = DefaultColormap(dpy, screen);
    int depth = DefaultDepth(dpy, screen);

    imlib_context_set_display(dpy);
    imlib_context_set_visual(visual);
    imlib_context_set_colormap(cmap);

    FontCache fonts(dpy, screen);
    auto root = layout_document(doc, styles, fonts, (float)width);
    int height = std::max(1, (int)std::ceil(root ? root->height : 1));

    Pixmap buffer = XCreatePixmap(dpy, RootWindow(dpy, screen), width, height, depth);
    {
        Painter p(dpy, buffer, visual, cmap, fonts);
        p.paint(root.get(), width, height, 0, 0);
    }
    XSync(dpy, False);

    imlib_context_set_drawable(buffer);
    Imlib_Image img = imlib_create_image_from_drawable(0, 0, 0, width, height, 1);
    if (!img) {
        std::fprintf(stderr, "htmlview: imlib capture failed\n");
        XFreePixmap(dpy, buffer);
        XCloseDisplay(dpy);
        return 1;
    }
    imlib_context_set_image(img);
    imlib_image_set_format("png");
    imlib_save_image(path.c_str());
    imlib_free_image();

    std::fprintf(stderr, "wrote %s (%dx%d)\n", path.c_str(), width, height);
    XFreePixmap(dpy, buffer);
    fonts.clear();  // close fonts before the Display does
    XCloseDisplay(dpy);
    return 0;
}

}  // namespace hv
