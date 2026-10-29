// RBS Engine - runtime game entity.
#pragma once

#include "core/common.h"
#include "world/map.h"

#include <deque>

namespace rbs {

enum class MoveType { None, Walk, Fly, Toss, Bounce, Push, Noclip };
enum class SolidType { Not, Trigger, BBox, Corpse };  // Corpse: hit by hitscan only

// A behaviour attached to an entity (like a Unity component). Implemented either in
// Lua (rbs.component) or in a native plugin (C ABI: C, C++, Rust, C#, Zig...).
struct Component {
    std::string type;
    json props = json::object();
    int luaRef = -2;           // Lua instance table (LUA_NOREF = -2)
    int native = -1;           // native behaviour index (plugins)
    void* nativeData = nullptr;
    bool started = false;
};

struct Entity {
    int id = 0;
    std::string classname;
    bool removed = false;

    // physics
    Vector3 pos{0, 0, 0};
    Vector3 vel{0, 0, 0};
    Vector3 angles{0, 0, 0};  // pitch, yaw, roll (degrees)
    Vector3 mins{-16, -24, -16};
    Vector3 maxs{16, 32, 16};
    MoveType movetype = MoveType::None;
    SolidType solid = SolidType::Not;
    bool onGround = false;
    int groundEntity = -1;
    float gravityScale = 1.0f;
    float bounce = 0.4f;
    float friction = 6.0f;
    int owner = -1;          // projectiles don't collide with their owner
    bool walked = false;     // monster called walk() this frame (no friction)
    bool blocked = false;    // last move was obstructed
    float lifetime = -1;     // auto remove after this many seconds (gibs)
    bool takeDamage = false;
    bool monster = false;
    bool dead = false;

    float health = 0;
    float maxHealth = 100;
    float armor = 0;
    int team = 0;

    // rendering
    std::string model;       // model path ("models/grunt.glb")
    std::string sprite;      // sprite texture (billboard) if no model
    float spriteSize = 32;
    bool spriteAdditive = false;
    float modelScale = 1.0f;
    Vector3 modelOffset{0, 0, 0};
    Color tint = WHITE;
    bool fullbright = false;
    bool visible = true;
    std::string anim;        // current animation name
    float animTime = 0;
    float animSpeed = 1.0f;
    bool animLoop = true;
    bool animDone = false;
    float flash = 0;         // white hit flash 0..1
    float spin = 0;          // degrees per second (pickups)
    float bobAmount = 0;     // vertical bobbing (pickups)

    // dynamic light attached to the entity
    Vector3 lightColor{0, 0, 0};
    float lightRadius = 0;

    // cached static lighting for models
    Vector3 lightAmbient{1, 1, 1}, lightDirColor{0, 0, 0}, lightDir{0, 1, 0};
    float lightUpdate = -1;

    // map data
    int mapIndex = -1;       // index into Map::entities (brush entities, -1 otherwise)
    std::string targetname, target;
    json props = json::object();

    // brush entity movement (doors/platforms)
    Vector3 moveOffset{0, 0, 0};

    // scripting
    int luaRef = -2;         // registry ref of the Lua `self` table (LUA_NOREF = -2)
    int classRef = -2;
    int nativeClass = -1;    // class implemented by a native plugin
    void* nativeData = nullptr;
    std::deque<Component> components;  // deque: callbacks may add components safely
    float nextThink = 0;

    // sound
    std::string loopSound;
    float loopVolume = 1.0f;
    float loopRadius = 600.0f;

    Vector3 Center() const { return pos + (mins + maxs) * 0.5f; }
    Vector3 AbsMin() const { return pos + mins; }
    Vector3 AbsMax() const { return pos + maxs; }
};

}  // namespace rbs
