#include "core/cvar.h"
#include "core/common.h"
#include "core/log.h"
#include "core/vfs.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace rbs {

void Cvar::Set(const std::string& v) {
    value = v;
    f = (float)atof(v.c_str());
    i = atoi(v.c_str());
}

namespace cvars {

struct Command {
    CommandFn fn;
    std::string help;
};

static std::map<std::string, std::unique_ptr<Cvar>>& Vars() {
    static std::map<std::string, std::unique_ptr<Cvar>> v;
    return v;
}
static std::map<std::string, Command>& Commands() {
    static std::map<std::string, Command> c;
    return c;
}

Cvar* Register(const char* name, const char* def, bool archive, const char* help) {
    auto& vars = Vars();
    auto it = vars.find(name);
    if (it != vars.end()) return it->second.get();
    auto cv = std::make_unique<Cvar>();
    cv->name = name;
    cv->defaultValue = def;
    cv->help = help ? help : "";
    cv->archive = archive;
    cv->Set(def);
    Cvar* p = cv.get();
    vars[name] = std::move(cv);
    return p;
}

Cvar* Find(const std::string& name) {
    auto it = Vars().find(ToLower(name));
    return it == Vars().end() ? nullptr : it->second.get();
}

void Set(const std::string& name, const std::string& value) {
    Cvar* cv = Find(name);
    if (!cv) cv = Register(ToLower(name).c_str(), value.c_str(), false, "");
    cv->Set(value);
}

float GetF(const std::string& name, float fallback) {
    Cvar* cv = Find(name);
    return cv ? cv->f : fallback;
}

std::vector<Cvar*> All() {
    std::vector<Cvar*> out;
    for (auto& kv : Vars()) out.push_back(kv.second.get());
    return out;
}

void AddCommand(const std::string& name, CommandFn fn, const char* help) { Commands()[name] = {std::move(fn), help ? help : ""}; }

static void ExecuteOne(const std::string& line) {
    auto args = SplitWords(line);
    if (args.empty()) return;
    std::string cmd = ToLower(args[0]);
    auto cit = Commands().find(cmd);
    if (cit != Commands().end()) {
        cit->second.fn(args);
        return;
    }
    if (Cvar* cv = Find(cmd)) {
        if (args.size() >= 2) cv->Set(args[1]);
        else LOG_INFO("%s = \"%s\" (default \"%s\")  %s", cv->name.c_str(), cv->value.c_str(), cv->defaultValue.c_str(), cv->help.c_str());
        return;
    }
    LOG_WARN("Unknown command: %s", cmd.c_str());
}

void Execute(const std::string& text) {
    std::string cur;
    bool quoted = false;
    for (char c : text) {
        if (c == '"') quoted = !quoted;
        if ((c == ';' || c == '\n') && !quoted) {
            ExecuteOne(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    ExecuteOne(cur);
}

std::vector<std::string> Complete(const std::string& prefix) {
    std::vector<std::string> out;
    std::string p = ToLower(prefix);
    for (auto& kv : Commands())
        if (StartsWith(kv.first, p)) out.push_back(kv.first);
    for (auto& kv : Vars())
        if (StartsWith(kv.first, p)) out.push_back(kv.first);
    return out;
}

static std::string ConfigPath() { return (std::filesystem::path(vfs::Root()) / "config.cfg").string(); }

void LoadConfig() {
    std::ifstream f(ConfigPath());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || StartsWith(line, "//")) continue;
        Execute(line);
    }
}

void SaveConfig() {
    std::ofstream f(ConfigPath());
    if (!f) return;
    f << "// RBS Engine config - generated, edit freely\n";
    for (auto& kv : Vars())
        if (kv.second->archive) f << kv.first << " \"" << kv.second->value << "\"\n";
}

static struct BuiltinCommands {
    BuiltinCommands() {
        AddCommand("help", [](const std::vector<std::string>&) {
            LOG_INFO("Commands:");
            for (auto& kv : Commands()) LOG_INFO("  %-18s %s", kv.first.c_str(), kv.second.help.c_str());
            LOG_INFO("Type a variable name to see its value, 'cvarlist' for all variables.");
        }, "list commands");
        AddCommand("cvarlist", [](const std::vector<std::string>&) {
            for (auto& kv : Vars()) LOG_INFO("  %-22s \"%s\"  %s", kv.first.c_str(), kv.second->value.c_str(), kv.second->help.c_str());
        }, "list console variables");
        AddCommand("set", [](const std::vector<std::string>& a) {
            if (a.size() >= 3) Set(a[1], a[2]);
        }, "set <var> <value>");
        AddCommand("echo", [](const std::vector<std::string>& a) {
            std::string s;
            for (size_t i = 1; i < a.size(); ++i) s += a[i] + " ";
            LOG_INFO("%s", s.c_str());
        }, "print text");
    }
} g_builtin;

}  // namespace cvars
}  // namespace rbs
