// RBS Engine - small immediate mode UI toolkit used by the editor.
#pragma once

#include <raylib.h>
#include <string>
#include <vector>

namespace rbs {
namespace ui {

struct Theme {
    Color bg{20, 23, 30, 255};
    Color panel{27, 31, 40, 255};
    Color panelAlt{32, 37, 47, 255};
    Color header{37, 43, 55, 255};
    Color border{52, 59, 74, 255};
    Color field{16, 19, 25, 255};
    Color button{45, 52, 66, 255};
    Color buttonHover{58, 67, 85, 255};
    Color accent{56, 120, 245, 255};
    Color accentDim{40, 78, 150, 255};
    Color text{208, 214, 224, 255};
    Color textDim{128, 137, 153, 255};
    Color warn{240, 180, 60, 255};
    Color danger{220, 70, 60, 255};
    int fontSize = 14;
};
extern Theme theme;

void BeginFrame();
void EndFrame();  // draws popups/tooltips/drag ghost on top
bool MouseOverUI();       // mouse is over a registered panel or popup (from the previous frame)
bool KeyboardCaptured();  // a text field has focus
void Block(Rectangle r);  // register a rectangle that blocks viewport input
void ClearFocus();

// drawing helpers
void Fill(Rectangle r, Color c);
void Outline(Rectangle r, Color c, float thick = 1);
void Text(const std::string& s, float x, float y, Color c, int size = 0, const char* font = "ui");
void TextCentered(const std::string& s, Rectangle r, Color c, int size = 0, const char* font = "ui");
float TextWidth(const std::string& s, int size = 0, const char* font = "ui");
std::string Ellipsize(const std::string& s, float maxWidth, int size = 0);

// widgets (ids must be unique within the frame; use PushId for repeated rows)
void PushId(const std::string& id);
void PopId();
bool Hovered(Rectangle r);
bool Clicked(Rectangle r, int button = MOUSE_BUTTON_LEFT);
bool DoubleClicked(Rectangle r);
bool Button(const std::string& id, Rectangle r, const std::string& label, bool selected = false, bool enabled = true);
bool TextField(const std::string& id, Rectangle r, std::string& value, const char* placeholder = nullptr);
bool FloatField(const std::string& id, Rectangle r, float& v, float speed = 1.0f, const char* fmt = "%.2f");
bool Checkbox(const std::string& id, Rectangle r, bool& v, const std::string& label);
bool Dropdown(const std::string& id, Rectangle r, std::string& value, const std::vector<std::string>& options);
bool Header(const std::string& id, Rectangle r, const std::string& label, bool& open);
bool ColorSwatch(const std::string& id, Rectangle r, Color c);
void Tooltip(const std::string& text);

// scrolling region: content is drawn with the returned y offset applied by the caller
float BeginScroll(const std::string& id, Rectangle r, float contentHeight);
void EndScroll();
void PushClip(Rectangle r);
void PopClip();

// drag & drop (project browser -> viewport etc.)
void StartDrag(const std::string& type, const std::string& payload, const std::string& label);
bool Dragging();
const std::string& DragType();
const std::string& DragPayload();
bool DragReleased();  // true on the frame the mouse button was released while dragging
void CancelDrag();

}  // namespace ui
}  // namespace rbs
