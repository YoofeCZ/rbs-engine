// RBS Engine - lightmap baker.
//
// Static lights are baked into a lightmap atlas with ray traced shadows, sun light
// (traced against sky faces), sky light, ambient occlusion and one bounce of indirect
// light. Baking runs on worker threads; results are cached per map in cache/.
#pragma once

#include "world/collision.h"

#include <atomic>
#include <map>
#include <thread>

namespace rbs {

struct BakeLight {
    Vector3 pos{0, 0, 0};
    Vector3 color{1, 1, 1};
    float intensity = 1.0f;
    float radius = 300.0f;
    bool spot = false;
    Vector3 dir{0, -1, 0};
    float coneCos = 0.7f;
    float coneOuterCos = 0.5f;
};

class Lightmap {
public:
    ~Lightmap();
    // Collects light entities from the map and assigns lightmap rectangles to faces.
    // Starts worker threads; poll Progress()/Done() then call Finish() on the main thread.
    void Start(Map& map, const std::string& cacheName);
    float Progress() const;
    bool Done() const;
    bool Finish();  // uploads the texture (main thread)
    void Unload();
    // Fully lit placeholder (for maps without lighting, e.g. in the editor).
    void MakeFullbright(Map& map);

    Texture2D Texture() const { return m_texture; }
    bool Valid() const { return m_texture.id != 0; }
    // Light for dynamic objects: ambient + strongest directional light.
    void LightAtPoint(const Map& map, const CollisionWorld& world, Vector3 p, Vector3& ambient, Vector3& dirColor, Vector3& dir) const;
    // Lightmap color at a point on a face (for decals).
    Vector3 SampleFace(const Map& map, int brush, int face, Vector3 p) const;

    static std::vector<BakeLight> CollectLights(const Map& map);

private:
    struct FaceRef {
        int brush, face;
    };
    Map* m_map = nullptr;
    CollisionWorld m_occluders;
    std::vector<BakeLight> m_lights;
    std::vector<FaceRef> m_faces;
    std::map<std::string, Vector3> m_albedo;
    std::map<std::string, bool> m_fullbright;
    bool IsFullbright(const std::string& tex) const {
        auto it = m_fullbright.find(tex);
        return it != m_fullbright.end() && it->second;
    }
    std::vector<Vector3> m_direct;  // float luxels (direct light)
    std::vector<Vector3> m_final;
    std::vector<Color> m_pixels;
    int m_width = 0, m_height = 0;
    Texture2D m_texture{};
    std::vector<std::thread> m_threads;
    std::atomic<int> m_next{0};
    std::atomic<int> m_doneCount{0};
    std::atomic<int> m_phase{0};
    int m_total = 0;
    bool m_fromCache = false;
    std::string m_cacheFile;
    uint64_t m_hash = 0;

    bool Allocate(Map& map, float luxel);
    void Worker(int phase);
    void RunPhases();
    Vector3 LuxelWorldPos(const BrushFace& f, int i, int j) const;
    Vector3 DirectLight(Vector3 p, Vector3 n, uint32_t seed) const;
    Vector3 Gather(Vector3 p, Vector3 n, uint32_t seed) const;
    Vector3 SampleDirect(int brush, int face, Vector3 p) const;
    bool LoadCache();
    void SaveCache();
    std::thread m_master;
};

}  // namespace rbs
