// Layout engine: turns the styled DOM into a positioned box tree.
//
// Two formatting contexts:
//   - Block: child block boxes stack vertically (the box model: margin /
//     border / padding / content).
//   - Inline: text + inline elements flow into line boxes with word wrapping,
//     whitespace collapsing (preserved for <pre>) and horizontal alignment.
//
// All geometry is in absolute document pixels. A box's (x,y,width,height) is
// its BORDER box; margins are consumed by the parent during stacking.
#pragma once

#include <Imlib2.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "font.h"
#include "style.h"

namespace hv {

// Interactive form controls. Mutable per-control state (text, checked) lives in
// a FormState map keyed by the source Element, so it survives relayout.
enum class WidgetKind { NoWidget, TextInput, Checkbox, Radio, Button, Select };

struct WidgetState {
    std::string text;       // text-input contents
    bool checked = false;   // checkbox / radio state
    int option = 0;         // selected <option> index for <select>
};
using FormState = std::unordered_map<const Element*, WidgetState>;

struct LayoutBox {
    enum class Kind { Block, Anonymous, TextRun, Image, Marker, Widget,
                      Table, TableRow, TableCell };
    Kind kind = Kind::Block;

    ListStyle marker = ListStyle::NoMarker;  // for Kind::Marker shape bullets
    WidgetKind widget = WidgetKind::NoWidget;    // for Kind::Widget controls

    const Element* element = nullptr;     // source element (may be null for anon/text)
    const ComputedStyle* style = nullptr; // style to paint with

    float x = 0, y = 0, width = 0, height = 0;  // border box, document coords

    // TextRun payload.
    std::string text;
    XftFont* font = nullptr;
    RGBA color{0, 0, 0, 255};
    TextDecoration decoration = TextDecoration::NoDecoration;

    // Image payload.
    std::string img_src;
    std::string bg_image;  // resolved local path for background-image (Block boxes)

    // Link target for hit-testing (propagated from enclosing <a>).
    std::string href;

    std::vector<std::unique_ptr<LayoutBox>> children;

    LayoutBox* add(std::unique_ptr<LayoutBox> c) {
        children.push_back(std::move(c));
        return children.back().get();
    }
};

// Given an <img>/asset `src`, return a local file path that Imlib2 can load
// (resolving relative paths, decoding data: URLs, fetching via CGI, etc.), or
// "" if it can't be obtained.
using ResolveImg = std::function<std::string(const std::string& src)>;

// Persistent cache of decoded Imlib2 images keyed by local file path.
// Images are retained across repaints and freed only on page navigation.
using ImgPixCache = std::shared_ptr<std::map<std::string, Imlib_Image>>;

// Build + position the box tree for the document body at the given viewport
// width. Returns the root box; total document height is root->height.
std::unique_ptr<LayoutBox> layout_document(const Document& doc, const StyleMap& sm,
                                           FontCache& fonts, float viewport_width,
                                           const ResolveImg& resolve_image,
                                           const ImgPixCache& img_cache);

struct SelectOption {
    std::string label;  // visible text
    std::string value;  // submitted value (value attr, or label)
};
// The <option> children of a <select>, in order.
std::vector<SelectOption> select_options(const Element* select);

}  // namespace hv
