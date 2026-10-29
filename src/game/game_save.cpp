// RBS Engine - save games: building the JSON of the whole game state and putting it back on a
// freshly loaded map. The map and the scripts spawn their entities in the same order every time,
// so an entity of the save is found again by its id; entities that were removed since (picked up,
// gibbed) are removed again, entities that scripts spawned later are spawned again.
#include "core/log.h"
#include "core/vfs.h"
#include "game/game.h"

#include <ctime>
#include <set>

namespace rbs {

static json V3J(Vector3 v) { return json::array({v.x, v.y, v.z}); }

static Vector3 J3(const json& j, Vector3 def = {0, 0, 0}) {
    if (!j.is_array() || j.size() < 3) return def;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// things that live for a moment (gibs, shots) are not saved
static bool SkipEntity(const Entity* e) {
    return e->removed || e->classname == "player" || e->classname == "gib" || e->classname == "fireball" || e->classname == "plasma_bolt" ||
           e->classname == "rocket" || e->lifetime >= 0;
}

bool Game::SaveAllowed() const {
    return m_active && !m_loading && m_player && !m_player->dead && !m_levelEnded && !m_uiFreeze;
}

json Game::BuildSave() {
    m_script.CallHook("_before_save");  // scripts that keep state outside entities write it now
    json s;
    s["format"] = "rbs-save";
    s["version"] = 1;
    s["map"] = m_mapPath;
    s["mod"] = vfs::CurrentMod();
    s["time"] = m_time;
    s["initial_next_id"] = m_initialNextId;
    s["stats"] = {{"kills", m_stats.kills}, {"monsters", m_stats.monsters}, {"secrets", m_stats.secrets}, {"secretsTotal", m_stats.secretsTotal},
                  {"items", m_stats.items}, {"itemsTotal", m_stats.itemsTotal}, {"time", m_stats.time}};
    json objectives = json::array();
    for (auto& o : m_objectives) objectives.push_back({{"id", o.id}, {"text", o.text}, {"done", o.done}});
    s["objectives"] = objectives;
    json persist = json::object();
    for (auto& kv : m_persist) persist[kv.first] = kv.second;
    s["persist"] = persist;

    // the player
    json p;
    p["pos"] = V3J(m_player->pos);
    p["vel"] = V3J(m_player->vel);
    p["yaw"] = m_ps.yaw;
    p["pitch"] = m_ps.pitch;
    p["health"] = m_player->health;
    p["maxHealth"] = m_player->maxHealth;
    p["armor"] = m_player->armor;
    json weapons = json::array();
    for (size_t i = 0; i < m_ps.hasWeapon.size() && i < m_script.weapons.size(); ++i)
        if (m_ps.hasWeapon[i]) weapons.push_back(m_script.weapons[i].id);
    p["weapons"] = weapons;
    if (m_ps.weapon >= 0 && m_ps.weapon < (int)m_script.weapons.size()) p["weapon"] = m_script.weapons[m_ps.weapon].id;
    json ammo = json::object(), keys = json::object();
    for (auto& kv : m_ps.ammo) ammo[kv.first] = kv.second;
    for (auto& kv : m_ps.keys)
        if (kv.second) keys[kv.first] = true;
    p["ammo"] = ammo;
    p["keys"] = keys;
    json pf = m_script.CallEntityJson("save_fields", m_player, nullptr);
    if (pf.is_object()) p["fields"] = pf;
    s["player"] = p;

    // everything else
    json ents = json::array();
    for (auto& up : m_entities) {
        Entity* e = up.get();
        if (SkipEntity(e)) continue;
        json j;
        j["id"] = e->id;
        j["class"] = e->classname;
        j["pos"] = V3J(e->pos);
        j["vel"] = V3J(e->vel);
        j["angles"] = V3J(e->angles);
        j["mins"] = V3J(e->mins);
        j["maxs"] = V3J(e->maxs);
        j["health"] = e->health;
        j["maxHealth"] = e->maxHealth;
        j["armor"] = e->armor;
        j["dead"] = e->dead;
        j["takeDamage"] = e->takeDamage;
        j["solid"] = (int)e->solid;
        j["movetype"] = (int)e->movetype;
        j["visible"] = e->visible;
        j["model"] = e->model;
        j["modelScale"] = e->modelScale;
        j["anim"] = e->anim;
        j["animTime"] = e->animTime;
        j["animLoop"] = e->animLoop;
        j["animDone"] = e->animDone;
        j["tint"] = json::array({e->tint.r, e->tint.g, e->tint.b, e->tint.a});
        j["moveOffset"] = V3J(e->moveOffset);
        j["targetname"] = e->targetname;
        j["target"] = e->target;
        j["nextThink"] = e->nextThink;
        if (e->id >= m_initialNextId) {
            j["props"] = e->props;  // an entity made while playing has to be made again
            j["yaw"] = e->angles.y;
        }
        json f = m_script.CallEntityJson("save_fields", e, nullptr);
        if (f.is_object()) j["fields"] = f;
        ents.push_back(j);
    }
    s["entities"] = ents;

    // what the load menu shows
    s["meta"] = {{"title", m_map.settings.title}, {"stamp", (long long)std::time(nullptr)}, {"time", m_stats.time}, {"health", m_player->health}};
    return s;
}

bool Game::StartLoadSave(const json& save) {
    std::string path = save.value("map", std::string());
    if (path.empty() || !vfs::Exists(path)) {
        LOG_ERR("Save game: map '%s' not found", path.c_str());
        return false;
    }
    if (!LoadMap(path, false)) return false;
    // LoadMap starts a new game: put the quest progress and the like back before the scripts start
    m_persist.clear();
    if (save.contains("persist") && save["persist"].is_object())
        for (auto it = save["persist"].begin(); it != save["persist"].end(); ++it)
            if (it.value().is_string()) m_persist[it.key()] = it.value().get<std::string>();
    m_pendingSave = save;
    return true;
}

static void ApplyEntity(Game* g, Script& script, Entity* e, const json& j) {
    e->pos = J3(j["pos"], e->pos);
    e->vel = J3(j["vel"]);
    e->angles = J3(j["angles"], e->angles);
    if (j.contains("mins")) e->mins = J3(j["mins"], e->mins);
    if (j.contains("maxs")) e->maxs = J3(j["maxs"], e->maxs);
    e->health = j.value("health", e->health);
    e->maxHealth = j.value("maxHealth", e->maxHealth);
    e->armor = j.value("armor", e->armor);
    e->dead = j.value("dead", e->dead);
    e->takeDamage = j.value("takeDamage", e->takeDamage);
    e->solid = (SolidType)j.value("solid", (int)e->solid);
    e->movetype = (MoveType)j.value("movetype", (int)e->movetype);
    e->visible = j.value("visible", e->visible);
    if (j.contains("model") && j["model"].is_string()) e->model = j["model"].get<std::string>();
    e->modelScale = j.value("modelScale", e->modelScale);
    e->anim = j.value("anim", e->anim);
    e->animTime = j.value("animTime", e->animTime);
    e->animLoop = j.value("animLoop", e->animLoop);
    e->animDone = j.value("animDone", e->animDone);
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() == 4)
        e->tint = Color{(unsigned char)j["tint"][0].get<int>(), (unsigned char)j["tint"][1].get<int>(), (unsigned char)j["tint"][2].get<int>(),
                        (unsigned char)j["tint"][3].get<int>()};
    e->targetname = j.value("targetname", e->targetname);
    e->target = j.value("target", e->target);
    e->nextThink = j.value("nextThink", e->nextThink);
    // doors and platforms: the collision follows the offset
    if (j.contains("moveOffset")) g->SetBrushOffset(e, J3(j["moveOffset"]));
    if (j.contains("fields") && j["fields"].is_object()) script.CallEntityJson("load_fields", e, j["fields"]);
}

void Game::ApplyPendingSave() {
    json save = m_pendingSave;
    m_pendingSave = json();
    if (save.value("initial_next_id", m_initialNextId) != m_initialNextId)
        LOG_WARN("Save game: the map or the scripts changed since it was saved (%d entities then, %d now); some things may be off",
                 save.value("initial_next_id", 0), m_initialNextId);
    m_time = save.value("time", 0.0f);
    if (save.contains("stats") && save["stats"].is_object()) {
        const json& st = save["stats"];
        m_stats.kills = st.value("kills", 0);
        m_stats.monsters = st.value("monsters", m_stats.monsters);
        m_stats.secrets = st.value("secrets", 0);
        m_stats.secretsTotal = st.value("secretsTotal", m_stats.secretsTotal);
        m_stats.items = st.value("items", 0);
        m_stats.itemsTotal = st.value("itemsTotal", m_stats.itemsTotal);
        m_stats.time = st.value("time", 0.0f);
    }
    m_objectives.clear();
    if (save.contains("objectives") && save["objectives"].is_array())
        for (auto& o : save["objectives"]) m_objectives.push_back({o.value("id", std::string()), o.value("text", std::string()), o.value("done", false)});

    // entities: the same ones get their state back, the missing ones were removed, new ones are made
    std::set<int> saved;
    std::vector<const json*> later;
    if (save.contains("entities") && save["entities"].is_array()) {
        for (auto& j : save["entities"]) {
            int id = j.value("id", -1);
            saved.insert(id);
            if (id >= m_initialNextId) {
                later.push_back(&j);
                continue;
            }
            Entity* e = Find(id);
            if (!e || e->classname != j.value("class", std::string())) {
                LOG_WARN("Save game: entity %d (%s) is not what it was when saved", id, j.value("class", std::string()).c_str());
                continue;
            }
            ApplyEntity(this, m_script, e, j);
        }
    }
    for (auto& up : m_entities) {
        Entity* e = up.get();
        if (!SkipEntity(e) && e->id < m_initialNextId && !saved.count(e->id)) Remove(e);
    }
    for (const json* j : later) {
        Entity* e = Spawn((*j).value("class", std::string()), J3((*j)["pos"]), (*j).contains("props") ? (*j)["props"] : json::object(), (*j).value("yaw", 0.0f));
        if (e) ApplyEntity(this, m_script, e, *j);
    }

    // the player last: its fields, inventory and view
    const json& p = save["player"];
    m_player->pos = J3(p["pos"], m_player->pos);
    m_player->vel = J3(p["vel"]);
    m_player->health = p.value("health", 100.0f);
    m_player->maxHealth = p.value("maxHealth", m_player->maxHealth);
    m_player->armor = p.value("armor", 0.0f);
    m_ps.yaw = p.value("yaw", 0.0f);
    m_ps.pitch = p.value("pitch", 0.0f);
    m_ps.hasWeapon.assign(m_script.weapons.size(), 0);
    m_ps.ammo.clear();
    m_ps.keys.clear();
    if (p.contains("weapons") && p["weapons"].is_array())
        for (auto& w : p["weapons"])
            if (w.is_string()) GiveWeapon(w.get<std::string>(), false);
    if (p.contains("ammo") && p["ammo"].is_object())
        for (auto it = p["ammo"].begin(); it != p["ammo"].end(); ++it) m_ps.ammo[it.key()] = it.value().get<int>();
    if (p.contains("keys") && p["keys"].is_object())
        for (auto it = p["keys"].begin(); it != p["keys"].end(); ++it) m_ps.keys[it.key()] = true;
    int cur = m_script.WeaponIndex(p.value("weapon", std::string()));
    m_ps.weapon = m_ps.pendingWeapon = cur;
    if (m_ps.weapon < 0) SelectBestWeapon();
    if (m_ps.pendingWeapon >= 0) m_ps.weapon = m_ps.pendingWeapon;
    m_ps.switchAnim = 0;
    if (p.contains("fields") && p["fields"].is_object()) m_script.CallEntityJson("load_fields", m_player, p["fields"]);
    UpdateCamera(0);
    Message("Game loaded", 1.5f);
}

}  // namespace rbs
