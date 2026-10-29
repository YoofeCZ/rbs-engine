# Modování

## Jak fungují mody

Engine čte všechna data přes **virtuální souborový systém** ve vrstvách:

1. `mods/<aktivní mod>/` – nejvyšší priorita
2. `base/` – základní hra

Když mod obsahuje soubor se stejnou cestou jako `base/`, použije se ten z modu. Nové soubory se prostě přidají. Takže:

- nová textura → `mods/mujmod/textures/moje_cihla.png`
- přebarvená cihla → `mods/mujmod/textures/brick_red.png` (přepíše originál)
- nový nepřítel → `mods/mujmod/scripts/autorun/monsters.lua`
- nový HUD → `mods/mujmod/scripts/hud.lua`
- jiné shadery → `mods/mujmod/shaders/world.fs`

Mod aktivuješ v menu **Mods**, příkazem `mod mujmod` v konzoli, nebo `rbs --mod mujmod`.

### Struktura modu

```
mods/mujmod/
  mod.json              {"title", "description", "author"} – zobrazí se v menu Mods (volitelné)
  game.cfg              příkazy konzole při aktivaci, např. "game_start_map maps/start.json"
  scripts/autorun/*.lua Lua kód, který se přidá k základní hře (spouští se po scripts/init.lua)
  maps/*.json           mapy (+ volitelně maps/<mapa>.lua se skriptem mapy)
  textures/ models/ sounds/ sprites/ decals/ hud/ fonts/ shaders/ lang/ prefabs/
  plugins/*.dll|so|dylib nativní pluginy (C, C++, Rust, C#…) – viz PLUGINS.md
```

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
