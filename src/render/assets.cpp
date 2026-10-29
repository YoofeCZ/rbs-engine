#include "render/assets.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/vfs.h"

#include <nlohmann/json.hpp>
#include <rlgl.h>

namespace rbs {
namespace assets {

static std::map<std::string, TextureInfo> g_textures;
static std::map<std::string, Texture2D> g_images;
static std::map<std::string, ModelAsset> g_models;
static std::map<std::string, Sound> g_sounds;
static std::map<std::string, Music> g_music;
static Shader g_modelShader{};
static bool g_haveModelShader = false;
static Cvar* r_texfilter = nullptr;

static const char* kImageExts[] = {".png", ".tga", ".jpg", ".bmp", ".qoi"};

void Init() { r_texfilter = cvars::Register("r_texfilter", "1", true, "0 = nearest (crunchy), 1 = bilinear, 2 = trilinear"); }

static int FilterMode() {
    int f = r_texfilter ? r_texfilter->i : 1;
    return f <= 0 ? TEXTURE_FILTER_POINT : (f == 1 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_TRILINEAR);
}

static Texture2D MakeChecker() {
    Image img = GenImageChecked(64, 64, 16, 16, Color{255, 0, 255, 255}, Color{20, 20, 20, 255});
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

static Texture2D LoadTex(const std::string& path, bool* alpha) {
    Image img = LoadImage(path.c_str());
    if (!img.data) return Texture2D{};
    if (alpha) {
        *alpha = false;
        if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 || img.format == PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA) {
            Color* px = LoadImageColors(img);
            for (int i = 0; i < img.width * img.height; ++i)
                if (px[i].a < 250) { *alpha = true; break; }
            UnloadImageColors(px);
        }
    }
    ImageMipmaps(&img);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, FilterMode());
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

bool IsToolTexture(const std::string& name) { return name == "clip" || name == "trigger" || name == "nodraw" || name == "skip"; }

TextureInfo& GetTexture(const std::string& name) {
    auto it = g_textures.find(name);
    if (it != g_textures.end()) return it->second;
    TextureInfo info;
    info.name = name;
    std::string path;
    for (const char* ext : kImageExts) {
        path = vfs::Resolve("textures/" + name + ext);
        if (!path.empty()) break;
    }
    if (!path.empty()) info.tex = LoadTex(path, &info.hasAlpha);
    if (info.tex.id == 0) {
        if (name != "sky" && !IsToolTexture(name)) LOG_WARN("Missing texture: %s", name.c_str());
        info.tex = MakeChecker();
        info.missing = true;
    }
    std::string meta;
    if (vfs::ReadText("textures/" + name + ".json", meta)) {
        auto j = nlohmann::json::parse(meta, nullptr, false, true);
        if (!j.is_discarded()) {
            info.fullbright = j.value("fullbright", false);
            info.surface = j.value("surface", info.surface);
            if (j.contains("scroll")) info.scroll = {j["scroll"][0].get<float>(), j["scroll"][1].get<float>()};
        }
    }
    return g_textures[name] = info;
}

Texture2D GetTexturePath(const std::string& rel) {
    auto it = g_images.find(rel);
    if (it != g_images.end()) return it->second;
    std::string path = vfs::Resolve(rel);
    Texture2D t{};
    if (!path.empty()) t = LoadTex(path, nullptr);
    if (t.id == 0) {
        LOG_WARN("Missing image: %s", rel.c_str());
        t = MakeChecker();
    }
    return g_images[rel] = t;
}

std::vector<std::string> ListTextures() {
    std::vector<std::string> out;
    for (auto& f : vfs::List("textures", "", true)) {
        std::string lower = ToLower(f);
        bool img = false;
        for (const char* ext : kImageExts)
            if (EndsWith(lower, ext)) img = true;
        if (!img) continue;
        std::string name = f.substr(9);  // strip "textures/"
        name = name.substr(0, name.find_last_of('.'));
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    for (const char* tool : {"sky", "clip", "trigger", "nodraw"})
        if (std::find(out.begin(), out.end(), tool) == out.end()) out.push_back(tool);
    std::sort(out.begin(), out.end());
    return out;
}

void ApplyTextureFilter() {
    for (auto& kv : g_textures) SetTextureFilter(kv.second.tex, FilterMode());
}

void SetModelShader(Shader s) {
    g_modelShader = s;
    g_haveModelShader = true;
    for (auto& kv : g_models)
        for (int i = 0; i < kv.second.model.materialCount; ++i) kv.second.model.materials[i].shader = s;
}

ModelAsset& GetModel(const std::string& rel) {
    auto it = g_models.find(rel);
    if (it != g_models.end()) return it->second;
    ModelAsset a;
    std::string path = vfs::Resolve(rel);
    if (!path.empty()) a.model = LoadModel(path.c_str());
    if (a.model.meshCount == 0) {
        LOG_WARN("Missing model: %s", rel.c_str());
        a.model = LoadModelFromMesh(GenMeshCube(16, 16, 16));
        a.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = MAGENTA;
        a.missing = true;
    } else {
        a.anims = LoadModelAnimations(path.c_str(), &a.animCount);
        for (int i = 0; i < a.animCount; ++i) a.animByName[ToLower(a.anims[i].name)] = i;
        for (int i = 0; i < a.model.materialCount; ++i) {
            Texture2D& t = a.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
            if (t.id > 0) {
                GenTextureMipmaps(&t);
                SetTextureFilter(t, FilterMode());
            }
        }
    }
    if (g_haveModelShader)
        for (int i = 0; i < a.model.materialCount; ++i) a.model.materials[i].shader = g_modelShader;
    a.bounds = GetModelBoundingBox(a.model);
    return g_models[rel] = a;
}

Sound GetSound(const std::string& rel) {
    auto it = g_sounds.find(rel);
    if (it != g_sounds.end()) return it->second;
    Sound s{};
    std::string path = vfs::Resolve(rel);
    if (!path.empty() && IsAudioDeviceReady()) s = LoadSound(path.c_str());
    else if (path.empty()) LOG_WARN("Missing sound: %s", rel.c_str());
    return g_sounds[rel] = s;
}

Music* GetMusic(const std::string& rel) {
    auto it = g_music.find(rel);
    if (it != g_music.end()) return &it->second;
    std::string path = vfs::Resolve(rel);
    if (path.empty() || !IsAudioDeviceReady()) return nullptr;
    Music m = LoadMusicStream(path.c_str());
    if (m.frameCount == 0) return nullptr;
    g_music[rel] = m;
    return &g_music[rel];
}

void Shutdown() {
    for (auto& kv : g_textures) UnloadTexture(kv.second.tex);
    for (auto& kv : g_images) UnloadTexture(kv.second);
    for (auto& kv : g_models) {
        // materials share the engine's model shader: detach it so UnloadModel won't free it
        for (int i = 0; i < kv.second.model.materialCount; ++i) {
            kv.second.model.materials[i].shader.id = rlGetShaderIdDefault();
            kv.second.model.materials[i].shader.locs = rlGetShaderLocsDefault();
        }
        UnloadModel(kv.second.model);
        if (kv.second.anims) UnloadModelAnimations(kv.second.anims, kv.second.animCount);
    }
    for (auto& kv : g_sounds)
        if (kv.second.frameCount) UnloadSound(kv.second);
    for (auto& kv : g_music) UnloadMusicStream(kv.second);
    g_textures.clear();
    g_images.clear();
    g_models.clear();
    g_sounds.clear();
    g_music.clear();
}

void ReloadAll() { Shutdown(); }

}  // namespace assets
}  // namespace rbs
