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

class Painter {
public:
    Painter(Display* dpy, Drawable target, Visual* visual, Colormap cmap, FontCache& fonts);
    ~Painter();

    // Clear the whole target to white, then paint the tree translated by
    // (-scroll_x, -scroll_y) so callers can implement scrolling.
    void paint(const LayoutBox* root, int width, int height,
               float scroll_x = 0, float scroll_y = 0);

private:
    void fill_rect(float x, float y, float w, float h, const RGBA& c);
    void paint_box(const LayoutBox* box, float ox, float oy);
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
};

}  // namespace hv
