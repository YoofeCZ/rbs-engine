// RBS Engine - save game files: one JSON file per slot in saves/<project>/. The contents are built and
// applied by Game::BuildSave / Game::StartLoadSave (game_save.cpp).
#pragma once

#include "core/common.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace rbs {
namespace savegame {

using json = nlohmann::json;

struct Slot {
    std::string id;           // "auto", "quick", "1".."10"
    bool exists = false;
    std::string title;        // map title
    long long stamp = 0;      // when it was saved (unix time)
    float playtime = 0;       // seconds in the map
    float health = 0;
};

// Slots a player can pick: saving offers quick and 1..10, loading also the autosave.
std::vector<std::string> SlotIds(bool forSaving);
std::string Dir();  // <engine folder>/saves/<project, or "base">
bool Write(const std::string& slot, const json& save, std::string* error = nullptr);
bool Read(const std::string& slot, json& out);
Slot Info(const std::string& slot);
std::string Latest();  // the slot saved most recently, "" when there is none

}  // namespace savegame
}  // namespace rbs
