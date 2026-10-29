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
}  // namespace dynlib
}  // namespace rbs
