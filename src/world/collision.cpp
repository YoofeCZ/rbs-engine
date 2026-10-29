#include "world/collision.h"

namespace rbs {

static constexpr float kSurfaceClipEps = 0.125f;

void CollisionWorld::Clear() {
    m_brushes.clear();
    m_order.clear();
    m_nodes.clear();
    m_dynamic.clear();
    m_entityOffsets.clear();
    m_entitySolid.clear();
}

static void AddPlaneIfMissing(CollisionBrush& cb, Vector3 n, float d) {
    for (auto& p : cb.planes)
        if (Dot(p.plane.n, n) > 0.9999f) return;
    cb.planes.push_back({{n, d}, -1});
}

void CollisionWorld::Build(const Map& map, const std::function<bool(const Brush&)>& solidFn) {
    Clear();
    m_entityOffsets.assign(map.entities.size(), {0, 0, 0});
    m_entitySolid.assign(map.entities.size(), 1);
    std::vector<int> statics;
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        const Brush& b = map.brushes[i];
        if (b.faces.empty() || !solidFn(b)) continue;
        CollisionBrush cb;
        cb.brushIndex = (int)i;
        cb.mapEntity = b.entity;
        cb.bounds = b.bounds;
        for (size_t f = 0; f < b.faces.size(); ++f) {
            cb.planes.push_back({b.faces[f].plane, (int)f});
            if (b.faces[f].tex.name == "sky") cb.sky = true;
        }
        // axial bevel planes keep boxes from catching on sloped brush edges
        AddPlaneIfMissing(cb, {1, 0, 0}, b.bounds.max.x);
        AddPlaneIfMissing(cb, {-1, 0, 0}, -b.bounds.min.x);
        AddPlaneIfMissing(cb, {0, 1, 0}, b.bounds.max.y);
        AddPlaneIfMissing(cb, {0, -1, 0}, -b.bounds.min.y);
        AddPlaneIfMissing(cb, {0, 0, 1}, b.bounds.max.z);
        AddPlaneIfMissing(cb, {0, 0, -1}, -b.bounds.min.z);
        int idx = (int)m_brushes.size();
        m_brushes.push_back(cb);
        if (b.entity >= 0) m_dynamic.push_back(idx);
        else statics.push_back(idx);
    }
    m_order = statics;
    if (!m_order.empty()) BuildNode(0, (int)m_order.size(), 0);
}

int CollisionWorld::BuildNode(int first, int count, int depth) {
    Node node;
    Vector3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < count; ++i) {
        const auto& b = m_brushes[m_order[first + i]].bounds;
        mn = VMin(mn, b.min);
        mx = VMax(mx, b.max);
    }
    node.bounds = {mn, mx};
    int idx = (int)m_nodes.size();
    m_nodes.push_back(node);
    if (count <= 4 || depth > 40) {
        m_nodes[idx].first = first;
        m_nodes[idx].count = count;
        return idx;
    }
    Vector3 size = mx - mn;
    int axis = (size.x > size.y && size.x > size.z) ? 0 : (size.y > size.z ? 1 : 2);
    auto begin = m_order.begin() + first;
    std::nth_element(begin, begin + count / 2, begin + count, [&](int a, int b) {
        const auto& A = m_brushes[a].bounds;
        const auto& B = m_brushes[b].bounds;
        return Comp(A.min, axis) + Comp(A.max, axis) < Comp(B.min, axis) + Comp(B.max, axis);
    });
    int half = count / 2;
    int l = BuildNode(first, half, depth + 1);
    int r = BuildNode(first + half, count - half, depth + 1);
    m_nodes[idx].left = l;
    m_nodes[idx].right = r;
    return idx;
}

void CollisionWorld::TraceBrush(const CollisionBrush& b, Vector3 offset, Vector3 start, Vector3 end, Vector3 ext, TraceResult& tr) const {
    float enterFrac = -1.0f, leaveFrac = 1.0f;
    const CollisionPlane* clip = nullptr;
    bool getout = false, startout = false;
    start -= offset;
    end -= offset;
    for (const auto& cp : b.planes) {
        const Plane& p = cp.plane;
        float dist = p.d + ext.x * fabsf(p.n.x) + ext.y * fabsf(p.n.y) + ext.z * fabsf(p.n.z);
        float d1 = Dot(start, p.n) - dist;
        float d2 = Dot(end, p.n) - dist;
        if (d2 > 0) getout = true;
        if (d1 > 0) startout = true;
        if (d1 > 0 && (d2 >= kSurfaceClipEps || d2 >= d1)) return;
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) {
            float f = (d1 - kSurfaceClipEps) / (d1 - d2);
            if (f < 0) f = 0;
            if (f > enterFrac) {
                enterFrac = f;
                clip = &cp;
            }
        } else {
            float f = (d1 + kSurfaceClipEps) / (d1 - d2);
            if (f > 1) f = 1;
            if (f < leaveFrac) leaveFrac = f;
        }
    }
    if (!startout) {
        tr.startSolid = true;
        tr.brush = b.brushIndex;
        if (!getout) {
            tr.allSolid = true;
            tr.fraction = 0;
        }
        return;
    }
    if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < tr.fraction && clip) {
        if (enterFrac < 0) enterFrac = 0;
        tr.fraction = enterFrac;
        tr.plane = clip->plane;
        tr.plane.d += Dot(clip->plane.n, offset);
        tr.brush = b.brushIndex;
        tr.face = clip->face;
        if (tr.face < 0) {
            // bevel plane: pick the real face with the most similar normal
            float best = -2;
            const auto& planes = b.planes;
            for (const auto& q : planes)
                if (q.face >= 0 && Dot(q.plane.n, clip->plane.n) > best) best = Dot(q.plane.n, clip->plane.n), tr.face = q.face;
        }
    }
}

static bool RayBox(Vector3 start, Vector3 invDir, float maxT, Vector3 bmin, Vector3 bmax) {
    float tmin = 0, tmax = maxT;
    for (int a = 0; a < 3; ++a) {
        float s = Comp(start, a), inv = Comp(invDir, a);
        float t1 = (Comp(bmin, a) - s) * inv;
        float t2 = (Comp(bmax, a) - s) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = fmaxf(tmin, t1);
        tmax = fminf(tmax, t2);
        if (tmin > tmax) return false;
    }
    return true;
}

TraceResult CollisionWorld::Trace(Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs) const {
    TraceResult tr;
    Vector3 center = (mins + maxs) * 0.5f;
    Vector3 ext = (maxs - mins) * 0.5f;
    Vector3 s = start + center, e = end + center;
    Vector3 dir = e - s;
    Vector3 inv{dir.x != 0 ? 1.0f / dir.x : 1e30f, dir.y != 0 ? 1.0f / dir.y : 1e30f, dir.z != 0 ? 1.0f / dir.z : 1e30f};
    Vector3 pad = ext + Vector3{1, 1, 1};

    if (!m_nodes.empty()) {
        int stack[128];
        int sp = 0;
        stack[sp++] = 0;
        while (sp > 0) {
            const Node& n = m_nodes[stack[--sp]];
            if (!RayBox(s, inv, tr.fraction, n.bounds.min - pad, n.bounds.max + pad)) continue;
            if (n.left < 0) {
                for (int i = 0; i < n.count; ++i) {
                    const CollisionBrush& b = m_brushes[m_order[n.first + i]];
                    if (!b.solid) continue;
                    TraceBrush(b, {0, 0, 0}, s, e, ext, tr);
                    if (tr.allSolid) break;
                }
                if (tr.allSolid) break;
            } else if (sp < 126) {
                stack[sp++] = n.left;
                stack[sp++] = n.right;
            }
        }
    }
    for (int idx : m_dynamic) {
        const CollisionBrush& b = m_brushes[idx];
        if (!b.solid || (b.mapEntity >= 0 && !m_entitySolid[b.mapEntity])) continue;
        Vector3 off = b.mapEntity >= 0 ? m_entityOffsets[b.mapEntity] : Vector3{0, 0, 0};
        if (!RayBox(s, inv, tr.fraction, b.bounds.min + off - pad, b.bounds.max + off + pad)) continue;
        TraceBrush(b, off, s, e, ext, tr);
    }
    tr.endpos = start + (end - start) * tr.fraction;
    return tr;
}

TraceResult CollisionWorld::TraceEntity(int mapEntity, Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs) const {
    TraceResult tr;
    Vector3 center = (mins + maxs) * 0.5f;
    Vector3 ext = (maxs - mins) * 0.5f;
    Vector3 s = start + center, e = end + center;
    for (int idx : m_dynamic) {
        const CollisionBrush& b = m_brushes[idx];
        if (b.mapEntity != mapEntity) continue;
        TraceBrush(b, EntityOffset(mapEntity), s, e, ext, tr);
    }
    tr.endpos = start + (end - start) * tr.fraction;
    return tr;
}

bool CollisionWorld::PointSolid(Vector3 p) const {
    TraceResult tr = Trace(p, p, {0, 0, 0}, {0, 0, 0});
    return tr.startSolid;
}

bool CollisionWorld::BoxSolid(Vector3 mins, Vector3 maxs) const {
    Vector3 c = (mins + maxs) * 0.5f;
    TraceResult tr = Trace(c, c, mins - c, maxs - c);
    return tr.startSolid;
}

void CollisionWorld::SetEntityOffset(int ent, Vector3 offset) {
    if (ent >= 0 && ent < (int)m_entityOffsets.size()) m_entityOffsets[ent] = offset;
}

Vector3 CollisionWorld::EntityOffset(int ent) const {
    if (ent >= 0 && ent < (int)m_entityOffsets.size()) return m_entityOffsets[ent];
    return {0, 0, 0};
}

void CollisionWorld::SetEntitySolid(int ent, bool solid) {
    if (ent >= 0 && ent < (int)m_entitySolid.size()) m_entitySolid[ent] = solid ? 1 : 0;
}

bool CollisionWorld::TraceAgainstBox(Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs, Vector3 boxMin, Vector3 boxMax, TraceResult& tr) {
    CollisionBrush b;
    b.brushIndex = -1;
    b.mapEntity = -1;
    b.bounds = {boxMin, boxMax};
    b.planes = {{{{1, 0, 0}, boxMax.x}, 0}, {{{-1, 0, 0}, -boxMin.x}, 1}, {{{0, 1, 0}, boxMax.y}, 2},
                {{{0, -1, 0}, -boxMin.y}, 3}, {{{0, 0, 1}, boxMax.z}, 4}, {{{0, 0, -1}, -boxMin.z}, 5}};
    Vector3 center = (mins + maxs) * 0.5f;
    Vector3 ext = (maxs - mins) * 0.5f;
    float before = tr.fraction;
    bool beforeSolid = tr.startSolid;
    CollisionWorld dummy;
    dummy.TraceBrush(b, {0, 0, 0}, start + center, end + center, ext, tr);
    tr.endpos = start + (end - start) * tr.fraction;
    return tr.fraction < before || (tr.startSolid && !beforeSolid);
}

}  // namespace rbs
