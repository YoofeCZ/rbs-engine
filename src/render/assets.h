// RBS Engine - asset caches (textures, models, sounds). Everything is loaded through
// the VFS, so mods can override or add assets.
#pragma once

#include "core/common.h"

#include <map>

namespace rbs {

// Optional per-texture metadata: textures/<name>.json
//   { "fullbright": true, "surface": "metal", "scroll": [0.1, 0], "nocollide": false }
struct TextureInfo {
    Texture2D tex{};
    std::string name;
    bool fullbright = false;
    bool missing = false;
    bool hasAlpha = false;
    std::string surface = "concrete";
    Vector2 scroll{0, 0};
};

struct ModelAsset {
    Model model{};
    ModelAnimation* anims = nullptr;
    int animCount = 0;
    std::map<std::string, int> animByName;
    BoundingBox bounds{};
    bool missing = false;
    int FindAnim(const std::string& name) const {
        auto it = animByName.find(name);
        return it == animByName.end() ? -1 : it->second;
    }
};

namespace assets {

void Init();
void Shutdown();
void ReloadAll();  // drops caches (after a mod switch)

// Texture names are relative to textures/ without extension ("brick_red").
// Special tool textures: sky, clip, trigger, nodraw.
TextureInfo& GetTexture(const std::string& name);
Texture2D GetTexturePath(const std::string& relPath);  // any image path ("sprites/flare.png")
bool IsToolTexture(const std::string& name);           // invisible in game
std::vector<std::string> ListTextures();
void ApplyTextureFilter();

ModelAsset& GetModel(const std::string& relPath);
void SetModelShader(Shader s);

Sound GetSound(const std::string& relPath);
Music* GetMusic(const std::string& relPath);

}  // namespace assets
}  // namespace rbs
