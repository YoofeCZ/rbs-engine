#include "script/dynlib.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
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

std::string ExecutablePath() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
    if (n == 0) return "";
    // narrow paths are in the ANSI code page on Windows (std::filesystem, fopen, LoadLibraryA)
    int len = WideCharToMultiByte(CP_ACP, 0, buf.data(), (int)n, nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, buf.data(), (int)n, &out[0], len, nullptr, nullptr);
    return out;
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    char real[PATH_MAX];
    return realpath(buf, real) ? std::string(real) : std::string(buf);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    return buf;
#endif
}

}  // namespace dynlib
}  // namespace rbs
