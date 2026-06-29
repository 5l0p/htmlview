// X11 presentation layer: live window + headless PNG export.

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <Imlib2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <sys/select.h>

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
    int refresh = 0;         // <meta http-equiv=refresh> seconds (0 = none)
    std::string refresh_url; // optional target of the meta refresh
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

// Within `form`, set the radio named `name` to `chosen` and clear its peers.
void select_radio(const Node* n, const std::string& name, const Element* chosen,
                  FormState& fs) {
    if (!n->is_element()) return;
    const auto* e = static_cast<const Element*>(n);
    if (e->tag == Tag::Input) {
        const std::string* t = e->attr("type");
        const std::string* nm = e->attr("name");
        if (t && nm && *nm == name) {
            std::string lt = *t;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (lt == "radio") fs[e].checked = (e == chosen);
        }
    }
    for (const auto& c : e->children) select_radio(c.get(), name, chosen, fs);
}

// Collect text-editable controls (in document order) for Tab navigation.
void collect_focusables(const LayoutBox* box, std::vector<const Element*>& out) {
    if (box->widget == WidgetKind::TextInput && box->element) out.push_back(box->element);
    for (const auto& c : box->children) collect_focusables(c.get(), out);
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
using FetchFn = std::function<std::string(const std::string&)>;

bool has_token(const std::string& list, const char* token) {
    std::stringstream ss(list);
    std::string w;
    while (ss >> w) {
        std::transform(w.begin(), w.end(), w.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (w == token) return true;
    }
    return false;
}

// Parse <meta http-equiv=refresh content="SECS[; url=URL]">.
void parse_meta_refresh(const Node* n, Page& pg) {
    if (!n->is_element()) return;
    const auto* e = static_cast<const Element*>(n);
    if (e->name == "meta") {
        const std::string* eq = e->attr("http-equiv");
        const std::string* content = e->attr("content");
        if (eq && content && has_token(*eq, "refresh")) {
            pg.refresh = std::atoi(content->c_str());
            size_t u = content->find("url=");
            if (u == std::string::npos) u = content->find("URL=");
            if (u != std::string::npos) {
                std::string url = content->substr(u + 4);
                size_t a = url.find_first_not_of(" \t'\"");
                size_t b = url.find_last_not_of(" \t'\"");
                if (a != std::string::npos) pg.refresh_url = url.substr(a, b - a + 1);
            }
        }
    }
    for (const auto& c : static_cast<const Element*>(n)->children)
        parse_meta_refresh(c.get(), pg);
}

Page build_page(std::string src, std::string base_dir, const std::string& title_fallback,
                const FetchFn& fetch) {
    Page pg;
    pg.doc = parse_html(src);
    pg.doc.base_dir = std::move(base_dir);
    pg.rules = parse_css(gather_css(pg.doc, fetch));
    pg.has_hover = stylesheet_has_hover(pg.rules);
    pg.styles = resolve_styles(pg.doc, pg.rules);
    init_form_state(pg.doc.root.get(), pg.form);
    parse_meta_refresh(pg.doc.root.get(), pg);
    pg.title = pg.doc.title.empty() ? title_fallback : pg.doc.title;
    pg.ok = true;
    return pg;
}

// No-op fetch for self-contained HTML (error pages, etc.).
const FetchFn kNoFetch = [](const std::string&) { return std::string(); };

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
    return build_page(std::move(html), "", heading, kNoFetch);
}

Page load_file_page(const std::string& path) {
    std::string src;
    if (!read_file(path, src)) {
        std::fprintf(stderr, "htmlview: cannot read %s\n", path.c_str());
        return make_error_page("Page not found", path + " could not be opened");
    }
    size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? "." : path.substr(0, slash);
    // Resolve <link> stylesheet hrefs against the document's directory.
    FetchFn fetch = [base](const std::string& href) -> std::string {
        std::string p = (!href.empty() && href[0] == '/') ? href : base + "/" + href;
        std::string s;
        if (!read_file(p, s)) std::fprintf(stderr, "htmlview: cannot read %s\n", p.c_str());
        return s;
    };
    return build_page(std::move(src), base, path, fetch);
}

// Run a CGI request and build a Page from the response, following Location:
// redirects (which become GETs). `body` non-empty implies POST. base_dir is
// empty (no local assets).
Page load_cgi_request(const std::string& program, const std::string& method,
                      const std::string& target, const std::string& body) {
    std::string t = target;
    std::string m = method, b = body;
    for (int hop = 0; hop < 8; ++hop) {
        std::string path = t, query;
        if (size_t q = t.find('?'); q != std::string::npos) {
            path = t.substr(0, q);
            query = t.substr(q + 1);
        }
        if (path.empty()) path = "/";
        CgiResponse r = run_cgi(program, m, path, query, b);
        if (!r.ok)
            return make_error_page("CGI error",
                                   (r.error.empty() ? "request failed" : r.error) +
                                       " (" + program + ")");
        if (!r.location.empty() && (r.status / 100) == 3) {
            t = r.location; m = "GET"; b.clear(); continue;  // redirect -> GET
        }
        std::string ct = r.content_type;
        std::string body = r.body;
        if (ct.rfind("text/plain", 0) == 0)
            body = "<pre>" + html_escape(body) + "</pre>";
        // <link> stylesheets are fetched as further GET requests to the program.
        FetchFn fetch = [&program](const std::string& href) -> std::string {
            std::string p = href, query;
            if (size_t q = href.find('?'); q != std::string::npos) {
                p = href.substr(0, q);
                query = href.substr(q + 1);
            }
            CgiResponse rr = run_cgi(program, "GET", p, query);
            return rr.ok ? rr.body : std::string();
        };
        return build_page(std::move(body), "", path, fetch);
    }
    return build_page("<title>CGI</title><p>too many redirects</p>", "", "CGI", kNoFetch);
}

Page load_cgi_page(const std::string& program, const std::string& target) {
    return load_cgi_request(program, "GET", target, "");
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

// Find the element targeted by a fragment: id == frag, or <a name == frag>.
const Element* find_anchor(const Node* n, const std::string& frag) {
    if (!n->is_element()) return nullptr;
    const auto* e = static_cast<const Element*>(n);
    if (const std::string* id = e->attr("id"); id && *id == frag) return e;
    if (e->tag == Tag::A)
        if (const std::string* nm = e->attr("name"); nm && *nm == frag) return e;
    for (const auto& c : e->children)
        if (const Element* r = find_anchor(c.get(), frag)) return r;
    return nullptr;
}

// First layout box produced by `target` (to read its document y).
const LayoutBox* find_box_for(const LayoutBox* box, const Element* target) {
    if (box->element == target) return box;
    for (const auto& c : box->children)
        if (const LayoutBox* r = find_box_for(c.get(), target)) return r;
    return nullptr;
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

int run_window(const std::string& target0, const std::string& cgi_program,
               int refresh_seconds) {
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

    using Clock = std::chrono::steady_clock;
    Clock::time_point refresh_deadline;
    int eff_refresh = 0;              // effective interval (page meta overrides CLI)
    std::string eff_refresh_url;

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
    // Scroll so the element named by `frag` is at the top (empty frag -> top).
    auto scroll_to_fragment = [&](const std::string& frag) {
        scroll_y = 0;
        if (!frag.empty() && root) {
            const Element* el = find_anchor(page.doc.root.get(), frag);
            if (const LayoutBox* b = el ? find_box_for(root.get(), el) : nullptr)
                scroll_y = b->y;
        }
        clamp_scroll();
        repaint();
    };
    // A page's <meta refresh> overrides the CLI --refresh interval; arm the timer.
    auto update_refresh = [&]() {
        if (page.refresh > 0) { eff_refresh = page.refresh; eff_refresh_url = page.refresh_url; }
        else { eff_refresh = refresh_seconds; eff_refresh_url.clear(); }
        if (eff_refresh > 0) refresh_deadline = Clock::now() + std::chrono::seconds(eff_refresh);
    };
    // Make `np` the current page and refresh the whole view.
    auto show_page = [&](Page np, const std::string& target) -> bool {
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
        update_refresh();
        return true;
    };
    // Load `target` (GET) into the view (no history change).
    auto load_into = [&](const std::string& target) -> bool {
        return show_page(load_target(target), target);
    };
    auto push_history = [&](const std::string& target) {
        history.resize(hindex + 1);  // drop any forward history
        history.push_back(target);
        hindex = history.size() - 1;
    };
    auto navigate = [&](const std::string& href, bool push = true) {
        if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
            std::fprintf(stderr, "htmlview: external link not followed: %s\n", href.c_str());
            return;
        }
        // Split off a #fragment.
        std::string base = href, frag;
        if (size_t h = href.find('#'); h != std::string::npos) {
            base = href.substr(0, h);
            frag = href.substr(h + 1);
        }
        if (base.empty()) {  // same-page fragment: just scroll
            scroll_to_fragment(frag);
            return;
        }
        std::string target;
        if (cgi_mode)
            target = base;  // a request path (optionally ?query) to the program
        else
            target = (base[0] == '/') ? base : page.doc.base_dir + "/" + base;
        // load_into always renders something (the page or an error page).
        if (load_into(target)) {
            if (push) push_history(target);
            if (!frag.empty()) scroll_to_fragment(frag);
        }
    };
    // Serialize the form enclosing `control` and submit it (GET query string, or
    // POST body in CGI mode).
    auto submit_form = [&](const Element* control) {
        const Element* form = enclosing_form(control);
        std::string fields;
        if (form) gather_fields(form, page.form, control, fields);
        else if (control->attr("name")) gather_fields(control, page.form, control, fields);

        std::string method;
        if (form && form->attr("method")) method = *form->attr("method");
        std::transform(method.begin(), method.end(), method.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });

        std::string action;
        if (form && form->attr("action")) action = *form->attr("action");
        if (action.empty()) {  // default: re-request the current path
            action = current_target;
            if (size_t q = action.find('?'); q != std::string::npos) action = action.substr(0, q);
        }

        if (method == "post" && cgi_mode) {
            // POST: fields go on the program's stdin, not the URL.
            if (show_page(load_cgi_request(cgi_program, "POST", action, fields), action))
                push_history(action);
        } else {
            std::string target = fields.empty() ? action : action + "?" + fields;
            if (load_into(target)) push_history(target);
        }
    };
    auto go = [&](int delta) {
        long ni = (long)hindex + delta;
        if (ni < 0 || ni >= (long)history.size()) return;
        if (load_into(history[ni])) hindex = (size_t)ni;
    };

    relayout();
    update_refresh();

    // Reload the current page (the refresh tick fired). Honours a meta-refresh
    // URL if the page set one; otherwise re-requests current_target in place.
    auto do_refresh = [&]() {
        if (!eff_refresh_url.empty()) navigate(eff_refresh_url, /*push=*/false);
        else load_into(current_target);
    };

    int xfd = ConnectionNumber(dpy);
    bool running = true;
    while (running) {
        // Drain queued X events.
        while (running && XPending(dpy)) {
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
                            case WidgetKind::Radio: {
                                const Element* form = enclosing_form(w->element);
                                std::string nm = w->element->attr("name")
                                                     ? *w->element->attr("name") : "";
                                if (form && !nm.empty())
                                    select_radio(form, nm, w->element, page.form);
                                else
                                    page.form[w->element].checked = true;
                                repaint();
                                break;
                            }
                            case WidgetKind::Button: {
                                const Element* be = w->element;
                                std::string type = be->attr("type") ? *be->attr("type") : "";
                                std::transform(type.begin(), type.end(), type.begin(),
                                               [](unsigned char c) { return (char)std::tolower(c); });
                                bool is_submit = (be->tag == Tag::Button && type != "button" &&
                                                  type != "reset") ||
                                                 (be->tag == Tag::Input && type == "submit");
                                if (is_submit) { repaint(); submit_form(be); }
                                else if (type == "reset") {
                                    if (const Element* form = enclosing_form(be)) {
                                        page.form.clear();  // re-seed defaults from the DOM
                                        init_form_state(form, page.form);
                                    }
                                    repaint();
                                } else { std::fprintf(stderr, "htmlview: button: %s\n", w->text.c_str());
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
                    bool textarea = focused->tag == Tag::Textarea;
                    if (ks2 == XK_Tab) {  // move focus between text controls
                        std::vector<const Element*> f;
                        if (root) collect_focusables(root.get(), f);
                        if (!f.empty()) {
                            auto it = std::find(f.begin(), f.end(), focused);
                            int idx = (it == f.end()) ? 0 : (int)(it - f.begin());
                            int dir = (ev.xkey.state & ShiftMask) ? -1 : 1;
                            focused = f[(idx + dir + (int)f.size()) % f.size()];
                            repaint();
                        }
                    } else if (ks2 == XK_Escape) {
                        focused = nullptr; repaint();
                    } else if (ks2 == XK_Return || ks2 == XK_KP_Enter) {
                        if (textarea) { text += '\n'; repaint(); }
                        else { focused = nullptr; repaint(); }  // single-line: blur
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
            }  // switch (ev.type)
        }  // drain while

        // Block until the next X event or the refresh deadline.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        struct timeval tv, *ptv = nullptr;
        if (eff_refresh > 0) {
            long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          refresh_deadline - Clock::now()).count();
            if (ms < 0) ms = 0;
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            ptv = &tv;
        }
        if (select(xfd + 1, &rfds, nullptr, nullptr, ptv) == 0 &&
            eff_refresh > 0 && Clock::now() >= refresh_deadline)
            do_refresh();
    }  // while (running)

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
