// RBS Engine - tracing, damage, weapons fire, gore.
#include "core/cvar.h"
#include "core/log.h"
#include "game/game.h"

#include <map>

namespace rbs {

TraceResult Game::Trace(Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs, const Entity* ignore, bool hitEntities) const {
    TraceResult tr = m_world.Trace(start, end, mins, maxs);
    if (tr.brush >= 0) {
        Entity* be = EntityForBrush(tr.brush);
        tr.entity = be ? be->id : -1;
        if (be && ignore && be == ignore) {
            // a brush entity tracing against itself: ignore its own brushes
            tr = TraceResult{};
            tr.endpos = end;
        }
    }
    if (!hitEntities) return tr;
    int ignoreId = ignore ? ignore->id : -1;
    int ignoreOwner = ignore ? ignore->owner : -1;
    bool ray = mins.x == 0 && mins.y == 0 && mins.z == 0 && maxs.x == 0 && maxs.y == 0 && maxs.z == 0;
    for (auto& up : m_entities) {
        const Entity* e = up.get();
        if (e->removed || e->mapIndex >= 0) continue;
        if (e->solid != SolidType::BBox && !(ray && e->solid == SolidType::Corpse)) continue;
        if (e->id == ignoreId || e->id == ignoreOwner || (ignoreId >= 0 && e->owner == ignoreId)) continue;
        Vector3 bmin = e->AbsMin(), bmax = e->AbsMax();
        // quick reject: segment bounds vs box
        Vector3 smin = VMin(start, end) + mins, smax = VMax(start, end) + maxs;
        if (smax.x < bmin.x || smin.x > bmax.x || smax.y < bmin.y || smin.y > bmax.y || smax.z < bmin.z || smin.z > bmax.z) continue;
        TraceResult et = tr;
        et.startSolid = false;
        if (CollisionWorld::TraceAgainstBox(start, end, mins, maxs, bmin, bmax, et)) {
            if (et.startSolid && !tr.startSolid && et.fraction >= tr.fraction) {
                continue;  // overlapping boxes don't block movement out of each other
            }
            tr = et;
            tr.entity = e->id;
            tr.brush = -1;
            tr.face = -1;
        }
    }
    return tr;
}

bool Game::Visible(const Entity* a, const Entity* b) const {
    Vector3 from = a == m_player ? EyePos() : a->pos + Vector3{0, a->maxs.y * 0.7f, 0};
    Vector3 to = b == m_player ? EyePos() : b->Center();
    TraceResult tr = m_world.TraceRay(from, to);
    return tr.fraction >= 0.999f;
}

Vector3 Game::LightAt(Vector3 p) const {
    Vector3 amb, dc, dir;
    m_lightmap.LightAtPoint(m_map, m_world, p, amb, dc, dir);
    return amb + dc * 0.5f;
}

void Game::UpdateEntityLight(Entity* e) {
    if (m_time < e->lightUpdate) return;
    e->lightUpdate = m_time + 0.12f + RandRange(0, 0.05f);
    m_lightmap.LightAtPoint(m_map, m_world, e->Center(), e->lightAmbient, e->lightDirColor, e->lightDir);
}

bool Game::DecalTrace(Vector3 from, Vector3 to, const std::string& tex, float size, Color tint) {
    TraceResult tr = m_world.TraceRay(from, to);
    if (!tr.Hit() || tr.startSolid || tr.brush < 0) return false;
    m_fx.AddDecal(m_world, m_map, m_lightmap, tr.endpos, tr.plane.n, tr.brush, tr.face, tex, size, tint);
    return true;
}

void Game::Damage(Entity* t, float amount, Entity* attacker, Vector3 dir, Vector3 point) {
    if (!t || t->removed || !t->takeDamage || amount <= 0) return;
    if (t == m_player && (m_ps.god || m_ps.noclip)) return;
    amount = EvDamaged(t, amount, attacker, dir);
    if (amount <= 0 || t->removed) return;
    if (t == m_player) {
        float save = fminf(t->armor, amount * 0.66f);
        t->armor -= save;
        amount -= save;
        m_ps.damageFlash = fminf(1.0f, m_ps.damageFlash + 0.25f + amount / 40.0f);
        m_ps.punch.x += RandRange(-1, 1) * fminf(amount * 0.4f, 8.0f);
        m_ps.punch.z += RandRange(-1, 1) * fminf(amount * 0.3f, 6.0f);
    }
    // knockback
    if (Len(dir) > 0.01f && (t->movetype == MoveType::Walk || t->movetype == MoveType::Bounce || t->movetype == MoveType::Toss)) {
        float kb = fminf(amount, 200.0f) * (t == m_player ? 6.0f : 3.0f);
        Vector3 push = Norm(dir) * kb;
        if (t->monster || t == m_player) push.y = fmaxf(push.y, kb * 0.15f);
        t->vel += push;
    }
    bool wasDead = t->dead;
    t->health -= amount;
    t->flash = 1.0f;
    if (t->health <= 0 && !wasDead) {
        Kill(t, attacker, amount);
    } else if (!wasDead) {
        EvPain(t, amount, attacker);
    }
}

void Game::RadiusDamage(Vector3 pos, float radius, float damage, Entity* attacker, Entity* ignore) {
    for (Entity* e : FindInRadius(pos, radius + 32)) {
        if (e == ignore || !e->takeDamage) continue;
        Vector3 c = e->Center();
        Vector3 closest = VMax(e->AbsMin(), VMin(pos, e->AbsMax()));
        float dist = Vector3Distance(pos, closest);
        if (dist > radius) continue;
        TraceResult tr = m_world.TraceRay(pos, c);
        if (tr.fraction < 0.98f) continue;
        float dmg = damage * (1.0f - dist / radius);
        if (e == attacker) dmg *= 0.5f;  // rocket jumping is allowed but hurts
        Vector3 dir = Norm(c - pos);
        Damage(e, dmg, attacker, dir, c);
    }
}

void Game::BloodSpray(Vector3 pos, Vector3 dir, int amount) {
    Vector3 light = VMax(LightAt(pos), Vector3{0.35f, 0.35f, 0.35f});
    m_fx.Blood(pos, dir, amount, light);
}

void Game::FireBullets(Entity* owner, Vector3 origin, Vector3 dir, int count, float spread, float damage, float range) {
    dir = Norm(dir);
    Vector3 up0 = fabsf(dir.y) > 0.99f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    Vector3 right = Norm(Cross(dir, up0));
    Vector3 up = Cross(right, dir);
    struct Hit {
        float dmg = 0;
        Vector3 point{0, 0, 0};
        int count = 0;
    };
    std::map<int, Hit> hits;
    for (int i = 0; i < count; ++i) {
        // gaussian-ish spread
        float sx = (RandF() + RandF() - 1.0f) * spread;
        float sy = (RandF() + RandF() - 1.0f) * spread;
        Vector3 d = Norm(dir + right * sx + up * sy);
        TraceResult tr = Trace(origin, origin + d * range, {0, 0, 0}, {0, 0, 0}, owner, true);
        if (!tr.Hit() || tr.startSolid) continue;
        Entity* hit = Find(tr.entity);
        if (hit && hit->takeDamage && hit->mapIndex < 0) {
            Hit& h = hits[hit->id];
            h.dmg += damage;
            h.point = tr.endpos;
            h.count++;
            if (hit->monster || hit == m_player) {
                // blood splatter on the wall behind the target
                if (GetRandomValue(0, 100) < 35) {
                    char tex[64];
                    snprintf(tex, sizeof(tex), "decals/blood%d.png", GetRandomValue(1, 4));
                    DecalTrace(tr.endpos, tr.endpos + d * 160.0f, tex, RandRange(24, 56), WHITE);
                }
            }
        } else {
            bool sky = tr.brush >= 0 && tr.face >= 0 && m_map.brushes[tr.brush].faces[tr.face].tex.name == "sky";
            if (sky) continue;
            Vector3 light = tr.brush >= 0 ? m_lightmap.SampleFace(m_map, tr.brush, tr.face, tr.endpos) : Vector3{1, 1, 1};
            m_fx.Spark(tr.endpos, tr.plane.n, light);
            if (tr.brush >= 0) m_fx.AddDecal(m_world, m_map, m_lightmap, tr.endpos, tr.plane.n, tr.brush, tr.face, "decals/bullet.png", RandRange(5, 7), WHITE);
            if (GetRandomValue(0, 100) < 12) PlaySound(StrFormat("sounds/ric%d.wav", GetRandomValue(1, 3)), tr.endpos, 0.5f, RandRange(0.9f, 1.1f));
        }
    }
    for (auto& kv : hits) {
        Entity* e = Find(kv.first);
        if (!e) continue;
        if (e->monster || e == m_player) BloodSpray(kv.second.point, -dir, 6 + kv.second.count * 3);
        Damage(e, kv.second.dmg, owner, dir, kv.second.point);
    }
}

Entity* Game::Projectile(const std::string& cls, Entity* owner, Vector3 origin, Vector3 dir, float speed) {
    json props = json::object();
    Entity* e = Spawn(cls, origin, props, YawFromDir(dir));
    if (!e) return nullptr;
    e->owner = owner ? owner->id : -1;
    e->vel = dir * speed;
    e->angles.x = asinf(Clampf(dir.y, -1, 1)) * RAD2DEG;
    if (e->movetype == MoveType::None) e->movetype = MoveType::Fly;
    return e;
}

void Game::Gib(Entity* e, int count) {
    if (!e) return;
    Vector3 c = e->Center();
    BloodSpray(c, {0, 1, 0}, 40);
    float gore = cvars::GetF("fx_gore", 1);
    if (gore > 0) {
        for (int i = 0; i < count; ++i) {
            json props = json::object();
            props["kind"] = i == 0 ? "head" : (i < 3 ? "chunk" : "small");
            Vector3 p = c + Vector3{RandRange(-8, 8), RandRange(-12, 12), RandRange(-8, 8)};
            Entity* g = Spawn("gib", p, props, RandRange(0, 360));
            if (!g) continue;
            g->vel = e->vel * 0.5f + Vector3{RandRange(-220, 220), RandRange(180, 420), RandRange(-220, 220)};
            if (g->movetype == MoveType::None) g->movetype = MoveType::Bounce;
        }
        // big splats around the death spot
        for (int i = 0; i < 6; ++i) {
            Vector3 d = Norm(Vector3{RandRange(-1, 1), RandRange(-1.0f, 0.2f), RandRange(-1, 1)});
            char tex[64];
            snprintf(tex, sizeof(tex), "decals/blood%d.png", GetRandomValue(1, 4));
            DecalTrace(c, c + d * 200.0f, tex, RandRange(40, 90), WHITE);
        }
    }
    PlaySound("sounds/gib.wav", c, 1.0f, RandRange(0.9f, 1.1f));
    if (e != m_player) {
        e->visible = false;
        e->solid = SolidType::Not;
        e->takeDamage = false;
        Remove(e);
    } else {
        e->visible = false;
    }
}

void Game::Explosion(Vector3 pos, float radius, float damage, Entity* attacker) {
    RadiusDamage(pos, radius, damage, attacker, nullptr);
    ParticleParams fire;
    fire.count = 26;
    fire.color = Color{255, 160, 60, 255};
    fire.speed = 260;
    fire.spread = 1.0f;
    fire.size = 22;
    fire.sizeVar = 8;
    fire.life = 0.5f;
    fire.gravity = -0.15f;
    fire.drag = 3.5f;
    fire.additive = true;
    fire.growth = 40;
    fire.texture = "sprites/fire.png";
    m_fx.Emit(pos, {0, 0, 0}, fire);
    ParticleParams smoke = fire;
    smoke.count = 14;
    smoke.color = Color{40, 36, 34, 200};
    smoke.additive = false;
    smoke.life = 1.6f;
    smoke.speed = 120;
    smoke.texture = "sprites/smoke.png";
    m_fx.Emit(pos, {0, 1, 0}, smoke);
    ParticleParams sparks;
    sparks.count = 20;
    sparks.color = Color{255, 210, 120, 255};
    sparks.speed = 520;
    sparks.size = 2;
    sparks.life = 0.6f;
    sparks.additive = true;
    sparks.collide = true;
    sparks.texture = "sprites/spark.png";
    m_fx.Emit(pos, {0, 0, 0}, sparks);
    m_fx.AddLight(pos, {1.6f, 0.9f, 0.4f}, radius * 3.0f, 0.45f);
    PlaySound("sounds/explode.wav", pos, 1.0f, RandRange(0.9f, 1.05f));
    // scorch mark on the nearest surface
    Vector3 dirs[6] = {{0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    float best = 1e9f;
    TraceResult bestTr;
    for (auto& d : dirs) {
        TraceResult tr = m_world.TraceRay(pos, pos + d * radius);
        if (tr.Hit() && !tr.startSolid && tr.fraction < best) best = tr.fraction, bestTr = tr;
    }
    if (best < 1.0f && bestTr.brush >= 0)
        m_fx.AddDecal(m_world, m_map, m_lightmap, bestTr.endpos, bestTr.plane.n, bestTr.brush, bestTr.face, "decals/scorch.png", radius * 0.9f, WHITE);
    if (m_player && Vector3Distance(m_player->pos, pos) < radius * 3) {
        float s = 1.0f - Vector3Distance(m_player->pos, pos) / (radius * 3);
        m_ps.punch.x += RandRange(-6, 6) * s;
        m_ps.punch.z += RandRange(-6, 6) * s;
    }
}

}  // namespace rbs
