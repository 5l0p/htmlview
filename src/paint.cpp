#include "paint.h"

#include <X11/extensions/Xrender.h>
#include <Imlib2.h>

#include <algorithm>

namespace hv {

namespace {
XRenderColor to_xrender(const RGBA& c) {
    // XRender wants premultiplied 16-bit components.
    auto comp = [&](uint8_t v) -> unsigned short {
        return (unsigned short)((int)v * 257 * c.a / 255);
    };
    XRenderColor x;
    x.red = comp(c.r);
    x.green = comp(c.g);
    x.blue = comp(c.b);
    x.alpha = (unsigned short)(c.a * 257);
    return x;
}
}  // namespace

Painter::Painter(Display* dpy, Drawable target, Visual* visual, Colormap cmap, FontCache& fonts)
    : dpy_(dpy), target_(target), visual_(visual), cmap_(cmap), fonts_(fonts) {
    XRenderPictFormat* fmt = XRenderFindVisualFormat(dpy_, visual_);
    pic_ = XRenderCreatePicture(dpy_, target_, fmt, 0, nullptr);
    xft_ = XftDrawCreate(dpy_, target_, visual_, cmap_);
    gc_ = XCreateGC(dpy_, target_, 0, nullptr);
}

Painter::~Painter() {
    for (auto& [k, col] : color_cache_) XftColorFree(dpy_, visual_, cmap_, &col);
    XftDrawDestroy(xft_);
    XFreeGC(dpy_, gc_);
    XRenderFreePicture(dpy_, pic_);
}

XftColor* Painter::xft_color(const RGBA& c) {
    uint32_t key = (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
    auto it = color_cache_.find(key);
    if (it != color_cache_.end()) return &it->second;
    XftColor col;
    XRenderColor rc = {(unsigned short)(c.r * 257), (unsigned short)(c.g * 257),
                       (unsigned short)(c.b * 257), (unsigned short)(c.a * 257)};
    XftColorAllocValue(dpy_, visual_, cmap_, &rc, &col);
    return &(color_cache_[key] = col);
}

void Painter::fill_rect(float x, float y, float w, float h, const RGBA& c) {
    if (w <= 0 || h <= 0 || c.transparent()) return;
    XRenderColor xc = to_xrender(c);
    XRenderFillRectangle(dpy_, PictOpOver, pic_, &xc, (int)x, (int)y,
                         (unsigned)(w + 0.5f), (unsigned)(h + 0.5f));
}

void Painter::paint_box(const LayoutBox* box, float ox, float oy) {
    float x = box->x + ox, y = box->y + oy;

    if (box->style) {
        const ComputedStyle& s = *box->style;
        if (!s.background_color.transparent())
            fill_rect(x, y, box->width, box->height, s.background_color);
        if (s.border.any()) {
            const Border& b = s.border;
            if (b.top.width > 0)    fill_rect(x, y, box->width, b.top.width, b.top.color);
            if (b.bottom.width > 0) fill_rect(x, y + box->height - b.bottom.width,
                                              box->width, b.bottom.width, b.bottom.color);
            if (b.left.width > 0)   fill_rect(x, y, b.left.width, box->height, b.left.color);
            if (b.right.width > 0)  fill_rect(x + box->width - b.right.width, y,
                                              b.right.width, box->height, b.right.color);
        }
    }

    if (box->kind == LayoutBox::Kind::Marker) {
        XftColor* col = xft_color(box->color);
        XSetForeground(dpy_, gc_, col->pixel);
        int s = std::max(4, (int)(box->width * 0.42f));  // shape diameter/side
        int cx = (int)(x + box->width - s);
        int cy = (int)(y + box->height * 0.5f - s / 2.0f);
        switch (box->marker) {
            case ListStyle::Disc:
                XFillArc(dpy_, target_, gc_, cx, cy, s, s, 0, 360 * 64);
                break;
            case ListStyle::Circle:
                XDrawArc(dpy_, target_, gc_, cx, cy, s, s, 0, 360 * 64);
                break;
            case ListStyle::Square:
                XFillRectangle(dpy_, target_, gc_, cx, cy, s, s);
                break;
            default:
                break;
        }
    }

    if (box->kind == LayoutBox::Kind::Image && !box->img_src.empty()) {
        Imlib_Image im = imlib_load_image(box->img_src.c_str());
        if (im) {
            imlib_context_set_image(im);
            int iw = imlib_image_get_width(), ih = imlib_image_get_height();
            imlib_context_set_drawable(target_);
            imlib_context_set_anti_alias(1);
            imlib_render_image_part_on_drawable_at_size(
                0, 0, iw, ih, (int)x, (int)y, (int)box->width, (int)box->height);
            imlib_free_image();
        }
    }

    if (box->kind == LayoutBox::Kind::TextRun && box->font && !box->text.empty()) {
        XftColor* col = xft_color(box->color);
        int baseline = (int)(y + box->font->ascent);
        XftDrawStringUtf8(xft_, col, box->font, (int)x, baseline,
                          (const FcChar8*)box->text.data(), (int)box->text.size());
        if (box->decoration == TextDecoration::Underline)
            fill_rect(x, baseline + 1, box->width, 1, box->color);
    }

    for (const auto& c : box->children) paint_box(c.get(), ox, oy);
}

void Painter::paint(const LayoutBox* root, int width, int height,
                    float scroll_x, float scroll_y) {
    fill_rect(0, 0, width, height, RGBA{255, 255, 255, 255});
    if (root) paint_box(root, -scroll_x, -scroll_y);
}

}  // namespace hv
