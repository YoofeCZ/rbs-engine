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
std::vector<std::string> Complete(const std::string& prefix);

void LoadConfig();
void SaveConfig();

}  // namespace cvars
}  // namespace rbs
