#include "core/input.h"
#include "core/common.h"
#include "core/cvar.h"

namespace rbs {
namespace input {

struct Binding {
    const char* cvar;
    const char* def;
    Cvar* cv = nullptr;
};

static Binding g_binds[(int)Action::Count] = {
    {"bind_forward", "W"}, {"bind_back", "S"}, {"bind_left", "A"}, {"bind_right", "D"}, {"bind_jump", "SPACE"},
    {"bind_fire", "MOUSE1"}, {"bind_altfire", "MOUSE2"}, {"bind_use", "E"}, {"bind_nextweapon", "WHEELDOWN"},
    {"bind_prevweapon", "WHEELUP"}, {"bind_objectives", "TAB"}, {"bind_walk", "LEFT_SHIFT"},
    {"bind_weapon1", "1"}, {"bind_weapon2", "2"}, {"bind_weapon3", "3"}, {"bind_weapon4", "4"}, {"bind_weapon5", "5"},
    {"bind_weapon6", "6"}, {"bind_weapon7", "7"}, {"bind_weapon8", "8"}, {"bind_weapon9", "9"}};

struct KeyName {
    const char* name;
    int key;
};

static const KeyName kKeys[] = {
    {"SPACE", KEY_SPACE}, {"ESCAPE", KEY_ESCAPE}, {"ENTER", KEY_ENTER}, {"TAB", KEY_TAB}, {"BACKSPACE", KEY_BACKSPACE},
    {"INSERT", KEY_INSERT}, {"DELETE", KEY_DELETE}, {"RIGHT", KEY_RIGHT}, {"LEFT", KEY_LEFT}, {"DOWN", KEY_DOWN},
    {"UP", KEY_UP}, {"PAGE_UP", KEY_PAGE_UP}, {"PAGE_DOWN", KEY_PAGE_DOWN}, {"HOME", KEY_HOME}, {"END", KEY_END},
    {"CAPS_LOCK", KEY_CAPS_LOCK}, {"LEFT_SHIFT", KEY_LEFT_SHIFT}, {"LEFT_CONTROL", KEY_LEFT_CONTROL},
    {"LEFT_ALT", KEY_LEFT_ALT}, {"RIGHT_SHIFT", KEY_RIGHT_SHIFT}, {"RIGHT_CONTROL", KEY_RIGHT_CONTROL},
    {"RIGHT_ALT", KEY_RIGHT_ALT}, {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3}, {"F4", KEY_F4}, {"F5", KEY_F5},
    {"F6", KEY_F6}, {"F7", KEY_F7}, {"F8", KEY_F8}, {"F9", KEY_F9}, {"F10", KEY_F10}, {"F11", KEY_F11},
    {"F12", KEY_F12}, {"MOUSE1", -100 - MOUSE_BUTTON_LEFT}, {"MOUSE2", -100 - MOUSE_BUTTON_RIGHT},
    {"MOUSE3", -100 - MOUSE_BUTTON_MIDDLE}, {"MOUSE4", -100 - MOUSE_BUTTON_SIDE}, {"MOUSE5", -100 - MOUSE_BUTTON_EXTRA},
    {"WHEELUP", -200}, {"WHEELDOWN", -201}};

int KeyFromName(const std::string& n) {
    std::string name = n;
    for (auto& c : name) c = (char)toupper((unsigned char)c);
    for (auto& k : kKeys)
        if (name == k.name) return k.key;
    if (name.size() == 1) {
        char c = name[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (int)c;
    }
    return 0;
}

std::string NameFromKey(int key) {
    for (auto& k : kKeys)
        if (k.key == key) return k.name;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, (char)key);
    return "?";
}

void Init() {
    for (auto& b : g_binds) b.cv = cvars::Register(b.cvar, b.def, true, "key binding (key name, MOUSE1-5, WHEELUP/WHEELDOWN)");
}

static bool KeyDown(int key) {
    if (key == 0) return false;
    if (key <= -200) return false;
    if (key <= -100) return IsMouseButtonDown(-100 - key);
    return IsKeyDown(key);
}

static bool KeyPressed(int key) {
    if (key == 0) return false;
    if (key == -200) return GetMouseWheelMove() > 0;
    if (key == -201) return GetMouseWheelMove() < 0;
    if (key <= -100) return IsMouseButtonPressed(-100 - key);
    return IsKeyPressed(key);
}

bool Down(Action a) {
    Binding& b = g_binds[(int)a];
    return b.cv && KeyDown(KeyFromName(b.cv->value));
}

bool Pressed(Action a) {
    Binding& b = g_binds[(int)a];
    return b.cv && KeyPressed(KeyFromName(b.cv->value));
}

}  // namespace input
}  // namespace rbs
