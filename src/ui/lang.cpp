#include "ui/lang.h"
#include "core/cvar.h"
#include "core/vfs.h"

#include <map>
#include <nlohmann/json.hpp>

namespace rbs {

static std::map<std::string, std::string> g_strings;
static std::string g_loaded = "?";
static Cvar* g_lang = nullptr;

void ReloadLanguage() {
    if (!g_lang) g_lang = cvars::Register("lang", "en", true, "user interface language (en, cs, ... see lang/)");
    g_strings.clear();
    g_loaded = g_lang->value;
    auto load = [](const std::string& rel) {
        std::string text;
        if (!vfs::ReadText(rel, text)) return;
        auto j = nlohmann::json::parse(text, nullptr, false, true);
        if (j.is_discarded() || !j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.value().is_string()) g_strings[it.key()] = it.value().get<std::string>();
    };
    // lang/cs.json, then extra files from mods and plugins: lang/cs/*.json
    load("lang/" + g_loaded + ".json");
    for (auto& f : vfs::List("lang/" + g_loaded, ".json")) load(f);
}

const char* Tr(const char* key) {
    if (!g_lang || g_lang->value != g_loaded) ReloadLanguage();
    auto it = g_strings.find(key);
    return it == g_strings.end() ? key : it->second.c_str();
}

}  // namespace rbs
