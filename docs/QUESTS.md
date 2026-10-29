# Questy a dialogy (grafy)

Příběh hry se dá poskládat z grafů bez kódu:

- **Quest** (`quests/<jméno>.json`) – úkoly hráče: zabij 3 gruntů, seber předmět, dojdi k páce; větvení, odměny, další questy.
- **Dialog** (`dialogues/<jméno>.json`) – rozhovor s postavou: řádky, odpovědi na výběr, podmínky, akce.

Oba se upravují v editoru jako graf (**Project → Quests / Dialogues**) a mají stejné uzly pro logiku a akce. Ukázka: mod **`quest_demo`** (`rbs --mod quest_demo`): v aréně stojí stráž, se kterou se mluví (`dialogues/guard.json`) a která dá quest `quests/clear_arena.json`.

## Jak graf funguje

Graf je sada uzlů, každý má **výstupní porty** (`next`, u Branch `yes` / `no`, u Choice jeden port na odpověď) propojené s dalšími uzly. Tok začíná v uzlu **Start**. Uzel se buď dokončí hned a pošle tok dál portem, nebo **čeká** (cíl čeká na zabití, řádek dialogu na klávesu). Z jednoho portu může vést víc spojení: pak běží **všechny větve najednou** a uzel **Join** počká, až dorazí všechny. Quest skončí uzlem **End** (success / fail) nebo když nic dalšího nečeká.

### Uzly

| Skupina | Uzly |
|---|---|
| **Flow** | `Start`, `End`, `Branch` (yes/no podle podmínky), `Chance` (yes/no náhodně), `Set variable`, `Join`, `Delay` |
| **Objective** (jen quest) | `Kill` (třída, počet), `Pick up` (třída, počet), `Use / reach target` (čeká, až se spustí target), `Wait for variable` |
| **Action** | `Message`, `Give` (zbraň, náboje, klíč, zdraví, brnění), `Fire targets`, `Spawn`, `Play sound`, `Start quest`, `Change map`, `Dialogue`, `Log` |
| **Dialogue** (jen dialog) | `Say` (kdo, text), `Choice` (odpovědi, každá má svůj port a volitelně proměnnou, která musí platit) |

`Branch` se ptá na: proměnnou (`==`, `<`…), stav questu (`quest active` / `done` / `failed`), klíč, zbraň nebo zdraví v %. Texty se překládají přes jazykové soubory a mohou obsahovat `{proměnná}`, která se nahradí hodnotou.

Objektiv **Kill** počítá i potomky třídy (`monster_grunt` počítá i `monster_heavy`); prázdná třída = jakákoli příšera. **Use / reach target** čeká na jméno targetu, které spustí tlačítko, trigger nebo uzel `Fire targets`.

### Proměnné

Proměnné jsou **společné všem questům a dialogům a přežijí změnu mapy** (jména jako `met_guard`, `gold`). Nenastavená proměnná se rovná `0` / `false` / prázdnému textu. V editoru je panel **Variables** (počáteční hodnoty a seznam proměnných, které graf používá). Z map je nastavuje entita `set_var`.

### Přenos mezi mapami

Stav questů (které běží, kolikátý je kdo zabitý, hotové / neúspěšné) a proměnné se ukládají při každé změně a po načtení další mapy se obnoví. Nová hra začíná od nuly.

## Editor

**Project → Quests** (nebo **Dialogues**): dvojklik otevře graf v záložce, **+ New quest** / **+ New dialogue** založí nový. Ovládání je jako u [animátorů](ANIMATION.md#editor-animátorů) a [behavior trees](BEHAVIOR.md#editor):

- Pravý klik na plátno = **Add node** (Flow / Objective / Action / Dialogue), na uzel = smazat.
- Spojení: tažením z tečky (portu) na pravém okraji uzlu na další uzel. Do Startu nic nevede, z End nic nevede.
- Inspector: jméno uzlu, typ, parametry podle typu (výběr třídy, zbraně, mapy, zvuku, questu, dialogu; u proměnných i seznam známých), u **Choice** seznam odpovědí (text, volitelná podmínka `needs var`, přidat / odebrat). Bez výběru: název, seznam problémů (neznámý uzel, port bez cíle, nedosažitelné uzly).
- **Arrange** rozloží graf zleva doprava, **Save** (Ctrl+S) uloží.

## Jak dialog vypadá a hraje (nastavení, kamera, text)

Dialog se nepíše jen jako text: má **mluvčí**, **styl okna** a každý řádek může mít vlastní **záběr kamery**, **animaci** a **hlas**. V editoru je vpravo **Preview**: 3D scéna se záběrem kamery, postavou, dialogovým oknem a textem, který se píše – tentýž Lua kód, který kreslí hru, takže náhled ukazuje přesně to, co uvidí hráč. Klikni na uzel **Say** nebo **Choice** (náhled ukáže jeho řádek), **Replay** pustí psaní znovu, **Next >** jde po první větvi na další řádek, odpovědi v náhledu jdou **kliknout** a následovat. **Max** náhled zvětší.

### Mluvčí (Inspector, když nic není vybrané → *Speakers*)

| Položka | Význam |
|---|---|
| `id`, `name` | jak se mluvčí volá v uzlech a co se píše nad textem |
| `entity` | jméno targetu postavy v mapě (npc): kamera ji natáčí, otáčí se k hráči. Prázdné = npc, které dialog spustilo; `player` = hráč |
| `model` | model pro náhled v editoru (prázdné = model entity z mapy podle `entity`) |
| `portrait`, `emotions` | obličej vedle textu (`sprites/…`); řádek si vybere jiný portrét polem `emotion` |
| `color`, `voice`, `pitch` | barva jména; zvuk, který hraje jako „blipy“ při psaní textu, a jeho výška |
| `head` | výška obličeje nad nohama pro kameru (0 = automaticky podle modelu) |

### Styl okna (Inspector → *Style of the dialogue box*)

Pozice (`bottom` / `top` / `center`), šířka, velikost písma, **rychlost psaní** (`text_speed`, znaků za sekundu; 0 = celý řádek hned), barvy okna a textu, hlasitost blipů, strana a velikost portrétu, **letterbox** (černé pruhy při záběru), zda se vůbec používá kamera, výchozí záběr a pohyb, zda se postava otáčí k hráči a zda se při záběru skryje HUD. Tlačítko *Reset style* vrátí výchozí hodnoty.

### Řádek (uzly Say a Choice)

| Parametr | Význam |
|---|---|
| `shot` | `auto` (výchozí ze stylu) · `none` (normální pohled hráče) · `close-up` (obličej) · `medium` (po hrudník) · `full` (celé tělo) · `over-shoulder` (přes rameno hráče) · `profile` (z boku) · `wide` (postava v prostředí) |
| `motion` | pomalý pohyb kamery: `push-in`, `pull-out`, `orbit`, `none`, `auto` |
| `emotion` | který portrét mluvčího se ukáže |
| `animation` | animace postavy při řádku (trigger jejího animátoru nebo animace modelu, např. `attack` jako gesto); po řádku se vrátí `idle` |
| `voice` | zvuk přehraný s řádkem (nahraná věta) |
| `speed` | rychlost psaní jen pro tento řádek |
| `auto` | po kolika sekundách po dopsání jde dál samo (0 = čeká na hráče) |

Další uzly: **Camera** (nastaví záběr od teď, bez řádku, na kohokoli) a **Animate** (spustí animaci postavy). Řádek se píše postupně; **Space / Enter / E / klik** nejdřív dopíše text, druhé stisknutí jde dál.

## Spuštění ve hře

| Co | Jak |
|---|---|
| Postava, se kterou se mluví | entita **`npc`** (kategorie *story*): vlastnost `dialogue`, model / animátor jako u jiných entit; hráč se na ni podívá a stiskne **E** (use) |
| Spuštění questu | uzel `Start quest` (z dialogu nebo jiného questu), entita **`quest_start`** (`auto = 1` při startu mapy, nebo po aktivaci / triggeru), z Lua `rbs.flow.start_quest("jméno")` |
| Proměnná z mapy | entita **`set_var`** (`key`, `value`, `op`), spouští se triggerem / tlačítkem |

Dialog zastaví hráče a uvolní kurzor (a podle stylu přepne kameru). Řádek dopíše a přeskočí **Space / Enter / E** nebo klik, odpovědi se volí klávesami **1–9**, šipkami a Enter, nebo myší. Úkoly aktivních questů se vypisují vpravo nahoře.

## Ladění

Konzole (`~`):

- `quest_debug 1` – loguje události (`kill`, `pickup`, `fire`…) a každé vstoupení do uzlu,
- `quest dump` – běžící questy a jejich čekající uzly, aktuální dialog a proměnné,
- `quest start <quest>`, `quest event kill monster_grunt`, `quest var <klíč> [hodnota]`, `quest say <dialog>`, `quest next`, `quest pick <n>`, `quest reset`, `quest map <mapa>`, `quest selftest`.

## Vlastní uzly v Lua

```lua
rbs.flow_node{
  id = "heal_all", category = "action", title = "Heal everyone", desc = "Vyléčí hráče.",
  scopes = {"quest", "dialogue"},          -- kde se nabízí
  params = { { name = "amount", type = "number", default = 50 } },
  enter = function(inst, node, st)
    local p = rbs.player()
    if p then p.health = math.min(100, p.health + node.params.amount) end
    return "next"                          -- port, kterým tok pokračuje; nil = uzel čeká
  end,
  -- volitelně: event(inst, node, st, druh, a, b), update(inst, node, st, dt), hud(inst, node, st)
}
```

`ports` může být seznam jmen (`{"yes", "no"}`), `{}` pro uzel, který tok ukončuje, nebo `"options"` (jako Choice). Typy parametrů: `number`, `bool`, `string`, `text`, `enum` (`values`), `class`, `weapon`, `map`, `sound`, `quest`, `dialogue`, `var`. Události, které engine posílá běžícím questům: `kill` (třída), `pickup` (třída), `fire` (jméno targetu), `var` (klíč), `quest` (jméno), `dialogue_end` (jméno). Zdroj vestavěných uzlů je `base/scripts/quests.lua`.
