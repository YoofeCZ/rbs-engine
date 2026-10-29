-- HUD drawn every frame. Coordinates are in pixels; `s` scales with the window height.
-- Replace this file in a mod to make a completely different HUD.

local GOLD = {r = 214, g = 168, b = 92, a = 255}
local CREAM = {r = 245, g = 232, b = 205, a = 255}
local PANEL = {r = 30, g = 24, b = 20, a = 220}
local EDGE = {r = 92, g = 74, b = 54, a = 255}

local function panel(x, y, w, h, s)
  rbs.draw_rect(x, y, w, h, PANEL)
  rbs.draw_rect(x, y, w, h, EDGE, true, 2 * s)
  rbs.draw_rect(x + 2 * s, y + 2 * s, w - 4 * s, h - 4 * s, {r = 0, g = 0, b = 0, a = 120}, true, 1)
end

local function face_image(p)
  local h = p.health
  if h > 80 then return "hud/face_0.png" end
  if h > 60 then return "hud/face_1.png" end
  if h > 40 then return "hud/face_2.png" end
  if h > 20 then return "hud/face_3.png" end
  return "hud/face_4.png"
end

function rbs.draw_hud(w, h)
  local p = rbs.player()
  if not p then return end
  local info = rbs.hud_info()
  local s = h / 900

  -- crosshair
  if not info.dead then
    rbs.draw_rect(w / 2 - 1.5 * s, h / 2 - 1.5 * s, 3 * s, 3 * s, {r = 255, g = 255, b = 255, a = 200})
  end

  -- bottom centre: HEALTH | face | AMMO
  local pw, ph = 260 * s, 64 * s
  local px, py = w / 2 - pw / 2, h - ph - 4 * s
  panel(px, py, pw, ph, s)
  local fw = 56 * s
  local fx = w / 2 - fw / 2
  rbs.draw_rect(fx - 2 * s, py + 3 * s, fw + 4 * s, ph - 6 * s, {r = 10, g = 8, b = 6, a = 255})
  local flash = info.damage_flash > 0.2 and {r = 255, g = 120, b = 120, a = 255} or {r = 255, g = 255, b = 255, a = 255}
  rbs.draw_image(face_image(p), fx, py + 4 * s, fw, ph - 8 * s, flash)

  local hp = math.max(0, math.floor(p.health + 0.5))
  local hcol = hp <= 25 and {r = 255, g = 80, b = 60, a = 255} or CREAM
  rbs.draw_text(rbs.tr("HEALTH"), px + 51 * s, py + 5 * s, math.floor(12 * s), GOLD, "center")
  rbs.draw_text(tostring(hp), px + 51 * s, py + 19 * s, math.floor(38 * s), hcol, "center", true)

  local ammo = info.infinite and "\u{221E}" or tostring(info.ammo or 0)
  local acol = (not info.infinite and (info.ammo or 0) <= 5) and {r = 255, g = 80, b = 60, a = 255} or CREAM
  rbs.draw_text(rbs.tr("AMMO"), px + pw - 51 * s, py + 5 * s, math.floor(12 * s), GOLD, "center")
  if info.infinite then
    rbs.draw_text(ammo, px + pw - 51 * s, py + 14 * s, math.floor(44 * s), acol, "center", true, "ui_bold")
  else
    rbs.draw_text(ammo, px + pw - 51 * s, py + 19 * s, math.floor(38 * s), acol, "center", true)
  end

  -- armor (left of the panel, only when you have some)
  if p.armor > 0 then
    local ax = px - 96 * s
    panel(ax, py + 14 * s, 88 * s, ph - 14 * s, s)
    rbs.draw_text(rbs.tr("ARMOR"), ax + 44 * s, py + 18 * s, math.floor(11 * s), GOLD, "center")
    rbs.draw_text(tostring(math.floor(p.armor)), ax + 44 * s, py + 30 * s, math.floor(28 * s), {r = 140, g = 190, b = 255, a = 255}, "center", true)
  end

  -- keycards
  local kx = px + pw + 10 * s
  for _, k in ipairs({{"red", {r = 255, g = 60, b = 50, a = 255}}, {"blue", {r = 60, g = 120, b = 255, a = 255}}, {"yellow", {r = 255, g = 220, b = 40, a = 255}}}) do
    if info.keys[k[1]] then
      rbs.draw_rect(kx, py + 20 * s, 14 * s, 22 * s, k[2])
      rbs.draw_rect(kx, py + 20 * s, 14 * s, 22 * s, {r = 0, g = 0, b = 0, a = 255}, true, 1)
      kx = kx + 18 * s
    end
  end

  -- mission goals (bottom right), toggle with TAB
  if #info.objectives > 0 and rbs.cvar("hud_objectives") ~= 0 then
    local gw = 300 * s
    local lineh = 22 * s
    local gh = (40 + #info.objectives * 22) * s
    local gx, gy = w - gw - 14 * s, h - gh - 14 * s
    panel(gx, gy, gw, gh, s)
    rbs.draw_rect(gx, gy, gw, 24 * s, {r = 70, g = 52, b = 36, a = 255})
    rbs.draw_text(rbs.tr("MISSION GOALS"), gx + gw / 2, gy + 5 * s, math.floor(15 * s), GOLD, "center")
    for i, o in ipairs(info.objectives) do
      local y = gy + 30 * s + (i - 1) * lineh
      local c = o.done and {r = 120, g = 200, b = 110, a = 255} or CREAM
      rbs.draw_text((o.done and "[x] " or "[ ] ") .. rbs.tr(o.text), gx + 12 * s, y, math.floor(15 * s), c, "left", false, "ui")
    end
  end
end
