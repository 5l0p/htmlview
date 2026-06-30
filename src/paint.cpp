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

Painter::Painter(Display* dpy, Drawable target, Visual* visual, Colormap cmap, FontCache& fonts,
                 ImgPixCache img_cache)
    : dpy_(dpy), target_(target), visual_(visual), cmap_(cmap), fonts_(fonts),
      img_cache_(std::move(img_cache)) {
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

void Painter::paint_bg_image(const std::string& path, float x, float y, float w, float h) {
    Imlib_Image im = nullptr;
    if (img_cache_) {
        auto& map = *img_cache_;
        auto it = map.find(path);
        if (it != map.end()) {
            im = it->second;
        } else {
            im = imlib_load_image(path.c_str());
            if (im) map[path] = im;
        }
    } else {
        im = imlib_load_image(path.c_str());
    }
    if (!im) return;
    imlib_context_set_image(im);
    int iw = imlib_image_get_width(), ih = imlib_image_get_height();
    if (iw > 0 && ih > 0) {
        imlib_context_set_drawable(target_);
        imlib_context_set_anti_alias(1);
        for (int ty = (int)y; ty < (int)(y + h); ty += ih)
            for (int tx = (int)x; tx < (int)(x + w); tx += iw) {
                int dw = std::min(iw, (int)(x + w) - tx);
                int dh = std::min(ih, (int)(y + h) - ty);
                imlib_render_image_part_on_drawable_at_size(0, 0, dw, dh, tx, ty, dw, dh);
            }
    }
    if (!img_cache_) imlib_free_image();
}

void Painter::paint_border_side(float x, float y, float w, float h, bool horizontal,
                                const BorderSide& b) {
    if (b.width <= 0 || b.color.transparent()) return;
    switch (b.style) {
        case BorderStyle::Solid:
            fill_rect(x, y, w, h, b.color);
            break;
        case BorderStyle::Double: {
            float t = std::max(1.0f, b.width / 3);
            if (horizontal) { fill_rect(x, y, w, t, b.color); fill_rect(x, y + h - t, w, t, b.color); }
            else { fill_rect(x, y, t, h, b.color); fill_rect(x + w - t, y, t, h, b.color); }
            break;
        }
        case BorderStyle::Dashed:
        case BorderStyle::Dotted: {
            XSetForeground(dpy_, gc_, xft_color(b.color)->pixel);
            int lw = (int)std::max(1.0f, b.width);
            char dashes[2];
            if (b.style == BorderStyle::Dotted) { dashes[0] = (char)lw; dashes[1] = (char)lw; }
            else { dashes[0] = (char)std::max(3, lw * 2); dashes[1] = (char)std::max(2, lw); }
            XSetLineAttributes(dpy_, gc_, lw, LineOnOffDash, CapButt, JoinMiter);
            XSetDashes(dpy_, gc_, 0, dashes, 2);
            if (horizontal) {
                int cy = (int)(y + h / 2);
                XDrawLine(dpy_, target_, gc_, (int)x, cy, (int)(x + w), cy);
            } else {
                int cx = (int)(x + w / 2);
                XDrawLine(dpy_, target_, gc_, cx, (int)y, cx, (int)(y + h));
            }
            XSetLineAttributes(dpy_, gc_, 1, LineSolid, CapButt, JoinMiter);  // reset
            break;
        }
    }
}

void Painter::paint_box(const LayoutBox* box, float ox, float oy) {
    float x = box->x + ox, y = box->y + oy;

    // Skip drawing for boxes entirely outside the viewport, but still recurse
    // into children — they may be absolutely/relatively positioned and in view.
    bool in_view = (y + box->height >= 0.f && y < (float)height_);

    if (in_view && box->style) {
        const ComputedStyle& s = *box->style;
        if (!s.background_color.transparent())
            fill_rect(x, y, box->width, box->height, s.background_color);
        if (!box->bg_image.empty())
            paint_bg_image(box->bg_image, x, y, box->width, box->height);
        if (s.border.any()) {
            const Border& b = s.border;
            float W = box->width, H = box->height;
            paint_border_side(x, y, W, b.top.width, true, b.top);
            paint_border_side(x, y + H - b.bottom.width, W, b.bottom.width, true, b.bottom);
            paint_border_side(x, y, b.left.width, H, false, b.left);
            paint_border_side(x + W - b.right.width, y, b.right.width, H, false, b.right);
        }
    }

    if (in_view && box->kind == LayoutBox::Kind::Marker) {
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

    if (in_view && box->kind == LayoutBox::Kind::Image && !box->img_src.empty()) {
        Imlib_Image im = nullptr;
        if (img_cache_) {
            auto& map = *img_cache_;
            auto it = map.find(box->img_src);
            if (it != map.end()) {
                im = it->second;
            } else {
                im = imlib_load_image(box->img_src.c_str());
                if (im) map[box->img_src] = im;
            }
        } else {
            im = imlib_load_image(box->img_src.c_str());
        }
        if (im) {
            imlib_context_set_image(im);
            int iw = imlib_image_get_width(), ih = imlib_image_get_height();
            imlib_context_set_drawable(target_);
            imlib_context_set_anti_alias(1);
            imlib_render_image_part_on_drawable_at_size(
                0, 0, iw, ih, (int)x, (int)y, (int)box->width, (int)box->height);
            if (!img_cache_) imlib_free_image();
        }
    }

    if (in_view && box->kind == LayoutBox::Kind::Widget) {
        paint_widget(box, x, y);
    }

    if (in_view && box->kind == LayoutBox::Kind::TextRun && box->font && !box->text.empty()) {
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
    bool is_password = false, is_textarea = false;
    if (box->element) {
        is_textarea = box->element->tag == Tag::Textarea;
        if (const std::string* t = box->element->attr("type")) {
            std::string lt = *t;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            is_password = (lt == "password");
        }
    }

    switch (box->widget) {
        case WidgetKind::Radio: {
            int d = (int)std::min(box->width, box->height) - 1;
            XSetForeground(dpy_, gc_, xft_color(white)->pixel);
            XFillArc(dpy_, target_, gc_, (int)x, (int)y, d, d, 0, 360 * 64);
            XSetForeground(dpy_, gc_, xft_color(focused ? focus : border)->pixel);
            XDrawArc(dpy_, target_, gc_, (int)x, (int)y, d, d, 0, 360 * 64);
            if (ws && ws->checked) {
                XSetForeground(dpy_, gc_, xft_color(dark)->pixel);
                XFillArc(dpy_, target_, gc_, (int)x + 3, (int)y + 3, d - 6, d - 6, 0, 360 * 64);
            }
            break;
        }
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
            std::string label;
            if (box->element) {
                auto opts = select_options(box->element);
                int idx = ws ? ws->option : 0;
                if (idx >= 0 && idx < (int)opts.size()) label = opts[idx].label;
            }
            if (box->font && !label.empty()) {
                int baseline = (int)(y + (box->height - (box->font->ascent + box->font->descent)) / 2 +
                                     box->font->ascent);
                XftDrawStringUtf8(xft_, xft_color(dark), box->font, (int)x + 6, baseline,
                                  (const FcChar8*)label.data(), (int)label.size());
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
            if (is_password) {  // mask: one bullet per code point
                int n = 0;
                for (char c : text) if (((unsigned char)c & 0xC0) != 0x80) ++n;
                std::string dots;
                for (int i = 0; i < n; ++i) dots += "\xe2\x80\xa2";
                text = dots;
            }
            if (box->font) {
                int line_h = box->font->ascent + box->font->descent;
                int tx = (int)x + 4;
                if (is_textarea) {
                    // Render each newline-separated line; caret after the last.
                    int ly = (int)y + 3;
                    std::string last;
                    size_t s = 0;
                    while (true) {
                        size_t nl = text.find('\n', s);
                        last = text.substr(s, nl == std::string::npos ? std::string::npos : nl - s);
                        if (!last.empty())
                            XftDrawStringUtf8(xft_, xft_color(dark), box->font, tx,
                                              ly + box->font->ascent,
                                              (const FcChar8*)last.data(), (int)last.size());
                        if (nl == std::string::npos) break;
                        ly += line_h;
                        s = nl + 1;
                    }
                    if (focused && form_->caret) {
                        int cx = tx + fonts_.measure(box->font, last) + 1;
                        fill_rect(cx, ly + 3, 1, line_h, dark);
                    }
                } else {
                    int baseline = (int)(y + box->font->ascent + 3);
                    if (!text.empty())
                        XftDrawStringUtf8(xft_, xft_color(dark), box->font, tx, baseline,
                                          (const FcChar8*)text.data(), (int)text.size());
                    if (focused && form_->caret) {
                        int cx = tx + fonts_.measure(box->font, text) + 1;
                        fill_rect(cx, y + 3, 1, line_h, dark);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void Painter::draw_dropdown(const LayoutBox* sel, int selected, float oy) {
    const RGBA white{255, 255, 255, 255};
    const RGBA border{120, 120, 120, 255};
    const RGBA dark{30, 30, 30, 255};
    const RGBA hl{60, 120, 220, 255};
    const RGBA hl_text{255, 255, 255, 255};
    if (!sel->element || !sel->font) return;

    auto opts = select_options(sel->element);
    float x = sel->x, w = sel->width, rh = sel->height;
    float top = sel->y + sel->height + oy;
    for (int i = 0; i < (int)opts.size(); ++i) {
        float ry = top + i * rh;
        bool on = (i == selected);
        fill_rect(x, ry, w, rh, on ? hl : white);
        stroke_rect(x, ry, w, rh, border);
        int baseline = (int)(ry + (rh - (sel->font->ascent + sel->font->descent)) / 2 +
                             sel->font->ascent);
        XftDrawStringUtf8(xft_, xft_color(on ? hl_text : dark), sel->font, (int)x + 6, baseline,
                          (const FcChar8*)opts[i].label.data(), (int)opts[i].label.size());
    }
}

void Painter::paint(const LayoutBox* root, int width, int height,
                    float scroll_x, float scroll_y, const FormView& form) {
    form_ = &form;
    height_ = height;
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
