# Rozpracováno / co zbývá

Poslední commit (`WIP: animator runtime`) se staví a smoke test prochází, ale je to jen jádro animátorů.

## Animátory (hotové včetně editoru)
Hotovo a **ověřeno ve hře** (macOS, 2026-10-29): jádro (`src/render/animation.*`, `src/game/animator.*`), vlastnost entity `animator`, Lua API, `anim_event`, ukázkové animátory grunt a fiend, napojení `monster_base`. Hotový je i **editor** v záložce uprostřed (`src/editor/editor_animator.cpp`, graf `src/ui/graph.*`): Project → Animators, graf stavů a přechodů, panel parametrů, živý náhled modelu, Inspector stavu / přechodu / parametru, přiřazení entitě (dropdown `animator`, drag z Projectu). Dokumentace `docs/ANIMATION.md`.

Ověřeno snímky skrytého editoru a skriptovanými příkazy (`animator open|create|add_state|link|param|save|dump…`): vykreslení, otevření / vytvoření / uložení, datové operace. **Neověřeno automaticky: myš v grafu** (tažení uzlů, tažení přechodu za tečku, kontextové menu, zoom, Delete) a dialog *New animator* – nejde je spustit bez vstupu z myši; vyzkoušej je ručně.

Zbývá:
- animační události (`events`, `anim_event`, zvuk události) a 1D blend (`blend_param`) ve **hře** nejsou vyzkoušené (v editoru se přehrávají v náhledu)
- tlačítko pro přehrání v Play ze záložky; po uložení se změna projeví až po novém startu mapy
- (hotovo 2026-10-29) výběr více uzlů, kopírování, vkládání, Ctrl+Z – viz EDITOR.md, Grafy

## Uzlový editor grafů (základ hotový)
Generický widget (`src/ui/graph.*`: pan/zoom, uzly, propojení tažením, mazání, kontextové menu) a záložky dokumentů ve středu editoru (`Document` v `editor_impl.h`) jsou hotové a používá je editor animátorů. Pro behavior trees a questy chybí: piny na uzlech s víc vstupy/výstupy (strom = hrany rodič → dítě v pořadí), výběr více uzlů, undo.

## Behavior trees + blackboardy (hotové)
Hotovo (2026-10-29): runtime v Lua `base/scripts/behavior.lua` (sequence, selector, parallel, 6 dekorátorů, 8 podmínek, 13 akcí, blackboard příšery i `global.`, vnímání vidí / slyší / ztratil, vlastní uzly přes `rbs.bt_node`), `ai = behavior` + vlastnost `behavior` u příšer, vzorové stromy `soldier` a `ambusher`, demo mod `behavior_demo`, ladění `bt_debug 1|2`, test uzlů `bt_selftest` (39 kontrol), editor stromů v záložce (`src/editor/editor_behavior.cpp`: graf, Add node / Add child menu, blackboard panel, Inspector s parametry z knihovny, Arrange, validace), Project → Behaviors, dropdown `behavior`, drag na příšeru. Dokumentace `docs/BEHAVIOR.md`.

Ověřeno: testem uzlů, během příšer ve hře (cesty ve stromu, slyšení výstřelu přes `fire`, uložení stromu editorem a jeho běh ve hře), snímky editoru, datovými operacemi přes příkazy `behavior …`. **Neověřeno automaticky:** myš v grafu stromu (tažení, vnořené menu *Add node*, Delete), dialog *New behavior*, automatické nastavení `ai = behavior` při výběru stromu v Inspectoru; ve hře nebyly vyzkoušené uzly `Flee from` (jen v testu semantiky), `Patrol`, `Parallel`, `Alert neighbors` v hustším souboji ani vlastní uzel z Lua z dokumentace.

Zbývá: ladění přímo v editoru (živé zvýraznění aktivní větve bez spuštění hry), undo, výběr více uzlů, kopírování podstromů, blackboard s typy a vlastními entitami, Lua `Condition` s dekorátory typu „abort lower priority“.

## Questy a dialogy jako graf (hotové)
Hotovo (2026-10-29): graf widget s **víc výstupními porty** (`ui::graph`, `Node::ports`), společný flow engine v Lua `base/scripts/quests.lua` (tokeny na uzlech, paralelní větve + Join, události kill / pickup / fire / var / quest / dialogue_end, proměnné sdílené mezi questy a dialogy, **uložení stavu mezi mapami** přes `rbs.persist_*` v C++), uzly (Start, End, Branch, Chance, Set variable, Join, Delay, Kill, Pick up, Use/reach target, Wait for variable, Message, Give, Fire targets, Spawn, Play sound, Start quest, Change map, Dialogue, Log, Say, Choice), HUD s úkoly a dialogové okno (klávesy, myš), zmrazení hráče při dialogu (`rbs.freeze`), entity `npc`, `quest_start`, `set_var`, hooky na zabití (`rbs.on_kill`) a sebrání předmětu, vlastní uzly přes `rbs.flow_node`, editor v záložkách (`src/editor/editor_flow.cpp`: Project → Quests / Dialogues, menu Add node, panel Variables, Inspector s parametry z knihovny a editorem odpovědí, Arrange, validace), dropdowny `quest` / `dialogue`, drag z Projectu, demo mod `quest_demo`, ladění `quest_debug 1` a konzole `quest …`, test `quest selftest` (36 kontrol). Dokumentace `docs/QUESTS.md`.

Ověřeno: testem semantiky, během v demo modu (dialog → volba → quest → skutečné zabití střelbou a sebrání předmětu → dokončení → změněný dialog), přenosem stavu přes změnu mapy, snímky dialogového okna (řádek i výběr), HUD s úkoly a editoru, uložením grafu z editoru a jeho během ve hře. **Neověřeno:** myš v grafu (tažení z konkrétního portu, vnořené menu, Delete), dialog *New quest / New dialogue*, skutečné ovládání dialogu klávesnicí a myší (v testech řízeno konzolí), použití NPC klávesou E, uzly `Give` (zbraň, náboje), `Spawn`, `Change map` přes samotný uzel, objektiv `Use / reach target` s reálným tlačítkem, vlastní uzel z dokumentace.

### Prezentace dialogů (hotové)
Hotovo (2026-10-29): `base/scripts/dialogue.lua` – **mluvčí** (jméno, barva, entita v mapě, portrét + emoce, hlas s výškou, výška hlavy), **styl okna** (pozice, šířka, písmo, rychlost psaní, barvy, portrét, letterbox, kamera, HUD…), **záběry kamery** (`close-up`, `medium`, `full`, `over-shoulder`, `profile`, `wide`) s pomalým pohybem (`push-in`, `pull-out`, `orbit`), text, který se píše se zvukovými blipy, animace postavy na řádku, uzly **Camera** a **Animate**, otáčení postavy k hráči; v C++ `rbs.camera_set / camera_clear` (plynulý přechod, návrat k hráči) a `Script::CallJson`; v editoru **živý náhled** (3D scéna se záběrem, postavou, oknem a psaním textu, Next / Replay / Max, klikací odpovědi) a **Speakers + Style** v Inspectoru. Náhled i hra kreslí stejným Lua kódem. Testy: `quest selftest` (53 kontrol včetně matematiky záběrů, psaní a stavu náhledu).

Ověřeno: snímky ve hře (close-up, medium, over-shoulder, profile s orbitem, wide, výběr odpovědí, letterbox, skrytý HUD, psaní textu) a v editoru (náhled řádku i výběru, Max, nastavení). **Neověřeno:** myš v náhledu (Next / Replay / Max, klik na odpověď), portréty a emoce (demo nemá obrázky), hlasová věta `voice` na řádku, `auto` posun, uzly Camera a Animate ve hře, mluvčí `player`, použití NPC klávesou E, kamera v mapě bez `entity` (spouštěno konzolí bez NPC).

Zbývá: ladění přímo v editoru bez spuštění hry (grafy), undo a více uzlů najednou, hráč jako postava v záběru (over-shoulder teď ukazuje jen mluvčího), překlady textů z editoru, nápověda „Press E to talk" u NPC, titulky / textové efekty (třes, pauza).

## Úpravy grafů: undo, výběr, schránka (hotové)
Společné pro všechny čtyři grafy (`editor_graph_edit.cpp`, výběr ve `ui::graph`): snímkový undo/redo (až 100 kroků, tahy myší a psaní se slučují do jednoho kroku), výběr více uzlů (Shift, rámeček, Ctrl+A), tažení skupiny, hromadné mazání, Ctrl+C / X / V / D se zachováním spojení mezi kopírovanými uzly a systémová schránka. Ověřeno skriptovanými příkazy (`… undo|redo|commit|copy|paste|duplicate|select_idx|delete_selected|undo_depth`) na behavior stromu, animátoru, dialogu i questu (počty, unikátní jména, platná spojení, odmítnutí vložení do jiného druhu grafu, přesná obnova dokumentu) a snímkem výběru. **Neověřeno automaticky:** samotná myš (výběrový rámeček, Shift-klik, tažení skupiny, Space + tažení) a klávesové zkratky v běžícím editoru.
Regresní test dialogu: `quest after`, `quest walk`, `rbs.camera_active` a dialog `auto_demo` hlídají, že se po dialogu uvolní skriptovaná kamera (smoke test `quest_camera_release`; ověřeno, že při rozbitém uvolnění selže).

## Cutscény jako graf (hotové)
Hotovo (2026-10-29): `base/scripts/cutscene.lua` na flow enginu (paralelní větve, Join, Delay… fungují i tam): uzly *Camera shot* (stejné záběry jako v dialozích), *Camera move*, *Animate*, *Move character*, *Fade*, *Title text* a akce *Cutscene* v questech / dialozích; cíle `player` / `targetname` / bod `x y z`; letterbox, schovaný HUD, nezranitelný hráč, přeskočení dvojitým Space / Enter (uzly mají `skip`, postavy skončí v koncovém stavu), plynulé vrácení kamery (`rbs.camera_*`); entita `cutscene_start`; editor: Project → Cutscenes, graf, Inspector (styl scény), pole `entity` s menu jmen z mapy, tlačítko *Look through this camera* (konzole `flow look`); konzole `quest cutscene` / `quest skip`; dokumentace `docs/CUTSCENES.md`.

Ověřeno: `quest selftest` (72 kontrol, z toho cutscény: běh, jen jedna najednou, návrat kamery a zranitelnosti, odeznění prolínačky, paralelní větve + Join, přeskočení; kontrola přeskočení spadne, když se rozbije `skip` uzlu Fade), intro v quest_demo do přirozeného konce i s přeskočením (kamera vypnutá, HUD a zbraň zpět, fade 0), snímky editoru (graf, Inspector, pohled kamerou), starší scénáře (BT test, quest, uvolnění kamery, save / load, editory) bez regrese.

Neověřeno automaticky: klikání myší v grafu a na tlačítko *Look through…* (testuje se přes konzoli), dvojitý Space / Enter ve hře, `cutscene_start` v mapě, smoke skript na Linuxu (běželo se ručně po scénářích na macOS).

Zbývá: časová osa (timeline) místo grafu, přehrání celé scény přímo v editoru, zvuk / hudba jako uzly, překlady titulků, vlastní kamery jako entity s dráhou.

## Ukládání her (hotové)
Hotovo: sloty (autosave při vstupu na mapu, quicksave F6 / F9, 10 slotů, libovolná jména), nabídky *Save game* / *Load game* a *Continue*, konzole `save` / `load` / `saves`, `rbs.save_game` / `rbs.load_game`; ukládá se hráč, všechny entity (včetně odstraněných a nově vzniklých), pole z Lua (`rbs.save_fields` / `load_fields`, hook `loaded`), stav questů a proměnné, úkoly, statistiky; atomický zápis. Dokumentace `docs/SAVES.md`.

Ověřeno: dvěma procesy (stav v jednom, načtení v druhém: quest, proměnná, pozice, zabitý grunt a sebraný předmět), na `e1m1` (25 KB, příšery, úkoly), snímky nabídek, testem polí (`quest selftest`), smoke testem. **Neověřeno:** klávesy F6 / F9 a klikání v nabídkách myší, uložení uprostřed boje s projektily, dveře a plošiny v pohybu, nativní pluginy, exportovaná samostatná hra (cesta `saves/`).
Zbývá: náhledový obrázek ve slotu, potvrzení přepsání slotu, ukládání stavu jednotlivých komponent, odkazy mezi entitami (vlastník střely, cíl příšery), uložení probíhajícího dialogu, třídy bez `loaded` s odkazy na jiné entity.

