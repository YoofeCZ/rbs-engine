// RBS Engine - game projects.
//
// A game made with the engine is a folder in mods/ (like a Quake/Half-Life mod or a Unity
// project): it can use everything in base/ and override or add any file. Its manifest
// mod.json holds the title, start map, menu look and player settings. A standalone build
// of a game has a launch.json next to base/ that starts that project directly.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace rbs {
namespace project {

struct Info {
    std::string folder;  // mods/<folder>; empty = the base game
    std::string title, description, author, version;
    std::string startMap;  // "maps/start.json"
    std::string menuTitle, menuSubtitle, menuBackground;
    bool showEditor = true, showMods = true;
    nlohmann::json manifest = nlohmann::json::object();  // the whole mod.json (scripts read it too)
};

// launch.json of a standalone build
struct Launch {
    bool present = false;
    std::string project;
    bool editor = true, mods = true, console = true;
};

const Info& Current();  // the active project's manifest
void Reload();          // re-read after switching projects or saving
// Writes mods/<active project>/mod.json.
bool SaveManifest(const nlohmann::json& manifest, std::string* error);
// Manifest of any project folder (for lists), without activating it.
Info Read(const std::string& folder);

void LoadLaunch();  // reads <root>/launch.json once at startup
const Launch& LaunchConfig();

bool ValidFolderName(const std::string& folder);

}  // namespace project
}  // namespace rbs
