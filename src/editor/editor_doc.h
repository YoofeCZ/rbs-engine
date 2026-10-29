// RBS Engine - helpers shared by the graph document editors (editor_animator.cpp,
// editor_behavior.cpp): JSON access and the label + field rows of their Inspectors.
#pragma once

#include "editor/editor_impl.h"
#include "ui/imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rbs {
namespace edoc {

constexpr float kRow = 22;
constexpr const char* kNone = "(none)";

// an edit was made: the document is unsaved, and UpdateUndo will store the state once the edit is finished
inline void Touch(Document& d) {
    d.dirty = true;
    d.confirmClose = false;
    d.previewStale = true;
    d.stamp++;
}

// "3 nodes selected" line at the top of an Inspector when more than one node is selected
inline float SelectionNote(const Document& d, Rectangle r, float y) {
    if (d.view.selNodes.size() < 2) return y;
    ui::Text(std::to_string(d.view.selNodes.size()) + " nodes selected (Delete, Ctrl+C / X / V, Ctrl+D)", r.x + 12, y, Color{120, 190, 255, 255}, 12);
    return y + 18;
}

inline json& Arr(json& d, const char* key) {
    if (!d.contains(key) || !d[key].is_array()) d[key] = json::array();
    return d[key];
}

inline std::string JStr(const json& j, const char* key, const std::string& def = "") {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}

inline float JNum(const json& j, const char* key, float def) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : def;
}

inline bool JBool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}

inline bool ReadPos(const json& j, Vector2& out) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return false;
    out = {j[0].get<float>(), j[1].get<float>()};
    return true;
}

inline std::string UniqueName(const std::vector<std::string>& taken, const std::string& base) {
    auto has = [&](const std::string& n) { return std::find(taken.begin(), taken.end(), n) != taken.end(); };
    if (!has(base)) return base;
    for (int i = 1;; ++i) {
        std::string n = base + " " + std::to_string(i);
        if (!has(n)) return n;
    }
}

// One Inspector column: a label on the left, the field on the right.
struct Fields {
    Rectangle r;  // the Inspector column
    float labelW = 98;
    float FieldX() const { return r.x + 10 + labelW; }
    float FieldW() const { return r.width - labelW - 24; }
    void Label(const std::string& s, float y, const char* help = nullptr) const {
        ui::Text(ui::Ellipsize(s, labelW - 4, 13), r.x + 12, y + 4, ui::theme.text, 13);
        if (help && ui::Hovered({r.x + 10, y, labelW, kRow - 2})) ui::Tooltip(help);
    }
    Rectangle Field(float y) const { return {FieldX(), y, FieldW(), kRow - 2}; }
};

// a dropdown whose first option stands for the empty value
inline bool OptionalDropdown(const std::string& id, Rectangle r, std::string& value, std::vector<std::string> options) {
    options.insert(options.begin(), kNone);
    std::string shown = value.empty() ? kNone : value;
    if (!ui::Dropdown(id, r, shown, options)) return false;
    value = shown == kNone ? "" : shown;
    return true;
}

// draws `text` wrapped to `width`, returns the y below it
inline float WrapText(const std::string& text, float x, float y, float width, Color c, int size = 12) {
    std::string line, word;
    auto flush = [&]() {
        ui::Text(line, x, y, c, size);
        y += size + 4;
        line.clear();
    };
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ' ') {
            std::string test = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::TextWidth(test, size) > width) {
                flush();
                line = word;
            } else {
                line = test;
            }
            word.clear();
        } else {
            word += text[i];
        }
    }
    if (!line.empty()) flush();
    return y;
}

}  // namespace edoc
}  // namespace rbs
