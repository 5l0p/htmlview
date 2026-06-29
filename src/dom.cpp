#include "dom.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace hv {

static std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

Tag tag_from_name(const std::string& name) {
    static const std::unordered_map<std::string, Tag> m = {
        {"html", Tag::Html}, {"head", Tag::Head}, {"body", Tag::Body},
        {"title", Tag::Title}, {"style", Tag::Style},
        {"h1", Tag::H1}, {"h2", Tag::H2}, {"h3", Tag::H3},
        {"h4", Tag::H4}, {"h5", Tag::H5}, {"h6", Tag::H6},
        {"p", Tag::P}, {"div", Tag::Div}, {"span", Tag::Span},
        {"pre", Tag::Pre}, {"br", Tag::Br}, {"hr", Tag::Hr},
        {"a", Tag::A}, {"img", Tag::Img},
        {"ul", Tag::Ul}, {"ol", Tag::Ol}, {"li", Tag::Li},
        {"b", Tag::B}, {"i", Tag::I}, {"em", Tag::Em}, {"strong", Tag::Strong},
        {"table", Tag::Table}, {"tr", Tag::Tr}, {"td", Tag::Td}, {"th", Tag::Th},
        {"form", Tag::Form}, {"input", Tag::Input}, {"button", Tag::Button},
        {"label", Tag::Label}, {"textarea", Tag::Textarea},
        {"select", Tag::Select}, {"option", Tag::Option},
    };
    auto it = m.find(to_lower(name));
    return it == m.end() ? Tag::Unknown : it->second;
}

const char* tag_name(Tag t) {
    switch (t) {
        case Tag::Html: return "html";   case Tag::Head: return "head";
        case Tag::Body: return "body";   case Tag::Title: return "title";
        case Tag::Style: return "style";
        case Tag::H1: return "h1"; case Tag::H2: return "h2"; case Tag::H3: return "h3";
        case Tag::H4: return "h4"; case Tag::H5: return "h5"; case Tag::H6: return "h6";
        case Tag::P: return "p"; case Tag::Div: return "div"; case Tag::Span: return "span";
        case Tag::Pre: return "pre"; case Tag::Br: return "br"; case Tag::Hr: return "hr";
        case Tag::A: return "a"; case Tag::Img: return "img";
        case Tag::Ul: return "ul"; case Tag::Ol: return "ol"; case Tag::Li: return "li";
        case Tag::B: return "b"; case Tag::I: return "i";
        case Tag::Em: return "em"; case Tag::Strong: return "strong";
        case Tag::Table: return "table"; case Tag::Tr: return "tr";
        case Tag::Td: return "td"; case Tag::Th: return "th";
        case Tag::Form: return "form"; case Tag::Input: return "input";
        case Tag::Button: return "button"; case Tag::Label: return "label";
        case Tag::Textarea: return "textarea"; case Tag::Select: return "select";
        case Tag::Option: return "option";
        case Tag::Unknown: default: return "?";
    }
}

bool is_void_tag(Tag t) {
    return t == Tag::Br || t == Tag::Hr || t == Tag::Img || t == Tag::Input;
}

void dump_dom(const Node* node, std::string& out, int indent) {
    std::string pad(indent * 2, ' ');
    if (node->is_text()) {
        const auto* t = static_cast<const Text*>(node);
        // Collapse whitespace just for readable dumping.
        std::string d;
        bool sp = false;
        for (char c : t->data) {
            if (std::isspace((unsigned char)c)) { sp = true; continue; }
            if (sp && !d.empty()) d += ' ';
            sp = false;
            d += c;
        }
        if (!d.empty()) out += pad + "\"" + d + "\"\n";
        return;
    }
    const auto* e = static_cast<const Element*>(node);
    out += pad + "<" + (e->tag == Tag::Unknown ? e->name : std::string(tag_name(e->tag)));
    for (const auto& [k, v] : e->attrs) out += " " + k + "=\"" + v + "\"";
    out += ">\n";
    for (const auto& c : e->children) dump_dom(c.get(), out, indent + 1);
}

}  // namespace hv
