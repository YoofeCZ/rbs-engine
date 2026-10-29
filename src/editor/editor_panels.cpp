// RBS Engine - editor panels: toolbar, hierarchy, inspector, project browser, console.
#include "app/app.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/project.h"
#include "core/vfs.h"
#include "editor/editor_impl.h"
#include "render/assets.h"
#include "script/plugins.h"
#include "ui/fonts.h"
#include "ui/imgui.h"

#include <filesystem>

namespace rbs {

using namespace ui;

static const float kRow = 22;
static std::string g_lastSnapKey;
static double g_lastSnapTime = 0;
static std::map<std::string, float> g_contentHeight;

static void SnapshotCoalesced(Editor::Impl* ed, const std::string& key) {
    if (key != g_lastSnapKey || GetTime() - g_lastSnapTime > 1.0) ed->Snapshot();
    g_lastSnapKey = key;
    g_lastSnapTime = GetTime();
}

static void PanelTitle(Rectangle r, const char* title) {
    Fill({r.x, r.y, r.width, 24}, theme.header);
    Text(title, r.x + 8, r.y + 5, theme.textDim, 12, "ui_bold");
}

static std::string Lower(std::string s) { return ToLower(std::move(s)); }

static bool Matches(const std::string& name, const std::string& filter) {
    return filter.empty() || Lower(name).find(Lower(filter)) != std::string::npos;
}

// ---------------------------------------------------------------------------------
// toolbar
// ---------------------------------------------------------------------------------
void Editor::Impl::DrawToolbar(Rectangle r) {
    Block(r);
    Fill(r, theme.panel);
    DrawLine((int)r.x, (int)(r.y + r.height - 1), (int)(r.x + r.width), (int)(r.y + r.height - 1), theme.border);
    float x = 8, y = r.y + 5, h = r.height - 10;
    auto btn = [&](const char* id, const char* label, float w, bool sel = false, bool enabled = true) {
        bool c = Button(id, {x, y, w, h}, label, sel, enabled);
        x += w + 4;
        return c;
    };
    auto sep = [&]() {
        DrawLine((int)x + 2, (int)y + 2, (int)x + 2, (int)(y + h - 2), theme.border);
        x += 10;
    };
    if (btn("new", "New", 44)) NewMap();
    if (btn("open", "Open", 50)) dialog = "open";
    if (btn("save", "Save", 46)) {
        if (path.empty()) dialog = "saveas", dialogText = "mymap";
        else SaveMap(path);
    }
    if (btn("saveas", "Save As", 64)) dialog = "saveas", dialogText = path.empty() ? "mymap" : std::filesystem::path(path).stem().string();
    sep();
    // with a graph open, Undo / Redo act on that graph; on the Level tab on the map
    Document* gd = Doc();
    if (btn("undo", "Undo", 48, false, gd ? !gd->undo.empty() || gd->stamp != gd->committed : !undo.empty())) gd ? DocUndo(*gd) : Undo();
    if (btn("redo", "Redo", 48, false, gd ? !gd->redo.empty() : !redo.empty())) gd ? DocRedo(*gd) : Redo();
    sep();
    struct T {
        Tool t;
        const char* label;
        const char* tip;
    };
    const T tools[] = {{Tool::Select, "Select V", "Select, move (drag), resize (handles), gizmo in 3D"},
                       {Tool::Brush, "Brush B", "Draw new brushes in a 2D view (drag) or click a surface in 3D"},
                       {Tool::Entity, "Entity E", "Place the current entity class (pick it in Project > Entities)"},
                       {Tool::Texture, "Texture T", "Click a face in 3D to paint the current texture (Shift = whole brush, Alt = pick)"},
                       {Tool::Face, "Face F", "Select faces in 3D to edit texture offset/scale/rotation"},
                       {Tool::Clip, "Clip C", "Click two points in a 2D view, then ENTER to split the selected brushes"}};
    for (auto& t : tools) {
        Rectangle br{x, y, 74, h};
        if (btn(t.label, t.label, 74, tool == t.t)) {
            tool = t.t;
            clipPoints.clear();
        }
        if (Hovered(br)) Tooltip(t.tip);
    }
    sep();
    if (btn("hollow", "Hollow", 60, false, !selBrushes.empty())) HollowSelection();
    if (btn("subtract", "Subtract", 70, false, !selBrushes.empty())) SubtractSelection();
    if (Hovered({x - 74, y, 70, h})) Tooltip("CSG: carve the selected brushes out of every brush they touch");
    if (btn("tie", "To Entity", 74, false, !selBrushes.empty())) dialog = "tie", dialogText = "func_door";
    sep();
    Text("Grid", x, y + 6, theme.textDim);
    x += 34;
    if (btn("gridm", "-", 22)) grid = std::max(1.0f, grid * 0.5f);
    Fill({x, y, 40, h}, theme.field);
    TextCentered(TextFormat("%g", grid), {x, y, 40, h}, theme.text);
    x += 44;
    if (btn("gridp", "+", 22)) grid = std::min(256.0f, grid * 2.0f);
    sep();
    if (btn("bake", baking ? "Baking..." : "Bake Light", 84, litPreview, !baking)) StartBake();
    if (Hovered({x - 88, y, 84, h})) Tooltip("Bake lightmaps (shadows, bounce light) and preview them in the 3D view (Ctrl+B)");
    if (btn("play", "Play  F5", 74, false)) Play(false);
    if (btn("playhere", "Play here F6", 96, false)) Play(true);
    std::string title = (path.empty() ? std::string("untitled") : path) + (dirty ? " *" : "");
    float tw = TextWidth(title);
    Fill({r.width - tw - 24, y, tw + 16, h}, theme.field);
    Text(title, r.width - tw - 16, y + 6, dirty ? theme.warn : theme.text);
}

// ---------------------------------------------------------------------------------
// hierarchy
// ---------------------------------------------------------------------------------
void Editor::Impl::DrawHierarchy(Rectangle r) {
    Block(r);
    Fill(r, theme.panel);
    DrawLine((int)(r.x + r.width - 1), (int)r.y, (int)(r.x + r.width - 1), (int)(r.y + r.height), theme.border);
    PanelTitle(r, "HIERARCHY");
    // the open game project; opens the Project dialog (new / open / settings / build)
    const project::Info& proj = project::Current();
    std::string projLabel = proj.folder.empty() ? "Project: base game (demo)" : "Project: " + proj.title;
    if (Button("projbtn", {r.x + 6, r.y + 28, r.width - 12, 24}, Ellipsize(projLabel, r.width - 24, 13), !proj.folder.empty())) OpenProjectDialog();
    if (Hovered({r.x + 6, r.y + 28, r.width - 12, 24})) Tooltip("New project, open project, settings, build the game");
    TextField("hier_search", {r.x + 6, r.y + 56, r.width - 12, 22}, hierSearch, "Search...");

    struct Row {
        int kind;  // 0 header, 1 entity, 2 brush, 3 map settings
        int index;
        int depth;
        std::string label;
        std::string headerKey;
        Color color;
    };
    std::vector<Row> rows;
    rows.push_back({3, -1, 0, "Map settings", "", Color{160, 170, 190, 255}});
    // entities by category
    std::map<std::string, std::vector<int>> cats;
    for (size_t i = 0; i < map.entities.size(); ++i) {
        const ClassInfo* ci = ClassFor(map.entities[i].classname);
        std::string cat = ci ? ci->category : "unknown";
        std::string name = map.entities[i].classname + " " + map.entities[i].Str("targetname");
        if (!Matches(name, hierSearch)) continue;
        cats[cat].push_back((int)i);
    }
    for (auto& kv : cats) {
        std::string key = "hcat_" + kv.first;
        if (!open.count(key)) open[key] = true;
        rows.push_back({0, -1, 0, kv.first + "  (" + std::to_string(kv.second.size()) + ")", key, theme.textDim});
        if (!open[key]) continue;
        for (int e : kv.second) {
            const MapEntity& me = map.entities[e];
            const ClassInfo* ci = ClassFor(me.classname);
            std::string label = me.classname;
            std::string tn = me.Str("targetname");
            if (!tn.empty()) label += "  '" + tn + "'";
            if (!me.components.empty()) label += "  +" + std::to_string(me.components.size());
            rows.push_back({1, e, 1, label, "", ci ? ci->color : Color{200, 200, 200, 255}});
            std::string ek = "hent_" + std::to_string(e);
            for (size_t b = 0; b < map.brushes.size(); ++b)
                if (map.brushes[b].entity == e && open[ek])
                    rows.push_back({2, (int)b, 2, "brush #" + std::to_string(b), "", Color{150, 160, 220, 255}});
        }
    }
    int worldCount = 0;
    for (auto& b : map.brushes)
        if (b.entity < 0) worldCount++;
    rows.push_back({0, -1, 0, "World brushes  (" + std::to_string(worldCount) + ")", "hier_world", theme.textDim});
    if (open["hier_world"]) {
        for (size_t b = 0; b < map.brushes.size(); ++b) {
            if (map.brushes[b].entity >= 0) continue;
            std::string tex = map.brushes[b].faces.empty() ? "" : map.brushes[b].faces[0].tex.name;
            std::string label = "brush #" + std::to_string(b) + "  " + tex;
            if (!Matches(label, hierSearch)) continue;
            rows.push_back({2, (int)b, 1, label, "", Color{110, 150, 235, 255}});
        }
    }

    Rectangle area{r.x, r.y + 84, r.width, r.height - 84};
    float scroll = BeginScroll("hier", area, rows.size() * 20.0f + 8);
    for (size_t i = 0; i < rows.size(); ++i) {
        Row& row = rows[i];
        Rectangle rr{area.x, area.y + 4 + i * 20.0f - scroll, area.width, 20};
        if (rr.y + rr.height < area.y || rr.y > area.y + area.height) continue;
        bool sel = (row.kind == 1 && selEnts.count(row.index)) || (row.kind == 2 && selBrushes.count(row.index)) ||
                   (row.kind == 3 && !HasSelection() && selFaces.empty());
        bool hov = Hovered(rr);
        if (sel) Fill(rr, theme.accentDim);
        else if (hov) Fill(rr, theme.panelAlt);
        float x = rr.x + 8 + row.depth * 14;
        if (row.kind == 0) {
            bool& o = open[row.headerKey];
            float ax = x + 4, ay = rr.y + 10;
            if (o) DrawTriangle({ax - 4, ay - 2}, {ax, ay + 3}, {ax + 4, ay - 2}, theme.textDim);
            else DrawTriangle({ax - 2, ay - 4}, {ax - 2, ay + 4}, {ax + 3, ay}, theme.textDim);
            Text(row.label, x + 12, rr.y + 3, theme.textDim, 13, "ui_bold");
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) o = !o;
            continue;
        }
        if (row.kind == 1) {
            bool hasBrushes = false;
            for (auto& b : map.brushes)
                if (b.entity == row.index) hasBrushes = true;
            if (hasBrushes) {
                std::string ek = "hent_" + std::to_string(row.index);
                Rectangle ar{x - 14, rr.y, 14, 20};
                float ax = x - 8, ay = rr.y + 10;
                if (open[ek]) DrawTriangle({ax - 4, ay - 2}, {ax, ay + 3}, {ax + 4, ay - 2}, theme.textDim);
                else DrawTriangle({ax - 2, ay - 4}, {ax - 2, ay + 4}, {ax + 3, ay}, theme.textDim);
                if (Clicked(ar)) {
                    open[ek] = !open[ek];
                    continue;
                }
            }
        }
        DrawRectangle((int)x, (int)rr.y + 6, 8, 8, row.color);
        Text(Ellipsize(row.label, rr.width - (x - rr.x) - 18), x + 14, rr.y + 3, sel ? WHITE : theme.text, 13);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool add = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SHIFT);
            if (!add) ClearSelection();
            if (row.kind == 1) SelectAllOfEntity(row.index);
            if (row.kind == 2) selBrushes.insert(row.index);
            if (row.kind == 3) ClearSelection();
            ClearFocus();
        }
        if (DoubleClicked(rr) && row.kind != 3) {
            BoundingBox bb;
            if (SelectionBounds(bb)) {
                Vector3 c = (bb.min + bb.max) * 0.5f;
                float size = std::max(64.0f, Len(bb.max - bb.min));
                views[1].camPos = c - AnglesForward(views[1].pitch, views[1].yaw) * (size * 1.6f);
                for (auto& v : views)
                    if (v.kind != ViewKind::Persp) v.center = {Comp(c, AxisU(v)), Comp(c, AxisV(v))};
            }
        }
        // drop a component onto an entity row
        if (row.kind == 1 && hov && DragReleased() && DragType() == "component") {
            Snapshot();
            map.entities[row.index].components.push_back({{"type", DragPayload()}, {"props", json::object()}});
            Changed(false);
            SetStatus("Added component " + DragPayload());
        }
    }
    EndScroll();
}

// ---------------------------------------------------------------------------------
// inspector
// ---------------------------------------------------------------------------------
static bool IsVec3(const json& v) { return v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number(); }

float Editor::Impl::PropEditor(const std::string& id, Rectangle r, float y, json& props, const json& defaults, bool allowRemove,
                               const json* enums, const json* help, bool* assetChanged) {
    // map edits go through undo and mark the map dirty; definition edits (assetChanged) don't
    auto commit = [&](const std::string& key) {
        if (assetChanged) {
            *assetChanged = true;
            return;
        }
        SnapshotCoalesced(this, id + key);
    };
    std::vector<std::string> keys;
    if (defaults.is_object())
        for (auto it = defaults.begin(); it != defaults.end(); ++it) keys.push_back(it.key());
    for (auto it = props.begin(); it != props.end(); ++it)
        if (std::find(keys.begin(), keys.end(), it.key()) == keys.end()) keys.push_back(it.key());
    float labelW = 104;
    float fx = r.x + 10 + labelW, fw = r.width - labelW - 20 - (allowRemove ? 24 : 0);
    for (auto& k : keys) {
        PushId(id + "_" + k);
        bool isSet = props.contains(k);
        const json& def = defaults.is_object() && defaults.contains(k) ? defaults[k] : json();
        json value = isSet ? props[k] : def;
        Text(Ellipsize(k, labelW - 6, 13), r.x + 12, y + 4, isSet ? theme.text : theme.textDim, 13);
        if (help && help->contains(k) && (*help)[k].is_string() && Hovered({r.x + 10, y, labelW, kRow - 2})) Tooltip((*help)[k].get<std::string>());
        Rectangle fr{fx, y, fw, kRow - 2};
        bool changed = false;
        json nv = value;
        const json* options = enums && enums->contains(k) && (*enums)[k].is_array() ? &(*enums)[k] : nullptr;
        if (k == "animator" || k == "behavior" || k == "quest" || k == "dialogue") {
            // asset files of the project: a dropdown (drop one from the Project panel too)
            bool anim = k == "animator";
            std::string cur = value.is_string() ? value.get<std::string>() : "";
            std::vector<std::string> opts = anim ? AnimatorOptions() : k == "behavior" ? BehaviorOptions() : k == "quest" ? QuestOptions() : DialogueOptions();
            if (!cur.empty() && std::find(opts.begin(), opts.end(), cur) == opts.end()) opts.push_back(cur);
            std::string shown = cur.empty() ? "(none)" : cur;
            bool pick = Dropdown("asset", fr, shown, opts);
            if (Hovered(fr) && DragReleased() && DragType() == k) shown = DragPayload(), pick = true;
            if (pick) {
                nv = shown == "(none)" ? "" : shown;
                changed = true;
                if (k == "behavior") {
                    // a tree needs ai = behavior (and without one the monster goes back to chasing)
                    std::string ai = props.is_object() && props.contains("ai") && props["ai"].is_string() ? props["ai"].get<std::string>() : "";
                    if (!nv.get<std::string>().empty()) props["ai"] = "behavior";
                    else if (ai == "behavior") props["ai"] = "chase";
                }
            }
        } else if (options) {
            // fixed choices (ai, attack_type...): a dropdown, keeping an unknown current value
            std::vector<std::string> opts;
            for (auto& o : *options)
                if (o.is_string()) opts.push_back(o.get<std::string>());
            std::string cur = value.is_string() ? value.get<std::string>() : value.dump();
            if (std::find(opts.begin(), opts.end(), cur) == opts.end()) opts.push_back(cur);
            if (Dropdown("enum", fr, cur, opts)) {
                nv = cur;
                changed = true;
            }
        } else if (value.is_number()) {
            float f = value.get<float>();
            if (FloatField("num", fr, f, fabsf(f) > 50 ? 1.0f : 0.05f, "%g")) {
                nv = value.is_number_integer() ? json((long long)roundf(f)) : json(f);
                changed = true;
            }
        } else if (value.is_boolean()) {
            bool b = value.get<bool>();
            if (Checkbox("chk", fr, b, "")) {
                nv = b;
                changed = true;
            }
        } else if (IsVec3(value)) {
            bool color = k.find("color") != std::string::npos;
            float cw = (fw - (color ? 26 : 0) - 8) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                float f = value[i].get<float>();
                if (FloatField("v" + std::to_string(i), {fx + i * (cw + 4), y, cw, kRow - 2}, f, color ? 1.0f : 1.0f, "%g")) {
                    nv[i] = f;
                    changed = true;
                }
            }
            if (color) {
                Vector3 c{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                if (c.x <= 1 && c.y <= 1 && c.z <= 1) c = c * 255.0f;
                ColorSwatch("sw", {fx + fw - 22, y + 1, 22, kRow - 4}, Color{(unsigned char)Clampf(c.x, 0, 255), (unsigned char)Clampf(c.y, 0, 255), (unsigned char)Clampf(c.z, 0, 255), 255});
            }
        } else {
            std::string s = value.is_string() ? value.get<std::string>() : (value.is_null() ? "" : value.dump());
            if (TextField("txt", fr, s)) {
                if (value.is_string() || value.is_null()) nv = s;
                else {
                    json parsed = json::parse(s, nullptr, false);
                    nv = parsed.is_discarded() ? json(s) : parsed;
                }
                changed = true;
            }
            // accept assets dropped from the project browser
            if (Hovered(fr) && DragReleased() && (DragType() == "model" || DragType() == "sprite" || DragType() == "sound" || DragType() == "texture")) {
                nv = DragPayload();
                changed = true;
            }
        }
        if (changed) {
            commit(k);
            props[k] = nv;
            if (!assetChanged) {
                Changed(false);
                meshDirty = true;
            }
        }
        if (allowRemove && isSet) {
            if (Button("rm", {r.x + r.width - 30, y, 20, kRow - 2}, "x")) {
                if (assetChanged) *assetChanged = true;
                else Snapshot();
                props.erase(k);
                if (!assetChanged) Changed(false);
            }
            if (Hovered({r.x + r.width - 30, y, 20, kRow - 2})) Tooltip(def.is_null() ? "Remove property" : "Reset to default");
        }
        PopId();
        y += kRow;
    }
    // add property
    if (allowRemove) {
        PushId(id + "_add");
        TextField("k", {r.x + 10, y, labelW - 4, kRow - 2}, newPropKey, "key");
        TextField("v", {fx, y, fw, kRow - 2}, newPropValue, "value");
        if (Button("add", {r.x + r.width - 30, y, 20, kRow - 2}, "+") && !newPropKey.empty()) {
            if (assetChanged) *assetChanged = true;
            else Snapshot();
            json parsed = json::parse(newPropValue, nullptr, false);
            props[newPropKey] = parsed.is_discarded() ? json(newPropValue) : parsed;
            newPropKey.clear();
            newPropValue.clear();
            if (!assetChanged) Changed(false);
        }
        PopId();
        y += kRow;
    }
    return y;
}

float Editor::Impl::InspectEntity(Rectangle r, float y) {
    int ei = *selEnts.begin();
    MapEntity& e = map.entities[ei];
    const ClassInfo* ci = ClassFor(e.classname);
    Text("Class", r.x + 12, y + 4, theme.textDim, 13);
    std::string cls = e.classname;
    if (Dropdown("class", {r.x + 114, y, r.width - 124, kRow}, cls, classNames)) {
        Snapshot();
        e.classname = cls;
        Changed(false);
        meshDirty = true;
    }
    y += kRow + 4;
    if (ci && !ci->desc.empty()) {
        // wrap the description
        std::string words = ci->desc, line;
        float maxW = r.width - 24;
        std::vector<std::string> lines;
        std::string word;
        for (size_t i = 0; i <= words.size(); ++i) {
            char c = i < words.size() ? words[i] : ' ';
            if (c == ' ') {
                std::string test = line.empty() ? word : line + " " + word;
                if (TextWidth(test, 12) > maxW && !line.empty()) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = test;
                }
                word.clear();
            } else {
                word += c;
            }
        }
        if (!line.empty()) lines.push_back(line);
        for (auto& l : lines) {
            Text(l, r.x + 12, y, theme.textDim, 12);
            y += 15;
        }
        y += 4;
    }
    if (Header("h_tr", {r.x, y, r.width, kRow}, "Transform", open["inspector_transform"])) {}
    y += kRow + 2;
    if (open["inspector_transform"]) {
        Text("Position", r.x + 12, y + 4, theme.textDim, 13);
        float cw = (r.width - 124 - 8) / 3.0f;
        Vector3 o = e.origin;
        bool ch = false;
        for (int i = 0; i < 3; ++i) {
            float f = Comp(o, i);
            const char* ids[3] = {"px", "py", "pz"};
            if (FloatField(ids[i], {r.x + 114 + i * (cw + 4), y, cw, kRow - 2}, f, 1.0f, "%g")) {
                SetComp(o, i, roundf(f));
                ch = true;
            }
        }
        if (ch) {
            SnapshotCoalesced(this, "pos");
            Vector3 d = o - e.origin;
            for (auto& b : map.brushes)
                if (b.entity == ei) b.Translate(d);
            e.origin = o;
            Changed(true);
        }
        y += kRow;
        Text("Angle", r.x + 12, y + 4, theme.textDim, 13);
        float a = e.angle;
        if (FloatField("angle", {r.x + 114, y, 80, kRow - 2}, a, 1.0f, "%g")) {
            SnapshotCoalesced(this, "angle");
            e.angle = fmodf(roundf(a) + 360.0f, 360.0f);
            Changed(false);
        }
        for (int k = 0; k < 4; ++k) {
            const int angles[4] = {0, 90, 180, 270};
            if (Button(TextFormat("a%d", k), {r.x + 200 + k * 34, y, 30, kRow - 2}, TextFormat("%d", angles[k]), (int)e.angle == angles[k])) {
                Snapshot();
                e.angle = (float)angles[k];
                Changed(false);
            }
        }
        y += kRow + 4;
    }
    if (Header("h_props", {r.x, y, r.width, kRow}, "Properties", open["inspector_props"])) {}
    y += kRow + 2;
    if (open["inspector_props"])
        y = PropEditor("props", r, y, e.props, ci ? ci->props : json::object(), true, ci ? &ci->enums : nullptr, ci ? &ci->help : nullptr) + 4;

    if (Header("h_comps", {r.x, y, r.width, kRow}, "Components", open["inspector_components"])) {}
    y += kRow + 2;
    if (open["inspector_components"]) {
        if (!e.components.is_array()) e.components = json::array();
        for (size_t i = 0; i < e.components.size(); ++i) {
            json& c = e.components[i];
            std::string type = c.value("type", std::string("?"));
            const ClassInfo* comp = ComponentFor(type);
            PushId("comp" + std::to_string(i));
            Fill({r.x + 6, y, r.width - 12, kRow}, theme.panelAlt);
            Text(type, r.x + 12, y + 4, comp ? theme.text : theme.danger, 13, "ui_bold");
            bool native = false;
            for (auto& nc : plugins::ComponentInfos())
                if (nc.name == type) native = true;
            Text(native ? "native" : "lua", r.x + r.width - 92, y + 5, theme.textDim, 11);
            bool removed = false;
            if (Button("rmc", {r.x + r.width - 32, y + 1, 22, kRow - 2}, "x")) {
                Snapshot();
                e.components.erase(e.components.begin() + i);
                Changed(false);
                removed = true;
            }
            y += kRow + 2;
            if (!removed) {
                if (comp && !comp->desc.empty()) {
                    Text(Ellipsize(comp->desc, r.width - 24, 12), r.x + 12, y, theme.textDim, 12);
                    y += 16;
                }
                if (!c.contains("props") || !c["props"].is_object()) c["props"] = json::object();
                y = PropEditor("cp" + std::to_string(i), r, y, c["props"], comp ? comp->props : json::object(), true, comp ? &comp->enums : nullptr,
                               comp ? &comp->help : nullptr) + 4;
            }
            PopId();
            if (removed) break;
        }
        std::string add = "Add Component...";
        std::vector<std::string> opts = componentNames;
        if (opts.empty()) opts.push_back("(no components registered)");
        if (Dropdown("addcomp", {r.x + 10, y, r.width - 20, kRow + 2}, add, opts) && ComponentFor(add)) {
            Snapshot();
            e.components.push_back({{"type", add}, {"props", json::object()}});
            Changed(false);
            SetStatus("Added component " + add);
        }
        y += kRow + 8;
    }
    // brush entity tools
    int brushCount = 0;
    for (auto& b : map.brushes)
        if (b.entity == ei) brushCount++;
    if (brushCount > 0) {
        Text(TextFormat("Brush entity with %d brush(es)", brushCount), r.x + 12, y + 4, theme.textDim, 13);
        y += kRow;
        if (Button("tow", {r.x + 10, y, (r.width - 26) / 2, kRow}, "Move to world")) MoveToWorld();
        y += kRow + 6;
    }
    float bw = (r.width - 26) / 2;
    if (Button("dup", {r.x + 10, y, bw, kRow}, "Duplicate")) DuplicateSelection({grid, 0, grid});
    if (Button("del", {r.x + 16 + bw, y, bw, kRow}, "Delete")) DeleteSelection();
    y += kRow + 6;
    if (Button("pref", {r.x + 10, y, r.width - 20, kRow}, "Save selection as prefab")) dialog = "prefab", dialogText = e.classname;
    return y + kRow + 8;
}

float Editor::Impl::InspectBrushes(Rectangle r, float y) {
    BoundingBox bb{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (int b : selBrushes) {
        if (b < 0 || b >= (int)map.brushes.size()) continue;
        bb.min = VMin(bb.min, map.brushes[b].bounds.min);
        bb.max = VMax(bb.max, map.brushes[b].bounds.max);
    }
    Text(TextFormat("%d brush(es) selected", (int)selBrushes.size()), r.x + 12, y + 4, theme.text, 14, "ui_bold");
    y += kRow + 4;
    float cw = (r.width - 124 - 8) / 3.0f;
    auto vecRow = [&](const char* label, const char* id, Vector3 v, int mode) {
        Text(label, r.x + 12, y + 4, theme.textDim, 13);
        for (int i = 0; i < 3; ++i) {
            float f = Comp(v, i);
            if (FloatField(TextFormat("%s%d", id, i), {r.x + 114 + i * (cw + 4), y, cw, kRow - 2}, f, 1.0f, "%g")) {
                SnapshotCoalesced(this, id);
                BoundingBox nb = bb;
                if (mode == 0) {  // move min corner
                    Vector3 d{0, 0, 0};
                    SetComp(d, i, roundf(f) - Comp(v, i));
                    MoveSelection(d);
                } else {  // size
                    SetComp(nb.max, i, Comp(bb.min, i) + std::max(1.0f, roundf(f)));
                    ResizeSelection(bb, nb);
                }
            }
        }
        y += kRow;
    };
    vecRow("Position", "bpos", bb.min, 0);
    vecRow("Size", "bsize", bb.max - bb.min, 1);
    y += 6;
    Text("Texture", r.x + 12, y + 4, theme.textDim, 13);
    std::string firstTex = map.brushes[*selBrushes.begin()].faces.empty() ? "" : map.brushes[*selBrushes.begin()].faces[0].tex.name;
    Texture2D t = assets::GetTexture(firstTex).tex;
    DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {r.x + 114, y, 44, 44}, {0, 0}, 0, WHITE);
    Text(Ellipsize(firstTex, r.width - 180, 13), r.x + 164, y + 2, theme.text, 13);
    if (Button("applytex", {r.x + 164, y + 20, r.width - 174, kRow}, "Apply '" + curTexture + "'")) {
        Snapshot();
        for (int b : selBrushes) ApplyTexture(curTexture, b, -1);
    }
    y += 52;
    float bw = (r.width - 26) / 2;
    auto pair = [&](const char* a, const char* b, bool* ca, bool* cb) {
        *ca = Button(a, {r.x + 10, y, bw, kRow}, a);
        *cb = Button(b, {r.x + 16 + bw, y, bw, kRow}, b);
        y += kRow + 4;
    };
    bool c1, c2;
    pair("Hollow", "Subtract (CSG)", &c1, &c2);
    if (c1) HollowSelection();
    if (c2) SubtractSelection();
    pair("Rotate Y 90", "Rotate Y -90", &c1, &c2);
    if (c1) RotateSelection(1, 90);
    if (c2) RotateSelection(1, -90);
    pair("Rotate X 90", "Rotate Z 90", &c1, &c2);
    if (c1) RotateSelection(0, 90);
    if (c2) RotateSelection(2, 90);
    pair("Rotate Y 15", "Rotate Y -15", &c1, &c2);
    if (c1) RotateSelection(1, 15);
    if (c2) RotateSelection(1, -15);
    pair("Flip X", "Flip Z", &c1, &c2);
    if (c1) FlipSelection(0);
    if (c2) FlipSelection(2);
    pair("To Entity...", "Move to world", &c1, &c2);
    if (c1) dialog = "tie", dialogText = "func_door";
    if (c2) MoveToWorld();
    pair("Duplicate", "Delete", &c1, &c2);
    if (c1) DuplicateSelection({grid, 0, grid});
    if (c2) DeleteSelection();
    if (Button("pref", {r.x + 10, y, r.width - 20, kRow}, "Save selection as prefab")) dialog = "prefab", dialogText = "myprefab";
    return y + kRow + 8;
}

float Editor::Impl::InspectFaces(Rectangle r, float y) {
    FaceRef first = *selFaces.begin();
    if (first.brush >= (int)map.brushes.size() || first.face >= (int)map.brushes[first.brush].faces.size()) {
        selFaces.clear();
        return y;
    }
    BrushFace& f0 = map.brushes[first.brush].faces[first.face];
    Text(TextFormat("%d face(s) selected", (int)selFaces.size()), r.x + 12, y + 4, theme.text, 14, "ui_bold");
    y += kRow + 4;
    Texture2D t = assets::GetTexture(f0.tex.name).tex;
    DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {r.x + 12, y, 64, 64}, {0, 0}, 0, WHITE);
    std::string tex = f0.tex.name;
    if (TextField("ftex", {r.x + 86, y, r.width - 96, kRow}, tex)) {
        Snapshot();
        for (auto& fr : selFaces) map.brushes[fr.brush].faces[fr.face].tex.name = tex;
        Changed();
    }
    if (Button("fapply", {r.x + 86, y + 26, r.width - 96, kRow}, "Apply '" + curTexture + "'")) {
        Snapshot();
        for (auto& fr : selFaces) map.brushes[fr.brush].faces[fr.face].tex.name = curTexture;
        Changed();
    }
    y += 72;
    auto field = [&](const char* label, const char* id, float FaceTex::*member, float speed) {
        Text(label, r.x + 12, y + 4, theme.textDim, 13);
        float v = f0.tex.*member;
        if (FloatField(id, {r.x + 114, y, r.width - 124, kRow - 2}, v, speed, "%g")) {
            SnapshotCoalesced(this, id);
            for (auto& fr : selFaces) map.brushes[fr.brush].faces[fr.face].tex.*member = v;
            Changed();
        }
        y += kRow;
    };
    field("Offset U", "ofu", &FaceTex::offU, 1.0f);
    field("Offset V", "ofv", &FaceTex::offV, 1.0f);
    field("Scale U", "scu", &FaceTex::scaleU, 0.01f);
    field("Scale V", "scv", &FaceTex::scaleV, 0.01f);
    field("Rotation", "rot", &FaceTex::rot, 1.0f);
    y += 4;
    float bw = (r.width - 26) / 2;
    if (Button("fit", {r.x + 10, y, bw, kRow}, "Fit to face")) {
        Snapshot();
        for (auto& fr : selFaces) {
            BrushFace& f = map.brushes[fr.brush].faces[fr.face];
            Texture2D tt = assets::GetTexture(f.tex.name).tex;
            FaceTex saved = f.tex;
            f.tex.offU = f.tex.offV = 0;
            f.tex.scaleU = f.tex.scaleV = 1;
            f.tex.rot = 0;
            float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
            for (auto& p : f.poly) {
                Vector2 uv = FaceUV(f, p, 1, 1);
                umin = fminf(umin, uv.x), umax = fmaxf(umax, uv.x);
                vmin = fminf(vmin, uv.y), vmax = fmaxf(vmax, uv.y);
            }
            f.tex = saved;
            f.tex.rot = 0;
            f.tex.scaleU = (umax - umin) / std::max(1, tt.width);
            f.tex.scaleV = (vmax - vmin) / std::max(1, tt.height);
            if (f.tex.scaleU == 0) f.tex.scaleU = 1;
            if (f.tex.scaleV == 0) f.tex.scaleV = 1;
            f.tex.offU = -umin / f.tex.scaleU;
            f.tex.offV = -vmin / f.tex.scaleV;
        }
        Changed();
    }
    if (Button("reset", {r.x + 16 + bw, y, bw, kRow}, "Reset")) {
        Snapshot();
        for (auto& fr : selFaces) {
            FaceTex& ft = map.brushes[fr.brush].faces[fr.face].tex;
            ft.offU = ft.offV = ft.rot = 0;
            ft.scaleU = ft.scaleV = 1;
        }
        Changed();
    }
    y += kRow + 4;
    if (Button("half", {r.x + 10, y, bw, kRow}, "Scale x0.5")) {
        Snapshot();
        for (auto& fr : selFaces) map.brushes[fr.brush].faces[fr.face].tex.scaleU *= 0.5f, map.brushes[fr.brush].faces[fr.face].tex.scaleV *= 0.5f;
        Changed();
    }
    if (Button("dbl", {r.x + 16 + bw, y, bw, kRow}, "Scale x2")) {
        Snapshot();
        for (auto& fr : selFaces) map.brushes[fr.brush].faces[fr.face].tex.scaleU *= 2, map.brushes[fr.brush].faces[fr.face].tex.scaleV *= 2;
        Changed();
    }
    return y + kRow + 8;
}

float Editor::Impl::InspectMap(Rectangle r, float y) {
    MapSettings& s = map.settings;
    Text("Map settings", r.x + 12, y + 4, theme.text, 14, "ui_bold");
    y += kRow + 4;
    auto label = [&](const char* l) { Text(l, r.x + 12, y + 4, theme.textDim, 13); };
    auto text = [&](const char* l, const char* id, std::string& v) {
        label(l);
        if (TextField(id, {r.x + 114, y, r.width - 124, kRow - 2}, v)) {
            Snapshot();
            Changed(false);
        }
        y += kRow;
    };
    auto num = [&](const char* l, const char* id, float& v, float speed) {
        label(l);
        float f = v;
        if (FloatField(id, {r.x + 114, y, r.width - 124, kRow - 2}, f, speed, "%g")) {
            SnapshotCoalesced(this, id);
            v = f;
            Changed(false);
            litPreview = false;
        }
        y += kRow;
    };
    auto vec = [&](const char* l, const char* id, Vector3& v, bool color) {
        label(l);
        float cw = (r.width - 124 - 8 - (color ? 26 : 0)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            float f = Comp(v, i);
            if (FloatField(TextFormat("%s%d", id, i), {r.x + 114 + i * (cw + 4), y, cw, kRow - 2}, f, 0.01f, "%.2f")) {
                SnapshotCoalesced(this, id);
                SetComp(v, i, f);
                Changed(false);
                litPreview = false;
            }
        }
        if (color) ColorSwatch(id, {r.x + r.width - 34, y + 1, 22, kRow - 4}, ColorFromVec(v));
        y += kRow;
    };
    text("Title", "title", s.title);
    text("Author", "author", s.author);
    text("Next map", "next", s.nextMap);
    text("Music", "music", s.music);
    num("Gravity", "grav", s.gravity, 1);
    y += 6;
    Text("Sky", r.x + 12, y + 4, theme.textDim, 13, "ui_bold");
    y += kRow;
    vec("Zenith", "skyz", s.skyZenith, true);
    vec("Horizon", "skyh", s.skyHorizon, true);
    vec("Clouds", "skyc", s.skyClouds, true);
    y += 6;
    Text("Lighting", r.x + 12, y + 4, theme.textDim, 13, "ui_bold");
    y += kRow;
    vec("Sun dir", "sund", s.sunDir, false);
    vec("Sun color", "sunc", s.sunColor, true);
    num("Sun power", "sunp", s.sunIntensity, 0.01f);
    vec("Ambient", "amb", s.ambient, true);
    num("Sky light", "skyl", s.skyLight, 0.01f);
    num("Bounce", "bounce", s.bounce, 0.01f);
    num("AO", "ao", s.ao, 0.01f);
    num("Luxel size", "lux", s.lightmapScale, 0.1f);
    y += 6;
    Text("Fog", r.x + 12, y + 4, theme.textDim, 13, "ui_bold");
    y += kRow;
    vec("Fog color", "fogc", s.fogColor, true);
    num("Fog density", "fogd", s.fogDensity, 0.00001f);
    return y + 8;
}

void Editor::Impl::DrawInspector(Rectangle r) {
    Block(r);
    Fill(r, theme.panel);
    DrawLine((int)r.x, (int)r.y, (int)r.x, (int)(r.y + r.height), theme.border);
    PanelTitle(r, "INSPECTOR");
    Rectangle area{r.x, r.y + 26, r.width, r.height - 26};
    float content = g_contentHeight["insp"];
    float scroll = BeginScroll("insp", area, content);
    float y0 = area.y + 6 - scroll;
    float y = y0;
    Rectangle cr{area.x, 0, area.width - 6, 0};
    // drop invalid selection indices (after undo etc.)
    for (auto it = selBrushes.begin(); it != selBrushes.end();) it = (*it >= (int)map.brushes.size()) ? selBrushes.erase(it) : std::next(it);
    for (auto it = selEnts.begin(); it != selEnts.end();) it = (*it >= (int)map.entities.size()) ? selEnts.erase(it) : std::next(it);
    if (HasSelection() || !selFaces.empty()) inspectKind.clear();  // picking in the scene leaves the asset
    if (activeDoc >= 0) y = InspectDocument(cr, y);
    else if (!inspectKind.empty()) y = InspectAsset(cr, y);
    else if (!selFaces.empty() && tool == Tool::Face) y = InspectFaces(cr, y);
    else if (selEnts.size() == 1) y = InspectEntity(cr, y);
    else if (!selBrushes.empty() && selEnts.empty()) y = InspectBrushes(cr, y);
    else if (!selEnts.empty()) {
        Text(TextFormat("%d entities, %d brushes selected", (int)selEnts.size(), (int)selBrushes.size()), cr.x + 12, y + 4, theme.text, 14, "ui_bold");
        y += kRow + 6;
        float bw = (cr.width - 26) / 2;
        if (Button("dup", {cr.x + 10, y, bw, kRow}, "Duplicate")) DuplicateSelection({grid, 0, grid});
        if (Button("del", {cr.x + 16 + bw, y, bw, kRow}, "Delete")) DeleteSelection();
        y += kRow + 6;
        if (Button("pref", {cr.x + 10, y, cr.width - 20, kRow}, "Save selection as prefab")) dialog = "prefab", dialogText = "myprefab";
        y += kRow + 6;
    } else {
        y = InspectMap(cr, y);
    }
    g_contentHeight["insp"] = y - y0 + 20;
    EndScroll();
}

// ---------------------------------------------------------------------------------
// project browser + console
// ---------------------------------------------------------------------------------
void Editor::Impl::DrawBottom(Rectangle r) {
    Block(r);
    Fill(r, theme.panel);
    DrawLine((int)r.x, (int)r.y, (int)(r.x + r.width), (int)r.y, theme.border);
    Fill({r.x, r.y, r.width, 26}, theme.header);
    if (Button("tab_proj", {r.x + 4, r.y + 3, 80, 20}, "Project", bottomTab == 0)) bottomTab = 0;
    if (Button("tab_cons", {r.x + 88, r.y + 3, 80, 20}, "Console", bottomTab == 1)) bottomTab = 1;
    Rectangle body{r.x, r.y + 26, r.width, r.height - 26};
    if (bottomTab == 0) DrawProject(body);
    else DrawConsole(body);
}

void Editor::Impl::DrawProject(Rectangle r) {
    const char* folders[] = {"Entities", "Weapons", "Animators", "Behaviors", "Quests", "Dialogues", "Components", "Textures", "Models", "Sprites", "Sounds", "Prefabs", "Maps", "Scripts"};
    const int folderCount = (int)(sizeof(folders) / sizeof(folders[0]));
    float fw = 120;
    Fill({r.x, r.y, fw, r.height}, theme.panelAlt);
    for (int i = 0; i < folderCount; ++i) {
        Rectangle fr{r.x + 4, r.y + 4 + i * 20.0f, fw - 8, 19};
        bool sel = projectFolder == folders[i];
        if (sel) Fill(fr, theme.accentDim);
        else if (Hovered(fr)) Fill(fr, theme.buttonHover);
        Text(folders[i], fr.x + 8, fr.y + 2, sel ? WHITE : theme.text, 13);
        if (Clicked(fr)) projectFolder = folders[i];
    }
    Rectangle top{r.x + fw + 6, r.y + 4, r.width - fw - 12, 22};
    TextField("proj_search", {top.x, top.y, 220, 22}, projectSearch, "Search...");
    float bx = top.x + 228;
    if (projectFolder == "Scripts" || projectFolder == "Entities" || projectFolder == "Components") {
        if (Button("reload", {bx, top.y, 120, 22}, "Reload scripts")) {
            RefreshClasses();
            SetStatus("Scripts reloaded: " + std::to_string(classes.size()) + " classes, " + std::to_string(components.size()) + " components");
        }
        bx += 126;
        if (Button("plug", {bx, top.y, 120, 22}, "Reload plugins")) {
            plugins::LoadAll();
            RefreshClasses();
            SetStatus("Native plugins reloaded");
        }
        bx += 126;
    }
    if (projectFolder == "Prefabs" && Button("saveprefab", {bx, top.y, 190, 22}, "Save selection as prefab", false, HasSelection()))
        dialog = "prefab", dialogText = "myprefab";
    if (projectFolder == "Animators") {
        if (Button("newanim", {bx, top.y, 140, 22}, "+ New animator")) {
            newAnimName = "new_animator";
            dialog = "newanimator";
            dialogText = newAnimName;
        }
        if (Hovered({bx, top.y, 140, 22})) Tooltip("Make an animation state machine for a model (idle, walk, attack...) and edit it as a graph");
        bx += 146;
    }
    if (projectFolder == "Quests" || projectFolder == "Dialogues") {
        bool quests = projectFolder == "Quests";
        if (Button("newflow", {bx, top.y, 140, 22}, quests ? "+ New quest" : "+ New dialogue")) {
            dialog = quests ? "newquest" : "newdialogue";
            dialogText = quests ? "new_quest" : "new_dialogue";
        }
        if (Hovered({bx, top.y, 140, 22}))
            Tooltip(quests ? "Make a quest (objectives, branches, rewards) and edit it as a graph" : "Make a conversation (lines, answers, actions) and edit it as a graph");
        bx += 146;
    }
    if (projectFolder == "Behaviors") {
        if (Button("newbt", {bx, top.y, 140, 22}, "+ New behavior")) {
            dialog = "newbehavior";
            dialogText = "new_behavior";
        }
        if (Hovered({bx, top.y, 140, 22})) Tooltip("Make a behavior tree for monsters (what they do: look, chase, attack, flee...) and edit it as a graph");
        bx += 146;
    }
    if (projectFolder == "Entities" || projectFolder == "Weapons") {
        bool weapons = projectFolder == "Weapons";
        if (Button("newdef", {bx, top.y, 130, 22}, weapons ? "+ New weapon" : "+ New type")) {
            newDefName.clear();
            newDefBase = weapons ? (weaponIds.empty() ? "" : weaponIds[0]) : (inspectKind == "class" ? inspectName : "monster_grunt");
            dialog = weapons ? "newweapon" : "newtype";
        }
        if (Hovered({bx, top.y, 130, 22}))
            Tooltip(weapons ? "Make a weapon without code: copy one and change its numbers" : "Make an enemy/item type without code: pick a base, change its properties");
        bx += 136;
    }
    std::string hint;
    if (projectFolder == "Textures") hint = "click = current texture, double click = apply to selection, drag onto a face";
    else if (projectFolder == "Entities") hint = "click = edit in Inspector + Entity tool, drag into a view to place";
    else if (projectFolder == "Weapons") hint = "click = edit in Inspector";
    else if (projectFolder == "Animators") hint = "double click = open the graph editor, drag onto an entity to use it";
    else if (projectFolder == "Quests") hint = "double click = open the graph editor, drag onto a quest_start entity";
    else if (projectFolder == "Dialogues") hint = "double click = open the graph editor, drag onto an npc";
    else if (projectFolder == "Behaviors") hint = "double click = open the graph editor, drag onto a monster to use it";
    else if (projectFolder == "Components") hint = "drag onto an entity (3D view or hierarchy), or use Add Component";
    else if (projectFolder == "Models" || projectFolder == "Sprites" || projectFolder == "Sounds") hint = "drag into a view to place";
    else if (projectFolder == "Scripts") hint = "click to open in your text editor";
    else if (projectFolder == "Maps") hint = "double click to open";
    Text(hint, bx + 6, top.y + 4, theme.textDim, 12);

    Rectangle area{r.x + fw + 4, r.y + 30, r.width - fw - 8, r.height - 34};
    bool tiles = projectFolder == "Textures" || projectFolder == "Sprites" || projectFolder == "Entities" || projectFolder == "Models" ||
                 projectFolder == "Weapons";
    float tile = projectFolder == "Entities" || projectFolder == "Weapons" ? 104 : 76;
    std::vector<std::string> items;
    if (projectFolder == "Entities") items = classNames;
    else if (projectFolder == "Weapons") items = weaponIds;
    else if (projectFolder == "Components") items = componentNames;
    else if (projectFolder == "Animators") items = animatorList;
    else if (projectFolder == "Behaviors") items = behaviorList;
    else if (projectFolder == "Quests") items = questList;
    else if (projectFolder == "Dialogues") items = dialogueList;
    else if (projectFolder == "Textures") items = textureList;
    else if (projectFolder == "Models") items = modelList;
    else if (projectFolder == "Sprites") items = spriteList;
    else if (projectFolder == "Sounds") items = soundList;
    else if (projectFolder == "Prefabs") items = prefabList;
    else if (projectFolder == "Maps") items = mapList;
    else if (projectFolder == "Scripts") items = scriptList;
    std::vector<std::string> filtered;
    for (auto& it : items)
        if (Matches(it, projectSearch)) filtered.push_back(it);
    int cols = tiles ? std::max(1, (int)((area.width - 8) / (tile + 6))) : std::max(1, (int)((area.width - 8) / 260));
    float cellW = tiles ? tile + 6 : (area.width - 8) / cols;
    float cellH = tiles ? tile + 20 : 22;
    int rows = ((int)filtered.size() + cols - 1) / cols;
    float scroll = BeginScroll("proj_" + projectFolder, area, rows * cellH + 8);
    for (size_t i = 0; i < filtered.size(); ++i) {
        const std::string& it = filtered[i];
        int cx = (int)i % cols, cy = (int)i / cols;
        Rectangle cell{area.x + 4 + cx * cellW, area.y + 4 + cy * cellH - scroll, cellW - 6, cellH - 2};
        if (cell.y + cell.height < area.y || cell.y > area.y + area.height) continue;
        bool hov = Hovered(cell);
        std::string name = std::filesystem::path(it).filename().string();
        bool selected = false;
        if (projectFolder == "Textures") selected = it == curTexture;
        if (projectFolder == "Entities") selected = it == curClass || (inspectKind == "class" && inspectName == it);
        if (projectFolder == "Weapons") selected = inspectKind == "weapon" && inspectName == it;
        if (selected) Fill(cell, theme.accentDim);
        else if (hov) Fill(cell, theme.buttonHover);
        if (tiles) {
            Rectangle img{cell.x + 3, cell.y + 3, cell.width - 6, cell.height - 22};
            if (projectFolder == "Textures") {
                Texture2D t = assets::GetTexture(it).tex;
                DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, img, {0, 0}, 0, WHITE);
            } else if (projectFolder == "Sprites") {
                Fill(img, Color{12, 12, 14, 255});
                Texture2D t = assets::GetTexturePath(it);
                float s = std::min(img.width / t.width, img.height / t.height);
                DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {img.x + (img.width - t.width * s) / 2, img.y + (img.height - t.height * s) / 2, t.width * s, t.height * s}, {0, 0}, 0, WHITE);
            } else if (projectFolder == "Entities") {
                const ClassInfo* ci = ClassFor(it);
                Color c = ci ? ci->color : GRAY;
                Fill(img, Color{(unsigned char)(c.r / 4), (unsigned char)(c.g / 4), (unsigned char)(c.b / 4), 255});
                DrawRectangle((int)(img.x + img.width / 2 - 10), (int)(img.y + img.height / 2 - 10), 20, 20, c);
                if (ci) Text(ci->category, img.x + 4, img.y + 2, theme.textDim, 11);
                if (ci && !ci->definition.empty()) Text("no-code", img.x + img.width - 46, img.y + 2, Color{120, 220, 140, 255}, 11);
            } else if (projectFolder == "Weapons") {
                int wi = script.WeaponIndex(it);
                Fill(img, Color{44, 36, 24, 255});
                if (wi >= 0) {
                    const WeaponDef& wd = script.weapons[wi];
                    TextCentered(std::to_string(wd.slot), {img.x, img.y + 4, img.width, img.height - 22}, Color{255, 200, 90, 255}, 26, "ui_bold");
                    Text(Ellipsize(wd.name, img.width - 8, 11), img.x + 4, img.y + img.height - 16, theme.textDim, 11);
                    if (!wd.definition.empty()) Text("no-code", img.x + img.width - 46, img.y + 2, Color{120, 220, 140, 255}, 11);
                }
            } else {
                Fill(img, Color{40, 34, 52, 255});
                TextCentered("3D", img, Color{180, 160, 255, 255}, 20, "ui_bold");
            }
            Text(Ellipsize(name, cell.width - 6, 11), cell.x + 3, cell.y + cell.height - 16, theme.text, 11);
        } else {
            Text(Ellipsize(it, cell.width - 10, 13), cell.x + 6, cell.y + 3, theme.text, 13);
        }
        if (hov) Tooltip(it);
        if (Clicked(cell)) {
            if (projectFolder == "Textures") {
                curTexture = it;
                if (tool != Tool::Face && tool != Tool::Texture && tool != Tool::Select) tool = Tool::Texture;
                StartDrag("texture", it, it);
            } else if (projectFolder == "Entities") {
                curClass = it;
                tool = Tool::Entity;
                ClearSelection();
                SelectAsset("class", it);
                StartDrag("class", it, it);
            } else if (projectFolder == "Weapons") {
                ClearSelection();
                SelectAsset("weapon", it);
            } else if (projectFolder == "Animators") {
                StartDrag("animator", it, name);
            } else if (projectFolder == "Behaviors") {
                StartDrag("behavior", it, name);
            } else if (projectFolder == "Quests") {
                StartDrag("quest", it, name);
            } else if (projectFolder == "Dialogues") {
                StartDrag("dialogue", it, name);
            } else if (projectFolder == "Components") {
                StartDrag("component", it, it);
            } else if (projectFolder == "Models") {
                StartDrag("model", it, name);
            } else if (projectFolder == "Sprites") {
                StartDrag("sprite", it, name);
            } else if (projectFolder == "Sounds") {
                Sound s = assets::GetSound(it);
                if (s.frameCount) PlaySound(s);
                StartDrag("sound", it, name);
            } else if (projectFolder == "Prefabs") {
                StartDrag("prefab", it, name);
            } else if (projectFolder == "Scripts") {
                std::string abs = vfs::Resolve(it);
                if (!abs.empty()) OpenURL(("file://" + std::filesystem::absolute(abs).generic_string()).c_str());
            }
        }
        if (DoubleClicked(cell)) {
            if (projectFolder == "Animators") {
                OpenDocument("animator", it);
            } else if (projectFolder == "Behaviors") {
                OpenDocument("behavior", it);
            } else if (projectFolder == "Quests") {
                OpenDocument("quest", it);
            } else if (projectFolder == "Dialogues") {
                OpenDocument("dialogue", it);
            } else if (projectFolder == "Maps") {
                if (dirty) {
                    dialog = "confirm_open";
                    dialogText = it;
                } else {
                    OpenMap(it);
                }
            } else if (projectFolder == "Textures") {
                Snapshot();
                if (!selFaces.empty())
                    for (auto& fr : selFaces) map.brushes[fr.brush].faces[fr.face].tex.name = it;
                else
                    for (int b : selBrushes) ApplyTexture(it, b, -1);
                Changed();
            } else if (projectFolder == "Components" && selEnts.size() == 1) {
                Snapshot();
                map.entities[*selEnts.begin()].components.push_back({{"type", it}, {"props", json::object()}});
                Changed(false);
            }
        }
    }
    EndScroll();
}

void Editor::Impl::DrawConsole(Rectangle r) {
    const auto& lines = LogLines();
    Rectangle area{r.x + 4, r.y + 4, r.width - 8, r.height - 34};
    Fill(area, theme.field);
    int lineH = 15;
    int visible = (int)(area.height / lineH);
    int start = std::max(0, (int)lines.size() - visible);
    PushClip(area);
    for (int i = start; i < (int)lines.size(); ++i) {
        Color c = lines[i].color;
        if (c.r == 245 && c.g == 245 && c.b == 245) c = theme.text;
        fonts::Text("mono", lines[i].text.c_str(), area.x + 6, area.y + 2 + (i - start) * lineH, 12, c);
    }
    PopClip();
    if (TextField("console_in", {r.x + 4, r.y + r.height - 28, r.width - 8, 24}, consoleInput, "console command (help, map, give, ...)")) {
        LOG_INFO("] %s", consoleInput.c_str());
        cvars::Execute(consoleInput);
        consoleInput.clear();
    }
}

// ---------------------------------------------------------------------------------
// dialogs / status / shortcuts
// ---------------------------------------------------------------------------------
void Editor::Impl::DrawDialogs(int w, int h) {
    if (dialog.empty()) return;
    if (dialog == "project" || dialog == "newproject" || dialog == "newtype" || dialog == "newweapon") {
        DrawProjectDialog(w, h);
        return;
    }
    Fill({0, 0, (float)w, (float)h}, Color{0, 0, 0, 110});
    float dw = 460, dh = dialog == "open" ? 420 : 150;
    Rectangle d{w * 0.5f - dw * 0.5f, h * 0.5f - dh * 0.5f, dw, dh};
    Fill(d, theme.panelAlt);
    Outline(d, theme.border);
    auto close = [&]() {
        dialog.clear();
        ClearFocus();
    };
    if (dialog == "open") {
        Text("Open map", d.x + 14, d.y + 10, theme.text, 16, "ui_bold");
        TextField("open_search", {d.x + 14, d.y + 38, dw - 28, 24}, dialogText, "filter...");
        Rectangle list{d.x + 14, d.y + 68, dw - 28, dh - 112};
        Fill(list, theme.field);
        std::vector<std::string> maps;
        for (auto& m : mapList)
            if (Matches(m, dialogText)) maps.push_back(m);
        float sc = BeginScroll("open_list", list, maps.size() * 22.0f);
        for (size_t i = 0; i < maps.size(); ++i) {
            Rectangle rr{list.x, list.y + i * 22.0f - sc, list.width, 22};
            if (Hovered(rr)) Fill(rr, theme.accentDim);
            Text(maps[i], rr.x + 8, rr.y + 4, theme.text, 13);
            if (Clicked(rr)) {
                std::string m = maps[i];
                close();
                OpenMap(m);
                EndScroll();
                return;
            }
        }
        EndScroll();
        if (Button("cancel", {d.x + dw - 104, d.y + dh - 36, 90, 26}, "Cancel") || IsKeyPressed(KEY_ESCAPE)) close();
        return;
    }
    if (dialog == "confirm_open") {
        Text("Discard unsaved changes?", d.x + 14, d.y + 14, theme.text, 16, "ui_bold");
        Text(dialogText, d.x + 14, d.y + 44, theme.textDim, 13);
        if (Button("yes", {d.x + dw - 200, d.y + dh - 40, 90, 28}, "Discard")) {
            std::string m = dialogText;
            close();
            OpenMap(m);
        }
        if (Button("no", {d.x + dw - 104, d.y + dh - 40, 90, 28}, "Cancel") || IsKeyPressed(KEY_ESCAPE)) close();
        return;
    }
    const char* title = dialog == "saveas"      ? "Save map as (maps/<name>.json)"
                        : dialog == "prefab"    ? "Save prefab (prefabs/<name>.json)"
                        : dialog == "newanimator" ? "New animator (animators/<name>.json)"
                        : dialog == "newbehavior" ? "New behavior (behaviors/<name>.json)"
                        : dialog == "newquest"    ? "New quest (quests/<name>.json)"
                        : dialog == "newdialogue" ? "New dialogue (dialogues/<name>.json)"
                                                : "Tie brushes to entity class";
    Text(title, d.x + 14, d.y + 12, theme.text, 15, "ui_bold");
    if (dialog == "tie") {
        std::vector<std::string> brushClasses;
        for (auto& c : classes)
            if (c.brush) brushClasses.push_back(c.name);
        Dropdown("tie_cls", {d.x + 14, d.y + 50, dw - 28, 26}, dialogText, brushClasses);
    } else {
        TextField("dlg_name", {d.x + 14, d.y + 50, dw - 28, 26}, dialogText);
    }
    bool ok = Button("ok", {d.x + dw - 200, d.y + dh - 40, 90, 28}, "OK");
    if (ok) {
        std::string t = dialogText;
        std::string kind = dialog;
        close();
        if (kind == "saveas" && !t.empty()) SaveMap(t);
        if (kind == "prefab" && !t.empty()) SavePrefab(t);
        if (kind == "tie" && !t.empty()) TieToEntity(t);
        if (kind == "newanimator" && !t.empty()) CreateAnimator(t);
        if (kind == "newbehavior" && !t.empty()) CreateBehavior(t);
        if (kind == "newquest" && !t.empty()) CreateFlow("quest", t);
        if (kind == "newdialogue" && !t.empty()) CreateFlow("dialogue", t);
        return;
    }
    if (Button("cancel", {d.x + dw - 104, d.y + dh - 40, 90, 28}, "Cancel") || IsKeyPressed(KEY_ESCAPE)) close();
}

void Editor::Impl::DrawStatusBar(Rectangle r) {
    Block(r);
    Fill(r, theme.header);
    const char* toolNames[] = {"Select", "Brush", "Entity", "Texture", "Face", "Clip"};
    std::string info = StrFormat("%s tool   |   texture: %s   |   entity: %s   |   %d brushes, %d entities", toolNames[(int)tool], curTexture.c_str(),
                                 curClass.c_str(), (int)map.brushes.size(), (int)map.entities.size());
    Text(info, r.x + 8, r.y + 5, theme.textDim, 12);
    if (GetTime() - statusTime < 6) {
        float tw = TextWidth(status, 12);
        Text(status, r.x + r.width - tw - 12, r.y + 5, theme.warn, 12);
    }
}

void Editor::Impl::HandleShortcuts() {
    if (drag == Drag::Look) return;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (ctrl) {
        if (IsKeyPressed(KEY_S)) {
            if (path.empty() || shift) dialog = "saveas", dialogText = "mymap";
            else SaveMap(path);
        }
        if (IsKeyPressed(KEY_Z)) shift ? Redo() : Undo();
        if (IsKeyPressed(KEY_Y)) Redo();
        if (IsKeyPressed(KEY_D)) DuplicateSelection({grid, 0, grid});
        if (IsKeyPressed(KEY_O)) dialog = "open";
        if (IsKeyPressed(KEY_N)) NewMap();
        if (IsKeyPressed(KEY_B)) StartBake();
        if (IsKeyPressed(KEY_C) && HasSelection()) {
            BoundingBox bb;
            SelectionBounds(bb);
            clipboard = CopySelectionJson(SnapV((bb.min + bb.max) * 0.5f));
            SetClipboardText(clipboard.c_str());
            SetStatus("Copied");
        }
        if (IsKeyPressed(KEY_V)) {
            const char* clip = GetClipboardText();
            std::string text = clip && StartsWith(clip, "{") ? clip : clipboard;
            if (!text.empty()) {
                Vector3 at = SnapV(views[1].camPos + AnglesForward(views[1].pitch, views[1].yaw) * 256.0f);
                if (hotView >= 0 && views[hotView].kind != ViewKind::Persp) {
                    BoundingBox bb;
                    Vector3 keep = SelectionBounds(bb) ? (bb.min + bb.max) * 0.5f : Vector3{0, 0, 0};
                    at = SnapV(ScreenToWorld2D(views[hotView], GetMousePosition(), keep));
                }
                PasteJson(text, at);
                SetStatus("Pasted");
            }
        }
        if (IsKeyPressed(KEY_A)) {
            ClearSelection();
            for (size_t i = 0; i < map.brushes.size(); ++i) selBrushes.insert((int)i);
            for (size_t i = 0; i < map.entities.size(); ++i) selEnts.insert((int)i);
        }
        return;
    }
    if (IsKeyPressed(KEY_V)) tool = Tool::Select;
    if (IsKeyPressed(KEY_B)) tool = Tool::Brush;
    if (IsKeyPressed(KEY_E) && drag != Drag::Look) tool = Tool::Entity;
    if (IsKeyPressed(KEY_T)) tool = Tool::Texture;
    if (IsKeyPressed(KEY_F)) tool = Tool::Face;
    if (IsKeyPressed(KEY_C)) {
        tool = Tool::Clip;
        clipPoints.clear();
    }
    if (IsKeyPressed(KEY_H)) HollowSelection();
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) DeleteSelection();
    if (IsKeyPressed(KEY_LEFT_BRACKET)) grid = std::max(1.0f, grid * 0.5f);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) grid = std::min(256.0f, grid * 2.0f);
    if (IsKeyPressed(KEY_F5)) Play(false);
    if (IsKeyPressed(KEY_F6)) Play(true);
    if (IsKeyPressed(KEY_R) && HasSelection()) RotateSelection(1, shift ? -90.0f : 90.0f);
    if (IsKeyPressed(KEY_TAB) && tool != Tool::Clip) maximized = maximized >= 0 ? -1 : (hotView >= 0 ? hotView : 1);
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (tool == Tool::Clip && !clipPoints.empty()) clipPoints.clear();
        else ClearSelection();
    }
    if (tool == Tool::Clip) {
        if (IsKeyPressed(KEY_TAB)) clipMode = (clipMode + 1) % 3;
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && clipPoints.size() == 2 && clipView >= 0) {
            EditorView& v = views[clipView];
            Vector3 depth{0, 0, 0};
            SetComp(depth, AxisDepth(v), 1);
            Plane p = PlaneFromPoints(clipPoints[0], clipPoints[1], clipPoints[1] + depth);
            ClipSelection(p, clipMode);
            clipPoints.clear();
        }
    }
    // nudge selection with arrow keys in the hovered 2D view
    if (hotView >= 0 && views[hotView].kind != ViewKind::Persp && HasSelection()) {
        EditorView& v = views[hotView];
        Vector3 d{0, 0, 0};
        float flip = v.kind == ViewKind::Top ? 1.0f : -1.0f;
        if (IsKeyPressed(KEY_LEFT)) SetComp(d, AxisU(v), -grid);
        if (IsKeyPressed(KEY_RIGHT)) SetComp(d, AxisU(v), grid);
        if (IsKeyPressed(KEY_UP)) SetComp(d, AxisV(v), -grid * flip);
        if (IsKeyPressed(KEY_DOWN)) SetComp(d, AxisV(v), grid * flip);
        if (Len(d) > 0) {
            Snapshot();
            MoveSelection(d);
        }
    }
}

}  // namespace rbs
