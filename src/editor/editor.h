// RBS Engine - level editor (Unity-style layout: hierarchy, scene views, inspector, project, console).
#pragma once

#include "world/map.h"

namespace rbs {

class App;

class Editor {
public:
    explicit Editor(App* app);
    ~Editor();
    void Open(const std::string& mapPath);  // "" = new map
    void Update(float dt, bool consoleHasFocus);
    void Draw(int w, int h);
    void OnReturnFromPlay();
    // copies a file or folder from disk into the active mod (or base) by type: images to
    // textures/, models to models/, sounds to sounds/... (same as dropping it on the window)
    void Import(const std::string& path);

    struct Impl;  // editor internals (editor_impl.h)

private:
    App* m_app;
    Impl* m;
};

}  // namespace rbs
