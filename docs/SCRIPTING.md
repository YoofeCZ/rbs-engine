# Lua skriptování

Veškerá herní logika základní hry je v Lua skriptech v `base/scripts/` – entity, zbraně, nepřátelé, předměty, HUD i pravidla hry. Můžeš je upravovat bez kompilace (stačí restartovat mapu), přepsat v modu, nebo přidat vlastní. Stejné API pro nativní jazyky popisuje [PLUGINS.md](PLUGINS.md).

- Lua verze: **5.4** v **sandboxu** – mody jsou cizí kód, takže skripty nemají přístup k disku ani k systému: knihovny base, string, table, math, utf8, coroutine a z `os` jen clock/time/date/difftime. Chybí `io`, `debug`, `package.loadlib`, `dofile`/`loadfile`; `load` přijímá jen zdrojový text (ne bytecode) a `rbs.read_file` čte jen soubory hry (relativní cesty bez `..`). Co potřebuje víc, patří do [nativního pluginu](PLUGINS.md).
- `require "monsters.grunt"` načte `scripts/monsters/grunt.lua` přes virtuální souborový systém (mod má přednost).
- Při startu mapy se spustí `scripts/init.lua`, potom **všechny** `scripts/autorun/*.lua` ze základní hry i aktivního modu, a nakonec skript mapy `maps/<mapa>.lua`.
- Chyby se vypisují do konzole (`` ` ``) se stack tracem, hra nespadne.

## Rychlý příklad

```lua
-- scripts/autorun/my_stuff.lua v modu
rbs.class("my_spinning_skull", {
  editor = { category = "props", color = {255, 80, 80}, mins = {-8, 0, -8}, maxs = {8, 16, 8},
             model = "models/gibs/head.glb", desc = "Točící se lebka, která dává zdraví.",
             props = { heal = 10 } },
  spawn = function(self)
    self.model = "models/gibs/head.glb"
    self.solid = "trigger"
    self.spin = 120               -- stupňů za sekundu
    self.bob = 4                  -- houpání nahoru/dolů
  end,
  touch = function(self, other)
    if other.classname ~= "player" then return end
    other.health = math.min(other.max_health, other.health + util.num(self.props.heal, 10))
    rbs.sound("sounds/pickup.wav", self.pos)
    rbs.remove(self)
  end,
})
```
Třída se hned objeví v editoru (Project → Entities) i s výchozími vlastnostmi v Inspectoru.

## Vektory

`vec(x, y, z)` vytvoří vektor. Osa **Y míří nahoru**, 1 jednotka ≈ 1 palec (hráč je 56 vysoký).

| | |
|---|---|
| `a + b`, `a - b`, `a * 2`, `2 * a`, `a * b`, `a / 2`, `-a`, `a == b` | operátory |
| `v:len()`, `v:len2d()`, `v:normalize()`, `v:dot(b)`, `v:cross(b)`, `v:dist(b)`, `v:flat()`, `v:copy()`, `v:lerp(b, t)` | metody |
| `vec.from_yaw(deg)`, `vec.from_angles(pitch, yaw)`, `vec.yaw(v)`, `vec.random()` | pomocné funkce |

Úhly jsou ve stupních: `yaw` 0 = +X, 90 = −Z (proti směru hodinových ručiček při pohledu shora), `pitch` kladný nahoru.

## Třídy entit — `rbs.class(name, def)`

```lua
rbs.class("monster_grunt", {
  base = "monster_base",          -- dědičnost (metody a pole se hledají v předkovi)
  editor = { ... },               -- popis pro editor (viz níže)
  spawn = function(self) end,
  update = function(self, dt) end,
  touch = function(self, other, normal) end,
  use = function(self, activator) end,
  damaged = function(self, amount, attacker, dir) return amount end,
  pain = function(self, amount, attacker) end,
  die = function(self, attacker, amount) end,
  blocked = function(self, other) end,     -- dveře/výtahy, které něco blokuje
})
```

| Callback | Kdy se volá |
|---|---|
| `spawn(self)` | po vytvoření entity (z mapy nebo `rbs.spawn`) |
| `update(self, dt)` | každý snímek |
| `touch(self, other, normal)` | trigger je překryt hráčem/nepřítelem, projektil něco trefil (`other` = nil pro zeď) |
| `use(self, activator)` | hráč stiskl E, nebo entitu „vystřelil“ jiný target (`target`/`targetname`) |
| `damaged(self, amount, attacker, dir)` | **před** aplikací poškození; vrať nové číslo (0 = ignorovat) |
| `pain(self, amount, attacker)` | po zásahu, který nezabil |
| `die(self, attacker, amount)` | zdraví kleslo na 0 (mrtvé entity dál dostávají `damaged` – rozstřelení mrtvoly) |
| `blocked(self, other)` | pohyblivý brush narazil do entity |

### Popis pro editor (`editor = {...}`)

| Pole | Význam |
|---|---|
| `desc` | popis zobrazený v Inspectoru |
| `category` | skupina v Hierarchy a v Project → Entities |
| `color` | barva v editoru `{r, g, b}` |
| `mins`, `maxs` | velikost boxu (relativně k počátku) |
| `model`, `sprite` | náhled v editoru (a výchozí model entity) |
| `brush = true` | brush entita (dveře, triggery…) – vzniká z brushů tlačítkem *To Entity* |
| `props` | výchozí hodnoty vlastností; zobrazí se v Inspectoru a doplní se do `self.props` |

### Pole entity (`self.xxx`)

| Pole | Typ | Popis |
|---|---|---|
| `id`, `classname` | čtení | identifikátor, třída |
| `pos`, `vel`, `angles` | vec | pozice, rychlost, úhly (pitch, yaw, roll) |
| `yaw`, `pitch` | číslo | otočení |
| `mins`, `maxs` | vec | kolizní box relativně k `pos` |
| `center`, `absmin`, `absmax` | vec, čtení | střed a absolutní box |
| `health`, `max_health`, `armor`, `team` | číslo | |
| `takedamage`, `monster`, `dead` | bool | |
| `movetype` | text | `none`, `walk` (gravitace + schody), `fly` (projektil), `toss`, `bounce`, `push`, `noclip` |
| `solid` | text | `not`, `bbox`, `bsp` (brush entita), `trigger`, `corpse` (jen pro hitscan) |
| `on_ground`, `blocked` | bool, čtení | stojí na zemi / poslední pohyb narazil |
| `gravity`, `bounce`, `friction` | číslo | fyzikální parametry |
| `owner` | entita | projektily nekolidují s vlastníkem |
| `model`, `scale`, `model_offset`, `tint` | | 3D model (`tint = {r=,g=,b=,a=}`) |
| `anim`, `anim_speed`, `anim_loop`, `anim_time`, `anim_done` | | animace modelu (glTF animace podle jména) |
| `sprite`, `sprite_size`, `sprite_additive` | | billboard sprite místo modelu |
| `visible`, `fullbright`, `flash` | | viditelnost, ignorování světla, bílý záblesk po zásahu |
| `light_color`, `light_radius` | vec, číslo | dynamické světlo připojené k entitě |
| `spin`, `bob` | číslo | otáčení (°/s) a houpání (předměty) |
| `loop_sound`, `loop_volume`, `loop_radius` | | smyčka zvuku na pozici entity |
| `targetname`, `target` | text | propojení entit (`rbs.fire_targets`) |
| `props` | tabulka | vlastnosti z editoru (+ výchozí z `editor.props`) |
| `offset` | vec | posun brush entity (dveře, výtahy, tlačítka) |
| `bmins`, `bmaxs`, `is_brush` | čtení | box brushů brush entity |
| `lifetime` | číslo | automatické odstranění po N sekundách |
| `removed` | bool, čtení | entita už neexistuje |

Vlastní pole si můžeš do `self` ukládat libovolně (`self.state = "chase"`).

### Metody každé entity

`self:remove()`, `self:sound(path, vol, pitch)`, `self:distance(other)`, `self:can_see(other)`, `self:walk(dir, speed)`, `self:face(pos, turn_speed, dt)`, `self:damage(amount, attacker, dir)`, `self:forward()`, `self:play(anim, loop, speed)`.

## Komponenty — `rbs.component(name, def)`

Komponenty jsou chování, která se dají **přidat k libovolné entitě** v editoru (Inspector → *Add Component*), podobně jako v Unity. Každá instance má vlastní tabulku s vlastnostmi.

```lua
rbs.component("rotator", {
  editor = { desc = "Otáčí entitou.", props = { speed = 90 } },
  start = function(c, ent) end,                 -- po spawn entity
  update = function(c, ent, dt) ent.yaw = ent.yaw + c.speed * dt end,
  -- volitelně: touch(c, ent, other, normal), use(c, ent, activator),
  --            damaged(c, ent, amount, attacker, dir) -> amount, pain(...), die(c, ent, attacker, amount), blocked(c, ent, other)
})
```
`c` je instance komponenty: obsahuje vlastnosti (`c.speed`), `c.entity` a `c.type`. Za běhu jde komponentu přidat přes `rbs.add_component(ent, "rotator", {speed = 30})`.

Základní hra obsahuje: `rotator`, `bobber`, `flicker_light`, `destructible`, `fire_on_death`, `hurt_on_touch`, `look_at_player`, `spawner`.

## Zbraně — `rbs.weapon{...}`

```lua
rbs.weapon{
  id = "shotgun", slot = 2, name = "Double-barrel shotgun",
  model = "models/weapons/v_shotgun.glb",
  ammo = "shells", ammo_per_shot = 2, start_ammo = 24,   -- bez `ammo` = nekonečná munice
  fire_rate = 0.95, automatic = true, recoil = 2.2,
  view_offset = {6.5, -8, 12},     -- vpravo, nahoru, dopředu (v prostoru kamery)
  view_rotation = {0, 2.5, 0}, view_scale = 1,
  muzzle = {0, 2.5, 34}, flash_color = {1, 0.7, 0.3}, flash_size = 26,
  fire = function(player)
    rbs.sound("sounds/shotgun.wav")
    rbs.fire_bullets(player, { count = 22, spread = 0.11, damage = 8 })
  end,
}
```
Model zbraně míří hlavní do **+Z** (glTF „dopředu“), Y nahoru, počátek u rukojeti.

## Funkce `rbs.*`

### Entity
| Funkce | |
|---|---|
| `rbs.spawn(class, pos, props?, yaw?) → ent` | vytvoří entitu |
| `rbs.remove(ent)` | odstraní entitu |
| `rbs.player()` | hráč |
| `rbs.get(id)` | entita podle id |
| `rbs.find(targetname)`, `rbs.find_class(class)`, `rbs.find_radius(pos, r)` | seznamy entit |
| `rbs.add_component(ent, type, props?)` | přidá komponentu za běhu |
| `rbs.walk(ent, dir, speed)`, `rbs.face(ent, pos, turn_speed, dt)` | pohyb a otáčení (AI) |
| `rbs.visible(a, b)` | přímá viditelnost |
| `rbs.model_bounds(path) → mins, maxs` | rozměry modelu |

### Svět a boj
| Funkce | |
|---|---|
| `rbs.trace(from, to, opts?)` | paprsek/box: `opts = {ignore=ent, mins=vec, maxs=vec, entities=true}` → `{hit, fraction, pos, normal, entity, sky, startsolid}` |
| `rbs.damage(target, amount, attacker?, dir?)` | poškození |
| `rbs.radius_damage(pos, radius, damage, attacker?, ignore?)` | plošné poškození |
| `rbs.fire_bullets(owner, {count, spread, damage, range, origin?, dir?})` | hitscan (krev, decaly, jiskry automaticky) |
| `rbs.projectile(class, owner, origin?, dir?, speed?)` | vystřelí entitu |
| `rbs.explosion(pos, radius, damage, attacker?)` | exploze s efekty a zvukem |
| `rbs.gib(ent, count?)` | roztrhá entitu na kusy |
| `rbs.blood(pos, dir, amount)` | krev |
| `rbs.aim() → eye, dir` | pozice očí a směr pohledu hráče |
| `rbs.light_at(pos)` | barva osvětlení v bodě |

### Efekty a zvuk
| Funkce | |
|---|---|
| `rbs.particles(pos, dir, {count, color, speed, spread, size, size_var, life, gravity, drag, additive, collide, blood, growth, texture})` | částice |
| `rbs.light(pos, color, radius, seconds)` | krátké dynamické světlo |
| `rbs.decal(from, to, texture, size, color?)` | decal na první plochu na paprsku |
| `rbs.sound(path, pos?, volume?, pitch?)` | zvuk (bez `pos` = 2D) |
| `rbs.flash(amount)` | zlatý záblesk obrazovky (sebrání předmětu) |

### Průběh hry
| Funkce | |
|---|---|
| `rbs.fire_targets(name, activator?)` | zavolá `use` na entitách s `targetname = name` + handlery `rbs.on` |
| `rbs.on(name, function(activator) end)` | reakce skriptu mapy na target |
| `rbs.message(text, seconds?)` | text uprostřed obrazovky |
| `rbs.objective(id, text, done)` | cíl mise (panel MISSION GOALS) |
| `rbs.change_map(name?)` | konec levelu (prázdné = `next_map` z nastavení mapy / menu) |
| `rbs.show_camera(targetname, seconds)` | obraz z `mission_camera` v rohu |
| `rbs.give_weapon(id, select?)`, `rbs.has_weapon(id)` | zbraně hráče |
| `rbs.give_ammo(type, n)`, `rbs.ammo(type)`, `rbs.max_ammo(type, max?)` | munice |
| `rbs.set_key(name, has?)`, `rbs.has_key(name)` | klíče (karty) |
| `rbs.stats()`, `rbs.add_stat(name, n?)` | `kills, monsters, secrets, secrets_total, items, items_total, time` |
| `rbs.time()` | čas mapy v sekundách |
| `rbs.cvar(name, value?)` | čtení/zápis proměnné konzole |
| `rbs.log(...)`, `print(...)` | výpis do konzole |
| `rbs.read_file(path)` | přečte soubor z VFS |

### Hooky
Definuj funkce v tabulce `rbs` (mody je mohou zřetězit: `local base = rbs.on_player_spawn; function rbs.on_player_spawn(p) base(p) ... end`):

| Hook | |
|---|---|
| `rbs.on_map_start()` | po spawnu všech entit |
| `rbs.on_update(dt)` | každý snímek |
| `rbs.on_player_spawn(player)` | start mapy (bez přeneseného inventáře) – výchozí zbraně |
| `rbs.on_player_death(player)` | smrt hráče |
| `rbs.draw_hud(w, h)` | vykreslení HUD (viz `scripts/hud.lua`) |

### 2D kreslení (HUD)
`rbs.screen() → w, h`, `rbs.draw_text(text, x, y, size, color, align?, shadow?, font?)`, `rbs.text_width(text, size, font?)`, `rbs.draw_rect(x, y, w, h, color, outline?, thickness?)`, `rbs.draw_image(path, x, y, w?, h?, color?, src?)`, `rbs.image_size(path)`, `rbs.hud_info()` (zbraň, munice, cíle, klíče, damage flash). Fonty: `"hud"`, `"title"`, `"ui"`, `"ui_bold"`, `"mono"` nebo libovolný `fonts/<jméno>.ttf`.

`rbs.tr(text)` vrátí překlad textu podle zvoleného jazyka (`lang/<kód>.json`, viz [MODDING.md](MODDING.md#fonty-a-překlady)); když překlad neexistuje, vrátí text beze změny. Zprávy `rbs.message` a texty cílů mise se překládají automaticky.

## Skript mapy

Soubor `maps/<mapa>.lua` vedle `maps/<mapa>.json` se načte automaticky:

```lua
function rbs.on_map_start()
  rbs.objective("power", "Zapni generátor", false)
end

rbs.on("generator_button", function(activator)    -- func_button s target = generator_button
  rbs.objective("power", "", true)
  rbs.fire_targets("big_door")
  rbs.show_camera("door_cam", 3)
end)
```

## Built-in třídy

`player_start`, `light`, `light_spot`, `sprite`, `fire`, `ambient_sound`, `mission_camera`, `prop_model`, `prop_barrel`, `info_null`, `info_objective`, `target_delay`, `target_message`, `target_sound`, `func_door`, `func_button`, `func_plat`, `func_wall`, `func_illusionary`, `trigger_once`, `trigger_multiple`, `trigger_hurt`, `trigger_push`, `trigger_secret`, `trigger_changelevel`, `trigger_camera`, předměty `item_*`, `weapon_*`, `key_*`, nepřátelé `monster_grunt`, `monster_heavy`, `monster_fiend`. Zdrojáky jsou v `base/scripts/` – nejlepší dokumentace i šablona pro vlastní obsah.
