// Example native plugin in C++: an automatic plasma turret + a "spinner" component.
//
// Build:  cmake -S . -B build && cmake --build build --config Release
// The library is copied to mods/plugin_demo/plugins/ automatically.
#include "rbs_plugin.hpp"

using namespace rbs;

// Stationary turret: tracks the player and fires plasma bolts while it can see them.
struct Turret : Behaviour {
    float nextShot = 0;
    float range = 900;
    float fireRate = 0.25f;

    void Spawn() override {
        range = PropF("range", 900);
        fireRate = PropF("fire_rate", 0.25f);
        self.SetModel("models/weapons/w_plasma.glb");
        self.Set("scale", 2.2f);
        self.SetBox({-14, -8, -14}, {14, 20, 14});
        self.SetSolid("bbox");
        self.Set("takedamage", 1);
        self.SetHealth(PropF("health", 80));
        self.SetLight({0.2f, 0.4f, 1.2f}, 90);
    }

    void Update(float dt) override {
        Entity p = Player();
        if (!p || p.Get("dead") != 0) return;
        if (self.Distance(p) > range || !self.CanSee(p)) return;
        self.Face(p.Center(), 120, dt);
        if (Time() < nextShot) return;
        nextShot = Time() + fireRate;
        Vec3 muzzle = self.Pos() + Vec3(0, 12, 0);
        Vec3 dir = (p.Center() - muzzle).Norm();
        Vec3 spread{Random(-0.04f, 0.04f), Random(-0.03f, 0.03f), Random(-0.04f, 0.04f)};
        api->projectile("plasma_bolt", self.id, muzzle + dir * 30.0f, (dir + spread).Norm(), 1100);
        Sound("sounds/plasma.wav", muzzle, 0.6f, 0.8f);
    }

    void Die(Entity attacker, float) override {
        api->explosion(self.Center(), 140, 60, attacker.id);
        self.Remove();
    }
};

// Component: spins an entity and makes it pulse (attach it to anything in the editor).
struct Spinner : Behaviour {
    float speed = 180;
    void Spawn() override { speed = PropF("speed", 180); }
    void Update(float dt) override { self.SetYaw(self.Yaw() + speed * dt); }
};

RBS_PLUGIN_INIT {
    Log("C++ example plugin loaded");
    RegisterEntity<Turret>("cpp_turret", R"({
        "desc": "Plasma turret written in C++ (sdk/examples/cpp_plugin).",
        "category": "monsters", "color": [80, 140, 255],
        "mins": [-14, -8, -14], "maxs": [14, 20, 14],
        "model": "models/weapons/w_plasma.glb",
        "props": {"range": 900, "fire_rate": 0.25, "health": 80}})");
    RegisterComponent<Spinner>("cpp_spinner", R"({"desc": "Spins the entity (C++ component).", "props": {"speed": 180}})");
}
