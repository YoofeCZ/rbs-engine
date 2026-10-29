// RBS Engine - editor: undo / redo and the clipboard of graph documents (animators, behavior
// trees, quests, dialogues). Undo works on snapshots of the document's JSON: UpdateUndo stores
// one whenever an edit is finished - while a mouse button is down (dragging nodes, a number
// field) or a text field has focus the edit counts as still going on, so a drag or a typed
// word is a single step.
#include "core/log.h"
#include "editor/editor_doc.h"
#include "editor/editor_impl.h"

namespace rbs {

using namespace edoc;

static const size_t kMaxUndo = 100;

static void Commit(Document& d) {
    d.committed = d.stamp;
    if (d.def == d.snap) return;
    d.undo.push_back(d.snap);
    if (d.undo.size() > kMaxUndo) d.undo.erase(d.undo.begin());
    d.redo.clear();
    d.snap = d.def;
}

void Editor::Impl::UpdateUndo(Document& d) {
    if (d.stamp == d.committed) return;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || ui::KeyboardCaptured()) return;  // the edit is still going on
    Commit(d);
}

// the document has a new def: selections and previews of the old one no longer apply
static void Restored(Document& d) {
    d.view.selNode = d.view.selLink = -1;
    d.view.selNodes.clear();
    d.view.lastSelNode = -1;
    d.selParam = -1;
    d.makeFrom = -1;
    d.previewStale = true;
    d.dirty = true;
    d.confirmClose = false;
    d.snap = d.def;
    d.committed = d.stamp;
}

void Editor::Impl::DocUndo(Document& d) {
    Commit(d);  // an edit in progress becomes a step first
    if (d.undo.empty()) return;
    d.redo.push_back(d.def);
    d.def = d.undo.back();
    d.undo.pop_back();
    Restored(d);
    SetStatus("Undo");
}

void Editor::Impl::DocRedo(Document& d) {
    Commit(d);
    if (d.redo.empty()) return;
    d.undo.push_back(d.def);
    d.def = d.redo.back();
    d.redo.pop_back();
    Restored(d);
    SetStatus("Redo");
}

static json CopyOf(Editor::Impl& ed, Document& d) {
    if (d.kind == "animator") return ed.CopyAnimatorSelection(d);
    if (d.kind == "behavior") return ed.CopyBehaviorSelection(d);
    return ed.CopyFlowSelection(d);
}

static bool Empty(const json& clip) {
    return !clip.is_object() || ((!clip.contains("nodes") || clip["nodes"].empty()) && (!clip.contains("states") || clip["states"].empty()));
}

void Editor::Impl::DocDeleteSelected(Document& d) {
    if (d.kind == "animator") DeleteAnimatorSelection(d);
    else if (d.kind == "behavior") DeleteBehaviorSelection(d);
    else DeleteFlowSelection(d);
}

void Editor::Impl::DocCopy(Document& d, bool cut) {
    json clip = CopyOf(*this, d);
    if (Empty(clip)) {
        SetStatus("Nothing selected to copy (click a node, Shift-click for more, or drag a box)");
        return;
    }
    graphClipboard = clip.dump();
    SetClipboardText(("rbs-graph:" + graphClipboard).c_str());  // survives a restart of the editor and other projects
    d.pasteN = 0;
    size_t n = clip.contains("nodes") ? clip["nodes"].size() : clip["states"].size();
    SetStatus(std::string(cut ? "Cut " : "Copied ") + std::to_string(n) + (n == 1 ? " node" : " nodes"));
    if (cut) DocDeleteSelected(d);
}

void Editor::Impl::DocPaste(Document& d) {
    std::string text = graphClipboard;
    if (const char* sys = GetClipboardText()) {
        std::string s = sys;
        if (s.rfind("rbs-graph:", 0) == 0) text = s.substr(10);  // the system clipboard wins when it holds nodes
    }
    json clip = json::parse(text, nullptr, false);
    if (clip.is_discarded() || !clip.is_object()) {
        SetStatus("Nothing to paste: copy some nodes first");
        return;
    }
    std::string kind = JStr(clip, "kind");
    if (kind != d.kind) {
        SetStatus("Those nodes come from a " + kind + " graph and cannot be pasted into a " + d.kind);
        return;
    }
    if (d.kind == "animator") PasteAnimator(d, clip);
    else if (d.kind == "behavior") PasteBehavior(d, clip);
    else PasteFlow(d, clip);
    ++d.pasteN;
}

void Editor::Impl::DocDuplicate(Document& d) {
    json clip = CopyOf(*this, d);
    if (Empty(clip)) return;
    if (d.kind == "animator") PasteAnimator(d, clip);
    else if (d.kind == "behavior") PasteBehavior(d, clip);
    else PasteFlow(d, clip);
    ++d.pasteN;
}

std::string Editor::Impl::DocCommand(Document& d, const std::vector<std::string>& a) {
    const std::string& cmd = a[1];
    if (cmd == "commit") {
        Commit(d);  // a finished edit (what the editor does when the mouse is released)
        return "";
    }
    if (cmd == "undo") {
        DocUndo(d);
        return "";
    }
    if (cmd == "redo") {
        DocRedo(d);
        return "";
    }
    if (cmd == "copy" || cmd == "cut") {
        DocCopy(d, cmd == "cut");
        return "";
    }
    if (cmd == "paste") {
        DocPaste(d);
        return "";
    }
    if (cmd == "duplicate") {
        DocDuplicate(d);
        return "";
    }
    if (cmd == "delete_selected") {
        DocDeleteSelected(d);
        return "";
    }
    if (cmd == "select_idx") {
        d.view.selNodes.clear();
        for (size_t i = 2; i < a.size(); ++i) d.view.selNodes.insert(atoi(a[i].c_str()));
        d.view.selNode = d.view.selNodes.empty() ? -1 : *d.view.selNodes.rbegin();
        d.view.lastSelNode = d.view.selNode;
        d.view.selLink = -1;
        return "";
    }
    if (cmd == "undo_depth") {
        size_t nodes = d.def.contains(d.kind == "animator" ? "states" : "nodes") && d.def[d.kind == "animator" ? "states" : "nodes"].is_array() ? d.def[d.kind == "animator" ? "states" : "nodes"].size() : 0;
        return "undo " + std::to_string(d.undo.size()) + " redo " + std::to_string(d.redo.size()) + " selected " + std::to_string(d.view.selNodes.size()) + " nodes " + std::to_string(nodes);
    }
    return "\x01";  // not handled here
}

}  // namespace rbs
