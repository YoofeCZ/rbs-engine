# Behavior trees (AI jako graf)

Místo vestavěných režimů (`chase`, `ranged`…) může příšera řídit **strom chování**: graf uzlů, který každý snímek rozhodne, co dělat – dívat se, hledat, útočit, utíkat. Strom se skládá z uzlů a upravuje se v editoru; je to soubor `behaviors/<jméno>.json`. Ukázky: `base/behaviors/soldier.json` (totéž co vestavěné `chase`, jako strom) a `ambusher.json` (uhýbá, hlídá, budí sousedy). Vyzkoušet jde v modu `behavior_demo` (`rbs --mod behavior_demo`).

## Jak strom funguje

Každý snímek se spustí **kořen**. Každý uzel odpoví jedním ze tří výsledků: **success** (povedlo se), **failure** (nepovedlo se), **running** (ještě pracuje, například jde k cíli).

| Druh | Uzly | Co dělá |
|---|---|---|
| **Composite** (víc dětí) | `Sequence`, `Selector`, `Parallel` | Sequence spouští děti popořadě a selže u prvního neúspěchu. Selector zkouší děti odshora a vezme první, které funguje; **vyhodnocuje se každý snímek**, takže vyšší priorita přeruší nižší. Parallel spustí všechny najednou. |
| **Decorator** (jedno dítě) | `Inverter`, `Succeeder`, `Failer`, `Repeat`, `Cooldown`, `Timeout` | Mění výsledek nebo způsob spuštění dítěte. |
| **Condition** | `Can see`, `Has target`, `Distance`, `Health`, `Compare`, `Chance`, `Heard something`, `Lost target` | Ptá se na stav; success = platí. |
| **Action** | `Wait`, `Idle`, `Move to`, `Flee from`, `Strafe`, `Face`, `Attack`, `Patrol`, `Set value`, `Animation trigger`, `Play sound`, `Alert neighbors`, `Log` | Něco udělá. |

Děti Sequence a Selectoru se spouštějí **shora dolů podle svislé polohy v grafu** (pořadí je u uzlu vidět jako `#1`, `#2`…). Parametry uzlů mají nápovědu (najeď na jméno).

U **Sequence** je zapnuté `memory` (pokračuje u dítěte, které běželo) a `recheck` (dokud běží pozdější dítě, kontroluje se, jestli pořád platí podmínky před ním; přestane-li, přeruší se). Přerušené uzly se uklidí (například `Flee from` zastaví příšeru).

## Blackboard

Každá příšera má tabulku hodnot (blackboard), kterou uzly čtou a zapisují podle jména. Engine udržuje tyto klíče sám:

| Klíč | Význam |
|---|---|
| `target` | nepřítel, o kterém ví (hráč) |
| `can_see` | vidí ho (zorné pole `sight_range`, přímá viditelnost) |
| `distance` | vzdálenost k hráči |
| `last_seen_pos`, `last_seen_time` | kde a kdy ho viděla naposledy |
| `heard_pos`, `heard_time` | odkud a kdy slyšela střelbu |
| `health_pct` | zdraví v procentech |
| `alerted` | všimla si hráče (viděla, slyšela, dostala zásah) |

Vlastní hodnoty stromu (jméno + počáteční hodnota) se píšou do panelu **Blackboard** v editoru. Jméno s předponou **`global.`** (například `global.alarm`) je **sdílené všemi příšerami**. Uzel `Set value` zapisuje, `Compare` porovnává.

Příšera **slyší** výstřely hráče do 1600 jednotek (`rbs.noise`). Zásah nebo spuštěný trigger ji také upozorní.

## Editor

**Project → Behaviors**: dvojklik otevře strom v záložce, **+ New behavior** založí nový. Ovládání je stejné jako u [animátorů](ANIMATION.md#editor-animátorů):

- Pravý klik na plátno = **Add node** (rozdělené na Composite / Decorator / Condition / Action); pravý klik na uzel = **Add child** (nový uzel rovnou připojený) nebo smazat.
- Spojení: tažením za tečku na pravém okraji uzlu na dítě. Uzel má jen jednoho rodiče (nové spojení staré nahradí), Decorator má jedno dítě, list nemůže mít děti a strom nesmí vést zpátky sám do sebe.
- Uzel **Root** vlevo: spojením z něj se určí první uzel stromu.
- Inspector: jméno uzlu, typ (stejného druhu), parametry podle typu, děti a jejich pořadí. Když nic nevybereš, ukáže se seznam problémů (uzel bez dětí, nepřipojené uzly, neznámý typ).
- **Arrange** rozloží strom zleva doprava, **Fit** ho vycentruje, **Save** (Ctrl+S) uloží.

## Použití u příšery

Vlastnost **`behavior`** (Inspector, dropdown; nebo přetáhni strom z Project → Behaviors na příšeru ve 3D pohledu) přepne `ai` na `behavior`. Bez kódu to jde i u typů vytvořených v **Project → Entities → New type**.

Vestavěná AI dál obstarává ucuknutí při zásahu (`pain`) a švih útoku; **uzel `Attack`** používá útok příšery (`attack_type`, `attack_delay`, `attack_time`, poškození… z jejích vlastností) a selže, dokud se příšera „vzpamatovává“ (`attack_cooldown`) nebo cíl není v dosahu.

## Ladění

V konzoli hry (`~`):

- `bt_debug 1` – vlevo nahoře ukáže u příšer v okolí, **kterou větví stromu právě jdou** (`Soldier > Chase > Move to`),
- `bt_debug 2` – navíc do konzole píše každou změnu větve a slyšené zvuky,
- `bt_selftest` – spustí test uzlů (`BT SELFTEST PASS n`).

## Vlastní uzly v Lua

```lua
rbs.bt_node{
  id = "shout", kind = "action", title = "Shout", desc = "Křikne a uspěje.",
  params = {
    { name = "text", type = "string", default = "!" },
    { name = "radius", type = "number", default = 600 },
  },
  run = function(self, p, ctx)
    rbs.log(p.text)
    for _, m in ipairs(rbs.find_radius(self.pos, p.radius)) do
      if m.monster and m ~= self then rbs.bt.alert(m, rbs.player()) end
    end
    return "success"            -- nebo "failure" / "running" (condition může vrátit true / false)
  end,
  reset = function(self, p, ctx) end,   -- volitelné: uzel byl přerušen
}
```

`kind` je `action` nebo `condition`. Typy parametrů: `number`, `bool`, `string`, `enum` (s `values`) a `key` (jméno hodnoty v blackboardu; čte se `rbs.bt.get(self, jméno)`, zapisuje `rbs.bt.set`). V `run` je `ctx.dt` (čas snímku) a `ctx.mem`, vlastní tabulka uzlu, která se smaže, když uzel přestane běžet. Nový uzel se hned objeví v nabídce editoru (po **Reload scripts** v Project). Zdroj vestavěných uzlů je `base/scripts/behavior.lua`.
