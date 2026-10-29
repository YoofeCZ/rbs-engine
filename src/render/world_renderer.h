// RBS Engine - converts map brushes to GPU meshes grouped by texture.
#pragma once

#include "render/lightmap.h"
#include "render/renderer.h"

namespace rbs {

class WorldRenderer {
public:
    ~WorldRenderer() { Unload(); }
    // showTools: draw clip/trigger brushes (editor). lightmap may be null.
    void Build(const Map& map, const Lightmap* lightmap, bool showTools);
    void Unload();
    // entityOffsets[i] = translation of map entity i (moving doors), hidden[i] = skip entity brushes
    void Draw(const std::vector<Vector3>* entityOffsets, const std::vector<char>* hiddenEntities, Color tint = WHITE) const;
    bool Empty() const { return m_batches.empty(); }

private:
    struct Batch {
        Mesh mesh{};
        std::string tex;
        int entity = -1;
        bool sky = false;
        Vector2 scroll{0, 0};
    };
    std::vector<Batch> m_batches;
    Texture2D m_lightmap{};
    Material m_material{};
};

}  // namespace rbs
