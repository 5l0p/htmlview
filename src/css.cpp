#include "css.h"

#include <algorithm>
#include <cctype>

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

// Strip /* ... */ comments.
std::string strip_comments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            size_t e = s.find("*/", i + 2);
            i = (e == std::string::npos) ? s.size() : e + 2;
        } else if (s.compare(i, 4, "<!--") == 0) {
            i += 4;  // legacy SGML comment wrapper around <style> contents
        } else if (s.compare(i, 3, "-->") == 0) {
            i += 3;
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

SimpleSelector parse_simple(const std::string& tok) {
    SimpleSelector ss;
    size_t i = 0;
    // leading type name (until . or #)
    while (i < tok.size() && tok[i] != '.' && tok[i] != '#') i++;
    std::string type = tok.substr(0, i);
    if (type != "*" && !type.empty()) ss.tag = to_lower(type);
    while (i < tok.size()) {
        char kind = tok[i++];
        size_t start = i;
        while (i < tok.size() && tok[i] != '.' && tok[i] != '#') i++;
        std::string name = tok.substr(start, i - start);
        if (kind == '#') ss.id = name;
        else if (kind == '.') ss.classes.push_back(name);
    }
    return ss;
}

Selector parse_selector(const std::string& text) {
    Selector sel;
    for (const std::string& part : split(trim(text), ' ')) {
        std::string p = trim(part);
        if (p.empty()) continue;
        sel.chain.push_back(parse_simple(p));
    }
    // specificity
    int a = 0, b = 0, c = 0;
    for (const auto& ss : sel.chain) {
        if (!ss.id.empty()) a++;
        b += (int)ss.classes.size();
        if (!ss.tag.empty()) c++;
    }
    sel.specificity = a * 100 + b * 10 + c;
    return sel;
}

}  // namespace

std::vector<Declaration> parse_inline_style(const std::string& src) {
    std::vector<Declaration> decls;
    for (const std::string& d : split(strip_comments(src), ';')) {
        std::string t = trim(d);
        if (t.empty()) continue;
        size_t colon = t.find(':');
        if (colon == std::string::npos) continue;
        Declaration dec;
        dec.property = to_lower(trim(t.substr(0, colon)));
        dec.value = trim(t.substr(colon + 1));
        if (!dec.property.empty()) decls.push_back(std::move(dec));
    }
    return decls;
}

std::vector<Rule> parse_css(const std::string& src_in) {
    std::string src = strip_comments(src_in);
    std::vector<Rule> rules;
    int order = 0;
    size_t i = 0;
    while (i < src.size()) {
        size_t brace = src.find('{', i);
        if (brace == std::string::npos) break;
        std::string prelude = src.substr(i, brace - i);
        size_t close = src.find('}', brace + 1);
        if (close == std::string::npos) break;
        std::string body = src.substr(brace + 1, close - brace - 1);
        i = close + 1;

        Rule rule;
        rule.order = order++;
        for (const std::string& sel_text : split(prelude, ',')) {
            std::string t = trim(sel_text);
            if (!t.empty()) rule.selectors.push_back(parse_selector(t));
        }
        rule.decls = parse_inline_style(body);
        if (!rule.selectors.empty() && !rule.decls.empty())
            rules.push_back(std::move(rule));
    }
    return rules;
}

}  // namespace hv
