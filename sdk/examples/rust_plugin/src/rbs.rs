//! Rust bindings for the RBS Engine plugin API (mirrors sdk/include/rbs_plugin.h).
//!
//! The raw `#[repr(C)]` structs match the C header field by field; the safe layer
//! below (`Entity`, `Behaviour`, `register_class`, ...) is what plugins normally use.
#![allow(dead_code)]

use std::ffi::{c_char, c_void, CStr, CString};

pub const API_VERSION: u32 = 1;
pub type EntityId = i32;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const fn new(x: f32, y: f32, z: f32) -> Self { Vec3 { x, y, z } }
    pub fn len(self) -> f32 { (self.x * self.x + self.y * self.y + self.z * self.z).sqrt() }
    pub fn norm(self) -> Vec3 {
        let l = self.len();
        if l > 0.0 { self * (1.0 / l) } else { Vec3::default() }
    }
}
impl std::ops::Add for Vec3 { type Output = Vec3; fn add(self, o: Vec3) -> Vec3 { Vec3::new(self.x + o.x, self.y + o.y, self.z + o.z) } }
impl std::ops::Sub for Vec3 { type Output = Vec3; fn sub(self, o: Vec3) -> Vec3 { Vec3::new(self.x - o.x, self.y - o.y, self.z - o.z) } }
impl std::ops::Mul<f32> for Vec3 { type Output = Vec3; fn mul(self, s: f32) -> Vec3 { Vec3::new(self.x * s, self.y * s, self.z * s) } }

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct TraceResult {
    pub fraction: f32,
    pub pos: Vec3,
    pub normal: Vec3,
    pub hit: i32,
    pub startsolid: i32,
    pub sky: i32,
    pub entity: EntityId,
}

type Ud = *mut c_void;

#[repr(C)]
pub struct RawBehaviour {
    pub user: Ud,
    pub create: Option<unsafe extern "C" fn(Ud, EntityId, *const c_char) -> Ud>,
    pub destroy: Option<unsafe extern "C" fn(Ud, EntityId)>,
    pub spawn: Option<unsafe extern "C" fn(Ud, EntityId)>,
    pub update: Option<unsafe extern "C" fn(Ud, EntityId, f32)>,
    pub touch: Option<unsafe extern "C" fn(Ud, EntityId, EntityId, Vec3)>,
    pub use_: Option<unsafe extern "C" fn(Ud, EntityId, EntityId)>,
    pub damaged: Option<unsafe extern "C" fn(Ud, EntityId, f32, EntityId, Vec3) -> f32>,
    pub pain: Option<unsafe extern "C" fn(Ud, EntityId, f32, EntityId)>,
    pub die: Option<unsafe extern "C" fn(Ud, EntityId, EntityId, f32)>,
    pub blocked: Option<unsafe extern "C" fn(Ud, EntityId, EntityId)>,
    pub editor_json: *const c_char,
}

type S = *const c_char;
type E = EntityId;

#[repr(C)]
pub struct Api {
    pub version: u32,
    pub size: u32,
    pub log: extern "C" fn(S),
    pub register_class: extern "C" fn(S, *const RawBehaviour),
    pub register_component: extern "C" fn(S, *const RawBehaviour),
    pub console: extern "C" fn(S),
    pub lua: extern "C" fn(S),
    pub cvar_get: extern "C" fn(S) -> f32,
    pub cvar_set: extern "C" fn(S, S),
    pub time: extern "C" fn() -> f32,
    pub random: extern "C" fn(f32, f32) -> f32,
    pub player: extern "C" fn() -> E,
    pub spawn: extern "C" fn(S, Vec3, S, f32) -> E,
    pub remove: extern "C" fn(E),
    pub exists: extern "C" fn(E) -> i32,
    pub get_float: extern "C" fn(E, S) -> f32,
    pub set_float: extern "C" fn(E, S, f32),
    pub get_vec: extern "C" fn(E, S) -> Vec3,
    pub set_vec: extern "C" fn(E, S, Vec3),
    pub get_string: extern "C" fn(E, S, *mut c_char, i32) -> i32,
    pub set_string: extern "C" fn(E, S, S),
    pub get_prop: extern "C" fn(E, S, *mut c_char, i32) -> i32,
    pub set_prop: extern "C" fn(E, S, S),
    pub find: extern "C" fn(S, *mut E, i32) -> i32,
    pub find_class: extern "C" fn(S, *mut E, i32) -> i32,
    pub find_radius: extern "C" fn(Vec3, f32, *mut E, i32) -> i32,
    pub walk: extern "C" fn(E, Vec3, f32),
    pub face: extern "C" fn(E, Vec3, f32, f32),
    pub visible: extern "C" fn(E, E) -> i32,
    pub trace: extern "C" fn(Vec3, Vec3, E) -> TraceResult,
    pub damage: extern "C" fn(E, f32, E, Vec3),
    pub radius_damage: extern "C" fn(Vec3, f32, f32, E),
    pub fire_bullets: extern "C" fn(E, Vec3, Vec3, i32, f32, f32),
    pub projectile: extern "C" fn(S, E, Vec3, Vec3, f32) -> E,
    pub explosion: extern "C" fn(Vec3, f32, f32, E),
    pub gib: extern "C" fn(E, i32),
    pub blood: extern "C" fn(Vec3, Vec3, i32),
    pub sound: extern "C" fn(S, Vec3, f32, f32),
    pub sound2d: extern "C" fn(S, f32, f32),
    pub particles: extern "C" fn(Vec3, Vec3, i32, u32, f32, f32, f32),
    pub light: extern "C" fn(Vec3, Vec3, f32, f32),
    pub message: extern "C" fn(S, f32),
    pub fire_targets: extern "C" fn(S, E),
    pub objective: extern "C" fn(S, S, i32),
    pub change_map: extern "C" fn(S),
    pub give_weapon: extern "C" fn(S, i32),
    pub give_ammo: extern "C" fn(S, i32),
}

static mut API: *const Api = std::ptr::null();

/// Called from `rbs_plugin_init`.
pub unsafe fn init(api: *const Api) -> bool {
    if api.is_null() || (*api).version < API_VERSION {
        return false;
    }
    API = api;
    true
}

pub fn api() -> &'static Api {
    unsafe { &*API }
}

fn cs(s: &str) -> CString { CString::new(s).unwrap_or_default() }

// ---- safe helpers -----------------------------------------------------------------

pub fn log(msg: &str) { (api().log)(cs(msg).as_ptr()) }
pub fn time() -> f32 { (api().time)() }
pub fn random(a: f32, b: f32) -> f32 { (api().random)(a, b) }
pub fn player() -> Entity { Entity((api().player)()) }
pub fn message(text: &str, seconds: f32) { (api().message)(cs(text).as_ptr(), seconds) }
pub fn sound(path: &str, pos: Vec3, volume: f32, pitch: f32) { (api().sound)(cs(path).as_ptr(), pos, volume, pitch) }
pub fn light(pos: Vec3, color: Vec3, radius: f32, seconds: f32) { (api().light)(pos, color, radius, seconds) }
pub fn particles(pos: Vec3, dir: Vec3, count: i32, rgba: u32, speed: f32, size: f32, life: f32) {
    (api().particles)(pos, dir, count, rgba, speed, size, life)
}
pub fn spawn(class: &str, pos: Vec3, props_json: &str, yaw: f32) -> Entity {
    Entity((api().spawn)(cs(class).as_ptr(), pos, cs(props_json).as_ptr(), yaw))
}
pub fn console(cmd: &str) { (api().console)(cs(cmd).as_ptr()) }

/// Handle to an engine entity.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Entity(pub EntityId);

impl Entity {
    pub fn valid(self) -> bool { self.0 != 0 && (api().exists)(self.0) != 0 }
    pub fn get(self, prop: &str) -> f32 { (api().get_float)(self.0, cs(prop).as_ptr()) }
    pub fn set(self, prop: &str, v: f32) { (api().set_float)(self.0, cs(prop).as_ptr(), v) }
    pub fn get_vec(self, prop: &str) -> Vec3 { (api().get_vec)(self.0, cs(prop).as_ptr()) }
    pub fn set_vec(self, prop: &str, v: Vec3) { (api().set_vec)(self.0, cs(prop).as_ptr(), v) }
    pub fn set_str(self, prop: &str, v: &str) { (api().set_string)(self.0, cs(prop).as_ptr(), cs(v).as_ptr()) }
    pub fn get_str(self, prop: &str) -> String {
        let mut buf = vec![0 as c_char; 256];
        let n = (api().get_string)(self.0, cs(prop).as_ptr(), buf.as_mut_ptr(), buf.len() as i32);
        if n as usize >= buf.len() {
            buf = vec![0 as c_char; n as usize + 1];
            (api().get_string)(self.0, cs(prop).as_ptr(), buf.as_mut_ptr(), buf.len() as i32);
        }
        unsafe { CStr::from_ptr(buf.as_ptr()) }.to_string_lossy().into_owned()
    }
    pub fn pos(self) -> Vec3 { self.get_vec("pos") }
    pub fn set_pos(self, v: Vec3) { self.set_vec("pos", v) }
    pub fn center(self) -> Vec3 { self.get_vec("center") }
    pub fn vel(self) -> Vec3 { self.get_vec("vel") }
    pub fn set_vel(self, v: Vec3) { self.set_vec("vel", v) }
    pub fn health(self) -> f32 { self.get("health") }
    pub fn set_health(self, v: f32) { self.set("health", v) }
    pub fn distance(self, other: Entity) -> f32 { (self.pos() - other.pos()).len() }
    pub fn can_see(self, other: Entity) -> bool { (api().visible)(self.0, other.0) != 0 }
    pub fn remove(self) { (api().remove)(self.0) }
    pub fn damage(self, amount: f32, attacker: Entity) { (api().damage)(self.0, amount, attacker.0, Vec3::default()) }
}

/// Instance data handed to every callback.
pub struct Ctx {
    pub this: Entity,
    /// JSON object with the instance properties (editor values + defaults).
    pub props: String,
}

impl Ctx {
    /// Reads a number from the props JSON (flat objects only).
    pub fn prop_f32(&self, key: &str, default: f32) -> f32 {
        let k = format!("\"{}\"", key);
        let Some(p) = self.props.find(&k) else { return default };
        let rest = &self.props[p + k.len()..];
        let Some(colon) = rest.find(':') else { return default };
        let num: String = rest[colon + 1..].trim_start().chars().take_while(|c| c.is_ascii_digit() || "+-.eE".contains(*c)).collect();
        num.parse().unwrap_or(default)
    }
}

/// Implement this for entity classes and components (all methods optional).
pub trait Behaviour: 'static {
    fn spawn(&mut self, _ctx: &Ctx) {}
    fn update(&mut self, _ctx: &Ctx, _dt: f32) {}
    fn touch(&mut self, _ctx: &Ctx, _other: Entity, _normal: Vec3) {}
    fn use_(&mut self, _ctx: &Ctx, _activator: Entity) {}
    fn damaged(&mut self, _ctx: &Ctx, amount: f32, _attacker: Entity, _dir: Vec3) -> f32 { amount }
    fn pain(&mut self, _ctx: &Ctx, _amount: f32, _attacker: Entity) {}
    fn die(&mut self, _ctx: &Ctx, _attacker: Entity, _amount: f32) {}
    fn blocked(&mut self, _ctx: &Ctx, _other: Entity) {}
}

type Factory = fn() -> Box<dyn Behaviour>;

struct Instance {
    ctx: Ctx,
    b: Box<dyn Behaviour>,
}

unsafe fn inst<'a>(d: Ud) -> &'a mut Instance { &mut *(d as *mut Instance) }

unsafe extern "C" fn cb_create(user: Ud, this: E, props: *const c_char) -> Ud {
    let factory: Factory = std::mem::transmute::<Ud, Factory>(user);
    let props = if props.is_null() { "{}".to_string() } else { CStr::from_ptr(props).to_string_lossy().into_owned() };
    Box::into_raw(Box::new(Instance { ctx: Ctx { this: Entity(this), props }, b: factory() })) as Ud
}
unsafe extern "C" fn cb_destroy(d: Ud, _: E) { drop(Box::from_raw(d as *mut Instance)) }
unsafe extern "C" fn cb_spawn(d: Ud, _: E) { let i = inst(d); i.b.spawn(&i.ctx) }
unsafe extern "C" fn cb_update(d: Ud, _: E, dt: f32) { let i = inst(d); i.b.update(&i.ctx, dt) }
unsafe extern "C" fn cb_touch(d: Ud, _: E, o: E, n: Vec3) { let i = inst(d); i.b.touch(&i.ctx, Entity(o), n) }
unsafe extern "C" fn cb_use(d: Ud, _: E, a: E) { let i = inst(d); i.b.use_(&i.ctx, Entity(a)) }
unsafe extern "C" fn cb_damaged(d: Ud, _: E, amt: f32, a: E, dir: Vec3) -> f32 { let i = inst(d); i.b.damaged(&i.ctx, amt, Entity(a), dir) }
unsafe extern "C" fn cb_pain(d: Ud, _: E, amt: f32, a: E) { let i = inst(d); i.b.pain(&i.ctx, amt, Entity(a)) }
unsafe extern "C" fn cb_die(d: Ud, _: E, a: E, amt: f32) { let i = inst(d); i.b.die(&i.ctx, Entity(a), amt) }
unsafe extern "C" fn cb_blocked(d: Ud, _: E, o: E) { let i = inst(d); i.b.blocked(&i.ctx, Entity(o)) }

fn raw(factory: Factory, editor_json: &str) -> RawBehaviour {
    // the editor JSON must stay alive: leak it (plugins live for the whole session)
    let json = Box::leak(cs(editor_json).into_boxed_c_str());
    RawBehaviour {
        user: factory as Ud,
        create: Some(cb_create),
        destroy: Some(cb_destroy),
        spawn: Some(cb_spawn),
        update: Some(cb_update),
        touch: Some(cb_touch),
        use_: Some(cb_use),
        damaged: Some(cb_damaged),
        pain: Some(cb_pain),
        die: Some(cb_die),
        blocked: Some(cb_blocked),
        editor_json: json.as_ptr(),
    }
}

pub fn register_class(name: &str, editor_json: &str, factory: Factory) {
    let r = raw(factory, editor_json);
    (api().register_class)(cs(name).as_ptr(), &r);
}

pub fn register_component(name: &str, editor_json: &str, factory: Factory) {
    let r = raw(factory, editor_json);
    (api().register_component)(cs(name).as_ptr(), &r);
}
