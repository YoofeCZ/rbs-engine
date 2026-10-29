#include "core/project.h"
#include "core/log.h"
#include "core/vfs.h"

#include <cctype>
#include <filesystem>
#include <fstream>

namespace rbs {
namespace project {

namespace fs = std::filesystem;
using json = nlohmann::json;

static Info g_current;
static bool g_loaded = false;
static Launch g_launch;

static json ReadJsonFile(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return json::object();
    json j = json::parse(f, nullptr, false, true);
    return j.is_object() ? j : json::object();
}

static std::string Str(const json& j, const char* key, const std::string& def = "") {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}

static bool Bool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}

Info Read(const std::string& folder) {
    Info info;
    info.folder = folder;
    if (folder.empty()) return info;
    info.manifest = ReadJsonFile(fs::path(vfs::Root()) / "mods" / folder / "mod.json");
    const json& m = info.manifest;
    info.title = Str(m, "title", folder);
    info.description = Str(m, "description");
    info.author = Str(m, "author");
    info.version = Str(m, "version");
    info.startMap = Str(m, "start_map");
    json menu = m.contains("menu") && m["menu"].is_object() ? m["menu"] : json::object();
    info.menuTitle = Str(menu, "title", info.title);
    info.menuSubtitle = Str(menu, "subtitle");
    info.menuBackground = Str(menu, "background");
    info.showEditor = Bool(menu, "show_editor", true);
    info.showMods = Bool(menu, "show_mods", true);
    return info;
}

void Reload() {
    g_current = Read(vfs::CurrentMod());
    g_loaded = true;
}

const Info& Current() {
    if (!g_loaded || g_current.folder != vfs::CurrentMod()) Reload();
    return g_current;
}

bool SaveManifest(const json& manifest, std::string* error) {
    const std::string& folder = vfs::CurrentMod();
    if (folder.empty()) {
        if (error) *error = "no project is open (the base game has no manifest)";
        return false;
    }
    fs::path p = fs::path(vfs::Root()) / "mods" / folder / "mod.json";
    std::ofstream f(p);
    if (!f) {
        if (error) *error = "cannot write " + p.string();
        return false;
    }
    f << manifest.dump(2) << "\n";
    f.close();
    Reload();
    return true;
}

void LoadLaunch() {
    fs::path p = fs::path(vfs::Root()) / "launch.json";
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    json j = ReadJsonFile(p);
    g_launch.present = true;
    g_launch.project = Str(j, "project");
    g_launch.editor = Bool(j, "editor", true);
    g_launch.mods = Bool(j, "mods", true);
    g_launch.console = Bool(j, "console", true);
    if (!g_launch.project.empty() && !ValidFolderName(g_launch.project)) {
        LOG_WARN("launch.json: invalid project name '%s'", g_launch.project.c_str());
        g_launch.project.clear();
    }
}

const Launch& LaunchConfig() { return g_launch; }

bool ValidFolderName(const std::string& folder) {
    if (folder.empty() || folder.size() > 64 || folder == "." || folder == "..") return false;
    for (char c : folder)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

}  // namespace project
}  // namespace rbs
