#include "app/app.h"
#include "core/cvar.h"
#include "core/input.h"
#include "core/log.h"
#include "core/vfs.h"
#include "editor/editor.h"
#include "render/assets.h"
#include "render/renderer.h"
#include "script/plugins.h"
#include "ui/fonts.h"
#include "ui/lang.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <rlgl.h>
#include <sstream>

namespace rbs {

static const Color kGold{232, 182, 92, 255};
static const Color kDim{150, 130, 110, 255};

App::App() {}
App::~App() { Shutdown(); }

bool App::Init(const LaunchOptions& opt) {
    m_opt = opt;
    cvars::Register("vid_width", "1600", true, "window width");
    cvars::Register("vid_height", "900", true, "window height");
    cvars::Register("vid_fullscreen", "0", true, "fullscreen");
    cvars::Register("vid_vsync", "1", true, "vertical sync");
    cvars::Register("fps_max", "300", true, "frame rate limit (0 = unlimited)");
    cvars::Register("game_start_map", "maps/e1m1.json", false, "map started by New Game (set in game.cfg)");
    input::Init();
    cvars::LoadConfig();
    ApplyGameConfig();

    int w = opt.width, h = opt.height;
    if (Cvar* cw = cvars::Find("vid_width")) if (cw->i > 0 && opt.width == 1600) w = cw->i;
    if (Cvar* ch = cvars::Find("vid_height")) if (ch->i > 0 && opt.height == 900) h = ch->i;
    unsigned flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    if (cvars::GetF("vid_vsync", 1) != 0 && opt.screenshot.empty()) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(w, h, "RBS Engine");
    SetExitKey(KEY_NULL);
    if (opt.fullscreen || cvars::GetF("vid_fullscreen") != 0) ToggleBorderlessWindowed();
    int fpsMax = (int)cvars::GetF("fps_max", 300);
    SetTargetFPS(opt.screenshot.empty() ? fpsMax : 0);
    InitAudioDevice();
    assets::Init();
    if (!gRenderer.Init()) LOG_ERR("Renderer initialisation had errors (see above)");
    plugins::LoadAll();
    m_editor = new Editor(this);
    RegisterCommands();

    if (!opt.map.empty()) StartMap(opt.map);
    else if (opt.editor) OpenEditor(opt.editorMap);
    return true;
}

void App::Shutdown() {
    if (!IsWindowReady()) return;
    cvars::SaveConfig();
    delete m_editor;
    m_editor = nullptr;
    m_game.Unload();
    plugins::UnloadAll();
    fonts::UnloadAll();
    assets::Shutdown();
    gRenderer.Shutdown();
    CloseAudioDevice();
    CloseWindow();
}

static std::string MapPathFromArg(const std::string& a) {
    std::string p = a;
    if (!StartsWith(p, "maps/")) p = "maps/" + p;
    if (!EndsWith(p, ".json")) p += ".json";
    return p;
}

void App::StartMap(const std::string& relPath, bool keepInventory) {
    std::string path = MapPathFromArg(relPath);
    if (!vfs::Exists(path)) {
        LOG_ERR("Map not found: %s", path.c_str());
        return;
    }
    m_fromEditor = false;
    m_paused = false;
    m_execDone = false;
    if (m_game.LoadMap(path, keepInventory)) {
        m_state = State::Game;
        m_page = MenuPage::Main;
        m_console.open = false;
    }
}

void App::PlayFromEditor(const Map& map, const std::string& name, Vector3* spawnPos, float spawnYaw) {
    Map copy = map;
    if (spawnPos) {
        // "play from here": temporary player start at the editor camera
        bool found = false;
        for (auto& e : copy.entities)
            if (e.classname == "player_start") {
                e.origin = *spawnPos;
                e.angle = spawnYaw;
                found = true;
            }
        if (!found) {
            MapEntity ps;
            ps.classname = "player_start";
            ps.origin = *spawnPos;
            ps.angle = spawnYaw;
            copy.entities.push_back(ps);
        }
    }
    m_fromEditor = true;
    m_paused = false;
    m_game.LoadMapData(copy, name.empty() ? "untitled" : name, false, "maps/" + (name.empty() ? std::string("untitled") : name) + ".json");
    m_state = State::Game;
}

void App::OpenEditor(const std::string& map) {
    m_game.Unload();
    m_state = State::Editor;
    m_paused = false;
    m_editor->Open(map.empty() ? "" : MapPathFromArg(map));
}

void App::ReturnToMenu() {
    m_game.Unload();
    m_state = State::Menu;
    m_page = MenuPage::Main;
    m_paused = false;
    m_menuSel = 0;
}

// game.cfg (in base or the active mod) sets the start map and other game variables
void App::ApplyGameConfig() {
    if (Cvar* start = cvars::Find("game_start_map")) start->Set(start->defaultValue);
    std::string text;
    if (vfs::ReadText("game.cfg", text)) cvars::ExecuteSettings(text, "game.cfg");
}

// --exec commands run once per launch (a "map" command in them must not re-trigger them)
void App::RunStartupExec() {
    if (m_opt.exec.empty()) return;
    std::string cmd = m_opt.exec;
    m_opt.exec.clear();
    cvars::Execute(cmd);
}

void App::SetCursor(bool gameplay) {
    bool want = gameplay && !m_console.open && IsWindowFocused();
    if (want && !IsCursorHidden()) DisableCursor();
    if (!want && IsCursorHidden()) EnableCursor();
}

void App::RegisterCommands() {
    cvars::AddCommand("map", [this](const std::vector<std::string>& a) {
        if (a.size() < 2) {
            for (auto& m : vfs::List("maps", ".json")) LOG_INFO("  %s", m.c_str());
            return;
        }
        StartMap(a[1]);
    }, "map <name> - start a map (no argument: list maps)");
    cvars::AddCommand("editor", [this](const std::vector<std::string>& a) { OpenEditor(a.size() > 1 ? a[1] : ""); }, "editor [map] - open the level editor");
    cvars::AddCommand("import", [this](const std::vector<std::string>& a) {
        if (a.size() < 2) {
            LOG_INFO("import <file or folder> - copy assets into the active mod (or base)");
            return;
        }
        for (size_t i = 1; i < a.size(); ++i) m_editor->Import(a[i]);
    }, "import <file or folder> - copy images/models/sounds/... into the active mod (also: drop files on the editor)");
    cvars::AddCommand("quit", [this](const std::vector<std::string>&) { m_quit = true; }, "exit the engine");
    cvars::AddCommand("menu", [this](const std::vector<std::string>&) { ReturnToMenu(); }, "back to the main menu");
    cvars::AddCommand("restart", [this](const std::vector<std::string>&) {
        if (m_game.Active()) m_game.Respawn();
    }, "restart the current map");
    cvars::AddCommand("noclip", [this](const std::vector<std::string>&) {
        if (!m_game.Active()) return;
        m_game.PS().noclip = !m_game.PS().noclip;
        LOG_INFO("noclip %s", m_game.PS().noclip ? "ON" : "OFF");
    }, "fly through walls");
    cvars::AddCommand("god", [this](const std::vector<std::string>&) {
        if (!m_game.Active()) return;
        m_game.PS().god = !m_game.PS().god;
        LOG_INFO("god mode %s", m_game.PS().god ? "ON" : "OFF");
    }, "invulnerability");
    cvars::AddCommand("give", [this](const std::vector<std::string>& a) {
        if (!m_game.Active() || a.size() < 2) return;
        if (a[1] == "all") {
            for (auto& w : m_game.Scripts().weapons) {
                m_game.GiveWeapon(w.id, false);
                if (!w.ammoType.empty()) m_game.GiveAmmo(w.ammoType, 200);
            }
            if (m_game.Player()) m_game.Player()->health = 200, m_game.Player()->armor = 200;
        } else if (a.size() >= 3) {
            m_game.GiveAmmo(a[1], atoi(a[2].c_str()));
        } else {
            m_game.GiveWeapon(a[1], true);
        }
    }, "give <weapon> | give <ammo> <n> | give all");
    cvars::AddCommand("weapon", [this](const std::vector<std::string>& a) {
        if (!m_game.Active() || a.size() < 2) return;
        int idx = m_game.Scripts().WeaponIndex(a[1]);
        if (idx < 0) {
            LOG_WARN("Unknown weapon %s", a[1].c_str());
            return;
        }
        auto& ps = m_game.PS();
        if ((int)ps.hasWeapon.size() <= idx || !ps.hasWeapon[idx]) {
            LOG_WARN("You don't have %s (try: give %s)", a[1].c_str(), a[1].c_str());
            return;
        }
        ps.weapon = ps.pendingWeapon = idx;
        ps.switchAnim = 0;
    }, "weapon <id> - switch weapon (pistol, shotgun, plasma, rocket...)");
    cvars::AddCommand("kill", [this](const std::vector<std::string>&) {
        if (m_game.Player()) m_game.Damage(m_game.Player(), 1000, nullptr, {0, 0, 0}, m_game.Player()->pos);
    }, "suicide");
    cvars::AddCommand("setpos", [this](const std::vector<std::string>& a) {
        if (!m_game.Player() || a.size() < 4) return;
        m_game.Player()->pos = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        m_game.Player()->vel = {0, 0, 0};
        if (a.size() >= 5) m_game.PS().yaw = (float)atof(a[4].c_str());
        if (a.size() >= 6) m_game.PS().pitch = (float)atof(a[5].c_str());
    }, "setpos x y z [yaw] [pitch]");
    cvars::AddCommand("fire", [this](const std::vector<std::string>&) {
        if (!m_game.Active() || !m_game.Player()) return;
        int w = m_game.PS().weapon;
        if (w >= 0) m_game.Scripts().FireWeapon(w, m_game.Player());
        m_game.PS().kick = 1.0f;
        m_game.PS().muzzleFlash = 0.06f;
    }, "fire the current weapon once (testing)");
    cvars::AddCommand("lua", [this](const std::vector<std::string>&) {
        std::string code = cvars::CommandArgs();
        if (m_game.Active()) m_game.Scripts().RunString(code);
        else LOG_WARN("lua: no game running");
    }, "lua <code> - run Lua in the game");
    cvars::AddCommand("mod", [this](const std::vector<std::string>& a) {
        if (a.size() < 2) {
            LOG_INFO("Active mod: %s", vfs::CurrentMod().empty() ? "(base)" : vfs::CurrentMod().c_str());
            for (auto& m : vfs::ListMods()) LOG_INFO("  %s", m.c_str());
            return;
        }
        ReturnToMenu();
        vfs::SetMod(a[1] == "base" ? "" : a[1]);
        assets::ReloadAll();
        gRenderer.ReloadShaders();
        plugins::LoadAll();
        fonts::UnloadAll();
        ReloadLanguage();
        ApplyGameConfig();
    }, "mod <name|base> - switch the active mod (no argument: list)");
    cvars::AddCommand("plugins", [](const std::vector<std::string>&) {
        auto list = plugins::Loaded();
        if (list.empty()) LOG_INFO("No native plugins loaded (put .dll/.so/.dylib files into plugins/)");
        for (auto& p : list) LOG_INFO("  %s", p.c_str());
    }, "list loaded native plugins");
    cvars::AddCommand("reload_plugins", [this](const std::vector<std::string>&) {
        if (m_game.Active()) {
            LOG_WARN("Plugins can only be reloaded outside of a running game (editor or menu)");
            return;
        }
        plugins::LoadAll();
    }, "reload native plugins (hot reload)");
    cvars::AddCommand("reload_shaders", [](const std::vector<std::string>&) { gRenderer.ReloadShaders(); }, "recompile shaders");
    cvars::AddCommand("screenshot", [](const std::vector<std::string>& a) {
        std::string name = a.size() > 1 ? a[1] : StrFormat("screenshot_%d.png", (int)time(nullptr));
        rlDrawRenderBatchActive();
        Image shot = LoadImageFromScreen();
        ExportImage(shot, name.c_str());
        UnloadImage(shot);
        LOG_INFO("Saved %s", name.c_str());
    }, "screenshot [file.png]");
    cvars::AddCommand("vid_restart", [](const std::vector<std::string>&) {
        int w = (int)cvars::GetF("vid_width", 1600), h = (int)cvars::GetF("vid_height", 900);
        SetWindowSize(w, h);
    }, "apply vid_width/vid_height");
}

// ---------------------------------------------------------------------------------
// main loop
// ---------------------------------------------------------------------------------
void App::Run() {
    while (!m_quit && !WindowShouldClose()) {
        float dt = GetFrameTime();
        int w = GetScreenWidth(), h = GetScreenHeight();
        if (IsKeyPressed(KEY_GRAVE) || (IsKeyPressed(KEY_F1) && m_state != State::Editor)) m_console.Toggle();
        bool consoleInput = m_console.Update();

        if (m_state == State::Game) {
            if (m_game.Loading()) {
                m_game.Update(dt, false);
            } else {
                if (!consoleInput && IsKeyPressed(KEY_ESCAPE)) {
                    if (m_fromEditor) {
                        m_game.Unload();
                        m_state = State::Editor;
                        m_editor->OnReturnFromPlay();
                        EnableCursor();
                        goto draw;
                    }
                    if (!m_game.LevelEnded()) {
                        m_paused = !m_paused;
                        m_page = MenuPage::Pause;
                        m_menuSel = 0;
                    }
                }
                bool input = !m_paused && !consoleInput && !m_game.LevelEnded();
                if (!m_paused) m_game.Update(dt, input);
                if (m_game.RestartRequested()) m_game.Respawn();
                if (m_game.Active() && !m_execDone && !m_game.Loading()) {
                    m_execDone = true;
                    RunStartupExec();
                }
                if (m_game.LevelEnded() && m_page != MenuPage::Intermission) {
                    m_page = MenuPage::Intermission;
                    m_intermissionTime = 0;
                    m_lastStats = m_game.Stats();
                    m_menuSel = 0;
                }
                if (m_page == MenuPage::Intermission && m_game.LevelEnded()) {
                    m_intermissionTime += dt;
                    if (m_intermissionTime > 1.0f && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                        std::string next = m_game.PendingMap();
                        if (m_fromEditor) {
                            m_game.Unload();
                            m_state = State::Editor;
                            m_editor->OnReturnFromPlay();
                        } else if (next.empty() || next == "*") {
                            ReturnToMenu();
                        } else {
                            StartMap(next, true);
                        }
                    }
                }
            }
            if (m_paused && m_page == MenuPage::Pause && !consoleInput) UpdateMenu(dt);
            SetCursor(!m_paused && !m_game.LevelEnded() && !m_game.Loading());
        } else if (m_state == State::Menu) {
            SetCursor(false);
            if (!consoleInput) UpdateMenu(dt);
        } else if (m_state == State::Editor) {
            m_editor->Update(dt, consoleInput);
        }
        // files dropped on the window are imported by the editor only; elsewhere drop them
        if (m_state != State::Editor && IsFileDropped()) UnloadDroppedFiles(LoadDroppedFiles());
        // --exec outside of a game (menu, editor) runs once the first frame is up
        if (m_state != State::Game && !m_execDone && m_frame > 1) {
            m_execDone = true;
            RunStartupExec();
        }

    draw:
        BeginDrawing();
        ClearBackground(BLACK);
        if (m_state == State::Game) {
            if (m_game.Loading()) DrawLoading(w, h);
            else {
                m_game.Draw(w, h);
                if (m_paused || m_page == MenuPage::Intermission) DrawMenu(w, h);
            }
        } else if (m_state == State::Menu) {
            DrawMenu(w, h);
        } else if (m_state == State::Editor) {
            m_editor->Draw(w, h);
        }
        m_console.Draw(w, h);
        m_frame++;
        if (!m_opt.screenshot.empty() && (m_state != State::Game || (!m_game.Loading() && m_execDone))) {
            static int readyFrames = 0;
            if (++readyFrames >= m_opt.screenshotFrames) {
                rlDrawRenderBatchActive();
                Image shot = LoadImageFromScreen();
                ExportImage(shot, m_opt.screenshot.c_str());
                UnloadImage(shot);
                LOG_INFO("Screenshot saved: %s", m_opt.screenshot.c_str());
                m_quit = true;
            }
        }
        EndDrawing();
    }
}

// ---------------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------------
bool App::MenuItem(const char* label, int index, int x, int y, int size, bool enabled) {
    float tw = fonts::Width("title", label, size);
    Rectangle r{(float)x - 12, (float)y - 4, tw + 24, (float)size + 8};
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (hover && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) m_menuSel = index;
    bool sel = m_menuSel == index;
    Color c = !enabled ? Color{90, 80, 70, 255} : (sel ? kGold : Color{200, 185, 165, 255});
    if (sel && enabled) {
        DrawRectangleGradientH((int)r.x - 20, (int)r.y, (int)r.width + 40, (int)r.height, Color{120, 40, 20, 0}, Color{120, 40, 20, 140});
        fonts::Text("title", ">", (float)x - 34, (float)y, size, kGold);
    }
    fonts::Text("title", label, (float)x + 2, (float)y + 2, size, Color{0, 0, 0, 180});
    fonts::Text("title", label, (float)x, (float)y, size, c);
    bool activate = enabled && sel && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)));
    return activate;
}

void App::UpdateMenu(float dt) {
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) m_menuSel++;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) m_menuSel--;
    if (IsKeyPressed(KEY_ESCAPE) && m_state == State::Menu && m_page != MenuPage::Main) {
        m_page = MenuPage::Main;
        m_menuSel = 0;
    }
    // embers
    if (m_embers.size() < 90 && GetRandomValue(0, 3) == 0)
        m_embers.push_back({{RandRange(0, (float)GetScreenWidth()), (float)GetScreenHeight() + 10}, {RandRange(-20, 20), RandRange(-90, -30)}, RandRange(3, 8), RandRange(1, 3)});
    for (auto& e : m_embers) {
        e.p.x += (e.v.x + sinf((float)GetTime() * 2 + e.size * 10) * 15) * dt;
        e.p.y += e.v.y * dt;
        e.life -= dt;
    }
    m_embers.erase(std::remove_if(m_embers.begin(), m_embers.end(), [](const Ember& e) { return e.life <= 0; }), m_embers.end());
}

void App::DrawMenuBackground(int w, int h, float alpha) {
    DrawRectangleGradientV(0, 0, w, h, Color{30, 10, 6, (unsigned char)(255 * alpha)}, Color{6, 4, 4, (unsigned char)(255 * alpha)});
    for (auto& e : m_embers) {
        unsigned char a = (unsigned char)(Clampf(e.life / 3.0f, 0, 1) * 200 * alpha);
        DrawCircleV(e.p, e.size, Color{255, 120, 40, a});
    }
}

void App::DrawLoading(int w, int h) {
    DrawMenuBackground(w, h, 1);
    float p = m_game.LoadProgress();
    const char* t = Tr("Baking lighting...");
    fonts::Text("title", t, w * 0.5f - fonts::Width("title", t, 28) * 0.5f, h * 0.5f - 50, 28, kGold);
    DrawRectangle(w / 2 - 200, h / 2, 400, 14, Color{40, 30, 25, 255});
    DrawRectangle(w / 2 - 200, h / 2, (int)(400 * p), 14, Color{200, 110, 40, 255});
    DrawRectangleLines(w / 2 - 200, h / 2, 400, 14, Color{120, 90, 60, 255});
    fonts::Text("ui", TextFormat("%d %%", (int)(p * 100)), w * 0.5f - 12, h * 0.5f + 24, 16, kDim);
}

void App::DrawMenu(int w, int h) {
    bool inGame = m_state == State::Game;
    if (inGame) DrawRectangle(0, 0, w, h, Color{0, 0, 0, 150});
    else DrawMenuBackground(w, h, 1);

    int x = (int)(w * 0.12f), y = (int)(h * 0.18f);
    if (!inGame) {
        fonts::Text("title", "RBS", (float)x + 4, (float)y + 4, 110, Color{0, 0, 0, 200});
        fonts::Text("title", "RBS", (float)x, (float)y, 110, Color{200, 60, 30, 255});
        fonts::Text("title", "ENGINE", (float)x + 230, (float)y + 58, 40, kGold);
        // sub-pages put their own title where the tagline is
        if (m_page == MenuPage::Main) fonts::Text("ui", Tr("retro boomer shooter engine"), (float)x + 6, (float)y + 120, 18, kDim);
        y += 190;
    } else {
        y = (int)(h * 0.3f);
    }
    int size = 34, step = 50;
    int idx = 0;

    if (m_page == MenuPage::Intermission) {
        const char* t = Tr("LEVEL COMPLETE");
        fonts::Text("title", t, w * 0.5f - fonts::Width("title", t, 48) * 0.5f, h * 0.2f, 48, kGold);
        auto line = [&](const char* label, const std::string& val, float yy) {
            fonts::Text("title", label, w * 0.5f - 220, yy, 30, Color{200, 185, 165, 255});
            fonts::Text("title", val.c_str(), w * 0.5f + 120, yy, 30, kGold);
        };
        int mins = (int)m_lastStats.time / 60, secs = (int)m_lastStats.time % 60;
        line(Tr("Kills"), StrFormat("%d / %d", m_lastStats.kills, m_lastStats.monsters), h * 0.36f);
        line(Tr("Secrets"), StrFormat("%d / %d", m_lastStats.secrets, m_lastStats.secretsTotal), h * 0.36f + 46);
        line(Tr("Items"), StrFormat("%d / %d", m_lastStats.items, m_lastStats.itemsTotal), h * 0.36f + 92);
        line(Tr("Time"), StrFormat("%d:%02d", mins, secs), h * 0.36f + 138);
        if (m_intermissionTime > 1.0f && fmodf((float)GetTime(), 1.0f) < 0.7f) {
            const char* c = Tr("Press ENTER to continue");
            fonts::Text("ui", c, w * 0.5f - fonts::Width("ui", c, 20) * 0.5f, h * 0.75f, 20, kDim);
        }
        return;
    }

    if (m_page == MenuPage::Main || m_page == MenuPage::Pause) {
        if (m_page == MenuPage::Pause) {
            fonts::Text("title", Tr("PAUSED"), (float)x, (float)y - 80, 48, kGold);
            int n = 4;
            m_menuSel = (m_menuSel % n + n) % n;
            if (MenuItem(Tr("Resume"), idx++, x, y, size)) m_paused = false;
            if (MenuItem(Tr("Restart level"), idx++, x, y + step, size)) {
                m_paused = false;
                m_game.Respawn();
            }
            if (MenuItem(Tr("Main menu"), idx++, x, y + step * 2, size)) ReturnToMenu();
            if (MenuItem(Tr("Quit"), idx++, x, y + step * 3, size)) m_quit = true;
            return;
        }
        int n = 6;
        m_menuSel = (m_menuSel % n + n) % n;
        if (MenuItem(Tr("New game"), idx++, x, y, size)) StartMap(cvars::Find("game_start_map")->value);
        if (MenuItem(Tr("Maps"), idx++, x, y + step, size)) {
            m_page = MenuPage::Maps;
            m_menuSel = 0;
            m_mapList = vfs::List("maps", ".json");
        }
        if (MenuItem(Tr("Level editor"), idx++, x, y + step * 2, size)) OpenEditor("");
        if (MenuItem(Tr("Mods"), idx++, x, y + step * 3, size)) {
            m_page = MenuPage::Mods;
            m_menuSel = 0;
            m_modList.clear();
            for (auto& folder : vfs::ListMods()) {
                ModInfo info{folder, folder, "", ""};
                std::ifstream f(std::filesystem::path(vfs::Root()) / "mods" / folder / "mod.json");
                auto j = nlohmann::json::parse(f, nullptr, false, true);
                if (j.is_object()) {
                    info.title = j.value("title", folder);
                    info.description = j.value("description", "");
                    info.author = j.value("author", "");
                }
                std::error_code ec;
                for (auto& e : std::filesystem::directory_iterator(std::filesystem::path(vfs::Root()) / "mods" / folder / "plugins", ec)) {
                    std::string ext = ToLower(e.path().extension().string());
                    if (ext == ".dll" || ext == ".so" || ext == ".dylib") info.nativePlugins++;
                }
                m_modList.push_back(info);
            }
        }
        if (MenuItem(Tr("Options"), idx++, x, y + step * 4, size)) {
            m_page = MenuPage::Options;
            m_menuSel = 0;
        }
        if (MenuItem(Tr("Quit"), idx++, x, y + step * 5, size)) m_quit = true;
        std::string mod = vfs::CurrentMod().empty() ? "base" : vfs::CurrentMod();
        fonts::Text("ui", TextFormat("v%s   mod: %s   F1 = %s", RBS_VERSION, mod.c_str(), Tr("console")), 16, h - 30.0f, 16, kDim);
        return;
    }

    if (m_page == MenuPage::Maps) {
        fonts::Text("title", Tr("MAPS"), (float)x, (float)y - 70, 40, kGold);
        int n = (int)m_mapList.size() + 1;
        m_menuSel = (m_menuSel % n + n) % n;
        int visible = std::max(1, (h - y - 60) / 40);
        int first = std::max(0, std::min(m_menuSel - visible / 2, n - visible));
        for (int i = first; i < n && i < first + visible; ++i) {
            int yy = y + (i - first) * 40;
            if (i < (int)m_mapList.size()) {
                std::string name = std::filesystem::path(m_mapList[i]).stem().string();
                if (MenuItem(name.c_str(), i, x, yy, 28)) StartMap(m_mapList[i]);
            } else if (MenuItem(Tr("Back"), i, x, yy, 28)) {
                m_page = MenuPage::Main;
                m_menuSel = 1;
            }
        }
        return;
    }

    if (m_page == MenuPage::Mods) {
        fonts::Text("title", Tr("MODS"), (float)x, (float)y - 70, 40, kGold);
        int n = (int)m_modList.size() + 2;
        m_menuSel = (m_menuSel % n + n) % n;
        std::string cur = vfs::CurrentMod();
        if (MenuItem(cur.empty() ? "* base" : "  base", 0, x, y, 28)) cvars::Execute("mod base");
        for (size_t i = 0; i < m_modList.size(); ++i) {
            const ModInfo& m = m_modList[i];
            std::string label = (cur == m.folder ? "* " : "  ") + m.title;
            if (MenuItem(label.c_str(), (int)i + 1, x, y + (int)(i + 1) * 40, 28)) cvars::Execute("mod " + m.folder);
        }
        if (MenuItem(Tr("Back"), n - 1, x, y + (n - 1) * 40 + 10, 28)) {
            m_page = MenuPage::Main;
            m_menuSel = 3;
        }
        float infoY = (float)y + n * 40 + 40;
        if (m_menuSel == 0) {
            fonts::Text("ui", Tr("The base game without mods."), (float)x, infoY, 18, kDim);
        } else if (m_menuSel <= (int)m_modList.size()) {
            const ModInfo& m = m_modList[m_menuSel - 1];
            std::string info = "mods/" + m.folder + (m.author.empty() ? "" : "   -   " + m.author);
            fonts::Text("ui", info.c_str(), (float)x, infoY, 16, kDim);
            // word-wrapped description
            std::string line;
            float ly = infoY + 24, maxW = w - x - 40.0f;
            std::istringstream words(m.description);
            for (std::string word; words >> word;) {
                std::string next = line.empty() ? word : line + " " + word;
                if (!line.empty() && fonts::Width("ui", next.c_str(), 18) > maxW) {
                    fonts::Text("ui", line.c_str(), (float)x, ly, 18, Color{200, 185, 165, 255});
                    ly += 24;
                    next = word;
                }
                line = next;
            }
            if (!line.empty()) fonts::Text("ui", line.c_str(), (float)x, ly, 18, Color{200, 185, 165, 255});
            if (m.nativePlugins > 0)
                fonts::Text("ui", Tr("Contains native plugins: they run with full access to your computer. Only use mods you trust."), (float)x,
                            ly + 30, 16, Color{255, 170, 60, 255});
        }
        fonts::Text("ui", Tr("Mods live in the mods/ folder. A mod can override or add any file."), (float)x, h - 60.0f, 16, kDim);
        return;
    }

    if (m_page == MenuPage::Options) {
        fonts::Text("title", Tr("OPTIONS"), (float)x, (float)y - 70, 40, kGold);
        struct Opt {
            const char* label;
            const char* cvar;
            float mn, mx, step;
        };
        static const Opt opts[] = {
            {"Mouse sensitivity", "sensitivity", 0.5f, 15, 0.25f}, {"Field of view", "fov", 70, 130, 5},
            {"Sound volume", "s_volume", 0, 1, 0.05f},              {"Music volume", "s_musicvolume", 0, 1, 0.05f},
            {"Render scale", "r_scale", 0.25f, 1, 0.05f},           {"Retro palette (levels)", "r_posterize", 0, 64, 4},
            {"Texture filtering", "r_texfilter", 0, 2, 1},          {"Gamma", "r_gamma", 0.5f, 2.0f, 0.05f},
            {"Gore", "fx_gore", 0, 2, 0.25f},                        {"Show debug info", "hud_debug", 0, 1, 1},
            {"Fullscreen", "vid_fullscreen", 0, 1, 1}};
        int n = (int)(sizeof(opts) / sizeof(opts[0])) + 2;
        m_menuSel = (m_menuSel % n + n) % n;
        int rowH = 34;
        for (int i = 0; i < n - 2; ++i) {
            const Opt& o = opts[i];
            Cvar* cv = cvars::Find(o.cvar);
            if (!cv) cv = cvars::Register(o.cvar, "0", true, "");
            int yy = y + i * rowH;
            bool sel = m_menuSel == i;
            Rectangle row{(float)x - 12, (float)yy - 2, 760, (float)rowH};
            if (CheckCollisionPointRec(GetMousePosition(), row) && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) m_menuSel = i;
            fonts::Text("title", Tr(o.label), (float)x, (float)yy, 24, sel ? kGold : Color{200, 185, 165, 255});
            float v = cv->f;
            float t = (v - o.mn) / (o.mx - o.mn);
            Rectangle bar{(float)x + 420, (float)yy + 8, 220, 10};
            DrawRectangleRec(bar, Color{50, 35, 28, 255});
            DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * Clampf(t, 0, 1)), (int)bar.height, Color{200, 110, 40, 255});
            fonts::Text("ui", TextFormat("%g", v), bar.x + bar.width + 14, (float)yy + 2, 18, kDim);
            float delta = 0;
            if (sel && (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))) delta = o.step;
            if (sel && (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))) delta = -o.step;
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), {bar.x - 4, bar.y - 8, bar.width + 8, bar.height + 16})) {
                float nt = Clampf((GetMousePosition().x - bar.x) / bar.width, 0, 1);
                float nv = o.mn + nt * (o.mx - o.mn);
                nv = roundf(nv / o.step) * o.step;
                cv->Set(TextFormat("%g", nv));
            }
            if (delta != 0) cv->Set(TextFormat("%g", Clampf(roundf((v + delta) / o.step) * o.step, o.mn, o.mx)));
            if (delta != 0 || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (std::string(o.cvar) == "r_texfilter") assets::ApplyTextureFilter();
                if (std::string(o.cvar) == "vid_fullscreen" && ((cv->i != 0) != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))) ToggleBorderlessWindowed();
            }
        }
        Cvar* lang = cvars::Find("lang");
        if (!lang) lang = cvars::Register("lang", "en", true, "");
        const char* langLabel = lang->value == "cs" ? "Jazyk: Čeština" : "Language: English";
        if (MenuItem(langLabel, n - 2, x, y + (n - 2) * rowH + 10, 24)) lang->Set(lang->value == "cs" ? "en" : "cs");
        if (MenuItem(Tr("Back"), n - 1, x, y + (n - 1) * rowH + 20, 28)) {
            cvars::SaveConfig();
            m_page = m_state == State::Game ? MenuPage::Pause : MenuPage::Main;
            m_menuSel = 0;
        }
        return;
    }
}

}  // namespace rbs
