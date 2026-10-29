// RBS Engine - editor: the behavior tree document (graph tab). Nodes are the tree's nodes,
// links go from a parent to its children (a composite runs them top to bottom, by their
// vertical position), the Blackboard panel lists the tree's own values and the Inspector
// edits the selected node with the parameters the node type declares. The node types come
// from Lua (rbs.bt_library in scripts/behavior.lua), so nodes added by a project's scripts
// show up too. The file is behaviors/<name>.json, read by the same Lua runtime.
#include "core/log.h"
#include "core/vfs.h"
#include "editor/editor_doc.h"
#include "editor/editor_impl.h"
#include "ui/imgui.h"

#include <algorithm>
#include <filesystem>
#include <functional>

namespace rbs {

using namespace ui;
using namespace edoc;
namespace fs = std::filesystem;

static const int kRootId = 100000;  // node id of the virtual Root node (tree nodes use their index)

// ---------------------------------------------------------------------------------
// the node library (from Lua)
// ---------------------------------------------------------------------------------
static const json* LibNode(Editor::Impl& ed, const std::string& type) {
    if (!ed.btLibrary.is_object() || !ed.btLibrary.contains("nodes") || !ed.btLibrary["nodes"].is_array()) return nullptr;
    for (auto& n : ed.btLibrary["nodes"])
        if (JStr(n, "id") == type) return &n;
    return nullptr;
}

static std::string KindOf(Editor::Impl& ed, const std::string& type) {
    const json* m = LibNode(ed, type);
    return m ? JStr(*m, "kind", "action") : "unknown";
}

static bool TakesChildren(const std::string& kind) { return kind == "composite" || kind == "decorator"; }

static Color KindColor(const std::string& kind) {
    if (kind == "composite") return {62, 94, 166, 255};
    if (kind == "decorator") return {112, 76, 156, 255};
    if (kind == "condition") return {38, 122, 94, 255};
    if (kind == "action") return {170, 102, 46, 255};
    return {150, 50, 50, 255};
}

static std::string KindName(const std::string& kind) {
    if (kind.empty()) return kind;
    std::string s = kind;
    s[0] = (char)toupper(s[0]);
    return s;
}

static const char* kKinds[] = {"composite", "decorator", "condition", "action"};

// ---------------------------------------------------------------------------------
// the tree in JSON
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

static std::vector<int> ChildIndices(Document& d, int idx) {
    std::vector<int> out;
    if (idx < 0 || idx >= (int)Nodes(d).size()) return out;
    json& n = Nodes(d)[idx];
    if (!n.contains("children") || !n["children"].is_array()) return out;
    for (auto& c : n["children"]) {
        int ci = c.is_string() ? IndexOf(d, c.get<std::string>()) : -1;
        if (ci >= 0) out.push_back(ci);
    }
    return out;
}

// index of the parent node, kRootId when the Root node leads to it, -1 when nothing does
static int ParentOf(Document& d, int idx) {
    if (JStr(d.def, "root") == IdOf(d, idx) && idx >= 0) return kRootId;
    for (int i = 0; i < (int)Nodes(d).size(); ++i)
        for (int c : ChildIndices(d, i))
            if (c == idx) return i;
    return -1;
}

static bool Reaches(Document& d, int from, int target) {
    for (int c : ChildIndices(d, from))
        if (c == target || Reaches(d, c, target)) return true;
    return false;
}

static void Detach(Document& d, int child) {
    int parent = ParentOf(d, child);
    std::string cid = IdOf(d, child);
    if (parent == kRootId) {
        d.def["root"] = "";
    } else if (parent >= 0) {
        json& ch = Nodes(d)[parent]["children"];
        for (size_t i = ch.size(); i-- > 0;)
            if (ch[i] == cid) ch.erase(ch.begin() + i);
    }
}

// children of a composite run top to bottom: keep the array in that order
static void SortChildren(Editor::Impl& ed, Document& d) {
    for (size_t i = 0; i < Nodes(d).size(); ++i) {
        json& n = Nodes(d)[i];
        if (KindOf(ed, JStr(n, "type")) != "composite" || !n.contains("children") || !n["children"].is_array()) continue;
        std::vector<std::pair<std::pair<float, float>, std::string>> order;
        for (auto& c : n["children"]) {
            std::string id = c.is_string() ? c.get<std::string>() : "";
            Vector2 p = NodePos(d, id, {0, 0});
            order.push_back({{p.y, p.x}, id});
        }
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        json sorted = json::array();
        for (auto& o : order) sorted.push_back(o.second);
        n["children"] = sorted;
    }
}

static bool CanAttach(Editor::Impl& ed, Document& d, int parent, int child) {
    if (child < 0 || child >= (int)Nodes(d).size()) return false;
    if (parent == kRootId) return true;
    if (parent < 0 || parent >= (int)Nodes(d).size() || parent == child) return false;
    if (!TakesChildren(KindOf(ed, JStr(Nodes(d)[parent], "type")))) return false;
    return !Reaches(d, child, parent);  // a node cannot lead back to itself
}

static bool Attach(Editor::Impl& ed, Document& d, int parent, int child) {
    if (!CanAttach(ed, d, parent, child)) return false;
    Detach(d, child);
    std::string cid = IdOf(d, child);
    if (parent == kRootId) {
        d.def["root"] = cid;
    } else {
        json& n = Nodes(d)[parent];
        if (KindOf(ed, JStr(n, "type")) == "decorator") n["children"] = json::array({cid});  // one child: the old one is let go
        else Arr(n, "children").push_back(cid);
    }
    SortChildren(ed, d);
    Touch(d);
    return true;
}

static int AddNode(Editor::Impl& ed, Document& d, const std::string& type, Vector2 pos, int parent) {
    std::string id = NewId(d);
    json n = {{"id", id}, {"type", type}};
    if (TakesChildren(KindOf(ed, type))) n["children"] = json::array();
    Nodes(d).push_back(n);
    SetNodePos(d, id, pos);
    int idx = (int)Nodes(d).size() - 1;
    if (parent != -1) Attach(ed, d, parent, idx);
    if (JStr(d.def, "root").empty() && parent == -1 && Nodes(d).size() == 1) d.def["root"] = id;
    Touch(d);
    return idx;
}

static void DeleteNode(Document& d, int idx) {
    if (idx < 0 || idx >= (int)Nodes(d).size()) return;
    std::string id = IdOf(d, idx);
    Detach(d, idx);
    Nodes(d).erase(Nodes(d).begin() + idx);
    LayoutOf(d).erase(id);
    d.view.selNode = d.view.selLink = -1;
    Touch(d);
}

static void DeleteLink(Document& d, int childIdx) {
    Detach(d, childIdx);
    d.view.selLink = -1;
    Touch(d);
}

static void SetType(Editor::Impl& ed, Document& d, int idx, const std::string& type) {
    json& n = Nodes(d)[idx];
    n["type"] = type;
    n.erase("params");
    if (!TakesChildren(KindOf(ed, type))) {
        // a leaf cannot keep children: they are let go (they stay in the file, unattached)
        n.erase("children");
    } else if (!n.contains("children")) {
        n["children"] = json::array();
    }
    Touch(d);
}

// the tree laid out left to right: depth is the column, leaves are stacked
static void Arrange(Editor::Impl& ed, Document& d) {
    float leaf = 0;
    std::function<float(int, int)> place = [&](int idx, int depth) {
        std::vector<int> kids = ChildIndices(d, idx);
        float y;
        if (kids.empty()) {
            y = leaf * 76;
            leaf += 1;
        } else {
            float sum = 0;
            for (int k : kids) sum += place(k, depth + 1);
            y = sum / kids.size();
        }
        SetNodePos(d, IdOf(d, idx), {260.0f + depth * 240.0f, roundf(y)});
        return y;
    };
    int root = IndexOf(d, JStr(d.def, "root"));
    float rootY = 0;
    if (root >= 0) rootY = place(root, 0);
    SetNodePos(d, "root", {20, roundf(rootY)});
    // nodes nothing leads to go below
    for (int i = 0; i < (int)Nodes(d).size(); ++i) {
        if (ParentOf(d, i) != -1) continue;
        if (i == root) continue;
        place(i, 0);
    }
    SortChildren(ed, d);
}

static std::vector<std::string> Issues(Editor::Impl& ed, Document& d) {
    std::vector<std::string> out;
    int root = IndexOf(d, JStr(d.def, "root"));
    if (root < 0) out.push_back("No root: drag from the Root node's dot onto the first node.");
    int unattached = 0;
    for (int i = 0; i < (int)Nodes(d).size(); ++i) {
        std::string type = JStr(Nodes(d)[i], "type");
        std::string kind = KindOf(ed, type);
        std::string name = JStr(Nodes(d)[i], "label", type);
        if (kind == "unknown") out.push_back("'" + name + "': unknown node type '" + type + "' (it always fails).");
        if (TakesChildren(kind) && ChildIndices(d, i).empty()) out.push_back("'" + name + "' has no children.");
        if (kind == "decorator" && ChildIndices(d, i).size() > 1) out.push_back("'" + name + "' is a decorator: only its first child is used.");
        if (i != root && ParentOf(d, i) == -1) ++unattached;
    }
    if (unattached) out.push_back(std::to_string(unattached) + " node(s) are not connected to the tree (never run).");
    return out;
}

// ---------------------------------------------------------------------------------
// listing, creating
// ---------------------------------------------------------------------------------
void Editor::Impl::RefreshBehaviorList() { behaviorList = vfs::List("behaviors", ".json", true); }

std::vector<std::string> Editor::Impl::BehaviorOptions() const {
    std::vector<std::string> out{kNone};
    for (auto& b : behaviorList) out.push_back(b);
    return out;
}

void Editor::Impl::InitBehaviorDocument(Document& d) {
    Arr(d.def, "nodes");
    Arr(d.def, "blackboard");
    // nodes without a saved position (a hand-written file) get an arranged layout
    bool missing = !d.def.contains("editor");
    for (auto& n : Nodes(d))
        if (!LayoutOf(d).contains(JStr(n, "id"))) missing = true;
    if (!LayoutOf(d).contains("root")) missing = true;
    if (missing) Arrange(*this, d);
}

bool Editor::Impl::CreateBehavior(const std::string& name) {
    bool valid = !name.empty();
    for (char c : name)
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_')) valid = false;
    if (!valid) {
        SetStatus("Use lowercase letters, digits and _ for the name");
        return false;
    }
    std::string path = "behaviors/" + name + ".json";
    if (vfs::Exists(path)) {
        SetStatus("'" + path + "' already exists");
        return false;
    }
    // a small tree that already works: shoot when it can, otherwise walk up to the target
    json def = {
        {"root", "n1"},
        {"blackboard", json::array()},
        {"nodes",
         json::array({{{"id", "n1"}, {"type", "selector"}, {"label", "Behavior"}, {"children", json::array({"n2", "n5", "n8"})}},
                      {{"id", "n2"}, {"type", "sequence"}, {"label", "Attack"}, {"children", json::array({"n3", "n4"})}},
                      {{"id", "n3"}, {"type", "can_see"}},
                      {{"id", "n4"}, {"type", "attack"}},
                      {{"id", "n5"}, {"type", "sequence"}, {"label", "Chase"}, {"children", json::array({"n6", "n7"})}},
                      {{"id", "n6"}, {"type", "has_target"}},
                      {{"id", "n7"}, {"type", "move_to"}, {"params", {{"range", 96}}}},
                      {{"id", "n8"}, {"type", "idle"}}})},
    };
    if (!vfs::WriteText(path, def.dump(2) + "\n")) {
        SetStatus("Cannot write " + path);
        return false;
    }
    RefreshBehaviorList();
    OpenDocument("behavior", path);
    std::string where = vfs::CurrentMod().empty() ? "base/" : "mods/" + vfs::CurrentMod() + "/";
    SetStatus("Created " + where + path);
    return true;
}

// ---------------------------------------------------------------------------------
// graph building
// ---------------------------------------------------------------------------------
static std::string Summary(Editor::Impl& ed, const json& node) {
    const json* meta = LibNode(ed, JStr(node, "type"));
    if (!meta) return "unknown type";
    if (!node.contains("params") || !node["params"].is_object()) return "";
    std::string s;
    if (meta->contains("params") && (*meta)["params"].is_array()) {
        for (auto& pd : (*meta)["params"]) {
            std::string name = JStr(pd, "name");
            if (!node["params"].contains(name)) continue;
            const json& v = node["params"][name];
            std::string text = v.is_string() ? v.get<std::string>() : v.dump();
            if (!s.empty()) s += ", ";
            s += text;
            if (s.size() > 34) break;
        }
    }
    return s;
}

static void BuildGraph(Editor::Impl& ed, Document& d, std::vector<graph::Node>& nodes, std::vector<graph::Link>& links) {
    json& ns = Nodes(d);
    for (size_t i = 0; i < ns.size(); ++i) {
        std::string type = JStr(ns[i], "type");
        std::string kind = KindOf(ed, type);
        const json* meta = LibNode(ed, type);
        graph::Node n;
        n.id = (int)i;
        n.title = JStr(ns[i], "label", meta ? JStr(*meta, "title", type) : type);
        n.subtitle = Summary(ed, ns[i]);
        n.color = KindColor(kind);
        n.out = TakesChildren(kind);
        n.pos = NodePos(d, JStr(ns[i], "id"), {260, (float)i * 70});
        n.size = {176, 46};
        std::string order;
        int parent = ParentOf(d, (int)i);
        if (parent >= 0 && parent != kRootId) {
            std::vector<int> sib = ChildIndices(d, parent);
            for (size_t k = 0; k < sib.size(); ++k)
                if (sib[k] == (int)i) order = "  #" + std::to_string(k + 1);
        }
        n.badge = KindName(kind) + order;
        nodes.push_back(n);
    }
    graph::Node root;
    root.id = kRootId;
    root.title = "Root";
    root.color = {70, 86, 70, 255};
    root.in = false;
    root.pos = NodePos(d, "root", {20, 0});
    root.size = {110, 36};
    nodes.push_back(root);

    for (size_t i = 0; i < ns.size(); ++i) {
        for (int c : ChildIndices(d, (int)i)) {
            graph::Link l;
            l.id = c;  // a node has one parent, so its index names the link into it
            l.from = (int)i;
            l.to = c;
            links.push_back(l);
        }
    }
    int rootIdx = IndexOf(d, JStr(d.def, "root"));
    if (rootIdx >= 0) {
        graph::Link l;
        l.id = rootIdx;
        l.from = kRootId;
        l.to = rootIdx;
        l.color = {120, 190, 120, 255};
        links.push_back(l);
    }
}

static std::vector<std::string> KeyNames(Editor::Impl& ed, Document& d) {
    std::vector<std::string> out;
    auto add = [&](const std::string& s) {
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    };
    if (ed.btLibrary.is_object() && ed.btLibrary.contains("keys") && ed.btLibrary["keys"].is_array())
        for (auto& k : ed.btLibrary["keys"]) add(JStr(k, "name"));
    for (auto& k : Arr(d.def, "blackboard")) add(JStr(k, "name"));
    // global keys other nodes already use
    for (auto& n : Nodes(d))
        if (n.contains("params") && n["params"].is_object())
            for (auto it = n["params"].begin(); it != n["params"].end(); ++it)
                if (it.value().is_string() && it.value().get<std::string>().rfind("global.", 0) == 0) add(it.value().get<std::string>());
    return out;
}

// ---------------------------------------------------------------------------------
// the document
// ---------------------------------------------------------------------------------
void Editor::Impl::DrawBehaviorDoc(Rectangle r) {
    Document* dp = Doc();
    if (!dp) return;
    Document& d = *dp;
    Block(r);
    bool showBB = open.count("bt_blackboard") ? open["bt_blackboard"] : true;

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
    if (btn("bb", "Blackboard", 88, showBB)) open["bt_blackboard"] = !showBB;
    Text("right click: add node   -   drag from the dot to the child   -   children run top to bottom   -   drag on empty: select box   -   Space+drag: move view   -   Ctrl+Z / C / V", bx + 8, bar.y + 8, theme.textDim, 12);

    Rectangle canvas{r.x, r.y + 30, r.width, r.height - 30};
    size_t keyRows = Arr(d.def, "blackboard").size();
    size_t builtinRows = btLibrary.is_object() && btLibrary.contains("keys") ? btLibrary["keys"].size() : 0;
    Rectangle bbR{canvas.x + 8, canvas.y + 8, 250, std::min(canvas.height - 16, 64.0f + 26.0f * keyRows + 18.0f * builtinRows + 28)};
    d.view.exclude.clear();
    if (showBB) d.view.exclude.push_back(bbR);

    std::vector<graph::Node> nodes;
    std::vector<graph::Link> links;
    BuildGraph(*this, d, nodes, links);
    if (!d.fitted) {
        float left = showBB ? bbR.width + 16 : 0;
        Rectangle free{canvas.x + left, canvas.y, std::max(100.0f, canvas.width - left), canvas.height};
        graph::Fit(d.view, free, nodes);
        d.view.pan.x += left;
        d.fitted = true;
    }
    graph::Events ev = graph::Draw("bt_graph", canvas, d.view, nodes, links);

    if (ev.moved) {
        for (auto& n : nodes) {
            if (n.id == kRootId) SetNodePos(d, "root", n.pos);
            else if (n.id < (int)Nodes(d).size()) SetNodePos(d, IdOf(d, n.id), n.pos);
        }
        SortChildren(*this, d);
        d.dirty = true;
        d.stamp++;
    }
    if (ev.selectionChanged) d.selParam = -1;
    if (ev.linkFrom >= 0 && ev.linkTo >= 0) {
        if (Attach(*this, d, ev.linkFrom, ev.linkTo)) {
            d.view.selNode = -1;
            d.view.selLink = ev.linkTo;
            d.selParam = -1;
        } else {
            SetStatus("That link is not possible (a leaf cannot have children, and a node cannot lead back to itself)");
        }
    }
    if (ev.deleteKey) {
        if (d.view.selLink >= 0) DeleteLink(d, d.view.selLink);
        else DeleteBehaviorSelection(d);
    }
    if (ev.context) {
        std::vector<std::string> items;
        d.menuTypes.clear();
        auto addTypes = [&](const std::string& prefix) {
            for (const char* kind : kKinds) {
                if (!btLibrary.is_object() || !btLibrary.contains("nodes")) break;
                for (auto& n : btLibrary["nodes"]) {
                    if (JStr(n, "kind") != kind) continue;
                    std::string item = prefix + KindName(kind) + "/" + JStr(n, "title", JStr(n, "id"));
                    items.push_back(item);
                    d.menuTypes[item] = JStr(n, "id");
                }
            }
        };
        if (ev.contextNode == kRootId) {
            addTypes("Add child/");
        } else if (ev.contextNode >= 0) {
            if (TakesChildren(KindOf(*this, JStr(Nodes(d)[ev.contextNode], "type")))) addTypes("Add child/");
            items.push_back("Delete node");
        } else if (ev.contextLink >= 0) {
            items.push_back("Delete link");
        } else {
            addTypes("Add node/");
        }
        graph::OpenMenu(d.view, items, ev);
    }
    if (ev.menuChoice >= 0) {
        const std::string& text = ev.menuText;
        if (text == "Delete node") {
            DeleteNode(d, ev.menuNode);
        } else if (text == "Delete link") {
            DeleteLink(d, ev.menuLink);
        } else if (d.menuTypes.count(text)) {
            const std::string& type = d.menuTypes[text];
            bool asChild = text.rfind("Add child/", 0) == 0 && ev.menuNode >= 0;
            Vector2 pos = ev.menuGraphPos;
            int parent = -1;
            if (asChild) {
                parent = ev.menuNode;
                Vector2 pp = ev.menuNode == kRootId ? NodePos(d, "root", {20, 0}) : NodePos(d, IdOf(d, ev.menuNode), {0, 0});
                size_t sib = ev.menuNode == kRootId ? 0 : ChildIndices(d, ev.menuNode).size();
                pos = {pp.x + 240, pp.y + sib * 70.0f};
            }
            int idx = AddNode(*this, d, type, pos, parent);
            d.view.selNode = idx;
            d.view.selLink = -1;
            d.selParam = -1;
        }
    }

    // ---- blackboard panel ----
    if (showBB) {
        Fill(bbR, Color{27, 31, 40, 240});
        Outline(bbR, theme.border);
        Text("BLACKBOARD", bbR.x + 10, bbR.y + 8, theme.textDim, 12, "ui_bold");
        if (Button("addkey", {bbR.x + bbR.width - 70, bbR.y + 5, 62, 20}, "+ Add")) {
            std::vector<std::string> names = KeyNames(*this, d);
            std::string name = UniqueName(names, "value");
            Arr(d.def, "blackboard").push_back({{"name", name}, {"value", "0"}});
            d.selParam = (int)d.def["blackboard"].size() - 1;
            d.view.selNode = d.view.selLink = -1;
            Touch(d);
        }
        float y = bbR.y + 30;
        Text("this tree's values (name, start value)", bbR.x + 10, y, theme.textDim, 11);
        y += 16;
        json& bb = Arr(d.def, "blackboard");
        int removeAt = -1;
        for (size_t i = 0; i < bb.size(); ++i) {
            PushId("k" + std::to_string(i));
            std::string name = JStr(bb[i], "name");
            std::string val = bb[i].contains("value") ? (bb[i]["value"].is_string() ? bb[i]["value"].get<std::string>() : bb[i]["value"].dump()) : "";
            if (TextField("kname", {bbR.x + 8, y, 118, kRow - 2}, name)) {
                std::string old = JStr(bb[i], "name");
                if (!name.empty() && name != old) {
                    // references in node parameters follow the rename
                    for (auto& n : Nodes(d))
                        if (n.contains("params") && n["params"].is_object())
                            for (auto it = n["params"].begin(); it != n["params"].end(); ++it)
                                if (it.value().is_string() && it.value().get<std::string>() == old) it.value() = name;
                    bb[i]["name"] = name;
                    Touch(d);
                }
            }
            if (TextField("kval", {bbR.x + 130, y, 78, kRow - 2}, val)) {
                bb[i]["value"] = val;
                Touch(d);
            }
            if (Button("kdel", {bbR.x + bbR.width - 30, y, 22, kRow - 2}, "x")) removeAt = (int)i;
            PopId();
            y += 26;
        }
        if (removeAt >= 0) {
            bb.erase(bb.begin() + removeAt);
            Touch(d);
        }
        y += 4;
        Text("built in (kept up to date by the engine)", bbR.x + 10, y, theme.textDim, 11);
        y += 16;
        if (btLibrary.is_object() && btLibrary.contains("keys")) {
            for (auto& k : btLibrary["keys"]) {
                Text(Ellipsize(JStr(k, "name"), 110, 12), bbR.x + 12, y, theme.text, 12);
                Text(Ellipsize(JStr(k, "type"), 90, 12), bbR.x + 134, y, theme.textDim, 12);
                if (Hovered({bbR.x + 10, y, bbR.width - 20, 16})) Tooltip(JStr(k, "help"));
                y += 18;
            }
        }
    }
}

// ---------------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------------
// one parameter of a node, drawn from the library's description of it
static float ParamRow(Editor::Impl& ed, Document& d, const Fields& f, float y, json& node, const json& pd) {
    std::string name = JStr(pd, "name"), type = JStr(pd, "type", "string");
    const char* help = pd.contains("help") && pd["help"].is_string() ? pd["help"].get_ref<const std::string&>().c_str() : nullptr;
    f.Label(name, y, help);
    json& params = node["params"];
    json cur = params.is_object() && params.contains(name) ? params[name] : (pd.contains("default") ? pd["default"] : json());
    auto store = [&](const json& v) {
        if (!node.contains("params") || !node["params"].is_object()) node["params"] = json::object();
        node["params"][name] = v;
        Touch(d);
    };
    Rectangle fr = f.Field(y);
    PushId("pr_" + name);
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
        std::string s = cur.is_string() ? cur.get<std::string>() : (cur.is_null() ? "" : cur.dump());
        if (Dropdown("e", fr, s, opts)) store(s);
    } else if (type == "key") {
        // a name typed in, or picked from the keys the tree knows (small button on the right)
        std::string s = cur.is_string() ? cur.get<std::string>() : "";
        if (TextField("k", {fr.x, fr.y, fr.width - 24, fr.height}, s)) store(s);
        std::string pick = "v";
        std::vector<std::string> keys = KeyNames(ed, d);
        if (Dropdown("kp", {fr.x + fr.width - 22, fr.y, 22, fr.height}, pick, keys)) store(pick);
    } else {
        std::string s = cur.is_string() ? cur.get<std::string>() : (cur.is_null() ? "" : cur.dump());
        if (TextField("s", fr, s)) {
            if (cur.is_number()) {
                json parsed = json::parse(s, nullptr, false);
                store(parsed.is_discarded() ? json(s) : parsed);
            } else {
                store(s);
            }
        }
    }
    PopId();
    return y + kRow + 2;
}

static float InspectNode(Editor::Impl& ed, Document& d, const Fields& f, float y, int idx) {
    Rectangle r = f.r;
    json& node = Nodes(d)[idx];
    std::string type = JStr(node, "type");
    std::string kind = KindOf(ed, type);
    const json* meta = LibNode(ed, type);
    Text("Node", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    Text(KindName(kind), r.x + r.width - 90, y + 2, KindColor(kind), 12, "ui_bold");
    y += kRow - 2;

    f.Label("Name", y, "shown on the node (optional)");
    std::string label = JStr(node, "label");
    if (TextField("nlabel", f.Field(y), label, meta ? JStr(*meta, "title").c_str() : "")) {
        if (label.empty()) node.erase("label");
        else node["label"] = label;
        Touch(d);
    }
    y += kRow + 2;

    // type: only the types of the same kind, so the connections stay valid
    f.Label("Type", y);
    std::vector<std::string> titles;
    std::map<std::string, std::string> idOf;
    if (ed.btLibrary.is_object() && ed.btLibrary.contains("nodes"))
        for (auto& n : ed.btLibrary["nodes"]) {
            if (JStr(n, "kind") != kind) continue;
            titles.push_back(JStr(n, "title", JStr(n, "id")));
            idOf[titles.back()] = JStr(n, "id");
        }
    std::string cur = meta ? JStr(*meta, "title", type) : type;
    if (titles.empty()) {
        Text(Ellipsize(type + " (not found)", f.FieldW(), 13), f.FieldX(), y + 4, Color{240, 100, 90, 255}, 13);
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
    if (TakesChildren(kind)) {
        std::vector<int> kids = ChildIndices(d, idx);
        Text(kind == "decorator" ? "Child" : "Children (run top to bottom)", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        y += kRow - 2;
        if (kids.empty()) {
            Text("none: drag from the dot on the node's right edge", r.x + 12, y + 2, theme.textDim, 12);
            y += kRow;
        }
        for (size_t i = 0; i < kids.size(); ++i) {
            Rectangle row{r.x + 10, y, r.width - 20, kRow - 2};
            if (Hovered(row)) Fill(row, theme.buttonHover);
            std::string ct = JStr(Nodes(d)[kids[i]], "type");
            const json* cm = LibNode(ed, ct);
            Text(std::to_string(i + 1) + ".  " + JStr(Nodes(d)[kids[i]], "label", cm ? JStr(*cm, "title", ct) : ct), row.x + 6, row.y + 3, theme.text, 13);
            if (Clicked(row)) d.view.selNode = kids[i];
            y += kRow;
        }
        y += 4;
    }
    y += 6;
    if (Button("ndel", {r.x + 10, y, r.width - 20, kRow}, "Delete node")) DeleteNode(d, idx);
    return y + kRow + 8;
}

float Editor::Impl::InspectBehavior(Rectangle r, float y) {
    Document* dp = Doc();
    if (!dp) return y;
    Document& d = *dp;
    Fields f{r};
    Text("Behavior: " + d.name, r.x + 12, y + 4, theme.text, 15, "ui_bold");
    y += kRow + 4;
    Text(Ellipsize(d.path, r.width - 24, 12), r.x + 12, y, theme.textDim, 12);
    y += 18;
    float bw = (r.width - 26) / 2;
    if (Button("dsave", {r.x + 10, y, bw, kRow + 2}, d.dirty ? "Save *" : "Save", d.dirty, d.dirty)) SaveDocument(d);
    if (Button("drevert", {r.x + 16 + bw, y, bw, kRow + 2}, "Revert", false, d.dirty)) ReloadDocument(d);
    y += kRow + 10;
    y = SelectionNote(d, r, y);

    int n = (int)Nodes(d).size();
    if (d.view.selLink >= 0 && d.view.selLink < n) {
        // a link: parent -> child
        int child = d.view.selLink;
        int parent = ParentOf(d, child);
        Text("Link", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        y += kRow - 2;
        std::string pn = parent == kRootId ? "Root" : (parent >= 0 ? JStr(Nodes(d)[parent], "label", JStr(Nodes(d)[parent], "type")) : "?");
        Text(pn + "  ->  " + JStr(Nodes(d)[child], "label", JStr(Nodes(d)[child], "type")), r.x + 12, y + 2, theme.text, 14, "ui_bold");
        y += kRow + 8;
        if (Button("ldel", {r.x + 10, y, r.width - 20, kRow}, "Delete link")) DeleteLink(d, child);
        return y + kRow + 8;
    }
    if (d.view.selNode >= 0 && d.view.selNode < n) return InspectNode(*this, d, f, y, d.view.selNode);
    if (d.view.selNode == kRootId) {
        Text("Root", r.x + 12, y + 2, theme.text, 14, "ui_bold");
        y += kRow + 2;
        y = WrapText("The tree is run from here every frame. Drag from the dot on this node to the first node (usually a Selector or Sequence).", r.x + 12, y, r.width - 24,
                     theme.textDim);
        return y + 10;
    }
    if (d.selParam >= 0 && d.selParam < (int)Arr(d.def, "blackboard").size()) {
        // handled in the panel; nothing more to edit here
        Text("Blackboard value", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        y += kRow;
        y = WrapText("Edit the name and the start value in the Blackboard panel. Nodes read and write it by name; a name starting with global. is shared by all monsters.",
                     r.x + 12, y, r.width - 24, theme.textDim);
        return y + 10;
    }

    Text("Behavior tree", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
    y += kRow - 2;
    Text(TextFormat("%d nodes, %d blackboard values", n, (int)Arr(d.def, "blackboard").size()), r.x + 12, y + 2, theme.textDim, 12);
    y += 22;
    std::vector<std::string> issues = Issues(*this, d);
    if (issues.empty()) {
        Text("No problems found.", r.x + 12, y, Color{120, 220, 140, 255}, 12);
        y += 18;
    }
    for (auto& s : issues) y = WrapText(s, r.x + 12, y, r.width - 24, Color{240, 180, 60, 255}) + 2;
    y += 10;
    const char* tips[] = {"Right click the canvas to add nodes, a node to add a child.",
                          "Drag from the dot on a node's right edge onto another",
                          "node to connect them. A Selector/Sequence runs its",
                          "children top to bottom (by their position).",
                          "",
                          "Use it: a monster's `behavior` property (Inspector",
                          "dropdown, or drag from Project -> Behaviors onto the",
                          "monster). In the game, console: bt_debug 1 shows what",
                          "every monster is doing, bt_debug 2 logs the changes.",
                          "See docs/BEHAVIOR.md."};
    for (const char* t : tips) {
        Text(t, r.x + 12, y, theme.textDim, 12);
        y += 17;
    }
    return y + 10;
}

// ---------------------------------------------------------------------------------
// scripted access (console command "behavior") for tests and automation
// ---------------------------------------------------------------------------------
std::string Editor::BehaviorCommand(const std::vector<std::string>& a) {
    if (a.size() < 2) return "behavior open|create|add|link|unlink|set|label|select|delete|arrange|save|dump|close";
    const std::string& cmd = a[1];
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : std::string(); };
    if (cmd == "open") {
        m->RefreshBehaviorList();
        m->OpenDocument("behavior", arg(2));
        return m->Doc() && m->Doc()->kind == "behavior" ? "opened " + m->Doc()->path : "not opened";
    }
    if (cmd == "create") {
        if (!m->CreateBehavior(arg(2))) return "create failed";
        return "created " + m->Doc()->path;
    }
    Document* d = m->Doc();
    if (!d || d->kind != "behavior") return "no behavior open (behavior open <name>)";
    {
        std::string r = m->DocCommand(*d, a);
        if (r != "\x01") return r;
    }
    auto node = [&](const std::string& id) { return id == "root" ? kRootId : IndexOf(*d, id); };
    if (cmd == "dump") return d->def.dump();
    if (cmd == "entity") {
        // selects a map entity on the Level tab (to look at its Inspector)
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
    if (cmd == "save") return m->SaveDocument(*d) ? "saved" : "save failed";
    if (cmd == "arrange") {
        Arrange(*m, *d);
        Touch(*d);
        d->fitted = false;
        return "";
    }
    if (cmd == "add") {
        // add <type> [parent id|root|-] [x y]
        if (!LibNode(*m, arg(2))) return "unknown node type: " + arg(2);
        int parent = arg(3).empty() || arg(3) == "-" ? -1 : node(arg(3));
        if (!arg(3).empty() && arg(3) != "-" && parent == -1) return "no such node: " + arg(3);
        Vector2 pos{(float)atof(arg(4).c_str()), (float)atof(arg(5).c_str())};
        if (parent != -1 && a.size() < 6) {
            Vector2 pp = parent == kRootId ? NodePos(*d, "root", {20, 0}) : NodePos(*d, IdOf(*d, parent), {0, 0});
            pos = {pp.x + 240, pp.y + (parent == kRootId ? 0 : ChildIndices(*d, parent).size()) * 70.0f};
        }
        int idx = AddNode(*m, *d, arg(2), pos, parent);
        return "node " + IdOf(*d, idx);
    }
    if (cmd == "link") {
        int p = node(arg(2)), c = node(arg(3));
        if (p == -1 || c == -1) return "no such node";
        return Attach(*m, *d, p, c) ? "linked" : "refused";
    }
    if (cmd == "unlink") {
        int c = node(arg(2));
        if (c < 0) return "no such node";
        DeleteLink(*d, c);
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
    if (cmd == "select_link") {
        d->view.selNode = -1;
        d->view.selLink = node(arg(2));
        return "";
    }
    if (cmd == "label") {
        int i = node(arg(2));
        if (i < 0 || i >= (int)Nodes(*d).size()) return "no such node";
        Nodes(*d)[i]["label"] = arg(3);
        Touch(*d);
        return "";
    }
    if (cmd == "set") {
        // set <id> <param> <value>: numbers and booleans are stored as such
        int i = node(arg(2));
        if (i < 0 || i >= (int)Nodes(*d).size()) return "no such node";
        json v = json::parse(arg(4), nullptr, false);
        if (v.is_discarded() || v.is_object() || v.is_array()) v = arg(4);
        Nodes(*d)[i]["params"][arg(3)] = v;
        Touch(*d);
        return "";
    }
    if (cmd == "type") {
        int i = node(arg(2));
        if (i < 0 || i >= (int)Nodes(*d).size() || !LibNode(*m, arg(3))) return "no such node or type";
        SetType(*m, *d, i, arg(3));
        return "";
    }
    if (cmd == "issues") {
        std::string out;
        for (auto& s : Issues(*m, *d)) out += s + " | ";
        return out.empty() ? "none" : out;
    }
    if (cmd == "key") {
        // key <name> <value>: adds or changes a blackboard value of the tree
        json& bb = Arr(d->def, "blackboard");
        for (auto& k : bb)
            if (JStr(k, "name") == arg(2)) {
                k["value"] = arg(3);
                Touch(*d);
                return "";
            }
        bb.push_back({{"name", arg(2)}, {"value", arg(3)}});
        Touch(*d);
        return "";
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

json Editor::Impl::CopyBehaviorSelection(Document& d) {
    json out = {{"kind", "behavior"}, {"nodes", json::array()}};
    std::set<std::string> ids;
    for (int i : SelectedNodes(d)) ids.insert(IdOf(d, i));
    for (int i : SelectedNodes(d)) {
        json n = Nodes(d)[i];
        if (n.contains("children") && n["children"].is_array()) {
            json kept = json::array();
            for (auto& c : n["children"])
                if (c.is_string() && ids.count(c.get<std::string>())) kept.push_back(c);  // links to nodes left behind are dropped
            n["children"] = kept;
        }
        Vector2 p = NodePos(d, JStr(n, "id"), {0, 0});
        out["nodes"].push_back({{"def", n}, {"pos", json::array({p.x, p.y})}});
    }
    return out;
}

void Editor::Impl::PasteBehavior(Document& d, const json& clip) {
    if (!clip.is_object() || JStr(clip, "kind") != "behavior" || !clip.contains("nodes")) return;
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
        if (!n.contains("children") || !n["children"].is_array()) continue;
        json kids = json::array();
        for (auto& c : n["children"])
            if (c.is_string() && renamed.count(c.get<std::string>())) kids.push_back(renamed[c.get<std::string>()]);
        n["children"] = kids;
    }
    if (added.empty()) return;
    SortChildren(*this, d);
    d.view.selNodes = added;
    d.view.selNode = *added.rbegin();
    d.view.lastSelNode = d.view.selNode;
    d.view.selLink = -1;
    d.selParam = -1;
    Touch(d);
}

void Editor::Impl::DeleteBehaviorSelection(Document& d) {
    std::vector<int> ids = SelectedNodes(d);
    for (size_t i = ids.size(); i-- > 0;) DeleteNode(d, ids[i]);
    d.view.selNode = d.view.selLink = -1;
    d.view.selNodes.clear();
    d.view.lastSelNode = -1;
}
}  // namespace rbs
