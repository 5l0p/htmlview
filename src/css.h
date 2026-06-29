// CSS parser for the htmlview subset.
//
// Supports: type, class (.c), id (#i) selectors, descendant combinator
// ("div p"), and selector grouping ("h1, h3"). Declarations are stored as raw
// property/value strings; interpretation (colors, lengths) happens in style.cpp.
#pragma once

#include <string>
#include <vector>

namespace hv {

struct Declaration {
    std::string property;  // lowercased
    std::string value;     // trimmed, original case
};

// One compound selector, e.g. "div.note#x". Empty tag means "*".
struct SimpleSelector {
    std::string tag;                     // lowercased element name, "" = any
    std::string id;                      // without '#'
    std::vector<std::string> classes;    // without '.'
    std::string pseudo;                  // e.g. "hover"/"visited", "" = none
};

// A full selector: a descendant chain whose last element is the subject.
// e.g. "div p" -> chain = [div, p]; matches a <p> with a <div> ancestor.
struct Selector {
    std::vector<SimpleSelector> chain;
    int specificity = 0;  // a*100 (id) + b*10 (class) + c*1 (type)
};

struct Rule {
    std::vector<Selector> selectors;  // grouped: any may match
    std::vector<Declaration> decls;
    int order = 0;  // source order, for cascade tie-breaking
};

std::vector<Rule> parse_css(const std::string& src);

// Parse a `style="..."` attribute body (declarations only) into a list.
std::vector<Declaration> parse_inline_style(const std::string& src);

}  // namespace hv
