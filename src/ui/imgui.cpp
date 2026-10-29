#include "ui/imgui.h"
#include "core/common.h"
#include "ui/fonts.h"

#include <functional>
#include <map>

namespace rbs {
namespace ui {

Theme theme;

namespace {

struct State {
    std::vector<Rectangle> blockers, prevBlockers;
    std::vector<std::string> idStack;
    std::vector<Rectangle> clipStack;
    // text editing
    std::string focusId;
    std::string editBuffer;
    int cursor = 0;
    bool focusIsFloat = false;
    std::string pendingCommitId;   // committed this frame by clicking elsewhere
    // float dragging
    std::string dragFloatId;
    float dragStartX = 0, dragStartValue = 0;
    bool dragMoved = false;
    // dropdown popup
    std::string popupId;
    Rectangle popupAnchor{};
    std::vector<std::string> popupOptions;
    std::string popupResultId, popupResult;
    float popupScroll = 0;
    bool popupJustOpened = false;
    // scroll areas
    std::map<std::string, float> scroll;
    struct ScrollFrame {
        std::string id;
        Rectangle r;
        float content;
    };
    std::vector<ScrollFrame> scrollStack;
    // tooltip
    std::string tooltip;
    // drag & drop
    bool dragging = false, dragPending = false, dragReleased = false;
    Vector2 dragStart{};
    std::string dragType, dragPayload, dragLabel;
    // double click
    double lastClickTime = 0;
    Vector2 lastClickPos{};
    bool doubleClick = false;
    bool mouseConsumed = false;
    // modal dialog: open this frame / last frame, widgets being drawn inside it
    bool modalNow = false, modalPrev = false, inModal = false;
};
State S;

std::string FullId(const std::string& id) {
    std::string s;
    for (auto& p : S.idStack) s += p + "/";
    return s + id;
}

bool InClip(Vector2 p) {
    if (S.clipStack.empty()) return true;
    return CheckCollisionPointRec(p, S.clipStack.back());
}

bool PopupCovers(Vector2 p);

}  // namespace

void BeginFrame() {
    S.prevBlockers = S.blockers;
    S.blockers.clear();
    S.modalPrev = S.modalNow;
    S.modalNow = S.inModal = false;
    S.tooltip.clear();
    S.dragReleased = false;
    S.doubleClick = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        double now = GetTime();
        Vector2 m = GetMousePosition();
        if (now - S.lastClickTime < 0.35 && Vector2Distance(m, S.lastClickPos) < 4) S.doubleClick = true;
        S.lastClickTime = now;
        S.lastClickPos = m;
    }
    // drag & drop state machine
    if (S.dragPending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && Vector2Distance(GetMousePosition(), S.dragStart) > 6) {
        S.dragging = true;
        S.dragPending = false;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (S.dragging) S.dragReleased = true;
        S.dragPending = false;
    }
}

void EndFrame() {
    // dropdown popup
    if (!S.popupId.empty()) {
        float itemH = 22;
        float h = std::min((float)S.popupOptions.size() * itemH, 360.0f);
        Rectangle r{S.popupAnchor.x, S.popupAnchor.y + S.popupAnchor.height, std::max(S.popupAnchor.width, 160.0f), h};
        if (r.y + r.height > GetScreenHeight()) r.y = S.popupAnchor.y - r.height;
        if (r.x + r.width > GetScreenWidth()) r.x = GetScreenWidth() - r.width;
        S.blockers.push_back(r);
        Fill({r.x + 3, r.y + 3, r.width, r.height}, Color{0, 0, 0, 120});
        Fill(r, theme.panelAlt);
        Outline(r, theme.border);
        float total = S.popupOptions.size() * itemH;
        if (CheckCollisionPointRec(GetMousePosition(), r)) S.popupScroll -= GetMouseWheelMove() * 44;
        S.popupScroll = Clampf(S.popupScroll, 0, std::max(0.0f, total - h));
        BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
        for (size_t i = 0; i < S.popupOptions.size(); ++i) {
            Rectangle ir{r.x, r.y + i * itemH - S.popupScroll, r.width, itemH};
            if (ir.y + ir.height < r.y || ir.y > r.y + r.height) continue;
            bool hov = CheckCollisionPointRec(GetMousePosition(), ir) && CheckCollisionPointRec(GetMousePosition(), r);
            if (hov) Fill(ir, theme.accentDim);
            Text(S.popupOptions[i], ir.x + 8, ir.y + 4, theme.text);
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                S.popupResultId = S.popupId;
                S.popupResult = S.popupOptions[i];
            }
        }
        EndScissorMode();
        if (S.popupJustOpened) S.popupJustOpened = false;
        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_ESCAPE)) S.popupId.clear();
    }
    // tooltip
    if (!S.tooltip.empty()) {
        Vector2 m = GetMousePosition();
        float w = TextWidth(S.tooltip) + 12;
        Rectangle r{m.x + 14, m.y + 16, w, 22};
        if (r.x + r.width > GetScreenWidth()) r.x = GetScreenWidth() - r.width - 4;
        Fill(r, Color{10, 12, 16, 235});
        Outline(r, theme.border);
        Text(S.tooltip, r.x + 6, r.y + 4, theme.text);
    }
    // drag ghost
    if (S.dragging) {
        Vector2 m = GetMousePosition();
        float w = TextWidth(S.dragLabel) + 16;
        Rectangle r{m.x + 12, m.y + 8, w, 22};
        Fill(r, Color{56, 120, 245, 200});
        Text(S.dragLabel, r.x + 8, r.y + 4, WHITE);
    }
    if (S.dragReleased) {
        S.dragging = false;
        S.dragType.clear();
        S.dragPayload.clear();
    }
    S.pendingCommitId.clear();
}

bool MouseOverUI() {
    Vector2 m = GetMousePosition();
    for (auto& r : S.prevBlockers)
        if (CheckCollisionPointRec(m, r)) return true;
    return !S.popupId.empty();
}

bool KeyboardCaptured() { return !S.focusId.empty(); }
void Block(Rectangle r) { S.blockers.push_back(r); }

void BeginModal() {
    S.modalNow = S.inModal = true;
    S.blockers.push_back({0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()});
}
void EndModal() { S.inModal = false; }
void ClearFocus() { S.focusId.clear(); }

void Fill(Rectangle r, Color c) { DrawRectangleRec(r, c); }
void Outline(Rectangle r, Color c, float thick) { DrawRectangleLinesEx(r, thick, c); }

void Text(const std::string& s, float x, float y, Color c, int size, const char* font) {
    fonts::Text(font, s.c_str(), x, y, size > 0 ? size : theme.fontSize, c);
}

void TextCentered(const std::string& s, Rectangle r, Color c, int size, const char* font) {
    int sz = size > 0 ? size : theme.fontSize;
    float w = fonts::Width(font, s.c_str(), sz);
    fonts::Text(font, s.c_str(), r.x + (r.width - w) * 0.5f, r.y + (r.height - sz) * 0.5f - 1, sz, c);
}

float TextWidth(const std::string& s, int size, const char* font) { return fonts::Width(font, s.c_str(), size > 0 ? size : theme.fontSize); }

std::string Ellipsize(const std::string& s, float maxW, int size) {
    if (TextWidth(s, size) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && TextWidth(t + "...", size) > maxW) t.pop_back();
    return t + "...";
}

void PushId(const std::string& id) { S.idStack.push_back(id); }
void PopId() {
    if (!S.idStack.empty()) S.idStack.pop_back();
}

namespace {
bool PopupCovers(Vector2 p) {
    if (S.popupId.empty()) return false;
    float itemH = 22;
    float h = std::min((float)S.popupOptions.size() * itemH, 360.0f);
    Rectangle r{S.popupAnchor.x, S.popupAnchor.y + S.popupAnchor.height, std::max(S.popupAnchor.width, 160.0f), h};
    return CheckCollisionPointRec(p, r);
}
}  // namespace

bool Hovered(Rectangle r) {
    // While a modal dialog is open (it is drawn last, so this is known from the previous
    // frame) only its own widgets take the mouse. On the frame it opens it ignores the
    // mouse too, so the click that opened it cannot also press something in it.
    if (S.inModal != S.modalPrev) return false;
    Vector2 m = GetMousePosition();
    return CheckCollisionPointRec(m, r) && InClip(m) && !PopupCovers(m);
}

bool Clicked(Rectangle r, int button) { return Hovered(r) && IsMouseButtonPressed(button); }
bool DoubleClicked(Rectangle r) { return Hovered(r) && S.doubleClick; }

bool Button(const std::string& id, Rectangle r, const std::string& label, bool selected, bool enabled) {
    bool hov = enabled && Hovered(r);
    Color bg = selected ? theme.accent : (hov ? theme.buttonHover : theme.button);
    if (!enabled) bg = theme.panelAlt;
    Fill(r, bg);
    Outline(r, selected ? Color{90, 150, 255, 255} : theme.border);
    TextCentered(Ellipsize(label, r.width - 8), r, enabled ? (selected ? WHITE : theme.text) : theme.textDim);
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool EditText(const std::string& fid, Rectangle r, std::string& value, const char* placeholder, bool* committed) {
    bool focused = S.focusId == fid;
    bool hov = Hovered(r);
    if (committed) *committed = false;
    if (!focused && hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        S.focusId = fid;
        S.editBuffer = value;
        S.cursor = (int)value.size();
        focused = true;
    } else if (focused && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hov) {
        // clicked elsewhere: commit
        value = S.editBuffer;
        S.focusId.clear();
        if (committed) *committed = true;
        Fill(r, theme.field);
        Outline(r, theme.border);
        Text(Ellipsize(value, r.width - 8), r.x + 5, r.y + (r.height - theme.fontSize) * 0.5f - 1, theme.text);
        return true;
    }
    Fill(r, theme.field);
    Outline(r, focused ? theme.accent : (hov ? theme.buttonHover : theme.border));
    if (!focused) {
        if (value.empty() && placeholder) Text(placeholder, r.x + 5, r.y + (r.height - theme.fontSize) * 0.5f - 1, theme.textDim);
        else Text(Ellipsize(value, r.width - 8), r.x + 5, r.y + (r.height - theme.fontSize) * 0.5f - 1, theme.text);
        return false;
    }
    bool changed = false;
    int c;
    while ((c = GetCharPressed()) != 0) {
        if (c < 32) continue;
        // encode UTF-8
        std::string enc;
        if (c < 0x80) enc += (char)c;
        else if (c < 0x800) enc += (char)(0xC0 | (c >> 6)), enc += (char)(0x80 | (c & 0x3F));
        else enc += (char)(0xE0 | (c >> 12)), enc += (char)(0x80 | ((c >> 6) & 0x3F)), enc += (char)(0x80 | (c & 0x3F));
        S.editBuffer.insert(S.cursor, enc);
        S.cursor += (int)enc.size();
    }
    auto prevChar = [&]() {
        int p = S.cursor - 1;
        while (p > 0 && (S.editBuffer[p] & 0xC0) == 0x80) p--;
        return p;
    };
    auto nextChar = [&]() {
        int p = S.cursor + 1;
        while (p < (int)S.editBuffer.size() && (S.editBuffer[p] & 0xC0) == 0x80) p++;
        return p;
    };
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && S.cursor > 0) {
        int p = prevChar();
        S.editBuffer.erase(p, S.cursor - p);
        S.cursor = p;
    }
    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) && S.cursor < (int)S.editBuffer.size()) {
        int p = nextChar();
        S.editBuffer.erase(S.cursor, p - S.cursor);
    }
    if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && S.cursor > 0) S.cursor = prevChar();
    if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) && S.cursor < (int)S.editBuffer.size()) S.cursor = nextChar();
    if (IsKeyPressed(KEY_HOME)) S.cursor = 0;
    if (IsKeyPressed(KEY_END)) S.cursor = (int)S.editBuffer.size();
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A)) {
        S.editBuffer.clear();
        S.cursor = 0;
    }
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        const char* clip = GetClipboardText();
        if (clip) {
            std::string cs = clip;
            cs.erase(std::remove(cs.begin(), cs.end(), '\n'), cs.end());
            S.editBuffer.insert(S.cursor, cs);
            S.cursor += (int)cs.size();
        }
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB)) {
        value = S.editBuffer;
        S.focusId.clear();
        changed = true;
        if (committed) *committed = true;
    } else if (IsKeyPressed(KEY_ESCAPE)) {
        S.focusId.clear();
    }
    // draw text with cursor, scrolled so the cursor stays visible
    std::string shown = S.focusId == fid ? S.editBuffer : value;
    float cx = TextWidth(shown.substr(0, std::min((int)shown.size(), S.cursor)));
    float off = std::max(0.0f, cx - (r.width - 12));
    BeginScissorMode((int)r.x + 2, (int)r.y, (int)r.width - 4, (int)r.height);
    float ty = r.y + (r.height - theme.fontSize) * 0.5f - 1;
    Text(shown, r.x + 5 - off, ty, theme.text);
    if (S.focusId == fid && fmod(GetTime(), 1.0) < 0.6) DrawRectangle((int)(r.x + 5 + cx - off), (int)ty, 1, theme.fontSize + 2, theme.text);
    EndScissorMode();
    return changed;
}

bool TextField(const std::string& id, Rectangle r, std::string& value, const char* placeholder) {
    std::string fid = FullId(id);
    bool committed = false;
    std::string before = value;
    EditText(fid, r, value, placeholder, &committed);
    return committed && value != before;
}

// "%.0f" turns -0.2 (or a snapped -0) into "-0": drop the sign when only zeros follow
static std::string FormatNumber(const char* fmt, float v) {
    std::string s = TextFormat(fmt, v);
    if (!s.empty() && s[0] == '-' && s.find_first_not_of("0.", 1) == std::string::npos) s.erase(0, 1);
    return s;
}

bool FloatField(const std::string& id, Rectangle r, float& v, float speed, const char* fmt) {
    std::string fid = FullId(id);
    if (S.focusId == fid) {
        std::string s = FormatNumber(fmt, v);
        bool committed = false;
        EditText(fid, r, s, nullptr, &committed);
        if (committed) {
            char* end = nullptr;
            float nv = strtof(s.c_str(), &end);
            if (end != s.c_str() && nv != v) {
                v = nv;
                return true;
            }
        }
        return false;
    }
    bool hov = Hovered(r);
    bool changed = false;
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        S.dragFloatId = fid;
        S.dragStartX = GetMousePosition().x;
        S.dragStartValue = v;
        S.dragMoved = false;
    }
    if (S.dragFloatId == fid) {
        float dx = GetMousePosition().x - S.dragStartX;
        if (fabsf(dx) > 3) S.dragMoved = true;
        if (S.dragMoved) {
            float nv = S.dragStartValue + dx * speed * (IsKeyDown(KEY_LEFT_SHIFT) ? 0.1f : 1.0f);
            if (nv != v) {
                v = nv;
                changed = true;
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (!S.dragMoved) {
                S.focusId = fid;
                S.editBuffer = FormatNumber(fmt, v);
                S.cursor = (int)S.editBuffer.size();
            }
            S.dragFloatId.clear();
        }
    }
    Fill(r, theme.field);
    Outline(r, hov || S.dragFloatId == fid ? theme.buttonHover : theme.border);
    TextCentered(FormatNumber(fmt, v), r, theme.text);
    if (hov) SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
    return changed;
}

bool Checkbox(const std::string& id, Rectangle r, bool& v, const std::string& label) {
    Rectangle box{r.x, r.y + (r.height - 14) * 0.5f, 14, 14};
    bool hov = Hovered(r);
    Fill(box, v ? theme.accent : theme.field);
    Outline(box, hov ? theme.buttonHover : theme.border);
    if (v) {
        DrawLineEx({box.x + 3, box.y + 7}, {box.x + 6, box.y + 10}, 2, WHITE);
        DrawLineEx({box.x + 6, box.y + 10}, {box.x + 11, box.y + 4}, 2, WHITE);
    }
    Text(label, r.x + 20, r.y + (r.height - theme.fontSize) * 0.5f - 1, theme.text);
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        v = !v;
        return true;
    }
    return false;
}

bool Dropdown(const std::string& id, Rectangle r, std::string& value, const std::vector<std::string>& options) {
    std::string fid = FullId(id);
    bool hov = Hovered(r);
    Fill(r, hov ? theme.buttonHover : theme.button);
    Outline(r, theme.border);
    Text(Ellipsize(value, r.width - 24), r.x + 6, r.y + (r.height - theme.fontSize) * 0.5f - 1, theme.text);
    float ax = r.x + r.width - 12, ay = r.y + r.height * 0.5f;
    DrawTriangle({ax - 4, ay - 2}, {ax, ay + 3}, {ax + 4, ay - 2}, theme.textDim);
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && S.popupId.empty()) {
        S.popupId = fid;
        S.popupAnchor = r;
        S.popupOptions = options;
        S.popupScroll = 0;
        S.popupJustOpened = true;
        // swallow this click for EndFrame's "click outside closes" check
        return false;
    }
    if (S.popupResultId == fid) {
        S.popupResultId.clear();
        if (S.popupResult != value) {
            value = S.popupResult;
            return true;
        }
    }
    return false;
}

bool Header(const std::string& id, Rectangle r, const std::string& label, bool& open) {
    bool hov = Hovered(r);
    Fill(r, hov ? theme.buttonHover : theme.header);
    float ax = r.x + 10, ay = r.y + r.height * 0.5f;
    if (open) DrawTriangle({ax - 4, ay - 2}, {ax, ay + 3}, {ax + 4, ay - 2}, theme.textDim);
    else DrawTriangle({ax - 2, ay - 4}, {ax - 2, ay + 4}, {ax + 3, ay}, theme.textDim);
    Text(label, r.x + 20, r.y + (r.height - theme.fontSize) * 0.5f - 1, theme.text, 0, "ui_bold");
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        open = !open;
        return true;
    }
    return false;
}

bool ColorSwatch(const std::string& id, Rectangle r, Color c) {
    Fill(r, c);
    Outline(r, theme.border);
    return Clicked(r);
}

void Tooltip(const std::string& text) { S.tooltip = text; }

float BeginScroll(const std::string& id, Rectangle r, float contentHeight) {
    std::string fid = FullId(id);
    float& sc = S.scroll[fid];
    if (Hovered(r) && !KeyboardCaptured()) sc -= GetMouseWheelMove() * 48.0f;
    sc = Clampf(sc, 0, std::max(0.0f, contentHeight - r.height));
    S.scrollStack.push_back({fid, r, contentHeight});
    PushClip(r);
    return sc;
}

void EndScroll() {
    if (S.scrollStack.empty()) return;
    auto f = S.scrollStack.back();
    S.scrollStack.pop_back();
    PopClip();
    if (f.content > f.r.height) {
        float sc = S.scroll[f.id];
        float barH = std::max(20.0f, f.r.height * f.r.height / f.content);
        float barY = f.r.y + (f.r.height - barH) * (sc / (f.content - f.r.height));
        DrawRectangleRounded({f.r.x + f.r.width - 5, barY, 4, barH}, 1.0f, 4, Color{90, 100, 120, 180});
    }
}

void PushClip(Rectangle r) {
    if (!S.clipStack.empty()) {
        Rectangle p = S.clipStack.back();
        float x0 = std::max(r.x, p.x), y0 = std::max(r.y, p.y);
        float x1 = std::min(r.x + r.width, p.x + p.width), y1 = std::min(r.y + r.height, p.y + p.height);
        r = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
    S.clipStack.push_back(r);
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
}

void PopClip() {
    if (S.clipStack.empty()) return;
    S.clipStack.pop_back();
    EndScissorMode();
    if (!S.clipStack.empty()) {
        Rectangle r = S.clipStack.back();
        BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    }
}

void StartDrag(const std::string& type, const std::string& payload, const std::string& label) {
    if (S.dragging || S.dragPending) return;
    S.dragPending = true;
    S.dragStart = GetMousePosition();
    S.dragType = type;
    S.dragPayload = payload;
    S.dragLabel = label;
}

bool Dragging() { return S.dragging; }
const std::string& DragType() { return S.dragType; }
const std::string& DragPayload() { return S.dragPayload; }
bool DragReleased() { return S.dragReleased; }
void CancelDrag() {
    S.dragging = S.dragPending = false;
    S.dragType.clear();
}

}  // namespace ui
}  // namespace rbs
