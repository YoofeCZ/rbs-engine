// RBS Engine - minimal cross-platform shared library loading.
// Kept in its own translation unit so <windows.h> never meets raylib.h.
#pragma once

#include <string>

namespace rbs {
namespace dynlib {
void* Open(const std::string& path, std::string* error);
void* Symbol(void* lib, const char* name);
void Close(void* lib);
const char* Extension();  // ".dll", ".so" or ".dylib"
// Absolute path of the running executable ("" if unknown), encoded like the engine's other
// paths - used to find the data folder and to export standalone games.
std::string ExecutablePath();
}  // namespace dynlib
}  // namespace rbs
