#include "script/script.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/project.h"
#include "core/vfs.h"
#include "game/game.h"
#include "render/assets.h"
#include "ui/fonts.h"
#include "ui/lang.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <unordered_map>

namespace rbs {

// ---------------------------------------------------------------------------------
// Embedded prelude: vector type, class system, generic entity methods.
// ---------------------------------------------------------------------------------
static const char* kPrelude = R"LUA(
local vec3 = {}
vec3.__index = vec3
local function V(x, y, z) return setmetatable({x = x or 0, y = y or 0, z = z or 0}, vec3) end
vec3.__add = function(a, b) return V(a.x + b.x, a.y + b.y, a.z + b.z) end
vec3.__sub = function(a, b) return V(a.x - b.x, a.y - b.y, a.z - b.z) end
vec3.__mul = function(a, b)
  if type(a) == "number" then return V(b.x * a, b.y * a, b.z * a) end
  if type(b) == "number" then return V(a.x * b, a.y * b, a.z * b) end
  return V(a.x * b.x, a.y * b.y, a.z * b.z)
end
vec3.__div = function(a, b) return V(a.x / b, a.y / b, a.z / b) end
vec3.__unm = function(a) return V(-a.x, -a.y, -a.z) end
vec3.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
vec3.__tostring = function(a) return string.format("(%.1f %.1f %.1f)", a.x, a.y, a.z) end
function vec3:len() return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z) end
function vec3:len2d() return math.sqrt(self.x * self.x + self.z * self.z) end
function vec3:normalize()
  local l = self:len()
  if l > 0 then return V(self.x / l, self.y / l, self.z / l) end
  return V()
end
function vec3:dot(b) return self.x * b.x + self.y * b.y + self.z * b.z end
function vec3:cross(b) return V(self.y * b.z - self.z * b.y, self.z * b.x - self.x * b.z, self.x * b.y - self.y * b.x) end
function vec3:dist(b) return (self - b):len() end
function vec3:flat() return V(self.x, 0, self.z) end
function vec3:copy() return V(self.x, self.y, self.z) end
function vec3:lerp(b, t) return V(self.x + (b.x - self.x) * t, self.y + (b.y - self.y) * t, self.z + (b.z - self.z) * t) end
vec = setmetatable({new = V, meta = vec3}, {__call = function(_, x, y, z) return V(x, y, z) end})
function vec.from_yaw(yaw)
  local r = math.rad(yaw)
  return V(math.cos(r), 0, -math.sin(r))
end
function vec.from_angles(pitch, yaw)
  local p, y = math.rad(pitch), math.rad(yaw)
  return V(math.cos(p) * math.cos(y), math.sin(p), -math.cos(p) * math.sin(y))
end
function vec.yaw(v) return math.deg(math.atan(-v.z, v.x)) end
function vec.random()
  while true do
    local v = V(math.random() * 2 - 1, math.random() * 2 - 1, math.random() * 2 - 1)
    local l = v:len()
    if l > 0.01 and l <= 1 then return v / l end
  end
end
rbs._vec3 = vec3

rbs.classes = {}
rbs.weapons = {}
rbs._targets = {}

Entity = {}
function Entity:remove() rbs.remove(self) end
function Entity:sound(path, volume, pitch) rbs.sound(path, self.pos, volume, pitch) end
function Entity:distance(other) return (self.pos - other.pos):len() end
function Entity:can_see(other) return rbs.visible(self, other) end
function Entity:walk(dir, speed) rbs.walk(self, dir, speed) end
function Entity:face(pos, speed, dt) rbs.face(self, pos, speed or 360, dt or 1) end
function Entity:damage(amount, attacker, dir) rbs.damage(self, amount, attacker, dir) end
function Entity:forward() return vec.from_yaw(self.yaw) end
function Entity:play(anim, loop, speed)
  if self.anim ~= anim then
    self.anim = anim
    self.anim_time = 0
  end
  if loop ~= nil then self.anim_loop = loop end
  self.anim_speed = speed or 1
end

function rbs.class(name, def)
  def = def or {}
  def.classname = name
  setmetatable(def, {__index = function(t, k)
    local base = rawget(t, "base")
    if base then
      local b = rbs.classes[base]
      if b then return b[k] end
    end
    return Entity[k]
  end})
  rbs.classes[name] = def
  rbs._register_class(name, def)
  return def
end

rbs.components = {}
Component = {}
function Component:sound(path, volume, pitch) rbs.sound(path, self.entity.pos, volume, pitch) end

function rbs.component(name, def)
  def = def or {}
  def.type = name
  setmetatable(def, {__index = function(t, k)
    local base = rawget(t, "base")
    if base then
      local b = rbs.components[base]
      if b then return b[k] end
    end
    return Component[k]
  end})
  rbs.components[name] = def
  rbs._register_component(name, def)
  return def
end

function rbs._new_component(name, ent, props)
  local cls = rbs.components[name]
  if not cls then return nil end
  local c = {}
  if cls.editor and cls.editor.props then
    for k, v in pairs(cls.editor.props) do c[k] = v end
  end
  for k, v in pairs(props or {}) do c[k] = v end
  c.entity = ent
  c.type = name
  return setmetatable(c, {__index = cls})
end

function rbs.weapon(def)
  -- weapons without their own fire() use the built-in shots (rbs.fire_weapon, weapons.lua)
  if def.fire == nil then def.fire = function(player, w) return rbs.fire_weapon(player, w) end end
  rbs.weapons[def.id] = def
  rbs._register_weapon(def)
  return def
end

function rbs.on(target, fn)
  local list = rbs._targets[target] or {}
  list[#list + 1] = fn
  rbs._targets[target] = list
end

function rbs._fire(target, activator)
  local list = rbs._targets[target]
  if list then
    for _, fn in ipairs(list) do fn(activator) end
  end
end

-- sandbox: no native libraries from Lua (that is what plugins are for), no bytecode
package.loadlib = nil
local raw_load = load
load = function(chunk, name, mode, env) return raw_load(chunk, name, "t", env) end

-- require() loads from the virtual file system: require "monsters.grunt" -> scripts/monsters/grunt.lua
package.path = ""
package.cpath = ""
package.searchers = {
  package.searchers[1],
  function(name)
    local path = "scripts/" .. name:gsub("%.", "/") .. ".lua"
    local fn, err = rbs._loadfile(path)
    if fn then return fn, path end
    return "\n\tno script '" .. path .. "'" .. (err and (" (" .. err .. ")") or "")
  end,
}
)LUA";

// ---------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------
static Script* GetScript(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "rbs_script");
    Script* s = (Script*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return s;
}

static Game* RequireGame(lua_State* L) {
    Script* s = GetScript(L);
    if (!s || !s->game) luaL_error(L, "this function is only available in game");
    return s->game;
}

static void PushVec(lua_State* L, Vector3 v) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, v.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z);
    lua_setfield(L, -2, "z");
    lua_getfield(L, LUA_REGISTRYINDEX, "rbs_vec3");
    lua_setmetatable(L, -2);
}

static Vector3 ToVec(lua_State* L, int idx, Vector3 def = {0, 0, 0}) {
    idx = lua_absindex(L, idx);
    if (!lua_istable(L, idx)) return def;
    Vector3 v = def;
    lua_getfield(L, idx, "x");
    if (lua_isnumber(L, -1)) {
        v.x = (float)lua_tonumber(L, -1);
        lua_getfield(L, idx, "y");
        v.y = (float)luaL_optnumber(L, -1, 0);
        lua_getfield(L, idx, "z");
        v.z = (float)luaL_optnumber(L, -1, 0);
        lua_pop(L, 3);
        return v;
    }
    lua_pop(L, 1);
    lua_rawgeti(L, idx, 1);
    lua_rawgeti(L, idx, 2);
    lua_rawgeti(L, idx, 3);
    if (lua_isnumber(L, -3)) v = {(float)lua_tonumber(L, -3), (float)lua_tonumber(L, -2), (float)lua_tonumber(L, -1)};
    lua_pop(L, 3);
    return v;
}

static Color ToColor(lua_State* L, int idx, Color def = WHITE) {
    idx = lua_absindex(L, idx);
    if (!lua_istable(L, idx)) return def;
    Color c = def;
    auto get = [&](const char* k, int i, unsigned char d) -> unsigned char {
        lua_getfield(L, idx, k);
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            lua_rawgeti(L, idx, i);
        }
        float f = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : (float)d;
        lua_pop(L, 1);
        return (unsigned char)Clampf(f, 0, 255);
    };
    c.r = get("r", 1, def.r);
    c.g = get("g", 2, def.g);
    c.b = get("b", 3, def.b);
    c.a = get("a", 4, def.a);
    return c;
}

static void PushColor(lua_State* L, Color c) {
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, c.r);
    lua_setfield(L, -2, "r");
    lua_pushinteger(L, c.g);
    lua_setfield(L, -2, "g");
    lua_pushinteger(L, c.b);
    lua_setfield(L, -2, "b");
    lua_pushinteger(L, c.a);
    lua_setfield(L, -2, "a");
}

static void PushJson(lua_State* L, const json& j, int depth = 0) {
    // every nesting level holds Lua stack slots: data nested deeper than any real file is cut off
    lua_checkstack(L, 2);
    if (depth > 32) {
        lua_pushnil(L);
        return;
    }
    switch (j.type()) {
        case json::value_t::null: lua_pushnil(L); break;
        case json::value_t::boolean: lua_pushboolean(L, j.get<bool>()); break;
        case json::value_t::number_integer:
        case json::value_t::number_unsigned: lua_pushinteger(L, j.get<long long>()); break;
        case json::value_t::number_float: lua_pushnumber(L, j.get<double>()); break;
        case json::value_t::string: lua_pushstring(L, j.get_ref<const std::string&>().c_str()); break;
        case json::value_t::array: {
            lua_createtable(L, (int)j.size(), 0);
            int i = 1;
            for (auto& v : j) {
                PushJson(L, v, depth + 1);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case json::value_t::object: {
            lua_createtable(L, 0, (int)j.size());
            for (auto it = j.begin(); it != j.end(); ++it) {
                PushJson(L, it.value(), depth + 1);
                lua_setfield(L, -2, it.key().c_str());
            }
            break;
        }
        default: lua_pushnil(L);
    }
}

static json ToJson(lua_State* L, int idx, int depth = 0) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN: return (bool)lua_toboolean(L, idx);
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) return (long long)lua_tointeger(L, idx);
            return lua_tonumber(L, idx);
        case LUA_TSTRING: return std::string(lua_tostring(L, idx));
        case LUA_TTABLE: {
            if (depth > 16) return nullptr;
            size_t len = lua_rawlen(L, idx);
            if (len > 0) {
                json arr = json::array();
                for (size_t i = 1; i <= len; ++i) {
                    lua_rawgeti(L, idx, (lua_Integer)i);
                    arr.push_back(ToJson(L, -1, depth + 1));
                    lua_pop(L, 1);
                }
                return arr;
            }
            json obj = json::object();
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                if (lua_type(L, -2) == LUA_TSTRING) obj[lua_tostring(L, -2)] = ToJson(L, -1, depth + 1);
                lua_pop(L, 1);
            }
            return obj;
        }
        default: return nullptr;
    }
}

static void PushEntity(lua_State* L, Entity* e) {
    if (!e || e->luaRef < 0) {
        lua_pushnil(L);
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, e->luaRef);
}

static Entity* ToEntity(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) return nullptr;
    Script* s = GetScript(L);
    if (!s || !s->game) return nullptr;
    lua_getfield(L, idx, "__id");
    int id = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return s->game->Find(id);
}

static Entity* CheckEntity(lua_State* L, int idx) {
    Entity* e = ToEntity(L, idx);
    if (!e) luaL_error(L, "argument %d: expected a live entity", idx);
    return e;
}

static void PushEntityList(lua_State* L, const std::vector<Entity*>& list) {
    lua_createtable(L, (int)list.size(), 0);
    int i = 1;
    for (Entity* e : list) {
        PushEntity(L, e);
        lua_rawseti(L, -2, i++);
    }
}

static float OptFieldF(lua_State* L, int idx, const char* key, float def) {
    lua_getfield(L, idx, key);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

static std::string OptFieldS(lua_State* L, int idx, const char* key, const std::string& def) {
    lua_getfield(L, idx, key);
    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

static bool OptFieldB(lua_State* L, int idx, const char* key, bool def) {
    lua_getfield(L, idx, key);
    bool v = lua_isnil(L, -1) ? def : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

static Vector3 OptFieldV(lua_State* L, int idx, const char* key, Vector3 def) {
    lua_getfield(L, idx, key);
    Vector3 v = ToVec(L, -1, def);
    lua_pop(L, 1);
    return v;
}

// ---------------------------------------------------------------------------------
// entity property access
// ---------------------------------------------------------------------------------
enum Prop {
    P_ID, P_CLASSNAME, P_POS, P_VEL, P_ANGLES, P_YAW, P_PITCH, P_MINS, P_MAXS, P_HEALTH, P_MAXHEALTH, P_ARMOR, P_TEAM,
    P_MODEL, P_SPRITE, P_SPRITESIZE, P_SPRITEADD, P_SCALE, P_MODELOFFSET, P_TINT, P_FULLBRIGHT, P_VISIBLE, P_ANIM,
    P_ANIMSPEED, P_ANIMLOOP, P_ANIMDONE, P_ANIMTIME, P_MOVETYPE, P_SOLID, P_ONGROUND, P_OWNER, P_TAKEDAMAGE, P_MONSTER,
    P_DEAD, P_TARGETNAME, P_TARGET, P_PROPS, P_LIGHTCOLOR, P_LIGHTRADIUS, P_SPIN, P_BOB, P_FLASH, P_GRAVITY, P_BOUNCE,
    P_FRICTION, P_LOOPSOUND, P_LOOPVOLUME, P_LOOPRADIUS, P_REMOVED, P_CENTER, P_ABSMIN, P_ABSMAX, P_OFFSET, P_NEXTTHINK,
    P_BMINS, P_BMAXS, P_BRUSH, P_BLOCKED, P_LIFETIME
};

static const std::unordered_map<std::string, Prop>& PropMap() {
    static const std::unordered_map<std::string, Prop> m = {
        {"id", P_ID}, {"classname", P_CLASSNAME}, {"pos", P_POS}, {"vel", P_VEL}, {"angles", P_ANGLES}, {"yaw", P_YAW},
        {"pitch", P_PITCH}, {"mins", P_MINS}, {"maxs", P_MAXS}, {"health", P_HEALTH}, {"max_health", P_MAXHEALTH},
        {"armor", P_ARMOR}, {"team", P_TEAM}, {"model", P_MODEL}, {"sprite", P_SPRITE}, {"sprite_size", P_SPRITESIZE},
        {"sprite_additive", P_SPRITEADD}, {"scale", P_SCALE}, {"model_offset", P_MODELOFFSET}, {"tint", P_TINT},
        {"fullbright", P_FULLBRIGHT}, {"visible", P_VISIBLE}, {"anim", P_ANIM}, {"anim_speed", P_ANIMSPEED},
        {"anim_loop", P_ANIMLOOP}, {"anim_done", P_ANIMDONE}, {"anim_time", P_ANIMTIME}, {"movetype", P_MOVETYPE},
        {"solid", P_SOLID}, {"on_ground", P_ONGROUND}, {"owner", P_OWNER}, {"takedamage", P_TAKEDAMAGE},
        {"monster", P_MONSTER}, {"dead", P_DEAD}, {"targetname", P_TARGETNAME}, {"target", P_TARGET}, {"props", P_PROPS},
        {"light_color", P_LIGHTCOLOR}, {"light_radius", P_LIGHTRADIUS}, {"spin", P_SPIN}, {"bob", P_BOB},
        {"flash", P_FLASH}, {"gravity", P_GRAVITY}, {"bounce", P_BOUNCE}, {"friction", P_FRICTION},
        {"loop_sound", P_LOOPSOUND}, {"loop_volume", P_LOOPVOLUME}, {"loop_radius", P_LOOPRADIUS},
        {"removed", P_REMOVED}, {"center", P_CENTER}, {"absmin", P_ABSMIN}, {"absmax", P_ABSMAX},
        {"offset", P_OFFSET}, {"next_think", P_NEXTTHINK}, {"bmins", P_BMINS}, {"bmaxs", P_BMAXS}, {"is_brush", P_BRUSH},
        {"blocked", P_BLOCKED}, {"lifetime", P_LIFETIME}};
    return m;
}

static const char* MoveTypeName(MoveType m) {
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

static MoveType MoveTypeFromName(const std::string& s) {
    if (s == "walk") return MoveType::Walk;
    if (s == "fly") return MoveType::Fly;
    if (s == "toss") return MoveType::Toss;
    if (s == "bounce") return MoveType::Bounce;
    if (s == "push") return MoveType::Push;
    if (s == "noclip") return MoveType::Noclip;
    return MoveType::None;
}

static void BrushBounds(Game* g, Entity* e, Vector3& mn, Vector3& mx) {
    mn = {1e30f, 1e30f, 1e30f};
    mx = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (auto& b : g->GetMap().brushes) {
        if (b.entity != e->mapIndex || e->mapIndex < 0) continue;
        mn = VMin(mn, b.bounds.min);
        mx = VMax(mx, b.bounds.max);
        any = true;
    }
    if (!any) mn = mx = e->pos;
    mn += e->moveOffset;
    mx += e->moveOffset;
}

static int EntityIndex(lua_State* L) {
    // self, key
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char* key = lua_tostring(L, 2);
        auto& pm = PropMap();
        auto it = pm.find(key);
        if (it != pm.end()) {
            Script* s = GetScript(L);
            lua_getfield(L, 1, "__id");
            int id = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            Entity* e = (s && s->game) ? s->game->Find(id) : nullptr;
            if (!e) {
                if (it->second == P_REMOVED) {
                    lua_pushboolean(L, 1);
                    return 1;
                }
                if (it->second == P_ID) {
                    lua_pushinteger(L, id);
                    return 1;
                }
                lua_pushnil(L);
                return 1;
            }
            switch (it->second) {
                case P_ID: lua_pushinteger(L, e->id); break;
                case P_CLASSNAME: lua_pushstring(L, e->classname.c_str()); break;
                case P_POS: PushVec(L, e->pos); break;
                case P_VEL: PushVec(L, e->vel); break;
                case P_ANGLES: PushVec(L, e->angles); break;
                case P_YAW: lua_pushnumber(L, e->angles.y); break;
                case P_PITCH: lua_pushnumber(L, e->angles.x); break;
                case P_MINS: PushVec(L, e->mins); break;
                case P_MAXS: PushVec(L, e->maxs); break;
                case P_HEALTH: lua_pushnumber(L, e->health); break;
                case P_MAXHEALTH: lua_pushnumber(L, e->maxHealth); break;
                case P_ARMOR: lua_pushnumber(L, e->armor); break;
                case P_TEAM: lua_pushinteger(L, e->team); break;
                case P_MODEL: lua_pushstring(L, e->model.c_str()); break;
                case P_SPRITE: lua_pushstring(L, e->sprite.c_str()); break;
                case P_SPRITESIZE: lua_pushnumber(L, e->spriteSize); break;
                case P_SPRITEADD: lua_pushboolean(L, e->spriteAdditive); break;
                case P_SCALE: lua_pushnumber(L, e->modelScale); break;
                case P_MODELOFFSET: PushVec(L, e->modelOffset); break;
                case P_TINT: PushColor(L, e->tint); break;
                case P_FULLBRIGHT: lua_pushboolean(L, e->fullbright); break;
                case P_VISIBLE: lua_pushboolean(L, e->visible); break;
                case P_ANIM: lua_pushstring(L, e->anim.c_str()); break;
                case P_ANIMSPEED: lua_pushnumber(L, e->animSpeed); break;
                case P_ANIMLOOP: lua_pushboolean(L, e->animLoop); break;
                case P_ANIMDONE: lua_pushboolean(L, e->animDone); break;
                case P_ANIMTIME: lua_pushnumber(L, e->animTime); break;
                case P_MOVETYPE: lua_pushstring(L, MoveTypeName(e->movetype)); break;
                case P_SOLID:
                    lua_pushstring(L, e->solid == SolidType::BBox ? (e->mapIndex >= 0 ? "bsp" : "bbox")
                                      : e->solid == SolidType::Trigger ? "trigger" : e->solid == SolidType::Corpse ? "corpse" : "not");
                    break;
                case P_ONGROUND: lua_pushboolean(L, e->onGround); break;
                case P_OWNER: PushEntity(L, s->game->Find(e->owner)); break;
                case P_TAKEDAMAGE: lua_pushboolean(L, e->takeDamage); break;
                case P_MONSTER: lua_pushboolean(L, e->monster); break;
                case P_DEAD: lua_pushboolean(L, e->dead); break;
                case P_TARGETNAME: lua_pushstring(L, e->targetname.c_str()); break;
                case P_TARGET: lua_pushstring(L, e->target.c_str()); break;
                case P_PROPS: PushJson(L, e->props); break;
                case P_LIGHTCOLOR: PushVec(L, e->lightColor); break;
                case P_LIGHTRADIUS: lua_pushnumber(L, e->lightRadius); break;
                case P_SPIN: lua_pushnumber(L, e->spin); break;
                case P_BOB: lua_pushnumber(L, e->bobAmount); break;
                case P_FLASH: lua_pushnumber(L, e->flash); break;
                case P_GRAVITY: lua_pushnumber(L, e->gravityScale); break;
                case P_BOUNCE: lua_pushnumber(L, e->bounce); break;
                case P_FRICTION: lua_pushnumber(L, e->friction); break;
                case P_LOOPSOUND: lua_pushstring(L, e->loopSound.c_str()); break;
                case P_LOOPVOLUME: lua_pushnumber(L, e->loopVolume); break;
                case P_LOOPRADIUS: lua_pushnumber(L, e->loopRadius); break;
                case P_REMOVED: lua_pushboolean(L, e->removed); break;
                case P_CENTER: {
                    if (e->mapIndex >= 0) {
                        Vector3 mn, mx;
                        BrushBounds(s->game, e, mn, mx);
                        PushVec(L, (mn + mx) * 0.5f);
                    } else {
                        PushVec(L, e->Center());
                    }
                    break;
                }
                case P_ABSMIN: PushVec(L, e->AbsMin()); break;
                case P_ABSMAX: PushVec(L, e->AbsMax()); break;
                case P_OFFSET: PushVec(L, e->moveOffset); break;
                case P_NEXTTHINK: lua_pushnumber(L, e->nextThink); break;
                case P_BMINS:
                case P_BMAXS: {
                    Vector3 mn, mx;
                    BrushBounds(s->game, e, mn, mx);
                    PushVec(L, it->second == P_BMINS ? mn : mx);
                    break;
                }
                case P_BRUSH: lua_pushboolean(L, e->mapIndex >= 0); break;
                case P_BLOCKED: lua_pushboolean(L, e->blocked); break;
                case P_LIFETIME: lua_pushnumber(L, e->lifetime); break;
            }
            return 1;
        }
    }
    // class methods / fields
    lua_getfield(L, 1, "__class");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);
        lua_gettable(L, -2);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    // generic Entity methods
    lua_getglobal(L, "Entity");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int EntityNewIndex(lua_State* L) {
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char* key = lua_tostring(L, 2);
        auto& pm = PropMap();
        auto it = pm.find(key);
        if (it != pm.end()) {
            Script* s = GetScript(L);
            lua_getfield(L, 1, "__id");
            int id = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            Entity* e = (s && s->game) ? s->game->Find(id) : nullptr;
            if (!e) return 0;
            switch (it->second) {
                case P_POS: e->pos = ToVec(L, 3, e->pos); break;
                case P_VEL: e->vel = ToVec(L, 3, e->vel); break;
                case P_ANGLES: e->angles = ToVec(L, 3, e->angles); break;
                case P_YAW: e->angles.y = (float)luaL_checknumber(L, 3); break;
                case P_PITCH: e->angles.x = (float)luaL_checknumber(L, 3); break;
                case P_MINS: e->mins = ToVec(L, 3, e->mins); break;
                case P_MAXS: e->maxs = ToVec(L, 3, e->maxs); break;
                case P_HEALTH: e->health = (float)luaL_checknumber(L, 3); break;
                case P_MAXHEALTH: e->maxHealth = (float)luaL_checknumber(L, 3); break;
                case P_ARMOR: e->armor = (float)luaL_checknumber(L, 3); break;
                case P_TEAM: e->team = (int)luaL_checkinteger(L, 3); break;
                case P_MODEL: e->model = luaL_optstring(L, 3, ""); break;
                case P_SPRITE: e->sprite = luaL_optstring(L, 3, ""); break;
                case P_SPRITESIZE: e->spriteSize = (float)luaL_checknumber(L, 3); break;
                case P_SPRITEADD: e->spriteAdditive = lua_toboolean(L, 3); break;
                case P_SCALE: e->modelScale = (float)luaL_checknumber(L, 3); break;
                case P_MODELOFFSET: e->modelOffset = ToVec(L, 3, e->modelOffset); break;
                case P_TINT: e->tint = ToColor(L, 3, e->tint); break;
                case P_FULLBRIGHT: e->fullbright = lua_toboolean(L, 3); break;
                case P_VISIBLE: e->visible = lua_toboolean(L, 3); break;
                case P_ANIM: {
                    std::string a = luaL_optstring(L, 3, "");
                    if (a != e->anim) {
                        e->anim = a;
                        e->animTime = 0;
                        e->animDone = false;
                    }
                    break;
                }
                case P_ANIMSPEED: e->animSpeed = (float)luaL_checknumber(L, 3); break;
                case P_ANIMLOOP: e->animLoop = lua_toboolean(L, 3); break;
                case P_ANIMTIME:
                    e->animTime = (float)luaL_checknumber(L, 3);
                    e->animDone = false;
                    break;
                case P_MOVETYPE: e->movetype = MoveTypeFromName(luaL_checkstring(L, 3)); break;
                case P_SOLID: {
                    std::string v = luaL_checkstring(L, 3);
                    e->solid = (v == "bbox" || v == "bsp") ? SolidType::BBox
                             : v == "trigger" ? SolidType::Trigger : v == "corpse" ? SolidType::Corpse : SolidType::Not;
                    if (e->mapIndex >= 0) s->game->SetBrushOffset(e, e->moveOffset);  // refresh brush solidity
                    break;
                }
                case P_OWNER: {
                    Entity* o = ToEntity(L, 3);
                    e->owner = o ? o->id : -1;
                    break;
                }
                case P_TAKEDAMAGE: e->takeDamage = lua_toboolean(L, 3); break;
                case P_MONSTER: e->monster = lua_toboolean(L, 3); break;
                case P_DEAD: e->dead = lua_toboolean(L, 3); break;
                case P_TARGETNAME: e->targetname = luaL_optstring(L, 3, ""); break;
                case P_TARGET: e->target = luaL_optstring(L, 3, ""); break;
                case P_PROPS: e->props = ToJson(L, 3); break;
                case P_LIGHTCOLOR: e->lightColor = ToVec(L, 3, e->lightColor); break;
                case P_LIGHTRADIUS: e->lightRadius = (float)luaL_checknumber(L, 3); break;
                case P_SPIN: e->spin = (float)luaL_checknumber(L, 3); break;
                case P_BOB: e->bobAmount = (float)luaL_checknumber(L, 3); break;
                case P_FLASH: e->flash = (float)luaL_checknumber(L, 3); break;
                case P_GRAVITY: e->gravityScale = (float)luaL_checknumber(L, 3); break;
                case P_BOUNCE: e->bounce = (float)luaL_checknumber(L, 3); break;
                case P_FRICTION: e->friction = (float)luaL_checknumber(L, 3); break;
                case P_LOOPSOUND: e->loopSound = luaL_optstring(L, 3, ""); break;
                case P_LOOPVOLUME: e->loopVolume = (float)luaL_checknumber(L, 3); break;
                case P_LOOPRADIUS: e->loopRadius = (float)luaL_checknumber(L, 3); break;
                case P_OFFSET: s->game->SetBrushOffset(e, ToVec(L, 3, e->moveOffset)); break;
                case P_NEXTTHINK: e->nextThink = (float)luaL_checknumber(L, 3); break;
                case P_LIFETIME: e->lifetime = (float)luaL_checknumber(L, 3); break;
                default: return luaL_error(L, "entity field '%s' is read-only", key);
            }
            return 0;
        }
    }
    lua_rawset(L, 1);
    return 0;
}

static int EntityToString(lua_State* L) {
    lua_getfield(L, 1, "__id");
    int id = (int)lua_tointeger(L, -1);
    lua_getfield(L, 1, "__classname");
    lua_pushfstring(L, "entity#%d(%s)", id, lua_tostring(L, -1));
    return 1;
}

// ---------------------------------------------------------------------------------
// rbs.* API
// ---------------------------------------------------------------------------------
static int L_log(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += "\t";
        out += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    LOG_INFO("%s", out.c_str());
    return 0;
}

// Scripts may only touch game files (relative paths inside base/ or the mod), never the rest
// of the disk: mods are untrusted content.
static bool SafeGamePath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p[0] == '\\' || p.find(':') != std::string::npos) return false;
    for (const std::string& part : {std::string("../"), std::string("..\\")})
        if (p.find(part) != std::string::npos) return false;
    return p != ".." && !EndsWith(p, "/..") && !EndsWith(p, "\\..");
}

static int L_loadfile(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::string text;
    if (!SafeGamePath(path) || !vfs::ReadText(path, text)) {
        lua_pushnil(L);
        lua_pushstring(L, "not found");
        return 2;
    }
    std::string chunk = std::string("@") + path;
    // "t": source only - precompiled bytecode can break out of the sandbox
    if (luaL_loadbufferx(L, text.data(), text.size(), chunk.c_str(), "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

static int L_dofile(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::string text;
    if (!SafeGamePath(path) || !vfs::ReadText(path, text)) return luaL_error(L, "script not found: %s", path);
    std::string chunk = std::string("@") + path;
    if (luaL_loadbufferx(L, text.data(), text.size(), chunk.c_str(), "t") != LUA_OK) return lua_error(L);
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - 1;
}

static int L_register_class(lua_State* L) {
    Script* s = GetScript(L);
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    ClassInfo ci;
    ci.name = name;
    lua_getfield(L, 2, "editor");
    if (lua_istable(L, -1)) {
        int ed = lua_gettop(L);
        ci.desc = OptFieldS(L, ed, "desc", "");
        ci.category = OptFieldS(L, ed, "category", "misc");
        lua_getfield(L, ed, "color");
        ci.color = ToColor(L, -1, ci.color);
        lua_pop(L, 1);
        ci.mins = OptFieldV(L, ed, "mins", ci.mins);
        ci.maxs = OptFieldV(L, ed, "maxs", ci.maxs);
        ci.model = OptFieldS(L, ed, "model", "");
        ci.sprite = OptFieldS(L, ed, "sprite", "");
        ci.brush = OptFieldB(L, ed, "brush", false);
        lua_getfield(L, ed, "props");
        if (lua_istable(L, -1)) ci.props = ToJson(L, -1);
        if (!ci.props.is_object()) ci.props = json::object();
        lua_pop(L, 1);
        lua_getfield(L, ed, "enums");
        if (lua_istable(L, -1)) ci.enums = ToJson(L, -1);
        if (!ci.enums.is_object()) ci.enums = json::object();
        lua_pop(L, 1);
        lua_getfield(L, ed, "help");
        if (lua_istable(L, -1)) ci.help = ToJson(L, -1);
        if (!ci.help.is_object()) ci.help = json::object();
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    // raw lookups: these must not be inherited from the base class through the metatable
    lua_pushstring(L, "base");
    lua_rawget(L, 2);
    if (lua_isstring(L, -1)) ci.base = lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_pushstring(L, "definition");
    lua_rawget(L, 2);
    if (lua_isstring(L, -1)) ci.definition = lua_tostring(L, -1);
    lua_pop(L, 1);
    for (auto& c : s->classes) {
        if (c.name == ci.name) {
            c = ci;
            return 0;
        }
    }
    s->classes.push_back(ci);
    return 0;
}

static int L_register_component(lua_State* L) {
    Script* s = GetScript(L);
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    ClassInfo ci;
    ci.name = name;
    ci.category = "component";
    lua_getfield(L, 2, "editor");
    if (lua_istable(L, -1)) {
        int ed = lua_gettop(L);
        ci.desc = OptFieldS(L, ed, "desc", "");
        ci.category = OptFieldS(L, ed, "category", "component");
        lua_getfield(L, ed, "props");
        if (lua_istable(L, -1)) ci.props = ToJson(L, -1);
        if (!ci.props.is_object()) ci.props = json::object();
        lua_pop(L, 1);
        lua_getfield(L, ed, "enums");
        if (lua_istable(L, -1)) ci.enums = ToJson(L, -1);
        if (!ci.enums.is_object()) ci.enums = json::object();
        lua_pop(L, 1);
        lua_getfield(L, ed, "help");
        if (lua_istable(L, -1)) ci.help = ToJson(L, -1);
        if (!ci.help.is_object()) ci.help = json::object();
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    for (auto& c : s->components) {
        if (c.name == ci.name) {
            c = ci;
            return 0;
        }
    }
    s->components.push_back(ci);
    return 0;
}

static int L_register_weapon(lua_State* L) {
    Script* s = GetScript(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    WeaponDef w;
    w.id = OptFieldS(L, 1, "id", "");
    if (w.id.empty()) return luaL_error(L, "weapon needs an id");
    w.name = OptFieldS(L, 1, "name", w.id);
    w.model = OptFieldS(L, 1, "model", "");
    w.icon = OptFieldS(L, 1, "icon", "");
    w.ammoType = OptFieldS(L, 1, "ammo", "");
    w.slot = (int)OptFieldF(L, 1, "slot", 1);
    w.fireRate = OptFieldF(L, 1, "fire_rate", 0.5f);
    w.ammoPerShot = (int)OptFieldF(L, 1, "ammo_per_shot", 1);
    w.automatic = OptFieldB(L, 1, "automatic", true);
    w.viewOffset = OptFieldV(L, 1, "view_offset", w.viewOffset);
    w.viewRot = OptFieldV(L, 1, "view_rotation", w.viewRot);
    w.viewScale = OptFieldF(L, 1, "view_scale", 1.0f);
    w.recoil = OptFieldF(L, 1, "recoil", 1.0f);
    w.muzzle = OptFieldV(L, 1, "muzzle", w.muzzle);
    w.flashColor = OptFieldV(L, 1, "flash_color", w.flashColor);
    w.flashSize = OptFieldF(L, 1, "flash_size", w.flashSize);
    w.startAmmo = (int)OptFieldF(L, 1, "start_ammo", 0);
    w.definition = OptFieldS(L, 1, "definition", "");
    lua_pushvalue(L, 1);
    w.luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
    for (auto& existing : s->weapons) {
        if (existing.id == w.id) {
            luaL_unref(L, LUA_REGISTRYINDEX, existing.luaRef);
            existing = w;
            return 0;
        }
    }
    s->weapons.push_back(w);
    std::stable_sort(s->weapons.begin(), s->weapons.end(), [](const WeaponDef& a, const WeaponDef& b) { return a.slot < b.slot; });
    return 0;
}

static int L_spawn(lua_State* L) {
    Game* g = RequireGame(L);
    const char* cls = luaL_checkstring(L, 1);
    Vector3 pos = ToVec(L, 2);
    json props = lua_istable(L, 3) ? ToJson(L, 3) : json::object();
    if (!props.is_object()) props = json::object();
    float yaw = (float)luaL_optnumber(L, 4, 0);
    PushEntity(L, g->Spawn(cls, pos, props, yaw));
    return 1;
}

static int L_remove(lua_State* L) {
    Game* g = RequireGame(L);
    if (Entity* e = ToEntity(L, 1)) g->Remove(e);
    return 0;
}

static int L_find(lua_State* L) {
    PushEntityList(L, RequireGame(L)->FindByTargetname(luaL_checkstring(L, 1)));
    return 1;
}
static int L_find_class(lua_State* L) {
    PushEntityList(L, RequireGame(L)->FindByClass(luaL_checkstring(L, 1)));
    return 1;
}
static int L_find_radius(lua_State* L) {
    PushEntityList(L, RequireGame(L)->FindInRadius(ToVec(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}
static int L_get(lua_State* L) {
    PushEntity(L, RequireGame(L)->Find((int)luaL_checkinteger(L, 1)));
    return 1;
}
static int L_player(lua_State* L) {
    PushEntity(L, RequireGame(L)->Player());
    return 1;
}
static int L_time(lua_State* L) {
    Script* s = GetScript(L);
    lua_pushnumber(L, s && s->game ? s->game->Time() : GetTime());
    return 1;
}

static int L_trace(lua_State* L) {
    Game* g = RequireGame(L);
    Vector3 a = ToVec(L, 1), b = ToVec(L, 2);
    Vector3 mins{0, 0, 0}, maxs{0, 0, 0};
    Entity* ignore = nullptr;
    bool ents = true;
    if (lua_istable(L, 3)) {
        mins = OptFieldV(L, 3, "mins", mins);
        maxs = OptFieldV(L, 3, "maxs", maxs);
        ents = OptFieldB(L, 3, "entities", true);
        lua_getfield(L, 3, "ignore");
        ignore = ToEntity(L, -1);
        lua_pop(L, 1);
    } else if (lua_istable(L, 3) == 0 && !lua_isnoneornil(L, 3)) {
        ignore = ToEntity(L, 3);
    }
    TraceResult tr = g->Trace(a, b, mins, maxs, ignore, ents);
    lua_createtable(L, 0, 8);
    lua_pushnumber(L, tr.fraction);
    lua_setfield(L, -2, "fraction");
    PushVec(L, tr.endpos);
    lua_setfield(L, -2, "pos");
    PushVec(L, tr.plane.n);
    lua_setfield(L, -2, "normal");
    lua_pushboolean(L, tr.Hit());
    lua_setfield(L, -2, "hit");
    lua_pushboolean(L, tr.startSolid);
    lua_setfield(L, -2, "startsolid");
    bool sky = false;
    const Map& m = g->GetMap();
    if (tr.brush >= 0 && tr.brush < (int)m.brushes.size() && tr.face >= 0 && tr.face < (int)m.brushes[tr.brush].faces.size())
        sky = m.brushes[tr.brush].faces[tr.face].tex.name == "sky";
    lua_pushboolean(L, sky);
    lua_setfield(L, -2, "sky");
    PushEntity(L, g->Find(tr.entity));
    lua_setfield(L, -2, "entity");
    return 1;
}

static int L_visible(lua_State* L) {
    Game* g = RequireGame(L);
    Entity* a = ToEntity(L, 1);
    Entity* b = ToEntity(L, 2);
    lua_pushboolean(L, a && b && g->Visible(a, b));
    return 1;
}

static int L_damage(lua_State* L) {
    Game* g = RequireGame(L);
    Entity* t = ToEntity(L, 1);
    if (!t) return 0;
    float amount = (float)luaL_checknumber(L, 2);
    Entity* attacker = ToEntity(L, 3);
    Vector3 dir = ToVec(L, 4, {0, 0, 0});
    g->Damage(t, amount, attacker, dir, t->Center());
    return 0;
}

static int L_radius_damage(lua_State* L) {
    Game* g = RequireGame(L);
    g->RadiusDamage(ToVec(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), ToEntity(L, 4), ToEntity(L, 5));
    return 0;
}

static int L_fire_bullets(lua_State* L) {
    Game* g = RequireGame(L);
    Entity* owner = CheckEntity(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    Vector3 origin = owner == g->Player() ? g->EyePos() : owner->Center();
    Vector3 dir = owner == g->Player() ? g->AimDir() : AnglesForward(owner->angles.x, owner->angles.y);
    origin = OptFieldV(L, 2, "origin", origin);
    dir = Norm(OptFieldV(L, 2, "dir", dir));
    int count = (int)OptFieldF(L, 2, "count", 1);
    float spread = OptFieldF(L, 2, "spread", 0.02f);
    float damage = OptFieldF(L, 2, "damage", 10);
    float range = OptFieldF(L, 2, "range", 8192);
    g->FireBullets(owner, origin, dir, count, spread, damage, range);
    return 0;
}

static int L_projectile(lua_State* L) {
    Game* g = RequireGame(L);
    const char* cls = luaL_checkstring(L, 1);
    Entity* owner = ToEntity(L, 2);
    Vector3 origin = ToVec(L, 3, owner ? (owner == g->Player() ? g->EyePos() : owner->Center()) : Vector3{0, 0, 0});
    Vector3 dir = Norm(ToVec(L, 4, owner == g->Player() ? g->AimDir() : Vector3{1, 0, 0}));
    float speed = (float)luaL_optnumber(L, 5, 1000);
    PushEntity(L, g->Projectile(cls, owner, origin, dir, speed));
    return 1;
}

static int L_gib(lua_State* L) {
    Game* g = RequireGame(L);
    Entity* e = CheckEntity(L, 1);
    g->Gib(e, (int)luaL_optinteger(L, 2, 8));
    return 0;
}

static int L_blood(lua_State* L) {
    RequireGame(L)->BloodSpray(ToVec(L, 1), ToVec(L, 2, {0, 1, 0}), (int)luaL_optinteger(L, 3, 12));
    return 0;
}

static int L_explosion(lua_State* L) {
    RequireGame(L)->Explosion(ToVec(L, 1), (float)luaL_optnumber(L, 2, 120), (float)luaL_optnumber(L, 3, 100), ToEntity(L, 4));
    return 0;
}

static int L_particles(lua_State* L) {
    Game* g = RequireGame(L);
    Vector3 pos = ToVec(L, 1), dir = ToVec(L, 2, {0, 1, 0});
    ParticleParams p;
    if (lua_istable(L, 3)) {
        p.count = (int)OptFieldF(L, 3, "count", (float)p.count);
        lua_getfield(L, 3, "color");
        p.color = ToColor(L, -1, p.color);
        lua_pop(L, 1);
        p.speed = OptFieldF(L, 3, "speed", p.speed);
        p.spread = OptFieldF(L, 3, "spread", p.spread);
        p.size = OptFieldF(L, 3, "size", p.size);
        p.sizeVar = OptFieldF(L, 3, "size_var", p.sizeVar);
        p.life = OptFieldF(L, 3, "life", p.life);
        p.gravity = OptFieldF(L, 3, "gravity", p.gravity);
        p.drag = OptFieldF(L, 3, "drag", p.drag);
        p.additive = OptFieldB(L, 3, "additive", p.additive);
        p.collide = OptFieldB(L, 3, "collide", p.collide);
        p.blood = OptFieldB(L, 3, "blood", p.blood);
        p.growth = OptFieldF(L, 3, "growth", p.growth);
        p.texture = OptFieldS(L, 3, "texture", p.texture);
    }
    g->Fx().Emit(pos, dir, p);
    return 0;
}

static int L_light(lua_State* L) {
    Game* g = RequireGame(L);
    Vector3 c = ToVec(L, 2, {1, 1, 1});
    if (c.x > 1.0f || c.y > 1.0f || c.z > 1.0f) c = c / 255.0f;
    g->Fx().AddLight(ToVec(L, 1), c, (float)luaL_optnumber(L, 3, 200), (float)luaL_optnumber(L, 4, 0.1));
    return 0;
}

static int L_decal(lua_State* L) {
    // rbs.decal(from, to, texture, size [, color])
    Game* g = RequireGame(L);
    Vector3 a = ToVec(L, 1), b = ToVec(L, 2);
    const char* tex = luaL_checkstring(L, 3);
    float size = (float)luaL_optnumber(L, 4, 16);
    Color c = ToColor(L, 5, WHITE);
    lua_pushboolean(L, g->DecalTrace(a, b, tex, size, c));
    return 1;
}

static int L_sound(lua_State* L) {
    Script* s = GetScript(L);
    const char* path = luaL_checkstring(L, 1);
    float vol = (float)luaL_optnumber(L, 3, 1.0);
    float pitch = (float)luaL_optnumber(L, 4, 1.0);
    if (s && s->game) {
        bool positional = lua_istable(L, 2);
        s->game->PlaySound(path, ToVec(L, 2), vol, pitch, positional);
    } else {
        Sound snd = assets::GetSound(path);
        if (snd.frameCount) {
            SetSoundVolume(snd, vol);
            SetSoundPitch(snd, pitch);
            PlaySound(snd);
        }
    }
    return 0;
}

static int L_fire_targets(lua_State* L) {
    Game* g = RequireGame(L);
    g->FireTargets(luaL_checkstring(L, 1), ToEntity(L, 2));
    return 0;
}

static int L_message(lua_State* L) {
    RequireGame(L)->Message(luaL_checkstring(L, 1), (float)luaL_optnumber(L, 2, 3));
    return 0;
}

static int L_objective(lua_State* L) {
    RequireGame(L)->SetObjective(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), lua_toboolean(L, 3));
    return 0;
}

static int L_change_map(lua_State* L) {
    RequireGame(L)->ChangeMap(luaL_optstring(L, 1, ""));
    return 0;
}

static int L_show_camera(lua_State* L) {
    RequireGame(L)->ShowCamera(luaL_checkstring(L, 1), (float)luaL_optnumber(L, 2, 4));
    return 0;
}

static int L_give_weapon(lua_State* L) {
    RequireGame(L)->GiveWeapon(luaL_checkstring(L, 1), lua_toboolean(L, 2));
    return 0;
}

static int L_has_weapon(lua_State* L) {
    Game* g = RequireGame(L);
    int idx = g->Scripts().WeaponIndex(luaL_checkstring(L, 1));
    auto& hw = g->PS().hasWeapon;
    lua_pushboolean(L, idx >= 0 && idx < (int)hw.size() && hw[idx]);
    return 1;
}

static int L_give_ammo(lua_State* L) {
    RequireGame(L)->GiveAmmo(luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2));
    return 0;
}

static int L_ammo(lua_State* L) {
    lua_pushinteger(L, RequireGame(L)->Ammo(luaL_checkstring(L, 1)));
    return 1;
}

static int L_max_ammo(lua_State* L) {
    Game* g = RequireGame(L);
    std::string type = luaL_checkstring(L, 1);
    if (lua_isnumber(L, 2)) g->PS().maxAmmo[type] = (int)lua_tointeger(L, 2);
    auto it = g->PS().maxAmmo.find(type);
    lua_pushinteger(L, it == g->PS().maxAmmo.end() ? 999 : it->second);
    return 1;
}

static int L_walk(lua_State* L) {
    Game* g = RequireGame(L);
    g->Walk(CheckEntity(L, 1), ToVec(L, 2), (float)luaL_optnumber(L, 3, 200));
    return 0;
}

static int L_face(lua_State* L) {
    Game* g = RequireGame(L);
    g->AimAt(CheckEntity(L, 1), ToVec(L, 2), (float)luaL_optnumber(L, 3, 360), (float)luaL_optnumber(L, 4, 1));
    return 0;
}

static int L_aim(lua_State* L) {
    Game* g = RequireGame(L);
    PushVec(L, g->EyePos());
    PushVec(L, g->AimDir());
    return 2;
}

static int L_cvar(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    if (!lua_isnoneornil(L, 2)) {
        // scripts are mod content: they may change game variables, not the player's settings
        Cvar* cv = cvars::Find(name);
        if (cv && cv->archive) LOG_WARN("rbs.cvar: '%s' is a player setting, scripts cannot change it", name);
        else cvars::Set(name, luaL_tolstring(L, 2, nullptr));
    }
    Cvar* cv = cvars::Find(name);
    if (!cv) {
        lua_pushnil(L);
        return 1;
    }
    char* end = nullptr;
    double d = strtod(cv->value.c_str(), &end);
    if (end && *end == 0 && !cv->value.empty()) lua_pushnumber(L, d);
    else lua_pushstring(L, cv->value.c_str());
    return 1;
}

static int L_set_key(lua_State* L) {
    RequireGame(L)->PS().keys[luaL_checkstring(L, 1)] = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
    return 0;
}

static int L_has_key(lua_State* L) {
    auto& keys = RequireGame(L)->PS().keys;
    auto it = keys.find(luaL_checkstring(L, 1));
    lua_pushboolean(L, it != keys.end() && it->second);
    return 1;
}

static int L_flash(lua_State* L) {
    Game* g = RequireGame(L);
    g->PS().pickupFlash = (float)luaL_optnumber(L, 1, 0.3);
    return 0;
}

static int L_stats(lua_State* L) {
    Game* g = RequireGame(L);
    GameStats& s = g->Stats();
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, s.kills);
    lua_setfield(L, -2, "kills");
    lua_pushinteger(L, s.monsters);
    lua_setfield(L, -2, "monsters");
    lua_pushinteger(L, s.secrets);
    lua_setfield(L, -2, "secrets");
    lua_pushinteger(L, s.secretsTotal);
    lua_setfield(L, -2, "secrets_total");
    lua_pushinteger(L, s.items);
    lua_setfield(L, -2, "items");
    lua_pushinteger(L, s.itemsTotal);
    lua_setfield(L, -2, "items_total");
    lua_pushnumber(L, s.time);
    lua_setfield(L, -2, "time");
    return 1;
}

static int L_add_stat(lua_State* L) {
    Game* g = RequireGame(L);
    std::string k = luaL_checkstring(L, 1);
    int n = (int)luaL_optinteger(L, 2, 1);
    GameStats& s = g->Stats();
    if (k == "kills") s.kills += n;
    else if (k == "monsters") s.monsters += n;
    else if (k == "secrets") s.secrets += n;
    else if (k == "secrets_total") s.secretsTotal += n;
    else if (k == "items") s.items += n;
    else if (k == "items_total") s.itemsTotal += n;
    return 0;
}

static int L_hud_info(lua_State* L) {
    Game* g = RequireGame(L);
    PlayerState& ps = g->PS();
    Script& sc = g->Scripts();
    lua_createtable(L, 0, 12);
    if (ps.weapon >= 0 && ps.weapon < (int)sc.weapons.size()) {
        const WeaponDef& w = sc.weapons[ps.weapon];
        lua_pushstring(L, w.name.c_str());
        lua_setfield(L, -2, "weapon");
        lua_pushstring(L, w.id.c_str());
        lua_setfield(L, -2, "weapon_id");
        lua_pushinteger(L, w.slot);
        lua_setfield(L, -2, "slot");
        lua_pushstring(L, w.ammoType.c_str());
        lua_setfield(L, -2, "ammo_type");
        bool inf = w.ammoType.empty() || cvars::GetF("g_infinite_ammo") != 0;
        lua_pushboolean(L, inf);
        lua_setfield(L, -2, "infinite");
        lua_pushinteger(L, inf ? -1 : g->Ammo(w.ammoType));
        lua_setfield(L, -2, "ammo");
    }
    lua_pushnumber(L, ps.damageFlash);
    lua_setfield(L, -2, "damage_flash");
    lua_pushboolean(L, g->Player() && g->Player()->dead);
    lua_setfield(L, -2, "dead");
    lua_createtable(L, (int)g->Objectives().size(), 0);
    int i = 1;
    for (auto& o : g->Objectives()) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, o.id.c_str());
        lua_setfield(L, -2, "id");
        lua_pushstring(L, o.text.c_str());
        lua_setfield(L, -2, "text");
        lua_pushboolean(L, o.done);
        lua_setfield(L, -2, "done");
        lua_rawseti(L, -2, i++);
    }
    lua_setfield(L, -2, "objectives");
    lua_createtable(L, 0, 4);
    for (auto& kv : ps.keys) {
        lua_pushboolean(L, kv.second);
        lua_setfield(L, -2, kv.first.c_str());
    }
    lua_setfield(L, -2, "keys");
    return 1;
}

static int L_light_at(lua_State* L) {
    PushVec(L, RequireGame(L)->LightAt(ToVec(L, 1)));
    return 1;
}

static int L_model_bounds(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!*path || !vfs::Exists(path)) return 0;
    ModelAsset& m = assets::GetModel(path);
    PushVec(L, m.bounds.min);
    PushVec(L, m.bounds.max);
    return 2;
}

static int L_add_component(lua_State* L) {
    Game* g = RequireGame(L);
    Entity* e = CheckEntity(L, 1);
    json props = lua_istable(L, 3) ? ToJson(L, 3) : json::object();
    if (!props.is_object()) props = json::object();
    lua_pushboolean(L, g->AddComponent(e, luaL_checkstring(L, 2), props));
    return 1;
}

static int L_read_file(lua_State* L) {
    std::string text;
    const char* path = luaL_checkstring(L, 1);
    if (!SafeGamePath(path) || !vfs::ReadText(path, text)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// rbs.list_files("entities", ".json") -> sorted relative paths from every layer (base + project)
static int L_list_files(lua_State* L) {
    std::string dir = luaL_checkstring(L, 1);
    std::string ext = luaL_optstring(L, 2, "");
    bool recursive = lua_toboolean(L, 3);
    lua_newtable(L);
    if (!SafeGamePath(dir)) return 1;
    int i = 1;
    for (auto& f : vfs::List(dir, ext, recursive)) {
        lua_pushstring(L, f.c_str());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// rbs.json_decode(text) -> value, or nil + error message
static int L_json_decode(lua_State* L) {
    size_t len = 0;
    const char* text = luaL_checklstring(L, 1, &len);
    json j = json::parse(text, text + len, nullptr, false, true);
    if (j.is_discarded()) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid JSON");
        return 2;
    }
    PushJson(L, j);
    return 1;
}

// rbs.json_encode(value) -> text
static int L_json_encode(lua_State* L) {
    json j = ToJson(L, 1);
    lua_pushstring(L, j.dump(2).c_str());
    return 1;
}

// rbs.project() -> the game's manifest (mod.json) as a table ({} for the base game)
static int L_project(lua_State* L) {
    PushJson(L, project::Current().manifest);
    return 1;
}

// ---- 2D drawing (HUD) --------------------------------------------------------------
static int L_screen(lua_State* L) {
    lua_pushinteger(L, GetScreenWidth());
    lua_pushinteger(L, GetScreenHeight());
    return 2;
}

// rbs.draw_text(text, x, y, size, color, align, shadow, font)
static int L_draw_text(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    float size = (float)luaL_optnumber(L, 4, 20);
    Color c = ToColor(L, 5, WHITE);
    const char* align = luaL_optstring(L, 6, "left");
    Font f = fonts::Get(luaL_optstring(L, 8, "hud"), (int)size);
    float spacing = 0;
    Vector2 m = MeasureTextEx(f, text, size, spacing);
    if (strcmp(align, "center") == 0) x -= m.x * 0.5f;
    else if (strcmp(align, "right") == 0) x -= m.x;
    if (lua_toboolean(L, 7)) DrawTextEx(f, text, {x + 2, y + 2}, size, spacing, Color{0, 0, 0, (unsigned char)(c.a * 0.8f)});
    DrawTextEx(f, text, {x, y}, size, spacing, c);
    return 0;
}

static int L_text_width(lua_State* L) {
    float size = (float)luaL_optnumber(L, 2, 20);
    Font f = fonts::Get(luaL_optstring(L, 3, "hud"), (int)size);
    lua_pushnumber(L, MeasureTextEx(f, luaL_checkstring(L, 1), size, 0).x);
    return 1;
}

static int L_draw_rect(lua_State* L) {
    Rectangle r{(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)};
    Color c = ToColor(L, 5, WHITE);
    if (lua_toboolean(L, 6)) DrawRectangleLinesEx(r, (float)luaL_optnumber(L, 7, 1), c);
    else DrawRectangleRec(r, c);
    return 0;
}

static int L_draw_image(lua_State* L) {
    Texture2D t = assets::GetTexturePath(luaL_checkstring(L, 1));
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_optnumber(L, 4, t.width), h = (float)luaL_optnumber(L, 5, t.height);
    Color c = ToColor(L, 6, WHITE);
    Rectangle src{0, 0, (float)t.width, (float)t.height};
    if (lua_istable(L, 7)) {
        src.x = OptFieldF(L, 7, "x", 0);
        src.y = OptFieldF(L, 7, "y", 0);
        src.width = OptFieldF(L, 7, "w", (float)t.width);
        src.height = OptFieldF(L, 7, "h", (float)t.height);
    }
    DrawTexturePro(t, src, {x, y, w, h}, {0, 0}, 0, c);
    return 0;
}

static int L_image_size(lua_State* L) {
    Texture2D t = assets::GetTexturePath(luaL_checkstring(L, 1));
    lua_pushinteger(L, t.width);
    lua_pushinteger(L, t.height);
    return 2;
}

// rbs.tr("New game") -> text in the language chosen in Options (lang/<code>.json)
static int L_tr(lua_State* L) {
    lua_pushstring(L, Tr(luaL_checkstring(L, 1)));
    return 1;
}

static const luaL_Reg kApi[] = {
    {"log", L_log}, {"_loadfile", L_loadfile}, {"dofile", L_dofile}, {"_register_class", L_register_class},
    {"_register_weapon", L_register_weapon}, {"_register_component", L_register_component}, {"spawn", L_spawn}, {"remove", L_remove}, {"find", L_find},
    {"find_class", L_find_class}, {"find_radius", L_find_radius}, {"get", L_get}, {"player", L_player},
    {"time", L_time}, {"trace", L_trace}, {"visible", L_visible}, {"damage", L_damage},
    {"radius_damage", L_radius_damage}, {"fire_bullets", L_fire_bullets}, {"projectile", L_projectile},
    {"gib", L_gib}, {"blood", L_blood}, {"explosion", L_explosion}, {"particles", L_particles}, {"light", L_light},
    {"decal", L_decal}, {"sound", L_sound}, {"fire_targets", L_fire_targets}, {"message", L_message},
    {"objective", L_objective}, {"change_map", L_change_map}, {"show_camera", L_show_camera},
    {"give_weapon", L_give_weapon}, {"has_weapon", L_has_weapon}, {"give_ammo", L_give_ammo}, {"ammo", L_ammo},
    {"max_ammo", L_max_ammo}, {"walk", L_walk}, {"face", L_face}, {"aim", L_aim}, {"cvar", L_cvar},
    {"set_key", L_set_key}, {"has_key", L_has_key}, {"flash", L_flash}, {"stats", L_stats}, {"add_stat", L_add_stat},
    {"hud_info", L_hud_info}, {"light_at", L_light_at}, {"read_file", L_read_file},
    {"model_bounds", L_model_bounds}, {"add_component", L_add_component}, {"screen", L_screen},
    {"draw_text", L_draw_text}, {"text_width", L_text_width}, {"draw_rect", L_draw_rect},
    {"draw_image", L_draw_image}, {"image_size", L_image_size}, {"tr", L_tr}, {"list_files", L_list_files},
    {"json_decode", L_json_decode}, {"json_encode", L_json_encode}, {"project", L_project}, {nullptr, nullptr}};

// ---------------------------------------------------------------------------------
// Script class
// ---------------------------------------------------------------------------------
static int Traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object)", 1);
    return 1;
}

Script::~Script() { Shutdown(); }

void Script::Shutdown() {
    if (L) lua_close(L);
    L = nullptr;
    weapons.clear();
    classes.clear();
    components.clear();
    m_hudBroken = false;
}

bool Script::CallFunction(int nargs, const char* what) {
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, Traceback);
    lua_insert(L, base);
    int r = lua_pcall(L, nargs, 1, base);
    lua_remove(L, base);
    if (r != LUA_OK) {
        LOG_ERR("Lua error (%s): %s", what, lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_pushnil(L);
        return false;
    }
    return true;
}

bool Script::Init(Game* g) {
    Shutdown();
    game = g;
    L = luaL_newstate();
    // safe subset of the standard library
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);
    lua_pop(L, 8);
    // strip dangerous os functions, keep clock/time/date
    lua_getglobal(L, "os");
    for (const char* f : {"execute", "remove", "rename", "exit", "tmpname", "getenv", "setlocale"}) {
        lua_pushnil(L);
        lua_setfield(L, -2, f);
    }
    lua_pop(L, 1);
    for (const char* f : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, f);
    }
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "rbs_script");

    luaL_newlib(L, kApi);
    lua_setglobal(L, "rbs");
    lua_pushcfunction(L, L_log);
    lua_setglobal(L, "print");

    // entity metatable
    lua_newtable(L);
    lua_pushcfunction(L, EntityIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, EntityNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, EntityToString);
    lua_setfield(L, -2, "__tostring");
    lua_setfield(L, LUA_REGISTRYINDEX, "rbs_entity_meta");

    if (luaL_loadbuffer(L, kPrelude, strlen(kPrelude), "=prelude") != LUA_OK || !CallFunction(0, "prelude")) {
        LOG_ERR("Lua prelude failed");
        return false;
    }
    lua_pop(L, 1);
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, "_vec3");
    lua_setfield(L, LUA_REGISTRYINDEX, "rbs_vec3");
    lua_pop(L, 1);

    std::string text;
    if (!vfs::ReadText("scripts/init.lua", text)) {
        LOG_ERR("scripts/init.lua not found");
        return false;
    }
    if (luaL_loadbufferx(L, text.data(), text.size(), "@scripts/init.lua", "t") != LUA_OK) {
        LOG_ERR("Lua: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    bool ok = CallFunction(0, "scripts/init.lua");
    lua_pop(L, 1);
    // additive scripts from every layer (base + active mod): scripts/autorun/*.lua
    for (auto& rel : vfs::List("scripts/autorun", ".lua")) {
        std::string code;
        if (!vfs::ReadText(rel, code)) continue;
        std::string chunk = "@" + rel;
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) {
            LOG_ERR("Lua: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            continue;
        }
        CallFunction(0, rel.c_str());
        lua_pop(L, 1);
    }
    // enemy/item types and weapons made without code (entities/*.json, weapons/*.json)
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, "_load_definitions");
    lua_remove(L, -2);
    if (lua_isfunction(L, -1)) {
        CallFunction(0, "definitions");
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    LOG_INFO("Scripts loaded: %d entity classes, %d weapons", (int)classes.size(), (int)weapons.size());
    return ok;
}

void Script::RunString(const std::string& code) {
    if (!L) return;
    if (luaL_loadbufferx(L, code.data(), code.size(), "=console", "t") != LUA_OK) {
        LOG_ERR("Lua: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    if (CallFunction(0, "console") && !lua_isnil(L, -1)) LOG_INFO("%s", luaL_tolstring(L, -1, nullptr)), lua_pop(L, 1);
    lua_pop(L, 1);
}

void Script::RunMapScript(const std::string& mapRelPath) {
    std::string path = mapRelPath;
    if (EndsWith(path, ".json")) path = path.substr(0, path.size() - 5);
    path += ".lua";
    std::string text;
    if (!vfs::ReadText(path, text)) return;
    std::string chunk = "@" + path;
    if (luaL_loadbufferx(L, text.data(), text.size(), chunk.c_str(), "t") != LUA_OK) {
        LOG_ERR("Lua: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    CallFunction(0, path.c_str());
    lua_pop(L, 1);
    LOG_INFO("Map script %s loaded", path.c_str());
}

bool Script::HasClass(const std::string& name) const {
    for (auto& c : classes)
        if (c.name == name) return true;
    return false;
}

const ClassInfo* Script::FindClass(const std::string& name) const {
    for (auto& c : classes)
        if (c.name == name) return &c;
    return nullptr;
}

bool Script::HasComponent(const std::string& name) const { return FindComponent(name) != nullptr; }

const ClassInfo* Script::FindComponent(const std::string& name) const {
    for (auto& c : components)
        if (c.name == name) return &c;
    return nullptr;
}

void Script::CreateComponent(Entity* e, Component& c) {
    if (!L) return;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, "_new_component");
    lua_remove(L, -2);
    lua_pushstring(L, c.type.c_str());
    PushEntity(L, e);
    PushJson(L, c.props);
    if (!CallFunction(3, "component create") || !lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    c.luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

void Script::ReleaseComponent(Component& c) {
    if (L && c.luaRef >= 0) luaL_unref(L, LUA_REGISTRYINDEX, c.luaRef);
    c.luaRef = LUA_NOREF;
}

bool Script::PushCompCallback(Entity* e, Component& c, const char* name) {
    if (!L || c.luaRef < 0) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c.luaRef);
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    lua_insert(L, -2);  // fn, instance
    PushEntity(L, e);
    return true;
}

void Script::CompStart(Entity* e, Component& c) {
    if (!PushCompCallback(e, c, "start")) return;
    std::string what = c.type + ":start";
    CallFunction(2, what.c_str());
    lua_pop(L, 1);
}

void Script::CompUpdate(Entity* e, Component& c, float dt) {
    if (!PushCompCallback(e, c, "update")) return;
    lua_pushnumber(L, dt);
    std::string what = c.type + ":update";
    if (!CallFunction(3, what.c_str())) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, c.luaRef);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "update");  // shadow the class method on this instance only
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void Script::CompTouch(Entity* e, Component& c, Entity* other, Vector3 normal) {
    if (!PushCompCallback(e, c, "touch")) return;
    PushEntity(L, other);
    PushVec(L, normal);
    CallFunction(4, "component touch");
    lua_pop(L, 1);
}

void Script::CompUse(Entity* e, Component& c, Entity* activator) {
    if (!PushCompCallback(e, c, "use")) return;
    PushEntity(L, activator);
    CallFunction(3, "component use");
    lua_pop(L, 1);
}

float Script::CompDamaged(Entity* e, Component& c, float amount, Entity* attacker, Vector3 dir) {
    if (!PushCompCallback(e, c, "damaged")) return amount;
    lua_pushnumber(L, amount);
    PushEntity(L, attacker);
    PushVec(L, dir);
    CallFunction(5, "component damaged");
    if (lua_isnumber(L, -1)) amount = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return amount;
}

void Script::CompPain(Entity* e, Component& c, float amount, Entity* attacker) {
    if (!PushCompCallback(e, c, "pain")) return;
    lua_pushnumber(L, amount);
    PushEntity(L, attacker);
    CallFunction(4, "component pain");
    lua_pop(L, 1);
}

void Script::CompDie(Entity* e, Component& c, Entity* attacker, float amount) {
    if (!PushCompCallback(e, c, "die")) return;
    PushEntity(L, attacker);
    lua_pushnumber(L, amount);
    CallFunction(4, "component die");
    lua_pop(L, 1);
}

void Script::CompBlocked(Entity* e, Component& c, Entity* other) {
    if (!PushCompCallback(e, c, "blocked")) return;
    PushEntity(L, other);
    CallFunction(3, "component blocked");
    lua_pop(L, 1);
}

json Script::WeaponData(const std::string& id) const {
    json out = json::object();
    if (!L) return out;
    int idx = WeaponIndex(id);
    if (idx < 0) return out;
    lua_rawgeti(L, LUA_REGISTRYINDEX, weapons[idx].luaRef);
    if (lua_istable(L, -1)) {
        json j = ToJson(L, -1);
        if (j.is_object())
            for (auto it = j.begin(); it != j.end(); ++it)
                if (!it.value().is_null() && it.key() != "definition" && it.key() != "id") out[it.key()] = it.value();  // functions come out as null
    }
    lua_pop(L, 1);
    return out;
}

json Script::LuaJson(const char* field) const {
    if (!L) return nullptr;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, field);
    json j = lua_istable(L, -1) ? ToJson(L, -1) : json();
    lua_pop(L, 2);
    return j;
}

bool Script::IsA(const std::string& cls, const std::string& base) const {
    std::string c = cls;
    for (int depth = 0; depth < 32 && !c.empty(); ++depth) {
        if (c == base) return true;
        const ClassInfo* ci = FindClass(c);
        c = ci ? ci->base : "";
    }
    return false;
}

int Script::WeaponIndex(const std::string& id) const {
    for (size_t i = 0; i < weapons.size(); ++i)
        if (weapons[i].id == id) return (int)i;
    return -1;
}

void Script::CreateEntityTable(Entity* e) {
    if (!L) return;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, e->id);
    lua_setfield(L, -2, "__id");
    lua_pushstring(L, e->classname.c_str());
    lua_setfield(L, -2, "__classname");
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, "classes");
    lua_getfield(L, -1, e->classname.c_str());
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -1);
        e->classRef = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_setfield(L, -4, "__class");
    } else {
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, "rbs_entity_meta");
    lua_setmetatable(L, -2);
    e->luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

void Script::ReleaseEntity(Entity* e) {
    if (!L) return;
    if (e->luaRef >= 0) luaL_unref(L, LUA_REGISTRYINDEX, e->luaRef);
    if (e->classRef >= 0) luaL_unref(L, LUA_REGISTRYINDEX, e->classRef);
    e->luaRef = e->classRef = LUA_NOREF;
}

bool Script::PushCallback(Entity* e, const char* name) {
    if (!L || e->classRef < 0 || e->luaRef < 0) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, e->classRef);
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, e->luaRef);
    return true;
}

void Script::OnSpawn(Entity* e) {
    if (!PushCallback(e, "spawn")) return;
    std::string what = e->classname + ":spawn";
    CallFunction(1, what.c_str());
    lua_pop(L, 1);
}

void Script::OnUpdate(Entity* e, float dt) {
    if (!PushCallback(e, "update")) return;
    lua_pushnumber(L, dt);
    std::string what = e->classname + ":update";
    if (!CallFunction(2, what.c_str())) {
        // stop calling a broken update every frame
        lua_rawgeti(L, LUA_REGISTRYINDEX, e->classRef);
        lua_pushnil(L);
        lua_setfield(L, -2, "update");
        lua_pop(L, 1);
        LOG_WARN("%s:update disabled after error", e->classname.c_str());
    }
    lua_pop(L, 1);
}

void Script::OnTouch(Entity* e, Entity* other, Vector3 normal) {
    if (!PushCallback(e, "touch")) return;
    PushEntity(L, other);
    PushVec(L, normal);
    std::string what = e->classname + ":touch";
    CallFunction(3, what.c_str());
    lua_pop(L, 1);
}

void Script::OnUse(Entity* e, Entity* activator) {
    if (!PushCallback(e, "use")) return;
    PushEntity(L, activator);
    std::string what = e->classname + ":use";
    CallFunction(2, what.c_str());
    lua_pop(L, 1);
}

float Script::OnDamage(Entity* e, float amount, Entity* attacker, Vector3 dir) {
    if (!PushCallback(e, "damaged")) return amount;
    lua_pushnumber(L, amount);
    PushEntity(L, attacker);
    PushVec(L, dir);
    std::string what = e->classname + ":damaged";
    CallFunction(4, what.c_str());
    if (lua_isnumber(L, -1)) amount = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return amount;
}

void Script::OnPain(Entity* e, float amount, Entity* attacker) {
    if (!PushCallback(e, "pain")) return;
    lua_pushnumber(L, amount);
    PushEntity(L, attacker);
    std::string what = e->classname + ":pain";
    CallFunction(3, what.c_str());
    lua_pop(L, 1);
}

void Script::OnDie(Entity* e, Entity* attacker, float amount) {
    if (!PushCallback(e, "die")) return;
    PushEntity(L, attacker);
    lua_pushnumber(L, amount);
    std::string what = e->classname + ":die";
    CallFunction(3, what.c_str());
    lua_pop(L, 1);
}

void Script::OnBlocked(Entity* e, Entity* other) {
    if (!PushCallback(e, "blocked")) return;
    PushEntity(L, other);
    std::string what = e->classname + ":blocked";
    CallFunction(2, what.c_str());
    lua_pop(L, 1);
}

void Script::FireWeapon(int index, Entity* owner) {
    if (!L || index < 0 || index >= (int)weapons.size()) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, weapons[index].luaRef);
    lua_getfield(L, -1, "fire");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return;
    }
    lua_insert(L, -2);  // fire, def
    PushEntity(L, owner);
    lua_insert(L, -2);  // fire, owner, def
    std::string what = "weapon " + weapons[index].id + ":fire";
    CallFunction(2, what.c_str());
    lua_pop(L, 1);
}

void Script::CallHook(const char* name) {
    if (!L) return;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    CallFunction(0, name);
    lua_pop(L, 1);
}

void Script::CallHookNumber(const char* name, float value) {
    if (!L) return;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushnumber(L, value);
    CallFunction(1, name);
    lua_pop(L, 1);
}

void Script::CallHookEntity(const char* name, Entity* e) {
    if (!L) return;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    PushEntity(L, e);
    CallFunction(1, name);
    lua_pop(L, 1);
}

void Script::FireTargetHandlers(const std::string& target, Entity* activator) {
    if (!L) return;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, "_fire");
    lua_remove(L, -2);
    lua_pushstring(L, target.c_str());
    PushEntity(L, activator);
    CallFunction(2, "rbs.on handler");
    lua_pop(L, 1);
}

bool Script::DrawHud(int w, int h) {
    if (!L || m_hudBroken) return false;
    lua_getglobal(L, "rbs");
    lua_getfield(L, -1, "draw_hud");
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    bool ok = CallFunction(2, "rbs.draw_hud");
    lua_pop(L, 1);
    if (!ok) {
        m_hudBroken = true;  // fall back to the built-in HUD instead of spamming errors
        LOG_WARN("rbs.draw_hud disabled after error (reload scripts to retry)");
    }
    return ok;
}

}  // namespace rbs
