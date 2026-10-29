// RBS Engine - map data (brushes + entities + global settings), stored as JSON.
#pragma once

#include "world/brush.h"

#include <nlohmann/json.hpp>

namespace rbs {

using json = nlohmann::json;

struct MapEntity {
    std::string classname = "info_null";
    Vector3 origin{0, 0, 0};
    float angle = 0;      // yaw in degrees
    json props = json::object();  // free-form key/values, editable in the editor
    json components = json::array();  // [{"type": "rotator", "props": {...}}, ...]

    std::string Str(const char* key, const std::string& def = "") const;
    float Num(const char* key, float def = 0) const;
    Vector3 Vec(const char* key, Vector3 def) const;
    bool Has(const char* key) const { return props.contains(key); }
};

struct MapSettings {
    std::string title = "Untitled";
    std::string author;
    // Sky (procedural shader)
    Vector3 skyZenith{0.25f, 0.05f, 0.04f};
    Vector3 skyHorizon{0.95f, 0.35f, 0.12f};
    Vector3 skyClouds{0.35f, 0.12f, 0.08f};
    // Sun (baked into lightmaps, traced against sky faces)
    Vector3 sunDir{-0.4f, -0.6f, -0.3f};
    Vector3 sunColor{1.0f, 0.7f, 0.45f};
    float sunIntensity = 0.0f;  // 0 = no sun
    Vector3 ambient{0.12f, 0.1f, 0.1f};
    Vector3 fogColor{0.2f, 0.08f, 0.05f};
    float fogDensity = 0.0f;
    float lightmapScale = 8.0f;  // world units per luxel
    float skyLight = 0.35f;      // ambient light coming from visible sky (needs bounce pass)
    float bounce = 1.0f;         // indirect light strength (0 = off)
    float ao = 0.6f;             // ambient occlusion strength (0 = off)
    float gravity = 800.0f;
    std::string music;
    std::string nextMap;
};

struct Map {
    MapSettings settings;
    std::vector<Brush> brushes;
    std::vector<MapEntity> entities;

    void Clear();
    json ToJson() const;
    bool FromJson(const json& j, std::string* err = nullptr);
    bool Load(const std::string& relPath, std::string* err = nullptr);  // via VFS
    bool Save(const std::string& relPath) const;                        // into VFS write dir
    std::string ToString() const { return ToJson().dump(1); }
    // Removes brushes whose owner entity was deleted and fixes indices after deleting entity `idx`.
    void RemoveEntity(int idx);
};

json Vec3ToJson(Vector3 v);
Vector3 JsonToVec3(const json& j, Vector3 def = {0, 0, 0});

}  // namespace rbs
