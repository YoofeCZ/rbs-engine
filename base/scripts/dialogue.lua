-- How a dialogue looks and plays (quests.lua runs the graph, this file shows it):
-- the dialogue box and its style, the speakers (name, colour, portrait, voice, the entity
-- they are), camera shots with a slow movement, text that types itself with a voice blip,
-- animations on the speaking character. The same code draws the preview in the editor
-- (rbs.flow_preview_state / rbs.flow_draw_dialogue). See docs/QUESTS.md.
--
-- In the dialogue file:
--   "speakers": [{"id": "guard", "name": "Guard", "entity": "guard_npc", "model": "...glb",
--                 "portrait": "sprites/guard.png", "emotions": {"angry": "sprites/guard_angry.png"},
--                 "color": [255,214,120], "voice": "sounds/blip.wav", "pitch": 1.0, "head": 58}]
--   "style":    {"position": "bottom", "text_speed": 45, "camera": true, "default_shot": "medium", ...}
-- and per Say / Choice node: shot, motion, emotion, animation, voice, speed, auto.

local flow = rbs.flow
local view = {}
flow.view = view

-- the editor builds its settings panel from these
view.shots = {"auto", "none", "close-up", "medium", "full", "over-shoulder", "profile", "wide"}
view.motions = {"auto", "none", "push-in", "pull-out", "orbit"}

-- ---------------------------------------------------------------------------------
-- style
-- ---------------------------------------------------------------------------------
view.defaults = {
  position = "bottom",              -- bottom | top | center
  width = 82,                       -- percent of the screen width
  font_size = 19,
  text_speed = 45,                  -- characters per second (0 = the whole line at once)
  box_color = {14, 17, 24}, box_alpha = 235,
  text_color = {235, 240, 248}, accent = {255, 214, 120},
  portrait_side = "left", portrait_size = 130,
  blip_volume = 0.5,
  letterbox = true,                 -- black bars while a camera shot is on
  camera = true,                    -- use camera shots at all
  default_shot = "medium", default_motion = "push-in",
  face_player = true,               -- the speaker turns to the player
  hide_hud = true,                  -- no health / ammo while a shot is on
}

rbs.dialogue_defaults = {style = view.defaults, shots = view.shots, motions = view.motions}

local function color(t, alpha)
  t = type(t) == "table" and t or {255, 255, 255}
  return {r = t[1] or t.r or 255, g = t[2] or t.g or 255, b = t[3] or t.b or 255, a = alpha or t[4] or t.a or 255}
end

-- the dialogue's style with the defaults filled in
function view.style_of(g)
  local s = {}
  for k, v in pairs(view.defaults) do s[k] = v end
  for k, v in pairs(g and g.style or {}) do
    if type(v) == type(view.defaults[k]) then s[k] = v end
  end
  return s
end

function view.speaker(g, id)
  for _, s in ipairs(g and g.speakers or {}) do
    if s.id == id or s.name == id then return s end
  end
  return {id = id, name = id}
end

-- ---------------------------------------------------------------------------------
-- text
-- ---------------------------------------------------------------------------------
local function char_count(text) return utf8.len(text) or #text end

local function head_chars(text, n)
  if n <= 0 then return "" end
  if n >= char_count(text) then return text end
  local off = utf8.offset(text, n + 1)
  return off and text:sub(1, off - 1) or text
end

local function wrap(text, width, size)
  local lines = {}
  for para in (text .. "\n"):gmatch("(.-)\n") do
    local line = ""
    for word in para:gmatch("%S+") do
      local test = line == "" and word or line .. " " .. word
      if line ~= "" and rbs.text_width(test, size) > width then
        lines[#lines + 1] = line
        line = word
      else
        line = test
      end
    end
    lines[#lines + 1] = line
  end
  return lines
end

-- ---------------------------------------------------------------------------------
-- camera shots
-- ---------------------------------------------------------------------------------
-- subject / other: {feet = vec, head = vec, height = number, yaw = degrees}. t = seconds
-- since the line began (for the slow movement). Returns position, look-at point, vertical fov.
function view.shot(shot, motion, t, subject, other)
  local h = math.max(20, subject.height)
  local up = vec(0, 1, 0)
  local f = vec.from_yaw(subject.yaw or 0)       -- the way the speaker faces
  local r = f:cross(up):normalize()
  local head = subject.head
  local pos, look, fov
  -- the dialogue box covers the bottom of the picture, so the shots aim a little below the
  -- subject: it sits in the upper part of the frame
  if shot == "close-up" then
    look = head - up * (h * 0.1)
    pos = look + f * (h * 0.75) + up * (h * 0.05)
    fov = 42
  elseif shot == "full" then
    look = subject.feet + up * (h * 0.42)
    pos = look + f * (h * 2.6) + r * (h * 0.25) + up * (h * 0.15)
    fov = 55
  elseif shot == "profile" then
    look = head - up * (h * 0.16)
    pos = look + r * (h * 1.3) + f * (h * 0.3)
    fov = 46
  elseif shot == "wide" then
    look = subject.feet + up * (h * 0.5)
    pos = head + f * (h * 2.7) + r * (h * 0.9) + up * (h * 0.6)
    fov = 60
  elseif shot == "over-shoulder" and other then
    -- from just behind the player's shoulder, whatever the distance really is
    local d = (head - other.head):flat():normalize()
    local side = d:cross(up):normalize()
    look = head - up * (h * 0.2)
    pos = head - d * (h * 1.9) + side * (h * 0.32) + up * (h * 0.15)
    fov = 46
  else  -- medium
    look = head - up * (h * 0.3)
    pos = look + f * (h * 1.35) + up * (h * 0.1)
    fov = 50
  end
  -- slow movement
  local dist = pos - look
  if motion == "push-in" then
    dist = dist * (1 - 0.2 * math.min(1, t / 6))
  elseif motion == "pull-out" then
    dist = dist * (1 + 0.25 * math.min(1, t / 6))
  elseif motion == "orbit" then
    local a = math.rad(math.min(t, 12) * 4)     -- four degrees a second
    local c, s = math.cos(a), math.sin(a)
    dist = vec(dist.x * c - dist.z * s, dist.y, dist.x * s + dist.z * c)
  end
  return look + dist, look, fov
end

-- body measurements of an entity (feet, head, height, facing)
function view.subject(ent, def)
  local mins, maxs = ent.mins, ent.maxs
  local base = mins and mins.y or 0
  local height = (maxs and mins) and (maxs.y - mins.y) or 64
  -- the model is what the player sees: its height wins over the collision box
  local model = ent.model
  if model and model ~= "" then
    local mn, mx = rbs.model_bounds(model)
    if mn then height = (mx.y - mn.y) * (tonumber(ent.scale) or 1) end
  end
  local feet = vec(ent.pos.x, ent.pos.y + base, ent.pos.z)
  local head_h = (def and tonumber(def.head)) or height * 0.9
  return {feet = feet, height = height, head = feet + vec(0, head_h, 0), yaw = ent.yaw or 0}
end

-- the entity that speaks: the player, a named entity, or the npc that started the dialogue
function view.actor(d, id)
  local def = view.speaker(d.graph, id)
  if def.entity == "player" or tostring(id):lower() == "player" then return rbs.player() end
  if def.entity and def.entity ~= "" then
    local e = rbs.find(def.entity)[1]
    if e and not e.removed then return e end
  end
  if d.actor and not d.actor.removed then return d.actor end
  return nil
end

-- ---------------------------------------------------------------------------------
-- a line (Say / Choice) as the UI shows it
-- ---------------------------------------------------------------------------------
function view.make_ui(g, n)
  local p = n.params
  local style = view.style_of(g)
  local sp = view.speaker(g, p.speaker)
  local ui = {
    kind = n.type == "choice" and "choice" or "say", node = n.id,
    speaker_id = p.speaker, speaker = flow.say_text(sp.name or p.speaker), speaker_color = sp.color,
    text = flow.say_text(p.text), shot = p.shot or "auto", motion = p.motion or "auto",
    emotion = p.emotion or "", animation = p.animation or "", voice = p.voice or "",
    speed = (tonumber(p.speed) or 0) > 0 and p.speed or style.text_speed, auto = tonumber(p.auto) or 0,
    t = 0, age = 0, shown = 0, sel = 1,
  }
  ui.len = char_count(ui.text)
  ui.portrait = sp.emotions and ui.emotion ~= "" and sp.emotions[ui.emotion] or sp.portrait
  if ui.speed <= 0 or ui.len == 0 then ui.shown, ui.done, ui.done_t = ui.len, true, 0 end
  return ui
end

local function play_animation(ent, anim)
  if not ent or anim == "" then return end
  ent:play(anim, false)
end

-- the typewriter: reveals characters with time and plays a blip for some of them
local function tick_text(d, ui, dt)
  ui.age = ui.age + dt
  if ui.done then
    ui.done_t = (ui.done_t or 0) + dt
    return
  end
  ui.t = ui.t + dt
  local target = math.min(ui.len, math.floor(ui.t * ui.speed))
  if target > ui.shown then
    local sp = view.speaker(d.graph, ui.speaker_id)
    local voice = ui.voice ~= "" and ui.voice or nil
    voice = voice or sp.voice
    if voice and voice ~= "" and rbs.time() - (ui.last_blip or -1) > 0.05 and target % 2 == 0 then
      ui.last_blip = rbs.time()
      rbs.sound(voice, nil, view.style_of(d.graph).blip_volume, (tonumber(sp.pitch) or 1) * (0.92 + math.random() * 0.16))
    end
    ui.shown = target
  end
  if ui.shown >= ui.len then ui.done, ui.done_t = true, 0 end
end

-- ---------------------------------------------------------------------------------
-- running a dialogue in the game
-- ---------------------------------------------------------------------------------
local function line_began(d, ui)
  local ent = view.actor(d, ui.speaker_id)
  play_animation(ent, ui.animation)
  ui.actor_animated = ui.animation ~= ""
  if ui.voice ~= "" then rbs.sound(ui.voice, nil, 1) end
end

local function line_ended(d, ui)
  if ui and ui.actor_animated then
    local ent = view.actor(d, ui.speaker_id)
    if ent then ent:play("idle", true) end
  end
end

local function clear_camera(d)
  if d.cam_active then
    rbs.camera_clear(4)
    d.cam_active = false
  end
  flow.cinematic = false
end

function view.on_finish(d)
  line_ended(d, d.ui)
  clear_camera(d)
  d.ui = nil
end

-- the player pressed "continue": completes the typing, or goes on. force = also skip the typing in one go
function flow.advance(force)
  local d = flow.dialogue
  if not d or d.done or not d.ui then return false end
  local ui = d.ui
  if not ui.done then
    ui.shown, ui.done, ui.done_t = ui.len, true, 0
    if not force then return true end
  end
  if ui.kind ~= "say" then return force and true or false end
  local node = d.nodes[ui.node]
  d.ui = nil
  line_ended(d, ui)
  if node then flow.leave(d, node, "next") end
  return true
end

function flow.pick(i, force)
  local d = flow.dialogue
  if not d or d.done or not d.ui or d.ui.kind ~= "choice" then return false end
  local ui = d.ui
  if not ui.done and not force then return false end
  local opt = ui.options[i]
  if not opt then return false end
  local node = d.nodes[ui.node]
  d.ui = nil
  line_ended(d, ui)
  if node then flow.leave(d, node, "o" .. opt.index) end
  return true
end

local function update_camera(d, dt)
  local g, ui = d.graph, d.ui
  local style = view.style_of(g)
  local shot = d.cam_shot or (ui and ui.shot) or "auto"
  local motion = d.cam_motion or (ui and ui.motion) or "auto"
  if ui and ui.shot ~= "auto" then shot = ui.shot end
  if ui and ui.motion ~= "auto" then motion = ui.motion end
  if shot == "auto" then shot = style.default_shot end
  if motion == "auto" then motion = style.default_motion end
  local who = d.cam_speaker or (ui and ui.speaker_id)
  local ent = style.camera and shot ~= "none" and who and view.actor(d, who) or nil
  if not ent then
    clear_camera(d)
    return
  end
  local p = rbs.player()
  local subject = view.subject(ent, view.speaker(g, who))
  local other = p and view.subject(p, nil) or nil
  local age = ui and ui.age or 0
  local pos, look, fov = view.shot(shot, motion, age, subject, other)
  rbs.camera_set(pos, look, fov, d.cam_active and 6 or 4)
  d.cam_active = true
  flow.cinematic = style.hide_hud
end

local function face_player(d, dt)
  local p = rbs.player()
  if not p or not view.style_of(d.graph).face_player then return end
  local seen = {}
  local function turn(ent)
    if ent and ent ~= p and not seen[ent] then
      seen[ent] = true
      ent:face(p.pos, 360, dt)
    end
  end
  if d.ui then turn(view.actor(d, d.ui.speaker_id)) end
  turn(d.actor)
end

-- called every frame while a dialogue runs: text, camera, characters and input
function view.update(d, dt)
  local ui = d.ui
  if ui then
    if not ui.started then ui.started = true; line_began(d, ui) end
    tick_text(d, ui, dt)
  end
  face_player(d, dt)
  update_camera(d, dt)
  ui = d.ui
  if not ui or rbs.time() < (d.block_until or 0) then return end
  local confirm = rbs.key_pressed("space") or rbs.key_pressed("enter") or rbs.key_pressed("e") or rbs.key_pressed("mouse1")
  if ui.auto > 0 and ui.done and ui.kind == "say" and ui.done_t >= ui.auto then
    flow.advance(true)
  elseif ui.kind == "say" then
    if confirm then flow.advance() end
  elseif ui.kind == "choice" then
    if not ui.done then
      if confirm then flow.advance() end
      return
    end
    local n = #ui.options
    for i = 1, math.min(9, n) do
      if rbs.key_pressed(tostring(i)) then flow.pick(i); return end
    end
    if rbs.key_pressed("up") then ui.sel = ((ui.sel or 1) - 2) % n + 1 end
    if rbs.key_pressed("down") then ui.sel = (ui.sel or 1) % n + 1 end
    if rbs.key_pressed("enter") or rbs.key_pressed("space") then
      flow.pick(ui.sel or 1)
    else
      local mx, my, click = rbs.mouse()
      for i, r in ipairs(ui.rects or {}) do
        if mx >= r.x and mx <= r.x + r.w and my >= r.y and my <= r.y + r.h then
          ui.sel = i
          if click then flow.pick(i) end
          break
        end
      end
    end
  end
end

-- ---------------------------------------------------------------------------------
-- drawing (the game and the editor preview)
-- ---------------------------------------------------------------------------------
-- Draws the dialogue box (and the black bars) inside the rectangle x, y, w, h; returns the
-- rectangles of the answers (for the mouse). ui is a line from make_ui.
function view.draw(ui, style, x, y, w, h, opts)
  opts = opts or {}
  local s = h / 900
  local rects = {}
  if opts.cinematic and style.letterbox then
    local bar = h * 0.11
    rbs.draw_rect(x, y, w, bar, {r = 0, g = 0, b = 0, a = 255})
    rbs.draw_rect(x, y + h - bar, w, bar, {r = 0, g = 0, b = 0, a = 255})
  end
  if not ui then return rects end
  local size = math.max(8, math.floor(style.font_size * s))
  local pad = 18 * s
  local bw = math.min(w * style.width / 100, 1250 * s)
  local portrait = ui.portrait and ui.portrait ~= ""
  local pw = portrait and style.portrait_size * s or 0
  local text_w = bw - pad * 2 - (portrait and pw + pad or 0)
  local lines = wrap(ui.text or "", text_w, size)
  local line_h = size + 6 * s
  local name_h = (ui.speaker and ui.speaker ~= "") and 26 * s or 0
  local body = name_h + #lines * line_h
  local opts_h = 0
  if ui.kind == "choice" and ui.done then opts_h = 8 * s + #(ui.options or {}) * (line_h + 6 * s) end
  local height = math.max(pad * 2 + body + opts_h + (ui.kind == "say" and 22 * s or 0), portrait and pw + pad * 2 or 0)
  local margin = opts.cinematic and style.letterbox and h * 0.11 + 24 * s or (opts.cinematic and 40 * s or 130 * s)
  local bx = x + (w - bw) / 2
  local by
  if style.position == "top" then by = y + margin
  elseif style.position == "center" then by = y + (h - height) / 2
  else by = y + h - height - margin end
  local box, accent, txt = color(style.box_color, style.box_alpha), color(style.accent), color(style.text_color)
  rbs.draw_rect(bx, by, bw, height, box)
  rbs.draw_rect(bx, by, bw, height, {r = 90, g = 100, b = 125, a = 255}, true, 2)
  local tx = bx + pad
  if portrait then
    local px = style.portrait_side == "right" and bx + bw - pad - pw or bx + pad
    rbs.draw_rect(px - 2, by + pad - 2, pw + 4, pw + 4, {r = 8, g = 10, b = 14, a = 255})
    rbs.draw_image(ui.portrait, px, by + pad, pw, pw)
    rbs.draw_rect(px - 2, by + pad - 2, pw + 4, pw + 4, {r = 90, g = 100, b = 125, a = 255}, true, 2)
    if style.portrait_side ~= "right" then tx = bx + pad * 2 + pw end
  end
  local ty = by + pad
  if name_h > 0 then
    rbs.draw_text(ui.speaker, tx, ty, math.floor(size * 1.05), ui.speaker_color and color(ui.speaker_color) or accent, "left", true, "ui_bold")
    ty = ty + name_h
  end
  local remaining = ui.done and math.huge or ui.shown or 0
  for _, l in ipairs(lines) do
    if remaining <= 0 then break end
    local lc = char_count(l)
    rbs.draw_text(remaining >= lc and l or head_chars(l, remaining), tx, ty, size, txt, "left", true, "ui")
    remaining = remaining - lc - 1
    ty = ty + line_h
  end
  if ui.kind == "choice" and ui.done then
    ty = ty + 8 * s
    for i, o in ipairs(ui.options or {}) do
      local r = {x = tx - 6 * s, y = ty - 2 * s, w = bx + bw - pad - tx + 12 * s, h = line_h + 2 * s}
      rects[i] = r
      local on = (ui.sel or 1) == i
      if on then rbs.draw_rect(r.x, r.y, r.w, r.h, {r = 56, g = 90, b = 160, a = 220}) end
      rbs.draw_text(i .. ".  " .. o.text, tx, ty, size, on and txt or {r = 170, g = 178, b = 192, a = 255}, "left", true, "ui")
      ty = ty + line_h + 6 * s
    end
  elseif ui.kind == "say" and ui.done then
    if math.floor(rbs.time() * 2) % 2 == 0 then
      rbs.draw_text("[Space]", bx + bw - pad, by + height - pad - 12 * s, math.floor(14 * s), {r = 170, g = 178, b = 192, a = 255}, "right", false, "ui")
    end
  end
  return rects
end

function view.draw_game(w, h)
  local d = flow.dialogue
  if not d or d.done then return end
  local style = view.style_of(d.graph)
  if d.ui then
    d.ui.rects = view.draw(d.ui, style, 0, 0, w, h, {cinematic = d.cam_active})
  elseif d.cam_active then
    view.draw(nil, style, 0, 0, w, h, {cinematic = true})
  end
end

-- ---------------------------------------------------------------------------------
-- nodes
-- ---------------------------------------------------------------------------------
local DIALOGUE = {"dialogue"}
local function node(def) rbs.flow_node(def) end

local line_params = {
  {name = "shot", type = "enum", values = view.shots, default = "auto", help = "camera shot for this line: auto = the dialogue's default | none = the normal view | close-up (face) | medium | full (body) | over-shoulder | profile | wide"},
  {name = "motion", type = "enum", values = view.motions, default = "auto", help = "slow camera movement during the line"},
  {name = "emotion", type = "string", default = "", help = "which portrait of the speaker to show (speaker's emotions)"},
  {name = "animation", type = "string", default = "", help = "animation of the speaking character (a trigger of its animator, or an animation of its model)"},
  {name = "voice", type = "sound", default = "", help = "a sound played with the line (a voice line)"},
  {name = "speed", type = "number", default = 0, help = "characters per second for this line (0 = the dialogue's text speed)"},
  {name = "auto", type = "number", default = 0, help = "go on by itself this many seconds after the line is shown (0 = wait for the player)"},
}

local function with(list, extra)
  local out = {}
  for _, p in ipairs(extra) do out[#out + 1] = p end
  for _, p in ipairs(list) do out[#out + 1] = p end
  return out
end

node({id = "say", category = "dialogue", title = "Say", scopes = DIALOGUE,
      desc = "A character says a line. The text types itself; Space / Enter / E or a click finishes it and goes on.",
      params = with(line_params, {{name = "speaker", type = "speaker", default = ""}, {name = "text", type = "text", default = ""}}),
      enter = function(inst, n) inst.ui = view.make_ui(inst.graph, n) end})

node({id = "choice", category = "dialogue", title = "Choice", ports = "options", scopes = DIALOGUE,
      desc = "The player picks one of the answers (keys 1-9, arrows + Enter, or the mouse); every answer has its own output.",
      params = with({line_params[1], line_params[2], line_params[5]}, {{name = "speaker", type = "speaker", default = ""},
                                                                         {name = "text", type = "text", default = "", help = "what is said before the answers (optional)"}}),
      enter = function(inst, n)
        local opts = {}
        for i, o in ipairs(n.options or {}) do
          local shown = true
          if o.require and o.require ~= "" then shown = flow.vars[o.require] and true or false end
          if shown then opts[#opts + 1] = {text = flow.say_text(o.text), index = i} end
        end
        if #opts == 0 then return "o1" end
        local ui = view.make_ui(inst.graph, n)
        ui.options = opts
        inst.ui = ui
      end})

node({id = "camera", category = "dialogue", title = "Camera", scopes = DIALOGUE,
      desc = "Sets the camera shot from here on (until the next Camera node or a line with its own shot).",
      params = {
        {name = "shot", type = "enum", values = view.shots, default = "medium"},
        {name = "motion", type = "enum", values = view.motions, default = "auto"},
        {name = "speaker", type = "speaker", default = "", help = "who to film (empty = whoever speaks)"},
      },
      enter = function(inst, n)
        inst.cam_shot = n.params.shot ~= "auto" and n.params.shot or nil
        inst.cam_motion = n.params.motion ~= "auto" and n.params.motion or nil
        inst.cam_speaker = n.params.speaker ~= "" and n.params.speaker or nil
        return "next"
      end})

node({id = "animate", category = "dialogue", title = "Animate", scopes = DIALOGUE,
      desc = "Plays an animation on a character (a trigger of its animator, or an animation of its model).",
      params = {{name = "speaker", type = "speaker", default = ""}, {name = "animation", type = "string", default = "attack"}},
      enter = function(inst, n)
        local ent = view.actor(inst, n.params.speaker)
        play_animation(ent, n.params.animation)
        return "next"
      end})

-- ---------------------------------------------------------------------------------
-- editor preview
-- ---------------------------------------------------------------------------------
local function arr(v) return {v.x, v.y, v.z} end

-- the first Say / Choice reached from the Start (following the ports in order)
local function first_line(g)
  local seen, queue, i = {}, {}, 1
  for _, s in ipairs(g.starts) do queue[#queue + 1] = s end
  while queue[i] do
    local n = queue[i]
    i = i + 1
    if not seen[n.id] then
      seen[n.id] = true
      if n.type == "say" or n.type == "choice" then return n end
      local ports = n.meta and n.meta.ports
      local names = {}
      if type(ports) == "table" then
        for _, p in ipairs(ports) do names[#names + 1] = p end
      else
        for p in pairs(n.out) do names[#names + 1] = p end
        table.sort(names)
      end
      for _, p in ipairs(names) do
        for _, t in ipairs(n.out[p] or {}) do queue[#queue + 1] = t end
      end
    end
  end
  for _, n in ipairs(g.nodes) do if n.type == "say" or n.type == "choice" then return n end end
end

-- Everything the editor needs to draw a line the way the game would: the typed text, the
-- camera shot around a stand-in speaker and player, and the style. a = {def, node, t, entities}
function rbs.flow_preview_state(a)
  local g = flow.build(a.def, "dialogue", "preview")
  if not g then return {error = "invalid dialogue"} end
  local n
  for _, nn in ipairs(g.nodes) do if nn.id == a.node then n = nn end end
  if not n or (n.type ~= "say" and n.type ~= "choice") then n = first_line(g) end
  if not n then return {empty = true} end
  local ui = view.make_ui(g, n)
  if n.type == "choice" then
    ui.options = {}
    for i, o in ipairs(n.options or {}) do ui.options[#ui.options + 1] = {text = flow.say_text(o.text), index = i} end
  end
  local t = tonumber(a.t) or 0
  ui.age = t
  if ui.speed > 0 then
    ui.shown = math.min(ui.len, math.floor(t * ui.speed))
    ui.done = ui.shown >= ui.len
  else
    ui.shown, ui.done = ui.len, true
  end
  local style = view.style_of(g)
  local sp = view.speaker(g, ui.speaker_id)
  local info = (a.entities or {})[sp.entity or ""] or {}
  local model = (sp.model and sp.model ~= "") and sp.model or info.model or "models/monsters/grunt.glb"
  local scale = tonumber(info.scale) or 1
  local height = 64 * scale
  local mn, mx = rbs.model_bounds(model)
  if mn then height = (mx.y - mn.y) * scale end
  -- the speaker stands at the origin facing +x, the player 240 units in front of it
  local subject = {feet = vec(0, 0, 0), height = height, head = vec(0, tonumber(sp.head) or height * 0.9, 0), yaw = 0}
  local other = {feet = vec(240, 0, 0), height = 56, head = vec(240, 50, 0), yaw = 180}
  local shot = ui.shot ~= "auto" and ui.shot or style.default_shot
  local motion = ui.motion ~= "auto" and ui.motion or style.default_motion
  local camera
  if style.camera and shot ~= "none" then
    local pos, look, fov = view.shot(shot, motion, t, subject, other)
    camera = {pos = arr(pos), look = arr(look), fov = fov}
  else
    -- no shot: what the player sees from where it stands
    camera = {pos = arr(other.head), look = arr(subject.head), fov = 70}
    shot = "none"
  end
  return {
    ui = ui, style = style, camera = camera, shot = shot, motion = motion, cinematic = shot ~= "none",
    speaker = {model = model, scale = scale, tint = info.tint, pos = {0, 0, 0}, yaw = 0},
    player = {pos = {240, 0, 0}, yaw = 180},
    node = n.id,
  }
end

-- Draws the dialogue box of a preview into a.rect = {x, y, w, h}; returns the answer rectangles.
function rbs.flow_draw_dialogue(a)
  local r = a.rect
  local rects = view.draw(a.ui, a.style, r[1], r[2], r[3], r[4], {cinematic = a.cinematic})
  return {rects = rects}
end

return view
