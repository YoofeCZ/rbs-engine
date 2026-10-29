// RBS Engine - node graph canvas used by the editor's graph documents (animators now,
// behavior trees and quests later). The widget only draws and handles the mouse: nodes,
// links and their meaning belong to the document, which gets the changes as Events.
#pragma once

#include "ui/imgui.h"

#include <string>
#include <vector>

namespace rbs {
namespace ui {
namespace graph {

struct Node {
    int id = 0;
    std::string title, subtitle, badge;
    Vector2 pos{};  // graph coordinates (top-left corner)
    Vector2 size{150, 40};
    Color color{70, 80, 100, 255};
    // Output ports, top to bottom on the right edge ("yes", "no", the options of a dialogue
    // choice...). Empty: one unnamed output and links run centre to centre (animators,
    // behavior trees); one or more: every link starts at its port's dot.
    std::vector<std::string> ports;
    bool out = true;       // can start a link (drag from the dot on its right edge)
    bool in = true;        // can receive a link
    bool active = false;   // highlighted, e.g. the state an animator is in right now
    bool movable = true;
};

struct Link {
    int id = 0;
    int from = 0, to = 0;  // node ids
    int fromPort = -1;     // index into the source node's ports (-1: none)
    Color color{190, 195, 205, 255};
    float marker = -1;     // 0..1: draws a dot travelling along the link (a transition in progress)
};

// Per document view state: owned by the document, kept between frames.
struct View {
    Vector2 pan{60, 60};    // graph origin on the canvas, in pixels
    float zoom = 1;
    int selNode = -1, selLink = -1;
    std::vector<Rectangle> exclude;  // screen rectangles covered by overlays (the canvas ignores the mouse there)
    // interaction (managed by Draw)
    int dragNode = -1, linkFrom = -1, linkFromPort = -1;
    Vector2 grab{}, rightDown{};
    bool panning = false, rightPending = false, focused = false, moved = false;
    // context menu opened by the document with OpenMenu
    std::vector<std::string> menuItems;  // "Add/Action/Move to": slashes make nested categories
    std::string menuPrefix;              // category the open menu is in ("Add/Action/")
    Vector2 menuPos{};
    int menuNode = -1, menuLink = -1;
    Vector2 menuGraphPos{};
    bool menuOpen = false;
};

struct Events {
    bool moved = false;                // a node was dragged (position changed)
    bool selectionChanged = false;
    int linkFrom = -1, linkTo = -1;    // a link was dragged from one node onto another
    int linkFromPort = -1;             // ... starting at this port of the source node (-1: it has none)
    bool deleteKey = false;            // Delete pressed while the canvas had focus
    bool context = false;              // right click: open a menu (see OpenMenu)
    int contextNode = -1, contextLink = -1;
    Vector2 contextGraphPos{};         // where in the graph it happened
    int menuChoice = -1;               // index chosen from the open menu
    std::string menuText;              // ... and its full text
    int menuNode = -1, menuLink = -1;  // what the menu was opened on
    Vector2 menuGraphPos{};
    int doubleClickNode = -1;
};

// Draws the canvas in `r` and handles input. Node positions are updated in place.
Events Draw(const std::string& id, Rectangle r, View& v, std::vector<Node>& nodes, const std::vector<Link>& links);

// Opens a context menu at the mouse (call from the Events::context handler).
void OpenMenu(View& v, const std::vector<std::string>& items, const Events& e);

// Centres the view on the nodes (and zooms out when they do not fit).
void Fit(View& v, Rectangle r, const std::vector<Node>& nodes);

Vector2 ToGraph(const View& v, Rectangle r, Vector2 screen);
Vector2 ToScreen(const View& v, Rectangle r, Vector2 graph);

}  // namespace graph
}  // namespace ui
}  // namespace rbs
