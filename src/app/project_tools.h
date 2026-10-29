// RBS Engine - creating game projects and exporting standalone games ("Build Game").
#pragma once

#include <string>

namespace rbs {
namespace project {

// Creates mods/<folder>/ with a manifest, a starter map (maps/start.json), a script template
// and a README. Does not switch to it.
bool Create(const std::string& folder, const std::string& title, std::string* error);

struct BuildOptions {
    std::string outDir;            // default: <root>/builds/<project folder>
    bool includeEditor = false;    // offer the level editor in the game's menu
    bool includeDemoMaps = false;  // also ship base/maps (the E1M1 demo)
};

// Exports the active project as a standalone game folder: the engine executable (renamed
// after the game), base/, the project, licenses and a launch.json that starts the game.
// `report` receives a summary or the error.
bool Build(const BuildOptions& options, std::string* report);

}  // namespace project
}  // namespace rbs
