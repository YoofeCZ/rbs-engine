#include "script/dynlib.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rbs {
namespace dynlib {

void* Open(const std::string& path, std::string* error) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h && error) *error = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
    return (void*)h;
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h && error) {
        const char* e = dlerror();
        *error = e ? e : "dlopen failed";
    }
    return h;
#endif
}

void* Symbol(void* lib, const char* name) {
    if (!lib) return nullptr;
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

void Close(void* lib) {
    if (!lib) return;
#if defined(_WIN32)
    FreeLibrary((HMODULE)lib);
#else
    dlclose(lib);
#endif
}

const char* Extension() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

}  // namespace dynlib
}  // namespace rbs
