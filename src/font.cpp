#include "font.h"

#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>

#include <sstream>

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

    int weight = (w == FontWeight::Bold) ? FC_WEIGHT_BOLD : FC_WEIGHT_MEDIUM;
    int slant = (st == FontStyle::Italic) ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;

    // Build a pattern with every requested family, in priority order, so
    // fontconfig picks the first one actually installed (then its own fallback).
    FcPattern* pat = FcPatternCreate();
    std::stringstream ss(family);
    std::string fam;
    bool any = false;
    while (std::getline(ss, fam, ',')) {
        if (fam.empty()) continue;
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)fam.c_str());
        any = true;
    }
    if (!any) FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)"sans");
    FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)px);
    FcPatternAddInteger(pat, FC_WEIGHT, weight);
    FcPatternAddInteger(pat, FC_SLANT, slant);

    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    XftDefaultSubstitute(dpy_, screen_, pat);
    FcResult res;
    FcPattern* match = FcFontMatch(nullptr, pat, &res);
    FcPatternDestroy(pat);

    XftFont* f = match ? XftFontOpenPattern(dpy_, match) : nullptr;
    if (!f) {  // last-ditch fallback
        if (match) FcPatternDestroy(match);
        f = XftFontOpen(dpy_, screen_, XFT_FAMILY, XftTypeString, "sans",
                        XFT_PIXEL_SIZE, XftTypeDouble, (double)px, nullptr);
    }
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
