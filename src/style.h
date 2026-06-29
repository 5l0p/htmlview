// Computed style + the cascade for the htmlview subset.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "css.h"
#include "html_parser.h"

namespace hv {

struct RGBA {
    uint8_t r = 0, g = 0, b = 0, a = 0;  // a=0 => transparent
    bool transparent() const { return a == 0; }
    bool operator==(const RGBA& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

enum class DisplayType { Block, Inline, ListItem, NoDisplay, Table, TableRow, TableCell };
enum class FontStyle { Normal, Italic };
enum class FontWeight { Normal, Bold };
enum class ListStyle { Disc, Circle, Square, Decimal, NoMarker };
enum class TextAlign { Left, Center, Right };
enum class TextDecoration { NoDecoration, Underline };

struct Edges {  // top, right, bottom, left (CSS order)
    float top = 0, right = 0, bottom = 0, left = 0;
};

struct BorderSide {
    float width = 0;
    RGBA color;  // defaults transparent; rendered only if width>0 && !transparent
};

struct Border {
    BorderSide top, right, bottom, left;
    bool any() const {
        return top.width > 0 || right.width > 0 || bottom.width > 0 || left.width > 0;
    }
};

constexpr float kAuto = -1.0f;

struct ComputedStyle {
    DisplayType display = DisplayType::Inline;

    // Inherited.
    std::string font_family = "sans";
    float font_size = 16.0f;  // px
    FontWeight font_weight = FontWeight::Normal;
    FontStyle font_style = FontStyle::Normal;
    RGBA color{0, 0, 0, 255};
    TextAlign text_align = TextAlign::Left;
    ListStyle list_style = ListStyle::Disc;
    bool preserve_ws = false;  // set true for <pre>

    // Non-inherited.
    RGBA background_color;  // transparent by default
    Edges margin, padding;
    Border border;
    TextDecoration text_decoration = TextDecoration::NoDecoration;
    float width = kAuto, height = kAuto;
};

using StyleMap = std::unordered_map<const Element*, ComputedStyle>;

// Resolve computed styles for every element in the document, applying the UA
// default stylesheet, the author rules, and inline style="" attributes.
StyleMap resolve_styles(const Document& doc, const std::vector<Rule>& author);

// Value parsers (also used by layout/paint). Return false on failure.
bool parse_color(const std::string& in, RGBA& out);
bool parse_length_px(const std::string& in, float em_base, float& out);

// Print resolved styles for --dump-style.
void dump_styles(const Node* root, const StyleMap& sm, std::string& out, int indent = 0);

}  // namespace hv
