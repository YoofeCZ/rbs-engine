# Nativní pluginy – C, C++, Rust, C#, Zig, Go…

Kromě Lua můžeš herní kód psát v libovolném jazyce, který umí vytvořit sdílenou knihovnu (`.dll` / `.so` / `.dylib`) s C exporty. Engine pluginy načte a jejich **třídy entit a komponenty** se chovají úplně stejně jako ty z Lua: objeví se v editoru (Project, Add Component), lze je kombinovat na jedné entitě a volají se ze stejných událostí.

```
sdk/include/rbs_plugin.h     C API (stabilní ABI, verzované)
sdk/include/rbs_plugin.hpp   C++ vrstva: rbs::Behaviour, RegisterEntity<T>(), RBS_PLUGIN_INIT
sdk/examples/c_plugin        čisté C          -> c_beacon
sdk/examples/cpp_plugin      C++ + CMake      -> cpp_turret, cpp_spinner
sdk/examples/rust_plugin     Rust (cdylib)    -> rust_heal_station, rust_hover
sdk/examples/csharp_plugin   C# NativeAOT     -> csharp_launch_pad, csharp_pulse_light
sdk/build_examples.sh|.ps1   sestaví všechny příklady do mods/plugin_demo/plugins/
```

## Jak to funguje

1. Plugin exportuje `int rbs_plugin_init(const rbs_api* api)` (a volitelně `void rbs_plugin_shutdown(void)`).
2. V `rbs_plugin_init` zaregistruje třídy (`api->register_class`) a komponenty (`api->register_component`) jako strukturu `rbs_behaviour` s ukazateli na funkce (`create`, `destroy`, `spawn`, `update`, `touch`, `use`, `damaged`, `pain`, `die`, `blocked`) a volitelným JSON popisem pro editor.
3. Pro každou instanci engine zavolá `create(user, entity, props_json)` – vrátíš svoje data (objekt), která pak dostáváš v každém callbacku.
4. Uvnitř callbacků používáš funkce z `rbs_api`: čtení/zápis vlastností entit (`get_float`, `set_vec`, `set_string`… se **stejnými názvy jako v Lua**: `pos`, `vel`, `health`, `model`, `anim`, `solid`…), `spawn`, `trace`, `damage`, `projectile`, `explosion`, `sound`, `particles`, `message`, `objective` a další.

Pluginy se hledají v `base/plugins/` a `mods/<mod>/plugins/` (aktivní mod). Engine načítá **kopii** knihovny, takže ji můžeš přeložit znovu a v editoru kliknout na **Reload plugins** (Project → Scripts) nebo napsat `reload_plugins` do konzole (mimo běžící hru).

Entity se odkazují **číselným id** (`rbs_entity`); id se během mapy nikdy nerecyklují, takže je bezpečné si je pamatovat a ověřit `api->exists(id)`.

## C++

```cpp
#include "rbs_plugin.hpp"
using namespace rbs;

struct Turret : Behaviour {
    float nextShot = 0;
    void Spawn() override {
        self.SetModel("models/weapons/w_plasma.glb");
        self.SetSolid("bbox");
        self.SetHealth(PropF("health", 80));      // vlastnost z editoru (nebo výchozí)
    }
    void Update(float dt) override {
        Entity p = Player();
        if (!self.CanSee(p)) return;
        self.Face(p.Center(), 120, dt);
        if (Time() > nextShot) {
            nextShot = Time() + 0.25f;
            api->projectile("plasma_bolt", self.id, self.Pos(), (p.Center() - self.Pos()).Norm(), 1100);
        }
    }
    void Die(Entity attacker, float) override { api->explosion(self.Center(), 140, 60, attacker.id); self.Remove(); }
};

RBS_PLUGIN_INIT {
    RegisterEntity<Turret>("cpp_turret", R"({"category":"monsters","props":{"health":80}})");
}
```
Sestavení: `cmake -S sdk/examples/cpp_plugin -B build-plugin && cmake --build build-plugin --config Release`.

## Rust

```rust
mod rbs;   // sdk/examples/rust_plugin/src/rbs.rs – zkopíruj si ho do svého projektu
use rbs::{Behaviour, Ctx, Vec3};

struct Hover { base_y: Option<f32> }
impl Behaviour for Hover {
    fn update(&mut self, ctx: &Ctx, _dt: f32) {
        let mut p = ctx.this.pos();
        let base = *self.base_y.get_or_insert(p.y);
        p.y = base + (rbs::time() * 2.0).sin() * ctx.prop_f32("height", 8.0);
        ctx.this.set_pos(p);
    }
}

#[no_mangle]
pub unsafe extern "C" fn rbs_plugin_init(api: *const rbs::Api) -> i32 {
    if !rbs::init(api) { return 1; }
    rbs::register_component("rust_hover", r#"{"props":{"height":8}}"#, || Box::new(Hover { base_y: None }));
    0
}
```
`Cargo.toml` musí mít `crate-type = ["cdylib"]`. Sestavení: `cargo build --release`, výsledek je `target/release/librust_plugin.so` (`rust_plugin.dll` na Windows).

## C# (.NET 8 NativeAOT)

C# se přeloží **ahead-of-time do nativní knihovny** – hráč nepotřebuje nainstalovaný .NET.

```csharp
public sealed class LaunchPad : Behaviour {
    public override void Spawn() { Self.SetStr("solid", "trigger"); Self.Set("light_radius", 140); }
    public override void Touch(Entity other, Vec3 n) {
        if (other.Id == Engine.Player.Id) other.Vel = new Vec3(0, PropF("power", 750), 0);
    }
}

public static unsafe class Plugin {
    [UnmanagedCallersOnly(EntryPoint = "rbs_plugin_init", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Init(Api* api) {
        Engine.A = api;
        Engine.RegisterClass("csharp_launch_pad", """{"props":{"power":750}}""", () => new LaunchPad());
        return 0;
    }
}
```
Projekt potřebuje `<PublishAot>true</PublishAot>` a `<NativeLib>Shared</NativeLib>` (viz `CsharpPlugin.csproj`). Sestavení: `dotnet publish -c Release -r linux-x64 -o bin/publish` (nebo `win-x64`, `osx-arm64`); knihovna je pak v `bin/publish/`. Výstupní složku zadávej vždy – bez ní ji „Developer PowerShell for VS“ přesune jinam (nastavuje `Platform=x64`).

## C

```c
#include "rbs_plugin.h"
static const rbs_api* api;

static void beacon_touch(void* data, rbs_entity self, rbs_entity other, rbs_vec3 n) {
    if (other == api->player()) { api->message("Hello from C!", 2); api->remove(self); }
}

RBS_EXPORT int rbs_plugin_init(const rbs_api* a) {
    api = a;
    static rbs_behaviour b;
    b.touch = beacon_touch;
    b.editor_json = "{\"category\":\"effects\"}";
    api->register_class("c_beacon", &b);
    return 0;
}
```
`cc -shared -fPIC -O2 -Isdk/include hello.c -o c_hello.so`

## Další jazyky

Cokoli s C FFI: **Zig** (`export fn rbs_plugin_init(api: *const c.rbs_api) c_int`, `@cImport` hlavičky), **Go** (`go build -buildmode=c-shared`, `//export rbs_plugin_init`), **Odin**, **Nim**, **D**, **Pascal**… Stačí přepsat struktury `rbs_api` a `rbs_behaviour` ve stejném pořadí polí jako v `rbs_plugin.h`.

## Popis pro editor (`editor_json`)

```json
{
  "desc": "Plasma turret",
  "category": "monsters",
  "color": [80, 140, 255],
  "mins": [-14, -8, -14], "maxs": [14, 20, 14],
  "model": "models/weapons/w_plasma.glb",
  "brush": false,
  "props": { "range": 900, "fire_rate": 0.25 }
}
```
Výchozí `props` se zobrazí v Inspectoru a předají se do `create()` (spolu s hodnotami nastavenými v editoru).

## Pravidla

- Callbacky běží v hlavním vlákně enginu; z jiných vláken `rbs_api` nevolej.
- Řetězce z enginu (`props_json`) platí jen během volání – zkopíruj si je.
- `editor_json` musí žít po celou dobu běhu pluginu (literál nebo vlastní kopie).
- Kompatibilita: zkontroluj `api->version >= RBS_PLUGIN_API_VERSION`. Nové funkce se přidávají jen na konec `rbs_api`.
- Pozor na bezpečnost: nativní plugin běží s plnými právy procesu. Pouštěj jen pluginy, kterým věříš (Lua skripty jsou naopak sandboxované).
