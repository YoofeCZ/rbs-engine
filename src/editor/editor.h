// RBS Engine - level editor (Unity-style layout: hierarchy, scene views, inspector, project, console).
#pragma once

#include "world/map.h"

#include <string>
#include <vector>

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
    // opens an animator in a graph tab (console: edit_animator <name>)
    void OpenAnimator(const std::string& name);
    // scripted editing for tests and automation (console: animator <command> ...); returns a message
    std::string AnimatorCommand(const std::vector<std::string>& args);
    // same for behavior trees (console: behavior <command> ...)
    std::string BehaviorCommand(const std::vector<std::string>& args);
    void OpenBehavior(const std::string& name);
    // quest and dialogue graphs (console: flow <command> ...)
    std::string FlowCommand(const std::vector<std::string>& args);
    void OpenFlow(const std::string& kind, const std::string& name);

    struct Impl;  // editor internals (editor_impl.h)

private:
    App* m_app;
    Impl* m;
};

}  // namespace rbs
