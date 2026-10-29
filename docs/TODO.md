# Rozpracováno / co zbývá

Poslední commit (`WIP: animator runtime`) se staví a smoke test prochází, ale je to jen jádro animátorů.

## Animátory (rozdělané)
Hotovo (C++/Lua): `src/render/animation.*` (vzorkování, prolínání póz, CPU skinning), `src/game/animator.*` (parsování `animators/*.json`, stavy, 1D blend, přechody s podmínkami / exit time / Any State, crossfade, události), vlastnost entity `animator`, Lua `rbs.anim_param`, `rbs.anim_trigger`, `rbs.anim_state`, callback `anim_event(self, name)`.

Zbývá:
- žádný ukázkový animátor v `base/animators/` (grunt, fiend); `monster_base` by měl mít vlastnost `animator` a posílat triggery `pain` / `attack` místo `self:play(...)`
- **nic z toho nebylo vyzkoušené ve hře** (jen se to překládá) – ověřit prolínání, události, `Entity:play` s animátorem (volá `rbs.anim_trigger`)
- editor: složka Animators v Project, graf stavů, Inspector, náhled
- dokumentace `docs/ANIMATION.md`, odkazy v README a SCRIPTING.md
- test v `tools/smoke_test.sh`

## Uzlový editor grafů (nezačato)
Generický widget (pan/zoom, uzly s piny, propojení, mazání, kontextové menu) a záložky dokumentů ve středu editoru. Použít pro animátory, behavior trees a questy.

## Behavior trees + blackboardy (nezačato)
`behaviors/*.json`, runtime v Lua (sequence, selector, parallel, dekorátory, podmínky, akce), blackboard nepřítele i globální, vnímání (vidí / slyší / ztratil hráče), `ai = behavior` u nepřátel, vlastní uzly z Lua, ladicí zobrazení, editor.

## Questy jako graf (nezačato)
`quests/*.json`: uzly start, objective (kill / pickup / use / target / var), branch, choice, say, join, akce, end; události, přenos stavu mezi mapami, HUD s úkoly, dialogové okno s volbami, entity `quest_start` / `set_var` / `npc`, editor.
