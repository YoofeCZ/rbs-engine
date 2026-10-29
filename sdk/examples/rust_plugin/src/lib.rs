//! Example RBS Engine plugin in Rust: a healing station and a hover component.
//!
//! Build:  cargo build --release
//! Copy target/release/librust_plugin.so (.dll / .dylib) into mods/plugin_demo/plugins/
mod rbs;
use rbs::{Behaviour, Ctx, Entity, Vec3};

/// Heals the player while they stand close to it, with a pulsing green light.
struct HealStation {
    pool: f32,
    next_sound: f32,
}

impl Behaviour for HealStation {
    fn spawn(&mut self, ctx: &Ctx) {
        let e = ctx.this;
        self.pool = ctx.prop_f32("pool", 100.0);
        e.set_str("model", "models/items/medkit.glb");
        e.set("scale", 2.0);
        e.set_vec("mins", Vec3::new(-18.0, 0.0, -18.0));
        e.set_vec("maxs", Vec3::new(18.0, 24.0, 18.0));
        e.set_str("solid", "bbox");
        e.set("light_radius", 160.0);
    }

    fn update(&mut self, ctx: &Ctx, dt: f32) {
        let e = ctx.this;
        let pulse = 0.6 + 0.4 * (rbs::time() * 3.0).sin();
        let strength = if self.pool > 0.0 { pulse } else { 0.1 };
        e.set_vec("light_color", Vec3::new(0.2 * strength, 1.2 * strength, 0.3 * strength));
        let p = rbs::player();
        if self.pool <= 0.0 || !p.valid() || p.get("dead") != 0.0 || e.distance(p) > 70.0 {
            return;
        }
        let max = p.get("max_health");
        let hp = p.health();
        if hp >= max {
            return;
        }
        let amount = (25.0 * dt).min(self.pool).min(max - hp);
        p.set_health(hp + amount);
        self.pool -= amount;
        if rbs::time() > self.next_sound {
            self.next_sound = rbs::time() + 0.6;
            rbs::sound("sounds/pickup.wav", e.pos(), 0.4, 1.4);
            rbs::particles(e.pos() + Vec3::new(0.0, 30.0, 0.0), Vec3::new(0.0, 1.0, 0.0), 6, 0x40FF60FF, 60.0, 4.0, 0.8);
        }
        if self.pool <= 0.0 {
            rbs::message("The healing station is empty", 2.0);
        }
    }
}

/// Component: makes any entity hover up and down.
struct Hover {
    base_y: Option<f32>,
}

impl Behaviour for Hover {
    fn update(&mut self, ctx: &Ctx, _dt: f32) {
        let e: Entity = ctx.this;
        let mut p = e.pos();
        let base = *self.base_y.get_or_insert(p.y);
        p.y = base + (rbs::time() * ctx.prop_f32("speed", 2.0)).sin() * ctx.prop_f32("height", 8.0);
        e.set_pos(p);
    }
}

#[no_mangle]
pub unsafe extern "C" fn rbs_plugin_init(api: *const rbs::Api) -> i32 {
    if !rbs::init(api) {
        return 1;
    }
    rbs::log("Rust example plugin loaded");
    rbs::register_class(
        "rust_heal_station",
        r#"{"desc": "Healing station written in Rust (sdk/examples/rust_plugin).",
            "category": "items", "color": [60, 255, 120], "mins": [-18, 0, -18], "maxs": [18, 24, 18],
            "model": "models/items/medkit.glb", "props": {"pool": 100}}"#,
        || Box::new(HealStation { pool: 100.0, next_sound: 0.0 }),
    );
    rbs::register_component(
        "rust_hover",
        r#"{"desc": "Hovers the entity up and down (Rust component).", "props": {"height": 8, "speed": 2}}"#,
        || Box::new(Hover { base_y: None }),
    );
    0
}

#[no_mangle]
pub extern "C" fn rbs_plugin_shutdown() {}
