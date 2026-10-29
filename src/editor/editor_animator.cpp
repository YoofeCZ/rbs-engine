// RBS Engine - editor: graph documents in tabs. The animator editor: states are nodes,
// transitions are links, parameters live in a panel on the left, a live preview of the
// model plays the animator on the right, and the Inspector edits the selected state,
// transition or parameter. The file is the animator JSON itself (src/game/animator.h);
// node positions are stored in its "editor" key.
#include "core/log.h"
#include "core/vfs.h"
#include "editor/editor_doc.h"
#include "editor/editor_impl.h"
#include "render/animation.h"
#include "ui/imgui.h"

#include <algorithm>
#include <filesystem>

namespace rbs {

using namespace ui;
using namespace edoc;
namespace fs = std::filesystem;

static const int kAnyId = 100000;  // node id of the "Any State" node (states use their index)

// ---------------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------------
static std::string Stem(const std::string& path) { return fs::path(path).stem().string(); }

static void Touch(Document& d) {
    d.dirty = true;
    d.previewStale = true;
    d.confirmClose = false;
}

// ---------------------------------------------------------------------------------
// the animator model: states, transitions, parameters
// ---------------------------------------------------------------------------------
static int StateIndex(Document& d, const std::string& name) {
    json& st = Arr(d.def, "states");
    for (size_t i = 0; i < st.size(); ++i)
        if (JStr(st[i], "name") == name) return (int)i;
    return -1;
}

static std::vector<std::string> StateNames(Document& d) {
    std::vector<std::string> out;
    for (auto& s : Arr(d.def, "states")) out.push_back(JStr(s, "name"));
    return out;
}

static std::vector<std::string> ParamNames(Document& d, bool withAuto) {
    std::vector<std::string> out;
    for (auto& p : Arr(d.def, "parameters")) out.push_back(JStr(p, "name"));
    if (withAuto)
        for (auto& a : animator::kAutoParams) out.push_back(a);
    return out;
}

static std::string ParamType(Document& d, const std::string& name) {
    for (auto& p : Arr(d.def, "parameters"))
        if (JStr(p, "name") == name) return JStr(p, "type", "float");
    if (name == "on_ground" || name == "dead") return "bool";
    return "float";
}

static json& Layout(Document& d) {
    json& ed = d.def["editor"];
    if (!ed.is_object()) ed = json::object();
    if (!ed.contains("layout") || !ed["layout"].is_object()) ed["layout"] = json::object();
    json& lay = ed["layout"];
    if (!lay.contains("states") || !lay["states"].is_object()) lay["states"] = json::object();
    return lay;
}

static Vector2 DefaultStatePos(int i) { return {240.0f + (i % 3) * 210.0f, 40.0f + (i / 3) * 120.0f}; }

static Vector2 StatePos(Document& d, int i) {
    json& st = Layout(d)["states"];
    Vector2 p = DefaultStatePos(i);
    std::string name = JStr(Arr(d.def, "states")[i], "name");
    if (st.contains(name)) ReadPos(st[name], p);
    return p;
}

static void SetStatePos(Document& d, const std::string& name, Vector2 p) { Layout(d)["states"][name] = json::array({roundf(p.x), roundf(p.y)}); }

static Vector2 AnyPos(Document& d) {
    Vector2 p{240, -90};  // above the first state, so its links fan out downwards
    json& lay = Layout(d);
    if (lay.contains("any")) ReadPos(lay["any"], p);
    return p;
}

// a state without a saved position keeps the one it has now, so deleting another state
// does not shuffle the graph
static void EnsureLayout(Document& d) {
    int n = (int)Arr(d.def, "states").size();
    for (int i = 0; i < n; ++i) {
        std::string name = JStr(d.def["states"][i], "name");
        json& st = Layout(d)["states"];
        if (!st.contains(name)) st[name] = json::array({DefaultStatePos(i).x, DefaultStatePos(i).y});
    }
}

static int AddState(Document& d, Vector2 pos, const std::string& clip) {
    std::string name = UniqueName(StateNames(d), clip.empty() ? "New State" : clip);
    // one-shot animations (attack, pain, death...) do not loop
    std::string lower = ToLower(clip);
    bool once = false;
    for (const char* w : {"attack", "pain", "hit", "death", "die", "shoot", "fire", "jump", "land", "melee", "swing"})
        if (lower.find(w) != std::string::npos) once = true;
    json s = {{"name", name}, {"clip", clip.empty() ? name : clip}, {"loop", !once}};
    Arr(d.def, "states").push_back(s);
    if (d.def["states"].size() == 1) d.def["default"] = name;
    SetStatePos(d, name, pos);
    Touch(d);
    return (int)d.def["states"].size() - 1;
}

static void DeleteState(Document& d, int idx) {
    json& st = Arr(d.def, "states");
    if (idx < 0 || idx >= (int)st.size()) return;
    std::string name = JStr(st[idx], "name");
    st.erase(st.begin() + idx);
    json& tr = Arr(d.def, "transitions");
    for (size_t i = tr.size(); i-- > 0;)
        if (JStr(tr[i], "from") == name || JStr(tr[i], "to") == name) tr.erase(tr.begin() + i);
    if (JStr(d.def, "default") == name) d.def["default"] = st.empty() ? "" : JStr(st[0], "name");
    Layout(d)["states"].erase(name);
    d.view.selNode = d.view.selLink = -1;
    Touch(d);
}

static bool RenameState(Document& d, int idx, const std::string& to) {
    json& st = Arr(d.def, "states");
    if (idx < 0 || idx >= (int)st.size() || to.empty()) return false;
    std::string from = JStr(st[idx], "name");
    if (from == to) return false;
    if (StateIndex(d, to) >= 0) return false;
    st[idx]["name"] = to;
    for (auto& t : Arr(d.def, "transitions")) {
        if (JStr(t, "from") == from) t["from"] = to;
        if (JStr(t, "to") == from) t["to"] = to;
    }
    if (JStr(d.def, "default") == from) d.def["default"] = to;
    json& lay = Layout(d)["states"];
    if (lay.contains(from)) {
        lay[to] = lay[from];
        lay.erase(from);
    }
    Touch(d);
    return true;
}

static int AddTransition(Document& d, const std::string& from, const std::string& to) {
    if (from == to) return -1;
    json t = {{"from", from}, {"to", to}, {"duration", 0.15}, {"conditions", json::array()}};
    if (from != "*") t["exit_time"] = 1.0;  // like Unity: leave when the clip ends, until conditions are added
    Arr(d.def, "transitions").push_back(t);
    Touch(d);
    return (int)d.def["transitions"].size() - 1;
}

static void DeleteTransition(Document& d, int idx) {
    json& tr = Arr(d.def, "transitions");
    if (idx < 0 || idx >= (int)tr.size()) return;
    tr.erase(tr.begin() + idx);
    d.view.selLink = -1;
    Touch(d);
}

static std::string DefaultOp(const std::string& type) { return type == "trigger" ? "trigger" : (type == "bool" ? "true" : ">"); }

static int AddParam(Document& d, const std::string& base, const std::string& type) {
    std::string name = UniqueName(ParamNames(d, true), base);
    Arr(d.def, "parameters").push_back({{"name", name}, {"type", type}, {"value", 0}});
    Touch(d);
    return (int)d.def["parameters"].size() - 1;
}

// parameters are referenced by name in conditions, speed_param and blend_param
static void RenameParamRefs(Document& d, const std::string& from, const std::string& to) {
    for (auto& t : Arr(d.def, "transitions"))
        for (auto& c : Arr(t, "conditions"))
            if (JStr(c, "param") == from) c["param"] = to;
    for (auto& s : Arr(d.def, "states")) {
        if (JStr(s, "speed_param") == from) s["speed_param"] = to;
        if (JStr(s, "blend_param") == from) s["blend_param"] = to;
    }
}

static void DeleteParam(Document& d, int idx) {
    json& ps = Arr(d.def, "parameters");
    if (idx < 0 || idx >= (int)ps.size()) return;
    std::string name = JStr(ps[idx], "name");
    ps.erase(ps.begin() + idx);
    for (auto& t : Arr(d.def, "transitions")) {
        json& cs = Arr(t, "conditions");
        for (size_t i = cs.size(); i-- > 0;)
            if (JStr(cs[i], "param") == name) cs.erase(cs.begin() + i);
    }
    for (auto& s : Arr(d.def, "states")) {
        if (JStr(s, "speed_param") == name) s.erase("speed_param");
        if (JStr(s, "blend_param") == name) s.erase("blend_param");
    }
    d.selParam = -1;
    Touch(d);
}

// ---------------------------------------------------------------------------------
// the model used for clip names and the preview
// ---------------------------------------------------------------------------------
static ModelAsset* PreviewModel(Document& d) {
    if (d.previewModel.empty() || !vfs::Exists(d.previewModel)) return nullptr;
    ModelAsset& m = assets::GetModel(d.previewModel);
    return m.missing ? nullptr : &m;
}

static std::vector<std::string> ClipNames(Document& d) {
    std::vector<std::string> out;
    if (ModelAsset* m = PreviewModel(d))
        for (auto& kv : m->animByName) out.push_back(kv.first);
    return out;
}

// ---------------------------------------------------------------------------------
// opening, saving, closing
// ---------------------------------------------------------------------------------
void Editor::Impl::RefreshAnimatorList() { animatorList = vfs::List("animators", ".json", true); }

std::vector<std::string> Editor::Impl::AnimatorOptions() const {
    std::vector<std::string> out{kNone};
    for (auto& a : animatorList) out.push_back(a);
    return out;
}

static std::string GuessPreviewModel(Editor::Impl& ed, const std::string& path) {
    for (auto& c : ed.classes) {
        std::string a = c.props.is_object() && c.props.contains("animator") && c.props["animator"].is_string() ? c.props["animator"].get<std::string>() : "";
        if (a != path) continue;
        std::string m = c.props.contains("model") && c.props["model"].is_string() ? c.props["model"].get<std::string>() : "";
        if (m.empty()) m = c.model;
        if (!m.empty()) return m;
    }
    return "";
}

void Editor::Impl::OpenDocument(const std::string& kind, const std::string& name) {
    if (kind != "animator" && kind != "behavior" && kind != "quest" && kind != "dialogue") return;
    std::string dir = kind == "animator" ? "animators/" : kind == "behavior" ? "behaviors/" : kind == "quest" ? "quests/" : "dialogues/";
    std::string path = name.find('/') != std::string::npos ? name : dir + name;
    if (fs::path(path).extension() != ".json") path += ".json";
    for (size_t i = 0; i < docs.size(); ++i) {
        if (docs[i]->path == path) {
            activeDoc = (int)i;
            return;
        }
    }
    std::string text;
    if (!vfs::ReadText(path, text)) {
        SetStatus("Cannot read " + path);
        return;
    }
    json j = json::parse(text, nullptr, false, true);
    if (!j.is_object()) {
        SetStatus(path + " is not a valid animator file");
        return;
    }
    auto d = std::make_unique<Document>();
    d->kind = kind;
    d->path = path;
    d->name = Stem(path);
    d->def = j;
    if (kind == "behavior") {
        InitBehaviorDocument(*d);
    } else if (kind == "quest" || kind == "dialogue") {
        InitFlowDocument(*d);
    } else {
        Arr(d->def, "states");
        Arr(d->def, "transitions");
        Arr(d->def, "parameters");
        EnsureLayout(*d);
        d->previewModel = d->def.contains("editor") && d->def["editor"].is_object() ? JStr(d->def["editor"], "preview_model") : "";
        if (d->previewModel.empty()) d->previewModel = GuessPreviewModel(*this, path);
    }
    docs.push_back(std::move(d));
    activeDoc = (int)docs.size() - 1;
}

bool Editor::Impl::CreateAnimator(const std::string& name) {
    bool valid = !name.empty();
    for (char c : name)
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_')) valid = false;
    if (!valid) {
        SetStatus("Use lowercase letters, digits and _ for the name");
        return false;
    }
    std::string path = "animators/" + name + ".json";
    if (vfs::Exists(path)) {
        SetStatus("'" + path + "' already exists");
        return false;
    }
    // a starting point that already works for a walking character: idle <-> walk by speed
    json def = {
        {"default", "idle"},
        {"parameters", json::array()},
        {"states", json::array({{{"name", "idle"}, {"clip", "idle"}, {"loop", true}},
                                {{"name", "walk"}, {"clip", "walk"}, {"loop", true}, {"speed_param", "speed"}, {"speed_ref", 150}}})},
        {"transitions", json::array({{{"from", "idle"}, {"to", "walk"}, {"duration", 0.15}, {"conditions", json::array({{{"param", "speed"}, {"op", ">"}, {"value", 12}}})}},
                                     {{"from", "walk"}, {"to", "idle"}, {"duration", 0.2}, {"conditions", json::array({{{"param", "speed"}, {"op", "<"}, {"value", 12}}})}}})},
        {"editor", {{"layout", {{"any", json::array({240, -90})}, {"states", {{"idle", json::array({240, 40})}, {"walk", json::array({450, 40})}}}}}}},
    };
    if (!vfs::WriteText(path, def.dump(2) + "\n")) {
        SetStatus("Cannot write " + path);
        return false;
    }
    RefreshAnimatorList();
    OpenDocument("animator", path);
    std::string where = vfs::CurrentMod().empty() ? "base/" : "mods/" + vfs::CurrentMod() + "/";
    SetStatus("Created " + where + path);
    return true;
}

bool Editor::Impl::SaveDocument(Document& d) {
    if (!vfs::WriteText(d.path, d.def.dump(2) + "\n")) {
        SetStatus("Cannot write " + d.path);
        return false;
    }
    d.dirty = false;
    if (d.kind == "animator") animator::ClearCache();  // placed entities pick the new version up the next time a map starts
    RefreshAnimatorList();
    RefreshBehaviorList();
    RefreshFlowLists();
    SetStatus("Saved " + d.path);
    return true;
}

void Editor::Impl::ReloadDocument(Document& d) {
    std::string text;
    if (!vfs::ReadText(d.path, text)) return;
    json j = json::parse(text, nullptr, false, true);
    if (!j.is_object()) return;
    d.def = j;
    if (d.kind == "behavior") {
        InitBehaviorDocument(d);
    } else if (d.kind == "quest" || d.kind == "dialogue") {
        InitFlowDocument(d);
    } else {
        Arr(d.def, "states");
        Arr(d.def, "transitions");
        Arr(d.def, "parameters");
        EnsureLayout(d);
    }
    d.view.selNode = d.view.selLink = -1;
    d.selParam = -1;
    d.dirty = false;
    d.previewStale = true;
}

void Editor::Impl::CloseDocument(int i, bool force) {
    if (i < 0 || i >= (int)docs.size()) return;
    Document& d = *docs[i];
    if (d.dirty && !force && !d.confirmClose) {
        d.confirmClose = true;
        SetStatus(d.path + " has unsaved changes: press x again to discard them, or Save (Ctrl+S)");
        return;
    }
    if (d.rt.id) UnloadRenderTexture(d.rt);
    docs.erase(docs.begin() + i);
    if (activeDoc == i) activeDoc = docs.empty() ? -1 : std::min(i, (int)docs.size() - 1);
    else if (activeDoc > i) --activeDoc;
}

void Editor::Impl::ReleaseDocuments() {
    for (auto& d : docs)
        if (d->rt.id) UnloadRenderTexture(d->rt);
    docs.clear();
    activeDoc = -1;
}

// ---------------------------------------------------------------------------------
// tabs
// ---------------------------------------------------------------------------------
void Editor::Impl::DrawTabs(Rectangle r) {
    Block(r);
    Fill(r, theme.header);
    DrawLine((int)r.x, (int)(r.y + r.height - 1), (int)(r.x + r.width), (int)(r.y + r.height - 1), theme.border);
    float x = r.x + 4;
    auto tab = [&](const std::string& id, const std::string& label, bool sel, bool closable, float& outX, bool& closed) {
        float w = std::min(210.0f, TextWidth(label, 13) + 22 + (closable ? 20 : 0));
        Rectangle tr{x, r.y + 3, w, r.height - 3};
        Fill(tr, sel ? theme.panel : theme.panelAlt);
        if (sel) Fill({tr.x, tr.y, tr.width, 2}, theme.accent);
        Outline(tr, theme.border);
        Text(Ellipsize(label, w - 16 - (closable ? 18 : 0), 13), tr.x + 10, tr.y + 5, sel ? WHITE : theme.textDim, 13);
        bool pick = false;
        closed = false;
        if (closable) {
            Rectangle xr{tr.x + tr.width - 19, tr.y + 4, 15, 15};
            if (Hovered(xr)) Fill(xr, theme.buttonHover);
            Text("x", xr.x + 4, xr.y, theme.textDim, 13);
            if (Clicked(xr)) closed = true;
            else pick = Clicked(tr);
        } else {
            pick = Clicked(tr);
        }
        x += w + 2;
        outX = x;
        (void)id;
        return pick;
    };
    float nx;
    bool closed;
    if (tab("lvl", "Level", activeDoc < 0, false, nx, closed)) activeDoc = -1;
    int closeIdx = -1;
    for (size_t i = 0; i < docs.size(); ++i) {
        PushId("doc" + std::to_string(i));
        const std::string& k = docs[i]->kind;
        std::string label = std::string(k == "behavior" ? "Behavior: " : k == "quest" ? "Quest: " : k == "dialogue" ? "Dialogue: " : "Animator: ") + docs[i]->name + (docs[i]->dirty ? " *" : "");
        if (tab("t", label, activeDoc == (int)i, true, nx, closed)) activeDoc = (int)i;
        if (closed) closeIdx = (int)i;
        PopId();
    }
    if (closeIdx >= 0) CloseDocument(closeIdx, false);
}

// ---------------------------------------------------------------------------------
// preview: the model plays the animator
// ---------------------------------------------------------------------------------
static void RebuildPreview(Document& d) {
    std::string err;
    auto asset = std::make_shared<AnimatorAsset>(animator::Parse(d.def, &err));
    if (asset->states.empty()) {
        d.preview.reset();
        d.previewStale = false;
        return;
    }
    auto inst = std::make_shared<AnimatorInstance>();
    inst->path = d.path;
    inst->asset = asset;
    for (auto& p : asset->params) inst->params[p.name] = p.value;
    if (d.preview) {
        // keep what the user set while editing
        for (auto& kv : d.preview->params) inst->params[kv.first] = kv.second;
        const AnimatorAsset* old = d.preview->asset.get();
        if (old && d.preview->state >= 0 && d.preview->state < (int)old->states.size()) {
            int s = asset->FindState(old->states[d.preview->state].name);
            if (s >= 0) {
                inst->state = s;
                inst->norm = d.preview->norm;
                inst->lastNorm = d.preview->lastNorm;
            }
        }
    }
    d.preview = inst;
    d.previewStale = false;
}

// Advances the animator and draws the model into the preview texture. Called before any
// clipping is set up: it renders to another target.
static void RenderPreview(Document& d, bool show) {
    ModelAsset* m = PreviewModel(d);
    if (d.previewStale || !d.preview) RebuildPreview(d);
    if (!m || !d.preview || !show) return;
    if (!d.rt.id) d.rt = LoadRenderTexture(256, 220);
    float dt = Clampf(GetFrameTime(), 0.0f, 0.05f);
    std::vector<const AnimEventDef*> fired;
    animator::Update(*d.preview, *m, dt, fired);
    for (const AnimEventDef* ev : fired) {
        std::string what = !ev->name.empty() ? ev->name : ev->sound;
        d.lastEvents = what.empty() ? "event" : what;
        d.lastEventTime = GetTime();
        if (!ev->sound.empty() && vfs::Exists(ev->sound)) {
            Sound s = assets::GetSound(ev->sound);
            if (s.frameCount) PlaySound(s);
        }
    }
    static anim::Pose pose;
    if (m->animCount > 0 && animator::BuildPose(*d.preview, *m, pose)) anim::Apply(*m, pose);
    BoundingBox b = m->bounds;
    Vector3 c = (b.min + b.max) * 0.5f;
    float size = std::max({b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z});
    if (size < 0.1f) c = {0, 0.8f, 0}, size = 1.8f;
    float yaw = d.previewYaw * DEG2RAD;
    Camera3D cam{};
    cam.target = c;
    cam.position = c + Vector3{sinf(yaw) * size * 1.7f, size * 0.25f, cosf(yaw) * size * 1.7f};
    cam.up = {0, 1, 0};
    cam.fovy = 45;
    cam.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(d.rt);
    ClearBackground(Color{26, 30, 40, 255});
    BeginMode3D(cam);
    DrawGrid(10, size * 0.2f);
    gRenderer.DrawModelLit(*m, MatrixIdentity(), {0.75f, 0.75f, 0.75f}, {0.5f, 0.5f, 0.45f}, Norm({0.3f, 0.8f, 0.4f}));
    EndMode3D();
    EndTextureMode();
}

// ---------------------------------------------------------------------------------
// the animator document
// ---------------------------------------------------------------------------------
static void BuildGraph(Document& d, std::vector<graph::Node>& nodes, std::vector<graph::Link>& links) {
    json& st = Arr(d.def, "states");
    std::string def = JStr(d.def, "default");
    const AnimatorInstance* run = d.preview.get();
    int active = run && run->asset ? run->state : -1;
    for (size_t i = 0; i < st.size(); ++i) {
        graph::Node n;
        n.id = (int)i;
        n.title = JStr(st[i], "name");
        n.subtitle = !JStr(st[i], "blend_param").empty() ? "blend: " + JStr(st[i], "blend_param") : JStr(st[i], "clip", n.title);
        if (!JBool(st[i], "loop", true)) n.badge = "once";
        if (n.title == def) {
            n.color = {196, 112, 36, 255};
            n.badge = n.badge.empty() ? "default" : "default, once";
        } else {
            n.color = {64, 74, 96, 255};
        }
        n.pos = StatePos(d, (int)i);
        n.size = {160, 44};
        n.active = (int)i == active;
        nodes.push_back(n);
    }
    graph::Node any;
    any.id = kAnyId;
    any.title = "Any State";
    any.color = {30, 108, 124, 255};
    any.in = false;
    any.pos = AnyPos(d);
    any.size = {140, 34};
    nodes.push_back(any);

    json& tr = Arr(d.def, "transitions");
    for (size_t i = 0; i < tr.size(); ++i) {
        std::string from = JStr(tr[i], "from"), to = JStr(tr[i], "to");
        int a = (from == "*" || from == "any") ? kAnyId : StateIndex(d, from);
        int b = StateIndex(d, to);
        if (a == -1 || b < 0) continue;  // points at a state that no longer exists
        graph::Link l;
        l.id = (int)i;
        l.from = a;
        l.to = b;
        if (a == kAnyId) l.color = {70, 170, 190, 255};
        if (run && run->prev >= 0 && run->fade < 1 && run->state == b && (a == kAnyId || a == run->prev)) l.marker = run->fade;
        links.push_back(l);
    }
}

static const char* kParamTypes[] = {"float", "int", "bool", "trigger"};

void Editor::Impl::DrawDocument(Rectangle r) {
    Document* dp = Doc();
    if (!dp) return;
    if (dp->kind == "behavior") {
        DrawBehaviorDoc(r);
        return;
    }
    if (dp->kind == "quest" || dp->kind == "dialogue") {
        DrawFlowDoc(r);
        return;
    }
    Document& d = *dp;
    Block(r);
    bool showPreview = open.count("anim_preview") ? open["anim_preview"] : true;
    bool showParams = open.count("anim_params") ? open["anim_params"] : true;
    RenderPreview(d, showPreview);

    // ---- toolbar ----
    Rectangle bar{r.x, r.y, r.width, 30};
    Fill(bar, theme.panel);
    float bx = bar.x + 6;
    auto btn = [&](const char* id, const char* label, float w, bool sel = false, bool enabled = true) {
        bool hit = Button(id, {bx, bar.y + 4, w, 22}, label, sel, enabled);
        bx += w + 4;
        return hit;
    };
    if (btn("save", d.dirty ? "Save *" : "Save", 70, d.dirty, d.dirty)) SaveDocument(d);
    if (btn("revert", "Revert", 64, false, d.dirty)) ReloadDocument(d);
    if (btn("fit", "Fit", 44)) d.fitted = false;
    if (btn("addstate", "+ State", 70)) {
        std::vector<graph::Node> tmp;
        std::vector<graph::Link> tl;
        Vector2 p = graph::ToGraph(d.view, {r.x, r.y + 30, r.width, r.height - 30}, {r.x + r.width * 0.5f - 80, r.y + r.height * 0.5f - 22});
        d.view.selNode = AddState(d, p, "");
        d.selParam = -1;
    }
    if (btn("params", "Parameters", 88, showParams)) open["anim_params"] = !showParams;
    if (btn("preview", "Preview", 72, showPreview)) open["anim_preview"] = !showPreview;
    Text(std::string("right click: menu   -   drag the dot on a state to another state: transition   -   Del: delete   -   wheel: zoom"), bx + 8, bar.y + 8, theme.textDim, 12);

    Rectangle canvas{r.x, r.y + 30, r.width, r.height - 30};

    // ---- overlays (their rectangles are excluded from the canvas input) ----
    float panelH = canvas.height - 16;
    Rectangle paramsR{canvas.x + 8, canvas.y + 8, 216, std::min(panelH, 60 + 24.0f * (Arr(d.def, "parameters").size() + animator::kAutoParams.size()))};
    Rectangle prevR{canvas.x + canvas.width - 8 - 272, canvas.y + 8, 272, 0};
    prevR.height = std::min(panelH, 28 + 232 + 54.0f);
    d.view.exclude.clear();
    if (showParams) d.view.exclude.push_back(paramsR);
    if (showPreview) d.view.exclude.push_back(prevR);

    std::vector<graph::Node> nodes;
    std::vector<graph::Link> links;
    EnsureLayout(d);
    BuildGraph(d, nodes, links);
    if (!d.fitted) {
        // fit into the part of the canvas the overlay panels leave free
        float left = showParams ? paramsR.width + 16 : 0, right = showPreview ? prevR.width + 16 : 0;
        Rectangle free{canvas.x + left, canvas.y, std::max(100.0f, canvas.width - left - right), canvas.height};
        graph::Fit(d.view, free, nodes);
        d.view.pan.x += left;
        d.fitted = true;
    }
    graph::Events ev = graph::Draw("anim_graph", canvas, d.view, nodes, links);

    // ---- apply graph events ----
    if (ev.moved) {
        for (auto& n : nodes) {
            if (n.id == kAnyId) {
                Layout(d)["any"] = json::array({roundf(n.pos.x), roundf(n.pos.y)});
            } else if (n.id < (int)d.def["states"].size()) {
                SetStatePos(d, JStr(d.def["states"][n.id], "name"), n.pos);
            }
        }
        d.dirty = true;
    }
    if (ev.selectionChanged) {
        d.selParam = -1;
        if (d.makeFrom >= 0 && d.view.selNode >= 0 && d.view.selNode != d.makeFrom && d.view.selNode != kAnyId) {
            std::string from = d.makeFrom == kAnyId ? "*" : JStr(d.def["states"][d.makeFrom], "name");
            int t = AddTransition(d, from, JStr(d.def["states"][d.view.selNode], "name"));
            d.makeFrom = -1;
            d.view.selNode = -1;
            d.view.selLink = t;
        }
    }
    if (ev.linkFrom >= 0 && ev.linkTo >= 0 && ev.linkTo != kAnyId) {
        std::string from = ev.linkFrom == kAnyId ? "*" : JStr(d.def["states"][ev.linkFrom], "name");
        int t = AddTransition(d, from, JStr(d.def["states"][ev.linkTo], "name"));
        if (t >= 0) {
            d.view.selNode = -1;
            d.view.selLink = t;
            d.selParam = -1;
        }
    }
    if (ev.deleteKey) {
        if (d.view.selLink >= 0) DeleteTransition(d, d.view.selLink);
        else if (d.view.selNode >= 0 && d.view.selNode != kAnyId) DeleteState(d, d.view.selNode);
    }
    if (d.makeFrom >= 0 && IsKeyPressed(KEY_ESCAPE)) d.makeFrom = -1;
    if (ev.context) {
        std::vector<std::string> items;
        if (ev.contextNode >= 0) {
            items.push_back("Make transition");
            if (ev.contextNode != kAnyId) {
                items.push_back("Set as default state");
                items.push_back("Delete state");
            }
        } else if (ev.contextLink >= 0) {
            items.push_back("Delete transition");
        } else {
            items.push_back("New state");
            for (auto& c : ClipNames(d)) items.push_back("State from clip: " + c);
        }
        graph::OpenMenu(d.view, items, ev);
    }
    if (ev.menuChoice >= 0) {
        // the menu items are rebuilt the same way: map the choice back through the text
        std::string choice = d.view.menuItems.size() > (size_t)ev.menuChoice ? d.view.menuItems[ev.menuChoice] : "";
        if (choice == "Make transition") d.makeFrom = ev.menuNode;
        else if (choice == "Set as default state") {
            d.def["default"] = JStr(d.def["states"][ev.menuNode], "name");
            Touch(d);
        } else if (choice == "Delete state") DeleteState(d, ev.menuNode);
        else if (choice == "Delete transition") DeleteTransition(d, ev.menuLink);
        else if (choice == "New state") {
            d.view.selNode = AddState(d, ev.menuGraphPos, "");
            d.selParam = -1;
        } else if (choice.rfind("State from clip: ", 0) == 0) {
            d.view.selNode = AddState(d, ev.menuGraphPos, choice.substr(17));
            d.selParam = -1;
        }
    }
    if (d.makeFrom >= 0) {
        Rectangle h{canvas.x + canvas.width * 0.5f - 190, canvas.y + canvas.height - 34, 380, 24};
        Fill(h, Color{20, 60, 90, 235});
        Outline(h, theme.accent);
        TextCentered("Click the state the transition should lead to (Esc cancels)", h, WHITE, 13);
    }

    // ---- parameters panel ----
    if (showParams) {
        Fill(paramsR, Color{27, 31, 40, 240});
        Outline(paramsR, theme.border);
        Text("PARAMETERS", paramsR.x + 10, paramsR.y + 8, theme.textDim, 12, "ui_bold");
        if (Button("addparam", {paramsR.x + paramsR.width - 70, paramsR.y + 5, 62, 20}, "+ Add")) {
            d.selParam = AddParam(d, "param", "float");
            d.view.selNode = d.view.selLink = -1;
        }
        float y = paramsR.y + 30;
        json& ps = Arr(d.def, "parameters");
        auto control = [&](const std::string& name, const std::string& type, Rectangle cr) {
            if (!d.preview) return;
            float v = d.preview->params.count(name) ? d.preview->params[name] : 0;
            if (type == "trigger") {
                if (Button("fire", cr, "Fire")) d.preview->triggers.insert(name);
            } else if (type == "bool") {
                bool b = v != 0;
                if (Checkbox("chk", cr, b, "")) d.preview->params[name] = b ? 1.0f : 0.0f;
            } else {
                if (FloatField("val", cr, v, 1.0f, "%g")) d.preview->params[name] = type == "int" ? roundf(v) : v;
            }
        };
        for (size_t i = 0; i < ps.size(); ++i) {
            PushId("p" + std::to_string(i));
            std::string name = JStr(ps[i], "name"), type = JStr(ps[i], "type", "float");
            Rectangle row{paramsR.x + 4, y, paramsR.width - 8, kRow};
            bool sel = d.selParam == (int)i;
            if (sel) Fill(row, theme.accentDim);
            else if (Hovered({row.x, row.y, 100, row.height})) Fill(row, theme.buttonHover);
            Text(Ellipsize(name, 96, 13), row.x + 6, row.y + 4, theme.text, 13);
            if (Clicked({row.x, row.y, 100, row.height})) {
                d.selParam = (int)i;
                d.view.selNode = d.view.selLink = -1;
            }
            control(name, type, {row.x + 106, row.y + 1, row.width - 110, row.height - 2});
            PopId();
            y += kRow + 2;
        }
        Text("built in", paramsR.x + 10, y + 3, theme.textDim, 11);
        y += 18;
        for (auto& a : animator::kAutoParams) {
            PushId("a_" + a);
            Rectangle row{paramsR.x + 4, y, paramsR.width - 8, kRow};
            Text(a, row.x + 6, row.y + 4, theme.textDim, 13);
            control(a, ParamType(d, a), {row.x + 106, row.y + 1, row.width - 110, row.height - 2});
            PopId();
            y += kRow + 2;
        }
    }

    // ---- preview panel ----
    if (showPreview) {
        Fill(prevR, Color{27, 31, 40, 240});
        Outline(prevR, theme.border);
        Text("PREVIEW", prevR.x + 10, prevR.y + 8, theme.textDim, 12, "ui_bold");
        Rectangle img{prevR.x + 8, prevR.y + 28, prevR.width - 16, 224};
        ModelAsset* m = PreviewModel(d);
        if (m && d.rt.id) {
            DrawTexturePro(d.rt.texture, {0, 0, (float)d.rt.texture.width, -(float)d.rt.texture.height}, img, {0, 0}, 0, WHITE);
            if (Hovered(img) && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) d.previewYaw -= GetMouseDelta().x * 0.6f;
        } else {
            Fill(img, Color{20, 23, 30, 255});
            TextCentered(d.previewModel.empty() ? "Pick a model in the Inspector" : "Model not found", img, theme.textDim, 13);
        }
        Outline(img, theme.border);
        float y = img.y + img.height + 6;
        std::string stateText = "-";
        if (d.preview && d.preview->asset && d.preview->state >= 0 && d.preview->state < (int)d.preview->asset->states.size()) {
            stateText = d.preview->asset->states[d.preview->state].name;
            if (d.preview->prev >= 0 && d.preview->fade < 1) stateText += TextFormat("  (blending %d%%)", (int)(d.preview->fade * 100));
        }
        Text("state: " + stateText, prevR.x + 10, y, theme.text, 13);
        std::string evText = !d.lastEvents.empty() && GetTime() - d.lastEventTime < 1.5 ? "event: " + d.lastEvents : "";
        Text(evText, prevR.x + 10, y + 18, Color{120, 230, 140, 255}, 13);
    }
}

// ---------------------------------------------------------------------------------
// Inspector for the open document
// ---------------------------------------------------------------------------------

static float InspectState(Editor::Impl& ed, Document& d, const Fields& f, float y, int idx) {
    json& st = d.def["states"][idx];
    Rectangle r = f.r;
    std::string name = JStr(st, "name");
    bool isDefault = JStr(d.def, "default") == name;
    Text("State", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;

    f.Label("Name", y);
    std::string nn = name;
    if (TextField("sname", f.Field(y), nn)) {
        if (!RenameState(d, idx, nn)) ed.SetStatus("That name is empty or already used by another state");
    }
    y += kRow + 2;

    if (isDefault) {
        Text("Default state (the animator starts here)", r.x + 12, y + 2, Color{230, 150, 60, 255}, 12);
    } else if (Button("setdef", {r.x + 10, y, r.width - 20, kRow}, "Set as default state")) {
        d.def["default"] = name;
        Touch(d);
    }
    y += kRow + 6;

    bool blend = !JStr(st, "blend_param").empty() || (st.contains("blend") && st["blend"].is_array() && !st["blend"].empty());
    std::vector<std::string> clips = ClipNames(d);
    auto clipField = [&](const std::string& id, Rectangle fr, std::string& value) {
        if (!clips.empty()) {
            std::vector<std::string> opts = clips;
            if (std::find(opts.begin(), opts.end(), value) == opts.end() && !value.empty()) opts.push_back(value);
            return Dropdown(id, fr, value, opts);
        }
        return TextField(id, fr, value);
    };

    if (!blend) {
        f.Label("Clip", y, "animation of the model played in this state");
        std::string clip = JStr(st, "clip", name);
        if (clipField("sclip", f.Field(y), clip)) {
            st["clip"] = clip;
            Touch(d);
        }
        y += kRow + 2;
    }
    f.Label("Loop", y);
    bool loop = JBool(st, "loop", true);
    if (Checkbox("sloop", f.Field(y), loop, "")) {
        st["loop"] = loop;
        Touch(d);
    }
    y += kRow + 2;
    f.Label("Speed", y, "playback speed (1 = normal)");
    float sp = JNum(st, "speed", 1);
    if (FloatField("sspeed", f.Field(y), sp, 0.02f, "%g")) {
        st["speed"] = sp;
        Touch(d);
    }
    y += kRow + 2;
    f.Label("Speed param", y, "playback speed is multiplied by this parameter / Speed ref (e.g. walk speed)");
    std::string spp = JStr(st, "speed_param");
    if (OptionalDropdown("sspp", f.Field(y), spp, ParamNames(d, true))) {
        if (spp.empty()) st.erase("speed_param");
        else st["speed_param"] = spp;
        Touch(d);
    }
    y += kRow + 2;
    if (!spp.empty()) {
        f.Label("Speed ref", y, "parameter value at which the clip plays at normal speed");
        float ref = JNum(st, "speed_ref", 1);
        if (FloatField("sref", f.Field(y), ref, 1.0f, "%g")) {
            st["speed_ref"] = ref;
            Touch(d);
        }
        y += kRow + 2;
    }

    y += 4;
    f.Label("1D blend", y, "mix several clips by one parameter (e.g. idle -> walk -> run by speed)");
    bool b = blend;
    if (Checkbox("sblend", f.Field(y), b, "")) {
        if (b) {
            std::vector<std::string> pn = ParamNames(d, true);
            st["blend_param"] = std::find(pn.begin(), pn.end(), "speed") != pn.end() ? "speed" : pn[0];
            st["blend"] = json::array({{{"clip", JStr(st, "clip", name)}, {"at", 0}}});
        } else {
            st.erase("blend_param");
            st.erase("blend");
        }
        Touch(d);
    }
    y += kRow + 2;
    if (blend) {
        f.Label("Blend by", y);
        std::string bp = JStr(st, "blend_param");
        if (Dropdown("sbp", f.Field(y), bp, ParamNames(d, true))) {
            st["blend_param"] = bp;
            Touch(d);
        }
        y += kRow + 2;
        json& motions = Arr(st, "blend");
        int removeAt = -1;
        for (size_t i = 0; i < motions.size(); ++i) {
            PushId("m" + std::to_string(i));
            float cw = r.width - 24 - 62 - 22;
            std::string clip = JStr(motions[i], "clip");
            if (clipField("mclip", {r.x + 10, y, cw, kRow - 2}, clip)) {
                motions[i]["clip"] = clip;
                Touch(d);
            }
            float at = JNum(motions[i], "at", 0);
            if (FloatField("mat", {r.x + 14 + cw, y, 58, kRow - 2}, at, 1.0f, "%g")) {
                motions[i]["at"] = at;
                Touch(d);
            }
            if (Button("mdel", {r.x + r.width - 30, y, 20, kRow - 2}, "x")) removeAt = (int)i;
            PopId();
            y += kRow;
        }
        if (removeAt >= 0) {
            motions.erase(motions.begin() + removeAt);
            Touch(d);
        }
        if (Button("madd", {r.x + 10, y, r.width - 20, kRow - 2}, "+ Clip at parameter value")) {
            motions.push_back({{"clip", clips.empty() ? "" : clips[0]}, {"at", motions.empty() ? 0 : JNum(motions.back(), "at", 0) + 100}});
            Touch(d);
        }
        y += kRow + 4;
    }

    // animation events
    Text("Events", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    if (Hovered({r.x + 10, y, 100, kRow - 2})) Tooltip("at a time in the clip (0-1): play a sound and/or call anim_event(self, name) in Lua");
    y += kRow - 2;
    json& evs = Arr(st, "events");
    int removeEv = -1;
    for (size_t i = 0; i < evs.size(); ++i) {
        PushId("e" + std::to_string(i));
        float x = r.x + 10, w = r.width - 20;
        float tw = 50, nw = 78, xw = 20;
        float sw = w - tw - nw - xw - 12;
        float t = JNum(evs[i], "time", 0);
        if (FloatField("etime", {x, y, tw, kRow - 2}, t, 0.01f, "%.2f")) {
            evs[i]["time"] = Clampf(t, 0, 1);
            Touch(d);
        }
        std::string en = JStr(evs[i], "name");
        if (TextField("ename", {x + tw + 4, y, nw, kRow - 2}, en, "name")) {
            evs[i]["name"] = en;
            Touch(d);
        }
        std::string snd = JStr(evs[i], "sound");
        if (OptionalDropdown("esnd", {x + tw + nw + 8, y, sw, kRow - 2}, snd, ed.soundList)) {
            if (snd.empty()) evs[i].erase("sound");
            else evs[i]["sound"] = snd;
            Touch(d);
        }
        if (Button("edel", {x + w - xw, y, xw, kRow - 2}, "x")) removeEv = (int)i;
        PopId();
        y += kRow;
    }
    if (removeEv >= 0) {
        evs.erase(evs.begin() + removeEv);
        Touch(d);
    }
    if (Button("eadd", {r.x + 10, y, r.width - 20, kRow - 2}, "+ Event")) {
        evs.push_back({{"time", 0.5}, {"name", "event"}});
        Touch(d);
    }
    y += kRow + 6;

    // transitions leaving this state, to jump to them
    Text("Transitions out", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    json& tr = Arr(d.def, "transitions");
    int shown = 0;
    for (size_t i = 0; i < tr.size(); ++i) {
        if (JStr(tr[i], "from") != name) continue;
        Rectangle row{r.x + 10, y, r.width - 20, kRow - 2};
        if (Hovered(row)) Fill(row, theme.buttonHover);
        Text("-> " + JStr(tr[i], "to"), row.x + 6, row.y + 3, theme.text, 13);
        if (Clicked(row)) {
            d.view.selLink = (int)i;
            d.view.selNode = -1;
        }
        y += kRow;
        ++shown;
    }
    if (!shown) {
        Text("none: drag from the dot on the state's right edge", r.x + 12, y + 2, theme.textDim, 12);
        y += kRow;
    }
    y += 8;
    if (Button("sdel", {r.x + 10, y, r.width - 20, kRow}, "Delete state")) DeleteState(d, idx);
    return y + kRow + 8;
}

static float InspectTransition(Editor::Impl& ed, Document& d, const Fields& f, float y, int idx) {
    (void)ed;
    json& tr = d.def["transitions"][idx];
    Rectangle r = f.r;
    std::string from = JStr(tr, "from"), to = JStr(tr, "to");
    Text("Transition", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    Text((from == "*" ? std::string("Any State") : from) + "  ->  " + to, r.x + 12, y + 2, theme.text, 15, "ui_bold");
    y += kRow + 6;

    f.Label("Duration", y, "crossfade between the two poses, in seconds");
    float dur = JNum(tr, "duration", 0.15f);
    if (FloatField("tdur", f.Field(y), dur, 0.005f, "%.2f")) {
        tr["duration"] = std::max(0.0f, dur);
        Touch(d);
    }
    y += kRow + 2;
    f.Label("Exit time", y, "only after the clip reached this fraction (1 = its end); off = whenever the conditions hold");
    bool has = tr.contains("exit_time") && tr["exit_time"].is_number();
    if (Checkbox("thas", f.Field(y), has, "use")) {
        if (has) tr["exit_time"] = 1.0;
        else tr.erase("exit_time");
        Touch(d);
    }
    y += kRow + 2;
    if (has) {
        f.Label("At", y);
        float et = JNum(tr, "exit_time", 1);
        if (FloatField("tet", f.Field(y), et, 0.01f, "%.2f")) {
            tr["exit_time"] = Clampf(et, 0, 1);
            Touch(d);
        }
        y += kRow + 2;
    }

    y += 4;
    Text("Conditions (all must hold)", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    json& cs = Arr(tr, "conditions");
    int removeAt = -1;
    std::vector<std::string> pn = ParamNames(d, true);
    std::vector<std::string> ops(animator::kOps.begin(), animator::kOps.end());
    for (size_t i = 0; i < cs.size(); ++i) {
        PushId("c" + std::to_string(i));
        float x = r.x + 10, w = r.width - 20;
        float pw = w * 0.38f, ow = 62, xw = 20;
        float vw = w - pw - ow - xw - 12;
        std::string param = JStr(cs[i], "param");
        std::vector<std::string> popts = pn;
        if (std::find(popts.begin(), popts.end(), param) == popts.end()) popts.push_back(param);
        if (Dropdown("cparam", {x, y, pw, kRow - 2}, param, popts)) {
            cs[i]["param"] = param;
            cs[i]["op"] = DefaultOp(ParamType(d, param));
            Touch(d);
        }
        std::string op = JStr(cs[i], "op", "true");
        if (Dropdown("cop", {x + pw + 4, y, ow, kRow - 2}, op, ops)) {
            cs[i]["op"] = op;
            Touch(d);
        }
        bool needsValue = op != "true" && op != "false" && op != "trigger";
        if (needsValue) {
            float val = JNum(cs[i], "value", 0);
            if (FloatField("cval", {x + pw + ow + 8, y, vw, kRow - 2}, val, 0.1f, "%g")) {
                cs[i]["value"] = val;
                Touch(d);
            }
        }
        if (Button("cdel", {x + w - xw, y, xw, kRow - 2}, "x")) removeAt = (int)i;
        PopId();
        y += kRow;
    }
    if (removeAt >= 0) {
        cs.erase(cs.begin() + removeAt);
        Touch(d);
    }
    if (Button("cadd", {r.x + 10, y, r.width - 20, kRow - 2}, "+ Condition")) {
        std::string param = pn.empty() ? "speed" : pn[0];
        cs.push_back({{"param", param}, {"op", DefaultOp(ParamType(d, param))}, {"value", 0}});
        Touch(d);
    }
    y += kRow + 4;
    if (cs.empty() && !has) {
        Text("No conditions and no exit time: it fires at once.", r.x + 12, y, Color{240, 180, 60, 255}, 12);
        y += 18;
    }
    y += 6;
    if (Button("tdel", {r.x + 10, y, r.width - 20, kRow}, "Delete transition")) DeleteTransition(d, idx);
    return y + kRow + 8;
}

static float InspectParam(Editor::Impl& ed, Document& d, const Fields& f, float y, int idx) {
    (void)ed;
    json& p = d.def["parameters"][idx];
    Rectangle r = f.r;
    Text("Parameter", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    std::string name = JStr(p, "name");
    f.Label("Name", y);
    std::string nn = name;
    if (TextField("pname", f.Field(y), nn) && !nn.empty() && nn != name) {
        std::vector<std::string> taken = ParamNames(d, true);
        if (std::find(taken.begin(), taken.end(), nn) != taken.end()) {
            ed.SetStatus("A parameter with that name already exists");
        } else {
            p["name"] = nn;
            RenameParamRefs(d, name, nn);
            if (d.preview && d.preview->params.count(name)) {
                d.preview->params[nn] = d.preview->params[name];
                d.preview->params.erase(name);
            }
            Touch(d);
        }
    }
    y += kRow + 2;
    f.Label("Type", y, "trigger: one-shot, reset after use; bool: on/off; int, float: numbers");
    std::string type = JStr(p, "type", "float");
    std::vector<std::string> types(std::begin(kParamTypes), std::end(kParamTypes));
    if (Dropdown("ptype", f.Field(y), type, types)) {
        p["type"] = type;
        Touch(d);
    }
    y += kRow + 2;
    if (type != "trigger") {
        f.Label("Start value", y);
        float v = JNum(p, "value", 0);
        if (type == "bool") {
            bool b = v != 0;
            if (Checkbox("pval", f.Field(y), b, "")) {
                p["value"] = b ? 1 : 0;
                Touch(d);
            }
        } else if (FloatField("pval", f.Field(y), v, 0.1f, "%g")) {
            p["value"] = type == "int" ? roundf(v) : v;
            Touch(d);
        }
        y += kRow + 2;
    }
    y += 6;
    if (Button("pdel", {r.x + 10, y, r.width - 20, kRow}, "Delete parameter (and its conditions)")) DeleteParam(d, idx);
    return y + kRow + 8;
}

float Editor::Impl::InspectDocument(Rectangle r, float y) {
    Document* dp = Doc();
    if (!dp) return y;
    if (dp->kind == "behavior") return InspectBehavior(r, y);
    if (dp->kind == "quest" || dp->kind == "dialogue") return InspectFlow(r, y);
    Document& d = *dp;
    Fields f{r};
    Text("Animator: " + d.name, r.x + 12, y + 4, theme.text, 15, "ui_bold");
    y += kRow + 4;
    Text(Ellipsize(d.path, r.width - 24, 12), r.x + 12, y, theme.textDim, 12);
    y += 18;
    float bw = (r.width - 26) / 2;
    if (Button("dsave", {r.x + 10, y, bw, kRow + 2}, d.dirty ? "Save *" : "Save", d.dirty, d.dirty)) SaveDocument(d);
    if (Button("drevert", {r.x + 16 + bw, y, bw, kRow + 2}, "Revert", false, d.dirty)) ReloadDocument(d);
    y += kRow + 10;

    int nstates = (int)Arr(d.def, "states").size();
    if (d.view.selLink >= 0 && d.view.selLink < (int)Arr(d.def, "transitions").size()) return InspectTransition(*this, d, f, y, d.view.selLink);
    if (d.view.selNode >= 0 && d.view.selNode < nstates) return InspectState(*this, d, f, y, d.view.selNode);
    if (d.view.selNode == kAnyId) {
        Text("Any State", r.x + 12, y + 2, theme.text, 14, "ui_bold");
        y += kRow + 2;
        const char* lines[] = {"Transitions from here apply in every state (death,", "hit reactions...). Right click -> Make transition,",
                               "or drag from its dot onto a state."};
        for (const char* t : lines) {
            Text(t, r.x + 12, y, theme.textDim, 12);
            y += 17;
        }
        return y + 8;
    }
    if (d.selParam >= 0 && d.selParam < (int)Arr(d.def, "parameters").size()) return InspectParam(*this, d, f, y, d.selParam);

    // nothing selected: settings of the whole animator
    Text("Animator", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    f.Label("Default", y, "state the animator starts in");
    std::string def = JStr(d.def, "default");
    if (Dropdown("gdef", f.Field(y), def, StateNames(d))) {
        d.def["default"] = def;
        Touch(d);
    }
    y += kRow + 2;
    f.Label("Preview model", y, "model used for the clip names and the preview (not saved into the game, only here)");
    std::string pm = d.previewModel;
    std::vector<std::string> models = modelList;
    if (OptionalDropdown("gmodel", f.Field(y), pm, models)) {
        d.previewModel = pm;
        d.def["editor"]["preview_model"] = pm;
        d.dirty = true;
        d.previewStale = true;
    }
    y += kRow + 8;
    y += 6;
    Text(TextFormat("%d states, %d transitions, %d parameters", nstates, (int)Arr(d.def, "transitions").size(), (int)Arr(d.def, "parameters").size()), r.x + 12, y,
         theme.textDim, 12);
    y += 22;
    const char* tips[] = {"Click a state, a transition or a parameter to edit it.",
                          "Use the animator: set the `animator` property of an entity",
                          "(Inspector dropdown, or drag this asset from Project ->",
                          "Animators onto an entity in the 3D view).",
                          "Monsters: Inspector -> animator; their AI only fires the",
                          "triggers `attack` and `pain`, walking follows `speed`,",
                          "death follows `dead`. See docs/ANIMATION.md."};
    for (const char* t : tips) {
        Text(t, r.x + 12, y, theme.textDim, 12);
        y += 17;
    }
    return y + 10;
}

// ---------------------------------------------------------------------------------
// scripted access (console command "animator"): the same operations the mouse does, for
// automated tests and for scripting the editor
// ---------------------------------------------------------------------------------
void Editor::OpenAnimator(const std::string& name) {
    m->RefreshAnimatorList();
    m->OpenDocument("animator", name);
}

void Editor::OpenBehavior(const std::string& name) {
    m->RefreshBehaviorList();
    m->OpenDocument("behavior", name);
}

std::string Editor::AnimatorCommand(const std::vector<std::string>& a) {
    if (a.size() < 2) return "animator open|close|select_state|select_link|select_param|select_none|add_state|link|delete_state|delete_link|rename_state|add_param|param|model|save|dump";
    const std::string& cmd = a[1];
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : std::string(); };
    if (cmd == "open") {
        OpenAnimator(arg(2));
        return m->Doc() ? "opened " + m->Doc()->path : "not opened";
    }
    if (cmd == "create") {
        if (!m->CreateAnimator(arg(2))) return "create failed";
        return "created " + m->Doc()->path;
    }
    Document* d = m->Doc();
    if (!d) return "no animator open (animator open <name>)";
    if (cmd == "close") {
        m->CloseDocument(m->activeDoc, true);
        return "closed";
    }
    if (cmd == "dump") return d->def.dump();
    if (cmd == "entity") {
        // selects a map entity on the Level tab (to look at its Inspector)
        m->activeDoc = -1;
        m->ClearSelection();
        int i = atoi(arg(2).c_str());
        if (i >= 0 && i < (int)m->map.entities.size()) m->selEnts.insert(i);
        return i >= 0 && i < (int)m->map.entities.size() ? "selected " + m->map.entities[i].classname : "no such entity";
    }
    if (cmd == "tab") {
        m->activeDoc = std::min(atoi(arg(2).c_str()), (int)m->docs.size() - 1);
        return "";
    }
    if (cmd == "save") return m->SaveDocument(*d) ? "saved" : "save failed";
    if (cmd == "select_none") {
        d->view.selNode = d->view.selLink = -1;
        d->selParam = -1;
        return "";
    }
    if (cmd == "select_state") {
        int i = arg(2) == "any" ? kAnyId : StateIndex(*d, arg(2));
        if (i == -1) return "no such state";
        d->view.selNode = i;
        d->view.selLink = -1;
        d->selParam = -1;
        return "";
    }
    if (cmd == "select_link") {
        d->view.selLink = atoi(arg(2).c_str());
        d->view.selNode = -1;
        d->selParam = -1;
        return "";
    }
    if (cmd == "select_param") {
        d->selParam = atoi(arg(2).c_str());
        d->view.selNode = d->view.selLink = -1;
        return "";
    }
    if (cmd == "add_state") {
        int i = AddState(*d, {(float)atof(arg(4).c_str()), (float)atof(arg(5).c_str())}, arg(2));
        if (a.size() < 6) SetStatePos(*d, JStr(d->def["states"][i], "name"), DefaultStatePos(i));
        return "state " + JStr(d->def["states"][i], "name");
    }
    if (cmd == "delete_state") {
        DeleteState(*d, StateIndex(*d, arg(2)));
        return "";
    }
    if (cmd == "rename_state") return RenameState(*d, StateIndex(*d, arg(2)), arg(3)) ? "renamed" : "rename failed";
    if (cmd == "link") {
        std::string from = arg(2) == "any" ? "*" : arg(2);
        if (from != "*" && StateIndex(*d, from) < 0) return "no such state: " + from;
        if (StateIndex(*d, arg(3)) < 0) return "no such state: " + arg(3);
        int t = AddTransition(*d, from, arg(3));
        return t >= 0 ? "transition " + std::to_string(t) : "refused";
    }
    if (cmd == "delete_link") {
        DeleteTransition(*d, atoi(arg(2).c_str()));
        return "";
    }
    if (cmd == "add_param") return "param " + std::to_string(AddParam(*d, arg(2), arg(3).empty() ? "float" : arg(3)));
    if (cmd == "param") {
        if (d->previewStale || !d->preview) RebuildPreview(*d);
        if (!d->preview) return "no preview";
        if (arg(3) == "fire") d->preview->triggers.insert(arg(2));
        else d->preview->params[arg(2)] = (float)atof(arg(3).c_str());
        return "";
    }
    if (cmd == "model") {
        d->previewModel = arg(2);
        d->def["editor"]["preview_model"] = arg(2);
        d->previewStale = true;
        return PreviewModel(*d) ? "model ok, clips: " + std::to_string(ClipNames(*d).size()) : "model not found";
    }
    return "unknown command";
}

}  // namespace rbs
