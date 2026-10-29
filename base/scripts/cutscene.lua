-- Cutscenes: a scene made of camera shots, character movement, animations, sounds, fades
-- and titles, built in the editor as a graph (Project > Cutscenes, cutscenes/<name>.json).
-- It runs on the same flow engine as quests and dialogues (scripts/quests.lua): parallel
-- branches from one port, Join, Delay, Branch, Set variable, Fire targets, Dialogue... work in it
-- too; the nodes below are the ones only a cutscene has. See docs/CUTSCENES.md.
--
-- While a cutscene runs the player is frozen (and by default cannot be hurt), the HUD is hidden
-- and black bars frame the picture. Pressing Space / Enter twice skips the scene: every waiting
-- node finishes at once and the characters end up where the scene would have put them.
--
-- Targets ("who", "to", "look_at") are the target name of an entity in the map, `player`, or a
-- point written "x y z".

local flow = rbs.flow
local view = flow.view
local cut = {}
flow.cut = cut

cut.defaults = {
  letterbox = true,       -- black bars top and bottom
  skippable = true,       -- Space / Enter twice skips it
  hide_hud = true,        -- no health / ammo / quests while it runs
  invulnerable = true,    -- the player cannot be hurt (monsters keep going)
}
rbs.cutscene_defaults = {style = cut.defaults}

cut.fade = 0              -- 0 = clear picture, 1 = black
cut.cam = nil             -- the last camera the scene set {pos, look, fov}: where a move starts from

function cut.style_of(g)
  local s = {}
  for k, v in pairs(cut.defaults) do s[k] = v end
  for k, v in pairs(g and g.style or {}) do
    if type(v) == type(cut.defaults[k]) then s[k] = v end
  end
  return s
end

-- ---------------------------------------------------------------------------------
-- targets
-- ---------------------------------------------------------------------------------
-- env.entities (the editor): {targetname = {pos = {x, y, z}, yaw, mins = {..}, maxs = {..}, model, scale}}
-- stands in for the map's entities, which do not exist there.
local function resolve(spec, env)
  spec = tostring(spec or "")
  if spec == "" then return nil end
  local x, y, z = spec:match("^%s*(%-?[%d%.]+)%s+(%-?[%d%.]+)%s+(%-?[%d%.]+)%s*$")
  if x then return {point = vec(tonumber(x), tonumber(y), tonumber(z))} end
  if env then
    local info = env.entities and env.entities[spec]
    if info then return {info = info} end
    if spec == "player" and env.player then return {info = env.player} end
    return nil
  end
  if spec == "player" then
    local p = rbs.player()
    return p and {ent = p} or nil
  end
  local e = rbs.find(spec)[1]
  return e and {ent = e} or nil
end
cut.resolve = resolve

-- the body measurements the shots work with, whatever the target is
local function subject_of(target)
  if not target then return nil end
  if target.ent then return view.subject(target.ent, nil) end
  if target.point then return {feet = target.point, height = 64, head = target.point + vec(0, 57, 0), yaw = 0} end
  local i = target.info
  local base = i.mins and i.mins[2] or 0
  local height = (i.maxs and i.mins) and (i.maxs[2] - i.mins[2]) or 64
  if i.model and i.model ~= "" then
    local mn, mx = rbs.model_bounds(i.model)
    if mn then height = (mx.y - mn.y) * (tonumber(i.scale) or 1) end
  end
  local feet = vec(i.pos[1], i.pos[2] + base, i.pos[3])
  return {feet = feet, height = height, head = feet + vec(0, height * 0.9, 0), yaw = i.yaw or 0}
end

local function position_of(target)
  if not target then return nil end
  if target.point then return target.point end
  if target.ent then return target.ent.pos end
  return vec(target.info.pos[1], target.info.pos[2], target.info.pos[3])
end

local function look_point(target)
  local s = subject_of(target)
  if not s then return nil end
  if target.point then return target.point end
  return s.feet + vec(0, s.height * 0.75, 0)
end

local function set_cam(pos, look, fov, rate)
  cut.cam = {pos = pos, look = look, fov = fov}
  rbs.camera_set(pos, look, fov, rate)
end

-- the shot of a "Camera shot" node at time t
local function shot_of(p, t, env)
  local target = resolve(p.who, env)
  local subject = subject_of(target)
  if not subject then return nil end
  local other
  if env then other = subject_of({info = env.player}) else other = subject_of(resolve("player")) end
  local pos, look, fov = view.shot(p.shot, p.motion, t, subject, other)
  return pos, look, fov
end

-- where a "Camera move" node ends: position, look-at point, fov (nil = keep)
local function move_end(p, env)
  local to = resolve(p.to, env)
  local pos = position_of(to)
  if not pos then return nil end
  local look
  if p.look_at ~= "" then
    look = look_point(resolve(p.look_at, env))
  elseif to.ent or to.info then
    local yaw = to.ent and to.ent.yaw or to.info.yaw or 0
    look = pos + vec.from_yaw(yaw) * 500
  end
  return pos, look, (tonumber(p.fov) or 0) > 0 and p.fov or nil
end

-- ---------------------------------------------------------------------------------
-- running a cutscene
-- ---------------------------------------------------------------------------------
function flow.run_cutscene(g, name)
  if flow.cutscene and not flow.cutscene.done then return false end
  local inst = flow.new_instance("cutscene", g, name)
  inst.style = cut.style_of(g)
  inst.t = 0
  flow.cutscene = inst
  rbs.freeze(true)
  local pl = rbs.player()
  if pl then
    inst.prev_takedamage = pl.takedamage
    if inst.style.invulnerable then pl.takedamage = false end
  end
  local pos, look, fov = rbs.camera_get()
  cut.cam = {pos = pos, look = look, fov = fov}
  flow.cinematic = inst.style.hide_hud
  for _, s in ipairs(g.starts) do flow.enter(inst, s) end
  if not inst.done and next(inst.active) == nil then flow.finish(inst, "success") end
  return true
end

function flow.start_cutscene(name)
  name = name:match("([^/]+)%.json$") or name
  if flow.cutscene and not flow.cutscene.done then return false end
  local g = flow.load("cutscene", name)
  if not g then return false end
  return flow.run_cutscene(g, name)
end

-- Skips to the end: every waiting node finishes at once (and puts things where it would have).
function cut.skip(inst)
  if not inst or inst.done or inst.skipping then return end
  inst.skipping = true
  local guard = 0
  while not inst.done and next(inst.active) and guard < 2000 do
    guard = guard + 1
    local ids = {}
    for id in pairs(inst.active) do ids[#ids + 1] = id end
    for _, id in ipairs(ids) do
      local st, node = inst.active[id], inst.nodes[id]
      local impl = node and flow.impl[node.type]
      if st and impl then
        if impl.skip then pcall(impl.skip, inst, node, st) end
        flow.leave(inst, node, "next")
      end
    end
  end
  if not inst.done then flow.finish(inst, "success") end
end

-- called when the scene ended (or was skipped): the game belongs to the player again
function cut.on_finish(inst)
  rbs.camera_clear(4)
  rbs.freeze(flow.dialogue ~= nil)
  local pl = rbs.player()
  if pl and inst.prev_takedamage ~= nil then pl.takedamage = inst.prev_takedamage end
  flow.cinematic = false
  cut.cam = nil
end

function cut.update(inst, dt)
  inst.t = inst.t + dt
  if inst.done or not inst.style.skippable or flow.dialogue then return end
  if rbs.key_pressed("space") or rbs.key_pressed("enter") then
    if inst.skip_armed and rbs.time() < inst.skip_armed then cut.skip(inst)
    else inst.skip_armed = rbs.time() + 2.0 end
  end
end

-- fades out what a scene left behind (a fade to black that was not undone)
function cut.idle(dt)
  if not flow.cutscene and cut.fade > 0 then cut.fade = math.max(0, cut.fade - dt * 2) end
end

-- ---------------------------------------------------------------------------------
-- drawing: bars, fade, titles
-- ---------------------------------------------------------------------------------
function cut.draw(w, h)
  local inst = flow.cutscene
  if inst and inst.style.letterbox then
    local bar = h * 0.11
    rbs.draw_rect(0, 0, w, bar, {r = 0, g = 0, b = 0, a = 255})
    rbs.draw_rect(0, h - bar, w, bar, {r = 0, g = 0, b = 0, a = 255})
  end
  if cut.fade > 0 then rbs.draw_rect(0, 0, w, h, {r = 0, g = 0, b = 0, a = math.floor(255 * math.min(1, cut.fade))}) end
  if not inst then return end
  local s = h / 900
  for id, st in pairs(inst.active) do
    local n = inst.nodes[id]
    if n and n.type == "cs_text" then
      local secs = math.max(0.1, n.params.seconds)
      local a = math.min(1, st.t / 0.4, (secs - st.t) / 0.4)
      if a > 0 then
        local size = math.floor((tonumber(n.params.size) or 40) * s)
        local y = n.params.position == "bottom" and h * 0.80 or h * 0.45
        rbs.draw_text(flow.say_text(n.params.text), w / 2, y, size, {r = 240, g = 236, b = 225, a = math.floor(255 * a)}, "center", true, "ui_bold")
      end
    end
  end
  if inst.skip_armed and rbs.time() < inst.skip_armed and inst.style.skippable then
    rbs.draw_text("Press Space again to skip", w - 20 * s, h - h * 0.11 - 28 * s, math.floor(16 * s), {r = 190, g = 196, b = 208, a = 255}, "right", true, "ui")
  end
end

-- ---------------------------------------------------------------------------------
-- nodes
-- ---------------------------------------------------------------------------------
local CUT = {"cutscene"}
local function node(def) rbs.flow_node(def) end

local function without(list, ...)
  local drop, out = {}, {}
  for _, v in ipairs({...}) do drop[v] = true end
  for _, v in ipairs(list) do if not drop[v] then out[#out + 1] = v end end
  return out
end

node({id = "cs_camera", category = "cutscene", title = "Camera shot", scopes = CUT,
      desc = "Films a character or a point with a shot (the same shots as in dialogues) for some seconds. 0 seconds sets the shot and goes on at once.",
      params = {
        {name = "who", type = "entity", default = "player", help = "the character (target name), player, or a point x y z"},
        {name = "shot", type = "enum", values = without(view.shots, "auto", "none"), default = "medium"},
        {name = "motion", type = "enum", values = without(view.motions, "auto"), default = "push-in"},
        {name = "seconds", type = "number", default = 3},
        {name = "blend", type = "number", default = 4, help = "how fast the camera moves to this shot (0 = a cut)"},
      },
      enter = function(inst, n, st)
        st.t = 0
        local pos, look, fov = shot_of(n.params, 0)
        if not pos then rbs.log("[cutscene " .. inst.name .. "] camera: no such target '" .. tostring(n.params.who) .. "'"); return "next" end
        set_cam(pos, look, fov, n.params.blend)
        if n.params.seconds <= 0 then return "next" end
      end,
      update = function(inst, n, st, dt)
        st.t = st.t + dt
        local pos, look, fov = shot_of(n.params, st.t)
        if pos then set_cam(pos, look, fov, n.params.blend) end
        if st.t >= n.params.seconds then return "next" end
      end})

node({id = "cs_camera_move", category = "cutscene", title = "Camera move", scopes = CUT,
      desc = "Moves the camera from where it is to another place (a point x y z, or a named entity such as a mission_camera, whose direction it takes when Look at is empty).",
      params = {
        {name = "to", type = "entity", default = "0 100 0"},
        {name = "look_at", type = "entity", default = "player", help = "what the camera ends up looking at (empty = the way the target entity faces)"},
        {name = "fov", type = "number", default = 0, help = "field of view at the end (0 = unchanged)"},
        {name = "seconds", type = "number", default = 3},
        {name = "ease", type = "enum", values = {"smooth", "linear"}, default = "smooth"},
      },
      enter = function(inst, n, st)
        st.t = 0
        st.from = cut.cam
        local pos = move_end(n.params)
        if not pos then rbs.log("[cutscene " .. inst.name .. "] camera move: no such target '" .. tostring(n.params.to) .. "'"); return "next" end
        if n.params.seconds <= 0 then return "next" end
      end,
      update = function(inst, n, st, dt)
        st.t = st.t + dt
        local pos, look, fov = move_end(n.params)
        local from = st.from
        if not pos or not from then return "next" end
        local p = math.min(1, st.t / math.max(0.01, n.params.seconds))
        local e = n.params.ease == "linear" and p or p * p * (3 - 2 * p)
        local lk = look or from.look
        set_cam(from.pos:lerp(pos, e), from.look:lerp(lk, e), fov and (from.fov + (fov - from.fov) * e) or from.fov, 0)
        if p >= 1 then return "next" end
      end,
      skip = function(inst, n)
        local pos, look, fov = move_end(n.params)
        if pos and cut.cam then set_cam(pos, look or cut.cam.look, fov or cut.cam.fov, 0) end
      end})

node({id = "cs_animate", category = "cutscene", title = "Animate", scopes = CUT,
      desc = "Plays an animation on a character (a trigger of its animator, or an animation of its model).",
      params = {{name = "who", type = "entity", default = ""}, {name = "animation", type = "string", default = "attack"}},
      enter = function(inst, n)
        local t = resolve(n.params.who)
        if t and t.ent and n.params.animation ~= "" then t.ent:play(n.params.animation, false) end
        return "next"
      end})

node({id = "cs_move", category = "cutscene", title = "Move character", scopes = CUT,
      desc = "Walks a character in a straight line to a point or an entity and goes on when it arrives. (The movement ignores walls: place the path so it does not need them.)",
      params = {
        {name = "who", type = "entity", default = ""},
        {name = "to", type = "entity", default = "", help = "a point x y z or the target name of an entity"},
        {name = "speed", type = "number", default = 120, help = "units per second"},
        {name = "face", type = "bool", default = true, help = "turns the character the way it walks"},
      },
      enter = function(inst, n, st)
        local who, to = resolve(n.params.who), resolve(n.params.to)
        if not (who and who.ent and to) then rbs.log("[cutscene " .. inst.name .. "] move: unknown character or destination"); return "next" end
        st.ent = who.ent
        local dest = position_of(to)
        -- the character keeps its height above the ground
        st.dest = vec(dest.x, who.ent.pos.y, dest.z)
        who.ent:play("walk", true)
      end,
      update = function(inst, n, st, dt)
        local ent = st.ent
        if not ent or ent.removed then return "next" end
        local to = st.dest - ent.pos
        local dist = to:len()
        local step = n.params.speed * dt
        if n.params.face then rbs.face(ent, st.dest, 720, dt) end
        if dist <= step then
          ent.pos = st.dest
          ent:play("idle", true)
          return "next"
        end
        ent.pos = ent.pos + to * (step / dist)
      end,
      skip = function(inst, n, st)
        if st.ent and not st.ent.removed and st.dest then
          st.ent.pos = st.dest
          st.ent:play("idle", true)
        end
      end})

node({id = "cs_fade", category = "cutscene", title = "Fade", scopes = CUT,
      desc = "Fades the picture to black or back. A fade to black that is still there when the scene ends fades out by itself.",
      params = {
        {name = "to", type = "enum", values = {"black", "clear"}, default = "black"},
        {name = "seconds", type = "number", default = 1},
      },
      enter = function(inst, n, st)
        st.t, st.from, st.target = 0, cut.fade, n.params.to == "black" and 1 or 0
        if n.params.seconds <= 0 then cut.fade = st.target; return "next" end
      end,
      update = function(inst, n, st, dt)
        st.t = st.t + dt
        local p = math.min(1, st.t / n.params.seconds)
        cut.fade = st.from + (st.target - st.from) * p
        if p >= 1 then return "next" end
      end,
      skip = function(inst, n, st) cut.fade = st.target or cut.fade end})

node({id = "cs_text", category = "cutscene", title = "Title text", scopes = CUT,
      desc = "Shows a text on the screen (fades in and out) for some seconds.",
      params = {
        {name = "text", type = "text", default = ""},
        {name = "seconds", type = "number", default = 3},
        {name = "position", type = "enum", values = {"center", "bottom"}, default = "center"},
        {name = "size", type = "number", default = 40},
      },
      enter = function(inst, n, st) st.t = 0 end,
      update = function(inst, n, st, dt)
        st.t = st.t + dt
        if st.t >= n.params.seconds then return "next" end
      end})

-- starting a cutscene from a quest or a dialogue: the flow goes on when it ended
node({id = "cutscene", category = "action", title = "Cutscene", scopes = {"quest", "dialogue"},
      desc = "Plays a cutscene; the quest or dialogue goes on when it ended (or was skipped).",
      params = {{name = "cutscene", type = "cutscene", default = ""}},
      enter = function(inst, n, st)
        local name = n.params.cutscene
        if name == "" then return "next" end
        st.name = name:match("([^/]+)%.json$") or name
        if not flow.start_cutscene(name) then return "next" end
      end,
      event = function(inst, n, st, k, a) if k == "cutscene_end" and a == st.name then return "next" end end})

-- ---------------------------------------------------------------------------------
-- editor: look through the camera of a node
-- ---------------------------------------------------------------------------------
-- a = {def, node, entities = {...}, player = {...}}: the camera a Camera shot / Camera move node ends
-- up with, as {pos, look, fov}, for the editor to put its 3D view there.
function rbs.flow_cutscene_shot(a)
  local g = flow.build(a.def, "cutscene", "preview")
  if not g then return {error = "invalid cutscene"} end
  local n
  for _, nn in ipairs(g.nodes) do if nn.id == a.node then n = nn end end
  if not n then return {error = "no such node"} end
  local env = {entities = a.entities or {}, player = a.player}
  local pos, look, fov
  if n.type == "cs_camera" then
    pos, look, fov = shot_of(n.params, tonumber(a.t) or 0, env)
  elseif n.type == "cs_camera_move" then
    pos, look, fov = move_end(n.params, env)
    look = look or (pos and pos + vec(1, 0, 0) * 500)
  else
    return {error = "this node has no camera"}
  end
  if not pos then return {error = "the target of this node is not in the map"} end
  return {camera = {pos = {pos.x, pos.y, pos.z}, look = {look.x, look.y, look.z}, fov = fov or 60}}
end

return cut
