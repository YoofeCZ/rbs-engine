#include "app/project_tools.h"
#include "core/common.h"
#include "core/log.h"
#include "core/project.h"
#include "core/vfs.h"
#include "script/dynlib.h"
#include "world/map.h"

#include <filesystem>
#include <fstream>
#include <functional>

namespace rbs {
namespace project {

namespace fs = std::filesystem;

static bool WriteFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return (bool)f;
}

bool Create(const std::string& folder, const std::string& title, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (!ValidFolderName(folder)) return fail("use letters, digits, '-' and '_' for the project folder name");
    fs::path dir = fs::path(vfs::Root()) / "mods" / folder;
    std::error_code ec;
    if (fs::exists(dir, ec)) return fail("mods/" + folder + " already exists");
    std::string name = title.empty() ? folder : title;

    nlohmann::json manifest = {
        {"title", name},
        {"description", ""},
        {"author", ""},
        {"version", "1.0"},
        {"start_map", "maps/start.json"},
        {"menu", {{"title", name}, {"subtitle", ""}, {"background", ""}, {"show_editor", true}, {"show_mods", false}}},
        {"player", {{"health", 100}, {"armor", 0}, {"start_weapons", {"pistol", "shotgun"}}, {"select", "shotgun"}}},
    };
    if (!WriteFile(dir / "mod.json", manifest.dump(2) + "\n")) return fail("cannot write " + (dir / "mod.json").string());

    Map map;
    map.MakeStarterRoom();
    map.settings.title = name;
    WriteFile(dir / "maps" / "start.json", map.ToJson().dump(1));

    WriteFile(dir / "scripts" / "autorun" / (folder + ".lua"),
              "-- " + name + ": optional Lua code of your game.\n"
              "--\n"
              "-- Enemies, weapons and items can be made without code in the editor\n"
              "-- (Project > Entities > New type, Project > Weapons > New weapon). Write code\n"
              "-- only for behaviour the built-in options don't cover. Docs: docs/GAME_CREATION.md,\n"
              "-- docs/AI.md and docs/SCRIPTING.md.\n"
              "--\n"
              "-- Example: an enemy with its own attack on top of the built-in AI\n"
              "--\n"
              "-- rbs.class(\"my_enemy\", {\n"
              "--   base = \"monster_base\",\n"
              "--   editor = { category = \"monsters\", desc = \"My enemy\", model = \"models/monsters/grunt.glb\",\n"
              "--              props = { health = 80, ai = \"chase\" } },\n"
              "--   attack = function(self, enemy)\n"
              "--     rbs.damage(enemy, 10, self)\n"
              "--   end,\n"
              "-- })\n");

    WriteFile(dir / "README.md",
              "# " + name + "\n\n"
              "A game made with RBS Engine. Open it in the editor (Project button in the Hierarchy panel)\n"
              "or run `rbs --mod " + folder + "`.\n\n"
              "| Folder | Contents |\n|---|---|\n"
              "| `mod.json` | title, start map, menu and player settings (editor: Project > Settings) |\n"
              "| `maps/` | levels |\n"
              "| `entities/` | enemy/item types made without code (editor: Project > Entities > New type) |\n"
              "| `weapons/` | weapons made without code (editor: Project > Weapons > New weapon) |\n"
              "| `scripts/autorun/` | your Lua code (optional) |\n"
              "| `textures/ models/ sounds/` | your assets (drop files on the editor window to import) |\n");
    LOG_INFO("Created project mods/%s", folder.c_str());
    return true;
}

// Copies a folder tree; skip(rel) decides per relative path (generic, with '/').
static bool CopyTree(const fs::path& from, const fs::path& to, const std::function<bool(const std::string&)>& skip, int& files,
                     uintmax_t& bytes, std::string* error) {
    std::error_code ec;
    if (!fs::exists(from, ec)) return true;
    for (auto it = fs::recursive_directory_iterator(from, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::string rel = fs::relative(it->path(), from, ec).generic_string();
        if (skip(rel)) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        fs::path dst = to / fs::path(rel);
        if (it->is_directory()) {
            fs::create_directories(dst, ec);
        } else if (it->is_regular_file()) {
            fs::create_directories(dst.parent_path(), ec);
            if (!fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec)) {
                if (error) *error = "cannot copy " + it->path().string() + ": " + ec.message();
                return false;
            }
            files++;
            bytes += it->file_size(ec);
        }
    }
    return true;
}

static std::string FileNameFromTitle(const std::string& title) {
    std::string out;
    for (char c : title) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_') out += c;
        else if (c == ' ' && !out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "game" : out;
}

bool Build(const BuildOptions& o, std::string* report) {
    auto fail = [&](const std::string& msg) {
        if (report) *report = msg;
        LOG_ERR("Build game: %s", msg.c_str());
        return false;
    };
    const Info& info = Current();
    if (info.folder.empty()) return fail("open a project first - the base game itself is not exported");
    fs::path root = vfs::Root();
    fs::path out = o.outDir.empty() ? root / "builds" / info.folder : fs::path(o.outDir);
    std::error_code ec;
    // never build into the folders being copied
    std::string outAbs = fs::weakly_canonical(out, ec).generic_string() + "/";
    for (const fs::path& src : {root / "base", root / "mods"}) {
        std::string srcAbs = fs::weakly_canonical(src, ec).generic_string() + "/";
        if (StartsWith(outAbs, srcAbs)) return fail("the build folder must be outside base/ and mods/");
    }
    if (fs::exists(out, ec)) {
        // only ever replace an earlier build, never an unrelated folder
        if (!fs::exists(out / "launch.json", ec)) return fail(out.string() + " exists and is not a game build");
        fs::remove_all(out, ec);
        if (ec) return fail("cannot clear " + out.string() + ": " + ec.message());
    }
    fs::create_directories(out, ec);
    if (ec) return fail("cannot create " + out.string() + ": " + ec.message());

    std::string exe = dynlib::ExecutablePath();
    if (exe.empty() || !fs::exists(exe, ec)) return fail("cannot find the engine executable");
    std::string gameName = FileNameFromTitle(info.title.empty() ? info.folder : info.title);
    fs::path exeOut = out / (gameName + fs::path(exe).extension().string());
    if (!fs::copy_file(exe, exeOut, fs::copy_options::overwrite_existing, ec)) return fail("cannot copy the executable: " + ec.message());
    fs::permissions(exeOut, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);

    int files = 1;
    uintmax_t bytes = fs::file_size(exeOut, ec);
    std::string err;
    // engine data and the stock content library; no caches, the demo levels only on request
    bool demo = o.includeDemoMaps;
    if (!CopyTree(root / "base", out / "base",
                  [demo](const std::string& rel) { return StartsWith(rel, "cache") || (!demo && StartsWith(rel, "maps")); }, files, bytes, &err))
        return fail(err);
    // the game itself; of the caches only the baked lighting is worth shipping
    if (!CopyTree(root / "mods" / info.folder, out / "mods" / info.folder,
                  [](const std::string& rel) {
                      if (!StartsWith(rel, "cache")) return false;
                      return rel != "cache" && !EndsWith(rel, ".lmcache");
                  },
                  files, bytes, &err))
        return fail(err);
    for (const char* lic : {"LICENSE", "THIRD_PARTY.md"}) {
        if (fs::exists(root / lic, ec)) {
            fs::create_directories(out / "licenses", ec);
            fs::copy_file(root / lic, out / "licenses" / lic, fs::copy_options::overwrite_existing, ec);
        }
    }
    nlohmann::json launch = {{"project", info.folder}, {"editor", o.includeEditor}, {"mods", info.showMods}, {"console", true}};
    WriteFile(out / "launch.json", launch.dump(2) + "\n");

    char size[32];
    snprintf(size, sizeof(size), "%.1f MB", bytes / (1024.0 * 1024.0));
    if (report) *report = "Built " + info.title + " into " + out.string() + " (" + std::to_string(files) + " files, " + size + "). Run " + exeOut.filename().string();
    LOG_INFO("%s", report ? report->c_str() : "Build done");
    return true;
}

}  // namespace project
}  // namespace rbs
