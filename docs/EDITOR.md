# Editor map

![Editor](images/editor.jpg)

Editor spustíš příkazem `rbs --editor [mapa]`, z hlavního menu (**Editor map**), nebo příkazem `editor` v konzoli. Mapy se ukládají jako JSON (`maps/<jméno>.json`) do otevřeného projektu (`mods/<projekt>/`), nebo do `base/`, když žádný projekt otevřený není.

Jak v editoru udělat celou hru – projekt, nepřátelé, zbraně, nastavení a export – popisuje [GAME_CREATION.md](GAME_CREATION.md).

## Rozložení

| Panel | Co obsahuje |
|---|---|
| **Toolbar** (nahoře) | New / Open / Save / Save As, Undo / Redo, nástroje, Hollow / Subtract / To Entity, mřížka, **Bake Light**, **Play**, **Play here** |
| **Hierarchy** (vlevo) | Nahoře tlačítko **Project** s názvem otevřené hry (dialog projektu), pod ním nastavení mapy, entity podle kategorií (brushe entity jsou pod ní), brushe světa. Vyhledávání, dvojklik = zaměřit kameru, přetažení komponenty na řádek = přidání komponenty |
| **Pohledy** (uprostřed) | **Top**, **Front**, **Side** (2D, mřížka) a **Perspective** (3D s osvětlením a gizmem). `Tab` zvětší pohled pod myší na celou plochu |
| **Inspector** (vpravo) | Podle výběru: **entita** (třída, popis, Transform, Properties, **Components** + *Add Component*), **brush** (pozice, velikost, textura, operace), **plocha** (posun, měřítko, rotace textury, *Fit to face*, *Reset*), **typ entity nebo zbraň** kliknutá v Project (úprava bez kódu, viz níže), nic = **nastavení mapy** (obloha, slunce, ambient, mlha, kvalita lightmap, gravitace, hudba, další mapa) |
| **Project** (dole) | Entities, Weapons, **Animators** (graf animací, viz [ANIMATION.md](ANIMATION.md#editor-animátorů)), **Behaviors** (stromy chování pro AI, viz [BEHAVIOR.md](BEHAVIOR.md)), **Quests** a **Dialogues** (příběh jako graf, viz [QUESTS.md](QUESTS.md)), Components, Textures, Models, Sprites, Sounds, Prefabs, Maps, Scripts – vše přetažitelné do pohledů. Tlačítka *+ New type*, *+ New weapon*, *Reload scripts* a *Reload plugins* |
| **Console** (dole, záložka) | log a příkazová řádka (stejná jako ve hře) |

## Ovládání pohledů

| | 2D pohledy | 3D pohled |
|---|---|---|
| Posun | prostřední nebo pravé tlačítko + tah | prostřední tlačítko + tah |
| Zoom | kolečko | kolečko = pohyb dopředu/dozadu |
| Létání | – | držet pravé tlačítko: myš = rozhlížení, `WASD` + `Q`/`E` = pohyb, `Shift` = rychle |

## Nástroje

| Klávesa | Nástroj | Použití |
|---|---|---|
| `V` | **Select** | klik = výběr (`Shift` přidá/odebere), tažením v prázdnu = obdélníkový výběr, tažením výběru = posun, úchyty = změna velikosti, ve 3D gizmo se šipkami. Klik na brush entity vybere celou entitu, `Alt`+klik jen jeden brush |
| `B` | **Brush** | tažením ve 2D pohledu nakreslíš nový kvádr aktuální texturou; ve 3D klikni na plochu |
| `E` | **Entity** | klik umístí aktuální třídu (vyber ji v Project → Entities) |
| `T` | **Texture** | klik na plochu ve 3D ji obarví aktuální texturou, `Shift` = celý brush, `Alt` = nabrat texturu z plochy |
| `F` | **Face** | výběr ploch ve 3D; v Inspectoru pak posun / měřítko / rotace textury |
| `C` | **Clip** | dva body ve 2D pohledu určí řez, `Tab` přepíná režim (obě části / přední / zadní), `Enter` řízne vybrané brushe |

## Klávesové zkratky

| Zkratka | Akce |
|---|---|
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | nová / otevřít / uložit mapu (`Ctrl+Shift+S` = uložit jako) |
| `Ctrl+Z` / `Ctrl+Y` (`Ctrl+Shift+Z`) | zpět / znovu |
| `Ctrl+C` / `Ctrl+V` | kopírovat / vložit (přes schránku jako JSON – jde i mezi dvěma okny editoru) |
| `Ctrl+D` | duplikovat výběr |
| `Ctrl+A` | vybrat vše |
| `Ctrl+B` | upéct osvětlení a zobrazit ho ve 3D |
| `H` | **Hollow** – udělá z kvádru dutou místnost (tloušťka stěn = mřížka) |
| `R` / `Shift+R` | otočit výběr o 90° |
| `Delete` / `Backspace` | smazat výběr |
| `[` / `]` | jemnější / hrubší mřížka |
| šipky | posun výběru o mřížku v 2D pohledu pod myší |
| `Tab` | maximalizovat pohled pod myší |
| `Esc` | zrušit výběr (ve hře: návrat do editoru) |
| `F5` | **Play** – zahrát mapu od `player_start` |
| `F6` | **Play here** – zahrát z místa a směru 3D kamery |

## Postup: první level za 5 minut

1. `Ctrl+N` – nová mapa už obsahuje místnost, dvě světla a `player_start`.
2. **Project → Textures**: klikni na texturu (stane se aktuální).
3. `B` a v pohledu **Top** natáhni obdélník – vznikne kvádr. Výšku dolaď v pohledu **Front** úchyty.
4. `H` – kvádr se změní na dutou místnost. Průchod mezi místnostmi vyřízneš: nakresli kvádr přes zeď a dej **Subtract**.
5. **Project → Entities**: přetáhni `light` do pohledu, v Inspectoru nastav barvu, intenzitu a dosah. Pak `monster_grunt`, `item_health`, `weapon_shotgun`…
6. `Ctrl+B` – upečou se lightmapy se stíny a odraženým světlem, náhled je ve 3D pohledu.
7. `F5` – hraj. `Esc` tě vrátí do editoru.
8. `Ctrl+S` – ulož. Mapu pak spustíš z menu **Mapy** nebo `map <jméno>`.

## Import vlastních souborů (drag & drop)

Přetáhni soubory nebo celé složky ze svého průzkumníku souborů **přímo do okna editoru**. Zkopírují se do aktivního modu (nebo do `base/`) podle typu:

| Soubory | Cíl |
|---|---|
| `.png .tga .jpg .bmp .qoi` | `textures/` (když je otevřená složka Project → Sprites, tak `sprites/`) |
| `.glb .gltf .obj .iqm .m3d .vox` | `models/` – složka s `.gltf`/`.obj` se zkopíruje celá (`models/<složka>/`), aby model našel své textury |
| `.wav .ogg .mp3 .flac .qoa .xm .mod` | `sounds/` |
| `.ttf .otf` | `fonts/` |
| `.lua` | `scripts/autorun/` (načte se hned) |
| `.json` | podle obsahu do `maps/` (mapa) nebo `prefabs/` (prefab) |

Pustíš-li jeden model, sprite, zvuk nebo prefab nad pohledem, rovnou se tam i umístí; texturu puštěnou na plochu ve 3D na ni rovnou nanese. Stejně funguje příkaz konzole `import <soubor nebo složka>`. Soubor se stejným jménem se přepíše a textura se hned obnoví.

## Projekt hry

Tlačítko **Project** nahoře v Hierarchy otevře dialog projektu:

- **New project...** – nová hra v `mods/<složka>/` se startovní místností; hned se otevře.
- seznam projektů + **Open** – přepnutí na jinou hru (`base` = ukázková hra). Změny v uložené mapě se před přepnutím uloží; mapa, která ještě nikdy uložená nebyla, by se ztratila – dialog na to upozorní.
- **nastavení** – název, autor, verze, popis, startovní mapa, hlavní menu (nadpis, podnadpis, pozadí – přetáhni sprite, editor a Mods v exportované hře), hráč na startu levelu (zdraví, brnění, zbraně). **Save settings** zapíše `mod.json`.
- **Build game** – export samostatné hry do `builds/<projekt>/` ([GAME_CREATION.md](GAME_CREATION.md#10-export-hry)).

## Typy entit a zbraně bez kódu

Klikni v **Project → Entities** na typ (např. `monster_grunt`) nebo v **Project → Weapons** na zbraň – Inspector ukáže jeho vlastnosti:

- U typu napsaného v kódu: **Create a variant (no code)** / **Create a new weapon from this** vytvoří kopii, kterou jde měnit; **Place** přepne na nástroj Entity s tímto typem (u zbraní **Place its pickup**).
- U typu bez kódu (štítek *no-code*, soubor `entities/<jméno>.json` nebo `weapons/<id>.json`): všechny vlastnosti jdou upravit. Hodnoty s pevnou nabídkou mají rozbalovací seznam (`ai`, `attack_type`, `shot`…), najetí myší na název vlastnosti ukáže nápovědu, `x` vrátí hodnotu na výchozí ze základu a dole jde přidat další vlastnost. **Save** uloží soubor a změna se hned projeví na všech umístěných kusech, **Revert** ji zahodí, **Delete** soubor smaže (po potvrzení).

Tlačítka **+ New type** a **+ New weapon** vytvoří nový typ rovnou (jméno + z čeho vychází). Vlastnosti AI nepřátel popisuje [AI.md](AI.md).

Vlastnosti jednoho umístěného kusu měníš jako dřív – vyber ho v mapě a uprav Properties v Inspectoru; přebijí hodnoty typu.

## Entity se brushi (dveře, výtahy, triggery)

Vyber brushe a klikni na **To Entity** (zadáš třídu, např. `func_door`). Brushe se stanou součástí entity – pohybují se s ní a mají její vlastnosti. Hotové třídy: `func_door`, `func_plat`, `func_button`, `func_wall`, `func_illusionary`, `trigger_once`, `trigger_multiple`, `trigger_hurt`, `trigger_push`, `trigger_secret`, `trigger_changelevel`, `trigger_camera`. Tlačítko **Move to world** v Inspectoru brush zase vrátí do světa.

Propojování: entita s vlastností `target` aktivuje všechny entity, jejichž `targetname` se shoduje (tlačítko → dveře, trigger → monstra, `info_objective`…).

## Komponenty

Vyber entitu → Inspector → **Add Component**, nebo přetáhni komponentu z **Project → Components** na entitu ve 3D pohledu či v Hierarchy. Komponenty mohou být z Lua i z nativních pluginů (C/C++/Rust/C#) – v Inspectoru je u nich štítek `lua` / `native`. Vlastnosti komponenty se editují stejně jako vlastnosti entity, `x` komponentu odebere.

## Prefaby

Vyber skupinu brushů a entit → **Project → Prefabs → Save selection as prefab**. Prefab (`prefabs/<jméno>.json`) pak přetáhneš do libovolné mapy.

## Osvětlení

- `light` – bodové světlo (`color`, `intensity`, `radius`), `light_spot` – reflektor (`pitch`, úhel `spot`). `dynamic = 1` = světlo počítané v reálném čase (může blikat, hýbat se).
- Nastavení mapy: slunce (směr, barva, síla – svítí přes plochy s texturou `sky`), světlo z oblohy, síla odraženého světla (`bounce`), ambient occlusion, velikost luxelu (menší = ostřejší stíny, delší pečení).
- Textury s `"fullbright": true` (lampy, obrazovky) svítí samy.
- Hra si lightmapy upeče sama při prvním spuštění mapy a uloží do `cache/`.

## Skripty a pluginy

**Project → Scripts** – klik otevře skript ve výchozím textovém editoru systému. Po úpravě klikni na **Reload scripts**: nové třídy a komponenty se hned objeví v Project a Inspectoru. Po přeložení nativního pluginu klikni na **Reload plugins**.

Lua API: [SCRIPTING.md](SCRIPTING.md), vlastní AI: [AI.md](AI.md), nativní pluginy: [PLUGINS.md](PLUGINS.md).

## Grafy (animátory, behavior trees, questy, dialogy)

Všechny čtyři grafy se ovládají stejně:

| Co | Jak |
|---|---|
| Vybrat uzel | klik; **Shift+klik** přidá / odebere, **tažení po prázdném plátně** nakreslí výběrový rámeček, **Ctrl+A** vybere vše |
| Přesunout | tažení uzlu; tažení uvnitř výběru posune celou skupinu |
| Posun pohledu | **Space + tažení**, prostřední nebo pravé tlačítko; kolečko zoomuje; **Fit** vycentruje |
| Spojit | tažení z tečky (portu) na pravém okraji uzlu na další uzel |
| Smazat | **Delete** smaže celý výběr (spojení vybrané šipky také) |
| Zpět / znovu | **Ctrl+Z**, **Ctrl+Shift+Z** nebo **Ctrl+Y**, tlačítka *Undo* / *Redo* nahoře (u otevřeného grafu platí pro něj, na záložce Level pro mapu). Tažení uzlů, tažení čísla nebo napsané slovo je jeden krok; *Revert* jde vrátit také |
| Kopírovat | **Ctrl+C**, **Ctrl+X**, **Ctrl+V**, **Ctrl+D** (duplikovat): kopírují se uzly i spojení mezi nimi (spojení na uzly mimo výběr se zahodí), vložené uzly dostanou nová jména / čísla a jsou vybrané. Schránka přežije restart editoru a jde vložit i do jiného projektu; vložit jde jen do stejného druhu grafu |

