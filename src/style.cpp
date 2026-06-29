#include "style.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace hv {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::vector<std::string> ws_split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string t;
    while (is >> t) out.push_back(t);
    return out;
}

}  // namespace

bool parse_color(const std::string& in_raw, RGBA& out) {
    std::string in = to_lower(trim(in_raw));
    if (in.empty()) return false;
    if (in == "transparent") { out = RGBA{0, 0, 0, 0}; return true; }

    static const std::unordered_map<std::string, RGBA> named = {
        {"black", {0, 0, 0, 255}},     {"white", {255, 255, 255, 255}},
        {"red", {255, 0, 0, 255}},     {"green", {0, 128, 0, 255}},
        {"blue", {0, 0, 255, 255}},    {"yellow", {255, 255, 0, 255}},
        {"gray", {128, 128, 128, 255}},{"grey", {128, 128, 128, 255}},
        {"silver", {192, 192, 192, 255}},{"maroon", {128, 0, 0, 255}},
        {"navy", {0, 0, 128, 255}},    {"olive", {128, 128, 0, 255}},
        {"purple", {128, 0, 128, 255}},{"teal", {0, 128, 128, 255}},
        {"orange", {255, 165, 0, 255}},{"lime", {0, 255, 0, 255}},
    };
    auto it = named.find(in);
    if (it != named.end()) { out = it->second; return true; }

    if (in[0] == '#') {
        std::string h = in.substr(1);
        auto hexv = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        if (h.size() == 3) {
            int r = hexv(h[0]), g = hexv(h[1]), b = hexv(h[2]);
            if (r < 0 || g < 0 || b < 0) return false;
            out = RGBA{(uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17), 255};
            return true;
        }
        if (h.size() == 6) {
            int v[6];
            for (int i = 0; i < 6; i++) { v[i] = hexv(h[i]); if (v[i] < 0) return false; }
            out = RGBA{(uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]),
                       (uint8_t)(v[4] * 16 + v[5]), 255};
            return true;
        }
        return false;
    }

    if (in.rfind("rgb", 0) == 0) {
        size_t lp = in.find('('), rp = in.find(')');
        if (lp == std::string::npos || rp == std::string::npos) return false;
        std::string body = in.substr(lp + 1, rp - lp - 1);
        std::replace(body.begin(), body.end(), ',', ' ');
        std::vector<std::string> parts = ws_split(body);
        if (parts.size() < 3) return false;
        int c[3];
        for (int i = 0; i < 3; i++) c[i] = std::clamp(std::atoi(parts[i].c_str()), 0, 255);
        out = RGBA{(uint8_t)c[0], (uint8_t)c[1], (uint8_t)c[2], 255};
        return true;
    }
    return false;
}

bool parse_length_px(const std::string& in_raw, float em_base, float& out) {
    std::string in = to_lower(trim(in_raw));
    if (in.empty()) return false;
    char* end = nullptr;
    double v = std::strtod(in.c_str(), &end);
    if (end == in.c_str()) return false;
    std::string unit = trim(in.substr(end - in.c_str()));
    if (unit.empty() || unit == "px") out = (float)v;
    else if (unit == "em") out = (float)v * em_base;
    else if (unit == "pt") out = (float)v * 96.0f / 72.0f;
    else out = (float)v;  // unknown unit: treat as px
    return true;
}

namespace {

void set_edges(Edges& e, const std::string& value, float em) {
    std::vector<std::string> p = ws_split(value);
    std::vector<float> v;
    for (auto& s : p) { float f; if (parse_length_px(s, em, f)) v.push_back(f); }
    if (v.empty()) return;
    if (v.size() == 1) e = {v[0], v[0], v[0], v[0]};
    else if (v.size() == 2) e = {v[0], v[1], v[0], v[1]};
    else if (v.size() == 3) e = {v[0], v[1], v[2], v[1]};
    else e = {v[0], v[1], v[2], v[3]};
}

// Parse a single border spec like "5px solid #463705" (tokens in any order).
BorderSide parse_border_side(const std::string& value, float em) {
    BorderSide side{1, RGBA{0, 0, 0, 255}};  // default medium black
    for (const std::string& tok : ws_split(value)) {
        float w;
        RGBA c;
        std::string t = to_lower(tok);
        if (t == "none" || t == "hidden") { side.width = 0; continue; }
        if (t == "solid" || t == "dashed" || t == "dotted" || t == "double") continue;
        if (parse_color(tok, c)) { side.color = c; continue; }
        if (parse_length_px(tok, em, w)) { side.width = w; continue; }
    }
    return side;
}

void apply_decl(ComputedStyle& s, const Declaration& d) {
    const std::string& p = d.property;
    const std::string& v = d.value;
    float em = s.font_size;

    if (p == "display") {
        std::string lv = to_lower(v);
        if (lv == "block") s.display = DisplayType::Block;
        else if (lv == "inline") s.display = DisplayType::Inline;
        else if (lv == "list-item") s.display = DisplayType::ListItem;
        else if (lv == "none") s.display = DisplayType::NoDisplay;
        else if (lv == "table") s.display = DisplayType::Table;
        else if (lv == "table-row") s.display = DisplayType::TableRow;
        else if (lv == "table-cell") s.display = DisplayType::TableCell;
    } else if (p == "color") {
        parse_color(v, s.color);
    } else if (p == "background-color" || p == "background") {
        RGBA c; if (parse_color(v, c)) s.background_color = c;
    } else if (p == "font-family") {
        // Keep the whole prioritised list (quotes stripped, comma-separated);
        // the font cache feeds it to fontconfig for first-available fallback.
        std::string cleaned;
        std::stringstream ss(v);
        std::string fam;
        while (std::getline(ss, fam, ',')) {
            fam = trim(fam);
            if (!fam.empty() && (fam.front() == '"' || fam.front() == '\'') && fam.size() >= 2)
                fam = fam.substr(1, fam.size() - 2);
            if (fam.empty()) continue;
            if (!cleaned.empty()) cleaned += ",";
            cleaned += fam;
        }
        if (!cleaned.empty()) s.font_family = cleaned;
    } else if (p == "font-size") {
        float f; if (parse_length_px(v, em, f)) s.font_size = f;
    } else if (p == "font-weight") {
        std::string lv = to_lower(v);
        s.font_weight = (lv == "bold" || lv == "bolder" || std::atoi(lv.c_str()) >= 600)
                            ? FontWeight::Bold : FontWeight::Normal;
    } else if (p == "font-style") {
        s.font_style = (to_lower(v) == "italic" || to_lower(v) == "oblique")
                           ? FontStyle::Italic : FontStyle::Normal;
    } else if (p == "text-align") {
        std::string lv = to_lower(v);
        if (lv == "center") s.text_align = TextAlign::Center;
        else if (lv == "right") s.text_align = TextAlign::Right;
        else s.text_align = TextAlign::Left;
    } else if (p == "text-decoration") {
        s.text_decoration = (to_lower(v).find("underline") != std::string::npos)
                                ? TextDecoration::Underline : TextDecoration::NoDecoration;
    } else if (p == "list-style-type" || p == "list-style") {
        std::string lv = to_lower(v);
        if (lv.find("circle") != std::string::npos) s.list_style = ListStyle::Circle;
        else if (lv.find("square") != std::string::npos) s.list_style = ListStyle::Square;
        else if (lv.find("decimal") != std::string::npos) s.list_style = ListStyle::Decimal;
        else if (lv.find("none") != std::string::npos) s.list_style = ListStyle::NoMarker;
        else s.list_style = ListStyle::Disc;
    } else if (p == "margin") {
        set_edges(s.margin, v, em);
    } else if (p == "margin-top") { float f; if (parse_length_px(v, em, f)) s.margin.top = f;
    } else if (p == "margin-right") { float f; if (parse_length_px(v, em, f)) s.margin.right = f;
    } else if (p == "margin-bottom") { float f; if (parse_length_px(v, em, f)) s.margin.bottom = f;
    } else if (p == "margin-left") { float f; if (parse_length_px(v, em, f)) s.margin.left = f;
    } else if (p == "padding") {
        set_edges(s.padding, v, em);
    } else if (p == "padding-top") { float f; if (parse_length_px(v, em, f)) s.padding.top = f;
    } else if (p == "padding-right") { float f; if (parse_length_px(v, em, f)) s.padding.right = f;
    } else if (p == "padding-bottom") { float f; if (parse_length_px(v, em, f)) s.padding.bottom = f;
    } else if (p == "padding-left") { float f; if (parse_length_px(v, em, f)) s.padding.left = f;
    } else if (p == "border") {
        BorderSide side = parse_border_side(v, em);
        s.border.top = s.border.right = s.border.bottom = s.border.left = side;
    } else if (p == "border-top") {
        s.border.top = parse_border_side(v, em);
    } else if (p == "border-right") {
        s.border.right = parse_border_side(v, em);
    } else if (p == "border-bottom") {
        s.border.bottom = parse_border_side(v, em);
    } else if (p == "border-left") {
        s.border.left = parse_border_side(v, em);
    } else if (p == "border-width") {
        float f; if (parse_length_px(v, em, f)) {
            s.border.top.width = s.border.right.width = s.border.bottom.width =
                s.border.left.width = f;
        }
    } else if (p == "border-color") {
        RGBA c; if (parse_color(v, c)) {
            s.border.top.color = s.border.right.color = s.border.bottom.color =
                s.border.left.color = c;
        }
    } else if (p == "width") {
        if (to_lower(v) == "auto") s.width = kAuto;
        else { float f; if (parse_length_px(v, em, f)) s.width = f; }
    } else if (p == "height") {
        if (to_lower(v) == "auto") s.height = kAuto;
        else { float f; if (parse_length_px(v, em, f)) s.height = f; }
    }
}

// --- UA default stylesheet, expressed in code. ---
ComputedStyle ua_defaults(const Element& e, const ComputedStyle& parent) {
    ComputedStyle s = parent;  // start from inherited values
    // Reset non-inherited properties to initial each element.
    s.background_color = RGBA{};
    s.margin = Edges{};
    s.padding = Edges{};
    s.border = Border{};
    s.text_decoration = TextDecoration::NoDecoration;
    s.width = s.height = kAuto;
    s.display = DisplayType::Inline;

    auto block = [&] { s.display = DisplayType::Block; };
    auto heading = [&](float px) { block(); s.font_size = px; s.font_weight = FontWeight::Bold;
                                   s.margin = {px * 0.5f, 0, px * 0.5f, 0}; };

    switch (e.tag) {
        case Tag::Html: case Tag::Body: case Tag::Div: case Tag::Head:
            block(); break;
        case Tag::H1: heading(32); break;
        case Tag::H2: heading(24); break;
        case Tag::H3: heading(18.72f); break;
        case Tag::H4: heading(16); break;
        case Tag::H5: heading(13.28f); break;
        case Tag::H6: heading(10.72f); break;
        case Tag::P: block(); s.margin = {14, 0, 14, 0}; break;
        case Tag::Pre:
            block(); s.font_family = "monospace"; s.preserve_ws = true;
            s.margin = {12, 0, 12, 0}; break;
        case Tag::Ul: case Tag::Ol:
            block(); s.margin = {12, 0, 12, 0}; s.padding.left = 40;
            s.list_style = (e.tag == Tag::Ol) ? ListStyle::Decimal : ListStyle::Disc;
            break;
        case Tag::Li: s.display = DisplayType::ListItem; break;
        case Tag::A:
            s.color = RGBA{0, 0, 238, 255};
            s.text_decoration = TextDecoration::Underline; break;
        case Tag::B: case Tag::Strong: s.font_weight = FontWeight::Bold; break;
        case Tag::I: case Tag::Em: s.font_style = FontStyle::Italic; break;
        case Tag::Table: s.display = DisplayType::Table; s.margin = {0, 0, 0, 0}; break;
        case Tag::Tr: s.display = DisplayType::TableRow; break;
        case Tag::Td: s.display = DisplayType::TableCell; s.padding = {2, 2, 2, 2}; break;
        case Tag::Th:
            s.display = DisplayType::TableCell; s.padding = {2, 2, 2, 2};
            s.font_weight = FontWeight::Bold; s.text_align = TextAlign::Center; break;
        case Tag::Title: case Tag::Style: s.display = DisplayType::NoDisplay; break;
        case Tag::Form: block(); s.margin = {0, 0, 8, 0}; break;
        case Tag::Option: s.display = DisplayType::NoDisplay; break;  // not rendered inline
        // Form controls render as inline replaced widgets (see layout/paint).
        case Tag::Input: case Tag::Button: case Tag::Textarea:
        case Tag::Select: case Tag::Label: break;
        case Tag::Span: case Tag::Br: case Tag::Img: break;  // inline
        default: break;
    }
    return s;
}

// Is `maybe_anc` the element itself or an ancestor of `node`?
bool is_self_or_ancestor(const Element* maybe_anc, const Element* node) {
    for (const Node* n = node; n; n = n->parent)
        if (n == maybe_anc) return true;
    return false;
}

bool selector_matches_simple(const SimpleSelector& ss, const Element& e,
                             const Element* hovered) {
    if (!ss.tag.empty() && ss.tag != e.name) return false;
    if (!ss.id.empty()) {
        const std::string* id = e.attr("id");
        if (!id || *id != ss.id) return false;
    }
    if (!ss.classes.empty()) {
        const std::string* cls = e.attr("class");
        if (!cls) return false;
        std::vector<std::string> have = ws_split(*cls);
        for (const auto& need : ss.classes)
            if (std::find(have.begin(), have.end(), need) == have.end()) return false;
    }
    if (!ss.pseudo.empty()) {
        // :hover is true for the hovered element and its ancestors; everything
        // else (:visited, unknown) never matches (no history/state tracked).
        if (ss.pseudo == "hover") {
            if (!hovered || !is_self_or_ancestor(&e, hovered)) return false;
        } else {
            return false;
        }
    }
    return true;
}

// Match a descendant chain against element e (subject = last in chain).
bool selector_matches(const Selector& sel, const Element& e, const Element* hovered) {
    if (sel.chain.empty()) return false;
    if (!selector_matches_simple(sel.chain.back(), e, hovered)) return false;
    // Walk ancestors to satisfy the preceding compounds, in order.
    int i = (int)sel.chain.size() - 2;
    const Node* anc = e.parent;
    while (i >= 0 && anc) {
        if (anc->is_element() &&
            selector_matches_simple(sel.chain[i], *static_cast<const Element*>(anc), hovered))
            --i;
        anc = anc->parent;
    }
    return i < 0;
}

void resolve_node(const Element& e, const ComputedStyle& parent,
                  const std::vector<Rule>& author, const Element* hovered, StyleMap& out) {
    ComputedStyle s = ua_defaults(e, parent);

    // Author rules: gather matching declaration blocks with (specificity, order).
    struct Match { int spec; int order; const std::vector<Declaration>* decls; };
    std::vector<Match> matches;
    for (const Rule& r : author) {
        int best = -1;
        for (const Selector& sel : r.selectors)
            if (selector_matches(sel, e, hovered)) best = std::max(best, sel.specificity);
        if (best >= 0) matches.push_back({best, r.order, &r.decls});
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return a.spec != b.spec ? a.spec < b.spec : a.order < b.order;
    });
    for (const Match& m : matches)
        for (const Declaration& d : *m.decls) apply_decl(s, d);

    // Inline style attribute wins.
    if (const std::string* inl = e.attr("style"))
        for (const Declaration& d : parse_inline_style(*inl)) apply_decl(s, d);

    out[&e] = s;

    for (const auto& child : e.children)
        if (child->is_element())
            resolve_node(*static_cast<const Element*>(child.get()), s, author, hovered, out);
}

}  // namespace

StyleMap resolve_styles(const Document& doc, const std::vector<Rule>& author,
                        const Element* hovered) {
    StyleMap out;
    ComputedStyle root_default;  // initial values, 16px black sans
    for (const auto& child : doc.root->children)
        if (child->is_element())
            resolve_node(*static_cast<const Element*>(child.get()), root_default, author,
                         hovered, out);
    return out;
}

bool stylesheet_has_hover(const std::vector<Rule>& author) {
    for (const Rule& r : author)
        for (const Selector& sel : r.selectors)
            for (const SimpleSelector& ss : sel.chain)
                if (ss.pseudo == "hover") return true;
    return false;
}

namespace {
const char* disp_name(DisplayType d) {
    switch (d) {
        case DisplayType::Block: return "block"; case DisplayType::Inline: return "inline";
        case DisplayType::ListItem: return "list-item"; case DisplayType::NoDisplay: return "none";
        case DisplayType::Table: return "table"; case DisplayType::TableRow: return "table-row";
        case DisplayType::TableCell: return "table-cell";
    }
    return "?";
}
std::string hexcol(const RGBA& c) {
    if (c.transparent()) return "transparent";
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}
}  // namespace

void dump_styles(const Node* node, const StyleMap& sm, std::string& out, int indent) {
    std::string pad(indent * 2, ' ');
    if (node->is_element()) {
        const auto* e = static_cast<const Element*>(node);
        auto it = sm.find(e);
        out += pad + "<" + (e->tag == Tag::Unknown ? e->name : tag_name(e->tag)) + ">";
        if (it != sm.end()) {
            const ComputedStyle& s = it->second;
            char buf[256];
            std::snprintf(buf, sizeof buf,
                          "  [%s font=%s/%.0fpx%s%s color=%s bg=%s m=%.0f,%.0f,%.0f,%.0f "
                          "p=%.0f,%.0f,%.0f,%.0f bw=%.0f deco=%s]",
                          disp_name(s.display), s.font_family.c_str(), s.font_size,
                          s.font_weight == FontWeight::Bold ? " bold" : "",
                          s.font_style == FontStyle::Italic ? " italic" : "",
                          hexcol(s.color).c_str(), hexcol(s.background_color).c_str(),
                          s.margin.top, s.margin.right, s.margin.bottom, s.margin.left,
                          s.padding.top, s.padding.right, s.padding.bottom, s.padding.left,
                          s.border.top.width,
                          s.text_decoration == TextDecoration::Underline ? "underline" : "none");
            out += buf;
        }
        out += "\n";
        for (const auto& c : e->children) dump_styles(c.get(), sm, out, indent + 1);
    }
}

}  // namespace hv
