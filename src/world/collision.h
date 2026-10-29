// RBS Engine - collision detection against brushes (swept AABB, Quake 3 style).
#pragma once

#include "world/map.h"

namespace rbs {

struct TraceResult {
    float fraction = 1.0f;
    Vector3 endpos{0, 0, 0};
    Plane plane;
    bool startSolid = false;
    bool allSolid = false;
    int brush = -1;   // index into Map::brushes
    int face = -1;    // face index inside that brush (-1 for bevel planes)
    int entity = -1;  // game entity hit (filled by the game layer)
    bool Hit() const { return fraction < 1.0f || startSolid; }
};

struct CollisionPlane {
    Plane plane;
    int face;  // -1 = bevel
};

struct CollisionBrush {
    std::vector<CollisionPlane> planes;
    BoundingBox bounds;
    int brushIndex;
    int mapEntity;   // owner map entity (-1 world)
    bool solid = true;
    bool sky = false;
};

class CollisionWorld {
public:
    // solidFn decides if a given map brush blocks movement (triggers do not).
    void Build(const Map& map, const std::function<bool(const Brush&)>& solidFn);
    void Clear();

    TraceResult Trace(Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs) const;
    TraceResult TraceRay(Vector3 start, Vector3 end) const { return Trace(start, end, {0, 0, 0}, {0, 0, 0}); }
    bool PointSolid(Vector3 p) const;
    bool BoxSolid(Vector3 mins, Vector3 maxs) const;  // absolute box

    // Trace only against the brushes of one map entity (door blocking tests).
    TraceResult TraceEntity(int mapEntity, Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs) const;
    // Moving brush entities (doors, platforms).
    void SetEntityOffset(int mapEntity, Vector3 offset);
    Vector3 EntityOffset(int mapEntity) const;
    void SetEntitySolid(int mapEntity, bool solid);

    const std::vector<CollisionBrush>& Brushes() const { return m_brushes; }

    // Generic helper: sweep a box against a static AABB (used for entities).
    static bool TraceAgainstBox(Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs, Vector3 boxMin, Vector3 boxMax, TraceResult& tr);

private:
    struct Node {
        BoundingBox bounds;
        int left = -1, right = -1;
        int first = 0, count = 0;
    };
    std::vector<CollisionBrush> m_brushes;  // static world brushes first (BVH), then dynamic
    std::vector<int> m_order;               // BVH leaf order
    std::vector<Node> m_nodes;
    std::vector<int> m_dynamic;             // indices of brushes owned by entities
    std::vector<Vector3> m_entityOffsets;   // per map entity
    std::vector<char> m_entitySolid;

    int BuildNode(int first, int count, int depth);
    void TraceBrush(const CollisionBrush& b, Vector3 offset, Vector3 start, Vector3 end, Vector3 ext, TraceResult& tr) const;
};

}  // namespace rbs
