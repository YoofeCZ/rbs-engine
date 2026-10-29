// RBS Engine - entry point.
//
//   rbs                      start with the main menu
//   rbs --map e1m1           start a map directly
//   rbs --editor [map]       open the level editor
//   rbs --mod mymod          run with a mod (folder in mods/)
//   rbs --screenshot out.png [--frames N] [--exec "cmd; cmd"]   automated capture
#include "app/app.h"
#include "core/log.h"
#include "core/project.h"
#include "core/vfs.h"
#include "script/dynlib.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    rbs::LogInit();
    // argv[0] can be a bare name found through PATH, so ask the OS where the executable is
    std::string exe = rbs::dynlib::ExecutablePath();
    if (exe.empty() && argc > 0) exe = argv[0];
    if (!rbs::vfs::Init(exe.c_str())) {
        fprintf(stderr, "RBS Engine: cannot find the game data folder (base/). Run from the engine folder.\n");
        return 1;
    }
    rbs::LaunchOptions opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--map" || a == "+map") opt.map = next();
        else if (a == "--mod" || a == "-game") opt.mod = next();
        else if (a == "--editor") {
            opt.editor = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') opt.editorMap = next();
        } else if (a == "--screenshot") opt.screenshot = next();
        else if (a == "--hidden") opt.hidden = true;
        else if (a == "--visible") opt.visible = true;
        else if (a == "--frames") opt.screenshotFrames = atoi(next().c_str());
        else if (a == "--exec") opt.exec = next();
        else if (a == "--width") opt.width = atoi(next().c_str());
        else if (a == "--height") opt.height = atoi(next().c_str());
        else if (a == "--fullscreen") opt.fullscreen = true;
        else if (a == "--help" || a == "-h") {
            printf("RBS Engine %s\n  --map <name>  --editor [map]  --mod <name>  --width W --height H  --fullscreen\n"
                   "  --screenshot <file.png> [--frames N] [--exec \"commands\"]\n"
                   "  --hidden / --visible   automated runs (--screenshot) use a hidden window and no sound unless --visible\n", RBS_VERSION);
            return 0;
        }
    }
    // a standalone game build starts its project (launch.json next to base/)
    rbs::project::LoadLaunch();
    if (opt.mod.empty()) opt.mod = rbs::project::LaunchConfig().project;
    if (!opt.mod.empty()) rbs::vfs::SetMod(opt.mod);
    rbs::App app;
    if (!app.Init(opt)) return 1;
    app.Run();
    app.Shutdown();
    return 0;
}
