// RBS Engine - Lua scripting layer.
//
// Game content (entity classes, weapons, HUD, game rules, map events) lives in Lua
// scripts under scripts/ so it can be modded without recompiling the engine.
#pragma once

#include "core/common.h"
#include "world/map.h"

struct lua_State;

namespace rbs {

class Game;
struct Entity;
struct Component;

struct WeaponDef {
    std::string id, name, model, ammoType, icon;
    int slot = 1;
    float fireRate = 0.5f;
    int ammoPerShot = 1;
    bool automatic = true;
    Vector3 viewOffset{7, -8, 14};  // right, down, forward (world units, camera space)
    Vector3 viewRot{0, 0, 0};       // extra rotation (degrees)
    float viewScale = 1.0f;
    float recoil = 1.0f;
    Vector3 muzzle{0, 2, 30};       // muzzle position in weapon model space (for flash)
    Vector3 flashColor{1.0f, 0.7f, 0.3f};
    float flashSize = 18.0f;
    int startAmmo = 0;
    std::string definition;  // "weapons/<id>.json" for weapons made without code
    int luaRef = -2;
};

// Entity class description, used by the editor (entity palette + inspector defaults).
struct ClassInfo {
    std::string name, desc, category = "misc";
    Color color{200, 200, 200, 255};
    Vector3 mins{-8, -8, -8}, maxs{8, 8, 8};
    std::string model, sprite;
    bool brush = false;
    json props = json::object();
    std::string base;        // parent class (rbs.class{base = ...})
    std::string definition;  // "entities/<name>.json" for types made without code
    json enums = json::object();  // property -> list of allowed values (editor dropdown)
    json help = json::object();   // property -> tooltip text
};

class Script {
public:
    ~Script();
    bool Init(Game* game);  // game may be null (editor: only class info is needed)
    void Shutdown();
    bool Ok() const { return L != nullptr; }

    // entity callbacks
    void CreateEntityTable(Entity* e);
    void ReleaseEntity(Entity* e);
    void OnSpawn(Entity* e);
    void OnUpdate(Entity* e, float dt);
    void OnTouch(Entity* e, Entity* other, Vector3 normal);
    void OnUse(Entity* e, Entity* activator);
    // called before damage is applied; the script may return a new amount (0 = ignore)
    float OnDamage(Entity* e, float amount, Entity* attacker, Vector3 dir);
    void OnPain(Entity* e, float amount, Entity* attacker);
    void OnDie(Entity* e, Entity* attacker, float amount);
    void OnBlocked(Entity* e, Entity* other);
    bool HasClass(const std::string& name) const;

    // weapons / game hooks
    void FireWeapon(int index, Entity* owner);
    void CallHook(const char* name);
    void CallHookNumber(const char* name, float value);
    void CallHookEntity(const char* name, Entity* e);
    void FireTargetHandlers(const std::string& target, Entity* activator);
    void RunMapScript(const std::string& mapRelPath);
    bool DrawHud(int w, int h);
    void RunString(const std::string& code);

    // components implemented in Lua (rbs.component)
    bool HasComponent(const std::string& name) const;
    void CreateComponent(Entity* e, Component& c);
    void ReleaseComponent(Component& c);
    void CompStart(Entity* e, Component& c);
    void CompUpdate(Entity* e, Component& c, float dt);
    void CompTouch(Entity* e, Component& c, Entity* other, Vector3 normal);
    void CompUse(Entity* e, Component& c, Entity* activator);
    float CompDamaged(Entity* e, Component& c, float amount, Entity* attacker, Vector3 dir);
    void CompPain(Entity* e, Component& c, float amount, Entity* attacker);
    void CompDie(Entity* e, Component& c, Entity* attacker, float amount);
    void CompBlocked(Entity* e, Component& c, Entity* other);

    std::vector<WeaponDef> weapons;
    std::vector<ClassInfo> classes;
    std::vector<ClassInfo> components;
    const ClassInfo* FindClass(const std::string& name) const;
    const ClassInfo* FindComponent(const std::string& name) const;
    int WeaponIndex(const std::string& id) const;
    // Data fields of a weapon (no functions, no id) - the editor copies them into new weapon files.
    json WeaponData(const std::string& id) const;
    // rbs.<field> converted to JSON (null if missing), e.g. "weapon_fields"
    json LuaJson(const char* field) const;
    // true if `cls` is `base` or derives from it
    bool IsA(const std::string& cls, const std::string& base) const;

    lua_State* L = nullptr;
    Game* game = nullptr;

private:
    bool m_hudBroken = false;
    bool CallFunction(int nargs, const char* what);
    bool PushCallback(Entity* e, const char* name);
    bool PushCompCallback(Entity* e, Component& c, const char* name);
};

}  // namespace rbs
