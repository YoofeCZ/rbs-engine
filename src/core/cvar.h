// RBS Engine - console variables and console commands.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace rbs {

struct Cvar {
    std::string name;
    std::string value;
    std::string defaultValue;
    std::string help;
    bool archive = false;  // saved to config.cfg
    float f = 0;
    int i = 0;
    void Set(const std::string& v);
};

using CommandFn = std::function<void(const std::vector<std::string>& args)>;

namespace cvars {

Cvar* Register(const char* name, const char* defaultValue, bool archive, const char* help);
Cvar* Find(const std::string& name);
void Set(const std::string& name, const std::string& value);
float GetF(const std::string& name, float fallback = 0);
std::vector<Cvar*> All();

void AddCommand(const std::string& name, CommandFn fn, const char* help);
// Executes a console line ("map e1m1", "sensitivity 3", ...). Multiple commands separated by ';'.
void Execute(const std::string& line);
// Variable assignments only ("game_start_map maps/arena.json"); commands and player
// settings (archived variables) are refused: for files that come with mods (game.cfg),
// which are untrusted content.
void ExecuteSettings(const std::string& text, const char* source);
// Unparsed text after the command name of the command being executed (quotes kept),
// for commands that take code or free text: lua print("hi")
const std::string& CommandArgs();
std::vector<std::string> Complete(const std::string& prefix);

void LoadConfig();
void SaveConfig();

}  // namespace cvars
}  // namespace rbs
