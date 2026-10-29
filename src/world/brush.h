// RBS Engine - convex brushes (Quake style: a brush is the intersection of half-spaces).
#pragma once

#include "core/common.h"

namespace rbs {

struct Plane {
    Vector3 n{0, 1, 0};  // outward normal
    float d = 0;         // dot(n, p) == d on the plane
    float Dist(Vector3 p) const { return Dot(n, p) - d; }
};

// Texture mapping of a single face. UVs are world aligned (projected on the dominant
// axis plane), 1 texel == 1 world unit at scale 1.
struct FaceTex {
    std::string name = "concrete";
    float offU = 0, offV = 0;
    float scaleU = 1, scaleV = 1;
    float rot = 0;  // degrees
};

struct BrushFace {
    Plane plane;
    FaceTex tex;
    std::vector<Vector3> poly;  // computed convex polygon (CCW seen from outside)
    // lightmap placement (filled by the lightmap baker)
    int lmX = 0, lmY = 0, lmW = 0, lmH = 0;
    int lmAxis = 0;
    float lmSMin = 0, lmTMin = 0;
    float lmLuxel = 8;  // world units per luxel used for this face
    bool lmValid = false;
};

struct Brush {
    std::vector<BrushFace> faces;
    BoundingBox bounds{};
    int entity = -1;  // owning map entity index (brush entities such as doors), -1 = world
    bool detail = false;  // reserved
    bool BuildPolygons();  // returns false if the brush is degenerate
    Vector3 Center() const { return (bounds.min + bounds.max) * 0.5f; }
    void Translate(Vector3 delta);
    // Applies an arbitrary affine transform to all vertices and rebuilds the planes.
    void Transform(const std::function<Vector3(Vector3)>& fn);
    bool ContainsPoint(Vector3 p, float eps = 0.01f) const;
    bool Intersects(const Brush& other) const;
};

Brush MakeBoxBrush(Vector3 mn, Vector3 mx, const std::string& tex);
// Splits a brush by a plane. front = part on the positive side of the plane.
void SplitBrush(const Brush& b, const Plane& p, const FaceTex& capTex, Brush* front, bool* hasFront, Brush* back, bool* hasBack);
// CSG subtraction: returns the fragments of `a` that lie outside `cutter`.
std::vector<Brush> SubtractBrush(const Brush& a, const Brush& cutter);
// Builds 6 wall brushes forming a hollow room from a solid box brush.
std::vector<Brush> HollowBrush(const Brush& b, float thickness);

// Texture projection helpers shared by renderer and lightmapper.
int DominantAxis(Vector3 n);
void ProjectST(Vector3 p, int axis, float& s, float& t);
Vector2 FaceUV(const BrushFace& f, Vector3 p, int texW, int texH);
Plane PlaneFromPoints(Vector3 a, Vector3 b, Vector3 c);

}  // namespace rbs
