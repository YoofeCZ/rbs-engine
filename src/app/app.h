// RBS Engine - application: main menu, game session, editor, console.
#pragma once

#include "game/game.h"
#include "ui/console.h"

namespace rbs {

class Editor;

struct LaunchOptions {
    std::string map;
    std::string mod;
    bool editor = false;
    std::string editorMap;
    std::string screenshot;  // automated capture (testing / docs)
    int screenshotFrames = 90;
    std::string exec;        // console commands executed after the map loaded
    int width = 1600, height = 900;
    bool fullscreen = false;
};

class App {
public:
    App();
    ~App();
    bool Init(const LaunchOptions& opt);
    void Run();
    void Shutdown();

    // used by console commands and the editor
    void StartMap(const std::string& relPath, bool keepInventory = false);
    void PlayFromEditor(const Map& map, const std::string& name, Vector3* spawnPos, float spawnYaw);
    void OpenEditor(const std::string& map);
    void ReturnToMenu();
    Game& GetGame() { return m_game; }
    // Opens another game project (folder in mods/, "base" = the base game).
    void SwitchProject(const std::string& folder, bool openEditor, const std::string& editorMap);
    // after the project's mod.json changed (start map, title...)
    void RefreshProjectSettings();
    void Quit() { m_quit = true; }

private:
    enum class State { Menu, Game, Editor };
    enum class MenuPage { Main, Maps, Mods, Options, Pause, Intermission };
    State m_state = State::Menu;
    MenuPage m_page = MenuPage::Main;
    Game m_game;
    Editor* m_editor = nullptr;
    Console m_console;
    LaunchOptions m_opt;
    bool m_quit = false;
    bool m_paused = false;
    bool m_fromEditor = false;
    int m_menuSel = 0;
    float m_menuScroll = 0;
    int m_frame = 0;
    bool m_execDone = false;
    std::vector<std::string> m_mapList;
    struct ModInfo {
        std::string folder, title, description, author;
        int nativePlugins = 0;  // .dll/.so/.dylib files in the mod's plugins/ folder
    };
    std::vector<ModInfo> m_modList;  // filled when the Mods page opens (from mods/*/mod.json)
    float m_intermissionTime = 0;
    GameStats m_lastStats;
    struct Ember {
        Vector2 p, v;
        float life, size;
    };
    std::vector<Ember> m_embers;

    void RegisterCommands();
    void UpdateMenu(float dt);
    void DrawMenu(int w, int h);
    void DrawMenuBackground(int w, int h, float alpha);
    void DrawLoading(int w, int h);
    bool MenuItem(const char* label, int index, int x, int y, int size, bool enabled = true);
    void SetCursor(bool gameplay);
    void RunStartupExec();
    void ApplyGameConfig();
    std::string WindowTitle() const;
    bool ShowEditorInMenu() const;
    bool ShowModsInMenu() const;
    void OpenModsPage();
};

}  // namespace rbs
