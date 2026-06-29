#include "layout.h"

#include <Imlib2.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace hv {

namespace {

const ComputedStyle kDefaultStyle{};

const ComputedStyle& style_of(const StyleMap& sm, const Element* e) {
    auto it = sm.find(e);
    return it == sm.end() ? kDefaultStyle : it->second;
}

bool is_block_level(const ComputedStyle& s) {
    switch (s.display) {
        case DisplayType::Block:
        case DisplayType::ListItem:
        case DisplayType::Table:
            return true;
        default:
            return false;
    }
}

const Element* find_body(const Element* root) {
    for (const auto& c : root->children) {
        if (!c->is_element()) continue;
        const auto* e = static_cast<const Element*>(c.get());
        if (e->tag == Tag::Body) return e;
        if (const Element* b = find_body(e)) return b;
    }
    return nullptr;
}

// --- Inline fragments ---------------------------------------------------

struct Frag {
    enum Type { Word, Space, Break, Image, Widget } type;
    std::string text;
    const ComputedStyle* style = nullptr;
    XftFont* font = nullptr;
    std::string href;
    const Element* element = nullptr;  // source element (for hover/hit-testing)
    std::string img_path;       // Image only
    float img_w = 0, img_h = 0;  // Image / Widget intrinsic size
    WidgetKind widget = WidgetKind::NoWidget;  // Widget only (label is in `text`)
    float voffset = 0;          // vertical baseline shift (sub/sup; +down, -up)
};

// Concatenate descendant text (used for a <button>'s label).
std::string text_content(const Node* n) {
    if (n->is_text()) return static_cast<const Text*>(n)->data;
    std::string out;
    for (const auto& c : static_cast<const Element*>(n)->children)
        out += text_content(c.get());
    return out;
}

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Build a widget fragment for a form control; returns false if not a control
// (or a hidden one). Intrinsic size goes in img_w/img_h, the label in text.
bool widget_frag(const Element* e, const ComputedStyle& st, FontCache& fonts, Frag& f) {
    XftFont* font = fonts.get(st);
    float line_h = (float)(font->ascent + font->descent);
    float charw = (float)fonts.measure(font, "0");
    f.type = Frag::Widget;
    f.style = &st;
    f.font = font;
    f.element = e;

    auto attr = [&](const char* k) -> std::string {
        const std::string* v = e->attr(k);
        return v ? *v : std::string();
    };

    if (e->tag == Tag::Input) {
        std::string type = attr("type");
        std::transform(type.begin(), type.end(), type.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (type == "hidden") return false;
        if (type == "checkbox" || type == "radio") {
            f.widget = (type == "radio") ? WidgetKind::Radio : WidgetKind::Checkbox;
            f.img_w = f.img_h = 13;
            return true;
        }
        if (type == "button" || type == "submit" || type == "reset" || type == "image") {
            f.widget = WidgetKind::Button;
            std::string label = attr("value");
            if (label.empty() && type == "image") label = attr("alt");
            if (label.empty())
                label = type == "submit" || type == "image" ? "Submit"
                        : type == "reset"                    ? "Reset"
                                                             : "Button";
            f.text = label;
            f.img_w = (float)fonts.measure(font, label) + 16;
            f.img_h = line_h + 8;
            return true;
        }
        // text-like input
        f.widget = WidgetKind::TextInput;
        int size = 20;
        if (const std::string* s = e->attr("size")) { int v = std::atoi(s->c_str()); if (v > 0) size = v; }
        f.img_w = size * charw + 8;
        f.img_h = line_h + 6;
        return true;
    }
    if (e->tag == Tag::Button) {
        f.widget = WidgetKind::Button;
        f.text = trimmed(text_content(e));
        if (f.text.empty()) f.text = "Button";
        f.img_w = (float)fonts.measure(font, f.text) + 16;
        f.img_h = line_h + 8;
        return true;
    }
    if (e->tag == Tag::Textarea) {
        f.widget = WidgetKind::TextInput;
        int cols = 20, rows = 3;
        if (const std::string* s = e->attr("cols")) { int v = std::atoi(s->c_str()); if (v > 0) cols = v; }
        if (const std::string* s = e->attr("rows")) { int v = std::atoi(s->c_str()); if (v > 0) rows = v; }
        f.img_w = cols * charw + 8;
        f.img_h = rows * line_h + 6;
        return true;
    }
    if (e->tag == Tag::Select) {
        f.widget = WidgetKind::Select;  // box + drawn arrow; painter shows selection
        float widest = 0;
        for (const SelectOption& o : select_options(e))
            widest = std::max(widest, (float)fonts.measure(font, o.label));
        f.img_w = widest + 28;  // room for the arrow
        f.img_h = line_h + 8;
        return true;
    }
    return false;
}

// Natural size of an image file via Imlib2 (Imlib context must be set up).
bool image_size(const std::string& path, float& w, float& h) {
    Imlib_Image im = imlib_load_image(path.c_str());
    if (!im) return false;
    imlib_context_set_image(im);
    w = (float)imlib_image_get_width();
    h = (float)imlib_image_get_height();
    imlib_free_image();
    return true;
}

std::string expand_tabs(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '\t') out += "    ";
        else out += c;
    }
    return out;
}

Frag text_frag(Frag::Type type, std::string text, const ComputedStyle* style,
               XftFont* font, const std::string& href, const Element* element,
               float voffset = 0) {
    Frag f;
    f.type = type;
    f.text = std::move(text);
    f.style = style;
    f.font = font;
    f.href = href;
    f.element = element;
    f.voffset = voffset;
    return f;
}

void collect_inline(const StyleMap& sm, FontCache& fonts, const ResolveImg& resolve_image,
                    const Node* node, const ComputedStyle& style, const std::string& href,
                    const Element* element, float voffset, bool preserve,
                    std::vector<Frag>& out) {
    if (node->is_text()) {
        const std::string& d = static_cast<const Text*>(node)->data;
        XftFont* font = fonts.get(style);
        if (preserve) {
            std::string text = expand_tabs(d);
            size_t start = 0;
            while (true) {
                size_t nl = text.find('\n', start);
                std::string line = text.substr(start, nl - start);
                out.push_back(text_frag(Frag::Word, line, &style, font, href, element, voffset));
                if (nl == std::string::npos) break;
                out.push_back(text_frag(Frag::Break, "", &style, font, href, element, voffset));
                start = nl + 1;
            }
        } else {
            size_t i = 0;
            while (i < d.size()) {
                if (std::isspace((unsigned char)d[i])) {
                    out.push_back(text_frag(Frag::Space, "", &style, font, href, element, voffset));
                    while (i < d.size() && std::isspace((unsigned char)d[i])) ++i;
                } else {
                    size_t s = i;
                    while (i < d.size() && !std::isspace((unsigned char)d[i])) ++i;
                    out.push_back(text_frag(Frag::Word, d.substr(s, i - s), &style, font, href, element, voffset));
                }
            }
        }
        return;
    }

    const auto* e = static_cast<const Element*>(node);
    const ComputedStyle& est = style_of(sm, e);
    if (est.display == DisplayType::NoDisplay) return;
    if (e->tag == Tag::Input || e->tag == Tag::Button || e->tag == Tag::Textarea ||
        e->tag == Tag::Select) {
        Frag f;
        if (widget_frag(e, est, fonts, f)) out.push_back(std::move(f));
        return;  // controls don't recurse into their children for inline flow
    }
    if (e->tag == Tag::Br) {
        out.push_back(text_frag(Frag::Break, "", &est, fonts.get(est), href, e));
        return;
    }
    if (e->tag == Tag::Img) {
        const std::string* src = e->attr("src");
        if (!src) return;
        std::string path = resolve_image ? resolve_image(*src) : *src;
        float w = 0, h = 0;
        bool have = !path.empty() && image_size(path, w, h);
        // Explicit width/height attributes override natural size.
        if (const std::string* a = e->attr("width")) { float v; if (parse_length_px(*a, est.font_size, v)) w = v; have = true; }
        if (const std::string* a = e->attr("height")) { float v; if (parse_length_px(*a, est.font_size, v)) h = v; have = true; }
        if (!have || w <= 0 || h <= 0) {
            // Unloadable/sizeless image: fall back to its alt text, if any.
            if (const std::string* alt = e->attr("alt"); alt && !alt->empty()) {
                XftFont* font = fonts.get(est);
                std::stringstream as(*alt);
                std::string word;
                bool first = true;
                while (as >> word) {
                    if (!first) out.push_back(text_frag(Frag::Space, "", &est, font, href, e));
                    out.push_back(text_frag(Frag::Word, word, &est, font, href, e));
                    first = false;
                }
            }
            return;
        }
        Frag f;
        f.type = Frag::Image;
        f.style = &est;
        f.href = href;
        f.element = e;
        f.img_path = path;
        f.img_w = w;
        f.img_h = h;
        out.push_back(std::move(f));
        return;
    }
    std::string new_href = href;
    if (e->tag == Tag::A)
        if (const std::string* h = e->attr("href")) new_href = *h;

    // sub/sup shift their content's baseline (relative to their own font size).
    float child_voffset = voffset;
    if (e->tag == Tag::Sup) child_voffset -= 0.45f * est.font_size;
    else if (e->tag == Tag::Sub) child_voffset += 0.25f * est.font_size;

    for (const auto& c : e->children)
        collect_inline(sm, fonts, resolve_image, c.get(), est, new_href, e, child_voffset,
                       preserve || est.preserve_ws, out);
}

// Lay out inline fragments into `parent` as TextRun boxes. Returns height used.
float layout_inline(FontCache& fonts, const std::vector<Frag>& frags, LayoutBox* parent,
                    float content_x, float content_width, float content_top,
                    TextAlign align) {
    struct Run {
        float xrel, width, asc, desc, voffset = 0;
        bool image = false;
        WidgetKind widget = WidgetKind::NoWidget;
        std::string text;
        XftFont* font = nullptr;
        RGBA color;
        TextDecoration deco;
        std::string href;
        std::string img_path;
        const Element* element = nullptr;
    };
    std::vector<Run> line;
    float pen = 0;
    bool pending_space = false;
    float y = content_top;

    auto flush = [&](bool force) {
        if (line.empty()) {
            if (force) y += 16 * 1.2f;  // blank line from a lone <br>
            return;
        }
        float max_asc = 0, max_desc = 0;
        for (const Run& r : line) {
            max_asc = std::max(max_asc, r.asc - r.voffset);   // voffset<0 (sup) -> taller
            max_desc = std::max(max_desc, r.desc + r.voffset);  // voffset>0 (sub) -> deeper
        }
        float offset = 0;
        if (align == TextAlign::Center) offset = std::max(0.0f, (content_width - pen) / 2);
        else if (align == TextAlign::Right) offset = std::max(0.0f, content_width - pen);
        float baseline = y + max_asc;
        for (const Run& r : line) {
            auto box = std::make_unique<LayoutBox>();
            box->href = r.href;
            box->element = r.element;
            box->x = content_x + offset + r.xrel;
            box->y = baseline - r.asc + r.voffset;
            box->width = r.width;
            box->height = r.asc + r.desc;
            if (r.image) {
                box->kind = LayoutBox::Kind::Image;
                box->img_src = r.img_path;
            } else if (r.widget != WidgetKind::NoWidget) {
                box->kind = LayoutBox::Kind::Widget;
                box->widget = r.widget;
                box->text = r.text;   // button label
                box->font = r.font;
                box->color = r.color;
            } else {
                box->kind = LayoutBox::Kind::TextRun;
                box->text = r.text;
                box->font = r.font;
                box->color = r.color;
                box->decoration = r.deco;
            }
            parent->add(std::move(box));
        }
        y += max_asc + max_desc;
        line.clear();
        pen = 0;
        pending_space = false;
    };

    for (const Frag& f : frags) {
        if (f.type == Frag::Break) { flush(true); continue; }
        if (f.type == Frag::Space) { pending_space = true; continue; }

        float w, asc, desc;
        Run r;
        if (f.type == Frag::Image) {
            w = f.img_w; asc = f.img_h; desc = 0;
            r.image = true; r.img_path = f.img_path;
        } else if (f.type == Frag::Widget) {
            w = f.img_w; asc = f.img_h; desc = 0;
            r.widget = f.widget; r.text = f.text; r.font = f.font;
            r.color = f.style->color;
        } else {  // Word
            w = (float)fonts.measure(f.font, f.text);
            asc = (float)f.font->ascent; desc = (float)f.font->descent;
            r.text = f.text; r.font = f.font; r.color = f.style->color;
            r.deco = f.style->text_decoration;
        }
        XftFont* space_font = f.font ? f.font : fonts.get(*f.style);
        float sw = (pending_space && !line.empty()) ? (float)fonts.measure(space_font, " ") : 0;
        if (!line.empty() && pen + sw + w > content_width) { flush(false); sw = 0; }
        if (line.empty()) { sw = 0; pending_space = false; }
        r.xrel = pen + sw; r.width = w; r.asc = asc; r.desc = desc; r.voffset = f.voffset;
        r.href = f.href; r.element = f.element;
        line.push_back(std::move(r));
        pen += sw + w;
        pending_space = false;
    }
    flush(false);
    return y - content_top;
}

// Forward decl.
std::unique_ptr<LayoutBox> layout_block(const StyleMap& sm, FontCache& fonts,
                                        const ResolveImg& resolve_image,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width);

// 1-based position of a list item among its list-item siblings.
int list_item_index(const StyleMap& sm, const Element* li) {
    int idx = 0;
    const Node* p = li->parent;
    if (!p) return 1;
    for (const auto& c : static_cast<const Element*>(p)->children) {
        if (!c->is_element()) continue;
        const auto* e = static_cast<const Element*>(c.get());
        if (style_of(sm, e).display == DisplayType::ListItem) ++idx;
        if (e == li) break;
    }
    return idx;
}

std::string marker_text(ListStyle ls, int index) {
    switch (ls) {
        case ListStyle::Disc:   return "\xe2\x80\xa2";  // •
        case ListStyle::Circle: return "\xe2\x97\xa6";  // ◦
        case ListStyle::Square: return "\xe2\x96\xaa";  // ▪
        case ListStyle::Decimal: return std::to_string(index) + ".";
        case ListStyle::NoMarker: return "";
    }
    return "";
}

// Decide whether a block's children form a block context (any block-level child).
bool has_block_children(const StyleMap& sm, const Element* el) {
    for (const auto& c : el->children) {
        if (!c->is_element()) continue;
        const auto* e = static_cast<const Element*>(c.get());
        const ComputedStyle& s = style_of(sm, e);
        if (s.display == DisplayType::NoDisplay) continue;
        if (is_block_level(s)) return true;
    }
    return false;
}

std::unique_ptr<LayoutBox> layout_table(const StyleMap& sm, FontCache& fonts,
                                        const ResolveImg& resolve_image,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width);

std::unique_ptr<LayoutBox> layout_block(const StyleMap& sm, FontCache& fonts,
                                        const ResolveImg& resolve_image,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width) {
    if (st.display == DisplayType::Table)
        return layout_table(sm, fonts, resolve_image, el, st, x, top, avail_width);

    auto box = std::make_unique<LayoutBox>();
    box->kind = LayoutBox::Kind::Block;
    box->element = el;
    box->style = &st;

    float bl = st.border.left.width, br = st.border.right.width;
    float pl = st.padding.left, pr = st.padding.right;
    float ml = st.margin.left, mr = st.margin.right;

    // Resolve width (auto / px / %); then auto left/right margins absorb any
    // leftover space (margin:auto centering).
    float content_width;
    if (!st.width.is_auto()) {
        content_width = std::max(0.0f, st.width.resolve(avail_width, 0));
        float free = avail_width - content_width - pl - pr - bl - br
                     - (st.margin_left_auto ? 0 : ml) - (st.margin_right_auto ? 0 : mr);
        if (st.margin_left_auto && st.margin_right_auto) ml = mr = std::max(0.0f, free / 2);
        else if (st.margin_left_auto) ml = std::max(0.0f, free);
        else if (st.margin_right_auto) mr = std::max(0.0f, free);
    } else {
        if (st.margin_left_auto) ml = 0;
        if (st.margin_right_auto) mr = 0;
        content_width = std::max(0.0f, avail_width - ml - mr - bl - br - pl - pr);
    }
    float box_width = content_width + pl + pr + bl + br;

    box->x = x + ml;
    box->y = top + st.margin.top;
    box->width = box_width;

    float content_x = box->x + bl + pl;
    float content_top = box->y + st.border.top.width + st.padding.top;
    float cursor = content_top;

    // List-item marker: a bullet/number set just outside the content edge,
    // aligned to the first line's baseline.
    if (st.display == DisplayType::ListItem && st.list_style != ListStyle::NoMarker) {
        XftFont* font = fonts.get(st);
        float line_h = (float)(font->ascent + font->descent);
        auto marker = std::make_unique<LayoutBox>();
        marker->color = st.color;
        marker->y = content_top;
        marker->height = line_h;
        if (st.list_style == ListStyle::Decimal) {
            // Numeric markers stay as text, right-aligned to the content edge.
            std::string text = marker_text(st.list_style, list_item_index(sm, el));
            float mw = (float)fonts.measure(font, text);
            marker->kind = LayoutBox::Kind::TextRun;
            marker->text = text;
            marker->font = font;
            marker->x = content_x - mw - 6;
            marker->width = mw;
        } else {
            // disc / circle / square drawn as shapes (font-independent).
            marker->kind = LayoutBox::Kind::Marker;
            marker->marker = st.list_style;
            marker->width = (float)font->ascent;
            marker->x = content_x - marker->width - 4;
        }
        box->add(std::move(marker));
    }

    if (has_block_children(sm, el)) {
        std::vector<const Node*> inline_run;
        float prev_margin_bottom = 0;  // for adjacent-sibling margin collapsing
        auto flush_inline = [&]() {
            if (inline_run.empty()) return;
            std::vector<Frag> frags;
            for (const Node* n : inline_run)
                collect_inline(sm, fonts, resolve_image, n, st, box->href, el, 0, st.preserve_ws, frags);
            if (!frags.empty()) {
                auto anon = std::make_unique<LayoutBox>();
                anon->kind = LayoutBox::Kind::Anonymous;
                anon->style = &st;
                anon->x = content_x;
                anon->y = cursor;
                anon->width = content_width;
                float h = layout_inline(fonts, frags, anon.get(), content_x, content_width,
                                        cursor, st.text_align);
                anon->height = h;
                cursor += h;
                box->add(std::move(anon));
                prev_margin_bottom = 0;  // inline content breaks margin adjacency
            }
            inline_run.clear();
        };

        for (const auto& c : el->children) {
            if (c->is_text()) { inline_run.push_back(c.get()); continue; }
            const auto* ce = static_cast<const Element*>(c.get());
            const ComputedStyle& cst = style_of(sm, ce);
            if (cst.display == DisplayType::NoDisplay) continue;
            if (is_block_level(cst)) {
                flush_inline();
                // Adjacent-sibling vertical margin collapsing: the gap between
                // two blocks is max(prev.bottom, next.top), not their sum.
                cursor -= std::min(prev_margin_bottom, cst.margin.top);
                auto child = layout_block(sm, fonts, resolve_image, ce, cst, content_x, cursor, content_width);
                cursor += cst.margin.top + child->height + cst.margin.bottom;
                prev_margin_bottom = cst.margin.bottom;
                box->add(std::move(child));
            } else {
                inline_run.push_back(c.get());  // inline element among blocks
            }
        }
        flush_inline();
    } else {
        std::vector<Frag> frags;
        for (const auto& c : el->children)
            collect_inline(sm, fonts, resolve_image, c.get(), st, box->href, el, 0, st.preserve_ws, frags);
        float h = layout_inline(fonts, frags, box.get(), content_x, content_width, cursor,
                                st.text_align);
        cursor += h;
    }

    // height: px is honoured; % needs a definite container height (rare) so we
    // fall back to the content height as browsers do for auto.
    float content_height = st.height.unit == Length::Px
                               ? st.height.value
                               : (cursor - content_top);
    box->height = st.border.top.width + st.padding.top + content_height
                  + st.padding.bottom + st.border.bottom.width;
    if (resolve_image && !st.background_image.empty())
        box->bg_image = resolve_image(st.background_image);
    return box;
}

// Shift a box and all its descendants vertically (cells are laid out at y=0
// then translated to their final row position once row heights are known).
void translate_box(LayoutBox* b, float dy) {
    b->y += dy;
    for (auto& c : b->children) translate_box(c.get(), dy);
}

// Table layout with colspan/rowspan via a grid-occupancy model. Columns are
// even width; row heights are the tallest cell (rowspanning cells distribute
// any overflow into their last spanned row). No auto min/max column sizing.
std::unique_ptr<LayoutBox> layout_table(const StyleMap& sm, FontCache& fonts,
                                        const ResolveImg& resolve_image,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width) {
    auto table = std::make_unique<LayoutBox>();
    table->kind = LayoutBox::Kind::Block;
    table->element = el;
    table->style = &st;

    float bl = st.border.left.width, brd = st.border.right.width;
    float pl = st.padding.left, pr = st.padding.right;
    float content_width = !st.width.is_auto()
                              ? std::max(0.0f, st.width.resolve(avail_width, 0))
                              : std::max(0.0f, avail_width - st.margin.left - st.margin.right
                                                   - bl - brd - pl - pr);
    table->x = x + st.margin.left;
    table->y = top + st.margin.top;
    table->width = content_width + pl + pr + bl + brd;

    float content_x = table->x + bl + pl;
    float content_top = table->y + st.border.top.width + st.padding.top;

    std::vector<const Element*> rows;
    for (const auto& c : el->children) {
        if (!c->is_element()) continue;
        const auto* e = static_cast<const Element*>(c.get());
        if (style_of(sm, e).display == DisplayType::TableRow) rows.push_back(e);
    }
    int nrows = (int)rows.size();
    if (nrows == 0) { table->height = 0; return table; }

    auto attr_int = [](const Element* e, const char* k, int def) {
        const std::string* v = e->attr(k);
        int n = v ? std::atoi(v->c_str()) : def;
        return n < 1 ? def : n;
    };

    // Assign each cell a grid slot, skipping cells covered by a rowspan above.
    struct Cell { const Element* el; const ComputedStyle* st; int row, col, cspan, rspan;
                  std::unique_ptr<LayoutBox> box; };
    std::vector<Cell> cells;
    std::vector<std::vector<char>> occ(nrows);
    auto occ_at = [&](int r, int c) -> char& {
        if ((int)occ[r].size() <= c) occ[r].resize(c + 1, 0);
        return occ[r][c];
    };
    int ncols = 1;
    for (int r = 0; r < nrows; ++r) {
        int col = 0;
        for (const auto& c : rows[r]->children) {
            if (!c->is_element()) continue;
            const auto* ce = static_cast<const Element*>(c.get());
            const ComputedStyle& cst = style_of(sm, ce);
            if (cst.display != DisplayType::TableCell) continue;
            while (occ_at(r, col)) ++col;
            int cspan = attr_int(ce, "colspan", 1);
            int rspan = std::min(attr_int(ce, "rowspan", 1), nrows - r);
            for (int dr = 0; dr < rspan; ++dr)
                for (int dc = 0; dc < cspan; ++dc) occ_at(r + dr, col + dc) = 1;
            cells.push_back({ce, &cst, r, col, cspan, rspan, nullptr});
            col += cspan;
        }
        ncols = std::max(ncols, std::max(col, (int)occ[r].size()));
    }
    float colw = content_width / ncols;

    // Pass 1: lay out each cell at y=0 to learn its natural height.
    for (Cell& ci : cells)
        ci.box = layout_block(sm, fonts, resolve_image, ci.el, *ci.st,
                              content_x + ci.col * colw, 0, ci.cspan * colw);

    // Row heights: single-row cells set the base; rowspanning cells push any
    // overflow into their last spanned row.
    std::vector<float> rowh(nrows, 0);
    for (const Cell& ci : cells)
        if (ci.rspan == 1) rowh[ci.row] = std::max(rowh[ci.row], ci.box->height);
    for (const Cell& ci : cells)
        if (ci.rspan > 1) {
            float have = 0;
            for (int dr = 0; dr < ci.rspan; ++dr) have += rowh[ci.row + dr];
            if (ci.box->height > have) rowh[ci.row + ci.rspan - 1] += ci.box->height - have;
        }
    std::vector<float> row_y(nrows + 1);
    row_y[0] = content_top;
    for (int r = 0; r < nrows; ++r) row_y[r + 1] = row_y[r] + rowh[r];

    // Build row boxes, then move each cell into its starting row at final y/height.
    std::vector<LayoutBox*> rowbox(nrows);
    for (int r = 0; r < nrows; ++r) {
        auto rb = std::make_unique<LayoutBox>();
        rb->kind = LayoutBox::Kind::Block;
        rb->element = rows[r];
        rb->style = &style_of(sm, rows[r]);
        rb->x = content_x;
        rb->y = row_y[r];
        rb->width = content_width;
        rb->height = rowh[r];
        rowbox[r] = table->add(std::move(rb));
    }
    for (Cell& ci : cells) {
        translate_box(ci.box.get(), row_y[ci.row]);  // from y=0 to final row top
        float h = 0;
        for (int dr = 0; dr < ci.rspan; ++dr) h += rowh[ci.row + dr];
        ci.box->height = h;  // stretch across spanned rows
        rowbox[ci.row]->add(std::move(ci.box));
    }

    float content_height = st.height.unit == Length::Px ? st.height.value
                                                        : (row_y[nrows] - content_top);
    table->height = st.border.top.width + st.padding.top + content_height
                    + st.padding.bottom + st.border.bottom.width;
    return table;
}

}  // namespace

std::unique_ptr<LayoutBox> layout_document(const Document& doc, const StyleMap& sm,
                                           FontCache& fonts, float viewport_width,
                                           const ResolveImg& resolve_image) {
    // Prefer <body>; otherwise lay out the document root directly (browsers
    // synthesize an implicit body for body-less fragments).
    const Element* container = find_body(doc.root.get());
    if (container) {
        const ComputedStyle& st = style_of(sm, container);
        return layout_block(sm, fonts, resolve_image, container, st, 0, 0, viewport_width);
    }
    static const ComputedStyle kRootBlock = [] {
        ComputedStyle s;
        s.display = DisplayType::Block;
        s.margin = {8, 8, 8, 8};  // default body margin
        return s;
    }();
    return layout_block(sm, fonts, resolve_image, doc.root.get(), kRootBlock, 0, 0, viewport_width);
}

std::vector<SelectOption> select_options(const Element* select) {
    std::vector<SelectOption> out;
    for (const auto& c : select->children) {
        if (!c->is_element()) continue;
        const auto* o = static_cast<const Element*>(c.get());
        if (o->tag != Tag::Option) continue;
        std::string label;
        for (const auto& t : o->children)
            if (t->is_text()) label += static_cast<const Text*>(t.get())->data;
        // trim
        size_t a = label.find_first_not_of(" \t\r\n");
        size_t b = label.find_last_not_of(" \t\r\n");
        label = (a == std::string::npos) ? "" : label.substr(a, b - a + 1);
        const std::string* v = o->attr("value");
        out.push_back({label, v ? *v : label});
    }
    return out;
}

}  // namespace hv
