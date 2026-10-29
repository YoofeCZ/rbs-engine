# Animátory

Animátor je **stavový automat, který vybírá animace modelu** (jako Animator v Unity). Bez něj entita přehrává jednu animaci jménem (`self:play("walk")`). S animátorem řekneš jen *co se děje* (rychlost, zásah, útok) a animátor sám vybere klip, plynule ho **prolne** s předchozím a pošle **události** v daných okamžicích klipu.

Animátor je soubor `animators/<jméno>.json` v `base/` nebo ve složce hry. Ukázky: `base/animators/grunt.json` a `fiend.json`. Upravuje se **v editoru jako graf** (viz níže), nebo ručně v textu.

## Editor animátorů

V editoru otevři **Project → Animators**: dvojklik na animátor ho otevře v záložce uprostřed (vedle záložky **Level**), **+ New animator** založí nový (idle ↔ walk podle rychlosti, ať je od čeho začít).

- **Graf stavů**: stav je uzel, přechod je šipka. Pravý klik na prázdno = nový stav (nebo stav rovnou z klipu modelu), na stavu = *Make transition*, *Set as default state*, *Delete state*, na šipce = smazat. Přechod vytvoříš i tažením za tečku na pravém okraji stavu na jiný stav. Uzly se přesouvají, pohled se posouvá Space + tažením nebo prostředním / pravým tlačítkem, kolečko zoomuje, **Fit** vycentruje graf, **Del** maže výběr; výběr více stavů, kopírování a Ctrl+Z jsou popsané v [EDITOR.md](EDITOR.md#grafy-animátory-behavior-trees-questy-dialogy). **Any State** je uzel vlevo nahoře: přechody z něj platí z každého stavu.
- **Parameters** (panel vlevo): parametry animátoru i vestavěné (`speed`, `dead`…). Hodnoty se dají měnit živě, trigger tlačítkem **Fire**; klik na jméno otevře jeho definici v Inspectoru (jméno, typ, počáteční hodnota).
- **Preview** (panel vpravo): model přehrává animátor, prolínání se vidí na modelu i v grafu (zelený okraj = aktivní stav, tečka na šipce = probíhající přechod); události se ukážou textem a přehraje se jejich zvuk. Model pro náhled (a z něj seznam klipů) se vybírá v Inspectoru, když nic nevybereš; u animátorů, které už používá nějaký typ entity, se najde sám. Tažením v náhledu se model otáčí.
- **Inspector** vpravo: u **stavu** jméno, klip, smyčka, rychlost, 1D blend a události; u **přechodu** délka prolnutí, exit time a podmínky; u **parametru** jméno, typ a hodnota.
- **Save** (nebo Ctrl+S) zapíše soubor do `animators/` projektu; polohy uzlů se ukládají do klíče `editor` v tom samém souboru a engine je ignoruje. Zavření záložky s neuloženými změnami vyžaduje druhý klik na ×.

Přiřazení entitě rukou: vyber entitu a v Inspectoru nastav vlastnost **`animator`** (dropdown se soubory z Animators), nebo přetáhni animátor z **Project → Animators** na entitu ve 3D pohledu. Vlastnost mají všechny příšery a `prop_model`.

## Použití u entity

Entitě s modelem nastav vlastnost **`animator`** na cestu k souboru, například `animators/grunt.json`:

- v editoru v Inspectoru (u nepřátel je to vlastnost `animator` vestavěné AI),
- v definici třídy: `props = { animator = "animators/grunt.json" }`,
- za běhu v Lua: `self.animator = "animators/grunt.json"` (prázdný řetězec animátor odpojí).

Vestavěná AI (`monster_base`) s animátorem posílá jen triggery **`attack`** a **`pain`**; chůze se řídí automatickým parametrem `speed` a smrt parametrem `dead`. `self:play("attack")` u entity s animátorem proto jen spustí trigger `attack`.

## Formát souboru

```json
{
  "default": "idle",
  "parameters": [
    {"name": "pain",   "type": "trigger"},
    {"name": "attack", "type": "trigger"}
  ],
  "states": [
    {"name": "idle",  "clip": "idle"},
    {"name": "walk",  "clip": "walk", "speed_param": "speed", "speed_ref": 150},
    {"name": "attack", "clip": "attack", "loop": false,
     "events": [{"time": 0.4, "name": "hit", "sound": "sounds/grunt_fire.wav"}]},
    {"name": "death", "clip": "death", "loop": false}
  ],
  "transitions": [
    {"from": "*",    "to": "death", "duration": 0.1, "conditions": [{"param": "dead", "op": ">", "value": 0.5}]},
    {"from": "*",    "to": "attack", "conditions": [{"param": "attack", "op": "trigger"}]},
    {"from": "idle", "to": "walk", "conditions": [{"param": "speed", "op": ">", "value": 12}]},
    {"from": "walk", "to": "idle", "conditions": [{"param": "speed", "op": "<", "value": 12}]},
    {"from": "attack", "to": "idle", "exit_time": 1}
  ]
}
```

### Parametry

`parameters` deklaruje vlastní parametry: `name`, `type` (`float`, `int`, `bool`, `trigger`) a počáteční `value`. **Trigger** je jednorázový: platí do dalšího snímku a pak se zahodí (nebo ho spotřebuje přechod, který ho používá).

Engine každý snímek sám nastavuje: **`speed`** (vodorovná rychlost), **`vertical_speed`**, **`on_ground`** (0/1), **`dead`** (0/1) a **`health`**. Nemusíš je deklarovat.

### Stavy

| Klíč | Výchozí | Význam |
|---|---|---|
| `name` | | jméno stavu |
| `clip` | = `name` | jméno animace v modelu (glTF) |
| `loop` | `true` | smyčka; jinak se po konci drží poslední póza |
| `speed` | 1 | rychlost přehrávání |
| `speed_param`, `speed_ref` | | rychlost se násobí `hodnota_parametru / speed_ref` (chůze podle rychlosti) |
| `blend_param`, `blend` | | 1D prolnutí: `"blend": [{"clip": "idle", "at": 0}, {"clip": "walk", "at": 150}]` míchá klipy podle parametru (místo `clip`) |
| `events` | | události v klipu, viz níže |

### Přechody

| Klíč | Význam |
|---|---|
| `from` | stav, nebo `"*"` / `"any"` = z libovolného stavu (smrt, zásah) |
| `to` | cílový stav |
| `duration` | délka prolnutí v sekundách (výchozí 0,15) |
| `exit_time` | přechod až po dosažení tohoto normalizovaného času stavu (1 = konec klipu) |
| `conditions` | všechny musí platit: `param` + `op` (`>`, `<`, `>=`, `<=`, `==`, `!=`, `true`, `false`, `trigger`) + `value` |

Přechody z `*` se zkoušejí jako první, pak přechody aktuálního stavu, vždy v pořadí v souboru – **smrt dej nahoru**. Stav se z `*` znovu spustí sám sebou jen triggerem (zásah během ucuknutí spustí `pain` znovu); podmínka na obyčejný parametr ho nerestartuje.

### Události

`{"time": 0.4, "name": "hit", "sound": "sounds/x.wav", "volume": 1}` – `time` je normalizovaný čas klipu (0–1). Zvuk se přehraje na entitě bez kódu; `name` zavolá callback `anim_event(self, name)` třídy nebo komponenty:

```lua
rbs.class("my_monster", {
  anim_event = function(self, name)
    if name == "hit" then self:attack_now() end
  end,
})
```

U smyčkových stavů se událost spustí při každém průchodu.

## Lua API

| | |
|---|---|
| `self:anim_param(name, value?)` / `rbs.anim_param(ent, name, value?)` | přečte nebo nastaví parametr (bool jako `true`/`false`) |
| `self:anim_trigger(name)` / `rbs.anim_trigger(ent, name)` | spustí trigger |
| `self:anim_state()` / `rbs.anim_state(ent)` | jméno stavu a jeho normalizovaný čas (`nil` bez animátoru) |
| `self.animator` | cesta k animátoru (zápis ho vymění) |

## Poznámky

- Animace v modelu se hledají podle jména bez ohledu na velikost písmen. Chybějící klip se nepřehraje; chybějící soubor animátoru se zapíše do logu (`Missing animator`).
- Soubory animátorů se při načtení mapy načtou znovu, takže úpravy projdou po restartu mapy.
- Editor je vestavěný, ale změny v editoru se projeví ve hře až po novém spuštění mapy (Play), animátory se načítají při startu mapy.
