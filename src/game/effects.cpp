#include "game/effects.h"
#include "core/cvar.h"
#include "render/lightmap.h"

#include <rlgl.h>

namespace rbs {

static Cvar* fx_maxdecals = nullptr;
static Cvar* fx_maxparticles = nullptr;
static Cvar* fx_gore = nullptr;

void Effects::Init() {
    fx_maxdecals = cvars::Register("fx_maxdecals", "1500", true, "maximum number of decals (blood, bullet holes)");
    fx_maxparticles = cvars::Register("fx_maxparticles", "4000", true, "maximum number of particles");
    fx_gore = cvars::Register("fx_gore", "1", true, "blood amount multiplier (0 = no blood)");
}

void Effects::Clear() {
    m_particles.clear();
    m_decals.clear();
    m_lights.clear();
    m_pendingBlood.clear();
}

int Effects::TexIndex(const std::string& path) {
    for (size_t i = 0; i < m_particleTex.size(); ++i)
        if (m_particleTex[i] == path) return (int)i;
    m_particleTex.push_back(path);
    return (int)m_particleTex.size() - 1;
}

void Effects::Emit(Vector3 pos, Vector3 dir, const ParticleParams& pp) {
    int tex = TexIndex(pp.texture);
    Vector3 d = Norm(dir);
    for (int i = 0; i < pp.count; ++i) {
        if ((int)m_particles.size() >= fx_maxparticles->i) m_particles.erase(m_particles.begin());
        Particle p;
        p.pos = pos;
        Vector3 rd = RandDir();
        Vector3 v = Len(d) > 0 ? Norm(d + rd * pp.spread) : rd;
        p.vel = v * (pp.speed * RandRange(0.4f, 1.0f));
        p.color = pp.color;
        p.size = pp.size + RandRange(-pp.sizeVar, pp.sizeVar);
        p.life = p.maxLife = pp.life * RandRange(0.6f, 1.2f);
        p.gravity = pp.gravity;
        p.drag = pp.drag;
        p.additive = pp.additive;
        p.blood = pp.blood;
        p.collide = pp.collide || pp.blood;
        p.tex = tex;
        p.rot = RandRange(0, 6.28f);
        p.rotSpeed = RandRange(-3, 3);
        p.growth = pp.growth;
        m_particles.push_back(p);
    }
}

void Effects::Blood(Vector3 pos, Vector3 dir, int amount, Vector3 light) {
    float gore = fx_gore ? fx_gore->f : 1.0f;
    if (gore <= 0) return;
    ParticleParams pp;
    pp.count = (int)(amount * gore);
    Vector3 lc = VMin(light, Vector3{1.2f, 1.2f, 1.2f});
    pp.color = ColorFromVec(Mul(Vector3{0.55f, 0.02f, 0.02f}, lc) + Vector3{0.05f, 0, 0});
    pp.speed = 160;
    pp.spread = 0.8f;
    pp.size = 3.0f;
    pp.sizeVar = 1.5f;
    pp.life = 1.2f;
    pp.gravity = 1.0f;
    pp.drag = 0.3f;
    pp.blood = true;
    pp.texture = "sprites/blood_drop.png";
    Emit(pos, dir, pp);
    // fine mist
    ParticleParams mist = pp;
    mist.count = (int)(amount * 0.5f * gore);
    mist.blood = false;
    mist.collide = false;
    mist.speed = 50;
    mist.size = 7;
    mist.sizeVar = 3;
    mist.life = 0.45f;
    mist.gravity = 0.15f;
    mist.growth = 14;
    mist.texture = "sprites/blood_mist.png";
    Emit(pos, dir, mist);
}

void Effects::Spark(Vector3 pos, Vector3 normal, Vector3 light) {
    ParticleParams dust;
    dust.count = 6;
    dust.color = ColorFromVec(Mul(Vector3{0.5f, 0.45f, 0.4f}, VMin(light, Vector3{1, 1, 1})), 0.7f);
    dust.speed = 40;
    dust.spread = 0.6f;
    dust.size = 5;
    dust.sizeVar = 2;
    dust.life = 0.7f;
    dust.gravity = 0.1f;
    dust.drag = 2.0f;
    dust.growth = 10;
    dust.texture = "sprites/smoke.png";
    Emit(pos + normal * 2, normal, dust);
    ParticleParams sp;
    sp.count = 4;
    sp.color = Color{255, 200, 120, 255};
    sp.speed = 220;
    sp.spread = 0.7f;
    sp.size = 1.5f;
    sp.sizeVar = 0.5f;
    sp.life = 0.3f;
    sp.gravity = 1.0f;
    sp.additive = true;
    sp.collide = true;
    sp.texture = "sprites/spark.png";
    Emit(pos + normal, normal, sp);
}

struct DVert {
    Vector3 p;
    Vector2 uv;
};

static std::vector<DVert> ClipDecal(const std::vector<DVert>& in, const Plane& pl) {
    std::vector<DVert> out;
    size_t n = in.size();
    for (size_t i = 0; i < n; ++i) {
        const DVert& a = in[i];
        const DVert& b = in[(i + 1) % n];
        float da = pl.Dist(a.p), db = pl.Dist(b.p);
        if (da <= 0) out.push_back(a);
        if ((da > 0) != (db > 0)) {
            float t = da / (da - db);
            out.push_back({a.p + (b.p - a.p) * t, {a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t}});
        }
    }
    return out;
}

void Effects::AddDecal(const CollisionWorld& world, const Map& map, const Lightmap& lm, Vector3 pos, Vector3 normal, int brush, int face,
                       const std::string& tex, float size, Color tint, float life) {
    if (brush < 0 || brush >= (int)map.brushes.size()) return;
    const Brush& b = map.brushes[brush];
    if (b.entity >= 0) return;  // no decals on moving brushes
    if (face >= 0 && face < (int)b.faces.size()) {
        const std::string& ft = b.faces[face].tex.name;
        if (ft == "sky") return;
        normal = b.faces[face].plane.n;
    }
    Vector3 up = fabsf(normal.y) > 0.7f ? Vector3{RandRange(-1, 1), 0, RandRange(-1, 1)} : Vector3{0, 1, 0};
    if (Len(up) < 0.1f) up = {1, 0, 0};
    Vector3 right = Norm(Cross(normal, up));
    up = Cross(right, normal);
    float ang = RandRange(0, 6.28f);
    Vector3 r2 = right * cosf(ang) + up * sinf(ang);
    up = up * cosf(ang) - right * sinf(ang);
    right = r2;
    float h = size * 0.5f;
    Vector3 c = pos + normal * 0.12f;
    std::vector<DVert> poly = {{c - right * h - up * h, {0, 1}}, {c + right * h - up * h, {1, 1}},
                               {c + right * h + up * h, {1, 0}}, {c - right * h + up * h, {0, 0}}};
    for (size_t i = 0; i < b.faces.size() && poly.size() >= 3; ++i) {
        if ((int)i == face) continue;
        poly = ClipDecal(poly, b.faces[i].plane);
    }
    if (poly.size() < 3) return;
    Decal d;
    for (auto& v : poly) {
        d.pts.push_back(v.p);
        d.uvs.push_back(v.uv);
    }
    Vector3 light = lm.Valid() ? lm.SampleFace(map, brush, face, pos) : Vector3{1, 1, 1};
    light = VMin(light, Vector3{1.4f, 1.4f, 1.4f});
    d.color = ColorFromVec(Mul(VecFromColor(tint), light), tint.a / 255.0f);
    d.tex = tex;
    d.life = life;
    d.fade = 1.0f;
    m_decals.push_back(d);
    int maxd = fx_maxdecals ? fx_maxdecals->i : 1500;
    if ((int)m_decals.size() > maxd) m_decals.erase(m_decals.begin(), m_decals.begin() + (m_decals.size() - maxd));
}

void Effects::AddLight(Vector3 pos, Vector3 color, float radius, float life) { m_lights.push_back({{pos, color, radius}, life, life}); }

void Effects::CollectLights(std::vector<DynamicLight>& out) const {
    for (auto& l : m_lights) {
        DynamicLight d = l.light;
        float f = l.maxLife > 0 ? Clampf(l.life / l.maxLife, 0, 1) : 1;
        d.color = d.color * f;
        out.push_back(d);
    }
}

void Effects::Update(float dt, const CollisionWorld& world, const Map& map, const Lightmap& lm, float gravity) {
    for (auto& l : m_lights) l.life -= dt;
    m_lights.erase(std::remove_if(m_lights.begin(), m_lights.end(), [](const TimedLight& l) { return l.life <= 0; }), m_lights.end());

    for (auto& p : m_particles) {
        p.life -= dt;
        if (p.life <= 0) continue;
        p.vel.y -= gravity * p.gravity * dt;
        p.vel = p.vel * fmaxf(0.0f, 1.0f - p.drag * dt);
        Vector3 np = p.pos + p.vel * dt;
        if (p.collide) {
            TraceResult tr = world.TraceRay(p.pos, np);
            if (tr.Hit() && !tr.startSolid) {
                if (p.blood) {
                    if (GetRandomValue(0, 100) < 45) m_pendingBlood.push_back({tr.endpos, tr.plane.n, tr.brush, tr.face, RandRange(6, 18)});
                    p.life = 0;
                    continue;
                }
                // bounce
                float vn = Dot(p.vel, tr.plane.n);
                p.vel = (p.vel - tr.plane.n * (1.6f * vn)) * 0.5f;
                np = tr.endpos + tr.plane.n * 0.1f;
            }
        }
        p.pos = np;
        p.rot += p.rotSpeed * dt;
        p.size = fmaxf(0.1f, p.size + p.growth * dt);
    }
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) { return p.life <= 0; }), m_particles.end());

    for (auto& pd : m_pendingBlood) {
        char tex[64];
        snprintf(tex, sizeof(tex), "decals/blood%d.png", GetRandomValue(1, 4));
        AddDecal(world, map, lm, pd.pos, pd.normal, pd.brush, pd.face, tex, pd.size, WHITE);
    }
    m_pendingBlood.clear();

    for (auto& d : m_decals) {
        if (d.life < 0) continue;
        d.life -= dt;
        d.fade = Clampf(d.life / 2.0f, 0, 1);
    }
    m_decals.erase(std::remove_if(m_decals.begin(), m_decals.end(), [](const Decal& d) { return d.life < 0 && d.fade <= 0; }), m_decals.end());
}

void Effects::DrawDecals() {
    if (m_decals.empty()) return;
    gRenderer.BeginUnlit(false);
    for (auto& d : m_decals) {
        Texture2D t = assets::GetTexturePath(d.tex);
        Color c = d.color;
        if (d.life >= 0) c.a = (unsigned char)(c.a * d.fade);
        rlCheckRenderBatchLimit((int)d.pts.size() * 3);
        rlSetTexture(t.id);  // see Renderer::Quad for why the texture is set twice
        rlBegin(RL_TRIANGLES);
        rlSetTexture(t.id);
        rlColor4ub(c.r, c.g, c.b, c.a);
        for (size_t i = 1; i + 1 < d.pts.size(); ++i) {
            for (size_t k : {(size_t)0, i, i + 1}) {
                rlTexCoord2f(d.uvs[k].x, d.uvs[k].y);
                rlVertex3f(d.pts[k].x, d.pts[k].y, d.pts[k].z);
            }
        }
        rlEnd();
    }
    rlSetTexture(0);
    gRenderer.EndUnlit();
}

void Effects::Draw(const Camera3D& cam) {
    for (int pass = 0; pass < 2; ++pass) {
        bool additive = pass == 1;
        gRenderer.BeginUnlit(additive);
        for (auto& p : m_particles) {
            if (p.additive != additive) continue;
            Color c = p.color;
            float f = Clampf(p.life / p.maxLife, 0, 1);
            if (additive) {
                c.r = (unsigned char)(c.r * f);
                c.g = (unsigned char)(c.g * f);
                c.b = (unsigned char)(c.b * f);
            } else {
                c.a = (unsigned char)(c.a * fminf(1.0f, f * 3.0f));
            }
            gRenderer.Billboard(cam, assets::GetTexturePath(m_particleTex[p.tex]), p.pos, p.size, c, p.rot);
        }
        gRenderer.EndUnlit();
    }
}

}  // namespace rbs
