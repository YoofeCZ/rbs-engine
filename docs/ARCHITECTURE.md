# Architektura enginu

Přehled pro ty, kdo chtějí upravovat samotný engine (C++). Herní logika je ve skriptech – viz [SCRIPTING.md](SCRIPTING.md).

## Zásady

- **C++17**, jeden spustitelný soubor `rbs`, vše v `namespace rbs`. Závislosti: [raylib](https://www.raylib.com) 5.5 (okno, vstup, OpenGL 3.3, audio, načítání assetů), Lua 5.4, nlohmann/json. Stahuje je CMake (`FetchContent`).
- **Data-driven**: engine neví nic o konkrétních zbraních či nepřátelích. Ty definují Lua skripty nebo nativní pluginy. C++ dodává fyziku, kolize, renderer, audio, editor a API.
- Souřadnice: **Y nahoru**, úhly ve stupních, `yaw 0 = +X`. Vektor dopředu = `(cos p · cos y, sin p, −cos p · sin y)` (`AnglesForward` v `core/common.h`).
- Všechny cesty k datům jdou přes **VFS** (`vfs::Resolve`), nikdy přímo na disk – jen tak mohou mody cokoli přepsat.
- `RAYMATH_DISABLE_CPP_OPERATORS` je definované; operátory pro `Vector3` jsou v `core/common.h`.

## Moduly (`src/`)

| Složka | Obsah |
|---|---|
| `core/` | `vfs` (vrstvy `mods/<mod>` → `base`), `project` (otevřený projekt hry: `mod.json`, `launch.json` exportované hry), `cvar` (proměnné a příkazy konzole, `config.cfg`), `input` (přemapovatelné klávesy `bind_*`), `log`, `common.h` (matematika, řetězce) |
| `world/` | `brush` (konvexní brushe z rovin, polygonizace v double, CSG: subtract, hollow, split; UV), `map` (JSON load/save, viz [MAP_FORMAT.md](MAP_FORMAT.md)), `collision` (BVH nad brushi, Quake 3 trace boxu s bevel rovinami, pohyblivé brush entity) |
| `render/` | `renderer` (shadery, scéna v nižším rozlišení, post-process, dynamická světla, billboardy), `world_renderer` (meshe mapy seskupené podle entity a textury), `lightmap` (pečení lightmap), `assets` (cache textur, modelů, zvuků, hudby) |
| `game/` | `game` (načtení mapy, entity, fyzika, triggery, zvuky), `player` (pohyb à la Quake), `game_combat` (hitscan, projektily, exploze, poškození, gibs), `game_draw` (kamera, scéna, zbraň v ruce, HUD), `effects` (částice, decaly, dočasná světla) |
| `script/` | `script` (Lua stav, proxy entit, celé `rbs.*` API, události), `plugins` (C ABI pro nativní pluginy), `dynlib` (načítání .dll/.so/.dylib – odděleně, aby se `<windows.h>` nepotkal s raylibem) |
| `editor/` | `editor` (stav, operace s mapou, undo, import), `editor_views` (2D/3D pohledy, picking, gizmo, nástroje), `editor_panels` (toolbar, Hierarchy, Inspector, Project, dialogy), `editor_project` (dialog projektu, typy entit a zbraně bez kódu v Inspectoru) |
| `ui/` | `imgui` (vlastní immediate-mode UI editoru), `fonts` (TTF/OTF včetně latinky rozšířené), `console`, `lang` (překlady) |
| `app/` | `App`: stavy Menu / Game / Editor, hlavní smyčka, menu, příkazy konzole, přepínání projektů; `project_tools` (nový projekt, export samostatné hry) |

## Snímek (frame)

```
App::Run
 ├─ vstup, konzole
 ├─ podle stavu: Game::Update(dt) | Editor::Update | menu
 │    Game::Update: hráč → fyzika entit → think/update (Lua, pluginy, komponenty)
 │                  → triggery → rbs.on_update → zvuky (dt omezené na 50 ms)
 └─ kreslení
      Game::Draw: BeginScene (render texture v r_scale)
                  → svět (lightmapy + dynamická světla + mlha, obloha shaderem)
                  → entity (glTF modely se skeletální animací) → decaly → částice
                  → zbraň v ruce (vlastní projekce, depth range [0, 0.02])
                  EndScene → post-process (posterizace, dithering, vinětace, gama)
                  → Present (zvětšení s ostrými pixely) → mission camera → HUD (Lua) → zprávy
```

## Entity a události

- `Entity` (`game/entity.h`) je jedna struktura pro všechno: pozice, rychlost, box, `movetype`, `solid`, model a animace, světlo, sprite, zvuková smyčka, `props` (JSON z mapy), komponenty.
- Třída entity je buď **Lua** tabulka (`rbs.class`), nebo **nativní** (`plugins::Create`). Komponenty (`std::deque<Component>`) mohou být z obou světů.
- Události (`Game::EvSpawn`, `EvUpdate`, `EvTouch`, `EvUse`, `EvDamaged`, `EvPain`, `EvDie`, `EvBlocked`) volají nejdřív třídu, pak každou komponentu.
- Poškození: `damaged` (může upravit hodnotu) → odečtení zdraví → `pain`, nebo `die`.
- Lua vidí entitu jako proxy tabulku: `__index`/`__newindex` mapují pole (`pos`, `health`, `model`…) na C++ data. Pluginy používají stejná jména přes `get_float`/`set_vec`…, takže API je jednotné.

## Lightmapy

`render/lightmap.cpp` – každá plocha dostane mřížku luxelů (`lightmap_scale`), ty se zabalí do atlasu (shelf packing, okraj 1 luxel).

1. **Přímé světlo**: bodová světla, reflektory, slunce (stínové paprsky přes BVH; slunce jen přes plochy `sky`).
2. **Gather**: odražené světlo (1 bounce), světlo z oblohy a ambient occlusion.

Pečení běží asynchronně na všech jádrech (`std::thread`). Výsledek se ukládá do `cache/<mapa>.lmcache` s hashem JSONu mapy. Hodnoty jsou uložené v poloviční intenzitě (2× overbright jako v Quake 3).

## Projekty a export

- Projekt hry = složka `mods/<projekt>/` (vrstva VFS nad `base/`) s `mod.json`. `project::Current()` ho čte, `App::SwitchProject` přepne VFS a znovu načte assety, shadery, pluginy, fonty, překlad a `ApplyGameConfig` (startovní mapa, `game.cfg` – jen herní proměnné, nastavení hráče ne).
- `project::Build` (`app/project_tools.cpp`) zkopíruje spustitelný soubor enginu (`dynlib::ExecutablePath`), `base/` bez cache, projekt a licence a zapíše `launch.json`. `vfs::Init` dá složce se `launch.json` vedle spustitelného souboru přednost, takže exportovaná hra vždy běží na svých datech.

## Skriptování a pluginy

- `Script::Init` vytvoří sandboxovaný Lua stav. Následuje prelude (`rbs.class`, `rbs.component`, `rbs.weapon`, `require` přes VFS) a `scripts/init.lua`, pak `scripts/autorun/*.lua` ze všech vrstev, `rbs._load_definitions()` (typy a zbraně bez kódu z `entities/*.json` a `weapons/*.json`, `scripts/definitions.lua`) a skript mapy.
- Obsah bez kódu je jen data nad vestavěnými třídami: vestavěná AI (`monsters/base.lua`), předměty (`entities/items.lua`) a výstřel zbraní (`rbs.fire_weapon`) čtou své chování z vlastností. Editor jejich nabídky a nápovědu bere z `editor.enums` / `editor.help` (a `rbs.weapon_fields`).
- Nativní pluginy: `plugins::LoadAll` najde knihovny v `plugins/` všech vrstev a každou zkopíruje do `cache/plugins/<jméno>_<n>` (aby šla původní přepsat → hot reload). Pak zavolá `rbs_plugin_init(&api)`. Tabulka `rbs_api` je **jen rozšiřovaná na konci** a verzovaná (`RBS_PLUGIN_API_VERSION`).

### Jak přidat funkci do API

1. **Lua**: napiš `static int L_xxx(lua_State*)` v `script/script.cpp` a přidej ji do `kApi[]`. Zdokumentuj v `docs/SCRIPTING.md`.
2. **Pluginy**: přidej ukazatel na **konec** `rbs_api` v `sdk/include/rbs_plugin.h`, implementuj `Api_xxx` v `script/plugins.cpp`, doplň obal v `rbs_plugin.hpp`. Rust (`src/rbs.rs`) a C# (`Rbs.cs`) mají struktury se stejným pořadím polí – doplň je taky.
3. Nová vlastnost entity: pole v `Entity`, záznam v tabulce vlastností v `script.cpp` (get/set) a ve `plugins.cpp` (`get_float`/`set_float`…).

## Editor

- `Editor::Impl` (`editor/editor_impl.h`) drží mapu, výběr, nástroje a 4 pohledy (`EditorView`: 2D s mřížkou nebo 3D s render texturou).
- Undo/redo ukládá celou mapu jako JSON (max. 120 kroků) – jednoduché a spolehlivé.
- **Play** předá kopii mapy do `App::PlayFromEditor`; hra si upeče lightmapy (nebo vezme z cache) a `Esc` vrátí zpět.
- Editor používá vlastní Lua stav bez hry, jen aby zjistil třídy a komponenty (`RefreshClasses`) a jejich popisy pro Inspector.

## Testování

Engine má režim pro automatické snímky obrazovky, což se hodí pro rychlé ověření změn i v CI s virtuálním displejem:

```bash
xvfb-run -a ./build/rbs --map e1m1 --frames 120 --screenshot shot.png
```
```bash
xvfb-run -a ./build/rbs --editor e1m1 --exec "import /home/me/textures" --frames 30 --screenshot editor.png
```

`--exec` spustí příkazy konzole (oddělené `;`), jakmile je mapa nebo editor připravený.

## Styl kódu

- Kód, který měníš, naformátuj podle `.clang-format` v kořeni repozitáře (4 mezery, 150 znaků na řádek).
- Komentáře vysvětlují *proč*, ne *co*. Krátké funkce, žádné výjimky v herní smyčce.
- Pull requesty: popiš změnu, přilož screenshot u vizuálních změn a ověř build na své platformě.
