// RBS Engine - C++ helper layer over the C plugin API (header only, C++17).
//
// Write entity classes and components as C++ classes, Unity MonoBehaviour style:
//
//     struct Turret : rbs::Behaviour {
//         void Spawn() override { self.SetModel("models/turret.glb"); self.SetSolid("bbox"); }
//         void Update(float dt) override { self.Face(rbs::Player().Pos(), 90, dt); }
//     };
//     RBS_PLUGIN_INIT {
//         rbs::RegisterEntity<Turret>("my_turret", R"({"category":"monsters","props":{"range":800}})");
//     }
//
// Build as a shared library and drop it into base/plugins or mods/<mod>/plugins.
#pragma once

#include "rbs_plugin.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace rbs {

inline const rbs_api* api = nullptr;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3(rbs_vec3 v) : x(v.x), y(v.y), z(v.z) {}
    operator rbs_vec3() const { return {x, y, z}; }
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float Dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    float Len() const { return std::sqrt(Dot(*this)); }
    Vec3 Norm() const {
        float l = Len();
        return l > 0 ? *this * (1.0f / l) : Vec3{};
    }
};

// Lightweight handle to an engine entity (just its id).
struct Entity {
    rbs_entity id = 0;
    Entity() = default;
    explicit Entity(rbs_entity i) : id(i) {}
    bool Valid() const { return id != 0 && api->exists(id); }
    explicit operator bool() const { return Valid(); }
    bool operator==(Entity o) const { return id == o.id; }

    float Get(const char* p) const { return api->get_float(id, p); }
    void Set(const char* p, float v) const { api->set_float(id, p, v); }
    Vec3 GetVec(const char* p) const { return api->get_vec(id, p); }
    void SetVec(const char* p, Vec3 v) const { api->set_vec(id, p, v); }
    std::string GetStr(const char* p) const {
        char buf[256];
        int n = api->get_string(id, p, buf, sizeof(buf));
        if (n < (int)sizeof(buf)) return buf;
        std::string s(n + 1, '\0');
        api->get_string(id, p, s.data(), n + 1);
        s.resize(n);
        return s;
    }
    void SetStr(const char* p, const char* v) const { api->set_string(id, p, v); }
    // map key/values set in the editor
    std::string Prop(const char* key, const char* def = "") const {
        char buf[512];
        int n = api->get_prop(id, key, buf, sizeof(buf));
        return n < 0 ? std::string(def) : std::string(buf);
    }
    float PropF(const char* key, float def) const {
        std::string s = Prop(key);
        return s.empty() ? def : std::strtof(s.c_str(), nullptr);
    }

    Vec3 Pos() const { return GetVec("pos"); }
    void SetPos(Vec3 v) const { SetVec("pos", v); }
    Vec3 Vel() const { return GetVec("vel"); }
    void SetVel(Vec3 v) const { SetVec("vel", v); }
    Vec3 Center() const { return GetVec("center"); }
    float Health() const { return Get("health"); }
    void SetHealth(float h) const { Set("health", h); }
    float Yaw() const { return Get("yaw"); }
    void SetYaw(float y) const { Set("yaw", y); }
    std::string Class() const { return GetStr("classname"); }
    void SetModel(const char* m) const { SetStr("model", m); }
    void SetAnim(const char* a) const { SetStr("anim", a); }
    void SetSolid(const char* s) const { SetStr("solid", s); }
    void SetMoveType(const char* m) const { SetStr("movetype", m); }
    void SetBox(Vec3 mins, Vec3 maxs) const {
        SetVec("mins", mins);
        SetVec("maxs", maxs);
    }
    void SetLight(Vec3 color, float radius) const {
        SetVec("light_color", color);
        Set("light_radius", radius);
    }
    void Remove() const { api->remove(id); }
    void Walk(Vec3 dir, float speed) const { api->walk(id, dir, speed); }
    void Face(Vec3 target, float turnSpeed, float dt) const { api->face(id, target, turnSpeed, dt); }
    bool CanSee(Entity other) const { return api->visible(id, other.id) != 0; }
    void Damage(float amount, Entity attacker = Entity(0), Vec3 dir = {}) const { api->damage(id, amount, attacker.id, dir); }
    float Distance(Entity o) const { return (Pos() - o.Pos()).Len(); }
};

inline Entity Player() { return Entity(api->player()); }
inline float Time() { return api->time(); }
inline float Random(float a, float b) { return api->random(a, b); }
inline void Log(const std::string& s) { api->log(s.c_str()); }
inline void Sound(const char* path, Vec3 pos, float vol = 1, float pitch = 1) { api->sound(path, pos, vol, pitch); }
inline void Message(const char* text, float seconds = 3) { api->message(text, seconds); }
inline Entity Spawn(const char* cls, Vec3 pos, const char* propsJson = nullptr, float yaw = 0) {
    return Entity(api->spawn(cls, pos, propsJson, yaw));
}

// Minimal lookup of numbers/strings in a flat JSON object (component props).
inline float JsonNumber(const std::string& json, const char* key, float def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return def;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return def;
    return std::strtof(json.c_str() + p + 1, nullptr);
}
inline std::string JsonString(const std::string& json, const char* key, const char* def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return def;
    p = json.find('"', json.find(':', p + k.size()) + 1);
    size_t e = json.find('"', p + 1);
    if (p == std::string::npos || e == std::string::npos) return def;
    return json.substr(p + 1, e - p - 1);
}

// Base class for C++ behaviours (entity classes and components).
struct Behaviour {
    Entity self;
    std::string props;  // JSON object with the instance properties (editor values + defaults)
    virtual ~Behaviour() = default;
    virtual void Spawn() {}  // class: spawned, component: start
    virtual void Update(float dt) {}
    virtual void Touch(Entity other, Vec3 normal) {}
    virtual void Use(Entity activator) {}
    virtual float Damaged(float amount, Entity attacker, Vec3 dir) { return amount; }
    virtual void Pain(float amount, Entity attacker) {}
    virtual void Die(Entity attacker, float amount) {}
    virtual void Blocked(Entity other) {}
    float PropF(const char* key, float def) const { return JsonNumber(props, key, def); }
    std::string PropS(const char* key, const char* def = "") const { return JsonString(props, key, def); }
};

namespace detail {
template <class T>
rbs_behaviour MakeBehaviour(const char* editorJson) {
    rbs_behaviour b{};
    b.editor_json = editorJson;
    b.create = [](void*, rbs_entity self, const char* props) -> void* {
        T* t = new T();
        t->self = Entity(self);
        t->props = props ? props : "{}";
        return t;
    };
    b.destroy = [](void* d, rbs_entity) { delete static_cast<T*>(d); };
    b.spawn = [](void* d, rbs_entity) { static_cast<T*>(d)->Spawn(); };
    b.update = [](void* d, rbs_entity, float dt) { static_cast<T*>(d)->Update(dt); };
    b.touch = [](void* d, rbs_entity, rbs_entity o, rbs_vec3 n) { static_cast<T*>(d)->Touch(Entity(o), Vec3(n)); };
    b.use = [](void* d, rbs_entity, rbs_entity a) { static_cast<T*>(d)->Use(Entity(a)); };
    b.damaged = [](void* d, rbs_entity, float amt, rbs_entity a, rbs_vec3 dir) { return static_cast<T*>(d)->Damaged(amt, Entity(a), Vec3(dir)); };
    b.pain = [](void* d, rbs_entity, float amt, rbs_entity a) { static_cast<T*>(d)->Pain(amt, Entity(a)); };
    b.die = [](void* d, rbs_entity, rbs_entity a, float amt) { static_cast<T*>(d)->Die(Entity(a), amt); };
    b.blocked = [](void* d, rbs_entity, rbs_entity o) { static_cast<T*>(d)->Blocked(Entity(o)); };
    return b;
}
}  // namespace detail

template <class T>
void RegisterEntity(const char* name, const char* editorJson = nullptr) {
    rbs_behaviour b = detail::MakeBehaviour<T>(editorJson);
    api->register_class(name, &b);
}

template <class T>
void RegisterComponent(const char* name, const char* editorJson = nullptr) {
    rbs_behaviour b = detail::MakeBehaviour<T>(editorJson);
    api->register_component(name, &b);
}

}  // namespace rbs

// Defines the exported entry point. The body runs once when the plugin loads.
#define RBS_PLUGIN_INIT                                                   \
    static void rbs_plugin_register();                                    \
    extern "C" RBS_EXPORT int rbs_plugin_init(const rbs_api* a) {         \
        if (!a || a->version < RBS_PLUGIN_API_VERSION) return 1;          \
        rbs::api = a;                                                     \
        rbs_plugin_register();                                            \
        return 0;                                                         \
    }                                                                     \
    static void rbs_plugin_register()
