// Computed style + the cascade for the htmlview subset.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
enum class VerticalAlign { Baseline, Sub, Super, TextTop, TextBottom, Middle, Top, Bottom };

struct Edges {  // top, right, bottom, left (CSS order)
    float top = 0, right = 0, bottom = 0, left = 0;
};

// A CSS length that may be auto, a pixel value, or a percentage of a base.
struct Length {
    enum Unit { Auto, Px, Percent };
    Unit unit = Auto;
    float value = 0;
    bool is_auto() const { return unit == Auto; }
    float resolve(float base, float fallback) const {
        if (unit == Px) return value;
        if (unit == Percent) return value / 100.0f * base;
        return fallback;
    }
};

// No "None" member — collides with the X11 macro; width==0 means no border.
enum class BorderStyle { Solid, Dashed, Dotted, Double };

struct BorderSide {
    float width = 0;
    RGBA color;  // defaults transparent; rendered only if width>0 && !transparent
    BorderStyle style = BorderStyle::Solid;
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
    RGBA background_color;        // transparent by default
    std::string background_image; // CSS url() (or data:), "" = none
    Edges margin, padding;
    bool margin_left_auto = false, margin_right_auto = false;  // for centering
    Border border;
    TextDecoration text_decoration = TextDecoration::NoDecoration;
    VerticalAlign vertical_align = VerticalAlign::Baseline;
    Length width, height;        // default: auto
    Length line_height;          // auto = normal (~1.2x); Px = absolute; Percent = of font-size
    bool border_collapse = false;
};

using StyleMap = std::unordered_map<const Element*, ComputedStyle>;

// Resolve computed styles for every element in the document, applying the UA
// default stylesheet, the author rules, and inline style="" attributes.
// `hovered` (if non-null) activates `:hover` rules for that element and its
// ancestors, so the cascade reflects the current pointer position.
// `errors`, if non-null, receives one entry per unrecognised CSS property
// (vendor-prefixed names are silently skipped). Entries may be duplicated;
// call dump_errors() to sort and deduplicate before display.
StyleMap resolve_styles(const Document& doc, const std::vector<Rule>& author,
                        const Element* hovered = nullptr,
                        std::vector<std::string>* errors = nullptr);

// True if any author rule uses a `:hover` pseudo-class (lets callers skip the
// re-resolve-on-hover machinery when it can have no effect).
bool stylesheet_has_hover(const std::vector<Rule>& author);

// Concatenate the document's CSS in source order: <style> block text plus the
// contents of every <link rel=stylesheet href>, fetched via `fetch` (which
// returns the resource text, or "" on failure).
std::string gather_css(const Document& doc,
                       const std::function<std::string(const std::string&)>& fetch);

// Value parsers (also used by layout/paint). Return false on failure.
bool parse_color(const std::string& in, RGBA& out);
bool parse_length_px(const std::string& in, float em_base, float& out);

// Print resolved styles for --dump-style.
void dump_styles(const Node* root, const StyleMap& sm, std::string& out, int indent = 0);

// Sort, deduplicate, and format an errors vector for --dump-errors output.
void dump_errors(std::vector<std::string> errors, std::string& out);

}  // namespace hv
