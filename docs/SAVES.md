# Ukládání her

Hra se dá uložit kdykoli, kdy hráč žije a neběží dialog: z pauzy (**Esc → Save game**), klávesou **F6** (rychlé uložení), **F9** (rychlé načtení), z konzole (`save [slot]`, `load [slot]`, `saves`) nebo ze skriptu. Hlavní menu nabízí **Continue** (nejnovější uložená hra) a **Load game**.

## Sloty

| Slot | Kdo ho píše |
|---|---|
| `auto` | engine, **při vstupu na každou mapu** |
| `quick` | F6 / `save` bez jména |
| `1` … `10` | hráč v nabídce *Save game* |
| libovolné jméno | konzole / skript (`save boss_fight`) |

Soubory jsou JSON v `saves/<projekt>/<slot>.json` vedle enginu (u exportované hry vedle hry); složka `saves/` je ve `.gitignore`. V nabídce slot ukazuje název mapy, datum, čas ve hře a zdraví. Uložit jde jen do hry stejného projektu; načtení uloženého z jiného projektu engine odmítne. Zápis je atomický (celý soubor se napíše a pak přejmenuje), takže pád nenechá napůl uloženou hru.

## Co se ukládá

- **Hráč:** pozice, pohled, rychlost, zdraví, brnění, zbraně, náboje, klíče, vybraná zbraň.
- **Entity:** každá entita mapy a skriptů (pozice, rychlost, zdraví, mrtvá / živá, viditelnost, model, animace, poloha dveří a plošin, barva…). Co se od uložení **odstranilo** (sebraný předmět, rozstřílená příšera), se po načtení odstraní znovu; co skripty **vytvořily za hry**, se vytvoří znovu. Přechodné věci (střely, kousky těl, efekty s časovačem) se neukládají.
- **Pole ze skriptů:** hodnoty, které skripty drží na entitě (`self.state`, `self.fired`, časovače…) se uloží a vrátí; viz níže.
- **Příběh:** stav questů (které běží, počty zabití…) a proměnné, úkoly na obrazovce, statistiky mapy a čas.

Entity se po načtení hledají podle čísla, které jim mapa a skripty dávají vždy ve stejném pořadí. Když po uložení změníš mapu nebo skripty tak, že se pořadí změní, engine to do logu napíše (`Save game: the map or the scripts changed…`) a některé věci nemusí sedět.

## Co v Lua

Skripty **nic dělat nemusí**: všechna prostá pole entity (čísla, text, true / false, vektory a tabulky z nich) se ukládají sama. Nedá se uložit **odkaz na jinou entitu** (příšera zapomene, koho honila), funkce a vnitřní položky enginu. Třída může v `loaded(self)` nastavit, co z toho potřebuje, po návratu uložených polí:

```lua
rbs.class("my_turret", {
  loaded = function(self)
    self.target = rbs.player()     -- odkazy se po načtení musí dohledat znovu
  end,
})
```

| Funkce | Co dělá |
|---|---|
| `rbs.save_game(slot)` | uloží hru (po konci snímku); například checkpoint z triggeru: `rbs.save_game("checkpoint")` |
| `rbs.load_game(slot)` | načte hru (po konci snímku) |

## Omezení

- Stav, který skript drží **mimo entity** (globální tabulky `rbs.*`, vlastní knihovny), se neuloží; ukládej ho do entit, nebo do hodnot kvestů (`rbs.flow.set`), které se ukládají.
- Probíhající dialog se uložit nedá (uložení se odmítne).
- Nativní pluginy (C / C++ / Rust / C#) si stav ukládat samy neumí; entity jejich tříd dostanou jen to, co ukládá engine.
