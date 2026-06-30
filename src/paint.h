// Painter: rasterises a positioned LayoutBox tree onto an X drawable.
// Backgrounds and borders use XRender solid fills; text uses Xft.
#pragma once

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include <cstdint>
#include <map>

#include "font.h"
#include "layout.h"

namespace hv {

// Current form interaction state, supplied to the painter so it can draw
// control values, checkbox ticks, and the focused text caret.
struct FormView {
    const FormState* state = nullptr;
    const Element* focused = nullptr;
    bool caret = false;
};

class Painter {
public:
    Painter(Display* dpy, Drawable target, Visual* visual, Colormap cmap, FontCache& fonts,
            ImgPixCache img_cache = nullptr);
    ~Painter();

    // Clear the whole target to white, then paint the tree translated by
    // (-scroll_x, -scroll_y) so callers can implement scrolling.
    void paint(const LayoutBox* root, int width, int height,
               float scroll_x = 0, float scroll_y = 0, const FormView& form = {});

    // Draw an open <select>'s option list below `sel` (document coords shifted
    // by oy, e.g. -scroll_y). Rows are sel->height tall; `selected` highlighted.
    void draw_dropdown(const LayoutBox* sel, int selected, float oy);

private:
    void fill_rect(float x, float y, float w, float h, const RGBA& c);
    void stroke_rect(float x, float y, float w, float h, const RGBA& c);
    void paint_box(const LayoutBox* box, float ox, float oy);
    void paint_widget(const LayoutBox* box, float x, float y);
    void paint_bg_image(const std::string& path, float x, float y, float w, float h);
    // One border side; `horizontal` true for top/bottom. Honors solid/dashed/
    // dotted/double.
    void paint_border_side(float x, float y, float w, float h, bool horizontal,
                           const BorderSide& b);
    XftColor* xft_color(const RGBA& c);

    Display* dpy_;
    Drawable target_;
    Visual* visual_;
    Colormap cmap_;
    FontCache& fonts_;
    Picture pic_;
    XftDraw* xft_;
    GC gc_;
    std::map<uint32_t, XftColor> color_cache_;
    ImgPixCache img_cache_;
    int height_ = 0;  // viewport height, set in paint(), used for culling
    const FormView* form_ = nullptr;  // valid only during paint()
};

}  // namespace hv
