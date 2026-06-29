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

void Painter::stroke_rect(float x, float y, float w, float h, const RGBA& c) {
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
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

    if (box->kind == LayoutBox::Kind::Widget) {
        paint_widget(box, x, y);
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

void Painter::paint_widget(const LayoutBox* box, float x, float y) {
    const RGBA border{120, 120, 120, 255};
    const RGBA white{255, 255, 255, 255};
    const RGBA face{225, 225, 225, 255};
    const RGBA dark{30, 30, 30, 255};
    const RGBA focus{60, 120, 220, 255};

    bool focused = form_ && form_->focused == box->element;
    const WidgetState* ws = nullptr;
    if (form_ && form_->state && box->element) {
        auto it = form_->state->find(box->element);
        if (it != form_->state->end()) ws = &it->second;
    }

    switch (box->widget) {
        case WidgetKind::Checkbox: {
            fill_rect(x, y, box->width, box->height, white);
            stroke_rect(x, y, box->width, box->height, focused ? focus : border);
            if (ws && ws->checked) {
                // draw a tick
                XftColor* col = xft_color(dark);
                XSetForeground(dpy_, gc_, col->pixel);
                int x0 = (int)x + 2, y0 = (int)y + (int)box->height / 2;
                XDrawLine(dpy_, target_, gc_, x0, y0, x0 + 3, y0 + 3);
                XDrawLine(dpy_, target_, gc_, x0 + 3, y0 + 3, (int)(x + box->width) - 2, (int)y + 2);
            }
            break;
        }
        case WidgetKind::Button: {
            fill_rect(x, y, box->width, box->height, face);
            stroke_rect(x, y, box->width, box->height, focused ? focus : border);
            if (box->font && !box->text.empty()) {
                int tw = fonts_.measure(box->font, box->text);
                int tx = (int)(x + (box->width - tw) / 2);
                int baseline = (int)(y + (box->height - (box->font->ascent + box->font->descent)) / 2 +
                                     box->font->ascent);
                XftDrawStringUtf8(xft_, xft_color(dark), box->font, tx, baseline,
                                  (const FcChar8*)box->text.data(), (int)box->text.size());
            }
            break;
        }
        case WidgetKind::Select: {
            fill_rect(x, y, box->width, box->height, face);
            stroke_rect(x, y, box->width, box->height, border);
            if (box->font && !box->text.empty()) {
                int baseline = (int)(y + (box->height - (box->font->ascent + box->font->descent)) / 2 +
                                     box->font->ascent);
                XftDrawStringUtf8(xft_, xft_color(dark), box->font, (int)x + 6, baseline,
                                  (const FcChar8*)box->text.data(), (int)box->text.size());
            }
            // down arrow at the right edge
            XftColor* col = xft_color(dark);
            XSetForeground(dpy_, gc_, col->pixel);
            int ax = (int)(x + box->width) - 12, ay = (int)(y + box->height / 2) - 2;
            XPoint tri[3] = {{(short)ax, (short)ay}, {(short)(ax + 8), (short)ay},
                             {(short)(ax + 4), (short)(ay + 5)}};
            XFillPolygon(dpy_, target_, gc_, tri, 3, Convex, CoordModeOrigin);
            break;
        }
        case WidgetKind::TextInput: {
            fill_rect(x, y, box->width, box->height, white);
            stroke_rect(x, y, box->width, box->height, focused ? focus : border);
            std::string text = ws ? ws->text : std::string();
            if (box->font) {
                int baseline = (int)(y + box->font->ascent + 3);
                int tx = (int)x + 4;
                if (!text.empty())
                    XftDrawStringUtf8(xft_, xft_color(dark), box->font, tx, baseline,
                                      (const FcChar8*)text.data(), (int)text.size());
                if (focused && form_->caret) {
                    int cx = tx + fonts_.measure(box->font, text) + 1;
                    fill_rect(cx, y + 3, 1, box->font->ascent + box->font->descent, dark);
                }
            }
            break;
        }
        default:
            break;
    }
}

void Painter::paint(const LayoutBox* root, int width, int height,
                    float scroll_x, float scroll_y, const FormView& form) {
    form_ = &form;
    // The body background propagates to the whole canvas (incl. the body
    // margin area), matching browser behaviour; default to white otherwise.
    RGBA canvas{255, 255, 255, 255};
    if (root && root->style && !root->style->background_color.transparent())
        canvas = root->style->background_color;
    fill_rect(0, 0, width, height, canvas);
    if (root) paint_box(root, -scroll_x, -scroll_y);
    form_ = nullptr;
}

}  // namespace hv
