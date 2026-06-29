#include "font.h"

#include <X11/Xft/Xft.h>

namespace hv {

FontCache::~FontCache() { clear(); }

void FontCache::clear() {
    for (auto& [k, f] : cache_)
        if (f) XftFontClose(dpy_, f);
    cache_.clear();
}

XftFont* FontCache::get(const ComputedStyle& s) {
    return get(s.font_family, s.font_size, s.font_weight, s.font_style);
}

XftFont* FontCache::get(const std::string& family, float px, FontWeight w, FontStyle st) {
    int ipx = (int)(px * 4 + 0.5f);  // quantise sub-pixel sizes for the key
    Key key{family, ipx, (int)w, (int)st};
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    int weight = (w == FontWeight::Bold) ? XFT_WEIGHT_BOLD : XFT_WEIGHT_MEDIUM;
    int slant = (st == FontStyle::Italic) ? XFT_SLANT_ITALIC : XFT_SLANT_ROMAN;

    XftFont* f = XftFontOpen(dpy_, screen_,
                             XFT_FAMILY, XftTypeString, family.c_str(),
                             XFT_PIXEL_SIZE, XftTypeDouble, (double)px,
                             XFT_WEIGHT, XftTypeInteger, weight,
                             XFT_SLANT, XftTypeInteger, slant,
                             nullptr);
    if (!f)  // last-ditch fallback
        f = XftFontOpen(dpy_, screen_, XFT_FAMILY, XftTypeString, "sans",
                        XFT_PIXEL_SIZE, XftTypeDouble, (double)px, nullptr);
    cache_[key] = f;
    return f;
}

int FontCache::measure(XftFont* f, const std::string& text) {
    if (!f || text.empty()) return 0;
    XGlyphInfo gi;
    XftTextExtentsUtf8(dpy_, f, (const FcChar8*)text.data(), (int)text.size(), &gi);
    return gi.xOff;  // advance width
}

}  // namespace hv
