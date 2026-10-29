#include "world/brush.h"

namespace rbs {

namespace {

struct DVec {
    double x, y, z;
};
inline DVec D(Vector3 v) { return {v.x, v.y, v.z}; }
inline Vector3 F(DVec v) { return {(float)v.x, (float)v.y, (float)v.z}; }
inline double DDot(DVec a, DVec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline DVec DCross(DVec a, DVec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline DVec DAdd(DVec a, DVec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline DVec DSub(DVec a, DVec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline DVec DMul(DVec a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline DVec DNorm(DVec a) {
    double l = sqrt(DDot(a, a));
    return l > 1e-12 ? DMul(a, 1.0 / l) : DVec{0, 0, 0};
}

constexpr double kClipEps = 0.001;

std::vector<DVec> BaseWinding(const Plane& p) {
    DVec n = DNorm(D(p.n));
    DVec up = fabs(n.y) < 0.9 ? DVec{0, 1, 0} : DVec{1, 0, 0};
    DVec u = DNorm(DCross(up, n));
    DVec v = DCross(n, u);
    DVec org = DMul(n, p.d);
    const double S = 131072.0;
    // CCW when looking against the normal (u x v == n)
    return {DAdd(org, DAdd(DMul(u, -S), DMul(v, -S))), DAdd(org, DAdd(DMul(u, S), DMul(v, -S))),
            DAdd(org, DAdd(DMul(u, S), DMul(v, S))), DAdd(org, DAdd(DMul(u, -S), DMul(v, S)))};
}

// Keeps the part of the polygon behind the plane (dist <= 0).
std::vector<DVec> ClipBack(const std::vector<DVec>& in, DVec n, double d) {
    std::vector<DVec> out;
    if (in.empty()) return out;
    size_t cnt = in.size();
    std::vector<double> dist(cnt);
    for (size_t i = 0; i < cnt; ++i) dist[i] = DDot(n, in[i]) - d;
    for (size_t i = 0; i < cnt; ++i) {
        const DVec& a = in[i];
        const DVec& b = in[(i + 1) % cnt];
        double da = dist[i], db = dist[(i + 1) % cnt];
        if (da <= kClipEps) out.push_back(a);
        if ((da > kClipEps && db < -kClipEps) || (da < -kClipEps && db > kClipEps)) {
            double t = da / (da - db);
            out.push_back(DAdd(a, DMul(DSub(b, a), t)));
        }
    }
    return out;
}

float SnapCoord(double v) {
    double r = round(v);
    if (fabs(v - r) < 0.01) return (float)r;
    return (float)v;
}

Vector3 NewellNormal(const std::vector<Vector3>& poly) {
    DVec n{0, 0, 0};
    for (size_t i = 0; i < poly.size(); ++i) {
        DVec a = D(poly[i]), b = D(poly[(i + 1) % poly.size()]);
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return F(DNorm(n));
}

}  // namespace

bool Brush::BuildPolygons() {
    bool any = false;
    Vector3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < faces.size(); ++i) {
        auto w = BaseWinding(faces[i].plane);
        for (size_t j = 0; j < faces.size() && !w.empty(); ++j) {
            if (i == j) continue;
            const Plane& p = faces[j].plane;
            // identical plane appearing twice: keep only the first one
            if (Dot(p.n, faces[i].plane.n) > 0.9999f && fabsf(p.d - faces[i].plane.d) < 0.01f) {
                if (j < i) { w.clear(); break; }
                continue;
            }
            w = ClipBack(w, D(p.n), p.d);
        }
        faces[i].poly.clear();
        if (w.size() < 3) continue;
        for (auto& v : w) {
            Vector3 fv{SnapCoord(v.x), SnapCoord(v.y), SnapCoord(v.z)};
            // drop duplicate consecutive points
            if (!faces[i].poly.empty() && Vector3Distance(faces[i].poly.back(), fv) < 0.01f) continue;
            faces[i].poly.push_back(fv);
        }
        if (faces[i].poly.size() >= 2 && Vector3Distance(faces[i].poly.front(), faces[i].poly.back()) < 0.01f) faces[i].poly.pop_back();
        if (faces[i].poly.size() < 3) {
            faces[i].poly.clear();
            continue;
        }
        any = true;
        for (auto& v : faces[i].poly) mn = VMin(mn, v), mx = VMax(mx, v);
    }
    // drop faces without geometry (redundant planes)
    faces.erase(std::remove_if(faces.begin(), faces.end(), [](const BrushFace& f) { return f.poly.empty(); }), faces.end());
    if (!any || faces.size() < 4) {
        bounds = {{0, 0, 0}, {0, 0, 0}};
        return false;
    }
    bounds = {mn, mx};
    Vector3 size = mx - mn;
    return size.x > 0.05f && size.y > 0.05f && size.z > 0.05f;
}

void Brush::Translate(Vector3 delta) {
    for (auto& f : faces) {
        f.plane.d += Dot(f.plane.n, delta);
        for (auto& v : f.poly) v += delta;
    }
    bounds.min += delta;
    bounds.max += delta;
}

void Brush::Transform(const std::function<Vector3(Vector3)>& fn) {
    // centroid of transformed vertices
    Vector3 c{0, 0, 0};
    int count = 0;
    for (auto& f : faces)
        for (auto& v : f.poly) c += fn(v), ++count;
    if (count == 0) return;
    c = c / (float)count;
    std::vector<BrushFace> out;
    for (auto& f : faces) {
        if (f.poly.size() < 3) continue;
        std::vector<Vector3> tp;
        Vector3 pc{0, 0, 0};
        for (auto& v : f.poly) {
            tp.push_back(fn(v));
            pc += tp.back();
        }
        pc = pc / (float)tp.size();
        Vector3 n = NewellNormal(tp);
        if (Len(n) < 0.5f) continue;
        if (Dot(n, pc - c) < 0) n = -n;
        BrushFace nf;
        nf.tex = f.tex;
        nf.plane.n = n;
        nf.plane.d = Dot(n, pc);
        out.push_back(nf);
    }
    faces = out;
    BuildPolygons();
}

bool Brush::ContainsPoint(Vector3 p, float eps) const {
    for (auto& f : faces)
        if (f.plane.Dist(p) > eps) return false;
    return !faces.empty();
}

bool Brush::Intersects(const Brush& other) const {
    if (bounds.max.x <= other.bounds.min.x + 0.01f || bounds.min.x >= other.bounds.max.x - 0.01f) return false;
    if (bounds.max.y <= other.bounds.min.y + 0.01f || bounds.min.y >= other.bounds.max.y - 0.01f) return false;
    if (bounds.max.z <= other.bounds.min.z + 0.01f || bounds.min.z >= other.bounds.max.z - 0.01f) return false;
    Brush both;
    both.faces = faces;
    for (auto& f : other.faces) both.faces.push_back(f);
    return both.BuildPolygons();
}

Brush MakeBoxBrush(Vector3 mn, Vector3 mx, const std::string& tex) {
    Brush b;
    Vector3 ns[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    float ds[6] = {mx.x, -mn.x, mx.y, -mn.y, mx.z, -mn.z};
    for (int i = 0; i < 6; ++i) {
        BrushFace f;
        f.plane.n = ns[i];
        f.plane.d = ds[i];
        f.tex.name = tex;
        b.faces.push_back(f);
    }
    b.BuildPolygons();
    return b;
}

void SplitBrush(const Brush& b, const Plane& p, const FaceTex& capTex, Brush* front, bool* hasFront, Brush* back, bool* hasBack) {
    Brush f = b, k = b;
    BrushFace capF, capB;
    capF.plane = {-p.n, -p.d};
    capF.tex = capTex;
    capB.plane = p;
    capB.tex = capTex;
    f.faces.push_back(capF);
    k.faces.push_back(capB);
    *hasFront = f.BuildPolygons();
    *hasBack = k.BuildPolygons();
    if (*hasFront) *front = f;
    if (*hasBack) *back = k;
}

std::vector<Brush> SubtractBrush(const Brush& a, const Brush& cutter) {
    std::vector<Brush> out;
    if (!a.Intersects(cutter)) {
        out.push_back(a);
        return out;
    }
    Brush remaining = a;
    for (auto& cf : cutter.faces) {
        Brush fr, bk;
        bool hf = false, hb = false;
        SplitBrush(remaining, cf.plane, cf.tex, &fr, &hf, &bk, &hb);
        if (hf) {
            fr.entity = a.entity;
            out.push_back(fr);
        }
        if (!hb) return out;  // nothing left inside the cutter
        remaining = bk;
    }
    // `remaining` lies completely inside the cutter -> discarded
    return out;
}

std::vector<Brush> HollowBrush(const Brush& b, float thickness) {
    Brush inner = b;
    for (auto& f : inner.faces) f.plane.d -= thickness;
    if (!inner.BuildPolygons()) return {b};
    return SubtractBrush(b, inner);
}

int DominantAxis(Vector3 n) {
    float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
    if (ay >= ax && ay >= az) return 1;
    if (ax >= az) return 0;
    return 2;
}

void ProjectST(Vector3 p, int axis, float& s, float& t) {
    if (axis == 0) { s = p.z; t = p.y; }
    else if (axis == 1) { s = p.x; t = p.z; }
    else { s = p.x; t = p.y; }
}

Vector2 FaceUV(const BrushFace& f, Vector3 p, int texW, int texH) {
    int axis = DominantAxis(f.plane.n);
    float s, t;
    if (axis == 1) {
        s = p.x;
        t = p.z;
    } else if (axis == 0) {
        s = f.plane.n.x > 0 ? -p.z : p.z;
        t = -p.y;
    } else {
        s = f.plane.n.z > 0 ? p.x : -p.x;
        t = -p.y;
    }
    float r = f.tex.rot * DEG2RAD;
    float cr = cosf(r), sr = sinf(r);
    float u = (s * cr - t * sr) / (f.tex.scaleU == 0 ? 1 : f.tex.scaleU) + f.tex.offU;
    float v = (s * sr + t * cr) / (f.tex.scaleV == 0 ? 1 : f.tex.scaleV) + f.tex.offV;
    return {u / (float)std::max(1, texW), v / (float)std::max(1, texH)};
}

Plane PlaneFromPoints(Vector3 a, Vector3 b, Vector3 c) {
    Plane p;
    p.n = Norm(Cross(b - a, c - a));
    p.d = Dot(p.n, a);
    return p;
}

}  // namespace rbs
