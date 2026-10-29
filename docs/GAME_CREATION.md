# Jak udělat vlastní hru

RBS Engine není jen jedna hra – je to **nástroj na tvorbu retro FPS** (jako Unity nebo FPS Creator, ale zaměřený na boomer shootery). Hra, kterou v něm uděláš, je **projekt**: složka v `mods/` s vlastními levely, nepřáteli, zbraněmi, menu a nastavením. Hotový projekt jedním tlačítkem vyexportuješ jako **samostatnou hru**, kterou můžeš dát hráčům, nahrát na itch.io nebo prodávat (licence MIT to dovoluje).

![Typ nepřítele bez kódu v editoru](images/editor_nocode.jpg)

Většinu hry uděláš **bez programování** – vestavěné chování (AI nepřátel, zbraně, předměty, dveře, triggery) se nastavuje v editoru. Kód (Lua, C, C++, Rust, C#) přijde na řadu jen pro věci, které vestavěné možnosti nepokrývají.

- [1. Nový projekt](#1-nový-projekt)
- [2. Levely](#2-levely)
- [3. Nepřátelé](#3-nepřátelé)
- [4. Zbraně](#4-zbraně)
- [5. Předměty](#5-předměty)
- [6. Logika levelu bez kódu](#6-logika-levelu-bez-kódu)
- [7. Nastavení hry a menu](#7-nastavení-hry-a-menu)
- [8. Vlastní grafika a zvuky](#8-vlastní-grafika-a-zvuky)
- [9. Kód, když je potřeba](#9-kód-když-je-potřeba)
- [10. Export hry](#10-export-hry)

## 1. Nový projekt

V editoru klikni na tlačítko **Project** úplně nahoře v panelu Hierarchy (ukazuje název otevřeného projektu) → **New project...**, napiš název hry a potvrď **Create**. Editor vytvoří `mods/<složka>/` se startovní místností a rovnou ji otevře.

Totéž z konzole: `new_project moje_hra Moje Hra`. Existující projekt otevřeš v témže dialogu (rozbalovací seznam + **Open**), příkazem `project moje_hra`, nebo `rbs --editor --mod moje_hra`.

```
mods/moje_hra/
  mod.json                 název, startovní mapa, menu, hráč (editor: Project > nastavení)
  maps/start.json          levely
  entities/*.json          typy nepřátel a předmětů bez kódu (Project > Entities > New type)
  weapons/*.json           zbraně bez kódu (Project > Weapons > New weapon)
  scripts/autorun/*.lua    tvůj kód (nepovinný)
  textures/ models/ sounds/ sprites/ ...   tvoje assety
  README.md
```

Projekt může používat všechno ze základní hry (`base/` – textury, modely, zvuky, nepřátele, zbraně, předměty) a cokoli z toho **přepsat** souborem se stejnou cestou. Detaily v [MODDING.md](MODDING.md).

## 2. Levely

Levely se staví z kvádrů (brushů) jako v Quake: nakreslíš místnosti, odečteš průchody, otexturuješ, rozmístíš světla a entity, upečeš osvětlení (`Ctrl+B`) a hraješ (`F5`). Celý postup a klávesy jsou v [EDITOR.md](EDITOR.md#postup-první-level-za-5-minut).

Další level: `Ctrl+N`, uložit (`Ctrl+S`) do `maps/`. Levely propojíš triggerem `trigger_changelevel` (vlastnost `map`), nebo v nastavení mapy (Inspector, když nic není vybrané) vyplníš **Next map**.

## 3. Nepřátelé

Základní hra má tři nepřátele (`monster_grunt` – voják, `monster_heavy` – těžký kulometčík, `monster_fiend` – skákající démon). Všichni používají **vestavěnou AI**, kterou řídí vlastnosti.

**Úprava jednoho nepřítele:** vyber ho v mapě a v Inspectoru → Properties změň, co chceš (zdraví, rychlost, režim AI, druh útoku, model, barvu…). Najetím myší na název vlastnosti se ukáže nápověda.

**Nový typ nepřítele:**
1. Project → **Entities** → klikni na nepřítele, ze kterého chceš vycházet (např. `monster_grunt`).
2. V Inspectoru **Create a variant (no code)** (nebo tlačítko **+ New type**), napiš jméno, třeba `zombie`.
3. Inspector teď ukazuje typ `zombie`: nastav vlastnosti, např. `ai` = `melee`, `speed` = 70, `attack_type` = `melee`, `tint` = zelená. **Save**.
4. **Place (Entity tool)** a klikáním ho rozmísti do mapy. Změny typu se projeví na všech umístěných kusech (pokud si kus danou hodnotu nepřepsal sám).

Typ se uloží jako `entities/zombie.json` – malý čitelný soubor, který jde upravit i ručně. Všechny vlastnosti AI, režimy (`chase`, `ranged`, `melee`, `turret`, `patrol`), druhy útoku, hlídky po `path_corner` a přepadení popisuje [AI.md](AI.md).

## 4. Zbraně

Project → **Weapons** ukazuje všechny zbraně (číslo = slot, klávesa, kterou se vybírá).

**Nová zbraň:** klikni na zbraň, ze které chceš vycházet → **Create a new weapon from this** (nebo **+ New weapon**) → jméno. Nová zbraň je kopie se všemi hodnotami, které stačí přepsat:

| Pole | |
|---|---|
| `name`, `slot` | název v HUD a klávesa |
| `model`, `pickup_model` | model v ruce (glTF, hlaveň ve směru +Z) a model ležící v levelu |
| `ammo`, `ammo_per_shot`, `start_ammo`, `max_ammo` | druh munice (`shells`, `cells`, `rockets` nebo vlastní název; prázdné = nekonečná) a kolik |
| `fire_rate`, `automatic`, `recoil` | sekundy mezi výstřely, střelba držením, kopnutí kamery |
| `shot` | `hitscan` (okamžité kulky: `bullets`, `spread`, `damage`) nebo `projectile` (`projectile`, `projectile_speed`, `projectile_damage`) |
| `sound`, `volume`, `smoke` | zvuk výstřelu, kouř z hlavně |
| `view_offset`, `view_rotation`, `view_scale`, `muzzle`, `flash_color`, `flash_size` | poloha modelu v zorném poli a záblesk |

Zbraň se uloží jako `weapons/<id>.json`, třeba:
```json
{
  "name": "Railgun",
  "slot": 6,
  "model": "models/weapons/v_plasma.glb",
  "pickup_model": "models/weapons/w_plasma.glb",
  "ammo": "cells",
  "ammo_per_shot": 10,
  "start_ammo": 50,
  "max_ammo": 300,
  "fire_rate": 1.2,
  "shot": "hitscan",
  "bullets": 1,
  "spread": 0,
  "damage": 120,
  "sound": "sounds/plasma.wav",
  "view_offset": [8, -8.5, 12],
  "view_rotation": [0, 3, 0],
  "view_scale": 0.9,
  "muzzle": [0, 2.8, 30],
  "flash_color": [0.4, 0.7, 1.6],
  "flash_size": 20
}
```

Každá zbraň má automaticky i entitu `weapon_<id>`, kterou hráč v levelu sebere (tlačítko **Place pickup** v Inspectoru). Se kterými zbraněmi hráč začíná, nastavíš v [nastavení hry](#7-nastavení-hry-a-menu). Zbraň, která dělá něco úplně jiného (granáty, laser, mrazák…), dostane vlastní funkci `fire` v Lua – viz [SCRIPTING.md](SCRIPTING.md#zbraně--rbsweapon).

## 5. Předměty

| Třída | Vlastnosti |
|---|---|
| `item_health`, `item_health_mega` | `amount` (kolik přidá), `max` (nad kolik nezvýší) |
| `item_armor` | `amount`, `max` |
| `item_ammo`, `item_shells`, `item_cells`, `item_rockets` | `ammo` (druh), `amount` |
| `item_weapon`, `weapon_<id>` | `weapon` (id zbraně) |
| `key_red`, `key_blue`, `key_yellow` | klíčové karty pro dveře s `key` |

Všechny předměty mají navíc `model`, `scale`, `tint`, `message` (text po sebrání), `pickup_sound` a `respawn` (za kolik sekund se objeví znovu, 0 = nikdy). Vlastní předmět uděláš stejně jako nepřítele – variantou v Project → Entities. Například `entities/item_medkit_big.json`:

```json
{
  "base": "item_health",
  "description": "Big medkit: 50 health, up to 150.",
  "properties": { "amount": 50, "max": 150, "scale": 1.5, "message": "Big medkit" }
}
```

## 6. Logika levelu bez kódu

Entity se propojují jmény: entita s `target = "dvere1"` při spuštění „vystřelí“ (aktivuje) všechny entity s `targetname = "dvere1"`. Brush entity (dveře, tlačítka, triggery) vzniknou z nakreslených brushů tlačítkem **To Entity**.

| Entita | Co dělá |
|---|---|
| `func_door` | posuvné dveře: otevřou se, když přijdeš blízko (`auto`), nebo když je něco aktivuje; `key` = potřebná karta |
| `func_button` | tlačítko (E nebo výstřel, `shootable`) – aktivuje `target` |
| `func_plat` | výtah |
| `func_wall`, `func_illusionary` | zeď, kterou jde skriptem schovat / falešná zeď, kterou jde projít (tajné místnosti) |
| `trigger_once`, `trigger_multiple` | když do nich hráč vejde, aktivují `target` (a ukážou `message`) |
| `trigger_hurt`, `trigger_push` | láva/drtič, skokový polštář |
| `trigger_secret` | tajná oblast (počítá se do statistik) |
| `trigger_changelevel` | konec levelu (`map` = další mapa) |
| `trigger_camera` + `mission_camera` | obraz z kamery v rohu obrazovky |
| `target_delay`, `target_message`, `target_sound` | zpoždění, text na obrazovce, zvuk |
| `info_objective` | cíl mise (Tab); splní se, když ho něco aktivuje |
| `path_corner` | bod hlídkové trasy nepřátel ([AI.md](AI.md#hlídky-path_corner)) |

Příklad: tlačítko otevře dveře a ukáže zprávu – tlačítko `target = "brana"`, dveře `targetname = "brana"`, `auto = 0`, a `target_message` s `targetname = "brana"`, `message = "Brána se otevírá!"`.

K libovolné entitě jde v Inspectoru přidat i **komponenty** (*Add Component*): otáčení, houpání, blikající světlo, zničitelnost, spawner nepřátel, poškození dotykem… ([EDITOR.md](EDITOR.md#komponenty)).

## 7. Nastavení hry a menu

Tlačítko **Project** v Hierarchy otevře nastavení projektu:

- **Title, Author, Version, Description** – název hry (i v titulku okna a v menu) a údaje o ní.
- **Start map** – level, kterým začíná *New game*.
- **Main menu** – velký nadpis, podnadpis a **pozadí** (přetáhni sprite z Project → Sprites, nebo napiš cestu k obrázku), jestli exportovaná hra nabídne editor a stránku **Mods**.
- **Player at the start of a level** – zdraví, brnění, startovní zbraně (id oddělená mezerou) a která je vybraná.

**Save settings** všechno zapíše do `mod.json`:

```json
{
  "title": "Moje Hra",
  "author": "Já",
  "version": "1.0",
  "description": "Pekelný pivovar plný démonů.",
  "start_map": "maps/start.json",
  "menu": { "title": "MOJE HRA", "subtitle": "Epizoda 1", "background": "sprites/menu.png",
            "show_editor": false, "show_mods": false },
  "player": { "health": 100, "max_health": 100, "armor": 0,
              "start_weapons": ["pistol", "shotgun"], "select": "shotgun",
              "max_ammo": { "shells": 50 }, "start_ammo": { "shells": 10 } }
}
```

`max_health`, `max_ammo` (limity munice podle druhu) a `start_ammo` (munice navíc na startu) jde zatím nastavit jen v souboru.

Pravidla pohybu a další herní proměnné konzole patří do `mods/<projekt>/game.cfg`, jedna na řádek:

```
sv_maxspeed 360
sv_jumpspeed 300
sv_airaccelerate 12
```

Do `game.cfg` jdou jen herní proměnné (`sv_*`, `g_*`, `game_start_map` a vlastní proměnné, které čtou tvoje skripty přes `rbs.cvar`). Nastavení hráče – citlivost myši, FOV, grafika, zvuk – patří hráči (menu **Options**), takže je projekt měnit nemůže.

## 8. Vlastní grafika a zvuky

Přetáhni soubory nebo celé složky z průzkumníku do okna editoru – textury (PNG/TGA/JPG), modely (glTF/GLB, OBJ, IQM), zvuky (WAV/OGG/MP3/FLAC), fonty a skripty se zkopírují do projektu a jdou hned použít ([EDITOR.md](EDITOR.md#import-vlastních-souborů-drag--drop)). Modely nepřátel potřebují animace `idle`, `walk`, `attack`, `pain`, `death`. Formáty a tipy pro Blender jsou v [MODDING.md](MODDING.md#formáty-assetů).

Soubor se stejnou cestou jako v `base/` přepíše původní – takhle vyměníš třeba model brokovnice (`models/weapons/v_shotgun.glb`) nebo zvuk výstřelu, aniž bys sahal na zbraň.

## 9. Kód, když je potřeba

- **Lua** (`scripts/autorun/*.lua` v projektu) – nové třídy entit, vlastní AI, zbraně s vlastní funkcí `fire`, pravidla hry, HUD, skripty levelů. Bez kompilace, v sandboxu. API: [SCRIPTING.md](SCRIPTING.md), vlastní AI: [AI.md](AI.md#vlastní-ai-v-lua).
- **Nativní pluginy** v C, C++, Rustu, C# a dalších jazycích – stejné třídy a komponenty, plný výkon: [PLUGINS.md](PLUGINS.md).

Kód a no-code jdou kombinovat: Lua třída se v editoru ukáže stejně jako vestavěná, a když ji uděláš přes `rbs.monster`, jde z ní dál dělat varianty bez kódu.

## 10. Export hry

V nastavení projektu klikni na **Build game** (nebo `build_game` v konzoli). Editor nejdřív uloží otevřenou mapu a nastavení a pak vytvoří `builds/<projekt>/`:

```
builds/moje_hra/
  Moje_Hra(.exe)        engine pojmenovaný podle hry
  base/                 data enginu a základní obsah (bez ukázkových levelů, pokud je nezaškrtneš)
  mods/moje_hra/        tvoje hra
  licenses/             licence enginu a knihoven (přibal je ke hře)
  launch.json           co se spustí: {"project": "moje_hra", "editor": false, "mods": false, "console": true}
```

Složku zabal (zip) a rozdávej. Hra se spustí rovnou do tvého menu, s tvým názvem, a vždy používá vlastní data, ať ji hráč spustí odkudkoli. Upečené osvětlení se přibalí; levely, které jsi nepekl, se upečou při prvním spuštění.

- Export zkopíruje engine, na kterém právě běžíš – verzi pro **Windows** vyexportuj na Windows, pro Linux na Linuxu, pro macOS na Macu (nebo vezmi engine z CI buildů a data přidej).
- `launch.json` jde upravit ručně: `"editor": true` nechá v menu editor (hráči můžou dělat vlastní levely), `"mods": true` stránku s mody, `"console": false` vypne konzoli.
- Konzolový příkaz: `build_game [složka] [editor|noeditor] [demo]`.
