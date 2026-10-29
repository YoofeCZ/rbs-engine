// RBS Engine - font cache. Fonts live in fonts/<name>.ttf|.otf (VFS, moddable) and are
// rasterized per pixel size on demand (Latin + Latin Extended, so Czech text works).
#pragma once

#include <raylib.h>
#include <string>

namespace rbs {
namespace fonts {

Font Get(const std::string& name, int pixelSize);  // name: "ui", "ui_bold", "mono", "title", "hud"...
void UnloadAll();
// helpers using the "ui" font
void Text(const std::string& name, const char* text, float x, float y, int size, Color c);
float Width(const std::string& name, const char* text, int size);

}  // namespace fonts
}  // namespace rbs
