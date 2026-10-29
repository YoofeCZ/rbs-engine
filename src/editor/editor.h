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

private:
    App* m_app;
    struct Impl;
    Impl* m;
};

}  // namespace rbs
