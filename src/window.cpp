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
    ResolveImg resolve_image;  // <img src> -> local file path (data:/file/CGI)
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
        } else if (e->tag == Tag::Select) {
            int idx = 0, i = 0;
            for (const auto& c : e->children)
                if (c->is_element() &&
                    static_cast<const Element*>(c.get())->tag == Tag::Option) {
                    if (static_cast<const Element*>(c.get())->attr("selected")) idx = i;
                    ++i;
                }
            fs[e].option = idx;
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

// If (px,py) is over an open select's option row, return its index, else -1.
int dropdown_hit(const LayoutBox* sel, float px, float py) {
    if (px < sel->x || px >= sel->x + sel->width) return -1;
    auto opts = select_options(sel->element);
    float top = sel->y + sel->height;
    for (int i = 0; i < (int)opts.size(); ++i) {
        float ry = top + i * sel->height;
        if (py >= ry && py < ry + sel->height) return i;
    }
    return -1;
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

// Find a <base href="..."> directory prefix (trailing slash stripped), or "".
std::string find_base_href(const Node* n) {
    if (!n->is_element()) return "";
    const auto* e = static_cast<const Element*>(n);
    if (e->name == "base")
        if (const std::string* h = e->attr("href")) {
            std::string b = *h;
            if (!b.empty() && b.back() == '/') b.pop_back();
            return b;
        }
    for (const auto& c : e->children)
        if (std::string r = find_base_href(c.get()); !r.empty()) return r;
    return "";
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
    // <base href> overrides the directory used to resolve relative links/srcs.
    if (std::string b = find_base_href(pg.doc.root.get()); !b.empty())
        pg.doc.base_dir = b;
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

std::string html_escape(const std::string& s);  // defined below

// --- image resolution (data: URLs, CGI fetch) ---------------------------

std::string b64_decode(const std::string& in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int T[256];
    for (int& v : T) v = -1;
    for (int i = 0; i < 64; ++i) T[(unsigned char)chars[i]] = i;
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        if (T[c] == -1) continue;  // skip whitespace/newlines
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) { out.push_back((char)((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

std::string url_decode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            out.push_back((char)std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else if (in[i] == '+') out.push_back(' ');
        else out.push_back(in[i]);
    }
    return out;
}

// Write `bytes` to a fresh /tmp file with extension `ext`; returns the path.
std::string write_temp(const std::string& bytes, const std::string& ext) {
    static int counter = 0;
    std::string path = "/tmp/htmlview_img_" + std::to_string(counter++) + ext;
    std::ofstream f(path, std::ios::binary);
    if (!f) return "";
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return path;
}

std::string ext_for(const std::string& meta_or_path) {
    if (meta_or_path.find("png") != std::string::npos) return ".png";
    if (meta_or_path.find("jpeg") != std::string::npos ||
        meta_or_path.find("jpg") != std::string::npos) return ".jpg";
    if (meta_or_path.find("gif") != std::string::npos) return ".gif";
    if (meta_or_path.find("bmp") != std::string::npos) return ".bmp";
    if (meta_or_path.find("webp") != std::string::npos) return ".webp";
    return ".img";
}

// Decode a data: URL ("data:[<mime>][;base64],<data>") to a temp file.
std::string data_url_to_temp(const std::string& src) {
    size_t comma = src.find(',');
    if (comma == std::string::npos) return "";
    std::string meta = src.substr(5, comma - 5);  // between "data:" and ","
    std::string data = src.substr(comma + 1);
    bool b64 = meta.find("base64") != std::string::npos;
    std::string bytes = b64 ? b64_decode(data) : url_decode(data);
    return write_temp(bytes, ext_for(meta));
}

using ImgCache = std::shared_ptr<std::map<std::string, std::string>>;

// Build a resolver mapping <img src> -> a loadable local path. `program` empty
// means file mode (resolve against base_dir); otherwise fetch via CGI GET.
ResolveImg make_image_resolver(std::string base, std::string program, ImgCache cache) {
    return [base, program, cache](const std::string& src) -> std::string {
        if (src.rfind("data:", 0) == 0) {
            auto it = cache->find(src);
            if (it != cache->end()) return it->second;
            return (*cache)[src] = data_url_to_temp(src);
        }
        if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0) return "";
        if (program.empty())  // file mode: a real path Imlib2 can open directly
            return (!src.empty() && src[0] == '/') ? src : base + "/" + src;
        // CGI mode: fetch the bytes and stash them in a temp file.
        auto it = cache->find(src);
        if (it != cache->end()) return it->second;
        std::string path = src, query;
        if (size_t q = src.find('?'); q != std::string::npos) {
            path = src.substr(0, q);
            query = src.substr(q + 1);
        }
        CgiResponse r = run_cgi(program, "GET", path, query);
        std::string out = r.ok ? write_temp(r.body, ext_for(r.content_type + " " + path)) : "";
        return (*cache)[src] = out;
    };
}

// Build a page for a binary CGI response: image/* is displayed; anything else
// is saved to a temp file and reported on a small info page.
Page make_binary_page(const std::string& content_type, const std::string& bytes) {
    std::string ctl = content_type;
    std::transform(ctl.begin(), ctl.end(), ctl.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::string tmp = write_temp(bytes, ext_for(content_type));

    if (ctl.rfind("image/", 0) == 0) {
        std::string html = "<title>image</title>"
                           "<body style=\"margin:12px; background:#f4f4f4\">"
                           "<img src=\"" + tmp + "\"></body>";
        Page pg = build_page(std::move(html), "", "image", kNoFetch);
        // File-mode resolver: load the absolute temp path directly.
        pg.resolve_image = make_image_resolver(
            "", "", std::make_shared<std::map<std::string, std::string>>());
        return pg;
    }
    std::string html =
        "<title>Binary content</title>"
        "<style> body { font-family: sans-serif; margin: 28px; }"
        "  .b { border: 2px solid #335577; background: #eef4ff; padding: 14px 18px; }"
        "  h1 { color: #335577; font-size: 20px; margin: 0 0 8px 0; }"
        "  code { background: #f2f2f2; padding: 1px 3px; } </style>"
        "<body><div class=\"b\"><h1>Binary content</h1>"
        "<p>Type: <code>" + html_escape(content_type.empty() ? "(none)" : content_type) + "</code></p>"
        "<p>Size: " + std::to_string(bytes.size()) + " bytes</p>"
        "<p>Saved to: <code>" + html_escape(tmp) + "</code></p></div></body>";
    return build_page(std::move(html), "", "Binary content", kNoFetch);
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
    Page pg = build_page(std::move(src), base, path, fetch);
    pg.resolve_image = make_image_resolver(pg.doc.base_dir, "",
                                           std::make_shared<std::map<std::string, std::string>>());
    return pg;
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
        // Dispatch on Content-Type, like a real CGI gateway.
        std::string ctl = r.content_type;
        std::transform(ctl.begin(), ctl.end(), ctl.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        bool is_html = ctl.empty() || ctl.rfind("text/html", 0) == 0 ||
                       ctl.rfind("application/xhtml", 0) == 0;
        bool is_text = ctl.rfind("text/", 0) == 0;
        if (!is_html && !is_text)
            return make_binary_page(r.content_type, r.body);

        std::string body = is_html ? r.body : ("<pre>" + html_escape(r.body) + "</pre>");
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
        Page pg = build_page(std::move(body), "", path, fetch);
        pg.resolve_image = make_image_resolver(pg.doc.base_dir, program,
                                               std::make_shared<std::map<std::string, std::string>>());
        return pg;
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
            } else if (type == "submit" || type == "button" || type == "image") {
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
            auto opts = select_options(e);
            int idx = 0;
            if (auto it = fs.find(e); it != fs.end()) idx = it->second.option;
            if (idx >= 0 && idx < (int)opts.size()) add(*name, opts[idx].value);
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
    ImgPixCache img_cache = std::make_shared<std::map<std::string, Imlib_Image>>();
    Pixmap page_buf = None;  // full-document off-screen pixmap (doc width × doc height)
    bool page_dirty = true;  // rebuild page_buf before next blit
    Imlib_Image resize_snapshot = nullptr;  // blurred capture used during live resize
    bool in_resize = false;
    float scroll_y = 0, doc_height = 0;
    const Element* hovered = nullptr;
    const Element* focused = nullptr;      // focused text-input element
    const Element* open_select = nullptr;  // <select> whose dropdown is open

    std::vector<std::string> history{target0};  // targets visited
    size_t hindex = 0;                           // current position in history

    using Clock = std::chrono::steady_clock;
    Clock::time_point refresh_deadline;
    Clock::time_point resize_deadline;
    int eff_refresh = 0;              // effective interval (page meta overrides CLI)
    std::string eff_refresh_url;

    auto relayout = [&]() {
        page_dirty = true;
        root = layout_document(page.doc, page.styles, fonts, (float)width, page.resolve_image,
                               img_cache);
        doc_height = root ? root->height : 0;
    };
    // Re-run the cascade with the current hover state, then relayout (layout
    // boxes hold pointers into the StyleMap, so it must be rebuilt wholesale).
    auto restyle_for_hover = [&]() {
        page.styles = resolve_styles(page.doc, page.rules, hovered);
        relayout();
    };
    auto repaint = [&]() {
        // While the user is dragging a resize, always show the blurred snapshot
        // rather than the stale page_buf (Expose events fire during resize too).
        if (in_resize && resize_snapshot) {
            imlib_context_set_image(resize_snapshot);
            imlib_context_set_drawable(buffer);
            imlib_context_set_anti_alias(1);
            imlib_render_image_on_drawable_at_size(0, 0, width, height);
            XCopyArea(dpy, buffer, win, gc, 0, 0, width, height, 0, 0);
            return;
        }
        // Rebuild the full-document pixmap only when content changed.
        if (page_dirty || page_buf == None) {
            if (page_buf != None) { XFreePixmap(dpy, page_buf); page_buf = None; }
            // Make page_buf at least as tall as the viewport so XCopyArea always
            // has a full height of source pixels even on short pages.
            int ph = std::max(height, std::max(1, (int)std::ceil(doc_height)));
            page_buf = XCreatePixmap(dpy, win, width, ph, depth);
            {
                Painter p(dpy, page_buf, visual, cmap, fonts, img_cache);
                FormView fv{&page.form, focused, focused != nullptr};
                p.paint(root.get(), width, ph, 0, 0, fv);
            }
            page_dirty = false;
        }
        // Blit the viewport-sized slice into the screen buffer.
        XCopyArea(dpy, page_buf, buffer, gc, 0, (int)scroll_y, width, height, 0, 0);
        // Draw any open dropdown as an overlay on the screen buffer.
        if (open_select && root) {
            Painter overlay(dpy, buffer, visual, cmap, fonts);
            if (const LayoutBox* sb = find_box_for(root.get(), open_select))
                overlay.draw_dropdown(sb, page.form[open_select].option, -scroll_y);
        }
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
        open_select = nullptr;
        // Free cached images from the previous page.
        for (auto& [path, im] : *img_cache) {
            imlib_context_set_image(im);
            imlib_free_image();
        }
        img_cache->clear();
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
        // Resolve relative targets against base_dir (which a <base href> may
        // have overridden). Absolute paths and base-less CGI requests pass through.
        std::string target;
        if (base[0] == '/' || page.doc.base_dir.empty())
            target = base;
        else
            target = page.doc.base_dir + "/" + base;
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
                    if (!in_resize) {
                        // First event of a drag: capture the current view as a
                        // blurred snapshot to display while the user is resizing.
                        in_resize = true;
                        if (resize_snapshot) {
                            imlib_context_set_image(resize_snapshot);
                            imlib_free_image();
                        }
                        imlib_context_set_drawable(buffer);
                        resize_snapshot = imlib_create_image_from_drawable(0, 0, 0, width, height, 0);
                        if (resize_snapshot) {
                            imlib_context_set_image(resize_snapshot);
                            imlib_image_blur(6);
                        }
                    }
                    width = ev.xconfigure.width;
                    height = ev.xconfigure.height;
                    XFreePixmap(dpy, buffer);
                    buffer = XCreatePixmap(dpy, win, width, height, depth);
                    // Arm the debounce: commit to a real relayout 150 ms after
                    // the last resize event.
                    resize_deadline = Clock::now() + std::chrono::milliseconds(150);
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
                    // An open dropdown intercepts the click (select an option or close).
                    if (open_select) {
                        const LayoutBox* sb = find_box_for(root.get(), open_select);
                        int oi = sb ? dropdown_hit(sb, dx, dy) : -1;
                        if (oi >= 0) page.form[open_select].option = oi;
                        open_select = nullptr;
                        repaint();
                        break;
                    }
                    const LayoutBox* w = find_widget(root.get(), dx, dy);
                    const Element* prev_focus = focused;
                    focused = nullptr;
                    if (w) {
                        switch (w->widget) {
                            case WidgetKind::TextInput: focused = w->element; page_dirty = true; repaint(); break;
                            case WidgetKind::Select: open_select = w->element; repaint(); break;
                            case WidgetKind::Checkbox:
                                page.form[w->element].checked = !page.form[w->element].checked;
                                page_dirty = true; repaint();
                                break;
                            case WidgetKind::Radio: {
                                const Element* form = enclosing_form(w->element);
                                std::string nm = w->element->attr("name")
                                                     ? *w->element->attr("name") : "";
                                if (form && !nm.empty())
                                    select_radio(form, nm, w->element, page.form);
                                else
                                    page.form[w->element].checked = true;
                                page_dirty = true; repaint();
                                break;
                            }
                            case WidgetKind::Button: {
                                const Element* be = w->element;
                                std::string type = be->attr("type") ? *be->attr("type") : "";
                                std::transform(type.begin(), type.end(), type.begin(),
                                               [](unsigned char c) { return (char)std::tolower(c); });
                                bool is_submit = (be->tag == Tag::Button && type != "button" &&
                                                  type != "reset") ||
                                                 (be->tag == Tag::Input &&
                                                  (type == "submit" || type == "image"));
                                if (is_submit) { repaint(); submit_form(be); }
                                else if (type == "reset") {
                                    if (const Element* form = enclosing_form(be)) {
                                        // Erase only controls that belong to this form.
                                        std::function<void(const Node*)> erase_form =
                                            [&](const Node* n) {
                                                if (!n->is_element()) return;
                                                const auto* e = static_cast<const Element*>(n);
                                                page.form.erase(e);
                                                for (const auto& c : e->children)
                                                    erase_form(c.get());
                                            };
                                        erase_form(form);
                                        init_form_state(form, page.form);
                                    }
                                    page_dirty = true; repaint();
                                } else { std::fprintf(stderr, "htmlview: button: %s\n", w->text.c_str());
                                         page_dirty = true; repaint(); }
                                break;
                            }
                            default: page_dirty = true; repaint(); break;
                        }
                    } else if (const LayoutBox* link = find_link(root.get(), dx, dy)) {
                        navigate(link->href);
                    } else if (prev_focus) {
                        page_dirty = true; repaint();  // clicked away: clear focus
                    }
                }
                break;
            case KeyPress: {
                // Escape closes an open dropdown before anything else.
                if (open_select) {
                    KeySym k = XLookupKeysym(&ev.xkey, 0);
                    if (k == XK_Escape) { open_select = nullptr; repaint(); break; }
                }
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
                            page_dirty = true; repaint();
                        }
                    } else if (ks2 == XK_Escape) {
                        focused = nullptr; page_dirty = true; repaint();
                    } else if (ks2 == XK_Return || ks2 == XK_KP_Enter) {
                        if (textarea) { text += '\n'; page_dirty = true; repaint(); }
                        else { focused = nullptr; page_dirty = true; repaint(); }  // single-line: blur
                    } else if (ks2 == XK_BackSpace) {
                        if (!text.empty()) {
                            // Remove a full UTF-8 code point: skip back over
                            // continuation bytes (0x80–0xBF) then erase one more.
                            while (!text.empty() &&
                                   ((unsigned char)text.back() & 0xC0) == 0x80)
                                text.pop_back();
                            if (!text.empty()) text.pop_back();
                        }
                        page_dirty = true; repaint();
                    } else if (n > 0 && (unsigned char)buf[0] >= 0x20 && buf[0] != 0x7f) {
                        text.append(buf, n);
                        page_dirty = true; repaint();
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

        // Block until the next X event, the refresh deadline, or the resize
        // debounce deadline — whichever comes first.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        struct timeval tv, *ptv = nullptr;
        auto ms_until = [](Clock::time_point t) -> long {
            return std::max(0L, std::chrono::duration_cast<std::chrono::milliseconds>(
                                    t - Clock::now()).count());
        };
        long wait_ms = LONG_MAX;
        if (eff_refresh > 0) wait_ms = std::min(wait_ms, ms_until(refresh_deadline));
        if (in_resize)       wait_ms = std::min(wait_ms, ms_until(resize_deadline));
        if (wait_ms != LONG_MAX) {
            tv.tv_sec  = wait_ms / 1000;
            tv.tv_usec = (wait_ms % 1000) * 1000;
            ptv = &tv;
        }
        if (select(xfd + 1, &rfds, nullptr, nullptr, ptv) == 0) {
            if (eff_refresh > 0 && Clock::now() >= refresh_deadline)
                do_refresh();
            if (in_resize && Clock::now() >= resize_deadline) {
                in_resize = false;
                if (resize_snapshot) {
                    imlib_context_set_image(resize_snapshot);
                    imlib_free_image();
                    resize_snapshot = nullptr;
                }
                relayout();
                clamp_scroll();
                repaint();
            }
        }
    }  // while (running)

    XFreeCursor(dpy, hand);
    if (resize_snapshot) { imlib_context_set_image(resize_snapshot); imlib_free_image(); }
    if (page_buf != None) XFreePixmap(dpy, page_buf);
    XFreePixmap(dpy, buffer);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    fonts.clear();  // close fonts before the Display does
    XCloseDisplay(dpy);
    return 0;
}

// Rasterise a fully-loaded page to a PNG. Shares the file/CGI loaders so it
// works in both modes.
int render_page_png(const Page& page, int width, const std::string& path) {
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
    ImgPixCache img_cache = std::make_shared<std::map<std::string, Imlib_Image>>();
    auto root = layout_document(page.doc, page.styles, fonts, (float)width, page.resolve_image,
                                img_cache);
    int height = std::max(1, (int)std::ceil(root ? root->height : 1));

    FormView fv{&page.form, nullptr, false};

    Pixmap buffer = XCreatePixmap(dpy, RootWindow(dpy, screen), width, height, depth);
    {
        Painter p(dpy, buffer, visual, cmap, fonts, img_cache);
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

int run_headless(const std::string& target, const std::string& cgi_program, int width,
                 const std::string& render_to, bool want_dom, bool want_style) {
    Page page = cgi_program.empty() ? load_file_page(target)
                                    : load_cgi_page(cgi_program, target);
    if (!page.ok) return 1;

    if (want_dom) {
        std::string out;
        dump_dom(page.doc.root.get(), out);
        std::fputs(out.c_str(), stdout);
        std::printf("--- title: \"%s\"\n", page.title.c_str());
        return 0;
    }
    if (want_style) {
        std::printf("--- %zu CSS rules parsed\n", page.rules.size());
        std::string out;
        dump_styles(page.doc.root.get(), page.styles, out);
        std::fputs(out.c_str(), stdout);
        return 0;
    }
    return render_page_png(page, width, render_to);
}

}  // namespace hv
