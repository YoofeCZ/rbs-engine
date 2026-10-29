// RBS Engine - editor internals (shared between the editor_*.cpp files).
#pragma once

#include "editor/editor.h"
#include "render/lightmap.h"
#include "render/renderer.h"
#include "render/world_renderer.h"
#include "script/script.h"

#include <map>
#include <set>

namespace rbs {

class App;

enum class ViewKind { Top, Front, Side, Persp };
enum class Tool { Select, Brush, Entity, Texture, Face, Clip };

struct EditorView {
    ViewKind kind = ViewKind::Top;
    Rectangle rect{};
    // 2D
    Vector2 center{0, 0};
    float zoom = 0.25f;  // pixels per world unit
    // 3D
    Vector3 camPos{-400, 300, 400};
    float yaw = -45, pitch = -20;
    RenderTexture2D rt{};
    Camera3D Camera() const;
};

struct FaceRef {
    int brush = -1, face = -1;
    bool operator<(const FaceRef& o) const { return brush < o.brush || (brush == o.brush && face < o.face); }
    bool operator==(const FaceRef& o) const { return brush == o.brush && face == o.face; }
};

struct Editor::Impl {
    App* app = nullptr;
    Editor* owner = nullptr;
    Map map;
    std::string path;
    bool dirty = false;

    Script script;  // class/component metadata (no game attached)
    std::vector<ClassInfo> classes, components;
    std::vector<std::string> classNames, componentNames;

    WorldRenderer world;
    bool meshDirty = true;
    Lightmap lightmap;
    Map bakeMap;
    bool baking = false, litPreview = false;
    WorldRenderer litWorld;

    // selection
    std::set<int> selBrushes, selEnts;
    std::set<FaceRef> selFaces;

    Tool tool = Tool::Select;
    std::string curTexture = "concrete";
    std::string curClass = "light";
    float grid = 16;
    float newBrushMin = 0, newBrushMax = 128;  // depth range for brushes drawn in 2D views

    EditorView views[4];
    int maximized = -1;  // -1 = 2x2 layout
    int hotView = -1;    // view under the mouse

    // interaction
    enum class Drag { None, Move, Resize, Create, Marquee, Pan, Look, Gizmo };
    Drag drag = Drag::None;
    int dragView = -1;
    Vector2 dragStartMouse{};
    Vector3 dragStartWorld{}, dragLastApplied{};
    int resizeHandle = -1;  // 0..7
    BoundingBox dragStartBounds{};
    int gizmoAxis = -1;
    Vector3 gizmoAccum{};
    std::vector<Vector3> clipPoints;  // clip tool (world, on view plane)
    int clipView = -1;
    int clipMode = 0;  // 0 both, 1 front, 2 back

    // undo
    std::vector<std::string> undo, redo;

    // panels
    int bottomTab = 0;  // 0 project, 1 console
    std::string projectFolder = "Textures";
    std::string projectSearch, hierSearch, consoleInput;
    std::map<std::string, bool> open;
    std::string dialog;  // "", "open", "saveas", "prefab", "tie"
    std::string dialogText;
    std::string status;
    double statusTime = 0;
    std::vector<std::string> textureList, modelList, soundList, spriteList, scriptList, mapList, prefabList;
    double lastListRefresh = -100;
    std::string newPropKey, newPropValue;
    std::string clipboard;

    // ---- core (editor.cpp) ----
    void Init();
    void RefreshClasses();
    void RefreshLists(bool force = false);
    void NewMap();
    bool OpenMap(const std::string& path);
    bool SaveMap(const std::string& path);
    void Snapshot();
    void Undo();
    void Redo();
    void Changed(bool geometry = true);
    void SetStatus(const std::string& s);

    const ClassInfo* ClassFor(const std::string& name) const;
    const ClassInfo* ComponentFor(const std::string& name) const;
    BoundingBox EntityBox(int ent) const;
    bool HasSelection() const { return !selBrushes.empty() || !selEnts.empty(); }
    bool SelectionBounds(BoundingBox& out) const;
    void ClearSelection();
    void SelectAllOfEntity(int ent);
    void DeleteSelection();
    void DuplicateSelection(Vector3 offset);
    std::string CopySelectionJson(Vector3 origin) const;
    void PasteJson(const std::string& text, Vector3 origin);
    void MoveSelection(Vector3 delta);
    void ResizeSelection(const BoundingBox& from, const BoundingBox& to);
    void RotateSelection(int axis, float degrees);
    void FlipSelection(int axis);
    void HollowSelection();
    void SubtractSelection();
    void ClipSelection(const Plane& p, int keep);
    void TieToEntity(const std::string& cls);
    void MoveToWorld();
    void ApplyTexture(const std::string& tex, int brush, int face);
    int CreateBrush(Vector3 mn, Vector3 mx);
    int CreateEntity(const std::string& cls, Vector3 pos);
    void SavePrefab(const std::string& name);
    void InsertPrefab(const std::string& relPath, Vector3 at);
    void StartBake();
    void UpdateBake();
    void Play(bool fromCamera);
    Vector3 SnapV(Vector3 v) const { return {Snap(v.x, grid), Snap(v.y, grid), Snap(v.z, grid)}; }

    // ---- views (editor_views.cpp) ----
    void LayoutViews(Rectangle area);
    void FitViews();
    bool fitPending = true;
    void UpdateViews(bool keyboardFree);
    void DrawViews();
    void UpdateView2D(int vi);
    void UpdateView3D(int vi, bool keyboardFree);
    void DrawView2D(int vi);
    void DrawView3D(int vi);
    Vector2 WorldToScreen2D(const EditorView& v, Vector3 p) const;
    Vector3 ScreenToWorld2D(const EditorView& v, Vector2 s, Vector3 keep) const;
    int AxisU(const EditorView& v) const;
    int AxisV(const EditorView& v) const;
    int AxisDepth(const EditorView& v) const;
    bool PickRay(Ray ray, int* brush, int* face, int* ent, float* dist) const;
    bool Pick2D(int vi, Vector2 mouse, bool add);
    void HandleDrop(int vi);
    Vector3 PlacementPoint(int vi, Vector2 mouse, const ClassInfo* ci);

    // ---- panels (editor_panels.cpp) ----
    void DrawToolbar(Rectangle r);
    void DrawHierarchy(Rectangle r);
    void DrawInspector(Rectangle r);
    void DrawBottom(Rectangle r);
    void DrawProject(Rectangle r);
    void DrawConsole(Rectangle r);
    void DrawDialogs(int w, int h);
    void DrawStatusBar(Rectangle r);
    float InspectEntity(Rectangle r, float y);
    float InspectBrushes(Rectangle r, float y);
    float InspectFaces(Rectangle r, float y);
    float InspectMap(Rectangle r, float y);
    float PropEditor(const std::string& id, Rectangle r, float y, json& props, const json& defaults, bool allowRemove);
    void HandleShortcuts();
};

}  // namespace rbs
