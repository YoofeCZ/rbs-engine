#include "ui/fonts.h"
#include "core/log.h"
#include "core/vfs.h"

#include <algorithm>
#include <cmath>

#include <map>
#include <vector>

namespace rbs {
namespace fonts {

static std::map<std::pair<std::string, int>, Font> g_fonts;

static std::vector<int> Codepoints() {
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 160; c < 384; ++c) cps.push_back(c);  // Latin-1 + Latin Extended-A (č, ř, š, ž, ů...)
    for (int c : {0x2026, 0x2013, 0x2014, 0x2022, 0x221E, 0x25B6, 0x2192, 0x2190, 0x2191, 0x2193, 0x00D7}) cps.push_back(c);
    return cps;
}

Font Get(const std::string& name, int size) {
    size = std::max(6, std::min(size, 256));
    auto key = std::make_pair(name, size);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    Font f = GetFontDefault();
    std::string path;
    for (const char* ext : {".ttf", ".otf"}) {
        path = vfs::Resolve("fonts/" + name + ext);
        if (!path.empty()) break;
    }
    if (!path.empty()) {
        static std::vector<int> cps = Codepoints();
        Font loaded = LoadFontEx(path.c_str(), size, cps.data(), (int)cps.size());
        if (loaded.texture.id != 0) {
            SetTextureFilter(loaded.texture, TEXTURE_FILTER_BILINEAR);
            f = loaded;
        }
    } else if (name != "hud") {
        LOG_WARN("Font not found: fonts/%s", name.c_str());
    }
    if (f.texture.id == GetFontDefault().texture.id && name == "hud") return Get("title", size);
    g_fonts[key] = f;
    return f;
}

void UnloadAll() {
    for (auto& kv : g_fonts)
        if (kv.second.texture.id != GetFontDefault().texture.id) UnloadFont(kv.second);
    g_fonts.clear();
}

void Text(const std::string& name, const char* text, float x, float y, int size, Color c) {
    Font f = Get(name, size);
    DrawTextEx(f, text, {roundf(x), roundf(y)}, (float)size, 0, c);
}

float Width(const std::string& name, const char* text, int size) {
    Font f = Get(name, size);
    return MeasureTextEx(f, text, (float)size, 0).x;
}

}  // namespace fonts
}  // namespace rbs
