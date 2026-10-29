// RBS Engine - editor: quest and dialogue graphs (graph tabs). Both are the same kind of
// graph - nodes with output ports linked to the nodes that come next - so one editor
// serves both; the node menu only offers the types that make sense in the kind of graph
// (objectives in quests, say / choice in dialogues). The node types, their parameters and
// ports come from Lua (rbs.flow_library in scripts/quests.lua), so nodes added by a
// project's scripts show up too. Files: quests/<name>.json and dialogues/<name>.json.
#include "core/log.h"
#include "core/vfs.h"
#include "editor/editor_doc.h"
#include "editor/editor_impl.h"
#include "render/animation.h"
#include "render/renderer.h"
#include "ui/imgui.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>

namespace rbs {

using namespace ui;
using namespace edoc;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------
// node library (from Lua)
// ---------------------------------------------------------------------------------
static const json* LibNode(Editor::Impl& ed, const std::string& type) {
    if (!ed.flowLibrary.is_object() || !ed.flowLibrary.contains("nodes") || !ed.flowLibrary["nodes"].is_array()) return nullptr;
    for (auto& n : ed.flowLibrary["nodes"])
        if (JStr(n, "id") == type) return &n;
    return nullptr;
}

static std::string CategoryOf(Editor::Impl& ed, const std::string& type) {
    const json* m = LibNode(ed, type);
    return m ? JStr(*m, "category", "action") : "unknown";
}

static Color CategoryColor(const std::string& cat, const std::string& type) {
    if (type == "start") return {52, 128, 70, 255};
    if (type == "end") return {150, 60, 60, 255};
    if (cat == "flow") return {70, 84, 112, 255};
    if (cat == "objective") return {172, 118, 40, 255};
    if (cat == "action") return {52, 108, 160, 255};
    if (cat == "dialogue") return {58, 138, 108, 255};
    if (cat == "cutscene") return {138, 96, 168, 255};
    return {150, 50, 50, 255};
}

static std::string Capital(std::string s) {
    if (!s.empty()) s[0] = (char)toupper(s[0]);
    return s;
}

static bool InScope(const json& meta, const std::string& kind) {
    if (!meta.contains("scopes") || !meta["scopes"].is_array()) return true;
    for (auto& s : meta["scopes"])
        if (s.is_string() && s.get<std::string>() == kind) return true;
    return false;
}

static const char* kCategories[] = {"flow", "objective", "action", "dialogue", "cutscene"};

// ---------------------------------------------------------------------------------
// the graph in JSON
// ---------------------------------------------------------------------------------
static json& Nodes(Document& d) { return Arr(d.def, "nodes"); }

static int IndexOf(Document& d, const std::string& id) {
    json& ns = Nodes(d);
    for (size_t i = 0; i < ns.size(); ++i)
        if (JStr(ns[i], "id") == id) return (int)i;
    return -1;
}

static std::string IdOf(Document& d, int idx) { return idx >= 0 && idx < (int)Nodes(d).size() ? JStr(Nodes(d)[idx], "id") : ""; }

static std::string NewId(Document& d) {
    for (int i = 1;; ++i) {
        std::string id = "n" + std::to_string(i);
        if (IndexOf(d, id) < 0) return id;
    }
}

static json& LayoutOf(Document& d) {
    json& ed = d.def["editor"];
    if (!ed.is_object()) ed = json::object();
    if (!ed.contains("layout") || !ed["layout"].is_object()) ed["layout"] = json::object();
    return ed["layout"];
}

static Vector2 NodePos(Document& d, const std::string& id, Vector2 def) {
    json& lay = LayoutOf(d);
    Vector2 p = def;
    if (lay.contains(id)) ReadPos(lay[id], p);
    return p;
}

static void SetNodePos(Document& d, const std::string& id, Vector2 p) { LayoutOf(d)[id] = json::array({roundf(p.x), roundf(p.y)}); }

// output ports of a node: the names used in the file ("next", "yes", "o1"...)
static std::vector<std::string> PortNames(Editor::Impl& ed, const json& node) {
    const json* meta = LibNode(ed, JStr(node, "type"));
    std::vector<std::string> out;
    if (!meta) return {"next"};  // unknown type: keep what links it has
    const json& ports = (*meta)["ports"];
    if (ports.is_string()) {  // "options": one port per answer of a choice
        size_t n = node.contains("options") && node["options"].is_array() ? node["options"].size() : 0;
        for (size_t i = 1; i <= std::max<size_t>(1, n); ++i) out.push_back("o" + std::to_string(i));
    } else if (ports.is_array()) {
        for (auto& p : ports)
            if (p.is_string()) out.push_back(p.get<std::string>());
    } else {
        out.push_back("next");
    }
    return out;
}

static std::string PortLabel(const json& node, const std::string& port, size_t count) {
    if (port.size() > 1 && port[0] == 'o' && isdigit((unsigned char)port[1])) {
        size_t i = (size_t)atoi(port.c_str() + 1);
        if (node.contains("options") && node["options"].is_array() && i >= 1 && i <= node["options"].size()) {
            std::string t = JStr(node["options"][i - 1], "text", "...");
            return t.size() > 20 ? t.substr(0, 19) + "..." : t;
        }
        return port;
    }
    return port == "next" && count == 1 ? "" : port;
}

static std::vector<std::string> LinkTargets(const json& node, const std::string& port) {
    std::vector<std::string> out;
    if (!node.contains("out") || !node["out"].is_object() || !node["out"].contains(port) || !node["out"][port].is_array()) return out;
    for (auto& t : node["out"][port])
        if (t.is_string()) out.push_back(t.get<std::string>());
    return out;
}

static bool AddLink(Editor::Impl& ed, Document& d, int src, const std::string& port, int tgt) {
    if (src < 0 || src >= (int)Nodes(d).size() || tgt < 0 || tgt >= (int)Nodes(d).size() || src == tgt) return false;
    json& n = Nodes(d)[src];
    std::vector<std::string> ports = PortNames(ed, n);
    if (std::find(ports.begin(), ports.end(), port) == ports.end()) return false;
    if (JStr(Nodes(d)[tgt], "type") == "start") return false;  // nothing leads into the start
    std::string tid = IdOf(d, tgt);
    json& arr = n["out"][port];
    if (!arr.is_array()) arr = json::array();
    for (auto& t : arr)
        if (t == tid) return false;
    arr.push_back(tid);
    Touch(d);
    return true;
}

static void RemoveLink(Document& d, int src, const std::string& port, int tgt) {
    if (src < 0 || src >= (int)Nodes(d).size()) return;
    json& n = Nodes(d)[src];
    if (!n.contains("out") || !n["out"].contains(port)) return;
    std::string tid = IdOf(d, tgt);
    json& arr = n["out"][port];
    for (size_t i = arr.size(); i-- > 0;)
        if (arr[i] == tid) arr.erase(arr.begin() + i);
    d.view.selLink = -1;
    Touch(d);
}

static int AddNode(Editor::Impl& ed, Document& d, const std::string& type, Vector2 pos) {
    std::string id = NewId(d);
    json n = {{"id", id}, {"type", type}, {"out", json::object()}};
    if (type == "choice") n["options"] = json::array({{{"text", "Yes"}}, {{"text", "No"}}});
    Nodes(d).push_back(n);
    SetNodePos(d, id, pos);
    Touch(d);
    (void)ed;
    return (int)Nodes(d).size() - 1;
}

static void DeleteNode(Document& d, int idx) {
    if (idx < 0 || idx >= (int)Nodes(d).size()) return;
    std::string id = IdOf(d, idx);
    Nodes(d).erase(Nodes(d).begin() + idx);
    // links into the node go with it
    for (auto& n : Nodes(d)) {
        if (!n.contains("out") || !n["out"].is_object()) continue;
        for (auto it = n["out"].begin(); it != n["out"].end(); ++it) {
            json& arr = it.value();
            for (size_t i = arr.size(); i-- > 0;)
                if (arr[i] == id) arr.erase(arr.begin() + i);
        }
    }
    LayoutOf(d).erase(id);
    d.view.selNode = d.view.selLink = -1;
    Touch(d);
}

static void SetType(Editor::Impl& ed, Document& d, int idx, const std::string& type) {
    json& n = Nodes(d)[idx];
    n["type"] = type;
    n.erase("params");
    if (type == "choice" && !n.contains("options")) n["options"] = json::array({{{"text", "Yes"}}, {{"text", "No"}}});
    if (type != "choice") n.erase("options");
    // links on ports the new type does not have are dropped
    std::vector<std::string> ports = PortNames(ed, n);
    if (n.contains("out") && n["out"].is_object()) {
        for (auto it = n["out"].begin(); it != n["out"].end();) {
            if (std::find(ports.begin(), ports.end(), it.key()) == ports.end()) it = n["out"].erase(it);
            else ++it;
        }
    }
    Touch(d);
}

// removing the answer i of a choice: its links go, the later answers move up one port
static void RemoveOption(Document& d, int idx, int i) {
    json& n = Nodes(d)[idx];
    json& opts = n["options"];
    if (i < 0 || i >= (int)opts.size()) return;
    opts.erase(opts.begin() + i);
    json& out = n["out"];
    out.erase("o" + std::to_string(i + 1));
    for (int k = i + 1; k <= (int)opts.size(); ++k) {
        std::string from = "o" + std::to_string(k + 1), to = "o" + std::to_string(k);
        if (out.contains(from)) {
            out[to] = out[from];
            out.erase(from);
        }
    }
    Touch(d);
}

static std::string Summary(Editor::Impl& ed, const json& node) {
    std::string type = JStr(node, "type");
    if (!LibNode(ed, type)) return "unknown type";
    if (!node.contains("params") || !node["params"].is_object()) return "";
    const json* meta = LibNode(ed, type);
    std::string s;
    if (meta->contains("params") && (*meta)["params"].is_array()) {
        for (auto& pd : (*meta)["params"]) {
            std::string name = JStr(pd, "name");
            if (!node["params"].contains(name)) continue;
            const json& v = node["params"][name];
            std::string text = v.is_string() ? v.get<std::string>() : v.dump();
            if (text.empty()) continue;
            if (!s.empty()) s += ", ";
            s += text;
            if (s.size() > 30) break;
        }
    }
    return s.size() > 32 ? s.substr(0, 31) + "..." : s;
}

// layers left to right by distance from the start; nodes nothing leads to go below
static void Arrange(Editor::Impl& ed, Document& d) {
    json& ns = Nodes(d);
    std::vector<int> depth(ns.size(), -1);
    std::vector<int> queue;
    for (size_t i = 0; i < ns.size(); ++i)
        if (JStr(ns[i], "type") == "start") depth[i] = 0, queue.push_back((int)i);
    for (size_t q = 0; q < queue.size(); ++q) {
        int i = queue[q];
        for (auto& port : PortNames(ed, ns[i]))
            for (auto& tid : LinkTargets(ns[i], port)) {
                int t = IndexOf(d, tid);
                if (t >= 0 && depth[t] < 0) depth[t] = depth[i] + 1, queue.push_back(t);
            }
    }
    int maxDepth = 0;
    for (int dp : depth) maxDepth = std::max(maxDepth, dp);
    std::vector<float> yAt(maxDepth + 3, 0);
    for (size_t i = 0; i < ns.size(); ++i) {
        int dp = depth[i] >= 0 ? depth[i] : maxDepth + 1;
        size_t ports = PortNames(ed, ns[i]).size();
        float h = std::max(56.0f, 22.0f * ports + 16);
        SetNodePos(d, IdOf(d, (int)i), {40.0f + dp * 270.0f, yAt[dp]});
        yAt[dp] += h + 34;
    }
}

static std::vector<std::string> Issues(Editor::Impl& ed, Document& d) {
    std::vector<std::string> out;
    json& ns = Nodes(d);
    int starts = 0;
    for (auto& n : ns)
        if (JStr(n, "type") == "start") ++starts;
    if (starts == 0) out.push_back("No Start node: the flow never begins.");
    // reachable from a start
    std::set<int> seen;
    std::vector<int> queue;
    for (size_t i = 0; i < ns.size(); ++i)
        if (JStr(ns[i], "type") == "start") queue.push_back((int)i), seen.insert((int)i);
    for (size_t q = 0; q < queue.size(); ++q) {
        for (auto& port : PortNames(ed, ns[queue[q]]))
            for (auto& tid : LinkTargets(ns[queue[q]], port)) {
                int t = IndexOf(d, tid);
                if (t >= 0 && !seen.count(t)) seen.insert(t), queue.push_back(t);
            }
    }
    int unreachable = 0;
    for (size_t i = 0; i < ns.size(); ++i) {
        std::string type = JStr(ns[i], "type");
        std::string label = JStr(ns[i], "label", type);
        const json* meta = LibNode(ed, type);
        if (!meta) out.push_back("'" + label + "': unknown node type '" + type + "' (the flow stops there).");
        else if (!InScope(*meta, d.kind)) out.push_back("'" + label + "' is not meant for " + d.kind + "s.");
        if (!seen.count((int)i)) ++unreachable;
        // a branch / choice with an unlinked port ends the flow there
        if (meta && (*meta)["ports"].is_array() && (*meta)["ports"].size() >= 2) {
            for (auto& port : PortNames(ed, ns[i]))
                if (LinkTargets(ns[i], port).empty()) out.push_back("'" + label + "': port '" + port + "' leads nowhere.");
        }
    }
    if (unreachable) out.push_back(std::to_string(unreachable) + " node(s) cannot be reached from the Start (never run).");
    return out;
}

// ---------------------------------------------------------------------------------
// listing, creating
// ---------------------------------------------------------------------------------
void Editor::Impl::RefreshFlowLists() {
    questList = vfs::List("quests", ".json", true);
    dialogueList = vfs::List("dialogues", ".json", true);
    cutsceneList = vfs::List("cutscenes", ".json", true);
}

void Editor::Impl::InitFlowDocument(Document& d) {
    Arr(d.def, "nodes");
    Arr(d.def, "vars");
    bool missing = !d.def.contains("editor");
    for (auto& n : Nodes(d))
        if (!LayoutOf(d).contains(JStr(n, "id"))) missing = true;
    if (missing) Arrange(*this, d);
}

bool Editor::Impl::CreateFlow(const std::string& kind, const std::string& name) {
    bool valid = !name.empty();
    for (char c : name)
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_')) valid = false;
    if (!valid) {
        SetStatus("Use lowercase letters, digits and _ for the name");
        return false;
    }
    std::string path = (kind == "quest" ? "quests/" : kind == "cutscene" ? "cutscenes/" : "dialogues/") + name + ".json";
    if (vfs::Exists(path)) {
        SetStatus("'" + path + "' already exists");
        return false;
    }
    json def;
    if (kind == "quest") {
        def = {{"title", name},
               {"vars", json::array()},
               {"nodes",
                json::array({{{"id", "n1"}, {"type", "start"}, {"out", {{"next", json::array({"n2"})}}}},
                             {{"id", "n2"}, {"type", "kill"}, {"params", {{"class", "monster_grunt"}, {"count", 3}, {"text", "Kill the grunts"}}}, {"out", {{"next", json::array({"n3"})}}}},
                             {{"id", "n3"}, {"type", "end"}, {"params", {{"result", "success"}}}, {"out", json::object()}}})}};
    } else if (kind == "cutscene") {
        def = {{"title", name},
               {"vars", json::array()},
               {"nodes",
                json::array({{{"id", "n1"}, {"type", "start"}, {"out", {{"next", json::array({"n2"})}}}},
                             {{"id", "n2"}, {"type", "cs_fade"}, {"params", {{"to", "black"}, {"seconds", 0}}}, {"out", {{"next", json::array({"n3"})}}}},
                             {{"id", "n3"}, {"type", "cs_camera"}, {"params", {{"who", "player"}, {"shot", "medium"}, {"motion", "push-in"}, {"seconds", 3}, {"blend", 0}}}, {"out", {{"next", json::array({"n4"})}}}},
                             {{"id", "n4"}, {"type", "cs_fade"}, {"params", {{"to", "clear"}, {"seconds", 1}}}, {"out", {{"next", json::array({"n5"})}}}},
                             {{"id", "n5"}, {"type", "end"}, {"out", json::object()}}})}};
    } else {
        def = {{"title", name},
               {"vars", json::array()},
               {"nodes",
                json::array({{{"id", "n1"}, {"type", "start"}, {"out", {{"next", json::array({"n2"})}}}},
                             {{"id", "n2"}, {"type", "say"}, {"params", {{"speaker", "Someone"}, {"text", "Hello there."}}}, {"out", {{"next", json::array({"n3"})}}}},
                             {{"id", "n3"}, {"type", "end"}, {"out", json::object()}}})}};
    }
    if (!vfs::WriteText(path, def.dump(2) + "\n")) {
        SetStatus("Cannot write " + path);
        return false;
    }
    RefreshFlowLists();
    OpenDocument(kind, path);
    std::string where = vfs::CurrentMod().empty() ? "base/" : "mods/" + vfs::CurrentMod() + "/";
    SetStatus("Created " + where + path);
    return true;
}

std::vector<std::string> Editor::Impl::QuestOptions() const {
    std::vector<std::string> out{kNone};
    for (auto& s : questList) out.push_back(s);
    return out;
}

std::vector<std::string> Editor::Impl::CutsceneOptions() const {
    std::vector<std::string> out{kNone};
    for (auto& s : cutsceneList) out.push_back(s);
    return out;
}

std::vector<std::string> Editor::Impl::DialogueOptions() const {
    std::vector<std::string> out{kNone};
    for (auto& s : dialogueList) out.push_back(s);
    return out;
}

// ---------------------------------------------------------------------------------
// graph building
// ---------------------------------------------------------------------------------
// links are named by where they start, which port and where they end
static int LinkId(int src, int port, int tgt) { return (src * 64 + port) * 4096 + tgt; }
static void DecodeLink(int id, int& src, int& port, int& tgt) {
    tgt = id % 4096;
    port = (id / 4096) % 64;
    src = id / (4096 * 64);
}

static void BuildGraph(Editor::Impl& ed, Document& d, std::vector<graph::Node>& nodes, std::vector<graph::Link>& links) {
    json& ns = Nodes(d);
    for (size_t i = 0; i < ns.size(); ++i) {
        std::string type = JStr(ns[i], "type");
        std::string cat = CategoryOf(ed, type);
        const json* meta = LibNode(ed, type);
        graph::Node n;
        n.id = (int)i;
        n.title = JStr(ns[i], "label", meta ? JStr(*meta, "title", type) : type);
        n.subtitle = Summary(ed, ns[i]);
        n.badge = Capital(cat);
        n.color = CategoryColor(cat, type);
        n.in = type != "start";
        std::vector<std::string> ports = PortNames(ed, ns[i]);
        bool noOut = meta && (*meta)["ports"].is_array() && (*meta)["ports"].empty();
        n.out = !noOut;
        if (!noOut)
            for (auto& p : ports) n.ports.push_back(PortLabel(ns[i], p, ports.size()));
        n.pos = NodePos(d, JStr(ns[i], "id"), {40 + (float)i * 40, (float)i * 30});
        n.size = {196, std::max(50.0f, 22.0f * (float)n.ports.size() + 16)};
        nodes.push_back(n);
    }
    for (size_t i = 0; i < ns.size(); ++i) {
        std::vector<std::string> ports = PortNames(ed, ns[i]);
        for (size_t p = 0; p < ports.size(); ++p) {
            for (auto& tid : LinkTargets(ns[i], ports[p])) {
                int t = IndexOf(d, tid);
                if (t < 0) continue;
                graph::Link l;
                l.id = LinkId((int)i, (int)p, t);
                l.from = (int)i;
                l.to = t;
                l.fromPort = (int)p;
                if (ports.size() >= 2 && p == 0 && ports[0] == "yes") l.color = {120, 200, 130, 255};
                if (ports.size() >= 2 && ports.size() == 2 && ports[1] == "no") {
                    if (p == 1) l.color = {220, 120, 110, 255};
                }
                links.push_back(l);
            }
        }
    }
}

static std::vector<std::string> VarNames(Editor::Impl& ed, Document& d) {
    std::vector<std::string> out;
    auto add = [&](const std::string& s) {
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    };
    for (auto& v : Arr(d.def, "vars")) add(JStr(v, "name"));
    // variables the nodes of this graph read or write
    for (auto& n : Nodes(d)) {
        const json* meta = LibNode(ed, JStr(n, "type"));
        if (!meta || !n.contains("params") || !n["params"].is_object()) continue;
        if (meta->contains("params") && (*meta)["params"].is_array())
            for (auto& pd : (*meta)["params"])
                if (JStr(pd, "type") == "var" && n["params"].contains(JStr(pd, "name")) && n["params"][JStr(pd, "name")].is_string()) add(n["params"][JStr(pd, "name")].get<std::string>());
        if (n.contains("options") && n["options"].is_array())
            for (auto& o : n["options"]) add(JStr(o, "require"));
    }
    return out;
}

// ---------------------------------------------------------------------------------
// the document
// ---------------------------------------------------------------------------------
static json RenderDialogueScene(Editor::Impl& ed, Document& d);
static void DrawDialoguePanel(Editor::Impl& ed, Document& d, Rectangle panel, const json& st);

void Editor::Impl::DrawFlowDoc(Rectangle r) {
    Document* dp = Doc();
    if (!dp) return;
    Document& d = *dp;
    Block(r);
    bool showVars = open.count("flow_vars") ? open["flow_vars"] : true;
    bool isDialogue = d.kind == "dialogue";
    bool showPreview = isDialogue && (open.count("flow_preview") ? open["flow_preview"] : true);
    json previewState;
    if (showPreview) previewState = RenderDialogueScene(*this, d);  // renders to a texture: before any clipping

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
    if (btn("arrange", "Arrange", 72)) {
        Arrange(*this, d);
        Touch(d);
        d.fitted = false;
    }
    if (btn("vars", "Variables", 82, showVars)) open["flow_vars"] = !showVars;
    if (isDialogue && btn("preview", "Preview", 72, showPreview)) open["flow_preview"] = !showPreview;
    Text("right click: add node   -   drag from a dot (port) to the next node   -   drag on empty: select box   -   Space+drag: move view   -   Ctrl+Z / C / V", bx + 8, bar.y + 8, theme.textDim, 12);

    Rectangle canvas{r.x, r.y + 30, r.width, r.height - 30};
    size_t declared = Arr(d.def, "vars").size();
    Rectangle varR{canvas.x + 8, canvas.y + 8, 250, std::min(canvas.height - 16, 78.0f + 26.0f * declared + 24)};
    Rectangle prevR{};
    if (showPreview) {
        float w = d.previewMax ? std::min(canvas.width - 16, (canvas.height - 16 - 60) * 16.0f / 9.0f + 16) : 440.0f;
        w = std::max(w, 320.0f);
        float h = 30 + (w - 16) * 9.0f / 16.0f + 40;
        prevR = {d.previewMax ? canvas.x + (canvas.width - w) / 2 : canvas.x + canvas.width - 8 - w, canvas.y + 8, w, h};
    }
    d.view.exclude.clear();
    if (showVars) d.view.exclude.push_back(varR);
    if (showPreview) d.view.exclude.push_back(prevR);

    std::vector<graph::Node> nodes;
    std::vector<graph::Link> links;
    BuildGraph(*this, d, nodes, links);
    if (!d.fitted) {
        float left = showVars ? varR.width + 16 : 0, right = showPreview && !d.previewMax ? prevR.width + 16 : 0;
        Rectangle free{canvas.x + left, canvas.y, std::max(100.0f, canvas.width - left - right), canvas.height};
        graph::Fit(d.view, free, nodes);
        d.view.pan.x += left;
        d.fitted = true;
    }
    graph::Events ev = graph::Draw("flow_graph", canvas, d.view, nodes, links);

    if (ev.moved) {
        for (auto& n : nodes)
            if (n.id < (int)Nodes(d).size()) SetNodePos(d, IdOf(d, n.id), n.pos);
        d.dirty = true;
        d.stamp++;
    }
    if (ev.selectionChanged) d.selParam = -1;
    if (ev.linkFrom >= 0 && ev.linkTo >= 0 && ev.linkFrom < (int)Nodes(d).size()) {
        std::vector<std::string> ports = PortNames(*this, Nodes(d)[ev.linkFrom]);
        int pi = ev.linkFromPort >= 0 ? ev.linkFromPort : 0;
        if (pi < (int)ports.size() && AddLink(*this, d, ev.linkFrom, ports[pi], ev.linkTo)) {
            d.view.selNode = -1;
            d.view.selLink = LinkId(ev.linkFrom, pi, ev.linkTo);
        } else {
            SetStatus("That link is not possible (nothing leads into a Start, and the same link cannot be made twice)");
        }
    }
    if (ev.deleteKey) {
        if (d.view.selLink >= 0) {
            int s, p, t;
            DecodeLink(d.view.selLink, s, p, t);
            std::vector<std::string> ports = s < (int)Nodes(d).size() ? PortNames(*this, Nodes(d)[s]) : std::vector<std::string>();
            if (p < (int)ports.size()) RemoveLink(d, s, ports[p], t);
        } else {
            DeleteFlowSelection(d);
        }
    }
    if (ev.context) {
        std::vector<std::string> items;
        d.menuTypes.clear();
        if (ev.contextNode >= 0) {
            items.push_back("Delete node");
        } else if (ev.contextLink >= 0) {
            items.push_back("Delete link");
        } else if (flowLibrary.is_object() && flowLibrary.contains("nodes")) {
            for (const char* cat : kCategories) {
                for (auto& n : flowLibrary["nodes"]) {
                    if (JStr(n, "category") != cat || !InScope(n, d.kind)) continue;
                    std::string item = "Add node/" + Capital(cat) + "/" + JStr(n, "title", JStr(n, "id"));
                    items.push_back(item);
                    d.menuTypes[item] = JStr(n, "id");
                }
            }
        }
        graph::OpenMenu(d.view, items, ev);
    }
    if (ev.menuChoice >= 0) {
        const std::string& text = ev.menuText;
        if (text == "Delete node") {
            DeleteNode(d, ev.menuNode);
        } else if (text == "Delete link") {
            int s, p, t;
            DecodeLink(ev.menuLink, s, p, t);
            std::vector<std::string> ports = s < (int)Nodes(d).size() ? PortNames(*this, Nodes(d)[s]) : std::vector<std::string>();
            if (p < (int)ports.size()) RemoveLink(d, s, ports[p], t);
        } else if (d.menuTypes.count(text)) {
            int idx = AddNode(*this, d, d.menuTypes[text], ev.menuGraphPos);
            d.view.selNode = idx;
            d.view.selLink = -1;
            d.selParam = -1;
        }
    }

    // ---- dialogue preview ----
    if (showPreview) DrawDialoguePanel(*this, d, prevR, previewState);

    // ---- variables panel ----
    if (showVars) {
        Fill(varR, Color{27, 31, 40, 240});
        Outline(varR, theme.border);
        Text("VARIABLES", varR.x + 10, varR.y + 8, theme.textDim, 12, "ui_bold");
        if (Button("addvar", {varR.x + varR.width - 70, varR.y + 5, 62, 20}, "+ Add")) {
            std::string name = UniqueName(VarNames(*this, d), "var");
            Arr(d.def, "vars").push_back({{"name", name}, {"value", "0"}});
            Touch(d);
        }
        float y = varR.y + 30;
        Text("start values (shared by all quests and dialogues)", varR.x + 10, y, theme.textDim, 11);
        y += 16;
        json& vars = Arr(d.def, "vars");
        int removeAt = -1;
        for (size_t i = 0; i < vars.size(); ++i) {
            PushId("v" + std::to_string(i));
            std::string name = JStr(vars[i], "name");
            std::string val = vars[i].contains("value") ? (vars[i]["value"].is_string() ? vars[i]["value"].get<std::string>() : vars[i]["value"].dump()) : "";
            if (TextField("vname", {varR.x + 8, y, 118, kRow - 2}, name) && !name.empty()) {
                std::string old = JStr(vars[i], "name");
                if (name != old) {
                    // nodes that use the variable follow the rename
                    for (auto& n : Nodes(d)) {
                        const json* meta = LibNode(*this, JStr(n, "type"));
                        if (!meta || !n.contains("params") || !n["params"].is_object()) continue;
                        for (auto& pd : (*meta)["params"])
                            if (JStr(pd, "type") == "var" && n["params"].contains(JStr(pd, "name")) && n["params"][JStr(pd, "name")] == old) n["params"][JStr(pd, "name")] = name;
                        if (n.contains("options") && n["options"].is_array())
                            for (auto& o : n["options"])
                                if (JStr(o, "require") == old) o["require"] = name;
                    }
                    vars[i]["name"] = name;
                    Touch(d);
                }
            }
            if (TextField("vval", {varR.x + 130, y, 78, kRow - 2}, val)) {
                vars[i]["value"] = val;
                Touch(d);
            }
            if (Button("vdel", {varR.x + varR.width - 30, y, 22, kRow - 2}, "x")) removeAt = (int)i;
            PopId();
            y += 26;
        }
        if (removeAt >= 0) {
            vars.erase(vars.begin() + removeAt);
            Touch(d);
        }
        std::vector<std::string> used = VarNames(*this, d);
        std::vector<std::string> undeclared;
        for (auto& u : used) {
            bool decl = false;
            for (auto& v : vars) decl = decl || JStr(v, "name") == u;
            if (!decl) undeclared.push_back(u);
        }
        if (!undeclared.empty()) {
            y += 4;
            Text("used here, not declared:", varR.x + 10, y, theme.textDim, 11);
            y += 15;
            for (auto& u : undeclared) {
                Text(Ellipsize(u, 220, 12), varR.x + 12, y, theme.text, 12);
                y += 16;
            }
        }
    }
}

// ---------------------------------------------------------------------------------
// dialogue preview: the line as the game shows it (3D shot, speaker, box, typing text)
// ---------------------------------------------------------------------------------
static bool IsLine(Document& d, int idx) {
    if (idx < 0 || idx >= (int)Nodes(d).size()) return false;
    std::string t = JStr(Nodes(d)[idx], "type");
    return t == "say" || t == "choice";
}

// the first Say / Choice reached from a port, passing through logic nodes (-1 if none)
static int FindNextLine(Editor::Impl& ed, Document& d, int from, const std::string& port) {
    std::vector<int> queue;
    std::set<int> seen;
    for (auto& tid : LinkTargets(Nodes(d)[from], port)) {
        int t = IndexOf(d, tid);
        if (t >= 0) queue.push_back(t);
    }
    for (size_t q = 0; q < queue.size() && q < 64; ++q) {
        int i = queue[q];
        if (seen.count(i)) continue;
        seen.insert(i);
        if (IsLine(d, i)) return i;
        for (auto& p : PortNames(ed, Nodes(d)[i]))
            for (auto& tid : LinkTargets(Nodes(d)[i], p)) {
                int t = IndexOf(d, tid);
                if (t >= 0) queue.push_back(t);
            }
    }
    return -1;
}

// targetname -> model of the map's entities: a speaker's "entity" shows that entity's model
static json MapEntityInfo(Editor::Impl& ed) {
    json out = json::object();
    for (auto& e : ed.map.entities) {
        std::string tn = e.Str("targetname", "");
        if (tn.empty()) continue;
        std::string model = e.Str("model", "");
        if (model.empty())
            if (const ClassInfo* ci = ed.ClassFor(e.classname)) model = ci->model;
        json info = {{"model", model}, {"scale", e.Num("scale", 1.0f)}, {"pos", {e.origin.x, e.origin.y, e.origin.z}}, {"yaw", e.angle}};
        if (const ClassInfo* ci = ed.ClassFor(e.classname))
            info["mins"] = {ci->mins.x, ci->mins.y, ci->mins.z}, info["maxs"] = {ci->maxs.x, ci->maxs.y, ci->maxs.z};
        out[tn] = info;
    }
    return out;
}

// where the player starts (the "player" of a cutscene shot in the editor)
static json PlayerInfo(Editor::Impl& ed) {
    for (auto& e : ed.map.entities)
        if (e.classname == "player_start") return {{"pos", {e.origin.x, e.origin.y, e.origin.z}}, {"yaw", e.angle}, {"mins", {-16, -24, -16}}, {"maxs", {16, 24, 16}}};
    return json();
}

static Vector3 V3(const json& a) { return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()}; }

void Editor::Impl::LookThroughCamera(Document& d, int nodeIndex) {
    json res = script.CallJson("flow_cutscene_shot", {{"def", d.def}, {"node", IdOf(d, nodeIndex)}, {"t", 0}, {"entities", MapEntityInfo(*this)}, {"player", PlayerInfo(*this)}});
    if (!res.is_object() || !res.contains("camera")) {
        SetStatus(res.is_object() && res.contains("error") ? res["error"].get<std::string>() : "Cannot work out the camera");
        return;
    }
    const json& c = res["camera"];
    Vector3 pos = V3(c["pos"]), look = V3(c["look"]);
    Vector3 dir = Vector3Normalize(look - pos);
    EditorView& v = views[1];
    v.camPos = pos;
    v.yaw = atan2f(-dir.z, dir.x) * RAD2DEG;
    v.pitch = asinf(Clamp(dir.y, -1.0f, 1.0f)) * RAD2DEG;
    activeDoc = -1;  // back to the map to see it
    SetStatus("3D view moved to the camera of '" + JStr(Nodes(d)[nodeIndex], "label", JStr(Nodes(d)[nodeIndex], "type")) + "'");
}


// Asks the script code what the line looks like and draws the 3D shot into the document's texture.
static json RenderDialogueScene(Editor::Impl& ed, Document& d) {
    // which line: the selected one, else the last one shown, else the first
    std::string id = d.previewNode;
    if (IsLine(d, d.view.selNode)) id = IdOf(d, d.view.selNode);
    if (id != d.previewNode) {
        d.previewNode = id;
        d.previewStart = GetTime();
    }
    json st = ed.script.CallJson("flow_preview_state", {{"def", d.def}, {"node", id}, {"t", GetTime() - d.previewStart}, {"entities", MapEntityInfo(ed)}});
    if (!st.is_object() || !st.contains("camera")) return st;
    if (st.contains("node") && st["node"].is_string()) d.previewNode = st["node"].get<std::string>();
    if (!d.rt.id) d.rt = LoadRenderTexture(640, 360);
    const json& c = st["camera"];
    Camera3D cam{};
    cam.position = V3(c["pos"]);
    cam.target = V3(c["look"]);
    cam.up = {0, 1, 0};
    cam.fovy = c["fov"].get<float>();
    cam.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(d.rt);
    ClearBackground(Color{26, 30, 40, 255});
    BeginMode3D(cam);
    DrawPlane({0, -0.2f, 0}, {900, 900}, Color{34, 38, 50, 255});
    DrawGrid(20, 40);
    std::string model = st["speaker"].contains("model") && st["speaker"]["model"].is_string() ? st["speaker"]["model"].get<std::string>() : "";
    if (!model.empty() && vfs::Exists(model)) {
        ModelAsset& ma = assets::GetModel(model);
        if (!ma.missing) {
            // the line's animation plays once (a gesture), otherwise the character idles
            double t = GetTime() - d.previewStart;
            std::string anim = st["ui"].contains("animation") && st["ui"]["animation"].is_string() ? st["ui"]["animation"].get<std::string>() : "";
            static anim::Pose pose;
            int clip = anim.empty() ? -1 : ma.FindAnim(ToLower(anim));
            bool once = clip >= 0;
            if (clip < 0) clip = ma.FindAnim("idle");
            if (ma.animCount > 0 && clip >= 0 && anim::Sample(ma, clip, (float)t, !once, pose)) anim::Apply(ma, pose);
            float scale = st["speaker"]["scale"].is_number() ? st["speaker"]["scale"].get<float>() : 1.0f;
            Matrix xf = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateY((st["speaker"]["yaw"].get<float>() + 90.0f) * DEG2RAD)),
                                       MatrixTranslate(0, 0, 0));
            gRenderer.DrawModelLit(ma, xf, {0.75f, 0.75f, 0.75f}, {0.5f, 0.5f, 0.45f}, Norm({0.3f, 0.8f, 0.4f}));
        }
    }
    // the player is only a marker: the shot is made for where it stands
    Vector3 pp = V3(st["player"]["pos"]);
    DrawCylinder(pp, 11, 11, 46, 12, Color{90, 110, 150, 255});
    DrawSphere(pp + Vector3{0, 54, 0}, 9, Color{120, 140, 180, 255});
    EndMode3D();
    EndTextureMode();
    return st;
}

// the preview panel: the rendered shot, the dialogue box on top (drawn by the same script code as
// in the game), and the controls
static void DrawDialoguePanel(Editor::Impl& ed, Document& d, Rectangle panel, const json& st) {
    Fill(panel, Color{27, 31, 40, 245});
    Outline(panel, theme.border);
    Text("PREVIEW", panel.x + 10, panel.y + 8, theme.textDim, 12, "ui_bold");
    float bx = panel.x + panel.width - 8;
    auto right = [&](const char* id, const char* label, float w, bool sel = false) {
        bx -= w;
        bool hit = Button(id, {bx, panel.y + 4, w, 20}, label, sel);
        bx -= 4;
        return hit;
    };
    if (right("pmax", "Max", 42, d.previewMax)) d.previewMax = !d.previewMax;
    bool replay = right("preplay", "Replay", 58);
    bool next = right("pnext", "Next >", 56);
    if (replay) d.previewStart = GetTime();

    Rectangle img{panel.x + 8, panel.y + 30, panel.width - 16, (panel.width - 16) * 9.0f / 16.0f};
    if (!st.is_object() || !st.contains("camera") || !d.rt.id) {
        Fill(img, Color{20, 23, 30, 255});
        TextCentered(st.is_object() && st.value("empty", false) ? "This dialogue has no Say / Choice node yet" : "No preview", img, theme.textDim, 13);
        return;
    }
    DrawTexturePro(d.rt.texture, {0, 0, (float)d.rt.texture.width, -(float)d.rt.texture.height}, img, {0, 0}, 0, WHITE);
    PushClip(img);
    json res = ed.script.CallJson("flow_draw_dialogue", {{"ui", st["ui"]}, {"style", st["style"]}, {"cinematic", st.value("cinematic", false)},
                                                         {"rect", json::array({img.x, img.y, img.width, img.height})}});
    PopClip();
    Outline(img, theme.border);

    // the answers can be clicked to follow them
    int cur = IndexOf(d, d.previewNode);
    if (cur >= 0 && res.is_object() && res.contains("rects") && res["rects"].is_array() && st["ui"].contains("options")) {
        const json& opts = st["ui"]["options"];
        for (size_t i = 0; i < res["rects"].size() && i < opts.size(); ++i) {
            const json& r = res["rects"][i];
            if (!r.is_object()) continue;
            Rectangle rr{r["x"].get<float>(), r["y"].get<float>(), r["w"].get<float>(), r["h"].get<float>()};
            if (Clicked(rr)) {
                int to = FindNextLine(ed, d, cur, "o" + std::to_string(opts[i].value("index", (int)i + 1)));
                if (to >= 0) d.view.selNode = to;
            }
        }
    }
    if (next && cur >= 0) {
        std::vector<std::string> ports = PortNames(ed, Nodes(d)[cur]);
        int to = ports.empty() ? -1 : FindNextLine(ed, d, cur, ports[0]);
        if (to >= 0) d.view.selNode = to;
        else ed.SetStatus("That was the last line on this path");
    }
    float y = img.y + img.height + 6;
    std::string info = "shot: " + st.value("shot", std::string("-")) + (st.value("cinematic", false) ? "  /  " + st.value("motion", std::string("none")) : "");
    Text(info, panel.x + 10, y, theme.text, 12);
    Text("answers can be clicked; Next follows the first path", panel.x + 10, y + 16, theme.textDim, 11);
}

// ---------------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------------
static float ParamRow(Editor::Impl& ed, Document& d, const Fields& f, float y, json& node, const json& pd) {
    std::string name = JStr(pd, "name"), type = JStr(pd, "type", "string");
    const char* help = pd.contains("help") && pd["help"].is_string() ? pd["help"].get_ref<const std::string&>().c_str() : nullptr;
    f.Label(name, y, help);
    json cur = node.contains("params") && node["params"].is_object() && node["params"].contains(name) ? node["params"][name] : (pd.contains("default") ? pd["default"] : json());
    auto store = [&](const json& v) {
        if (!node.contains("params") || !node["params"].is_object()) node["params"] = json::object();
        node["params"][name] = v;
        Touch(d);
    };
    Rectangle fr = f.Field(y);
    PushId("pr_" + name);
    auto text = [&]() { return cur.is_string() ? cur.get<std::string>() : (cur.is_null() ? "" : cur.dump()); };
    auto assetDrop = [&](const std::string& dragType, std::string& value) {
        if (Hovered(fr) && DragReleased() && DragType() == dragType) {
            value = DragPayload();
            return true;
        }
        return false;
    };
    if (type == "number") {
        float v = cur.is_number() ? cur.get<float>() : 0;
        if (FloatField("n", fr, v, fabsf(v) > 50 ? 1.0f : 0.05f, "%g")) store(v);
    } else if (type == "bool") {
        bool b = cur.is_boolean() ? cur.get<bool>() : false;
        if (Checkbox("b", fr, b, "")) store(b);
    } else if (type == "enum") {
        std::vector<std::string> opts;
        if (pd.contains("values") && pd["values"].is_array())
            for (auto& o : pd["values"]) opts.push_back(o.is_string() ? o.get<std::string>() : o.dump());
        std::string s = text();
        if (Dropdown("e", fr, s, opts)) store(s);
    } else if (type == "class" || type == "weapon" || type == "map" || type == "sound" || type == "quest" || type == "dialogue" || type == "cutscene") {
        std::vector<std::string> opts = type == "class" ? ed.classNames : type == "weapon" ? ed.weaponIds : type == "map" ? ed.mapList : type == "sound" ? ed.soundList : type == "quest" ? ed.questList : type == "cutscene" ? ed.cutsceneList : ed.dialogueList;
        std::string s = text();
        bool changed = OptionalDropdown("a", fr, s, opts);
        if (type == "quest" || type == "dialogue" || type == "cutscene") changed = assetDrop(type, s) || changed;
        if (changed) store(s);
    } else if (type == "entity") {
        // "player", a target name of an entity in the open map, or a point "x y z"
        std::string s = text();
        if (TextField("en", {fr.x, fr.y, fr.width - 24, fr.height}, s)) store(s);
        std::string pick = "v";
        std::vector<std::string> names{"player"};
        for (auto& e : ed.map.entities) {
            std::string tn = e.Str("targetname");
            if (!tn.empty() && std::find(names.begin(), names.end(), tn) == names.end()) names.push_back(tn);
        }
        if (Dropdown("enp", {fr.x + fr.width - 22, fr.y, 22, fr.height}, pick, names)) store(pick);
    } else if (type == "speaker") {
        // a speaker id (from this dialogue's Speakers), or any name typed in
        std::string s = text();
        if (TextField("sp", {fr.x, fr.y, fr.width - 24, fr.height}, s)) store(s);
        std::string pick = "v";
        std::vector<std::string> ids;
        for (auto& sp : Arr(d.def, "speakers")) ids.push_back(JStr(sp, "id"));
        if (!ids.empty() && Dropdown("spp", {fr.x + fr.width - 22, fr.y, 22, fr.height}, pick, ids)) store(pick);
    } else if (type == "var") {
        std::string s = text();
        if (TextField("v", {fr.x, fr.y, fr.width - 24, fr.height}, s)) store(s);
        std::string pick = "v";
        if (Dropdown("vp", {fr.x + fr.width - 22, fr.y, 22, fr.height}, pick, VarNames(ed, d))) store(pick);
    } else {  // string, text
        std::string s = text();
        if (TextField("s", fr, s)) store(s);
    }
    PopId();
    return y + kRow + 2;
}

static float InspectNode(Editor::Impl& ed, Document& d, const Fields& f, float y, int idx) {
    Rectangle r = f.r;
    json& node = Nodes(d)[idx];
    std::string type = JStr(node, "type");
    std::string cat = CategoryOf(ed, type);
    const json* meta = LibNode(ed, type);
    Text("Node", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    Text(Capital(cat), r.x + r.width - 90, y + 2, CategoryColor(cat, type), 12, "ui_bold");
    y += kRow - 2;

    f.Label("Name", y, "shown on the node (optional)");
    std::string label = JStr(node, "label");
    if (TextField("nlabel", f.Field(y), label, meta ? JStr(*meta, "title").c_str() : "")) {
        if (label.empty()) node.erase("label");
        else node["label"] = label;
        Touch(d);
    }
    y += kRow + 2;

    f.Label("Type", y);
    std::vector<std::string> titles;
    std::map<std::string, std::string> idOf;
    if (ed.flowLibrary.is_object() && ed.flowLibrary.contains("nodes"))
        for (auto& n : ed.flowLibrary["nodes"]) {
            if (JStr(n, "category") != cat || !InScope(n, d.kind)) continue;
            titles.push_back(JStr(n, "title", JStr(n, "id")));
            idOf[titles.back()] = JStr(n, "id");
        }
    std::string cur = meta ? JStr(*meta, "title", type) : type;
    if (titles.empty() || type == "start") {
        Text(Ellipsize(cur, f.FieldW(), 13), f.FieldX(), y + 4, meta ? theme.text : Color{240, 100, 90, 255}, 13);
    } else if (Dropdown("ntype", f.Field(y), cur, titles) && idOf.count(cur) && idOf[cur] != type) {
        SetType(ed, d, idx, idOf[cur]);
    }
    y += kRow + 4;

    if (meta && !JStr(*meta, "desc").empty()) y = WrapText(JStr(*meta, "desc"), r.x + 12, y, r.width - 24, theme.textDim) + 4;

    if (meta && meta->contains("params") && (*meta)["params"].is_array() && !(*meta)["params"].empty()) {
        Text("Parameters", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        y += kRow - 2;
        for (auto& pd : (*meta)["params"]) y = ParamRow(ed, d, f, y, node, pd);
        y += 4;
    }

    if (meta && (*meta)["ports"].is_string()) {
        // the answers of a choice: each is a port of its own
        Text("Answers", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        if (Hovered({r.x + 10, y, 100, kRow - 2})) Tooltip("answer text, and optionally a variable that must be true for the answer to be offered");
        y += kRow - 2;
        json& opts = Arr(node, "options");
        int removeAt = -1;
        for (size_t i = 0; i < opts.size(); ++i) {
            PushId("o" + std::to_string(i));
            float x = r.x + 10, w = r.width - 20;
            float rw = 78, xw = 20;
            std::string text = JStr(opts[i], "text");
            if (TextField("otext", {x, y, w - rw - xw - 8, kRow - 2}, text)) {
                opts[i]["text"] = text;
                Touch(d);
            }
            std::string req = JStr(opts[i], "require");
            if (TextField("oreq", {x + w - rw - xw - 4, y, rw, kRow - 2}, req, "needs var")) {
                if (req.empty()) opts[i].erase("require");
                else opts[i]["require"] = req;
                Touch(d);
            }
            if (Button("odel", {x + w - xw, y, xw, kRow - 2}, "x") && opts.size() > 1) removeAt = (int)i;
            PopId();
            y += kRow;
        }
        if (removeAt >= 0) RemoveOption(d, idx, removeAt);
        if (Button("oadd", {r.x + 10, y, r.width - 20, kRow - 2}, "+ Answer")) {
            opts.push_back({{"text", "Answer " + std::to_string(opts.size() + 1)}});
            Touch(d);
        }
        y += kRow + 4;
    }
    y += 6;
    if (type == "cs_camera" || type == "cs_camera_move") {
        if (Button("nlook", {r.x + 10, y, r.width - 20, kRow}, "Look through this camera")) ed.LookThroughCamera(d, idx);
        if (Hovered({r.x + 10, y, r.width - 20, kRow})) Tooltip("Puts the 3D view where this shot films from (the end of the move)");
        y += kRow + 4;
    }
    {
        if (Button("ndel", {r.x + 10, y, r.width - 20, kRow}, "Delete node")) DeleteNode(d, idx);
    }
    return y + kRow + 8;
}

// ---------------------------------------------------------------------------------
// dialogue settings: the look of the box and the speakers
// ---------------------------------------------------------------------------------
static std::vector<std::string> StringList(const json& j, bool dropAuto) {
    std::vector<std::string> out;
    if (j.is_array())
        for (auto& v : j)
            if (v.is_string() && !(dropAuto && v.get<std::string>() == "auto")) out.push_back(v.get<std::string>());
    return out;
}

static float InspectStyle(Editor::Impl& ed, Document& d, const Fields& f, float y) {
    Rectangle r = f.r;
    json& style = d.def["style"];
    if (!style.is_object()) style = json::object();
    const json defs = ed.dialogueDefaults.is_object() && ed.dialogueDefaults.contains("style") ? ed.dialogueDefaults["style"] : json::object();
    struct Spec {
        const char* key;
        const char* help;
    };
    static const Spec specs[] = {
        {"position", "where the box sits: bottom, top or center"},
        {"width", "box width in percent of the screen"},
        {"font_size", "text size (scales with the screen height)"},
        {"text_speed", "characters per second; 0 shows the whole line at once"},
        {"blip_volume", "volume of the speaker's voice blip while the text types"},
        {"box_color", "colour of the box (red, green, blue)"},
        {"box_alpha", "box opacity 0-255"},
        {"text_color", "colour of the text"},
        {"accent", "colour of the speaker name (a speaker's own colour wins)"},
        {"portrait_side", "which side of the box the speaker's portrait is on"},
        {"portrait_size", "portrait size in pixels at 900p"},
        {"letterbox", "black bars top and bottom while a camera shot is on"},
        {"camera", "use camera shots (off = the player keeps looking where they were)"},
        {"default_shot", "shot for lines that say auto"},
        {"default_motion", "slow camera movement for lines that say auto"},
        {"face_player", "the speaking character turns to the player"},
        {"hide_hud", "hide health and ammo while a shot is on"},
    };
    for (const Spec& sp : specs) {
        if (!defs.contains(sp.key)) continue;
        const json& def = defs[sp.key];
        json cur = style.contains(sp.key) ? style[sp.key] : def;
        std::string label = sp.key;
        std::replace(label.begin(), label.end(), '_', ' ');
        f.Label(label, y, sp.help);
        Rectangle fr = f.Field(y);
        PushId(std::string("st_") + sp.key);
        auto store = [&](const json& v) {
            style[sp.key] = v;
            Touch(d);
        };
        std::string key = sp.key;
        if (def.is_boolean()) {
            bool b = cur.is_boolean() ? cur.get<bool>() : false;
            if (Checkbox("b", fr, b, "")) store(b);
        } else if (def.is_number()) {
            float v = cur.is_number() ? cur.get<float>() : 0;
            if (FloatField("n", fr, v, key == "blip_volume" ? 0.02f : 1.0f, "%g")) store(v);
        } else if (def.is_string()) {
            std::vector<std::string> opts = key == "position" ? std::vector<std::string>{"bottom", "top", "center"}
                                            : key == "portrait_side" ? std::vector<std::string>{"left", "right"}
                                            : key == "default_shot" ? StringList(ed.dialogueDefaults.value("shots", json::array()), true)
                                                                    : StringList(ed.dialogueDefaults.value("motions", json::array()), true);
            std::string s = cur.is_string() ? cur.get<std::string>() : "";
            if (Dropdown("e", fr, s, opts)) store(s);
        } else if (def.is_array() && def.size() >= 3) {
            float cw = (fr.width - 28) / 3.0f;
            json nv = cur;
            bool changed = false;
            for (int i = 0; i < 3; ++i) {
                float v = cur.is_array() && cur.size() > (size_t)i && cur[i].is_number() ? cur[i].get<float>() : 0;
                if (FloatField("c" + std::to_string(i), {fr.x + i * (cw + 2), fr.y, cw, fr.height}, v, 1.0f, "%g")) {
                    nv[i] = (int)roundf(Clampf(v, 0, 255));
                    changed = true;
                }
            }
            ColorSwatch("sw", {fr.x + fr.width - 24, fr.y, 24, fr.height},
                        Color{(unsigned char)(cur.is_array() ? cur[0].get<int>() : 0), (unsigned char)(cur.is_array() ? cur[1].get<int>() : 0),
                              (unsigned char)(cur.is_array() ? cur[2].get<int>() : 0), 255});
            if (changed) store(nv);
        }
        PopId();
        y += kRow + 2;
    }
    if (Button("streset", {r.x + 10, y, r.width - 20, kRow}, "Reset style to the defaults", false, !style.empty())) {
        style = json::object();
        Touch(d);
    }
    return y + kRow + 8;
}

static float InspectSpeakers(Editor::Impl& ed, Document& d, const Fields& f, float y) {
    Rectangle r = f.r;
    json& speakers = Arr(d.def, "speakers");
    std::vector<std::string> targetnames;
    for (auto& e : ed.map.entities) {
        std::string tn = e.Str("targetname", "");
        if (!tn.empty() && std::find(targetnames.begin(), targetnames.end(), tn) == targetnames.end()) targetnames.push_back(tn);
    }
    int removeAt = -1;
    for (size_t i = 0; i < speakers.size(); ++i) {
        json& sp = speakers[i];
        PushId("spk" + std::to_string(i));
        std::string key = "spk_" + std::to_string(i);
        if (!ed.open.count(key)) ed.open[key] = i == 0;
        bool& isOpen = ed.open[key];
        Header("h", {r.x + 8, y, r.width - 16, kRow}, JStr(sp, "name", JStr(sp, "id", "speaker")), isOpen);
        y += kRow + 2;
        if (isOpen) {
            auto touch = [&]() { Touch(d); };
            f.Label("Id", y, "what Say / Choice nodes call this speaker");
            std::string id = JStr(sp, "id");
            if (TextField("id", f.Field(y), id) && !id.empty() && id != JStr(sp, "id")) {
                std::string old = JStr(sp, "id");
                for (auto& n : Nodes(d))
                    if (n.contains("params") && n["params"].is_object() && n["params"].contains("speaker") && n["params"]["speaker"] == old) n["params"]["speaker"] = id;
                sp["id"] = id;
                touch();
            }
            y += kRow + 2;
            f.Label("Name", y, "shown above the text");
            std::string name = JStr(sp, "name");
            if (TextField("name", f.Field(y), name)) {
                sp["name"] = name;
                touch();
            }
            y += kRow + 2;
            f.Label("Entity", y, "target name of the character in the map (an npc): the camera films it, it turns to the player. Empty = the npc that started the dialogue; player = the player");
            {
                Rectangle fr = f.Field(y);
                std::string en = JStr(sp, "entity");
                if (TextField("ent", {fr.x, fr.y, fr.width - 24, fr.height}, en)) {
                    if (en.empty()) sp.erase("entity");
                    else sp["entity"] = en;
                    touch();
                }
                std::string pick = "v";
                std::vector<std::string> opts = targetnames;
                opts.push_back("player");
                if (Dropdown("entp", {fr.x + fr.width - 22, fr.y, 22, fr.height}, pick, opts)) {
                    sp["entity"] = pick;
                    touch();
                }
            }
            y += kRow + 2;
            f.Label("Model", y, "model shown in the editor preview (empty = the entity's own model)");
            std::string model = JStr(sp, "model");
            if (OptionalDropdown("model", f.Field(y), model, ed.modelList)) {
                if (model.empty()) sp.erase("model");
                else sp["model"] = model;
                touch();
            }
            y += kRow + 2;
            f.Label("Portrait", y, "face picture next to the text (sprites/...)");
            std::string portrait = JStr(sp, "portrait");
            if (OptionalDropdown("portrait", f.Field(y), portrait, ed.spriteList)) {
                if (portrait.empty()) sp.erase("portrait");
                else sp["portrait"] = portrait;
                touch();
            }
            y += kRow + 2;
            f.Label("Colour", y, "colour of the name");
            {
                Rectangle fr = f.Field(y);
                float cw = (fr.width - 28) / 3.0f;
                json col = sp.contains("color") && sp["color"].is_array() && sp["color"].size() >= 3 ? sp["color"] : json::array({255, 214, 120});
                bool changed = false;
                for (int k = 0; k < 3; ++k) {
                    float v = col[k].is_number() ? col[k].get<float>() : 0;
                    if (FloatField("c" + std::to_string(k), {fr.x + k * (cw + 2), fr.y, cw, fr.height}, v, 1.0f, "%g")) {
                        col[k] = (int)roundf(Clampf(v, 0, 255));
                        changed = true;
                    }
                }
                ColorSwatch("sw", {fr.x + fr.width - 24, fr.y, 24, fr.height}, Color{(unsigned char)col[0].get<int>(), (unsigned char)col[1].get<int>(), (unsigned char)col[2].get<int>(), 255});
                if (changed) {
                    sp["color"] = col;
                    touch();
                }
            }
            y += kRow + 2;
            f.Label("Voice", y, "sound played as blips while the text types");
            std::string voice = JStr(sp, "voice");
            if (OptionalDropdown("voice", f.Field(y), voice, ed.soundList)) {
                if (voice.empty()) sp.erase("voice");
                else sp["voice"] = voice;
                touch();
            }
            y += kRow + 2;
            f.Label("Pitch", y, "voice pitch (1 = as recorded, higher = squeakier)");
            float pitch = JNum(sp, "pitch", 1);
            if (FloatField("pitch", f.Field(y), pitch, 0.02f, "%.2f")) {
                sp["pitch"] = pitch;
                touch();
            }
            y += kRow + 2;
            f.Label("Head", y, "height of the face above the feet, for the camera (0 = automatic)");
            float head = JNum(sp, "head", 0);
            if (FloatField("head", f.Field(y), head, 0.5f, "%g")) {
                if (head <= 0) sp.erase("head");
                else sp["head"] = head;
                touch();
            }
            y += kRow + 2;
            // emotions: other portraits, picked by a line's `emotion`
            Text("Emotions", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
            if (Hovered({r.x + 10, y, 100, kRow - 2})) Tooltip("more portraits; a Say node's emotion picks one by name");
            y += kRow - 2;
            json& em = sp["emotions"];
            if (!em.is_object()) em = json::object();
            std::string renameFrom, renameTo, removeKey;
            int ei = 0;
            for (auto it = em.begin(); it != em.end(); ++it, ++ei) {
                PushId("e" + std::to_string(ei));
                std::string en = it.key();
                std::string img = it.value().is_string() ? it.value().get<std::string>() : "";
                if (TextField("en", {r.x + 10, y, 90, kRow - 2}, en) && !en.empty() && en != it.key()) renameFrom = it.key(), renameTo = en;
                if (OptionalDropdown("ei", {r.x + 104, y, r.width - 104 - 34, kRow - 2}, img, ed.spriteList)) {
                    it.value() = img;
                    touch();
                }
                if (Button("ed", {r.x + r.width - 30, y, 20, kRow - 2}, "x")) removeKey = it.key();
                PopId();
                y += kRow;
            }
            if (!renameFrom.empty() && !em.contains(renameTo)) {
                em[renameTo] = em[renameFrom];
                em.erase(renameFrom);
                touch();
            }
            if (!removeKey.empty()) {
                em.erase(removeKey);
                touch();
            }
            if (Button("eadd", {r.x + 10, y, r.width - 20, kRow - 2}, "+ Emotion")) {
                em[em.contains("happy") ? "emotion" + std::to_string(em.size() + 1) : "happy"] = "";
                touch();
            }
            y += kRow + 2;
            if (Button("sdel", {r.x + 10, y, r.width - 20, kRow - 2}, "Delete speaker")) removeAt = (int)i;
            y += kRow + 6;
        }
        PopId();
    }
    if (removeAt >= 0) {
        speakers.erase(speakers.begin() + removeAt);
        Touch(d);
    }
    if (Button("spadd", {r.x + 10, y, r.width - 20, kRow}, "+ Speaker")) {
        std::vector<std::string> ids;
        for (auto& s : speakers) ids.push_back(JStr(s, "id"));
        std::string id = UniqueName(ids, speakers.empty() ? "Guard" : "Speaker");
        speakers.push_back({{"id", id}, {"name", id}});
        Touch(d);
    }
    return y + kRow + 8;
}

float Editor::Impl::InspectFlow(Rectangle r, float y) {
    Document* dp = Doc();
    if (!dp) return y;
    Document& d = *dp;
    Fields f{r};
    std::string kindName = d.kind == "quest" ? "Quest" : d.kind == "cutscene" ? "Cutscene" : "Dialogue";
    Text(kindName + ": " + d.name, r.x + 12, y + 4, theme.text, 15, "ui_bold");
    y += kRow + 4;
    Text(Ellipsize(d.path, r.width - 24, 12), r.x + 12, y, theme.textDim, 12);
    y += 18;
    float bw = (r.width - 26) / 2;
    if (Button("dsave", {r.x + 10, y, bw, kRow + 2}, d.dirty ? "Save *" : "Save", d.dirty, d.dirty)) SaveDocument(d);
    if (Button("drevert", {r.x + 16 + bw, y, bw, kRow + 2}, "Revert", false, d.dirty)) ReloadDocument(d);
    y += kRow + 10;
    y = SelectionNote(d, r, y);

    int n = (int)Nodes(d).size();
    if (d.view.selLink >= 0) {
        int s, p, t;
        DecodeLink(d.view.selLink, s, p, t);
        if (s < n && t < n) {
            std::vector<std::string> ports = PortNames(*this, Nodes(d)[s]);
            Text("Link", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
            y += kRow - 2;
            std::string port = p < (int)ports.size() ? PortLabel(Nodes(d)[s], ports[p], ports.size()) : "";
            Text(JStr(Nodes(d)[s], "label", JStr(Nodes(d)[s], "type")) + (port.empty() ? "" : " [" + port + "]") + "  ->  " + JStr(Nodes(d)[t], "label", JStr(Nodes(d)[t], "type")), r.x + 12,
                 y + 2, theme.text, 13, "ui_bold");
            y += kRow + 8;
            if (Button("ldel", {r.x + 10, y, r.width - 20, kRow}, "Delete link") && p < (int)ports.size()) RemoveLink(d, s, ports[p], t);
            return y + kRow + 8;
        }
    }
    if (d.view.selNode >= 0 && d.view.selNode < n) return InspectNode(*this, d, f, y, d.view.selNode);

    Text(kindName, r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    f.Label("Title", y, d.kind == "quest" ? "shown in the quest list on the screen" : d.kind == "cutscene" ? "name of the cutscene" : "name of the dialogue");
    std::string title = JStr(d.def, "title");
    if (TextField("gtitle", f.Field(y), title)) {
        d.def["title"] = title;
        Touch(d);
    }
    y += kRow + 6;
    Text(TextFormat("%d nodes, %d variables", n, (int)Arr(d.def, "vars").size()), r.x + 12, y, theme.textDim, 12);
    y += 22;
    if (d.kind == "dialogue") {
        // how the dialogue looks and who speaks (the preview shows the result)
        bool& spOpen = open["dlg_speakers"];
        bool& stOpen = open["dlg_style"];
        if (!open.count("dlg_speakers_init")) {
            open["dlg_speakers_init"] = true;
            spOpen = true;
        }
        Header("dsp", {r.x + 8, y, r.width - 16, kRow}, "Speakers (" + std::to_string(Arr(d.def, "speakers").size()) + ")", spOpen);
        y += kRow + 4;
        if (spOpen) y = InspectSpeakers(*this, d, f, y);
        Header("dst", {r.x + 8, y, r.width - 16, kRow}, "Style of the dialogue box", stOpen);
        y += kRow + 4;
        if (stOpen) y = InspectStyle(*this, d, f, y);
    }
    if (d.kind == "cutscene") {
        bool& stOpen = open["cut_style"];
        if (!open.count("cut_style_init")) {
            open["cut_style_init"] = true;
            stOpen = true;
        }
        Header("cst", {r.x + 8, y, r.width - 16, kRow}, "How the cutscene plays", stOpen);
        y += kRow + 4;
        if (stOpen) {
            struct Opt { const char* key; const char* label; const char* help; };
            static const Opt opts[] = {{"letterbox", "Black bars", "cinema bars at the top and bottom"},
                                       {"skippable", "Skippable", "pressing Space or Enter twice skips it"},
                                       {"hide_hud", "Hide HUD", "no health, ammo or objectives while it plays"},
                                       {"invulnerable", "Invulnerable", "the player cannot be hurt during the scene"}};
            json& style = d.def["style"];
            if (!style.is_object()) style = json::object();
            for (auto& o : opts) {
                bool def = JBool(cutsceneDefaults.value("style", json::object()), o.key, true);
                bool b = JBool(style, o.key, def);
                f.Label(o.label, y, o.help);
                PushId(o.key);
                if (Checkbox("c", f.Field(y), b, "")) {
                    style[o.key] = b;
                    Touch(d);
                }
                PopId();
                y += kRow;
            }
            y += 6;
        }
    }
    std::vector<std::string> issues = Issues(*this, d);
    if (issues.empty()) {
        Text("No problems found.", r.x + 12, y, Color{120, 220, 140, 255}, 12);
        y += 18;
    }
    for (auto& s : issues) y = WrapText(s, r.x + 12, y, r.width - 24, Color{240, 180, 60, 255}) + 2;
    y += 10;
    std::vector<std::string> tips = d.kind == "cutscene" ? std::vector<std::string>{"Right click the canvas to add nodes. Camera shot films a",
                                                                                     "character (target name, player or a point x y z), Camera",
                                                                                     "move flies to a point, Animate / Move character direct",
                                                                                     "actors, Fade and Title text dress it up.",
                                                                                     "Nodes on parallel ports run together; a Join waits.",
                                                                                     "Select a camera node and press 'Look through this camera'",
                                                                                     "to see its shot in the 3D view.",
                                                                                     "",
                                                                                     "Play it: a cutscene_start entity, the Cutscene node of a",
                                                                                     "quest / dialogue, or console: quest cutscene <name>."}
                                                    : d.kind == "quest" ? std::vector<std::string>{"Right click the canvas to add nodes. Drag from a dot on",
                                                                                 "the right edge of a node to the node that comes next.",
                                                                                 "A port can lead to several nodes at once (they all run;",
                                                                                 "a Join waits for them). Objectives wait for kills,",
                                                                                 "pickups or a target; Branch picks yes / no.",
                                                                                 "",
                                                                                 "Start it: a dialogue's Start quest node, a quest_start",
                                                                                 "entity (auto or triggered), or another quest.",
                                                                                 "See docs/QUESTS.md."}
                                                    : std::vector<std::string>{"Right click the canvas to add nodes. Say shows a line,",
                                                                               "Choice offers answers (each answer is a port).",
                                                                               "Branch checks a variable or a quest; Give, Message,",
                                                                               "Start quest... are actions.",
                                                                               "",
                                                                               "Use it: an npc entity's `dialogue` property, or a",
                                                                               "quest's Dialogue node. See docs/QUESTS.md."};
    for (auto& t : tips) {
        Text(t, r.x + 12, y, theme.textDim, 12);
        y += 17;
    }
    return y + 10;
}

// ---------------------------------------------------------------------------------
// scripted access (console command "flow") for tests and automation
// ---------------------------------------------------------------------------------
void Editor::OpenFlow(const std::string& kind, const std::string& name) {
    m->RefreshFlowLists();
    m->OpenDocument(kind, name);
}

std::string Editor::FlowCommand(const std::vector<std::string>& a) {
    if (a.size() < 2) return "flow open|create|add|link|unlink|set|option|label|select|look|delete|arrange|save|dump|issues|var|title|entity|close";
    const std::string& cmd = a[1];
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : std::string(); };
    auto isFlow = [&](Document* d) { return d && IsFlowKind(d->kind); };
    if (cmd == "open") {
        OpenFlow(arg(2), arg(3));
        return isFlow(m->Doc()) ? "opened " + m->Doc()->path : "not opened";
    }
    if (cmd == "create") {
        if (!m->CreateFlow(arg(2), arg(3))) return "create failed";
        return "created " + m->Doc()->path;
    }
    Document* d = m->Doc();
    if (!isFlow(d)) return "no quest, dialogue or cutscene open (flow open quest|dialogue|cutscene <name>)";
    {
        std::string r = m->DocCommand(*d, a);
        if (r != "\x01") return r;
    }
    auto node = [&](const std::string& id) { return IndexOf(*d, id); };
    if (cmd == "dump") return d->def.dump();
    if (cmd == "entity") {
        m->activeDoc = -1;
        m->ClearSelection();
        int i = atoi(arg(2).c_str());
        if (i >= 0 && i < (int)m->map.entities.size()) m->selEnts.insert(i);
        return i >= 0 && i < (int)m->map.entities.size() ? "selected " + m->map.entities[i].classname : "no such entity";
    }
    if (cmd == "close") {
        m->CloseDocument(m->activeDoc, true);
        return "closed";
    }
    if (cmd == "preview") {
        // preview max | normal | show | hide
        if (arg(2) == "max") d->previewMax = true;
        else if (arg(2) == "normal") d->previewMax = false;
        else if (arg(2) == "hide") m->open["flow_preview"] = false;
        else if (arg(2) == "show") m->open["flow_preview"] = true;
        return "";
    }
    if (cmd == "save") return m->SaveDocument(*d) ? "saved" : "save failed";
    if (cmd == "arrange") {
        Arrange(*m, *d);
        Touch(*d);
        d->fitted = false;
        return "";
    }
    if (cmd == "add") {
        if (!LibNode(*m, arg(2))) return "unknown node type: " + arg(2);
        int idx = AddNode(*m, *d, arg(2), {(float)atof(arg(3).c_str()), (float)atof(arg(4).c_str())});
        if (a.size() < 5) Arrange(*m, *d);
        return "node " + IdOf(*d, idx);
    }
    if (cmd == "link") {
        // link <from id> <port> <to id>
        int s = node(arg(2)), t = node(arg(4));
        if (s < 0 || t < 0) return "no such node";
        return AddLink(*m, *d, s, arg(3), t) ? "linked" : "refused";
    }
    if (cmd == "unlink") {
        int s = node(arg(2)), t = node(arg(4));
        if (s < 0 || t < 0) return "no such node";
        RemoveLink(*d, s, arg(3), t);
        return "";
    }
    if (cmd == "delete") {
        DeleteNode(*d, node(arg(2)));
        return "";
    }
    if (cmd == "select") {
        d->view.selLink = -1;
        d->selParam = -1;
        d->view.selNode = arg(2) == "none" ? -1 : node(arg(2));
        return "";
    }
    if (cmd == "look") {  // flow look <node>: the 3D view goes where that camera node films from
        int i = node(arg(2));
        if (i < 0) return "no such node";
        m->LookThroughCamera(*d, i);
        return "";
    }
    if (cmd == "label") {
        int i = node(arg(2));
        if (i < 0) return "no such node";
        Nodes(*d)[i]["label"] = arg(3);
        Touch(*d);
        return "";
    }
    if (cmd == "title") {
        d->def["title"] = arg(2);
        Touch(*d);
        return "";
    }
    if (cmd == "set") {
        int i = node(arg(2));
        if (i < 0) return "no such node";
        json v = json::parse(arg(4), nullptr, false);
        if (v.is_discarded() || v.is_object() || v.is_array()) v = arg(4);
        Nodes(*d)[i]["params"][arg(3)] = v;
        Touch(*d);
        return "";
    }
    if (cmd == "option") {
        // option <id> add <text> | option <id> remove <n> | option <id> require <n> <var>
        int i = node(arg(2));
        if (i < 0 || JStr(Nodes(*d)[i], "type") != "choice") return "not a choice node";
        json& opts = Arr(Nodes(*d)[i], "options");
        if (arg(3) == "add") opts.push_back({{"text", arg(4)}});
        else if (arg(3) == "remove") RemoveOption(*d, i, atoi(arg(4).c_str()) - 1);
        else if (arg(3) == "require") opts[atoi(arg(4).c_str()) - 1]["require"] = arg(5);
        Touch(*d);
        return "";
    }
    if (cmd == "var") {
        json& vars = Arr(d->def, "vars");
        for (auto& v : vars)
            if (JStr(v, "name") == arg(2)) {
                v["value"] = arg(3);
                Touch(*d);
                return "";
            }
        vars.push_back({{"name", arg(2)}, {"value", arg(3)}});
        Touch(*d);
        return "";
    }
    if (cmd == "type") {
        int i = node(arg(2));
        if (i < 0 || !LibNode(*m, arg(3))) return "no such node or type";
        SetType(*m, *d, i, arg(3));
        return "";
    }
    if (cmd == "issues") {
        std::string out;
        for (auto& s : Issues(*m, *d)) out += s + " | ";
        return out.empty() ? "none" : out;
    }
    return "unknown command";
}

// ---------------------------------------------------------------------------------
// selection: copy, paste, delete
// ---------------------------------------------------------------------------------
static std::vector<int> SelectedNodes(Document& d) {
    std::vector<int> ids;
    for (int id : d.view.selNodes)
        if (id >= 0 && id < (int)Nodes(d).size()) ids.push_back(id);
    return ids;  // ascending
}

json Editor::Impl::CopyFlowSelection(Document& d) {
    json out = {{"kind", d.kind}, {"nodes", json::array()}};
    std::set<std::string> ids;
    for (int i : SelectedNodes(d)) ids.insert(IdOf(d, i));
    for (int i : SelectedNodes(d)) {
        json n = Nodes(d)[i];
        if (n.contains("out") && n["out"].is_object()) {
            for (auto it = n["out"].begin(); it != n["out"].end(); ++it) {
                json kept = json::array();
                for (auto& t : it.value())
                    if (t.is_string() && ids.count(t.get<std::string>())) kept.push_back(t);  // links to nodes left behind are dropped
                it.value() = kept;
            }
        }
        Vector2 p = NodePos(d, JStr(n, "id"), {0, 0});
        out["nodes"].push_back({{"def", n}, {"pos", json::array({p.x, p.y})}});
    }
    return out;
}

void Editor::Impl::PasteFlow(Document& d, const json& clip) {
    if (!clip.is_object() || JStr(clip, "kind") != d.kind || !clip.contains("nodes")) return;
    std::map<std::string, std::string> renamed;
    std::set<int> added;
    float off = 40.0f * (d.pasteN + 1);
    std::vector<std::string> newIds;
    for (auto& e : clip["nodes"]) {
        json n = e["def"];
        std::string old = JStr(n, "id"), nid = NewId(d);
        renamed[old] = nid;
        n["id"] = nid;
        Nodes(d).push_back(n);
        Vector2 p{0, 0};
        ReadPos(e["pos"], p);
        SetNodePos(d, nid, {p.x + off, p.y + off});
        added.insert((int)Nodes(d).size() - 1);
        newIds.push_back(nid);
    }
    for (auto& nid : newIds) {
        json& n = Nodes(d)[IndexOf(d, nid)];
        if (!n.contains("out") || !n["out"].is_object()) continue;
        for (auto it = n["out"].begin(); it != n["out"].end(); ++it) {
            json kids = json::array();
            for (auto& t : it.value())
                if (t.is_string() && renamed.count(t.get<std::string>())) kids.push_back(renamed[t.get<std::string>()]);
            it.value() = kids;
        }
    }
    if (added.empty()) return;
    d.view.selNodes = added;
    d.view.selNode = *added.rbegin();
    d.view.lastSelNode = d.view.selNode;
    d.view.selLink = -1;
    Touch(d);
}

void Editor::Impl::DeleteFlowSelection(Document& d) {
    std::vector<int> ids = SelectedNodes(d);
    for (size_t i = ids.size(); i-- > 0;) DeleteNode(d, ids[i]);
    d.view.selNode = d.view.selLink = -1;
    d.view.selNodes.clear();
    d.view.lastSelNode = -1;
}
}  // namespace rbs
