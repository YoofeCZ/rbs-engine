#include "game/savegame.h"
#include "core/log.h"
#include "core/vfs.h"

#include <filesystem>
#include <fstream>

namespace rbs {
namespace savegame {

namespace fs = std::filesystem;

std::string Dir() {
    std::string mod = vfs::CurrentMod();
    return (fs::path(vfs::Root()) / "saves" / (mod.empty() ? "base" : mod)).string();
}

static std::string PathOf(const std::string& slot) {
    // slot ids come from the menu or the console: keep them plain so they cannot name another folder
    std::string clean;
    for (char c : slot)
        if (isalnum((unsigned char)c) || c == '_' || c == '-') clean += c;
    if (clean.empty()) clean = "quick";
    return (fs::path(Dir()) / (clean + ".json")).string();
}

std::vector<std::string> SlotIds(bool forSaving) {
    std::vector<std::string> out;
    if (!forSaving) out.push_back("auto");
    out.push_back("quick");
    for (int i = 1; i <= 10; ++i) out.push_back(std::to_string(i));
    return out;
}

bool Write(const std::string& slot, const json& save, std::string* error) {
    std::error_code ec;
    fs::create_directories(Dir(), ec);
    std::string path = PathOf(slot), tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) {
            if (error) *error = "cannot write " + tmp;
            return false;
        }
        f << save.dump();
        if (!f) {
            if (error) *error = "disk full or write error in " + tmp;
            return false;
        }
    }
    // written completely first, then renamed over the old save: a crash cannot leave half a save
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

bool Read(const std::string& slot, json& out) {
    std::ifstream f(PathOf(slot), std::ios::binary);
    if (!f) return false;
    out = json::parse(f, nullptr, false);
    return out.is_object() && out.value("format", std::string()) == "rbs-save";
}

Slot Info(const std::string& slot) {
    Slot s;
    s.id = slot;
    json j;
    if (!Read(slot, j)) return s;
    s.exists = true;
    const json& m = j.contains("meta") && j["meta"].is_object() ? j["meta"] : json::object();
    s.title = m.value("title", std::string("?"));
    s.stamp = m.value("stamp", 0LL);
    s.playtime = m.value("time", 0.0f);
    s.health = m.value("health", 0.0f);
    return s;
}

std::string Latest() {
    std::string best;
    long long bestStamp = -1;
    for (auto& id : SlotIds(false)) {
        Slot s = Info(id);
        if (s.exists && s.stamp > bestStamp) best = id, bestStamp = s.stamp;
    }
    return best;
}

}  // namespace savegame
}  // namespace rbs
