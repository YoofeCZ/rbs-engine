# Cutscény (grafy)

Cutscéna je filmová scéna složená z kamerových záběrů, pohybu a animací postav, prolínaček a titulků. Skládá se v editoru jako graf (**Project → Cutscenes**, soubor `cutscenes/<jméno>.json`) a běží na stejném flow enginu jako [questy a dialogy](QUESTS.md), takže funguje všechno, co znáš odtud: paralelní větve z jednoho portu, `Join`, `Delay`, `Branch`, `Set variable`, `Fire targets`, `Play sound`, `Dialogue`…

Ukázka: mod **`quest_demo`** – `cutscenes/intro.json` (zkus v konzoli `quest cutscene intro`).

## Co se děje, když scéna běží

- hráč se nehýbe ani nestřílí a (výchozí) ho nelze zranit; příšery jedou dál
- HUD se schová a nahoře a dole se objeví černé pruhy
- **Space / Enter dvakrát za sebou** scénu přeskočí: všechno, co čeká, se dokončí hned a postavy skončí tam, kam by je scéna dovedla
- po konci se kamera plynule vrátí k hráči, zbraň a HUD se vrátí, prolínačka do černé, která v grafu zůstala, sama odezní

Chování se nastavuje v Inspectoru (klikni na prázdné místo grafu → **How the cutscene plays**): *Black bars*, *Skippable*, *Hide HUD*, *Invulnerable*.

## Cíle (kdo / kam)

Parametry `who`, `to` a `look_at` berou:

| Zápis | Význam |
|---|---|
| `player` | hráč |
| `guard` | entita v mapě s tímhle `targetname` |
| `0 150 -100` | bod `x y z` |

V editoru je u pole malé menu se jmény entit z otevřené mapy.

## Uzly (skupina Cutscene)

| Uzel | Co dělá | Parametry |
|---|---|---|
| **Camera shot** | natáčí postavu nebo bod záběrem jako v dialozích | `who`, `shot` (close-up, medium, full, over-shoulder, profile, wide), `motion` (none, push-in, pull-out, orbit), `seconds`, `blend` (rychlost přeletu, 0 = střih) |
| **Camera move** | přeletí kamerou na místo | `to`, `look_at` (prázdné = směr, kterým se dívá cílová entita), `fov` (0 = beze změny), `seconds`, `ease` (smooth / linear) |
| **Animate** | spustí postavě animaci | `who`, `animation` |
| **Move character** | postava dojde na místo (při přeskočení se tam přenese) | `who`, `to`, `speed`, `face` |
| **Fade** | prolínačka do černé / zpět | `to` (black / clear), `seconds` |
| **Title text** | text na obrazovce na pár vteřin | `text`, `seconds`, `position`, `size` |

Uzel **Cutscene** (skupina Action) v questu nebo dialogu pustí scénu a graf pokračuje, až skončí (nebo ji hráč přeskočí).

Uzel s `seconds = 0` jen nastaví stav a hned pokračuje (např. `Fade` do černé na začátku scény).

## Jak scénu pustit

- entita **`cutscene_start`** v mapě: `cutscene` (soubor), `auto = 1` (hned po startu mapy), jinak ji spustí `use` / target; `once = 1` si pamatuje, že už hrála (ukládá se do savu)
- uzel **Cutscene** v questu nebo dialogu
- konzole: `quest cutscene <jméno>`, přeskočení `quest skip`
- z Lua: `rbs.flow.start_cutscene("intro")`

Hraje vždy jen jedna scéna najednou.

## Editor

**Project → Cutscenes** → dvojklik otevře graf, **+ New cutscene** založí novou (Fade → Camera shot → Fade → End). Ovládání je stejné jako u ostatních grafů (pravý klik = Add node, tažení z portu = spojení, Ctrl+Z / C / V, výběr rámečkem).

U uzlů **Camera shot** a **Camera move** je v Inspectoru tlačítko **Look through this camera**: 3D pohled se přesune tam, odkud by záběr (u pohybu jeho konec) natáčel, a vidíš, co kamera uvidí. Záběr se počítá stejným kódem jako ve hře.

Pozor: v editoru nejde scénu přehrát celou, jen nahlédnout kamerou; přehrát ji jde ve hře (F6 / konzole).

## Vlastní uzly

Stejně jako u questů přes `rbs.flow_node{...}`; `scopes = {"cutscene"}` omezí uzel na cutscény. Uzel může mít `skip = function(inst, node, state)` – volá se při přeskočení a má dát věci do koncového stavu.

## Test

`quest selftest` obsahuje kontroly cutscén (běh, přeskočení, paralelní větve, Join, vrácení kamery a hráče). Kód: `base/scripts/cutscene.lua`.
