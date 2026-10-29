// RBS Engine - the game world: entities, player, physics, combat.
#pragma once

#include "game/effects.h"
#include "game/entity.h"
#include "render/lightmap.h"
#include "render/world_renderer.h"
#include "script/script.h"

#include <map>
#include <unordered_map>

namespace rbs {

struct Objective {
    std::string id, text;
    bool done = false;
};

struct PlayerState {
    float pitch = 0, yaw = 0;
    bool noclip = false, god = false;
    float viewHeight = 22.0f;
    float bobTime = 0, bobAmount = 0;
    float stepSmooth = 0;      // camera offset after stepping up stairs
    float landDip = 0;
    Vector3 punch{0, 0, 0};    // view kick (pitch, yaw, roll)
    int weapon = -1, pendingWeapon = -1;
    float nextFire = 0;
    float switchAnim = 0;      // 0 = raised, 1 = lowered
    float kick = 0;            // weapon recoil animation 0..1
    float swayX = 0, swayY = 0;
    std::vector<char> hasWeapon;
    std::map<std::string, int> ammo;
    std::map<std::string, int> maxAmmo;
    float damageFlash = 0, pickupFlash = 0;
    float deadTime = 0;
    float footstep = 0;
    bool wasOnGround = true;
    float fallSpeed = 0;
    float muzzleFlash = 0;
    std::map<std::string, bool> keys;   // keycards etc.
    bool showObjectives = true;
};

struct GameStats {
    int kills = 0, monsters = 0;
    int secrets = 0, secretsTotal = 0;
    int items = 0, itemsTotal = 0;
    float time = 0;
};

// Inventory carried over between maps.
struct Carry {
    bool valid = false;
    float health = 100, armor = 0;
    std::vector<std::string> weapons;
    std::string current;
    std::map<std::string, int> ammo;
};

class Game {
public:
    Game();
    ~Game();

    // Starts loading a map (VFS path "maps/e1m1.json"). Lighting bakes in the background.
    bool LoadMap(const std::string& relPath, bool keepInventory = false);
    bool LoadMapData(const Map& map, const std::string& name, bool keepInventory = false, const std::string& relPath = "");
    void Unload();
    bool Loading() const { return m_loading; }
    float LoadProgress() const { return m_lightmap.Progress(); }
    bool Active() const { return m_active; }
    const std::string& MapName() const { return m_mapName; }
    const std::string& MapPath() const { return m_mapPath; }

    void Update(float dt, bool inputEnabled);
    void Draw(int screenW, int screenH);
    void DrawHud(int screenW, int screenH);

    // ---- API used by scripts and native plugins ---------------------------------------
    Entity* Spawn(const std::string& classname, Vector3 pos, const json& props = json::object(), float yaw = 0,
                  int mapIndex = -1, const json* components = nullptr);
    void Remove(Entity* e);
    Entity* Find(int id) const;
    Entity* Player() const { return m_player; }
    std::vector<Entity*> FindByTargetname(const std::string& name) const;
    std::vector<Entity*> FindByClass(const std::string& cls) const;
    std::vector<Entity*> FindInRadius(Vector3 pos, float radius) const;
    const std::vector<std::unique_ptr<Entity>>& Entities() const { return m_entities; }
    bool AddComponent(Entity* e, const std::string& type, const json& props);

    TraceResult Trace(Vector3 start, Vector3 end, Vector3 mins, Vector3 maxs, const Entity* ignore, bool hitEntities = true) const;
    bool Visible(const Entity* a, const Entity* b) const;
    void Damage(Entity* target, float amount, Entity* attacker, Vector3 dir, Vector3 point);
    void RadiusDamage(Vector3 pos, float radius, float damage, Entity* attacker, Entity* ignore);
    void FireBullets(Entity* owner, Vector3 origin, Vector3 dir, int count, float spread, float damage, float range);
    Entity* Projectile(const std::string& cls, Entity* owner, Vector3 origin, Vector3 dir, float speed);
    void Gib(Entity* e, int count);
    void BloodSpray(Vector3 pos, Vector3 dir, int amount);
    void Explosion(Vector3 pos, float radius, float damage, Entity* attacker);
    void PlaySound(const std::string& path, Vector3 pos, float volume = 1.0f, float pitch = 1.0f, bool positional = true);
    void FireTargets(const std::string& target, Entity* activator);
    void SetObjective(const std::string& id, const std::string& text, bool done);
    void Message(const std::string& text, float seconds);
    void ChangeMap(const std::string& map);
    void ShowCamera(const std::string& cameraTargetname, float seconds);
    bool SetBrushOffset(Entity* e, Vector3 offset);
    void GiveWeapon(const std::string& id, bool select);
    void GiveAmmo(const std::string& type, int amount);
    int Ammo(const std::string& type) const;
    void Walk(Entity* e, Vector3 dir, float speed);
    void AimAt(Entity* e, Vector3 target, float turnSpeed, float dt);
    Vector3 EyePos() const;
    Vector3 AimDir() const;
    void Kill(Entity* e, Entity* attacker, float amount);
    Vector3 LightAt(Vector3 p) const;
    bool DecalTrace(Vector3 from, Vector3 to, const std::string& tex, float size, Color tint);

    Effects& Fx() { return m_fx; }
    PlayerState& PS() { return m_ps; }
    GameStats& Stats() { return m_stats; }
    const std::vector<Objective>& Objectives() const { return m_objectives; }
    // values scripts keep across maps (quest progress): cleared when a new game starts
    std::map<std::string, std::string>& Persist() { return m_persist; }
    // while a script shows a dialogue the player neither moves nor shoots and the cursor is free
    bool UiFrozen() const { return m_uiFreeze; }
    // A script camera (dialogue shots): the view moves smoothly (rate = how fast, 0 = at once) to
    // this position / look-at point / vertical fov, and back to the player when cleared.
    void SetScriptCamera(Vector3 pos, Vector3 look, float fov, float rate);
    void ClearScriptCamera(float rate);
    bool ScriptCameraActive() const { return m_scriptCam.active; }
    void SetUiFreeze(bool f) { m_uiFreeze = f; }
    const Map& GetMap() const { return m_map; }
    Script& Scripts() { return m_script; }
    float Time() const { return m_time; }
    Camera3D& Camera() { return m_camera; }
    const std::string& PendingMap() const { return m_pendingMap; }
    bool LevelEnded() const { return m_levelEnded; }
    bool RestartRequested() const { return m_restart; }
    Carry MakeCarry() const;

    // ---- save games (game_save.cpp) ----
    bool SaveAllowed() const;                  // alive, in a running map, no dialogue open
    json BuildSave();                          // the whole state of the game as JSON
    bool StartLoadSave(const json& save);      // loads the save's map; the state is put on it when the map is ready
    // scripts ask for a save / load (rbs.save_game / rbs.load_game); the app does it after the frame
    void RequestSave(const std::string& slot) { m_saveRequest = slot; }
    void RequestLoad(const std::string& slot) { m_loadRequest = slot; }
    std::string TakeSaveRequest() { std::string r; r.swap(m_saveRequest); return r; }
    std::string TakeLoadRequest() { std::string r; r.swap(m_loadRequest); return r; }
    bool TakeAutosaveRequest() {               // true once after a new map started (the app writes the autosave)
        bool r = m_autosaveRequest;
        m_autosaveRequest = false;
        return r;
    }
    void Respawn();
    std::string DebugText() const;

private:
    // world
    Map m_map;
    std::string m_mapName, m_mapPath;
    CollisionWorld m_world;
    Lightmap m_lightmap;
    WorldRenderer m_worldRenderer;
    Effects m_fx;
    Script m_script;
    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<int, Entity*> m_byId;
    std::vector<int> m_mapEntToId;  // map entity index -> game entity id
    int m_nextId = 1;
    Entity* m_player = nullptr;
    PlayerState m_ps;
    GameStats m_stats;
    Carry m_carry;
    std::vector<Objective> m_objectives;
    std::map<std::string, std::string> m_persist;
    int m_initialNextId = 1;      // entity ids below this were made by the map and the scripts' spawn()
    json m_pendingSave;           // a save waiting for its map to finish loading
    bool m_autosaveRequest = false;
    std::string m_saveRequest, m_loadRequest;
    void ApplyPendingSave();
    struct ScriptCamera {
        bool active = false, releasing = false, init = false;
        Vector3 pos{}, look{}, tpos{}, tlook{};
        float fov = 60, tfov = 60, rate = 6;
        // while releasing: where it started, and how far along the blend back to the player is
        Vector3 relPos{}, relLook{};
        float relFov = 60, relT = 0, relDur = 0.6f;
    } m_scriptCam;
    bool m_uiFreeze = false;
    std::string m_message;
    float m_messageTime = 0;
    std::string m_pendingMap;
    bool m_levelEnded = false;
    bool m_restart = false;
    float m_time = 0;
    bool m_loading = false;
    bool m_active = false;
    Camera3D m_camera{};
    float m_fovY = 74;
    std::vector<Vector3> m_brushOffsets;
    std::vector<char> m_hiddenBrushEnts;
    Music* m_music = nullptr;
    struct LoopSound {
        int entity;
        std::string path;
        Music music;
    };
    std::vector<LoopSound> m_loops;
    std::map<std::string, std::vector<Sound>> m_soundAliases;
    int m_pipCamera = -1;
    float m_pipTime = 0;
    RenderTexture2D m_pipTarget{};
    Vector3 m_listenerPos{0, 0, 0}, m_listenerRight{1, 0, 0};

    // events (dispatch to native plugin or Lua class + components)
    void EvSpawn(Entity* e);
    void EvUpdate(Entity* e, float dt);
    void EvTouch(Entity* e, Entity* other, Vector3 n);
    void EvUse(Entity* e, Entity* activator);
    float EvDamaged(Entity* e, float amount, Entity* attacker, Vector3 dir);
    void EvPain(Entity* e, float amount, Entity* attacker);
    void EvDie(Entity* e, Entity* attacker, float amount);
    void EvBlocked(Entity* e, Entity* other);
    void EvAnimEvent(Entity* e, const std::string& name);  // Lua only (anim_event callback)
    void CreateComponent(Entity* e, Component& c);
    void DestroyEntityScripts(Entity* e);

    void FinishLoading();
    void SpawnMapEntities();
    void SpawnPlayer();
    void UpdatePlayer(float dt, bool input);
    void PlayerMove(float dt, Vector3 wishdir, float wishspeed, bool jump);
    void UpdateWeapon(float dt, bool input);
    void SelectBestWeapon();
    void UpdateEntities(float dt);
    void RunPhysics(Entity* e, float dt);
    void CheckTriggers();
    bool GroundTrace(Entity* e, Vector3* normal, int* groundEnt);
    int SlideMove(Entity* e, float dt);
    void StepSlideMove(Entity* e, float dt, float stepHeight);
    void UpdateCamera(float dt);
    void UpdateSounds();
    void CleanupRemoved();
    void DrawEntities(const Camera3D& cam);
    void DrawViewWeapon(float aspect);
    void DrawWorld3D(const Camera3D& cam, float aspect, bool viewWeapon);
    void UpdateEntityLight(Entity* e);
    void AnimateEntity(Entity* e, float dt);
    Sound GetSoundAlias(const std::string& path);
    void PlayerDied();
    void DrawFallbackHud(int w, int h);
    Entity* EntityForBrush(int brush) const;
};

}  // namespace rbs
