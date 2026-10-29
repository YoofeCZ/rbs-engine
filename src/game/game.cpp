#include "game/game.h"
#include "core/cvar.h"
#include "game/animator.h"
#include "core/log.h"
#include "core/vfs.h"
#include "script/plugins.h"

#include <filesystem>

namespace rbs {

static Cvar* g_infinite_ammo = nullptr;
static Cvar* s_volume = nullptr;
static Cvar* s_musicvolume = nullptr;

Game::Game() {
    g_infinite_ammo = cvars::Register("g_infinite_ammo", "0", false, "weapons never run out of ammo");
    cvars::Register("bt_debug", "0", false, "show the behavior tree state of nearby monsters");
    cvars::Register("quest_debug", "0", false, "log quest and dialogue events and the nodes they enter");
    cvars::Register("quest_autodump", "0", false, "tests: seconds after a map started (or a save was loaded) to run `quest dump` once");
    s_volume = cvars::Register("s_volume", "0.8", true, "sound effects volume");
    s_musicvolume = cvars::Register("s_musicvolume", "0.5", true, "music volume");
    m_fx.Init();
}

Game::~Game() { Unload(); }

// ---------------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------------
bool Game::LoadMap(const std::string& relPath, bool keepInventory) {
    Map map;
    std::string err;
    if (!map.Load(relPath, &err)) {
        LOG_ERR("Cannot load map %s: %s", relPath.c_str(), err.c_str());
        return false;
    }
    return LoadMapData(map, std::filesystem::path(relPath).stem().string(), keepInventory, relPath);
}

bool Game::LoadMapData(const Map& map, const std::string& name, bool keepInventory, const std::string& relPath) {
    Carry carry = keepInventory ? MakeCarry() : Carry{};
    if (!keepInventory) m_persist.clear();  // a new game forgets quest progress
    Unload();
    m_mapPath = relPath.empty() ? "maps/" + name + ".json" : relPath;
    m_carry = carry;
    m_map = map;
    m_mapName = name;
    m_world.Build(m_map, [](const Brush& b) {
        for (auto& f : b.faces)
            if (f.tex.name != "trigger") return true;
        return false;
    });
    m_lightmap.Start(m_map, name);
    m_loading = true;
    m_active = false;
    LOG_INFO("Loading map %s (%d brushes, %d entities)", name.c_str(), (int)m_map.brushes.size(), (int)m_map.entities.size());
    return true;
}

void Game::FinishLoading() {
    m_lightmap.Finish();
    m_worldRenderer.Build(m_map, &m_lightmap, false);
    m_fx.Clear();
    m_brushOffsets.assign(m_map.entities.size(), {0, 0, 0});
    m_hiddenBrushEnts.assign(m_map.entities.size(), 0);
    m_mapEntToId.assign(m_map.entities.size(), 0);
    m_time = 0;
    m_stats = GameStats{};
    m_objectives.clear();
    m_uiFreeze = false;
    m_message.clear();
    m_pendingMap.clear();
    m_levelEnded = m_restart = false;
    plugins::SetGame(this);
    animator::ClearCache();  // pick up animators edited since the last map
    if (!m_script.Init(this)) LOG_ERR("Script initialisation failed - the game may not work correctly");
    SpawnMapEntities();
    SpawnPlayer();
    m_script.RunMapScript(m_mapPath);
    m_script.CallHook("on_map_start");
    if (!m_map.settings.music.empty()) {
        m_music = assets::GetMusic(m_map.settings.music);
        if (m_music) {
            m_music->looping = true;
            PlayMusicStream(*m_music);
        }
    }
    m_loading = false;
    m_active = true;
    // everything the map and its scripts made so far is "the map": a save finds its entities by id
    m_initialNextId = m_nextId;
    if (m_pendingSave.is_object()) ApplyPendingSave();
    else m_autosaveRequest = true;  // a map that was just entered: the app writes the autosave
    UpdateCamera(0);
}

void Game::Unload() {
    m_pendingSave = json();
    for (auto& e : m_entities) DestroyEntityScripts(e.get());
    m_entities.clear();
    m_byId.clear();
    m_player = nullptr;
    for (auto& l : m_loops) {
        StopMusicStream(l.music);
        UnloadMusicStream(l.music);
    }
    m_loops.clear();
    if (m_music) StopMusicStream(*m_music);
    m_music = nullptr;
    for (auto& kv : m_soundAliases)
        for (size_t i = 1; i < kv.second.size(); ++i) UnloadSoundAlias(kv.second[i]);
    m_soundAliases.clear();
    m_script.Shutdown();
    plugins::SetGame(nullptr);
    m_worldRenderer.Unload();
    m_lightmap.Unload();
    m_world.Clear();
    m_fx.Clear();
    if (m_pipTarget.id) UnloadRenderTexture(m_pipTarget);
    m_pipTarget = RenderTexture2D{};
    m_pipCamera = -1;
    m_active = false;
    m_loading = false;
    m_nextId = 1;
}

void Game::SpawnMapEntities() {
    for (size_t i = 0; i < m_map.entities.size(); ++i) {
        const MapEntity& me = m_map.entities[i];
        bool brushEnt = false;
        for (auto& b : m_map.brushes)
            if (b.entity == (int)i) { brushEnt = true; break; }
        if (me.classname == "player_start" || me.classname == "info_player_start") continue;
        Entity* e = Spawn(me.classname, me.origin, me.props, me.angle, brushEnt ? (int)i : -1, &me.components);
        if (e && brushEnt) m_mapEntToId[i] = e->id;
    }
}

void Game::SpawnPlayer() {
    Vector3 pos{0, 64, 0};
    float yaw = 0;
    for (auto& me : m_map.entities) {
        if (me.classname == "player_start" || me.classname == "info_player_start") {
            pos = me.origin;
            yaw = me.angle;
            break;
        }
    }
    m_ps = PlayerState{};
    m_ps.hasWeapon.assign(m_script.weapons.size(), 0);
    m_ps.yaw = yaw;
    Entity* p = Spawn("player", pos, json::object(), yaw);
    m_player = p;
    p->mins = {-16, -24, -16};
    p->maxs = {16, 32, 16};
    p->solid = SolidType::BBox;
    p->movetype = MoveType::Walk;
    p->takeDamage = true;
    if (p->health <= 0) p->health = 100;
    p->maxHealth = std::max(p->maxHealth, 100.0f);
    // make sure we don't start inside the floor
    for (int i = 0; i < 32 && m_world.BoxSolid(p->AbsMin(), p->AbsMax()); ++i) p->pos.y += 4;
    if (m_carry.valid) {
        p->health = m_carry.health;
        p->armor = m_carry.armor;
        for (auto& w : m_carry.weapons) GiveWeapon(w, false);
        for (auto& kv : m_carry.ammo) m_ps.ammo[kv.first] = kv.second;
        int idx = m_script.WeaponIndex(m_carry.current);
        if (idx >= 0) m_ps.weapon = m_ps.pendingWeapon = idx;
    } else {
        m_script.CallHookEntity("on_player_spawn", p);
    }
    if (m_ps.weapon < 0) SelectBestWeapon();
    if (m_ps.pendingWeapon >= 0) m_ps.weapon = m_ps.pendingWeapon;
}

Carry Game::MakeCarry() const {
    Carry c;
    if (!m_player || m_player->dead) return c;
    c.valid = true;
    c.health = m_player->health;
    c.armor = m_player->armor;
    for (size_t i = 0; i < m_ps.hasWeapon.size() && i < m_script.weapons.size(); ++i)
        if (m_ps.hasWeapon[i]) c.weapons.push_back(m_script.weapons[i].id);
    if (m_ps.weapon >= 0 && m_ps.weapon < (int)m_script.weapons.size()) c.current = m_script.weapons[m_ps.weapon].id;
    c.ammo = m_ps.ammo;
    return c;
}

void Game::Respawn() {
    m_restart = false;
    Map copy = m_map;
    std::string name = m_mapName, path = m_mapPath;
    // the map content is unchanged, so the lightmap comes straight from the cache
    LoadMapData(copy, name, false, path);
}

// ---------------------------------------------------------------------------------
// entities
// ---------------------------------------------------------------------------------
Entity* Game::Spawn(const std::string& classname, Vector3 pos, const json& props, float yaw, int mapIndex, const json* components) {
    auto ent = std::make_unique<Entity>();
    Entity* e = ent.get();
    e->id = m_nextId++;
    e->classname = classname;
    e->pos = pos;
    e->angles = {0, yaw, 0};
    e->props = props.is_object() ? props : json::object();
    e->mapIndex = mapIndex;
    // class defaults from the editor description (Lua or native)
    int native = plugins::FindClass(classname);
    const ClassInfo* ci = nullptr;
    if (native >= 0) {
        for (auto& c : plugins::ClassInfos())
            if (c.name == classname) ci = &c;
    } else {
        ci = m_script.FindClass(classname);
    }
    if (ci) {
        for (auto it = ci->props.begin(); it != ci->props.end(); ++it)
            if (!e->props.contains(it.key())) e->props[it.key()] = it.value();
        e->mins = ci->mins;
        e->maxs = ci->maxs;
        if (!ci->model.empty()) e->model = ci->model;
    } else if (classname != "player" && classname != "gib" && mapIndex < 0) {
        static std::map<std::string, bool> warned;
        if (!warned[classname]) LOG_WARN("Unknown entity class '%s' (no Lua or native class registered)", classname.c_str());
        warned[classname] = true;
    }
    if (e->props.contains("targetname")) e->targetname = e->props["targetname"].is_string() ? e->props["targetname"].get<std::string>() : "";
    if (e->props.contains("target")) e->target = e->props["target"].is_string() ? e->props["target"].get<std::string>() : "";
    if (mapIndex >= 0) {
        e->solid = SolidType::BBox;  // brush entities are solid unless the script says otherwise
        e->movetype = MoveType::Push;
    }
    m_byId[e->id] = e;
    m_entities.push_back(std::move(ent));

    if (native >= 0) {
        e->nativeClass = native;
        e->nativeData = plugins::Create(native, e, e->props);
    }
    m_script.CreateEntityTable(e);
    if (components && components->is_array()) {
        for (auto& jc : *components) {
            if (!jc.is_object() || !jc.contains("type")) continue;
            Component c;
            c.type = jc["type"].get<std::string>();
            if (jc.contains("props") && jc["props"].is_object()) c.props = jc["props"];
            e->components.push_back(c);
        }
    }
    for (auto& c : e->components) CreateComponent(e, c);
    EvSpawn(e);
    // any entity with a model can get an animator through its properties
    if (!e->animator && !e->removed) {
        auto it = e->props.find("animator");
        if (it != e->props.end() && it->is_string() && !it->get<std::string>().empty()) e->animator = animator::Create(it->get<std::string>());
    }
    if (mapIndex >= 0) SetBrushOffset(e, e->moveOffset);
    return e;
}

void Game::CreateComponent(Entity* e, Component& c) {
    int native = plugins::FindComponent(c.type);
    const ClassInfo* ci = nullptr;
    if (native >= 0) {
        for (auto& info : plugins::ComponentInfos())
            if (info.name == c.type) ci = &info;
    } else {
        ci = m_script.FindComponent(c.type);
    }
    if (!ci) {
        LOG_WARN("Unknown component '%s' on %s", c.type.c_str(), e->classname.c_str());
        return;
    }
    for (auto it = ci->props.begin(); it != ci->props.end(); ++it)
        if (!c.props.contains(it.key())) c.props[it.key()] = it.value();
    if (native >= 0) {
        c.native = native;
        c.nativeData = plugins::Create(native, e, c.props);
    } else {
        m_script.CreateComponent(e, c);
    }
}

bool Game::AddComponent(Entity* e, const std::string& type, const json& props) {
    if (!e || e->removed) return false;
    Component c;
    c.type = type;
    c.props = props.is_object() ? props : json::object();
    e->components.push_back(c);
    Component& added = e->components.back();
    CreateComponent(e, added);
    if (added.native < 0 && added.luaRef < 0) return false;
    added.started = true;
    if (added.native >= 0) plugins::Spawn(added.native, added.nativeData, e);
    else m_script.CompStart(e, added);
    return true;
}

void Game::DestroyEntityScripts(Entity* e) {
    for (auto& c : e->components) {
        if (c.native >= 0) plugins::Destroy(c.native, c.nativeData, e);
        else m_script.ReleaseComponent(c);
        c.native = -1;
        c.nativeData = nullptr;
    }
    if (e->nativeClass >= 0) plugins::Destroy(e->nativeClass, e->nativeData, e);
    e->nativeClass = -1;
    e->nativeData = nullptr;
    m_script.ReleaseEntity(e);
}

void Game::Remove(Entity* e) {
    if (!e || e == m_player) return;
    e->removed = true;
}

Entity* Game::Find(int id) const {
    auto it = m_byId.find(id);
    if (it == m_byId.end() || it->second->removed) return nullptr;
    return it->second;
}

std::vector<Entity*> Game::FindByTargetname(const std::string& name) const {
    std::vector<Entity*> out;
    if (name.empty()) return out;
    for (auto& e : m_entities)
        if (!e->removed && e->targetname == name) out.push_back(e.get());
    return out;
}

std::vector<Entity*> Game::FindByClass(const std::string& cls) const {
    std::vector<Entity*> out;
    for (auto& e : m_entities)
        if (!e->removed && e->classname == cls) out.push_back(e.get());
    return out;
}

std::vector<Entity*> Game::FindInRadius(Vector3 pos, float radius) const {
    std::vector<Entity*> out;
    for (auto& e : m_entities)
        if (!e->removed && Vector3Distance(e->Center(), pos) <= radius) out.push_back(e.get());
    return out;
}

Entity* Game::EntityForBrush(int brush) const {
    if (brush < 0 || brush >= (int)m_map.brushes.size()) return nullptr;
    int me = m_map.brushes[brush].entity;
    if (me < 0 || me >= (int)m_mapEntToId.size()) return nullptr;
    return Find(m_mapEntToId[me]);
}

// ---------------------------------------------------------------------------------
// event dispatch: native class or Lua class, then components
// ---------------------------------------------------------------------------------
void Game::EvSpawn(Entity* e) {
    if (e->nativeClass >= 0) plugins::Spawn(e->nativeClass, e->nativeData, e);
    else m_script.OnSpawn(e);
    for (auto& c : e->components) {
        if (c.started || e->removed) continue;
        c.started = true;
        if (c.native >= 0) plugins::Spawn(c.native, c.nativeData, e);
        else m_script.CompStart(e, c);
    }
}

void Game::EvUpdate(Entity* e, float dt) {
    if (e->nativeClass >= 0) plugins::Update(e->nativeClass, e->nativeData, e, dt);
    else m_script.OnUpdate(e, dt);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) plugins::Update(c.native, c.nativeData, e, dt);
        else m_script.CompUpdate(e, c, dt);
    }
}

void Game::EvTouch(Entity* e, Entity* other, Vector3 n) {
    if (e->nativeClass >= 0) plugins::Touch(e->nativeClass, e->nativeData, e, other, n);
    else m_script.OnTouch(e, other, n);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) plugins::Touch(c.native, c.nativeData, e, other, n);
        else m_script.CompTouch(e, c, other, n);
    }
}

void Game::EvUse(Entity* e, Entity* activator) {
    if (e->nativeClass >= 0) plugins::Use(e->nativeClass, e->nativeData, e, activator);
    else m_script.OnUse(e, activator);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) plugins::Use(c.native, c.nativeData, e, activator);
        else m_script.CompUse(e, c, activator);
    }
}

float Game::EvDamaged(Entity* e, float amount, Entity* attacker, Vector3 dir) {
    if (e->nativeClass >= 0) amount = plugins::Damaged(e->nativeClass, e->nativeData, e, amount, attacker, dir);
    else amount = m_script.OnDamage(e, amount, attacker, dir);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) amount = plugins::Damaged(c.native, c.nativeData, e, amount, attacker, dir);
        else amount = m_script.CompDamaged(e, c, amount, attacker, dir);
    }
    return amount;
}

void Game::EvPain(Entity* e, float amount, Entity* attacker) {
    if (e->nativeClass >= 0) plugins::Pain(e->nativeClass, e->nativeData, e, amount, attacker);
    else m_script.OnPain(e, amount, attacker);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) plugins::Pain(c.native, c.nativeData, e, amount, attacker);
        else m_script.CompPain(e, c, amount, attacker);
    }
}

void Game::EvDie(Entity* e, Entity* attacker, float amount) {
    if (e->nativeClass >= 0) plugins::Die(e->nativeClass, e->nativeData, e, attacker, amount);
    else m_script.OnDie(e, attacker, amount);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) plugins::Die(c.native, c.nativeData, e, attacker, amount);
        else m_script.CompDie(e, c, attacker, amount);
    }
}

void Game::EvAnimEvent(Entity* e, const std::string& name) {
    if (e->nativeClass < 0) m_script.OnAnimEvent(e, name);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native < 0) m_script.CompAnimEvent(e, c, name);
    }
}

void Game::EvBlocked(Entity* e, Entity* other) {
    if (e->nativeClass >= 0) plugins::Blocked(e->nativeClass, e->nativeData, e, other);
    else m_script.OnBlocked(e, other);
    for (size_t i = 0; i < e->components.size() && !e->removed; ++i) {
        Component& c = e->components[i];
        if (c.native >= 0) plugins::Blocked(c.native, c.nativeData, e, other);
        else m_script.CompBlocked(e, c, other);
    }
}

// ---------------------------------------------------------------------------------
// main update
// ---------------------------------------------------------------------------------
void Game::Update(float dt, bool input) {
    if (m_loading) {
        if (m_lightmap.Done()) FinishLoading();
        return;
    }
    if (!m_active) return;
    dt = std::min(dt, 0.05f);
    m_time += dt;
    if (m_player && !m_player->dead) m_stats.time += dt;

    UpdatePlayer(dt, input && !m_uiFreeze);
    UpdateEntities(dt);
    CheckTriggers();
    m_script.CallHookNumber("_update", dt);  // engine scripts (quests); a map script owns on_update
    m_script.CallHookNumber("on_update", dt);
    m_fx.Update(dt, m_world, m_map, m_lightmap, m_map.settings.gravity);
    CleanupRemoved();
    UpdateCamera(dt);
    UpdateSounds();
    if (m_messageTime > 0) m_messageTime -= dt;
    if (m_pipTime > 0) {
        m_pipTime -= dt;
        if (m_pipTime <= 0) m_pipCamera = -1;
    }
    if (m_music) {
        SetMusicVolume(*m_music, s_musicvolume->f);
        UpdateMusicStream(*m_music);
    }
}

void Game::UpdateEntities(float dt) {
    for (size_t i = 0; i < m_entities.size(); ++i) {
        Entity* e = m_entities[i].get();
        if (e->removed || e == m_player) continue;
        e->walked = false;
        EvUpdate(e, dt);
        if (e->removed) continue;
        RunPhysics(e, dt);
        AnimateEntity(e, dt);
        if (e->flash > 0) e->flash = fmaxf(0, e->flash - dt * 6);
        if (e->lifetime > 0) {
            e->lifetime -= dt;
            if (e->lifetime <= 0) Remove(e);
        }
    }
    if (m_player) {
        AnimateEntity(m_player, dt);
        if (m_player->flash > 0) m_player->flash = fmaxf(0, m_player->flash - dt * 6);
        // components on the player (and an optional Lua "player" class) still get updates
        EvUpdate(m_player, dt);
    }
}

void Game::CleanupRemoved() {
    bool any = false;
    for (auto& e : m_entities)
        if (e->removed) { any = true; break; }
    if (!any) return;
    for (auto& e : m_entities) {
        if (!e->removed) continue;
        DestroyEntityScripts(e.get());
        m_byId.erase(e->id);
        if (e->mapIndex >= 0 && e->mapIndex < (int)m_hiddenBrushEnts.size()) {
            m_hiddenBrushEnts[e->mapIndex] = 1;
            m_world.SetEntitySolid(e->mapIndex, false);
        }
    }
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(), [](const std::unique_ptr<Entity>& e) { return e->removed; }),
                     m_entities.end());
}

void Game::AnimateEntity(Entity* e, float dt) {
    if (e->animator) {
        if (e->model.empty()) return;
        std::shared_ptr<AnimatorInstance> keep = e->animator;  // event handlers may replace it
        AnimatorInstance& a = *keep;
        a.params["speed"] = sqrtf(e->vel.x * e->vel.x + e->vel.z * e->vel.z);
        a.params["vertical_speed"] = e->vel.y;
        a.params["on_ground"] = e->onGround ? 1.0f : 0.0f;
        a.params["dead"] = e->dead ? 1.0f : 0.0f;
        a.params["health"] = e->health;
        std::vector<const AnimEventDef*> fired;
        animator::Update(a, assets::GetModel(e->model), dt, fired);
        // mirror the state for scripts that read self.anim / self.anim_done
        if (const AnimStateDef* s = animator::CurrentState(a)) {
            e->anim = s->blend.empty() ? s->clip : s->blend[0].clip;
            e->animDone = !s->loop && a.norm >= 1.0f;
        }
        for (const AnimEventDef* ev : fired) {
            if (!ev->sound.empty()) PlaySound(ev->sound, e->Center(), ev->volume);
            if (!ev->name.empty() && !e->removed) EvAnimEvent(e, ev->name);
        }
        return;
    }
    if (e->model.empty() || e->anim.empty()) return;
    ModelAsset& m = assets::GetModel(e->model);
    int ai = m.FindAnim(e->anim);
    if (ai < 0) return;
    float len = (float)m.anims[ai].frameCount / 60.0f;
    e->animTime += dt * e->animSpeed;
    if (e->animTime >= len) {
        if (e->animLoop) e->animTime = len > 0 ? fmodf(e->animTime, len) : 0;
        else {
            e->animTime = len;
            e->animDone = true;
        }
    }
}

// ---------------------------------------------------------------------------------
// physics
// ---------------------------------------------------------------------------------
static Vector3 ClipVelocity(Vector3 in, Vector3 n, float overbounce) {
    float backoff = Dot(in, n) * overbounce;
    Vector3 out = in - n * backoff;
    if (fabsf(out.x) < 0.01f) out.x = 0;
    if (fabsf(out.y) < 0.01f) out.y = 0;
    if (fabsf(out.z) < 0.01f) out.z = 0;
    return out;
}

bool Game::GroundTrace(Entity* e, Vector3* normal, int* groundEnt) {
    TraceResult tr = Trace(e->pos, e->pos - Vector3{0, 0.25f, 0}, e->mins, e->maxs, e, true);
    if (tr.fraction >= 1.0f || tr.allSolid) return false;
    if (tr.plane.n.y < 0.7f) return false;
    if (normal) *normal = tr.plane.n;
    if (groundEnt) *groundEnt = tr.entity;
    return true;
}

int Game::SlideMove(Entity* e, float dt) {
    constexpr int kMaxBumps = 4, kMaxPlanes = 5;
    Vector3 planes[kMaxPlanes];
    int numPlanes = 0;
    Vector3 primal = e->vel, original = e->vel;
    float timeLeft = dt;
    int blocked = 0;
    for (int bump = 0; bump < kMaxBumps; ++bump) {
        if (Dot(e->vel, e->vel) < 1e-6f) break;
        Vector3 end = e->pos + e->vel * timeLeft;
        TraceResult tr = Trace(e->pos, end, e->mins, e->maxs, e, true);
        if (tr.allSolid) {
            e->vel = {0, 0, 0};
            return 3 | 4;
        }
        if (tr.fraction > 0) {
            e->pos = tr.endpos;
            original = e->vel;
            numPlanes = 0;
        }
        if (tr.fraction >= 1.0f) break;
        blocked |= 4;
        if (tr.plane.n.y > 0.7f) blocked |= 1;
        if (fabsf(tr.plane.n.y) < 0.05f) blocked |= 2;
        if (Entity* hit = Find(tr.entity)) {
            if (e->movetype == MoveType::Fly || e->movetype == MoveType::Bounce || e->movetype == MoveType::Toss) EvTouch(e, hit, tr.plane.n);
        }
        timeLeft -= timeLeft * tr.fraction;
        if (numPlanes >= kMaxPlanes) {
            e->vel = {0, 0, 0};
            return blocked | 3;
        }
        planes[numPlanes++] = tr.plane.n;
        int i;
        Vector3 nv{0, 0, 0};
        for (i = 0; i < numPlanes; ++i) {
            nv = ClipVelocity(original, planes[i], 1.0f);
            int j;
            for (j = 0; j < numPlanes; ++j)
                if (j != i && Dot(nv, planes[j]) < 0) break;
            if (j == numPlanes) break;
        }
        if (i != numPlanes) {
            e->vel = nv;
        } else {
            if (numPlanes != 2) {
                e->vel = {0, 0, 0};
                return blocked | 3;
            }
            Vector3 dir = Cross(planes[0], planes[1]);
            e->vel = dir * Dot(dir, e->vel);
        }
        if (Dot(e->vel, primal) <= 0) {
            e->vel = {0, 0, 0};
            return blocked;
        }
    }
    return blocked;
}

void Game::StepSlideMove(Entity* e, float dt, float stepHeight) {
    Vector3 startPos = e->pos, startVel = e->vel;
    int blocked = SlideMove(e, dt);
    e->blocked = (blocked & 2) != 0;
    if (!(blocked & 4)) return;
    TraceResult tr = Trace(startPos, startPos - Vector3{0, stepHeight, 0}, e->mins, e->maxs, e, true);
    if (e->vel.y > 0 && (tr.fraction >= 1.0f || tr.plane.n.y < 0.7f)) return;  // never step up while going up
    Vector3 downPos = e->pos, downVel = e->vel;
    tr = Trace(startPos, startPos + Vector3{0, stepHeight, 0}, e->mins, e->maxs, e, true);
    if (tr.allSolid) return;
    float stepSize = tr.endpos.y - startPos.y;
    e->pos = tr.endpos;
    e->vel = startVel;
    SlideMove(e, dt);
    tr = Trace(e->pos, e->pos - Vector3{0, stepSize, 0}, e->mins, e->maxs, e, true);
    if (!tr.allSolid) e->pos = tr.endpos;
    if (tr.fraction < 1.0f) e->vel = ClipVelocity(e->vel, tr.plane.n, 1.001f);
    // only keep the step if it got us further horizontally
    float dDown = (downPos.x - startPos.x) * (downPos.x - startPos.x) + (downPos.z - startPos.z) * (downPos.z - startPos.z);
    float dUp = (e->pos.x - startPos.x) * (e->pos.x - startPos.x) + (e->pos.z - startPos.z) * (e->pos.z - startPos.z);
    if (dDown > dUp + 0.01f) {
        e->pos = downPos;
        e->vel = downVel;
        return;
    }
    float stepped = e->pos.y - startPos.y;
    if (e == m_player && stepped > 2.0f) m_ps.stepSmooth -= stepped;
    e->blocked = false;
}

void Game::RunPhysics(Entity* e, float dt) {
    float g = m_map.settings.gravity * e->gravityScale;
    switch (e->movetype) {
        case MoveType::None:
        case MoveType::Push: break;
        case MoveType::Noclip: e->pos += e->vel * dt; break;
        case MoveType::Walk: {
            Vector3 n{0, 1, 0};
            int ge = -1;
            e->onGround = GroundTrace(e, &n, &ge) && e->vel.y <= 1.0f;
            e->groundEntity = e->onGround ? ge : -1;
            if (e->onGround) {
                if (!e->walked) {
                    float f = fmaxf(0.0f, 1.0f - e->friction * dt);
                    e->vel.x *= f;
                    e->vel.z *= f;
                }
                if (e->vel.y < 0) e->vel.y = 0;
            } else {
                e->vel.y -= g * dt;
            }
            if (Dot(e->vel, e->vel) > 0.0001f) StepSlideMove(e, dt, 18.0f);
            break;
        }
        case MoveType::Fly: {
            Vector3 end = e->pos + e->vel * dt;
            TraceResult tr = Trace(e->pos, end, e->mins, e->maxs, e, true);
            e->pos = tr.endpos;
            if (tr.Hit() && !tr.startSolid) {
                Entity* hit = Find(tr.entity);
                bool sky = tr.brush >= 0 && tr.face >= 0 && m_map.brushes[tr.brush].faces[tr.face].tex.name == "sky";
                if (sky) {
                    Remove(e);  // projectiles vanish into the sky
                    break;
                }
                EvTouch(e, hit, tr.plane.n);
                if (!e->removed && e->movetype == MoveType::Fly) e->vel = {0, 0, 0};
            }
            break;
        }
        case MoveType::Toss:
        case MoveType::Bounce: {
            Vector3 n{0, 1, 0};
            e->onGround = GroundTrace(e, &n, nullptr) && e->vel.y <= 1.0f;
            if (!e->onGround) e->vel.y -= g * dt;
            Vector3 end = e->pos + e->vel * dt;
            TraceResult tr = Trace(e->pos, end, e->mins, e->maxs, e, true);
            e->pos = tr.endpos;
            if (tr.Hit() && !tr.startSolid) {
                float speed = Len(e->vel);
                EvTouch(e, Find(tr.entity), tr.plane.n);
                if (e->removed) break;
                float overbounce = e->movetype == MoveType::Bounce ? 1.0f + e->bounce : 1.0f;
                e->vel = ClipVelocity(e->vel, tr.plane.n, overbounce);
                if (tr.plane.n.y > 0.7f) {
                    e->vel.x *= 0.7f;
                    e->vel.z *= 0.7f;
                    if (speed < 60 || e->vel.y < 30) {
                        e->onGround = true;
                        e->vel = {0, 0, 0};
                    }
                }
            }
            if (e->onGround) {
                e->vel.x *= fmaxf(0, 1 - 4 * dt);
                e->vel.z *= fmaxf(0, 1 - 4 * dt);
            }
            break;
        }
    }
    if (e->spin != 0) e->angles.y = fmodf(e->angles.y + e->spin * dt, 360.0f);
}

void Game::Walk(Entity* e, Vector3 dir, float speed) {
    dir.y = 0;
    dir = Norm(dir);
    e->vel.x = dir.x * speed;
    e->vel.z = dir.z * speed;
    e->walked = true;
}

void Game::AimAt(Entity* e, Vector3 target, float turnSpeed, float dt) {
    Vector3 d = target - e->pos;
    if (d.x * d.x + d.z * d.z < 0.01f) return;
    float want = YawFromDir(d);
    float diff = fmodf(want - e->angles.y + 540.0f, 360.0f) - 180.0f;
    float step = turnSpeed * dt;
    if (fabsf(diff) <= step) e->angles.y = want;
    else e->angles.y += diff > 0 ? step : -step;
    float flat = sqrtf(d.x * d.x + d.z * d.z);
    e->angles.x = atan2f(d.y, flat) * RAD2DEG;
}

void Game::CheckTriggers() {
    for (size_t i = 0; i < m_entities.size(); ++i) {
        Entity* t = m_entities[i].get();
        if (t->removed || t->solid != SolidType::Trigger) continue;
        Vector3 mn, mx;
        if (t->mapIndex >= 0) {
            mn = {1e30f, 1e30f, 1e30f};
            mx = {-1e30f, -1e30f, -1e30f};
            for (auto& b : m_map.brushes)
                if (b.entity == t->mapIndex) mn = VMin(mn, b.bounds.min), mx = VMax(mx, b.bounds.max);
            mn += t->moveOffset;
            mx += t->moveOffset;
        } else {
            mn = t->AbsMin();
            mx = t->AbsMax();
        }
        for (size_t j = 0; j < m_entities.size() && !t->removed; ++j) {
            Entity* o = m_entities[j].get();
            if (o == t || o->removed || o->solid != SolidType::BBox || o->mapIndex >= 0) continue;
            if (!(o == m_player || o->monster)) continue;
            Vector3 a = o->AbsMin(), b = o->AbsMax();
            if (a.x > mx.x || b.x < mn.x || a.y > mx.y || b.y < mn.y || a.z > mx.z || b.z < mn.z) continue;
            EvTouch(t, o, {0, 1, 0});
        }
    }
}

bool Game::SetBrushOffset(Entity* e, Vector3 offset) {
    if (!e) return false;
    if (e->mapIndex < 0) {
        e->moveOffset = offset;
        return true;
    }
    int mi = e->mapIndex;
    bool solid = e->solid == SolidType::BBox;
    Vector3 old = e->moveOffset;
    Vector3 delta = offset - old;
    m_world.SetEntitySolid(mi, solid);
    if (mi < (int)m_hiddenBrushEnts.size()) m_hiddenBrushEnts[mi] = !e->visible;
    if (Dot(delta, delta) < 1e-8f) {
        if (mi < (int)m_brushOffsets.size()) m_brushOffsets[mi] = offset;
        return true;
    }
    // riders: entities standing on this brush entity move with it
    std::vector<Entity*> riders;
    for (auto& o : m_entities)
        if (!o->removed && o->groundEntity == e->id && o->mapIndex < 0) riders.push_back(o.get());
    m_world.SetEntityOffset(mi, offset);
    if (solid) {
        m_world.SetEntitySolid(mi, false);
        for (Entity* r : riders) {
            TraceResult tr = Trace(r->pos, r->pos + delta, r->mins, r->maxs, r, true);
            if (!tr.allSolid) r->pos = tr.endpos;
        }
        m_world.SetEntitySolid(mi, true);
        for (auto& o : m_entities) {
            Entity* x = o.get();
            if (x->removed || x->solid != SolidType::BBox || x->mapIndex >= 0) continue;
            TraceResult tr = m_world.TraceEntity(mi, x->pos, x->pos, x->mins, x->maxs);
            if (tr.startSolid) {
                // try pushing the entity along the move direction
                TraceResult pt = Trace(x->pos, x->pos + delta, x->mins, x->maxs, x, true);
                TraceResult again = m_world.TraceEntity(mi, pt.endpos, pt.endpos, x->mins, x->maxs);
                if (!pt.allSolid && !again.startSolid) {
                    x->pos = pt.endpos;
                    continue;
                }
                m_world.SetEntityOffset(mi, old);
                EvBlocked(e, x);
                return false;
            }
        }
    }
    e->moveOffset = offset;
    if (mi < (int)m_brushOffsets.size()) m_brushOffsets[mi] = offset;
    return true;
}

// ---------------------------------------------------------------------------------
// game flow
// ---------------------------------------------------------------------------------
void Game::FireTargets(const std::string& target, Entity* activator) {
    if (target.empty()) return;
    for (Entity* e : FindByTargetname(target)) EvUse(e, activator);
    m_script.FireTargetHandlers(target, activator);
}

void Game::SetObjective(const std::string& id, const std::string& text, bool done) {
    for (auto& o : m_objectives) {
        if (o.id == id) {
            if (!text.empty()) o.text = text;
            if (done && !o.done) PlaySound("sounds/objective.wav", {0, 0, 0}, 0.8f, 1.0f, false);
            o.done = done;
            return;
        }
    }
    m_objectives.push_back({id, text, done});
}

void Game::Message(const std::string& text, float seconds) {
    m_message = text;
    m_messageTime = seconds;
}

void Game::ChangeMap(const std::string& map) {
    if (map.empty()) {
        m_levelEnded = true;
        m_pendingMap = m_map.settings.nextMap;
        if (m_pendingMap.empty()) m_pendingMap = "*";  // back to menu after the stats screen
        return;
    }
    m_pendingMap = map;
    m_levelEnded = true;
}

void Game::ShowCamera(const std::string& name, float seconds) {
    for (Entity* e : FindByTargetname(name)) {
        m_pipCamera = e->id;
        m_pipTime = seconds;
        return;
    }
}

void Game::PlayerDied() {
    m_ps.deadTime = 0;
    m_script.CallHookEntity("on_player_death", m_player);
}

void Game::Kill(Entity* e, Entity* attacker, float amount) {
    if (e->dead) return;
    e->dead = true;
    if (e->monster) m_stats.kills++;
    // quests count kills (scripts/quests.lua); before EvDie, which may gib and remove the monster
    if (e->monster) m_script.CallHookEntity("on_kill", e);
    EvDie(e, attacker, amount);
    if (e == m_player) PlayerDied();
}

// ---------------------------------------------------------------------------------
// sound
// ---------------------------------------------------------------------------------
Sound Game::GetSoundAlias(const std::string& path) {
    auto& pool = m_soundAliases[path];
    if (pool.empty()) {
        Sound s = assets::GetSound(path);
        if (s.frameCount == 0) return s;
        pool.push_back(s);
    }
    for (auto& s : pool)
        if (!IsSoundPlaying(s)) return s;
    if (pool.size() < 8) {
        pool.push_back(LoadSoundAlias(pool[0]));
        return pool.back();
    }
    // the pool is full: reuse the oldest alias. pool[0] is the base sound that owns the data and
    // must stay first - Unload frees pool[1..] as aliases, the assets free pool[0]
    StopSound(pool[1]);
    std::rotate(pool.begin() + 1, pool.begin() + 2, pool.end());
    return pool.back();
}

void Game::PlaySound(const std::string& path, Vector3 pos, float volume, float pitch, bool positional) {
    if (!IsAudioDeviceReady() || path.empty()) return;
    Sound s = GetSoundAlias(path);
    if (s.frameCount == 0) return;
    float vol = volume * s_volume->f;
    float pan = 0.5f;
    if (positional) {
        Vector3 d = pos - m_listenerPos;
        float dist = Len(d);
        float att = Clampf(1.0f - (dist - 64.0f) / 1800.0f, 0.0f, 1.0f);
        vol *= att * att;
        if (dist > 1.0f) pan = 0.5f - 0.4f * Dot(d / dist, m_listenerRight);
    }
    if (vol <= 0.01f) return;
    SetSoundVolume(s, vol);
    SetSoundPitch(s, pitch);
    SetSoundPan(s, pan);
    ::PlaySound(s);
}

void Game::UpdateSounds() {
    if (!IsAudioDeviceReady()) return;
    // looping ambient sounds attached to entities
    for (auto& e : m_entities) {
        if (e->removed || e->loopSound.empty()) continue;
        bool found = false;
        for (auto& l : m_loops)
            if (l.entity == e->id && l.path == e->loopSound) found = true;
        if (found) continue;
        std::string path = vfs::Resolve(e->loopSound);
        if (path.empty()) {
            LOG_WARN("Missing loop sound %s", e->loopSound.c_str());
            e->loopSound.clear();
            continue;
        }
        Music m = LoadMusicStream(path.c_str());
        if (m.frameCount == 0) continue;
        m.looping = true;
        SetMusicVolume(m, 0);
        PlayMusicStream(m);
        m_loops.push_back({e->id, e->loopSound, m});
    }
    for (size_t i = 0; i < m_loops.size();) {
        LoopSound& l = m_loops[i];
        Entity* e = Find(l.entity);
        if (!e || e->loopSound != l.path) {
            StopMusicStream(l.music);
            UnloadMusicStream(l.music);
            m_loops.erase(m_loops.begin() + i);
            continue;
        }
        Vector3 d = e->Center() - m_listenerPos;
        float dist = Len(d);
        float att = Clampf(1.0f - dist / fmaxf(1.0f, e->loopRadius), 0.0f, 1.0f);
        SetMusicVolume(l.music, e->loopVolume * att * att * s_volume->f);
        SetMusicPan(l.music, dist > 1 ? 0.5f - 0.4f * Dot(d / dist, m_listenerRight) : 0.5f);
        UpdateMusicStream(l.music);
        ++i;
    }
}

}  // namespace rbs
