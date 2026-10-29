#include "script/plugins.h"
#include "rbs_plugin.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/vfs.h"
#include "game/game.h"
#include "script/dynlib.h"

#include <cstring>
#include <filesystem>

namespace rbs {
namespace plugins {

namespace fs = std::filesystem;

struct Native {
    std::string name;
    rbs_behaviour b{};
    bool component = false;
    int plugin = -1;
};

struct Plugin {
    std::string name, path;
    void* lib = nullptr;
    rbs_plugin_shutdown_fn shutdown = nullptr;
};

static Game* g_game = nullptr;
static std::vector<Native> g_natives;
static std::vector<Plugin> g_plugins;
static std::vector<ClassInfo> g_classInfos, g_compInfos;
static int g_loading = -1;  // plugin currently running rbs_plugin_init
static int g_reloadCounter = 0;

void SetGame(Game* g) { g_game = g; }

static Vector3 V(rbs_vec3 v) { return {v.x, v.y, v.z}; }
static rbs_vec3 R(Vector3 v) { return {v.x, v.y, v.z}; }
static Entity* E(rbs_entity id) { return g_game && id > 0 ? g_game->Find(id) : nullptr; }

static ClassInfo InfoFromJson(const std::string& name, const char* text, bool component) {
    ClassInfo ci;
    ci.name = name;
    ci.category = component ? "component" : "native";
    if (!text || !*text) return ci;
    json j = json::parse(text, nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) {
        LOG_WARN("Plugin class %s: invalid editor_json", name.c_str());
        return ci;
    }
    ci.desc = j.value("desc", std::string());
    ci.category = j.value("category", ci.category);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 3)
        ci.color = Color{(unsigned char)j["color"][0].get<int>(), (unsigned char)j["color"][1].get<int>(), (unsigned char)j["color"][2].get<int>(), 255};
    ci.mins = JsonToVec3(j.value("mins", json()), ci.mins);
    ci.maxs = JsonToVec3(j.value("maxs", json()), ci.maxs);
    ci.model = j.value("model", std::string());
    ci.sprite = j.value("sprite", std::string());
    ci.brush = j.value("brush", false);
    if (j.contains("props") && j["props"].is_object()) ci.props = j["props"];
    return ci;
}

static void RegisterBehaviour(const char* name, const rbs_behaviour* b, bool component) {
    if (!name || !b) return;
    for (auto& n : g_natives) {
        if (n.name == name && n.component == component) {
            n.b = *b;
            n.plugin = g_loading;
            return;
        }
    }
    Native n;
    n.name = name;
    n.b = *b;
    n.component = component;
    n.plugin = g_loading;
    g_natives.push_back(n);
    ClassInfo ci = InfoFromJson(name, b->editor_json, component);
    (component ? g_compInfos : g_classInfos).push_back(ci);
    LOG_INFO("  native %s: %s", component ? "component" : "class", name);
}

// ---- C API implementation ---------------------------------------------------------
static void Api_log(const char* t) { LOG_INFO("[plugin] %s", t ? t : ""); }
static void Api_register_class(const char* n, const rbs_behaviour* b) { RegisterBehaviour(n, b, false); }
static void Api_register_component(const char* n, const rbs_behaviour* b) { RegisterBehaviour(n, b, true); }
static void Api_console(const char* c) {
    if (c) cvars::Execute(c);
}
static void Api_lua(const char* code) {
    if (g_game && code) g_game->Scripts().RunString(code);
}
static float Api_cvar_get(const char* n) { return n ? cvars::GetF(n) : 0.0f; }
static void Api_cvar_set(const char* n, const char* v) {
    if (n && v) cvars::Set(n, v);
}
static float Api_time(void) { return g_game ? g_game->Time() : (float)GetTime(); }
static float Api_random(float a, float b) { return RandRange(a, b); }
static rbs_entity Api_player(void) { return g_game && g_game->Player() ? g_game->Player()->id : 0; }

static rbs_entity Api_spawn(const char* cls, rbs_vec3 pos, const char* props, float yaw) {
    if (!g_game || !cls) return 0;
    json p = json::object();
    if (props && *props) {
        p = json::parse(props, nullptr, false, true);
        if (p.is_discarded() || !p.is_object()) p = json::object();
    }
    Entity* e = g_game->Spawn(cls, V(pos), p, yaw);
    return e ? e->id : 0;
}
static void Api_remove(rbs_entity id) {
    if (Entity* e = E(id)) g_game->Remove(e);
}
static int32_t Api_exists(rbs_entity id) { return E(id) != nullptr; }

static float Api_get_float(rbs_entity id, const char* p) {
    Entity* e = E(id);
    if (!e || !p) return 0;
    std::string k = p;
    if (k == "health") return e->health;
    if (k == "max_health") return e->maxHealth;
    if (k == "armor") return e->armor;
    if (k == "yaw") return e->angles.y;
    if (k == "pitch") return e->angles.x;
    if (k == "scale") return e->modelScale;
    if (k == "anim_speed") return e->animSpeed;
    if (k == "anim_time") return e->animTime;
    if (k == "anim_done") return e->animDone;
    if (k == "anim_loop") return e->animLoop;
    if (k == "sprite_size") return e->spriteSize;
    if (k == "sprite_additive") return e->spriteAdditive;
    if (k == "light_radius") return e->lightRadius;
    if (k == "spin") return e->spin;
    if (k == "bob") return e->bobAmount;
    if (k == "flash") return e->flash;
    if (k == "gravity") return e->gravityScale;
    if (k == "bounce") return e->bounce;
    if (k == "friction") return e->friction;
    if (k == "team") return (float)e->team;
    if (k == "takedamage") return e->takeDamage;
    if (k == "monster") return e->monster;
    if (k == "dead") return e->dead;
    if (k == "visible") return e->visible;
    if (k == "fullbright") return e->fullbright;
    if (k == "on_ground") return e->onGround;
    if (k == "next_think") return e->nextThink;
    if (k == "loop_volume") return e->loopVolume;
    if (k == "loop_radius") return e->loopRadius;
    if (k == "owner") return (float)e->owner;
    return 0;
}

static void Api_set_float(rbs_entity id, const char* p, float v) {
    Entity* e = E(id);
    if (!e || !p) return;
    std::string k = p;
    if (k == "health") e->health = v;
    else if (k == "max_health") e->maxHealth = v;
    else if (k == "armor") e->armor = v;
    else if (k == "yaw") e->angles.y = v;
    else if (k == "pitch") e->angles.x = v;
    else if (k == "scale") e->modelScale = v;
    else if (k == "anim_speed") e->animSpeed = v;
    else if (k == "anim_time") e->animTime = v, e->animDone = false;
    else if (k == "anim_loop") e->animLoop = v != 0;
    else if (k == "sprite_size") e->spriteSize = v;
    else if (k == "sprite_additive") e->spriteAdditive = v != 0;
    else if (k == "light_radius") e->lightRadius = v;
    else if (k == "spin") e->spin = v;
    else if (k == "bob") e->bobAmount = v;
    else if (k == "flash") e->flash = v;
    else if (k == "gravity") e->gravityScale = v;
    else if (k == "bounce") e->bounce = v;
    else if (k == "friction") e->friction = v;
    else if (k == "team") e->team = (int)v;
    else if (k == "takedamage") e->takeDamage = v != 0;
    else if (k == "monster") e->monster = v != 0;
    else if (k == "dead") e->dead = v != 0;
    else if (k == "visible") e->visible = v != 0;
    else if (k == "fullbright") e->fullbright = v != 0;
    else if (k == "next_think") e->nextThink = v;
    else if (k == "loop_volume") e->loopVolume = v;
    else if (k == "loop_radius") e->loopRadius = v;
    else if (k == "owner") e->owner = (int)v;
    else LOG_WARN("[plugin] set_float: unknown property '%s'", p);
}

static rbs_vec3 Api_get_vec(rbs_entity id, const char* p) {
    Entity* e = E(id);
    if (!e || !p) return {0, 0, 0};
    std::string k = p;
    if (k == "pos") return R(e->pos);
    if (k == "vel") return R(e->vel);
    if (k == "angles") return R(e->angles);
    if (k == "mins") return R(e->mins);
    if (k == "maxs") return R(e->maxs);
    if (k == "model_offset") return R(e->modelOffset);
    if (k == "light_color") return R(e->lightColor);
    if (k == "offset") return R(e->moveOffset);
    if (k == "center") return R(e->Center());
    if (k == "forward") return R(AnglesForward(e->angles.x, e->angles.y));
    return {0, 0, 0};
}

static void Api_set_vec(rbs_entity id, const char* p, rbs_vec3 v) {
    Entity* e = E(id);
    if (!e || !p) return;
    std::string k = p;
    if (k == "pos") e->pos = V(v);
    else if (k == "vel") e->vel = V(v);
    else if (k == "angles") e->angles = V(v);
    else if (k == "mins") e->mins = V(v);
    else if (k == "maxs") e->maxs = V(v);
    else if (k == "model_offset") e->modelOffset = V(v);
    else if (k == "light_color") e->lightColor = V(v);
    else if (k == "offset") g_game->SetBrushOffset(e, V(v));
    else LOG_WARN("[plugin] set_vec: unknown property '%s'", p);
}

static int32_t CopyOut(const std::string& s, char* buf, int32_t size) {
    if (buf && size > 0) {
        size_t n = std::min((size_t)size - 1, s.size());
        memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    return (int32_t)s.size();
}

static const char* MoveTypeStr(MoveType m) {
    switch (m) {
        case MoveType::Walk: return "walk";
        case MoveType::Fly: return "fly";
        case MoveType::Toss: return "toss";
        case MoveType::Bounce: return "bounce";
        case MoveType::Push: return "push";
        case MoveType::Noclip: return "noclip";
        default: return "none";
    }
}

static int32_t Api_get_string(rbs_entity id, const char* p, char* buf, int32_t size) {
    Entity* e = E(id);
    if (!e || !p) return CopyOut("", buf, size);
    std::string k = p;
    if (k == "classname") return CopyOut(e->classname, buf, size);
    if (k == "model") return CopyOut(e->model, buf, size);
    if (k == "sprite") return CopyOut(e->sprite, buf, size);
    if (k == "anim") return CopyOut(e->anim, buf, size);
    if (k == "targetname") return CopyOut(e->targetname, buf, size);
    if (k == "target") return CopyOut(e->target, buf, size);
    if (k == "loop_sound") return CopyOut(e->loopSound, buf, size);
    if (k == "movetype") return CopyOut(MoveTypeStr(e->movetype), buf, size);
    if (k == "solid")
        return CopyOut(e->solid == SolidType::BBox ? "bbox" : e->solid == SolidType::Trigger ? "trigger" : e->solid == SolidType::Corpse ? "corpse" : "not", buf, size);
    return CopyOut("", buf, size);
}

static void Api_set_string(rbs_entity id, const char* p, const char* v) {
    Entity* e = E(id);
    if (!e || !p) return;
    std::string k = p, val = v ? v : "";
    if (k == "model") e->model = val;
    else if (k == "sprite") e->sprite = val;
    else if (k == "anim") {
        if (e->anim != val) e->anim = val, e->animTime = 0, e->animDone = false;
    } else if (k == "targetname") e->targetname = val;
    else if (k == "target") e->target = val;
    else if (k == "loop_sound") e->loopSound = val;
    else if (k == "movetype") {
        e->movetype = val == "walk" ? MoveType::Walk : val == "fly" ? MoveType::Fly : val == "toss" ? MoveType::Toss
                    : val == "bounce" ? MoveType::Bounce : val == "push" ? MoveType::Push : val == "noclip" ? MoveType::Noclip : MoveType::None;
    } else if (k == "solid") {
        e->solid = (val == "bbox" || val == "bsp") ? SolidType::BBox
                 : val == "trigger" ? SolidType::Trigger : val == "corpse" ? SolidType::Corpse : SolidType::Not;
        if (e->mapIndex >= 0) g_game->SetBrushOffset(e, e->moveOffset);
    } else LOG_WARN("[plugin] set_string: unknown property '%s'", p);
}

static int32_t Api_get_prop(rbs_entity id, const char* key, char* buf, int32_t size) {
    Entity* e = E(id);
    if (!e || !key || !e->props.contains(key)) {
        CopyOut("", buf, size);
        return -1;
    }
    const json& v = e->props[key];
    return CopyOut(v.is_string() ? v.get<std::string>() : v.dump(), buf, size);
}

static void Api_set_prop(rbs_entity id, const char* key, const char* value) {
    Entity* e = E(id);
    if (!e || !key) return;
    e->props[key] = value ? value : "";
}

static int32_t OutList(const std::vector<Entity*>& list, rbs_entity* out, int32_t max) {
    int32_t n = 0;
    for (Entity* e : list) {
        if (n < max && out) out[n] = e->id;
        n++;
    }
    return n;
}
static int32_t Api_find(const char* n, rbs_entity* out, int32_t max) { return g_game && n ? OutList(g_game->FindByTargetname(n), out, max) : 0; }
static int32_t Api_find_class(const char* n, rbs_entity* out, int32_t max) { return g_game && n ? OutList(g_game->FindByClass(n), out, max) : 0; }
static int32_t Api_find_radius(rbs_vec3 p, float r, rbs_entity* out, int32_t max) { return g_game ? OutList(g_game->FindInRadius(V(p), r), out, max) : 0; }

static void Api_walk(rbs_entity id, rbs_vec3 dir, float speed) {
    if (Entity* e = E(id)) g_game->Walk(e, V(dir), speed);
}
static void Api_face(rbs_entity id, rbs_vec3 t, float speed, float dt) {
    if (Entity* e = E(id)) g_game->AimAt(e, V(t), speed, dt);
}
static int32_t Api_visible(rbs_entity a, rbs_entity b) {
    Entity *ea = E(a), *eb = E(b);
    return ea && eb && g_game->Visible(ea, eb);
}
static rbs_trace_result Api_trace(rbs_vec3 from, rbs_vec3 to, rbs_entity ignore) {
    rbs_trace_result r{};
    r.fraction = 1;
    r.pos = to;
    if (!g_game) return r;
    TraceResult tr = g_game->Trace(V(from), V(to), {0, 0, 0}, {0, 0, 0}, E(ignore), true);
    r.fraction = tr.fraction;
    r.pos = R(tr.endpos);
    r.normal = R(tr.plane.n);
    r.hit = tr.Hit();
    r.startsolid = tr.startSolid;
    r.entity = tr.entity > 0 ? tr.entity : 0;
    const Map& m = g_game->GetMap();
    if (tr.brush >= 0 && tr.brush < (int)m.brushes.size() && tr.face >= 0 && tr.face < (int)m.brushes[tr.brush].faces.size())
        r.sky = m.brushes[tr.brush].faces[tr.face].tex.name == "sky";
    return r;
}
static void Api_damage(rbs_entity t, float amount, rbs_entity attacker, rbs_vec3 dir) {
    if (Entity* e = E(t)) g_game->Damage(e, amount, E(attacker), V(dir), e->Center());
}
static void Api_radius_damage(rbs_vec3 p, float r, float d, rbs_entity attacker) {
    if (g_game) g_game->RadiusDamage(V(p), r, d, E(attacker), nullptr);
}
static void Api_fire_bullets(rbs_entity owner, rbs_vec3 origin, rbs_vec3 dir, int32_t count, float spread, float damage) {
    if (Entity* e = E(owner)) g_game->FireBullets(e, V(origin), Norm(V(dir)), count, spread, damage, 8192);
}
static rbs_entity Api_projectile(const char* cls, rbs_entity owner, rbs_vec3 origin, rbs_vec3 dir, float speed) {
    if (!g_game || !cls) return 0;
    Entity* e = g_game->Projectile(cls, E(owner), V(origin), Norm(V(dir)), speed);
    return e ? e->id : 0;
}
static void Api_explosion(rbs_vec3 p, float r, float d, rbs_entity attacker) {
    if (g_game) g_game->Explosion(V(p), r, d, E(attacker));
}
static void Api_gib(rbs_entity id, int32_t count) {
    if (Entity* e = E(id)) g_game->Gib(e, count);
}
static void Api_blood(rbs_vec3 p, rbs_vec3 d, int32_t amount) {
    if (g_game) g_game->BloodSpray(V(p), V(d), amount);
}
static void Api_sound(const char* path, rbs_vec3 p, float vol, float pitch) {
    if (g_game && path) g_game->PlaySound(path, V(p), vol, pitch, true);
}
static void Api_sound2d(const char* path, float vol, float pitch) {
    if (g_game && path) g_game->PlaySound(path, {0, 0, 0}, vol, pitch, false);
}
static void Api_particles(rbs_vec3 p, rbs_vec3 d, int32_t count, uint32_t rgba, float speed, float size, float life) {
    if (!g_game) return;
    ParticleParams pp;
    pp.count = count;
    pp.color = Color{(unsigned char)(rgba >> 24), (unsigned char)(rgba >> 16), (unsigned char)(rgba >> 8), (unsigned char)rgba};
    pp.speed = speed;
    pp.size = size;
    pp.life = life;
    g_game->Fx().Emit(V(p), V(d), pp);
}
static void Api_light(rbs_vec3 p, rbs_vec3 c, float r, float t) {
    if (g_game) g_game->Fx().AddLight(V(p), V(c), r, t);
}
static void Api_message(const char* t, float s) {
    if (g_game && t) g_game->Message(t, s);
}
static void Api_fire_targets(const char* t, rbs_entity a) {
    if (g_game && t) g_game->FireTargets(t, E(a));
}
static void Api_objective(const char* id, const char* text, int32_t done) {
    if (g_game && id) g_game->SetObjective(id, text ? text : "", done != 0);
}
static void Api_change_map(const char* m) {
    if (g_game) g_game->ChangeMap(m ? m : "");
}
static void Api_give_weapon(const char* id, int32_t select) {
    if (g_game && id) g_game->GiveWeapon(id, select != 0);
}
static void Api_give_ammo(const char* t, int32_t n) {
    if (g_game && t) g_game->GiveAmmo(t, n);
}

static rbs_api MakeApi() {
    rbs_api a{};
    a.version = RBS_PLUGIN_API_VERSION;
    a.size = sizeof(rbs_api);
    a.log = Api_log;
    a.register_class = Api_register_class;
    a.register_component = Api_register_component;
    a.console = Api_console;
    a.lua = Api_lua;
    a.cvar_get = Api_cvar_get;
    a.cvar_set = Api_cvar_set;
    a.time = Api_time;
    a.random = Api_random;
    a.player = Api_player;
    a.spawn = Api_spawn;
    a.remove = Api_remove;
    a.exists = Api_exists;
    a.get_float = Api_get_float;
    a.set_float = Api_set_float;
    a.get_vec = Api_get_vec;
    a.set_vec = Api_set_vec;
    a.get_string = Api_get_string;
    a.set_string = Api_set_string;
    a.get_prop = Api_get_prop;
    a.set_prop = Api_set_prop;
    a.find = Api_find;
    a.find_class = Api_find_class;
    a.find_radius = Api_find_radius;
    a.walk = Api_walk;
    a.face = Api_face;
    a.visible = Api_visible;
    a.trace = Api_trace;
    a.damage = Api_damage;
    a.radius_damage = Api_radius_damage;
    a.fire_bullets = Api_fire_bullets;
    a.projectile = Api_projectile;
    a.explosion = Api_explosion;
    a.gib = Api_gib;
    a.blood = Api_blood;
    a.sound = Api_sound;
    a.sound2d = Api_sound2d;
    a.particles = Api_particles;
    a.light = Api_light;
    a.message = Api_message;
    a.fire_targets = Api_fire_targets;
    a.objective = Api_objective;
    a.change_map = Api_change_map;
    a.give_weapon = Api_give_weapon;
    a.give_ammo = Api_give_ammo;
    return a;
}

static rbs_api g_api;

void LoadAll() {
    UnloadAll();
    g_api = MakeApi();
    std::string ext = dynlib::Extension();
    std::vector<std::string> files = vfs::List("plugins", ext, false);
    if (files.empty()) return;
    std::error_code ec;
    fs::path tmpDir = fs::path(vfs::WriteDir()) / "cache" / "plugins";
    fs::create_directories(tmpDir, ec);
    for (auto& rel : files) {
        std::string path = vfs::Resolve(rel);
        if (path.empty()) continue;
        // load a copy so the original can be rebuilt while the engine runs (hot reload)
        fs::path copy = tmpDir / (fs::path(rel).stem().string() + "_" + std::to_string(++g_reloadCounter) + ext);
        fs::copy_file(path, copy, fs::copy_options::overwrite_existing, ec);
        std::string loadPath = ec ? path : copy.string();
        std::string err;
        void* lib = dynlib::Open(loadPath, &err);
        if (!lib) {
            LOG_ERR("Plugin %s: %s", rel.c_str(), err.c_str());
            continue;
        }
        auto init = (rbs_plugin_init_fn)dynlib::Symbol(lib, "rbs_plugin_init");
        if (!init) {
            LOG_ERR("Plugin %s: missing rbs_plugin_init export", rel.c_str());
            dynlib::Close(lib);
            continue;
        }
        Plugin p;
        p.name = fs::path(rel).stem().string();
        p.path = path;
        p.lib = lib;
        p.shutdown = (rbs_plugin_shutdown_fn)dynlib::Symbol(lib, "rbs_plugin_shutdown");
        g_loading = (int)g_plugins.size();
        g_plugins.push_back(p);
        LOG_INFO("Loading plugin %s", rel.c_str());
        int r = init(&g_api);
        g_loading = -1;
        if (r != 0) LOG_ERR("Plugin %s: rbs_plugin_init returned %d", rel.c_str(), r);
    }
}

void UnloadAll() {
    for (auto& p : g_plugins) {
        if (p.shutdown) p.shutdown();
        dynlib::Close(p.lib);
    }
    g_plugins.clear();
    g_natives.clear();
    g_classInfos.clear();
    g_compInfos.clear();
}

std::vector<std::string> Loaded() {
    std::vector<std::string> out;
    for (auto& p : g_plugins) out.push_back(p.name);
    return out;
}

int FindClass(const std::string& name) {
    for (size_t i = 0; i < g_natives.size(); ++i)
        if (!g_natives[i].component && g_natives[i].name == name) return (int)i;
    return -1;
}

int FindComponent(const std::string& name) {
    for (size_t i = 0; i < g_natives.size(); ++i)
        if (g_natives[i].component && g_natives[i].name == name) return (int)i;
    return -1;
}

const std::vector<ClassInfo>& ClassInfos() { return g_classInfos; }
const std::vector<ClassInfo>& ComponentInfos() { return g_compInfos; }

static const rbs_behaviour* B(int b) { return (b >= 0 && b < (int)g_natives.size()) ? &g_natives[b].b : nullptr; }

void* Create(int b, Entity* e, const json& props) {
    const rbs_behaviour* bh = B(b);
    if (!bh || !bh->create) return nullptr;
    std::string p = props.dump();
    return bh->create(bh->user, e->id, p.c_str());
}
void Destroy(int b, void* data, Entity* e) {
    if (auto* bh = B(b))
        if (bh->destroy) bh->destroy(data, e->id);
}
void Spawn(int b, void* data, Entity* e) {
    if (auto* bh = B(b))
        if (bh->spawn) bh->spawn(data, e->id);
}
void Update(int b, void* data, Entity* e, float dt) {
    if (auto* bh = B(b))
        if (bh->update) bh->update(data, e->id, dt);
}
void Touch(int b, void* data, Entity* e, Entity* other, Vector3 n) {
    if (auto* bh = B(b))
        if (bh->touch) bh->touch(data, e->id, other ? other->id : 0, R(n));
}
void Use(int b, void* data, Entity* e, Entity* a) {
    if (auto* bh = B(b))
        if (bh->use) bh->use(data, e->id, a ? a->id : 0);
}
float Damaged(int b, void* data, Entity* e, float amount, Entity* attacker, Vector3 dir) {
    if (auto* bh = B(b))
        if (bh->damaged) return bh->damaged(data, e->id, amount, attacker ? attacker->id : 0, R(dir));
    return amount;
}
void Pain(int b, void* data, Entity* e, float amount, Entity* attacker) {
    if (auto* bh = B(b))
        if (bh->pain) bh->pain(data, e->id, amount, attacker ? attacker->id : 0);
}
void Die(int b, void* data, Entity* e, Entity* attacker, float amount) {
    if (auto* bh = B(b))
        if (bh->die) bh->die(data, e->id, attacker ? attacker->id : 0, amount);
}
void Blocked(int b, void* data, Entity* e, Entity* other) {
    if (auto* bh = B(b))
        if (bh->blocked) bh->blocked(data, e->id, other ? other->id : 0);
}

}  // namespace plugins
}  // namespace rbs
