# Formát mapy (`maps/*.json`)

Mapa je čitelný JSON. Většinou ho vytváří editor, ale dá se psát ručně nebo generovat skriptem (viz `tools/mapgen.py`).

## Souřadnice a jednotky

- **Y míří nahoru**, X a Z tvoří vodorovnou rovinu.
- 1 jednotka ≈ 1 palec: hráč je široký 32 a vysoký 56 jednotek (oči ve výšce ~ 46), dveře mívají 96–128.
- Úhly jsou ve stupních. `angle` (yaw) 0 = pohled do +X, 90 = do −Z, 180 = do −X, 270 = do +Z.

## Kostra souboru

```json
{
  "format": "rbs-map",
  "version": 1,
  "settings": { ... },
  "brushes": [ ... ],
  "entities": [ ... ]
}
```

## `settings`

Vše je volitelné; chybějící klíče mají výchozí hodnotu.

| Klíč | Výchozí | Význam |
|---|---|---|
| `title`, `author` | `"Untitled"`, `""` | název a autor |
| `sky_zenith`, `sky_horizon`, `sky_clouds` | tmavě červená, oranžová, hnědá | barvy procedurální oblohy (RGB 0–1) |
| `sun_dir` | `[-0.4, -0.6, -0.3]` | směr slunečních paprsků |
| `sun_color`, `sun_intensity` | `[1, 0.7, 0.45]`, `0` | barva a síla slunce (0 = bez slunce). Slunce svítí jen tam, kam „vidí“ přes plochy `sky` |
| `ambient` | `[0.12, 0.1, 0.1]` | minimální světlo všude |
| `fog_color`, `fog_density` | `[0.2, 0.08, 0.05]`, `0` | mlha (0 = vypnutá; typicky 0.0002–0.002) |
| `lightmap_scale` | `8` | jednotek na jeden luxel lightmapy (menší = ostřejší stíny) |
| `sky_light` | `0.35` | ambientní světlo z viditelné oblohy |
| `bounce` | `1` | síla odraženého světla (0 = vypnuto) |
| `ao` | `0.6` | síla ambient occlusion (0 = vypnuto) |
| `gravity` | `800` | gravitace (jednotek/s²) |
| `music` | `""` | hudba na pozadí (`sounds/music.ogg`) |
| `next_map` | `""` | mapa po `trigger_changelevel` bez vlastního cíle (`"*"` = zpět do menu) |

## Brushe

Brush je **konvexní těleso** ohraničené rovinami (jako v Quake). Každá plocha je rovina s **normálou mířící ven** – bod `p` leží uvnitř, když pro všechny plochy platí `dot(n, p) − d ≤ 0`.

```json
{ "faces": [
    { "n": [0, 1, 0],  "d": 0,    "tex": "concrete", "scale": [2, 2] },
    { "n": [0, -1, 0], "d": 16,   "tex": "concrete" },
    { "n": [1, 0, 0],  "d": 512,  "tex": "brick_red", "off": [32, 0], "rot": 90 },
    ...
] }
```

| Klíč plochy | Význam |
|---|---|
| `n`, `d` | normála (jednotkový vektor ven) a vzdálenost roviny od počátku |
| `tex` | jméno textury bez přípony (`textures/<tex>.png`). Speciální: `sky`, `clip`, `trigger`, `nodraw` |
| `off` | posun textury v texelech `[u, v]` |
| `scale` | měřítko `[u, v]` (2 = textura dvakrát větší) |
| `rot` | rotace textury ve stupních |

Textury jsou zarovnané ke světu: plocha se promítne na rovinu kolmou k ose, ke které má normála nejblíž, a 1 texel = 1 jednotka (při měřítku 1). Díky tomu na sebe textury sousedních brushů navazují.

### Zkratka `box`

Kvádr jde zapsat stručně – hodí se pro ruční psaní a generátory:

```json
{ "box": [[-512, -16, -512], [512, 0, 512]], "tex": "concrete",
  "faces_tex": { "+y": "floor_checker", "-y": "concrete_dark" } }
```

`faces_tex` přepíše texturu jednotlivých stran (`+x`, `-x`, `+y`, `-y`, `+z`, `-z`). Editor mapu ukládá vždy v plné podobě s rovinami.

## Entity

```json
{
  "class": "monster_grunt",
  "origin": [128, 0, -256],
  "angle": 90,
  "props": { "health": 60, "targetname": "ambush1" },
  "components": [
    { "type": "rotator", "props": { "speed": 45 } },
    { "type": "cpp_spinner", "props": {} }
  ]
}
```

| Klíč | Význam |
|---|---|
| `class` | třída entity (z Lua `rbs.class` nebo z nativního pluginu) |
| `origin` | pozice – počátek kolizního boxu entity (`mins`/`maxs` třídy jsou relativní k němu). Editor staví nové entity rovnou na podlahu |
| `angle` | natočení (yaw) ve stupních |
| `props` | libovolné vlastnosti – čísla, texty, barvy `[r, g, b]` (0–255), vektory jako text `"0 96 0"`. Skript je čte přes `self.props`, plugin přes `props_json` / `get_prop` |
| `components` | seznam komponent: `type` = jméno komponenty, `props` = její vlastnosti |
| `brushes` | brushe patřící entitě (dveře, výtahy, triggery) – stejný formát jako v `brushes` mapy |

Obecné vlastnosti jsou `targetname` (jméno, na které lze cílit) a `target` (koho aktivovat). Ostatní (`message`, `delay`, `model`, `scale`, `speed`…) závisí na třídě – výchozí hodnoty i popis třídy ukazuje Inspector.

## Prefaby (`prefabs/*.json`)

Stejný formát jako mapa, jen s `"format": "rbs-prefab"`, bez `settings` a se souřadnicemi relativně k bodu vložení. Editor je ukládá tlačítkem *Save selection as prefab* a stejný JSON používá i pro kopírování přes schránku.

## Soubory kolem mapy

| Soubor | Význam |
|---|---|
| `maps/<mapa>.lua` | skript mapy, načte se automaticky (viz [SCRIPTING.md](SCRIPTING.md)) |
| `cache/<mapa>.lmcache` | upečené lightmapy; vytváří se samo a při změně mapy se přepočítá. Do repozitáře ani modu ho nedávej |
