#include "core/vfs.h"
#include "core/log.h"
#include "core/common.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace rbs {
namespace vfs {

static std::string g_root;
static std::string g_mod;
static std::vector<std::string> g_layers;  // absolute dirs, highest priority first
static std::vector<std::string> g_textureFolders;

static void RebuildLayers() {
    g_layers.clear();
    if (!g_mod.empty()) g_layers.push_back((fs::path(g_root) / "mods" / g_mod).string());
    g_layers.push_back((fs::path(g_root) / "base").string());
}

bool Init(const char* argv0) {
    std::vector<fs::path> starts;
    std::error_code ec;
    starts.push_back(fs::current_path(ec));
    if (argv0 && *argv0) {
        fs::path exe = fs::absolute(fs::path(argv0), ec);
        starts.push_back(exe.parent_path());
    }
    for (auto start : starts) {
        fs::path p = start;
        for (int i = 0; i < 6 && !p.empty(); ++i) {
            if (fs::exists(p / "base" / "scripts" / "init.lua", ec)) {
                g_root = fs::canonical(p, ec).string();
                RebuildLayers();
                LOG_INFO("Data root: %s", g_root.c_str());
                return true;
            }
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    return false;
}

void SetMod(const std::string& mod) {
    g_mod = mod;
    RebuildLayers();
    LOG_INFO("Active mod: %s", mod.empty() ? "(none - base game)" : mod.c_str());
}

const std::string& CurrentMod() { return g_mod; }
const std::string& Root() { return g_root; }
std::string WriteDir() { return g_layers.empty() ? std::string() : g_layers.front(); }

std::vector<std::string> ListMods() {
    std::vector<std::string> out;
    std::error_code ec;
    fs::path dir = fs::path(g_root) / "mods";
    if (!fs::exists(dir, ec)) return out;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory()) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::string Resolve(const std::string& rel) {
    if (rel.empty()) return {};
    std::error_code ec;
    fs::path rp(rel);
    if (rp.is_absolute()) return fs::exists(rp, ec) ? rel : std::string();
    for (auto& layer : g_layers) {
        fs::path p = fs::path(layer) / rel;
        if (fs::exists(p, ec)) return p.string();
    }
    if (StartsWith(rel, "textures/")) {
        std::string name = rel.substr(9);
        for (auto& dir : g_textureFolders) {
            fs::path p = fs::path(dir) / name;
            if (fs::exists(p, ec)) return p.string();
        }
    }
    return {};
}

bool Exists(const std::string& rel) { return !Resolve(rel).empty(); }

bool ReadText(const std::string& rel, std::string& out) {
    std::string p = Resolve(rel);
    if (p.empty()) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool WriteText(const std::string& rel, const std::string& text) {
    fs::path p = fs::path(WriteDir()) / rel;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    if (!f) {
        LOG_ERR("Cannot write %s", p.string().c_str());
        return false;
    }
    f << text;
    return true;
}

std::vector<std::string> List(const std::string& relDir, const std::string& ext, bool recursive) {
    std::set<std::string> found;
    std::error_code ec;
    auto scan = [&](const fs::path& base, const std::string& prefix) {
        fs::path dir = base;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        auto add = [&](const fs::directory_entry& e) {
            if (!e.is_regular_file()) return;
            std::string name = fs::relative(e.path(), dir, ec).generic_string();
            if (!ext.empty() && !EndsWith(ToLower(name), ext)) return;
            found.insert(prefix + name);
        };
        if (recursive) {
            for (auto& e : fs::recursive_directory_iterator(dir, ec)) add(e);
        } else {
            for (auto& e : fs::directory_iterator(dir, ec)) add(e);
        }
    };
    std::string prefix = relDir.empty() ? "" : relDir + "/";
    for (auto& layer : g_layers) scan(fs::path(layer) / relDir, prefix);
    if (relDir == "textures")
        for (auto& d : g_textureFolders) scan(fs::path(d), prefix);
    return std::vector<std::string>(found.begin(), found.end());
}

void AddTextureFolder(const std::string& absDir) {
    std::error_code ec;
    if (!fs::is_directory(absDir, ec)) {
        LOG_WARN("Not a folder: %s", absDir.c_str());
        return;
    }
    for (auto& d : g_textureFolders)
        if (d == absDir) return;
    g_textureFolders.push_back(absDir);
    LOG_INFO("Added texture folder %s", absDir.c_str());
}

const std::vector<std::string>& TextureFolders() { return g_textureFolders; }

}  // namespace vfs
}  // namespace rbs
