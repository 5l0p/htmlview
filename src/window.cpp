// X11 presentation layer: live window + headless PNG export.

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <Imlib2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "font.h"
#include "layout.h"
#include "paint.h"
#include "view.h"

namespace hv {

int run_window(const Document& doc, const StyleMap& styles, const std::string& title) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { std::fprintf(stderr, "htmlview: cannot open X display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Visual* visual = DefaultVisual(dpy, screen);
    Colormap cmap = DefaultColormap(dpy, screen);
    int depth = DefaultDepth(dpy, screen);
    unsigned long white = WhitePixel(dpy, screen);

    int width = 800, height = 600;
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, width, height,
                                     0, white, white);
    XStoreName(dpy, win, title.c_str());
    XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask);
    XMapWindow(dpy, win);

    imlib_context_set_display(dpy);
    imlib_context_set_visual(visual);
    imlib_context_set_colormap(cmap);

    FontCache fonts(dpy, screen);
    GC gc = XCreateGC(dpy, win, 0, nullptr);

    Pixmap buffer = XCreatePixmap(dpy, win, width, height, depth);
    std::unique_ptr<LayoutBox> root;
    float scroll_y = 0, doc_height = 0;

    auto relayout = [&]() {
        root = layout_document(doc, styles, fonts, (float)width);
        doc_height = root ? root->height : 0;
    };
    auto repaint = [&]() {
        Painter p(dpy, buffer, visual, cmap, fonts);
        p.paint(root.get(), width, height, 0, scroll_y);
        XCopyArea(dpy, buffer, win, gc, 0, 0, width, height, 0, 0);
    };
    auto clamp_scroll = [&]() {
        float max_scroll = std::max(0.0f, doc_height - height);
        scroll_y = std::clamp(scroll_y, 0.0f, max_scroll);
    };

    relayout();

    bool running = true;
    while (running) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) repaint();
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != width || ev.xconfigure.height != height) {
                    width = ev.xconfigure.width;
                    height = ev.xconfigure.height;
                    XFreePixmap(dpy, buffer);
                    buffer = XCreatePixmap(dpy, win, width, height, depth);
                    relayout();
                    clamp_scroll();
                    repaint();
                }
                break;
            case ButtonPress:
                if (ev.xbutton.button == 4) { scroll_y -= 48; clamp_scroll(); repaint(); }
                else if (ev.xbutton.button == 5) { scroll_y += 48; clamp_scroll(); repaint(); }
                break;
            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_q || ks == XK_Escape) running = false;
                else if (ks == XK_Down) { scroll_y += 48; clamp_scroll(); repaint(); }
                else if (ks == XK_Up) { scroll_y -= 48; clamp_scroll(); repaint(); }
                else if (ks == XK_Page_Down) { scroll_y += height - 40; clamp_scroll(); repaint(); }
                else if (ks == XK_Page_Up) { scroll_y -= height - 40; clamp_scroll(); repaint(); }
                break;
            }
        }
    }

    XFreePixmap(dpy, buffer);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    fonts.clear();  // close fonts before the Display does
    XCloseDisplay(dpy);
    return 0;
}

int render_to_png(const Document& doc, const StyleMap& styles, int width,
                  const std::string& path) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { std::fprintf(stderr, "htmlview: cannot open X display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Visual* visual = DefaultVisual(dpy, screen);
    Colormap cmap = DefaultColormap(dpy, screen);
    int depth = DefaultDepth(dpy, screen);

    imlib_context_set_display(dpy);
    imlib_context_set_visual(visual);
    imlib_context_set_colormap(cmap);

    FontCache fonts(dpy, screen);
    auto root = layout_document(doc, styles, fonts, (float)width);
    int height = std::max(1, (int)std::ceil(root ? root->height : 1));

    Pixmap buffer = XCreatePixmap(dpy, RootWindow(dpy, screen), width, height, depth);
    {
        Painter p(dpy, buffer, visual, cmap, fonts);
        p.paint(root.get(), width, height, 0, 0);
    }
    XSync(dpy, False);

    imlib_context_set_drawable(buffer);
    Imlib_Image img = imlib_create_image_from_drawable(0, 0, 0, width, height, 1);
    if (!img) {
        std::fprintf(stderr, "htmlview: imlib capture failed\n");
        XFreePixmap(dpy, buffer);
        XCloseDisplay(dpy);
        return 1;
    }
    imlib_context_set_image(img);
    imlib_image_set_format("png");
    imlib_save_image(path.c_str());
    imlib_free_image();

    std::fprintf(stderr, "wrote %s (%dx%d)\n", path.c_str(), width, height);
    XFreePixmap(dpy, buffer);
    fonts.clear();  // close fonts before the Display does
    XCloseDisplay(dpy);
    return 0;
}

}  // namespace hv
