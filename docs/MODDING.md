# Modování

Mod i celá nová hra jsou v RBS totéž: **projekt** = složka v `mods/`. Postup tvorby vlastní hry krok za krokem (editor, nepřátelé a zbraně bez kódu, nastavení, export) je v [GAME_CREATION.md](GAME_CREATION.md); tady jsou technické detaily souborů.

## Jak fungují mody

Engine čte všechna data přes **virtuální souborový systém** ve vrstvách:

1. `mods/<aktivní mod>/` – nejvyšší priorita
2. `base/` – základní hra

Když mod obsahuje soubor se stejnou cestou jako `base/`, použije se ten z modu. Nové soubory se prostě přidají. Takže:

- nová textura → `mods/mujmod/textures/moje_cihla.png`
- přebarvená cihla → `mods/mujmod/textures/brick_red.png` (přepíše originál)
- nový nepřítel bez kódu → `mods/mujmod/entities/zombie.json`, s vlastním kódem → `mods/mujmod/scripts/autorun/monsters.lua`
- nová zbraň → `mods/mujmod/weapons/railgun.json`
- nový HUD → `mods/mujmod/scripts/hud.lua`
- jiné shadery → `mods/mujmod/shaders/world.fs`

Mod aktivuješ v menu **Mods**, příkazem `mod mujmod` (nebo `project mujmod`) v konzoli, nebo `rbs --mod mujmod`. V editoru přes tlačítko **Project** v Hierarchy.

### Struktura modu

```
mods/mujmod/
  mod.json              nastavení projektu: název, startovní mapa, menu, hráč (volitelné, viz níže)
  game.cfg              herní proměnné, např. "sv_maxspeed 360" (ne nastavení hráče)
  entities/*.json       typy nepřátel a předmětů bez kódu ({"base", "description", "properties"})
  weapons/*.json        zbraně bez kódu (pole jako u rbs.weapon, volitelně "base")
  scripts/autorun/*.lua Lua kód, který se přidá k základní hře (spouští se po scripts/init.lua)
  maps/*.json           mapy (+ volitelně maps/<mapa>.lua se skriptem mapy)
  textures/ models/ sounds/ sprites/ decals/ hud/ fonts/ shaders/ lang/ prefabs/
  plugins/*.dll|so|dylib nativní pluginy (C, C++, Rust, C#…) – viz PLUGINS.md
```

### `mod.json`

Editor ho upravuje v dialogu **Project** (tlačítko v Hierarchy). Všechna pole jsou volitelná:

| Pole | |
|---|---|
| `title`, `description`, `author`, `version` | údaje o hře – menu Mods, titulek okna, patička menu |
| `start_map` | mapa pro *New game*, např. `maps/start.json` |
| `menu.title`, `menu.subtitle`, `menu.background` | nadpis, podnadpis a obrázek pozadí hlavního menu |
| `menu.show_editor`, `menu.show_mods` | jestli exportovaná hra nabídne v menu editor a stránku Mods |
| `player.health`, `player.max_health`, `player.armor` | hráč na začátku levelu |
| `player.start_weapons`, `player.select` | startovní zbraně (seznam id) a která je v ruce |
| `player.max_ammo`, `player.start_ammo` | limity a startovní munice podle druhu, např. `{"shells": 50}` |

Skripty ho čtou přes `rbs.project()` – můžeš si do něj přidat i vlastní nastavení své hry.

### `launch.json`

Leží vedle `base/` v **exportované hře** (Project → *Build game*) a říká, co se spustí: `{"project": "mujmod", "editor": false, "mods": false, "console": true}`. S ním se engine chová jako samostatná hra – rovnou otevře projekt, v menu je jen to, co povolíš, a data hledá vždy ve vlastní složce. Ve vývojové verzi enginu žádný `launch.json` není.

### Typy a zbraně bez kódu

`entities/<jméno>.json` vytvoří novou třídu entit z existující (`base`) se změněnými vlastnostmi; `weapons/<id>.json` novou zbraň (jako kopii zbraně `base`, nebo úplně od nuly). Načítají se po všech skriptech, takže můžou vycházet i z tříd napsaných v Lua nebo z jiných definic. Editor je vytváří a upravuje sám (Project → *New type* / *New weapon*), formát a příklady jsou v [GAME_CREATION.md](GAME_CREATION.md#3-nepřátelé) a [AI.md](AI.md#příklady-bez-kódu). Chyby (neznámý `base`, neplatný JSON) se vypíšou do konzole.

Ukázky: [`mods/example_mod`](../mods/example_mod) (granátomet, nový nepřítel, aréna – jen Lua) a [`mods/plugin_demo`](../mods/plugin_demo) (nativní pluginy).

## Formáty assetů

### Textury (`textures/<jméno>.png`)
- PNG, TGA, JPG, BMP, QOI. Mod může originál přepsat i v jiném formátu (`brick_red.tga` v modu nahradí `brick_red.png` v base). Na velikosti nezáleží; **1 pixel = 1 jednotka** při měřítku 1 (cihla 256×256 pokryje 256×256 jednotek). Měřítko/posun se nastaví per plocha v editoru.
- Průhlednost: alfa < 50 % se zahodí (mříže, ploty).
- Volitelný popis `textures/<jméno>.json`:
  ```json
  { "fullbright": true, "surface": "metal", "scroll": [0.1, 0] }
  ```
  `fullbright` = svítí (a v bounce osvětlení vyzařuje světlo), `scroll` = posun textury za sekundu.
- Speciální jména: `sky` (procedurální obloha), `clip` (neviditelná zeď), `trigger` (objem triggeru), `nodraw`.
- Ve hře přepínej filtrování `r_texfilter 0` (ostré pixely) / `1` / `2`.

### 3D modely (`models/*.glb`)
- **glTF 2.0** (.glb/.gltf) – doporučeno, včetně skeletální animace. Funguje také OBJ, IQM, M3D.
- Konvence: **Y nahoru, model kouká do +Z**, **počátek u nohou**, 1 jednotka ≈ 1 palec (postava ~56 vysoká).
- Animace podle jména: nepřátelé ze základní hry používají `idle`, `walk`, `attack`, `pain`, `death`.
- **Blender**: *File → Export → glTF 2.0*, formát *glb*, zapnout *+Y Up*, u animací *Always Sample Animations*. Textury mohou být vložené do .glb.
- Zbraně v ruce: hlaveň míří do +Z, počátek u rukojeti; pozici v obraze nastavíš ve `view_offset` zbraně.

### Zvuky (`sounds/`)
WAV, OGG, MP3, FLAC, QOA (hudba i XM/MOD). Pozicované zvuky by měly být mono. Smyčky (`ambient_sound`, vlastnost `loop_sound`) a hudba se streamují.

### Sprity a decaly
`sprites/*.png` (efekty, částice; animace = vodorovný pás snímků + vlastnost `frames`), `decals/*.png` (krev `blood1-4`, `bullet`, `scorch`) – RGBA.

### Fonty a překlady
`fonts/<jméno>.ttf|otf` (`hud`, `title`, `ui`, `ui_bold`, `mono`). Fonty se načítají včetně znaků latinky rozšířené (čeština, polština…).

Překlady: `lang/<kód>.json` mapuje anglický text na přeložený (`"New game": "Nová hra"`). Překládá se menu, nastavení, zprávy uprostřed obrazovky (`rbs.message`), cíle mise a vše, co skript prožene přes `rbs.tr("text")`. Jazyk se přepíná v *Nastavení* nebo `lang cs` v konzoli. Mod přidá vlastní řetězce souborem `lang/<kód>/<cokoli>.json` (např. `mods/example_mod/lang/cs/example_mod.json`) – nepřepíše tím základní překlad.

### Shadery
GLSL 330 v `shaders/` (`world`, `model`, `sky`, `unlit`, `post`). Po úpravě `reload_shaders` v konzoli.

## Mapy

Mapy jsou JSON soubory (`maps/*.json`, viz [MAP_FORMAT.md](MAP_FORMAT.md)) – tvoří se v editoru (`rbs --editor`). Osvětlení se peče při prvním spuštění a ukládá do `cache/`.

Skript mapy `maps/<mapa>.lua` může přidávat cíle mise, reagovat na triggery (`rbs.on("jmeno", fn)`) atd. – viz [SCRIPTING.md](SCRIPTING.md).

## Generátory zástupných assetů

V `tools/` jsou Python skripty, které vytvořily všechny výchozí assety (`gen_textures.py`, `gen_models.py`, `gen_sounds.py`, `gen_demo_map.py`). Hodí se jako ukázka formátů – např. `tools/gltf_writer.py` je jednoduchý zapisovač glTF s animacemi a `tools/mapgen.py` knihovna pro generování map z kódu.

## Sdílení modu

Zabal složku `mods/<mod>` (bez `cache/`) do zipu. Hráč ji rozbalí do `mods/` a vybere v menu. Nativní pluginy přibal přeložené pro cílové platformy, nebo nech hráče je sestavit.

Celou hru jako samostatnou aplikaci (engine + data, bez nutnosti cokoli instalovat) vytvoří **Build game** – viz [GAME_CREATION.md](GAME_CREATION.md#10-export-hry).
