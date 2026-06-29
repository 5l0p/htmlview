// X11 presentation layer: live window + headless PNG export.

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <Imlib2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "cgi.h"
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
    FormState form;         // mutable control state, keyed by Element
    std::string title;
    bool has_hover = false;  // stylesheet uses :hover -> track pointer for restyle
    bool ok = false;
};

std::string node_text(const Node* n) {
    if (n->is_text()) return static_cast<const Text*>(n)->data;
    std::string out;
    for (const auto& c : static_cast<const Element*>(n)->children) out += node_text(c.get());
    return out;
}

void init_form_state(const Node* n, FormState& fs) {
    if (n->is_element()) {
        const auto* e = static_cast<const Element*>(n);
        if (e->tag == Tag::Input) {
            std::string type = e->attr("type") ? *e->attr("type") : "text";
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (type == "checkbox" || type == "radio")
                fs[e].checked = e->attr("checked") != nullptr;
            else if (type != "button" && type != "submit" && type != "reset" && type != "hidden")
                fs[e].text = e->attr("value") ? *e->attr("value") : "";
        } else if (e->tag == Tag::Textarea) {
            std::string t = node_text(e);
            size_t a = t.find_first_not_of(" \t\r\n");
            fs[e].text = (a == std::string::npos) ? "" : t.substr(a);
        }
        for (const auto& c : e->children) init_form_state(c.get(), fs);
    }
}

// Deepest widget box under (px,py) in document coords.
const LayoutBox* find_widget(const LayoutBox* box, float px, float py) {
    const LayoutBox* found = nullptr;
    if (box->widget != WidgetKind::NoWidget && px >= box->x && px < box->x + box->width &&
        py >= box->y && py < box->y + box->height)
        found = box;
    for (const auto& c : box->children)
        if (const LayoutBox* r = find_widget(c.get(), px, py)) found = r;
    return found;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Build a Page from HTML source (shared by the file and CGI loaders).
Page build_page(std::string src, std::string base_dir, const std::string& title_fallback) {
    Page pg;
    pg.doc = parse_html(src);
    pg.doc.base_dir = std::move(base_dir);
    pg.rules = parse_css(pg.doc.css);
    pg.has_hover = stylesheet_has_hover(pg.rules);
    pg.styles = resolve_styles(pg.doc, pg.rules);
    init_form_state(pg.doc.root.get(), pg.form);
    pg.title = pg.doc.title.empty() ? title_fallback : pg.doc.title;
    pg.ok = true;
    return pg;
}

std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

// A generic, self-contained error page shown in the window so failures are
// visible directly rather than only on stderr.
Page make_error_page(const std::string& heading, const std::string& detail) {
    std::string html =
        "<title>" + html_escape(heading) + "</title>"
        "<style>"
        "  body { font-family: sans-serif; margin: 28px; background: #ffffff; color: #222; }"
        "  .err { border: 2px solid #cc3333; background: #fff0f0; padding: 14px 18px; }"
        "  h1 { color: #cc3333; font-size: 22px; margin: 0 0 8px 0; }"
        "  code { background: #f2f2f2; padding: 1px 3px; }"
        "</style>"
        "<body><div class=\"err\"><h1>" + html_escape(heading) + "</h1>"
        "<p><code>" + html_escape(detail) + "</code></p></div></body>";
    Page pg = build_page(std::move(html), "", heading);
    return pg;
}

Page load_file_page(const std::string& path) {
    std::string src;
    if (!read_file(path, src)) {
        std::fprintf(stderr, "htmlview: cannot read %s\n", path.c_str());
        return make_error_page("Page not found", path + " could not be opened");
    }
    size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? "." : path.substr(0, slash);
    return build_page(std::move(src), base, path);
}

// Run a CGI request for `target` ("path?query") and build a Page from the
// response, following Location: redirects. base_dir is empty (no local assets).
Page load_cgi_page(const std::string& program, const std::string& target) {
    std::string t = target;
    for (int hop = 0; hop < 8; ++hop) {
        std::string path = t, query;
        if (size_t q = t.find('?'); q != std::string::npos) {
            path = t.substr(0, q);
            query = t.substr(q + 1);
        }
        if (path.empty()) path = "/";
        CgiResponse r = run_cgi(program, "GET", path, query);
        if (!r.ok)
            return make_error_page("CGI error",
                                   (r.error.empty() ? "request failed" : r.error) +
                                       " (" + program + ")");
        if (!r.location.empty() && (r.status / 100) == 3) { t = r.location; continue; }
        std::string ct = r.content_type;
        std::string body = r.body;
        if (ct.rfind("text/plain", 0) == 0)
            body = "<pre>" + html_escape(body) + "</pre>";
        return build_page(std::move(body), "", path);
    }
    return build_page("<title>CGI</title><p>too many redirects</p>", "", "CGI");
}

// --- form serialization -------------------------------------------------

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else if (c == ' ') out += '+';
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 0xF]; }
    }
    return out;
}

const Element* enclosing_form(const Element* e) {
    for (const Node* n = e; n; n = n->parent)
        if (n->is_element() && static_cast<const Element*>(n)->tag == Tag::Form)
            return static_cast<const Element*>(n);
    return nullptr;
}

void gather_fields(const Node* n, const FormState& fs, const Element* clicked,
                   std::string& out) {
    if (!n->is_element()) return;
    const auto* e = static_cast<const Element*>(n);
    const std::string* name = e->attr("name");
    auto add = [&](const std::string& key, const std::string& val) {
        if (!out.empty()) out += "&";
        out += url_encode(key) + "=" + url_encode(val);
    };
    if (name) {
        auto val_attr = [&](const char* def) {
            const std::string* v = e->attr("value");
            return v ? *v : std::string(def);
        };
        if (e->tag == Tag::Input) {
            std::string type = e->attr("type") ? *e->attr("type") : "text";
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (type == "checkbox" || type == "radio") {
                auto it = fs.find(e);
                if (it != fs.end() && it->second.checked) add(*name, val_attr("on"));
            } else if (type == "submit" || type == "button") {
                if (e == clicked) add(*name, val_attr(""));  // only the clicked submit
            } else if (type == "hidden") {
                add(*name, val_attr(""));
            } else {  // text-like
                auto it = fs.find(e);
                add(*name, it != fs.end() ? it->second.text : val_attr(""));
            }
        } else if (e->tag == Tag::Textarea) {
            auto it = fs.find(e);
            add(*name, it != fs.end() ? it->second.text : "");
        } else if (e->tag == Tag::Select) {
            for (const auto& c : e->children)
                if (c->is_element() &&
                    static_cast<const Element*>(c.get())->tag == Tag::Option) {
                    const auto* o = static_cast<const Element*>(c.get());
                    const std::string* v = o->attr("value");
                    std::string label;
                    for (const auto& cc : o->children)
                        if (cc->is_text()) label += static_cast<const Text*>(cc.get())->data;
                    add(*name, v ? *v : label);
                    break;  // first option (no selection tracking yet)
                }
        }
    }
    for (const auto& c : e->children) gather_fields(c.get(), fs, clicked, out);
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

}  // namespace

int run_window(const std::string& target0, const std::string& cgi_program) {
    bool cgi_mode = !cgi_program.empty();
    auto load_target = [&](const std::string& target) -> Page {
        return cgi_mode ? load_cgi_page(cgi_program, target) : load_file_page(target);
    };

    Page page = load_target(target0);
    if (!page.ok) return 1;
    std::string current_target = target0;

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
    const Element* focused = nullptr;  // focused text-input element

    std::vector<std::string> history{target0};  // targets visited
    size_t hindex = 0;                           // current position in history

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
        FormView fv{&page.form, focused, focused != nullptr};
        p.paint(root.get(), width, height, 0, scroll_y, fv);
        XCopyArea(dpy, buffer, win, gc, 0, 0, width, height, 0, 0);
    };
    auto clamp_scroll = [&]() {
        float max_scroll = std::max(0.0f, doc_height - height);
        scroll_y = std::clamp(scroll_y, 0.0f, max_scroll);
    };
    // Load `target` into the view (no history change). Returns success.
    auto load_into = [&](const std::string& target) -> bool {
        Page np = load_target(target);
        if (!np.ok) return false;
        page = std::move(np);
        current_target = target;
        XStoreName(dpy, win, page.title.c_str());
        scroll_y = 0;
        hovered = nullptr;
        focused = nullptr;
        relayout();
        clamp_scroll();
        repaint();
        return true;
    };
    auto push_history = [&](const std::string& target) {
        history.resize(hindex + 1);  // drop any forward history
        history.push_back(target);
        hindex = history.size() - 1;
    };
    auto navigate = [&](const std::string& href) {
        if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
            std::fprintf(stderr, "htmlview: external link not followed: %s\n", href.c_str());
            return;
        }
        std::string target;
        if (cgi_mode)
            target = href;  // a request path (optionally ?query) to the program
        else
            target = (href[0] == '/') ? href : page.doc.base_dir + "/" + href;
        // load_into always renders something (the page or an error page).
        if (load_into(target)) push_history(target);
    };
    // Serialize the form enclosing `control` and issue a GET to its action.
    auto submit_form = [&](const Element* control) {
        const Element* form = enclosing_form(control);
        std::string query;
        if (form) gather_fields(form, page.form, control, query);
        else if (control->attr("name")) gather_fields(control, page.form, control, query);

        std::string action;
        if (form && form->attr("action")) action = *form->attr("action");
        if (action.empty()) {  // default: re-request the current path
            action = current_target;
            if (size_t q = action.find('?'); q != std::string::npos) action = action.substr(0, q);
        }
        std::string target = query.empty() ? action : action + "?" + query;
        if (load_into(target)) push_history(target);
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
                    float dx = ev.xbutton.x, dy = ev.xbutton.y + scroll_y;
                    const LayoutBox* w = find_widget(root.get(), dx, dy);
                    const Element* prev_focus = focused;
                    focused = nullptr;
                    if (w) {
                        switch (w->widget) {
                            case WidgetKind::TextInput: focused = w->element; repaint(); break;
                            case WidgetKind::Checkbox:
                                page.form[w->element].checked = !page.form[w->element].checked;
                                repaint();
                                break;
                            case WidgetKind::Button: {
                                const Element* be = w->element;
                                std::string type = be->attr("type") ? *be->attr("type") : "";
                                std::transform(type.begin(), type.end(), type.begin(),
                                               [](unsigned char c) { return (char)std::tolower(c); });
                                bool is_submit = (be->tag == Tag::Button && type != "button" &&
                                                  type != "reset") ||
                                                 (be->tag == Tag::Input && type == "submit");
                                if (is_submit) { repaint(); submit_form(be); }
                                else { std::fprintf(stderr, "htmlview: button: %s\n", w->text.c_str());
                                       repaint(); }
                                break;
                            }
                            default: repaint(); break;
                        }
                    } else if (const LayoutBox* link = find_link(root.get(), dx, dy)) {
                        navigate(link->href);
                    } else if (prev_focus) {
                        repaint();  // clicked away: clear focus
                    }
                }
                break;
            case KeyPress: {
                // When a text field is focused, route typing into it.
                if (focused) {
                    char buf[32];
                    KeySym ks2;
                    int n = XLookupString(&ev.xkey, buf, sizeof buf, &ks2, nullptr);
                    std::string& text = page.form[focused].text;
                    if (ks2 == XK_Escape || ks2 == XK_Return || ks2 == XK_KP_Enter) {
                        focused = nullptr; repaint();
                    } else if (ks2 == XK_BackSpace) {
                        if (!text.empty()) text.pop_back();
                        repaint();
                    } else if (n > 0 && (unsigned char)buf[0] >= 0x20 && buf[0] != 0x7f) {
                        text.append(buf, n);
                        repaint();
                    }
                    break;
                }
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

    FormState form;
    init_form_state(doc.root.get(), form);
    FormView fv{&form, nullptr, false};

    Pixmap buffer = XCreatePixmap(dpy, RootWindow(dpy, screen), width, height, depth);
    {
        Painter p(dpy, buffer, visual, cmap, fonts);
        p.paint(root.get(), width, height, 0, 0, fv);
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
