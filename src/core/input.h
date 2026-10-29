// RBS Engine - rebindable input actions ("bind_forward W" in the console / config.cfg).
#pragma once

#include <string>

namespace rbs {

enum class Action {
    Forward, Back, Left, Right, Jump, Fire, AltFire, Use, NextWeapon, PrevWeapon, Objectives, Walk,
    Weapon1, Weapon2, Weapon3, Weapon4, Weapon5, Weapon6, Weapon7, Weapon8, Weapon9, Count
};

namespace input {
void Init();
bool Down(Action a);
bool Pressed(Action a);
int KeyFromName(const std::string& name);  // raylib key code, or -100-button for mouse buttons
std::string NameFromKey(int key);
}  // namespace input

}  // namespace rbs
