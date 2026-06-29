#include "html_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace hv {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

class Parser {
public:
    explicit Parser(const std::string& src) : s(src), n(src.size()) {}

    Document run() {
        Document doc;
        doc.root = std::make_unique<Element>();
        doc.root->name = "#document";
        stack.push_back(doc.root.get());

        while (pos < n) {
            if (s[pos] == '<') {
                if (starts_with("<!--")) { skip_comment(); continue; }
                if (s[pos + 1] == '!' || s[pos + 1] == '?') { skip_bogus(); continue; }
                if (s[pos + 1] == '/') { parse_end_tag(doc); continue; }
                parse_start_tag(doc);
            } else {
                parse_text();
            }
        }
        return doc;
    }

private:
    const std::string& s;
    size_t pos = 0;
    size_t n;
    std::vector<Element*> stack;  // open elements; back() is current

    Element* cur() { return stack.back(); }

    bool starts_with(const char* lit) const {
        size_t k = 0;
        while (lit[k]) {
            if (pos + k >= n || s[pos + k] != lit[k]) return false;
            ++k;
        }
        return true;
    }

    void skip_comment() {
        pos += 4;  // "<!--"
        size_t e = s.find("-->", pos);
        pos = (e == std::string::npos) ? n : e + 3;
    }

    void skip_bogus() {  // <!doctype ...>, <?...?>
        size_t e = s.find('>', pos);
        pos = (e == std::string::npos) ? n : e + 1;
    }

    void parse_text() {
        size_t start = pos;
        while (pos < n && s[pos] != '<') ++pos;
        std::string raw = s.substr(start, pos - start);
        std::string text = decode_entities(raw);
        cur()->append(std::make_unique<Text>(std::move(text)));
    }

    // Read an element name [a-zA-Z][a-zA-Z0-9]* starting at pos.
    std::string read_name() {
        size_t start = pos;
        while (pos < n && (std::isalnum((unsigned char)s[pos]) ||
                           s[pos] == '-' || s[pos] == ':'))
            ++pos;
        return to_lower(s.substr(start, pos - start));
    }

    void skip_ws() {
        while (pos < n && std::isspace((unsigned char)s[pos])) ++pos;
    }

    void parse_start_tag(Document& doc) {
        ++pos;  // '<'
        std::string name = read_name();
        Tag tag = tag_from_name(name);

        auto el = std::make_unique<Element>();
        el->name = name;
        el->tag = tag;

        // Attributes.
        bool self_close = false;
        while (pos < n) {
            skip_ws();
            if (pos >= n) break;
            if (s[pos] == '>') { ++pos; break; }
            if (s[pos] == '/' && pos + 1 < n && s[pos + 1] == '>') {
                self_close = true;
                pos += 2;
                break;
            }
            // attribute name
            size_t astart = pos;
            while (pos < n && s[pos] != '=' && s[pos] != '>' &&
                   s[pos] != '/' && !std::isspace((unsigned char)s[pos]))
                ++pos;
            if (pos == astart) { ++pos; continue; }  // stray char, skip
            std::string aname = to_lower(s.substr(astart, pos - astart));
            std::string aval;
            skip_ws();
            if (pos < n && s[pos] == '=') {
                ++pos;
                skip_ws();
                if (pos < n && (s[pos] == '"' || s[pos] == '\'')) {
                    char q = s[pos++];
                    size_t vstart = pos;
                    while (pos < n && s[pos] != q) ++pos;
                    aval = s.substr(vstart, pos - vstart);
                    if (pos < n) ++pos;  // closing quote
                } else {
                    size_t vstart = pos;
                    while (pos < n && !std::isspace((unsigned char)s[pos]) &&
                           s[pos] != '>')
                        ++pos;
                    aval = s.substr(vstart, pos - vstart);
                }
            }
            el->attrs[aname] = decode_entities(aval);
        }

        apply_implicit_close(tag);

        Element* raw_el = static_cast<Element*>(cur()->append(std::move(el)));

        // Raw-text elements: consume content verbatim up to the close tag.
        if (tag == Tag::Style || tag == Tag::Title) {
            std::string text = read_rawtext(name);
            if (tag == Tag::Title) doc.title += collapse_ws(text);
            if (tag == Tag::Style) doc.css += text + "\n";
            raw_el->append(std::make_unique<Text>(text));
            return;  // already consumed its end tag
        }

        if (!self_close && !is_void_tag(tag) && !is_void_name(name)) {
            stack.push_back(raw_el);
        }
    }

    // Void elements outside our Tag enum (parsed as Tag::Unknown).
    static bool is_void_name(const std::string& n) {
        return n == "meta" || n == "link" || n == "base" || n == "input" ||
               n == "col" || n == "area" || n == "source" || n == "embed" ||
               n == "param" || n == "wbr" || n == "track";
    }

    void parse_end_tag(Document&) {
        pos += 2;  // "</"
        std::string name = read_name();
        size_t e = s.find('>', pos);
        pos = (e == std::string::npos) ? n : e + 1;
        Tag tag = tag_from_name(name);

        // Pop down the stack to the nearest matching open element.
        for (size_t i = stack.size(); i-- > 1;) {  // keep synthetic root at [0]
            bool match = (tag != Tag::Unknown) ? stack[i]->tag == tag
                                               : stack[i]->name == name;
            if (match) {
                stack.resize(i);
                return;
            }
        }
        // No match: ignore stray end tag.
    }

    // Implicit close before opening `tag`, for the common HTML omission rules.
    void apply_implicit_close(Tag tag) {
        auto top = [&]() { return stack.size() > 1 ? cur()->tag : Tag::Unknown; };

        if (tag == Tag::Li && top() == Tag::Li) stack.pop_back();

        if (tag == Tag::Tr) {
            if (top() == Tag::Td || top() == Tag::Th) stack.pop_back();
            if (top() == Tag::Tr) stack.pop_back();
        }
        if (tag == Tag::Td || tag == Tag::Th) {
            if (top() == Tag::Td || top() == Tag::Th) stack.pop_back();
        }
        // A new block-level element closes an open <p>.
        static const auto is_block = [](Tag t) {
            switch (t) {
                case Tag::P: case Tag::Div: case Tag::Pre: case Tag::Hr:
                case Tag::H1: case Tag::H2: case Tag::H3:
                case Tag::H4: case Tag::H5: case Tag::H6:
                case Tag::Ul: case Tag::Ol: case Tag::Li:
                case Tag::Table: case Tag::Tr: case Tag::Td: case Tag::Th:
                    return true;
                default: return false;
            }
        };
        if (is_block(tag) && top() == Tag::P) stack.pop_back();
    }

    // Consume raw text until the matching </name>; returns the inner text and
    // advances pos past the end tag.
    std::string read_rawtext(const std::string& name) {
        std::string close = "</" + name;
        size_t start = pos;
        size_t search = pos;
        while (search < n) {
            size_t f = s.find('<', search);
            if (f == std::string::npos) { pos = n; return s.substr(start); }
            if (to_lower(s.substr(f, close.size())) == close) {
                std::string text = s.substr(start, f - start);
                size_t e = s.find('>', f);
                pos = (e == std::string::npos) ? n : e + 1;
                return text;
            }
            search = f + 1;
        }
        pos = n;
        return s.substr(start);
    }

    static std::string collapse_ws(const std::string& in) {
        std::string out;
        bool sp = false;
        for (char c : in) {
            if (std::isspace((unsigned char)c)) { sp = true; continue; }
            if (sp && !out.empty()) out += ' ';
            sp = false;
            out += c;
        }
        return out;
    }

    static std::string decode_entities(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size();) {
            if (in[i] != '&') { out += in[i++]; continue; }
            size_t semi = in.find(';', i);
            if (semi == std::string::npos || semi - i > 10) { out += in[i++]; continue; }
            std::string ent = in.substr(i + 1, semi - i - 1);
            std::string rep = named_entity(ent);
            if (!rep.empty()) { out += rep; i = semi + 1; }
            else if (!ent.empty() && ent[0] == '#') { out += numeric_entity(ent); i = semi + 1; }
            else out += in[i++];
        }
        return out;
    }

    static std::string named_entity(const std::string& e) {
        if (e == "amp") return "&";
        if (e == "lt") return "<";
        if (e == "gt") return ">";
        if (e == "quot") return "\"";
        if (e == "apos") return "'";
        if (e == "nbsp") return "\xc2\xa0";  // U+00A0 in UTF-8
        if (e == "copy") return "\xc2\xa9";
        if (e == "reg") return "\xc2\xae";
        if (e == "trade") return "\xe2\x84\xa2";
        if (e == "deg") return "\xc2\xb0";
        if (e == "times") return "\xc3\x97";
        if (e == "divide") return "\xc3\xb7";
        if (e == "mdash") return "\xe2\x80\x94";
        if (e == "ndash") return "\xe2\x80\x93";
        if (e == "hellip") return "\xe2\x80\xa6";
        if (e == "laquo") return "\xc2\xab";
        if (e == "raquo") return "\xc2\xbb";
        if (e == "larr") return "\xe2\x86\x90";
        if (e == "uarr") return "\xe2\x86\x91";
        if (e == "rarr") return "\xe2\x86\x92";
        if (e == "darr") return "\xe2\x86\x93";
        if (e == "harr") return "\xe2\x86\x94";
        return "";
    }

    // Encode a Unicode code point as UTF-8.
    static std::string numeric_entity(const std::string& e) {
        long cp = 0;
        if (e.size() > 1 && (e[1] == 'x' || e[1] == 'X'))
            cp = std::strtol(e.c_str() + 2, nullptr, 16);
        else
            cp = std::strtol(e.c_str() + 1, nullptr, 10);
        std::string out;
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        return out;
    }
};

}  // namespace

Document parse_html(const std::string& src) {
    return Parser(src).run();
}

}  // namespace hv
