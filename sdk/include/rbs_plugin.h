/*
 * RBS Engine - native plugin API (C ABI)
 * ======================================
 *
 * Game code can be written in any language that can build a shared library with
 * C-compatible exports: C, C++, Rust, C# (NativeAOT), Zig, Odin, Nim, Go (c-shared)...
 *
 * A plugin is a .dll / .so / .dylib placed in   base/plugins/   or   mods/<mod>/plugins/
 * It must export:
 *
 *     int  rbs_plugin_init(const rbs_api* api);   // return 0 on success
 *     void rbs_plugin_shutdown(void);             // optional
 *
 * In rbs_plugin_init the plugin registers entity classes and components with
 * api->register_class / api->register_component. The engine then calls the
 * behaviour callbacks; inside them the plugin uses the api functions to read and
 * modify entities and the world.
 *
 * Entities are referenced by integer ids (rbs_entity). Ids are never reused during a
 * map, so holding an id is always safe - api->exists() tells whether it is alive.
 *
 * Property names are shared with the Lua API, e.g. "pos", "vel", "yaw", "health",
 * "model", "anim", "movetype", "solid"... (see docs/SCRIPTING.md).
 *
 * The API is versioned: check api->version >= RBS_PLUGIN_API_VERSION. New functions
 * are only ever appended at the end of rbs_api, so old plugins keep working.
 *
 * This header is MIT licensed like the rest of the engine.
 */
#ifndef RBS_PLUGIN_H
#define RBS_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RBS_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#define RBS_EXPORT __declspec(dllexport)
#else
#define RBS_EXPORT __attribute__((visibility("default")))
#endif

typedef int32_t rbs_entity; /* 0 = no entity */

typedef struct rbs_vec3 {
    float x, y, z;
} rbs_vec3;

typedef struct rbs_trace_result {
    float fraction;     /* 0..1, 1 = nothing hit */
    rbs_vec3 pos;       /* end position */
    rbs_vec3 normal;    /* surface normal at the hit */
    int32_t hit;        /* non-zero if something was hit */
    int32_t startsolid; /* started inside geometry */
    int32_t sky;        /* hit a sky surface */
    rbs_entity entity;  /* entity hit (0 = world) */
} rbs_trace_result;

/*
 * Callbacks of an entity class or a component. Any pointer may be NULL.
 * `data` is whatever `create` returned for this instance (may be NULL).
 */
typedef struct rbs_behaviour {
    void* user; /* passed back to create() - handy for language bindings */
    void* (*create)(void* user, rbs_entity self, const char* props_json);
    void (*destroy)(void* data, rbs_entity self);
    void (*spawn)(void* data, rbs_entity self); /* class: spawned / component: start */
    void (*update)(void* data, rbs_entity self, float dt);
    void (*touch)(void* data, rbs_entity self, rbs_entity other, rbs_vec3 normal);
    void (*use)(void* data, rbs_entity self, rbs_entity activator);
    /* called before damage is applied; return the (possibly modified) amount */
    float (*damaged)(void* data, rbs_entity self, float amount, rbs_entity attacker, rbs_vec3 dir);
    void (*pain)(void* data, rbs_entity self, float amount, rbs_entity attacker);
    void (*die)(void* data, rbs_entity self, rbs_entity attacker, float amount);
    void (*blocked)(void* data, rbs_entity self, rbs_entity other);
    /* Editor metadata as JSON (optional), e.g.
       {"desc":"Rotating turret","category":"monsters","color":[255,80,40],
        "mins":[-16,-24,-16],"maxs":[16,32,16],"model":"models/turret.glb",
        "props":{"speed":90,"health":50}}                                         */
    const char* editor_json;
} rbs_behaviour;

typedef struct rbs_api {
    uint32_t version; /* RBS_PLUGIN_API_VERSION of the engine */
    uint32_t size;    /* sizeof(rbs_api) as compiled into the engine */

    /* ---- registration & utilities ---------------------------------------- */
    void (*log)(const char* text);
    void (*register_class)(const char* name, const rbs_behaviour* behaviour);
    void (*register_component)(const char* name, const rbs_behaviour* behaviour);
    void (*console)(const char* command);     /* run a console command */
    void (*lua)(const char* code);            /* run Lua code (bridge to Lua scripts) */
    float (*cvar_get)(const char* name);
    void (*cvar_set)(const char* name, const char* value);
    float (*time)(void);                      /* game time in seconds */
    float (*random)(float min, float max);

    /* ---- entities --------------------------------------------------------- */
    rbs_entity (*player)(void);
    rbs_entity (*spawn)(const char* classname, rbs_vec3 pos, const char* props_json, float yaw);
    void (*remove)(rbs_entity e);
    int32_t (*exists)(rbs_entity e);
    float (*get_float)(rbs_entity e, const char* prop);
    void (*set_float)(rbs_entity e, const char* prop, float value);
    rbs_vec3 (*get_vec)(rbs_entity e, const char* prop);
    void (*set_vec)(rbs_entity e, const char* prop, rbs_vec3 value);
    /* copies the string into buf (always 0-terminated), returns full length */
    int32_t (*get_string)(rbs_entity e, const char* prop, char* buf, int32_t bufsize);
    void (*set_string)(rbs_entity e, const char* prop, const char* value);
    /* map key/value property ("speed", "message"...) as text, returns length or -1 */
    int32_t (*get_prop)(rbs_entity e, const char* key, char* buf, int32_t bufsize);
    void (*set_prop)(rbs_entity e, const char* key, const char* value);
    int32_t (*find)(const char* targetname, rbs_entity* out, int32_t max);
    int32_t (*find_class)(const char* classname, rbs_entity* out, int32_t max);
    int32_t (*find_radius)(rbs_vec3 pos, float radius, rbs_entity* out, int32_t max);

    /* ---- movement / AI helpers ------------------------------------------- */
    void (*walk)(rbs_entity e, rbs_vec3 dir, float speed);
    void (*face)(rbs_entity e, rbs_vec3 target, float turn_speed, float dt);
    int32_t (*visible)(rbs_entity a, rbs_entity b);
    rbs_trace_result (*trace)(rbs_vec3 from, rbs_vec3 to, rbs_entity ignore);

    /* ---- combat ----------------------------------------------------------- */
    void (*damage)(rbs_entity target, float amount, rbs_entity attacker, rbs_vec3 dir);
    void (*radius_damage)(rbs_vec3 pos, float radius, float damage, rbs_entity attacker);
    void (*fire_bullets)(rbs_entity owner, rbs_vec3 origin, rbs_vec3 dir, int32_t count, float spread, float damage);
    rbs_entity (*projectile)(const char* classname, rbs_entity owner, rbs_vec3 origin, rbs_vec3 dir, float speed);
    void (*explosion)(rbs_vec3 pos, float radius, float damage, rbs_entity attacker);
    void (*gib)(rbs_entity e, int32_t count);
    void (*blood)(rbs_vec3 pos, rbs_vec3 dir, int32_t amount);

    /* ---- effects, sound, game flow --------------------------------------- */
    void (*sound)(const char* path, rbs_vec3 pos, float volume, float pitch);
    void (*sound2d)(const char* path, float volume, float pitch);
    void (*particles)(rbs_vec3 pos, rbs_vec3 dir, int32_t count, uint32_t rgba, float speed, float size, float life);
    void (*light)(rbs_vec3 pos, rbs_vec3 color, float radius, float seconds);
    void (*message)(const char* text, float seconds);
    void (*fire_targets)(const char* target, rbs_entity activator);
    void (*objective)(const char* id, const char* text, int32_t done);
    void (*change_map)(const char* map);
    void (*give_weapon)(const char* id, int32_t select);
    void (*give_ammo)(const char* type, int32_t amount);
} rbs_api;

typedef int (*rbs_plugin_init_fn)(const rbs_api* api);
typedef void (*rbs_plugin_shutdown_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* RBS_PLUGIN_H */
