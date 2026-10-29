# Umělá inteligence nepřátel

Každý nepřítel v RBS používá **vestavěnou AI** (třída `monster_base`), dokud mu nenapíšeš vlastní. Máš dvě cesty a jdou kombinovat:

1. **Bez programování** – chování nastavíš vlastnostmi v editoru: pro celý typ nepřítele (Project → Entities → *New type*, uloží se do `entities/<jméno>.json`), nebo jen pro jeden umístěný kus v Inspectoru.
2. **Vlastním kódem** – v Lua (nebo v [nativním pluginu](PLUGINS.md)) přepíšeš jen to, co potřebuješ: útok, rozhodování, nebo celé chování. Všechno ostatní zůstane vestavěné.

Zdrojový kód vestavěné AI je v `base/scripts/monsters/base.lua` – je krátký a dá se číst jako šablona.

## Jak vestavěná AI funguje

```
idle ──uvidí hráče / zásah / trigger──▶ chase ──hráč v dosahu útoku──▶ attack ──▶ chase
 │                                       │ ▲
 └─ s patrol_path chodí po path_corner   ├─▶ pain  (zásah, náhodně podle pain_chance)
                                         └─▶ flee  (zdraví pod flee_health)
                                                             zdraví 0 ──▶ dead
```

- **idle** – čeká (nebo hlídkuje). Každých 0,3 s se podívá, jestli vidí hráče do vzdálenosti `sight_range`. Když ho uvidí, probudí i nepřátele do 500 jednotek kolem sebe.
- **chase** – metoda `think()` rozhodne, jestli útočit, nebo kam jít (podle režimu `ai`).
- **attack** – přehraje animaci `attack`, po `attack_delay` sekundách zavolá `attack()` a `attack_time` sekund stojí. Další útok nejdřív za `attack_cooldown` sekund.
- **pain**, **flee**, **dead** – krátké ucuknutí, útěk, smrt (pod `gib_health` se tělo rozletí na kusy; i mrtvolu jde rozstřílet).

Z 3D modelu se používají animace pojmenované `idle`, `walk`, `attack`, `pain` a `death` (glTF, viz [MODDING.md](MODDING.md#3d-modely-modelsglb)). Když model nějakou nemá, prostě se nepřehraje. Místo vestavěné AI může příšeru řídit **strom chování** (`ai = behavior`, vlastnost `behavior`), viz [BEHAVIOR.md](BEHAVIOR.md). Vlastnost `animator` připojí [animátor](ANIMATION.md) (prolínání, přechody, události); AI mu pak posílá jen triggery `attack` a `pain` a chůzi a smrt si animátor řídí sám.

## Vlastnosti

V editoru má každá vlastnost nápovědu (najeď myší na její název) a výběrové vlastnosti mají rozbalovací seznam.

**Chování**

| Vlastnost | Výchozí | Význam |
|---|---|---|
| `health` | 60 | zdraví |
| `ai` | `chase` | režim, viz níže |
| `speed` | 150 | rychlost chůze (hráč běží 320) |
| `turn_speed` | 540 | otáčení ve stupních za sekundu |
| `sight_range` | 1800 | na jakou vzdálenost si hráče všimne |
| `wake_on_sight` | `true` | vypnuto = přepadení: čeká, dokud ho nezraníš nebo neaktivuje trigger |
| `keep_distance` | 96 | `chase`: jak blízko k hráči se zastaví; `ranged`: odstup, který drží |
| `patrol_path` | `""` | `targetname` prvního `path_corner` hlídkové trasy |
| `flee_health` | 0 | pod tímto zdravím na chvíli uteče (0 = nikdy) |

**Útok**

| Vlastnost | Výchozí | Význam |
|---|---|---|
| `attack_type` | `hitscan` | druh útoku, viz níže |
| `attack_range` | 900 | od jaké vzdálenosti začne útočit |
| `attack_cooldown` | 1.5 | sekundy mezi útoky (trochu náhodně) |
| `attack_delay` | 0.35 | nápřah: sekundy od začátku animace k výstřelu/úderu |
| `attack_time` | 0.8 | jak dlouho ho útok zastaví |
| `bullets`, `spread`, `bullet_damage` | 1, 0.05, 5 | hitscan: počet broků, rozptyl (0.005 = odstřelovač, 0.2 = brokovnice), poškození broku |
| `projectile`, `projectile_speed`, `projectile_damage` | `fireball`, 700, 15 | projektil: třída (`fireball`, `plasma_bolt`, `rocket` nebo vlastní), rychlost, poškození přímým zásahem |
| `melee_range`, `melee_damage` | 72, 15 | dosah a síla úderu zblízka |
| `leap` | `false` | ze střední vzdálenosti po hráči skočí (útoky zblízka) |
| `attack_sound`, `melee_sound` | `""` | zvuk výstřelu / úderu |
| `muzzle_flash` | `true` | záblesk světla při výstřelu |

**Reakce a vzhled**

| Vlastnost | Výchozí | Význam |
|---|---|---|
| `pain_chance` | 0.6 | šance 0–1, že po zásahu ucukne |
| `gib_health` | -40 | pod tímto zdravím se tělo rozletí na kusy |
| `sight_sound`, `pain_sound`, `death_sound` | zvuky vojáka | zvuky |
| `model` | `""` | glTF model (prázdné = model typu) |
| `scale` | 1 | velikost modelu |
| `tint` | 255 255 255 | obarvení modelu (násobí barvy textury) |

### Režimy `ai`

| Režim | Chování |
|---|---|
| `chase` | běží k hráči, zastaví se na `keep_distance` a útočí |
| `ranged` | drží odstup `keep_distance`: když jsi blízko, couvá, jinak uhýbá do stran a střílí |
| `melee` | jde až k tobě (na `melee_range`) |
| `turret` | nikdy se nehne, jen se otáčí a útočí |
| `patrol` | jako `chase`; hlídkovat ale může každý režim, stačí vyplnit `patrol_path` |

### Druhy útoku `attack_type`

| Druh | |
|---|---|
| `hitscan` | okamžité kulky: `bullets`, `spread`, `bullet_damage` |
| `projectile` | vystřelí entitu `projectile` rychlostí `projectile_speed` |
| `melee` | úder zblízka; s `leap = true` po hráči skočí |
| `melee+projectile` | zblízka drápy, z dálky projektil (jako `monster_fiend`) |
| `none` | neútočí vůbec |
| `custom` | útok je vlastní kód třídy (metoda `attack`, viz níže) |

## Hlídky (`path_corner`)

1. Postav do mapy entity `path_corner` (Project → Entities, kategorie *ai*) tam, kudy má nepřítel chodit.
2. Každé dej jméno (`targetname`, např. `p1`, `p2`, `p3`) a do `target` napiš jméno dalšího bodu. Poslední bod odkaž zpátky na první a trasa bude dokola.
3. `wait` = kolik sekund na bodu postojí.
4. Nepříteli nastav `patrol_path` = `p1`. Chodí poloviční rychlostí, dokud tě neuvidí.

## Přepadení (ambush)

Nepříteli vypni `wake_on_sight` a dej mu `targetname`, třeba `ambush1`. Na místo, kudy hráč půjde, nakresli brush, převeď ho na `trigger_once` (tlačítko *To Entity*) a jeho `target` nastav na `ambush1`. Nepřítel se probudí, až hráč vstoupí do triggeru (nebo když ho zraní). Stejné jméno může mít víc nepřátel – probudí se všichni.

## Příklady bez kódu

Typ nepřítele je soubor `entities/<jméno>.json` v projektu. Editor ho vytvoří sám (*New type* / *Create a variant*), ale jde psát i ručně. `properties` obsahují jen to, co se liší od základu (`base`).

Zombie – pomalá, jen zblízka:
```json
{
  "base": "monster_grunt",
  "description": "Slow zombie, claws up close.",
  "properties": {
    "health": 120,
    "ai": "melee",
    "speed": 70,
    "attack_type": "melee",
    "melee_damage": 20,
    "pain_chance": 0.2,
    "tint": [120, 200, 120]
  }
}
```

Odstřelovač – nehýbe se, míří a dá jednu přesnou ránu:
```json
{
  "base": "monster_grunt",
  "description": "Sniper: never moves, aims, one precise shot.",
  "properties": {
    "ai": "turret",
    "attack_type": "hitscan",
    "bullets": 1,
    "spread": 0.005,
    "bullet_damage": 35,
    "attack_delay": 0.8,
    "attack_cooldown": 3,
    "attack_range": 3000,
    "sight_range": 3000
  }
}
```

Další nápady: `ranged` + `projectile` = čaroděj, který drží odstup; `flee_health` = zbabělec, který při zranění uteče pro posily; `melee` + `leap` + vysoké `speed` = skákající pes.

## Vlastní AI v Lua

Napiš třídu, jejímž základem je `monster_base` nebo kterýkoli nepřítel, a přepiš jen to, co potřebuješ. Kód patří do `scripts/autorun/<cokoli>.lua` v projektu (nový projekt už takový soubor má, se zakomentovaným příkladem).

`rbs.monster(jméno, def)` je zkratka pro `rbs.class`: doplní kategorii *monsters* a v editoru ukáže **všechny** vestavěné vlastnosti AI (s výchozími hodnotami z `editor.props`), takže tvůj nepřítel jde dál ladit bez kódu.

| Co přepsat | Kdy se volá |
|---|---|
| `attack(self, enemy)` | když má zaútočit (po `attack_delay`). Nastav `attack_type = "custom"`, ať je v editoru vidět, že útok je kód |
| `think(self, dt, player)` | každý snímek ve stavu *chase*: rozhoduje, jestli útočit a kam jít |
| `update(self, dt)` | každý snímek, celé chování (stavy pain, attack, flee… pak řešíš sám) |
| `wake(self, enemy)`, `pain`, `die`, `damaged`, `use`, `spawn` | probuzení a běžné callbacky entit ([SCRIPTING.md](SCRIPTING.md#třídy-entit--rbsclassname-def)) |

Původní verzi zavoláš přes třídu: `rbs.classes.monster_base.think(self, dt, player)`.

Pomůcky, které nepřítel má: `self:move(dt, dir, speed, face_dir?, look_at?)` (chůze s obcházením překážek), `self:walk(dir, speed)`, `self:face(pos, turn_speed, dt)`, `self:play(anim, loop, speed?)`, `self:distance(e)`, `self:can_see(e)`, `self:forward()`, `self:reach()` (dosah útoku); pole `self.state`, `self.enemy`, `self.next_attack` a všechny vlastnosti z tabulky výše (`self.speed`, `self.attack_cooldown`…). Další funkce (`rbs.projectile`, `rbs.fire_bullets`, `rbs.damage`, `rbs.spawn`, `rbs.trace`…) popisuje [SCRIPTING.md](SCRIPTING.md).

> Vlastní vlastnosti nepojmenovávej jako metody entit (`sound`, `damage`, `remove`, `walk`, `face`, `forward`, `play`, `distance`, `can_see`) – `self.damage` by pak místo čísla vrátilo funkci.

### Vlastní útok: vyvolávač

Místo střelby přivolá vojáky (nejvýš `max_minions` živých najednou). Pohyb, odstup a reakce zůstávají vestavěné.

```lua
rbs.monster("monster_summoner", {
  base = "monster_fiend",
  editor = { desc = "Summoner: calls in soldiers.",
             props = { health = 150, ai = "ranged", keep_distance = 500, attack_type = "custom",
                       attack_range = 1200, attack_cooldown = 5, tint = {120, 255, 120}, max_minions = 3 } },
  attack = function(self, enemy)
    self.minions = self.minions or {}
    for i = #self.minions, 1, -1 do          -- zapomeň mrtvé
      if self.minions[i].dead or self.minions[i].removed then table.remove(self.minions, i) end
    end
    if #self.minions >= util.num(self.props.max_minions, 3) then return end
    local pos = self.pos + self:forward() * 64
    local m = rbs.spawn("monster_grunt", pos, nil, self.yaw)
    if m then
      m:wake(enemy)
      table.insert(self.minions, m)
      rbs.particles(pos, vec(0, 1, 0), { count = 30, color = {120, 255, 120, 255}, speed = 120, size = 4, life = 0.6 })
    end
  end,
})
```

Vlastní vlastnost (`max_minions`) čti přes `self.props` – tam je i hodnota nastavená v Inspectoru pro jeden konkrétní kus.

### Vlastní rozhodování: berserker

Když je těžce zraněný, zrychlí a střílí častěji. Zbytek rozhodování nechá vestavěné AI.

```lua
rbs.monster("monster_berserker", {
  base = "monster_grunt",
  editor = { desc = "Gets faster and shoots more often when hurt.", props = { health = 120 } },
  think = function(self, dt, player)
    if not self.enraged and self.health < self.max_health * 0.5 then
      self.enraged = true
      self.speed = self.speed * 1.8
      self.attack_cooldown = self.attack_cooldown * 0.4
      rbs.sound(self.sight_sound, self.pos, 1.0, 0.7)
    end
    rbs.classes.monster_base.think(self, dt, player)
  end,
})
```

### Úplně vlastní AI: mina

Vlastní `update` přebírá celé chování. Mina se po spatření hráče rozjede za ním a vybuchne.

```lua
rbs.class("monster_seeker", {
  base = "monster_base",
  editor = { category = "monsters", color = {255, 60, 60}, mins = {-10, 0, -10}, maxs = {10, 20, 10},
             model = "models/items/rockets.glb", desc = "Mine that rolls after the player and explodes.",
             props = { health = 20, speed = 260 } },
  update = function(self, dt)
    if self.dead then return end
    local p = rbs.player()
    if not p or p.dead then return end
    if self.state == "idle" then
      if self:distance(p) < self.sight_range and self:can_see(p) then self.state = "chase" end
      return
    end
    self:move(dt, (p.pos - self.pos):flat():normalize(), self.speed, true)
    if self:distance(p) < 48 then rbs.damage(self, 1000, self) end
  end,
  die = function(self)
    if self.exploded then return end
    self.exploded = true
    rbs.explosion(self.center, 160, 60, self)
    rbs.remove(self)
  end,
})
```

Tady je použitý `rbs.class` místo `rbs.monster`, takže editor ukáže jen vlastnosti z `props` (`health`, `speed`). Ostatní (`sight_range`…) mají výchozí hodnoty z `monster_base`.

Hotový nepřítel s vlastním útokem je i v ukázkovém modu: `mods/example_mod/scripts/autorun/example_mod.lua` (`monster_imp`).

## AI v nativním pluginu (C, C++, Rust, C#)

Nepřítele jde napsat celého v kompilovaném jazyce – třída z pluginu dostává stejné události (`spawn`, `update`, `damaged`, `pain`, `die`…) a má přístup ke stejným funkcím enginu. Hotový příklad je `cpp_turret` v `sdk/examples/cpp_plugin` (věž, která sleduje hráče a střílí plazmu), návod v [PLUGINS.md](PLUGINS.md). Pluginem můžeš také napsat **komponentu** a přidat ji v editoru k libovolnému nepříteli (Inspector → *Add Component*), třeba léčivou auru nebo štít.
