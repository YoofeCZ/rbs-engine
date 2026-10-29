/* Minimal native plugin in plain C: a "c_beacon" entity that blinks a light and
 * says hello when the player touches it.
 *
 * Build (Linux):   cc -shared -fPIC -O2 -I../../include hello.c -o c_hello.so
 * Build (macOS):   cc -shared -fPIC -O2 -I../../include hello.c -o c_hello.dylib
 * Build (Windows): cl /LD /O2 /I..\..\include hello.c /Fe:c_hello.dll
 * then copy the library into mods/<your mod>/plugins/ (or base/plugins/).
 */
#include "rbs_plugin.h"

#include <math.h>
#include <stddef.h>

static const rbs_api* api;

static void beacon_spawn(void* data, rbs_entity self) {
    (void)data;
    rbs_vec3 mins = {-8, -8, -8}, maxs = {8, 24, 8};
    api->set_vec(self, "mins", mins);
    api->set_vec(self, "maxs", maxs);
    api->set_string(self, "solid", "trigger");
    api->set_string(self, "sprite", "sprites/flare.png");
    api->set_float(self, "sprite_size", 40);
    api->set_float(self, "sprite_additive", 1);
    api->set_float(self, "light_radius", 200);
}

static void beacon_update(void* data, rbs_entity self, float dt) {
    (void)data;
    (void)dt;
    float t = api->time();
    float k = 0.5f + 0.5f * sinf(t * 6.0f);
    rbs_vec3 c = {1.5f * k, 0.3f * k, 0.2f * k};
    api->set_vec(self, "light_color", c);
}

static void beacon_touch(void* data, rbs_entity self, rbs_entity other, rbs_vec3 n) {
    (void)data;
    (void)n;
    if (other != 0 && other == api->player()) {
        api->message("Hello from a C plugin!", 2.0f);
        api->remove(self);
    }
}

RBS_EXPORT int rbs_plugin_init(const rbs_api* a) {
    if (!a || a->version < RBS_PLUGIN_API_VERSION) return 1;
    api = a;
    static rbs_behaviour beacon; /* copied by the engine, static just to be safe */
    beacon.spawn = beacon_spawn;
    beacon.update = beacon_update;
    beacon.touch = beacon_touch;
    beacon.editor_json = "{\"desc\":\"Blinking beacon (plain C plugin)\",\"category\":\"effects\",\"color\":[255,60,40]}";
    api->register_class("c_beacon", &beacon);
    api->log("C example plugin loaded");
    return 0;
}

RBS_EXPORT void rbs_plugin_shutdown(void) {}
