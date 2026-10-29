#include "editor/editor.h"
#include "app/app.h"

namespace rbs {

struct Editor::Impl {
    Map map;
    std::string path;
};

Editor::Editor(App* app) : m_app(app), m(new Impl) {}
Editor::~Editor() { delete m; }
void Editor::Open(const std::string& mapPath) {
    m->path = mapPath;
    if (!mapPath.empty()) m->map.Load(mapPath);
}
void Editor::Update(float, bool) {
    if (IsKeyPressed(KEY_F5)) m_app->PlayFromEditor(m->map, "editor", nullptr, 0);
}
void Editor::Draw(int w, int h) { DrawText("Editor (work in progress)", 20, 20, 20, RAYWHITE); }
void Editor::OnReturnFromPlay() {}

}  // namespace rbs
