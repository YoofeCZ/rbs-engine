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

    struct Impl;  // editor internals (editor_impl.h)

private:
    App* m_app;
    Impl* m;
};

}  // namespace rbs
