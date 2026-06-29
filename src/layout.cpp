#include "layout.h"

#include <Imlib2.h>

#include <algorithm>
#include <cctype>
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
    enum Type { Word, Space, Break, Image } type;
    std::string text;
    const ComputedStyle* style = nullptr;
    XftFont* font = nullptr;
    std::string href;
    const Element* element = nullptr;  // source element (for hover/hit-testing)
    std::string img_path;       // Image only
    float img_w = 0, img_h = 0;  // Image only
};

std::string resolve_path(const std::string& base_dir, const std::string& src) {
    if (src.empty() || src[0] == '/') return src;
    if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0) return src;
    return base_dir + "/" + src;
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
               XftFont* font, const std::string& href, const Element* element) {
    Frag f;
    f.type = type;
    f.text = std::move(text);
    f.style = style;
    f.font = font;
    f.href = href;
    f.element = element;
    return f;
}

void collect_inline(const StyleMap& sm, FontCache& fonts, const std::string& base_dir,
                    const Node* node, const ComputedStyle& style, const std::string& href,
                    const Element* element, bool preserve, std::vector<Frag>& out) {
    if (node->is_text()) {
        const std::string& d = static_cast<const Text*>(node)->data;
        XftFont* font = fonts.get(style);
        if (preserve) {
            std::string text = expand_tabs(d);
            size_t start = 0;
            while (true) {
                size_t nl = text.find('\n', start);
                std::string line = text.substr(start, nl - start);
                out.push_back(text_frag(Frag::Word, line, &style, font, href, element));
                if (nl == std::string::npos) break;
                out.push_back(text_frag(Frag::Break, "", &style, font, href, element));
                start = nl + 1;
            }
        } else {
            size_t i = 0;
            while (i < d.size()) {
                if (std::isspace((unsigned char)d[i])) {
                    out.push_back(text_frag(Frag::Space, "", &style, font, href, element));
                    while (i < d.size() && std::isspace((unsigned char)d[i])) ++i;
                } else {
                    size_t s = i;
                    while (i < d.size() && !std::isspace((unsigned char)d[i])) ++i;
                    out.push_back(text_frag(Frag::Word, d.substr(s, i - s), &style, font, href, element));
                }
            }
        }
        return;
    }

    const auto* e = static_cast<const Element*>(node);
    const ComputedStyle& est = style_of(sm, e);
    if (est.display == DisplayType::NoDisplay) return;
    if (e->tag == Tag::Br) {
        out.push_back(text_frag(Frag::Break, "", &est, fonts.get(est), href, e));
        return;
    }
    if (e->tag == Tag::Img) {
        const std::string* src = e->attr("src");
        if (!src) return;
        std::string path = resolve_path(base_dir, *src);
        float w = 0, h = 0;
        bool have = image_size(path, w, h);
        // Explicit width/height attributes override natural size.
        if (const std::string* a = e->attr("width")) { float v; if (parse_length_px(*a, est.font_size, v)) w = v; have = true; }
        if (const std::string* a = e->attr("height")) { float v; if (parse_length_px(*a, est.font_size, v)) h = v; have = true; }
        if (!have || w <= 0 || h <= 0) return;  // unloadable / sizeless: skip
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

    for (const auto& c : e->children)
        collect_inline(sm, fonts, base_dir, c.get(), est, new_href, e,
                       preserve || est.preserve_ws, out);
}

// Lay out inline fragments into `parent` as TextRun boxes. Returns height used.
float layout_inline(FontCache& fonts, const std::vector<Frag>& frags, LayoutBox* parent,
                    float content_x, float content_width, float content_top,
                    TextAlign align) {
    struct Run {
        float xrel, width, asc, desc;
        bool image = false;
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
            max_asc = std::max(max_asc, r.asc);
            max_desc = std::max(max_desc, r.desc);
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
            box->y = baseline - r.asc;
            box->width = r.width;
            box->height = r.asc + r.desc;
            if (r.image) {
                box->kind = LayoutBox::Kind::Image;
                box->img_src = r.img_path;
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
        r.xrel = pen + sw; r.width = w; r.asc = asc; r.desc = desc;
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
                                        const std::string& base_dir,
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
                                        const std::string& base_dir,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width);

std::unique_ptr<LayoutBox> layout_block(const StyleMap& sm, FontCache& fonts,
                                        const std::string& base_dir,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width) {
    if (st.display == DisplayType::Table)
        return layout_table(sm, fonts, base_dir, el, st, x, top, avail_width);

    auto box = std::make_unique<LayoutBox>();
    box->kind = LayoutBox::Kind::Block;
    box->element = el;
    box->style = &st;

    float bl = st.border.left.width, br = st.border.right.width;
    float pl = st.padding.left, pr = st.padding.right;
    float content_width = (st.width != kAuto)
                              ? st.width
                              : std::max(0.0f, avail_width - st.margin.left - st.margin.right
                                                   - bl - br - pl - pr);
    float box_width = content_width + pl + pr + bl + br;

    box->x = x + st.margin.left;
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
                collect_inline(sm, fonts, base_dir, n, st, box->href, el, st.preserve_ws, frags);
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
                auto child = layout_block(sm, fonts, base_dir, ce, cst, content_x, cursor, content_width);
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
            collect_inline(sm, fonts, base_dir, c.get(), st, box->href, el, st.preserve_ws, frags);
        float h = layout_inline(fonts, frags, box.get(), content_x, content_width, cursor,
                                st.text_align);
        cursor += h;
    }

    float content_height = (st.height != kAuto) ? st.height : (cursor - content_top);
    box->height = st.border.top.width + st.padding.top + content_height
                  + st.padding.bottom + st.border.bottom.width;
    return box;
}

// Simple table layout: even column widths, row height = tallest cell, cells
// stretched to the row height. No colspan/rowspan, no auto min/max sizing.
std::unique_ptr<LayoutBox> layout_table(const StyleMap& sm, FontCache& fonts,
                                        const std::string& base_dir,
                                        const Element* el, const ComputedStyle& st,
                                        float x, float top, float avail_width) {
    auto table = std::make_unique<LayoutBox>();
    table->kind = LayoutBox::Kind::Block;
    table->element = el;
    table->style = &st;

    float bl = st.border.left.width, brd = st.border.right.width;
    float pl = st.padding.left, pr = st.padding.right;
    float content_width = (st.width != kAuto)
                              ? st.width
                              : std::max(0.0f, avail_width - st.margin.left - st.margin.right
                                                   - bl - brd - pl - pr);
    table->x = x + st.margin.left;
    table->y = top + st.margin.top;
    table->width = content_width + pl + pr + bl + brd;

    float content_x = table->x + bl + pl;
    float content_top = table->y + st.border.top.width + st.padding.top;

    // Gather rows and the max column count.
    std::vector<const Element*> rows;
    for (const auto& c : el->children) {
        if (!c->is_element()) continue;
        const auto* e = static_cast<const Element*>(c.get());
        if (style_of(sm, e).display == DisplayType::TableRow) rows.push_back(e);
    }
    int ncols = 1;
    for (const Element* row : rows) {
        int n = 0;
        for (const auto& c : row->children)
            if (c->is_element() &&
                style_of(sm, static_cast<const Element*>(c.get())).display == DisplayType::TableCell)
                ++n;
        ncols = std::max(ncols, n);
    }
    float colw = content_width / ncols;

    float rowtop = content_top;
    for (const Element* row : rows) {
        const ComputedStyle& rst = style_of(sm, row);
        auto rowbox = std::make_unique<LayoutBox>();
        rowbox->kind = LayoutBox::Kind::Block;
        rowbox->element = row;
        rowbox->style = &rst;
        rowbox->x = content_x;
        rowbox->y = rowtop;
        rowbox->width = content_width;

        std::vector<LayoutBox*> cells;
        float rowh = 0;
        int col = 0;
        for (const auto& c : row->children) {
            if (!c->is_element()) continue;
            const auto* ce = static_cast<const Element*>(c.get());
            const ComputedStyle& cst = style_of(sm, ce);
            if (cst.display != DisplayType::TableCell) continue;
            float cx = content_x + col * colw;
            auto cell = layout_block(sm, fonts, base_dir, ce, cst, cx, rowtop, colw);
            rowh = std::max(rowh, cell->height);
            cells.push_back(cell.get());
            rowbox->add(std::move(cell));
            ++col;
        }
        for (LayoutBox* cell : cells) cell->height = rowh;  // stretch to row height
        rowbox->height = rowh;
        rowtop += rowh;
        table->add(std::move(rowbox));
    }

    float content_height = (st.height != kAuto) ? st.height : (rowtop - content_top);
    table->height = st.border.top.width + st.padding.top + content_height
                    + st.padding.bottom + st.border.bottom.width;
    return table;
}

}  // namespace

std::unique_ptr<LayoutBox> layout_document(const Document& doc, const StyleMap& sm,
                                           FontCache& fonts, float viewport_width) {
    const Element* body = find_body(doc.root.get());
    if (!body) {
        auto empty = std::make_unique<LayoutBox>();
        empty->width = viewport_width;
        return empty;
    }
    const ComputedStyle& st = style_of(sm, body);
    return layout_block(sm, fonts, doc.base_dir, body, st, 0, 0, viewport_width);
}

}  // namespace hv
