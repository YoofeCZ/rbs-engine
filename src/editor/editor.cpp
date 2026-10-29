// RBS Engine - level editor: core state, map operations, undo/redo.
#include "editor/editor_impl.h"
#include "app/app.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/vfs.h"
#include "script/plugins.h"
#include "ui/imgui.h"

#include <filesystem>
#include <fstream>

namespace rbs {

Camera3D EditorView::Camera() const {
    Camera3D c{};
    c.position = camPos;
    c.target = camPos + AnglesForward(pitch, yaw);
    c.up = {0, 1, 0};
    c.fovy = 70;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

// ---------------------------------------------------------------------------------
// Editor public API
// ---------------------------------------------------------------------------------
Editor::Editor(App* app) : m_app(app), m(new Impl) {
    m->app = app;
    m->owner = this;
}

Editor::~Editor() {
    m->ReleaseDocuments();
    for (auto& v : m->views)
        if (v.rt.id) UnloadRenderTexture(v.rt);
    delete m;
}

void Editor::Open(const std::string& mapPath) {
    m->Init();
    if (mapPath.empty() || !m->OpenMap(mapPath)) m->NewMap();
}

void Editor::Import(const std::string& path) { m->ImportPaths({path}, false); }


void Editor::OnReturnFromPlay() {
    m->RefreshClasses();
    m->meshDirty = true;
    m->litPreview = false;
    EnableCursor();
}

void Editor::Update(float dt, bool consoleHasFocus) {
    if (!m->pendingProject.empty()) {
        // switching projects reloads every asset, so it happens outside of drawing
        std::string folder = m->pendingProject, map = m->pendingProjectMap;
        m->pendingProject.clear();
        m->app->SwitchProject(folder, true, map);
        return;
    }
    if (IsCursorHidden() && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) EnableCursor();
    m->UpdateBake();
    m->HandleDroppedFiles();
    bool keyboardFree = !consoleHasFocus && !ui::KeyboardCaptured() && m->dialog.empty();
    if (keyboardFree && m->activeDoc < 0) m->HandleShortcuts();
    if (m->dialog.empty() && m->activeDoc < 0) m->UpdateViews(keyboardFree);
    if (keyboardFree && m->activeDoc >= 0) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        Document& d = *m->Doc();
        if (ctrl && IsKeyPressed(KEY_S)) m->SaveDocument(d);
        else if (ctrl && IsKeyPressed(KEY_Z)) shift ? m->DocRedo(d) : m->DocUndo(d);
        else if (ctrl && IsKeyPressed(KEY_Y)) m->DocRedo(d);
        else if (ctrl && IsKeyPressed(KEY_C)) m->DocCopy(d, false);
        else if (ctrl && IsKeyPressed(KEY_X)) m->DocCopy(d, true);
        else if (ctrl && IsKeyPressed(KEY_V)) m->DocPaste(d);
        else if (ctrl && IsKeyPressed(KEY_D)) m->DocDuplicate(d);
    }
    if (m->meshDirty) {
        m->world.Build(m->map, nullptr, true);
        m->meshDirty = false;
    }
    m->RefreshLists();
}

void Editor::Draw(int w, int h) {
    ui::BeginFrame();
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    ClearBackground(ui::theme.bg);
    const float tb = 36, sb = 24, left = 250, right = 340, bottom = 230;
    const float tabH = 26;
    Rectangle viewArea{left, tb + tabH, w - left - right, h - tb - sb - bottom - tabH};
    if (m->activeDoc < 0) {
        m->LayoutViews(viewArea);
        m->DrawViews();
    } else {
        m->DrawDocument(viewArea);
    }
    m->DrawTabs({left, tb, w - left - right, tabH});
    m->DrawToolbar({0, 0, (float)w, tb});
    m->DrawHierarchy({0, tb, left, h - tb - sb});
    m->DrawInspector({w - right, tb, right, h - tb - sb});
    m->DrawBottom({left, h - sb - bottom, w - left - right, bottom});
    m->DrawStatusBar({0, h - sb, (float)w, sb});
    // dialogs and the bake progress are modal: the panels under them ignore the mouse
    if (!m->dialog.empty()) {
        ui::BeginModal();
        m->DrawDialogs(w, h);
        ui::EndModal();
    }
    if (m->baking) {
        ui::BeginModal();
        Rectangle r{w * 0.5f - 200, h * 0.5f - 40, 400, 80};
        ui::Fill({0, 0, (float)w, (float)h}, Color{0, 0, 0, 120});
        ui::Fill(r, ui::theme.panelAlt);
        ui::Outline(r, ui::theme.border);
        ui::Text("Baking lighting...", r.x + 16, r.y + 14, ui::theme.text, 16, "ui_bold");
        ui::Fill({r.x + 16, r.y + 46, r.width - 32, 12}, ui::theme.field);
        ui::Fill({r.x + 16, r.y + 46, (r.width - 32) * m->lightmap.Progress(), 12}, ui::theme.accent);
        ui::EndModal();
    }
    if (m->Doc()) m->UpdateUndo(*m->Doc());  // after every panel had its say in this frame
    ui::EndFrame();
}

// ---------------------------------------------------------------------------------
// setup / files
// ---------------------------------------------------------------------------------
void Editor::Impl::Init() {
    RefreshClasses();
    views[0].kind = ViewKind::Top;
    views[1].kind = ViewKind::Persp;
    views[2].kind = ViewKind::Front;
    views[3].kind = ViewKind::Side;
    for (int i = 0; i < 4; ++i)
        if (views[i].kind != ViewKind::Persp) views[i].zoom = 0.3f;
    open["inspector_props"] = true;
    open["inspector_components"] = true;
    open["inspector_transform"] = true;
    open["hier_world"] = false;
    open["hier_entities"] = true;
    RefreshLists(true);
}

void Editor::Impl::RefreshClasses() {
    script.Shutdown();
    script.Init(nullptr);
    classes = script.classes;
    for (auto& c : plugins::ClassInfos()) classes.push_back(c);
    components = script.components;
    for (auto& c : plugins::ComponentInfos()) components.push_back(c);
    // hide internal classes from the palette
    classes.erase(std::remove_if(classes.begin(), classes.end(), [](const ClassInfo& c) {
                      return c.name == "player" || c.name == "gib" || c.name == "item_base" || c.name == "monster_base" ||
                             c.name == "plasma_bolt" || c.name == "rocket" || c.name == "fireball";
                  }),
                  classes.end());
    std::sort(classes.begin(), classes.end(), [](const ClassInfo& a, const ClassInfo& b) {
        return a.category == b.category ? a.name < b.name : a.category < b.category;
    });
    classNames.clear();
    for (auto& c : classes) classNames.push_back(c.name);
    componentNames.clear();
    for (auto& c : components) componentNames.push_back(c.name);
    weaponIds.clear();
    for (auto& w : script.weapons) weaponIds.push_back(w.id);
    btLibrary = script.LuaJson("bt_library");
    if (!btLibrary.is_object()) btLibrary = json::object();
    flowLibrary = script.LuaJson("flow_library");
    if (!flowLibrary.is_object()) flowLibrary = json::object();
    cutsceneDefaults = script.LuaJson("cutscene_defaults");
    if (!cutsceneDefaults.is_object()) cutsceneDefaults = json::object();
    dialogueDefaults = script.LuaJson("dialogue_defaults");
    if (!dialogueDefaults.is_object()) dialogueDefaults = json::object();
    weaponFields = script.LuaJson("weapon_fields");
    if (!weaponFields.is_object()) weaponFields = json::object();
}

void Editor::Impl::RefreshLists(bool force) {
    if (!force && GetTime() - lastListRefresh < 3.0) return;
    lastListRefresh = GetTime();
    textureList = assets::ListTextures();
    auto strip = [](std::vector<std::string> v) { return v; };
    modelList = strip(vfs::List("models", ".glb", true));
    for (auto& f : vfs::List("models", ".gltf", true)) modelList.push_back(f);
    for (auto& f : vfs::List("models", ".obj", true)) modelList.push_back(f);
    for (const char* ext : {".iqm", ".m3d", ".vox"})
        for (auto& f : vfs::List("models", ext, true)) modelList.push_back(f);
    soundList = vfs::List("sounds", ".wav", true);
    for (const char* ext : {".ogg", ".mp3", ".flac", ".qoa"})
        for (auto& f : vfs::List("sounds", ext, true)) soundList.push_back(f);
    spriteList = vfs::List("sprites", ".png", true);
    scriptList = vfs::List("scripts", ".lua", true);
    mapList = vfs::List("maps", ".json", false);
    prefabList = vfs::List("prefabs", ".json", true);
    RefreshAnimatorList();
    RefreshBehaviorList();
    RefreshFlowLists();
}

// ---------------------------------------------------------------------------------
// importing files dragged in from the OS (like dropping assets into Unity's Project)
// ---------------------------------------------------------------------------------
std::string Editor::Impl::ImportFile(const std::string& srcPath, int& count) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path src(srcPath);
    if (!fs::exists(src, ec)) {
        LOG_WARN("Import: %s not found", srcPath.c_str());
        return "";
    }
    fs::path root = fs::path(vfs::WriteDir());
    auto copy = [&](const fs::path& from, const fs::path& to) {
        fs::create_directories(to.parent_path(), ec);
        if (fs::exists(to, ec) && fs::equivalent(from, to, ec)) return true;
        if (!fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec)) {
            LOG_WARN("Import failed: %s -> %s (%s)", from.string().c_str(), to.string().c_str(), ec.message().c_str());
            return false;
        }
        LOG_INFO("Imported %s", to.string().c_str());
        ++count;
        return true;
    };
    if (fs::is_directory(src, ec)) {
        // a folder with a glTF/OBJ model keeps its layout (the model references its .bin/textures)
        bool modelFolder = false;
        for (auto& e : fs::directory_iterator(src, ec)) {
            std::string ext = ToLower(e.path().extension().string());
            if (ext == ".gltf" || ext == ".obj") modelFolder = true;
        }
        std::string last;
        if (modelFolder) {
            for (auto& e : fs::recursive_directory_iterator(src, ec)) {
                if (!e.is_regular_file()) continue;
                fs::path rel = fs::relative(e.path(), src, ec);
                fs::path dst = root / "models" / src.filename() / rel;
                std::string ext = ToLower(e.path().extension().string());
                if (copy(e.path(), dst) && (ext == ".gltf" || ext == ".obj"))
                    last = "model:" + fs::relative(dst, root, ec).generic_string();
            }
            return last;
        }
        for (auto& e : fs::directory_iterator(src, ec)) {
            std::string r = ImportFile(e.path().string(), count);
            if (!r.empty()) last = r;
        }
        return last;
    }
    std::string ext = ToLower(src.extension().string());
    std::string name = src.filename().string();
    auto is = [&](std::initializer_list<const char*> list) {
        for (const char* e : list)
            if (ext == e) return true;
        return false;
    };
    std::string folder, type;
    if (is({".png", ".tga", ".jpg", ".jpeg", ".bmp", ".qoi"})) {
        bool sprite = projectFolder == "Sprites";
        folder = sprite ? "sprites" : "textures";
        type = sprite ? "sprite" : "texture";
        if (ext == ".jpeg") name = src.stem().string() + ".jpg";
    } else if (is({".glb", ".gltf", ".obj", ".iqm", ".m3d", ".vox"})) {
        folder = "models", type = "model";
    } else if (is({".wav", ".ogg", ".mp3", ".flac", ".qoa", ".xm", ".mod"})) {
        folder = "sounds", type = "sound";
    } else if (is({".ttf", ".otf"})) {
        folder = "fonts";
    } else if (ext == ".lua") {
        folder = "scripts/autorun";
    } else if (ext == ".json") {
        std::ifstream f(src);
        json j = json::parse(f, nullptr, false, true);
        std::string format = j.is_object() && j.contains("format") && j["format"].is_string() ? j["format"].get<std::string>() : "";
        if (format == "rbs-map") folder = "maps", type = "map";
        else if (format == "rbs-prefab") folder = "prefabs", type = "prefab";
        else if (j.is_object() && src.parent_path().filename() == "textures") folder = "textures";  // texture meta
    }
    if (folder.empty()) {
        LOG_WARN("Import: unknown file type %s", srcPath.c_str());
        return "";
    }
    fs::path dst = root / folder / name;
    if (!copy(src, dst)) return "";
    if (type == "texture") {
        std::string tex = fs::path(name).stem().string();
        assets::ReloadTexture(tex);
        return "texture:" + tex;
    }
    return type.empty() ? "" : type + ":" + folder + "/" + name;
}

void Editor::Impl::HandleDroppedFiles() {
    if (!IsFileDropped()) return;
    FilePathList files = LoadDroppedFiles();
    std::vector<std::string> paths(files.paths, files.paths + files.count);
    UnloadDroppedFiles(files);
    ImportPaths(paths, true);
}

void Editor::Impl::ImportPaths(const std::vector<std::string>& paths, bool placeAtMouse) {
    int count = 0;
    std::string last;
    bool scripts = false;
    for (auto& p : paths) {
        std::string ext = ToLower(std::filesystem::path(p).extension().string());
        if (ext == ".lua" || std::filesystem::is_directory(p)) scripts = true;
        std::string r = ImportFile(p, count);
        if (!r.empty()) last = r;
    }
    if (count == 0) {
        SetStatus("Nothing imported (see the console)");
        return;
    }
    RefreshLists(true);
    if (scripts) RefreshClasses();
    meshDirty = true;  // re-texture faces whose image changed
    std::string where = vfs::CurrentMod().empty() ? "base/" : "mods/" + vfs::CurrentMod() + "/";
    SetStatus(StrFormat("Imported %d file(s) into %s", count, where.c_str()));
    // a single asset dropped over a view is placed there, like dragging it from the Project panel
    size_t colon = last.find(':');
    if (paths.size() != 1 || colon == std::string::npos) return;
    std::string type = last.substr(0, colon), payload = last.substr(colon + 1);
    if (type == "texture") projectFolder = "Textures", curTexture = payload;
    else if (type == "model") projectFolder = "Models";
    else if (type == "sound") projectFolder = "Sounds";
    else if (type == "sprite") projectFolder = "Sprites";
    else if (type == "prefab") projectFolder = "Prefabs";
    if (type == "map") {
        projectFolder = "Maps";
        return;
    }
    if (!placeAtMouse) return;
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < 4; ++i)
        if (views[i].rect.width > 0 && CheckCollisionPointRec(mouse, views[i].rect) && (maximized < 0 || maximized == i)) {
            DropAsset(i, mouse, type, payload);
            break;
        }
}

void Editor::Impl::NewMap() {
    map.MakeStarterRoom();
    path.clear();
    dirty = false;
    undo.clear();
    redo.clear();
    ClearSelection();
    meshDirty = true;
    litPreview = false;
    // 3D view starts inside the starter room, behind the player start, looking in
    views[1].camPos = {0, 110, 470};
    views[1].yaw = 90;
    views[1].pitch = -12;
    fitPending = true;
    SetStatus("New map");
}

bool Editor::Impl::OpenMap(const std::string& p) {
    std::string err;
    Map m2;
    if (!m2.Load(p, &err)) {
        SetStatus("Cannot open " + p + ": " + err);
        LOG_ERR("Editor: cannot open %s: %s", p.c_str(), err.c_str());
        return false;
    }
    map = m2;
    path = p;
    dirty = false;
    undo.clear();
    redo.clear();
    ClearSelection();
    meshDirty = true;
    litPreview = false;
    for (auto& e : map.entities) {
        if (e.classname == "player_start") {
            views[1].camPos = e.origin + Vector3{0, 60, 0};
            views[1].yaw = e.angle;
            views[1].pitch = -10;
            break;
        }
    }
    fitPending = true;
    SetStatus("Opened " + p);
    return true;
}

bool Editor::Impl::SaveMap(const std::string& p) {
    std::string target = p;
    if (!StartsWith(target, "maps/")) target = "maps/" + target;
    if (!EndsWith(target, ".json")) target += ".json";
    if (!map.Save(target)) {
        SetStatus("Save failed: " + target);
        return false;
    }
    path = target;
    dirty = false;
    RefreshLists(true);
    SetStatus("Saved " + target);
    return true;
}

void Editor::Impl::SetStatus(const std::string& s) {
    status = s;
    statusTime = GetTime();
    LOG_INFO("[editor] %s", s.c_str());
}

void Editor::Impl::Snapshot() {
    undo.push_back(map.ToJson().dump());
    if (undo.size() > 120) undo.erase(undo.begin());
    redo.clear();
}

void Editor::Impl::Undo() {
    if (undo.empty()) return;
    redo.push_back(map.ToJson().dump());
    map.FromJson(json::parse(undo.back()));
    undo.pop_back();
    ClearSelection();
    Changed();
    SetStatus("Undo");
}

void Editor::Impl::Redo() {
    if (redo.empty()) return;
    undo.push_back(map.ToJson().dump());
    map.FromJson(json::parse(redo.back()));
    redo.pop_back();
    ClearSelection();
    Changed();
    SetStatus("Redo");
}

void Editor::Impl::Changed(bool geometry) {
    dirty = true;
    if (geometry) {
        meshDirty = true;
        litPreview = false;
    }
}

// ---------------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------------
const ClassInfo* Editor::Impl::ClassFor(const std::string& name) const {
    for (auto& c : classes)
        if (c.name == name) return &c;
    return script.FindClass(name);
}

const ClassInfo* Editor::Impl::ComponentFor(const std::string& name) const {
    for (auto& c : components)
        if (c.name == name) return &c;
    return nullptr;
}

BoundingBox Editor::Impl::EntityBox(int ei) const {
    const MapEntity& e = map.entities[ei];
    bool any = false;
    BoundingBox bb{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (auto& b : map.brushes) {
        if (b.entity != ei) continue;
        bb.min = VMin(bb.min, b.bounds.min);
        bb.max = VMax(bb.max, b.bounds.max);
        any = true;
    }
    if (any) return bb;
    const ClassInfo* ci = ClassFor(e.classname);
    Vector3 mn = ci ? ci->mins : Vector3{-8, -8, -8}, mx = ci ? ci->maxs : Vector3{8, 8, 8};
    return {e.origin + mn, e.origin + mx};
}

bool Editor::Impl::SelectionBounds(BoundingBox& out) const {
    bool any = false;
    out = {{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (int b : selBrushes) {
        if (b < 0 || b >= (int)map.brushes.size()) continue;
        out.min = VMin(out.min, map.brushes[b].bounds.min);
        out.max = VMax(out.max, map.brushes[b].bounds.max);
        any = true;
    }
    for (int e : selEnts) {
        if (e < 0 || e >= (int)map.entities.size()) continue;
        BoundingBox bb = EntityBox(e);
        out.min = VMin(out.min, bb.min);
        out.max = VMax(out.max, bb.max);
        any = true;
    }
    return any;
}

void Editor::Impl::ClearSelection() {
    selBrushes.clear();
    selEnts.clear();
    selFaces.clear();
}

void Editor::Impl::SelectAllOfEntity(int ent) {
    selEnts.insert(ent);
    for (size_t i = 0; i < map.brushes.size(); ++i)
        if (map.brushes[i].entity == ent) selBrushes.insert((int)i);
}

// ---------------------------------------------------------------------------------
// operations
// ---------------------------------------------------------------------------------
void Editor::Impl::DeleteSelection() {
    if (!HasSelection()) return;
    Snapshot();
    std::vector<int> brushes(selBrushes.begin(), selBrushes.end());
    std::sort(brushes.rbegin(), brushes.rend());
    for (int b : brushes)
        if (b >= 0 && b < (int)map.brushes.size()) map.brushes.erase(map.brushes.begin() + b);
    std::vector<int> ents(selEnts.begin(), selEnts.end());
    std::sort(ents.rbegin(), ents.rend());
    for (int e : ents) map.RemoveEntity(e);
    ClearSelection();
    Changed();
    SetStatus("Deleted");
}

std::string Editor::Impl::CopySelectionJson(Vector3 origin) const {
    Map tmp;
    tmp.settings = map.settings;
    std::map<int, int> remap;
    for (int e : selEnts) {
        if (e < 0 || e >= (int)map.entities.size()) continue;
        MapEntity me = map.entities[e];
        me.origin -= origin;
        remap[e] = (int)tmp.entities.size();
        tmp.entities.push_back(me);
    }
    for (int b : selBrushes) {
        if (b < 0 || b >= (int)map.brushes.size()) continue;
        Brush br = map.brushes[b];
        auto it = remap.find(br.entity);
        br.entity = it == remap.end() ? -1 : it->second;
        br.Translate(-origin);
        tmp.brushes.push_back(br);
    }
    json j = tmp.ToJson();
    j.erase("settings");
    j["format"] = "rbs-prefab";
    return j.dump(1);
}

void Editor::Impl::PasteJson(const std::string& text, Vector3 origin) {
    json j = json::parse(text, nullptr, false, true);
    if (j.is_discarded()) return;
    Map tmp;
    if (!tmp.FromJson(j)) return;
    Snapshot();
    ClearSelection();
    int entBase = (int)map.entities.size();
    for (auto& e : tmp.entities) {
        MapEntity me = e;
        me.origin += origin;
        selEnts.insert((int)map.entities.size());
        map.entities.push_back(me);
    }
    for (auto& b : tmp.brushes) {
        Brush br = b;
        br.Translate(origin);
        if (br.entity >= 0) br.entity += entBase;
        selBrushes.insert((int)map.brushes.size());
        map.brushes.push_back(br);
    }
    Changed();
}

void Editor::Impl::DuplicateSelection(Vector3 offset) {
    if (!HasSelection()) return;
    BoundingBox bb;
    SelectionBounds(bb);
    Vector3 o = SnapV(bb.min);
    std::string j = CopySelectionJson(o);
    PasteJson(j, o + offset);
    SetStatus("Duplicated");
}

void Editor::Impl::MoveSelection(Vector3 d) {
    for (int b : selBrushes)
        if (b >= 0 && b < (int)map.brushes.size()) map.brushes[b].Translate(d);
    for (int e : selEnts)
        if (e >= 0 && e < (int)map.entities.size()) map.entities[e].origin += d;
    Changed();
}

void Editor::Impl::ResizeSelection(const BoundingBox& from, const BoundingBox& to) {
    Vector3 fs = from.max - from.min, ts = to.max - to.min;
    auto mapP = [&](Vector3 p) {
        Vector3 r = p;
        for (int a = 0; a < 3; ++a) {
            float f = Comp(fs, a);
            if (f > 0.001f) SetComp(r, a, Comp(to.min, a) + (Comp(p, a) - Comp(from.min, a)) * Comp(ts, a) / f);
        }
        return r;
    };
    for (int b : selBrushes)
        if (b >= 0 && b < (int)map.brushes.size()) map.brushes[b].Transform(mapP);
    for (int e : selEnts) {
        if (e < 0 || e >= (int)map.entities.size()) continue;
        bool hasBrushes = false;
        for (auto& br : map.brushes)
            if (br.entity == e) hasBrushes = true;
        if (hasBrushes) map.entities[e].origin = mapP(map.entities[e].origin);
        else if (selEnts.size() > 1 || !selBrushes.empty()) map.entities[e].origin = mapP(map.entities[e].origin);
    }
    Changed();
}

void Editor::Impl::RotateSelection(int axis, float deg) {
    BoundingBox bb;
    if (!SelectionBounds(bb)) return;
    Snapshot();
    Vector3 c = SnapV((bb.min + bb.max) * 0.5f);
    Vector3 ax = axis == 0 ? Vector3{1, 0, 0} : (axis == 1 ? Vector3{0, 1, 0} : Vector3{0, 0, 1});
    float rad = deg * DEG2RAD;
    auto rot = [&](Vector3 p) {
        Vector3 r = Vector3RotateByAxisAngle(p - c, ax, rad) + c;
        // keep coordinates clean for right angles
        if (fmodf(fabsf(deg), 90.0f) < 0.001f) r = {roundf(r.x * 1000) / 1000, roundf(r.y * 1000) / 1000, roundf(r.z * 1000) / 1000};
        return r;
    };
    for (int b : selBrushes)
        if (b >= 0 && b < (int)map.brushes.size()) map.brushes[b].Transform(rot);
    for (int e : selEnts) {
        if (e < 0 || e >= (int)map.entities.size()) continue;
        map.entities[e].origin = rot(map.entities[e].origin);
        if (axis == 1) map.entities[e].angle = fmodf(map.entities[e].angle + deg + 360.0f, 360.0f);
    }
    Changed();
}

void Editor::Impl::FlipSelection(int axis) {
    BoundingBox bb;
    if (!SelectionBounds(bb)) return;
    Snapshot();
    Vector3 c = (bb.min + bb.max) * 0.5f;
    auto flip = [&](Vector3 p) {
        SetComp(p, axis, 2 * Comp(c, axis) - Comp(p, axis));
        return p;
    };
    for (int b : selBrushes)
        if (b >= 0 && b < (int)map.brushes.size()) map.brushes[b].Transform(flip);
    for (int e : selEnts)
        if (e >= 0 && e < (int)map.entities.size()) map.entities[e].origin = flip(map.entities[e].origin);
    Changed();
}

void Editor::Impl::HollowSelection() {
    if (selBrushes.empty()) return;
    Snapshot();
    std::vector<Brush> out;
    std::set<int> newSel;
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        if (!selBrushes.count((int)i)) {
            out.push_back(map.brushes[i]);
            continue;
        }
        for (auto& b : HollowBrush(map.brushes[i], std::max(4.0f, grid))) {
            b.entity = map.brushes[i].entity;
            newSel.insert((int)out.size());
            out.push_back(b);
        }
    }
    map.brushes = out;
    selBrushes = newSel;
    Changed();
    SetStatus("Hollowed");
}

void Editor::Impl::SubtractSelection() {
    if (selBrushes.empty()) return;
    Snapshot();
    std::vector<Brush> cutters;
    for (int b : selBrushes) cutters.push_back(map.brushes[b]);
    std::vector<Brush> out;
    int changedCount = 0;
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        if (selBrushes.count((int)i)) continue;  // cutters are removed
        std::vector<Brush> pieces{map.brushes[i]};
        for (auto& c : cutters) {
            std::vector<Brush> next;
            for (auto& p : pieces) {
                if (!p.Intersects(c)) {
                    next.push_back(p);
                    continue;
                }
                changedCount++;
                for (auto& f : SubtractBrush(p, c)) next.push_back(f);
            }
            pieces = next;
        }
        for (auto& p : pieces) out.push_back(p);
    }
    map.brushes = out;
    ClearSelection();
    Changed();
    SetStatus(StrFormat("Subtracted from %d brush(es)", changedCount));
}

void Editor::Impl::ClipSelection(const Plane& p, int keep) {
    if (selBrushes.empty()) return;
    Snapshot();
    std::vector<Brush> out;
    std::set<int> newSel;
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        if (!selBrushes.count((int)i)) {
            out.push_back(map.brushes[i]);
            continue;
        }
        Brush f, b;
        bool hf = false, hb = false;
        FaceTex cap;
        cap.name = map.brushes[i].faces.empty() ? curTexture : map.brushes[i].faces[0].tex.name;
        SplitBrush(map.brushes[i], p, cap, &f, &hf, &b, &hb);
        if (hf && keep != 2) {
            f.entity = map.brushes[i].entity;
            newSel.insert((int)out.size());
            out.push_back(f);
        }
        if (hb && keep != 1) {
            b.entity = map.brushes[i].entity;
            newSel.insert((int)out.size());
            out.push_back(b);
        }
    }
    map.brushes = out;
    selBrushes = newSel;
    Changed();
    SetStatus("Clipped");
}

void Editor::Impl::TieToEntity(const std::string& cls) {
    if (selBrushes.empty()) return;
    Snapshot();
    BoundingBox bb{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (int b : selBrushes) bb.min = VMin(bb.min, map.brushes[b].bounds.min), bb.max = VMax(bb.max, map.brushes[b].bounds.max);
    MapEntity e;
    e.classname = cls;
    e.origin = SnapV((bb.min + bb.max) * 0.5f);
    int idx = (int)map.entities.size();
    map.entities.push_back(e);
    bool trigger = StartsWith(cls, "trigger_");
    for (int b : selBrushes) {
        map.brushes[b].entity = idx;
        if (trigger)
            for (auto& f : map.brushes[b].faces) f.tex.name = "trigger";
    }
    selEnts.clear();
    selEnts.insert(idx);
    Changed();
    SetStatus("Created brush entity " + cls);
}

void Editor::Impl::MoveToWorld() {
    if (selBrushes.empty()) return;
    Snapshot();
    std::set<int> owners;
    for (int b : selBrushes) {
        if (map.brushes[b].entity >= 0) owners.insert(map.brushes[b].entity);
        map.brushes[b].entity = -1;
    }
    // remove brush entities that lost all their brushes
    std::vector<int> empty;
    for (int o : owners) {
        bool has = false;
        for (auto& b : map.brushes)
            if (b.entity == o) has = true;
        if (!has) empty.push_back(o);
    }
    std::sort(empty.rbegin(), empty.rend());
    for (int e : empty) map.RemoveEntity(e);
    selEnts.clear();
    Changed();
    SetStatus("Moved to world");
}

void Editor::Impl::ApplyTexture(const std::string& tex, int brush, int face) {
    if (brush < 0 || brush >= (int)map.brushes.size()) return;
    Brush& b = map.brushes[brush];
    if (face >= 0 && face < (int)b.faces.size()) b.faces[face].tex.name = tex;
    else
        for (auto& f : b.faces) f.tex.name = tex;
    Changed();
}

int Editor::Impl::CreateBrush(Vector3 mn, Vector3 mx) {
    Brush b = MakeBoxBrush(VMin(mn, mx), VMax(mn, mx), curTexture);
    if (!b.BuildPolygons()) return -1;
    Snapshot();
    map.brushes.push_back(b);
    Changed();
    return (int)map.brushes.size() - 1;
}

int Editor::Impl::CreateEntity(const std::string& cls, Vector3 pos) {
    const ClassInfo* ci = ClassFor(cls);
    if (ci && ci->brush) {
        // brush entities need a brush: create a default sized one
        Vector3 h{32, 32, 32};
        int b = CreateBrush(pos - h, pos + h);
        if (b < 0) return -1;
        ClearSelection();
        selBrushes.insert(b);
        TieToEntity(cls);
        return (int)map.entities.size() - 1;
    }
    Snapshot();
    MapEntity e;
    e.classname = cls;
    e.origin = pos;
    map.entities.push_back(e);
    Changed(false);
    meshDirty = true;
    return (int)map.entities.size() - 1;
}

void Editor::Impl::SavePrefab(const std::string& name) {
    if (!HasSelection() || name.empty()) return;
    BoundingBox bb;
    SelectionBounds(bb);
    Vector3 origin = SnapV({(bb.min.x + bb.max.x) * 0.5f, bb.min.y, (bb.min.z + bb.max.z) * 0.5f});
    std::string rel = "prefabs/" + name + (EndsWith(name, ".json") ? "" : ".json");
    if (vfs::WriteText(rel, CopySelectionJson(origin))) SetStatus("Saved prefab " + rel);
    RefreshLists(true);
}

void Editor::Impl::InsertPrefab(const std::string& rel, Vector3 at) {
    std::string text;
    if (!vfs::ReadText(rel, text)) return;
    PasteJson(text, SnapV(at));
    SetStatus("Inserted " + rel);
}

void Editor::Impl::StartBake() {
    if (baking) return;
    bakeMap = map;
    std::string name = path.empty() ? "editor_untitled" : std::filesystem::path(path).stem().string();
    lightmap.Start(bakeMap, name);
    baking = true;
}

void Editor::Impl::UpdateBake() {
    if (!baking || !lightmap.Done()) return;
    lightmap.Finish();
    litWorld.Build(bakeMap, &lightmap, true);
    baking = false;
    litPreview = true;
    SetStatus("Lighting baked - preview shows the baked lightmap until the next edit");
}

void Editor::Impl::Play(bool fromCamera) {
    std::string name = path.empty() ? "untitled" : std::filesystem::path(path).stem().string();
    Vector3 pos = views[1].camPos;
    float yaw = views[1].yaw;
    if (!fromCamera) {
        bool hasStart = false;
        for (auto& e : map.entities)
            if (e.classname == "player_start") hasStart = true;
        if (!hasStart) {
            SetStatus("No player_start in the map - playing from the camera");
            fromCamera = true;
        }
    }
    ui::ClearFocus();
    app->PlayFromEditor(map, name, fromCamera ? &pos : nullptr, yaw);
}

}  // namespace rbs
