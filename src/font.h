// Xft font cache + text measurement.
//
// Opening Xft fonts is expensive, so we memoise by (family, px, weight, style).
// All layout text measurement and painting goes through here.
#pragma once

#include <X11/Xft/Xft.h>

#include <map>
#include <string>
#include <tuple>

#include "style.h"

namespace hv {

class FontCache {
public:
    FontCache(Display* dpy, int screen) : dpy_(dpy), screen_(screen) {}
    ~FontCache();

    // Resolve an XftFont for the given style. Never returns null (falls back).
    XftFont* get(const ComputedStyle& s);
    XftFont* get(const std::string& family, float px, FontWeight w, FontStyle st);

    // Width in pixels of a UTF-8 string in the given font.
    int measure(XftFont* f, const std::string& text);

    // Close all cached fonts. Must be called while the Display is still open;
    // idempotent, and invoked by the destructor as a fallback.
    void clear();

private:
    using Key = std::tuple<std::string, int, int, int>;  // family, px*4, weight, style
    Display* dpy_;
    int screen_;
    std::map<Key, XftFont*> cache_;
};

}  // namespace hv
