#include "ui/graph.h"
#include "core/common.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace rbs {
namespace ui {
namespace graph {

static const float kMinZoom = 0.35f, kMaxZoom = 2.2f;
static const float kPinRadius = 7;

Vector2 ToScreen(const View& v, Rectangle r, Vector2 g) { return {r.x + v.pan.x + g.x * v.zoom, r.y + v.pan.y + g.y * v.zoom}; }
Vector2 ToGraph(const View& v, Rectangle r, Vector2 s) { return {(s.x - r.x - v.pan.x) / v.zoom, (s.y - r.y - v.pan.y) / v.zoom}; }

static Rectangle NodeRect(const View& v, Rectangle r, const Node& n) {
    Vector2 p = ToScreen(v, r, n.pos);
    return {p.x, p.y, n.size.x * v.zoom, n.size.y * v.zoom};
}

static Vector2 PinPos(const Rectangle& nr) { return {nr.x + nr.width, nr.y + nr.height * 0.5f}; }

// dot of port i of n on the right edge of the node
static Vector2 PortPos(const Rectangle& nr, const Node& n, int i) {
    if (n.ports.size() <= 1 || i < 0) return PinPos(nr);
    return {nr.x + nr.width, nr.y + nr.height * (i + 1) / (float)(n.ports.size() + 1)};
}

struct Segment {
    Vector2 a, b;
};

// Straight arrow between two node rectangles, clipped to their borders. Links between the
// same two nodes (both directions or several) are spread side by side.
static Segment LinkSegment(const Rectangle& ra, const Rectangle& rb, int idA, int idB, float offset) {
    Vector2 ca{ra.x + ra.width * 0.5f, ra.y + ra.height * 0.5f}, cb{rb.x + rb.width * 0.5f, rb.y + rb.height * 0.5f};
    Vector2 d{cb.x - ca.x, cb.y - ca.y};
    float len = sqrtf(d.x * d.x + d.y * d.y);
    if (len < 1e-3f) d = {1, 0}, len = 1;
    d = {d.x / len, d.y / len};
    // the side is taken from the direction low id -> high id so reverse links separate
    Vector2 canon = idA < idB ? d : Vector2{-d.x, -d.y};
    Vector2 perp{-canon.y, canon.x};
    ca = {ca.x + perp.x * offset, ca.y + perp.y * offset};
    cb = {cb.x + perp.x * offset, cb.y + perp.y * offset};
    auto reach = [](const Rectangle& rc, Vector2 dir) {
        float tx = fabsf(dir.x) > 1e-4f ? rc.width * 0.5f / fabsf(dir.x) : 1e9f;
        float ty = fabsf(dir.y) > 1e-4f ? rc.height * 0.5f / fabsf(dir.y) : 1e9f;
        return std::min(tx, ty);
    };
    float ta = reach(ra, d), tb = reach(rb, d);
    return {{ca.x + d.x * ta, ca.y + d.y * ta}, {cb.x - d.x * tb, cb.y - d.y * tb}};
}

static float DistToSegment(Vector2 p, const Segment& s) {
    Vector2 d{s.b.x - s.a.x, s.b.y - s.a.y};
    float l2 = d.x * d.x + d.y * d.y;
    float t = l2 > 1e-6f ? Clampf(((p.x - s.a.x) * d.x + (p.y - s.a.y) * d.y) / l2, 0, 1) : 0;
    float dx = p.x - (s.a.x + d.x * t), dy = p.y - (s.a.y + d.y * t);
    return sqrtf(dx * dx + dy * dy);
}

static const Node* FindNode(const std::vector<Node>& nodes, int id) {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

// What the open menu shows at its current level: the items under the prefix, with
// categories (items that continue with a slash) folded into one row each.
struct MenuEntry {
    std::string label;
    bool category = false, back = false;
    int index = -1;  // into View::menuItems (items only)
};

static std::vector<MenuEntry> MenuEntries(const View& v) {
    std::vector<MenuEntry> out;
    if (!v.menuPrefix.empty()) out.push_back({"<  Back", false, true, -1});
    for (size_t i = 0; i < v.menuItems.size(); ++i) {
        const std::string& s = v.menuItems[i];
        if (s.compare(0, v.menuPrefix.size(), v.menuPrefix) != 0) continue;
        std::string rest = s.substr(v.menuPrefix.size());
        size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            out.push_back({rest, false, false, (int)i});
        } else {
            std::string cat = rest.substr(0, slash);
            bool seen = false;
            for (auto& e : out) seen = seen || (e.category && e.label == cat);
            if (!seen) out.push_back({cat, true, false, -1});
        }
    }
    return out;
}

static Rectangle MenuRect(const View& v) {
    float w = 150;
    auto entries = MenuEntries(v);
    for (auto& e : entries) w = std::max(w, TextWidth(e.label) + 36);
    Rectangle m{v.menuPos.x, v.menuPos.y, w, entries.size() * 22.0f + 6};
    if (m.x + m.width > GetScreenWidth()) m.x = GetScreenWidth() - m.width;
    if (m.y + m.height > GetScreenHeight()) m.y = GetScreenHeight() - m.height;
    return m;
}

void OpenMenu(View& v, const std::vector<std::string>& items, const Events& e) {
    v.menuItems = items;
    v.menuPrefix.clear();
    v.menuPos = GetMousePosition();
    v.menuNode = e.contextNode;
    v.menuLink = e.contextLink;
    v.menuGraphPos = e.contextGraphPos;
    v.menuOpen = !items.empty();
}

void Fit(View& v, Rectangle r, const std::vector<Node>& nodes) {
    if (nodes.empty()) {
        v.pan = {r.width * 0.5f, r.height * 0.5f};
        v.zoom = 1;
        return;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto& n : nodes) {
        x0 = std::min(x0, n.pos.x), y0 = std::min(y0, n.pos.y);
        x1 = std::max(x1, n.pos.x + n.size.x), y1 = std::max(y1, n.pos.y + n.size.y);
    }
    float w = std::max(1.0f, x1 - x0), h = std::max(1.0f, y1 - y0);
    v.zoom = Clampf(std::min((r.width - 80) / w, (r.height - 80) / h), kMinZoom, 1.0f);
    v.pan = {r.width * 0.5f - (x0 + w * 0.5f) * v.zoom, r.height * 0.5f - (y0 + h * 0.5f) * v.zoom};
}

static void DrawArrowHead(Vector2 tip, Vector2 dir, float size, Color c) {
    Vector2 perp{-dir.y, dir.x};
    Vector2 base{tip.x - dir.x * size, tip.y - dir.y * size};
    // DrawTriangle wants counter-clockwise vertices, which depends on the direction: draw both windings
    Vector2 p1{base.x + perp.x * size * 0.55f, base.y + perp.y * size * 0.55f};
    Vector2 p2{base.x - perp.x * size * 0.55f, base.y - perp.y * size * 0.55f};
    DrawTriangle(tip, p1, p2, c);
    DrawTriangle(tip, p2, p1, c);
}

Events Draw(const std::string& id, Rectangle r, View& v, std::vector<Node>& nodes, const std::vector<Link>& links) {
    Events ev;
    Vector2 mouse = GetMousePosition();
    auto excluded = [&](Vector2 p) {
        for (auto& e : v.exclude)
            if (CheckCollisionPointRec(p, e)) return true;
        return false;
    };
    bool menuHit = v.menuOpen && CheckCollisionPointRec(mouse, MenuRect(v));
    bool over = Hovered(r) && !excluded(mouse) && !menuHit;

    // ---- link geometry (needs the node rectangles of this frame) ----
    std::map<std::pair<int, int>, std::vector<size_t>> pairs;
    for (size_t i = 0; i < links.size(); ++i) pairs[{std::min(links[i].from, links[i].to), std::max(links[i].from, links[i].to)}].push_back(i);
    std::vector<Segment> segs(links.size());
    std::vector<bool> valid(links.size(), false);
    for (auto& kv : pairs) {
        for (size_t k = 0; k < kv.second.size(); ++k) {
            size_t i = kv.second[k];
            const Node* a = FindNode(nodes, links[i].from);
            const Node* b = FindNode(nodes, links[i].to);
            if (!a || !b) continue;
            float offset = ((float)k - (kv.second.size() - 1) * 0.5f) * 11.0f * v.zoom;
            if (!a->ports.empty() && links[i].fromPort >= 0) {
                // starts at its port's dot, ends where the line to the target's centre meets its border
                Rectangle ra = NodeRect(v, r, *a), rb = NodeRect(v, r, *b);
                Vector2 p0 = PortPos(ra, *a, links[i].fromPort);
                Vector2 cb{rb.x + rb.width * 0.5f, rb.y + rb.height * 0.5f};
                Vector2 d{cb.x - p0.x, cb.y - p0.y};
                float len = sqrtf(d.x * d.x + d.y * d.y);
                if (len < 1e-3f) d = {-1, 0}, len = 1;
                d = {d.x / len, d.y / len};
                float tx = fabsf(d.x) > 1e-4f ? rb.width * 0.5f / fabsf(d.x) : 1e9f;
                float ty = fabsf(d.y) > 1e-4f ? rb.height * 0.5f / fabsf(d.y) : 1e9f;
                float t = std::min(tx, ty);
                segs[i] = {p0, {cb.x - d.x * t, cb.y - d.y * t}};
            } else {
                segs[i] = LinkSegment(NodeRect(v, r, *a), NodeRect(v, r, *b), a->id, b->id, offset);
            }
            valid[i] = true;
        }
    }

    // ---- hit testing helpers ----
    auto nodeAt = [&](Vector2 p) -> int {
        for (int i = (int)nodes.size() - 1; i >= 0; --i)
            if (CheckCollisionPointRec(p, NodeRect(v, r, nodes[i]))) return nodes[i].id;
        return -1;
    };
    int pinPort = -1;  // port of the dot pinAt found
    auto pinAt = [&](Vector2 p) -> int {
        pinPort = -1;
        for (int i = (int)nodes.size() - 1; i >= 0; --i) {
            if (!nodes[i].out) continue;
            Rectangle nr = NodeRect(v, r, nodes[i]);
            float rad = kPinRadius * std::max(0.8f, v.zoom) + 3;
            int count = std::max<int>(1, (int)nodes[i].ports.size());
            for (int k = 0; k < count; ++k) {
                Vector2 pp = nodes[i].ports.empty() ? PinPos(nr) : PortPos(nr, nodes[i], k);
                if ((p.x - pp.x) * (p.x - pp.x) + (p.y - pp.y) * (p.y - pp.y) <= rad * rad) {
                    pinPort = nodes[i].ports.empty() ? -1 : k;
                    return nodes[i].id;
                }
            }
        }
        return -1;
    };
    auto linkAt = [&](Vector2 p) -> int {
        int best = -1;
        float bestD = 7;
        for (size_t i = 0; i < links.size(); ++i) {
            if (!valid[i]) continue;
            float d = DistToSegment(p, segs[i]);
            if (d < bestD) bestD = d, best = links[i].id;
        }
        return best;
    };

    // ---- input ----
    if (v.menuOpen) {
        Rectangle m = MenuRect(v);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            bool keepOpen = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, m)) {
                auto entries = MenuEntries(v);
                int i = (int)((mouse.y - m.y - 3) / 22.0f);
                if (i >= 0 && i < (int)entries.size()) {
                    const MenuEntry& e = entries[i];
                    if (e.back) {
                        // up one level
                        std::string p = v.menuPrefix.substr(0, v.menuPrefix.size() - 1);
                        size_t slash = p.rfind('/');
                        v.menuPrefix = slash == std::string::npos ? "" : p.substr(0, slash + 1);
                        keepOpen = true;
                    } else if (e.category) {
                        v.menuPrefix += e.label + "/";
                        keepOpen = true;
                    } else {
                        ev.menuChoice = e.index;
                        ev.menuText = v.menuItems[e.index];
                        ev.menuNode = v.menuNode;
                        ev.menuLink = v.menuLink;
                        ev.menuGraphPos = v.menuGraphPos;
                    }
                }
            }
            v.menuOpen = keepOpen;
            if (CheckCollisionPointRec(mouse, m)) over = false;
        }
        if (IsKeyPressed(KEY_ESCAPE)) v.menuOpen = false;
    }

    if (over) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            Vector2 g = ToGraph(v, r, mouse);
            v.zoom = Clampf(v.zoom * powf(1.12f, wheel), kMinZoom, kMaxZoom);
            Vector2 s = ToScreen(v, r, g);
            v.pan.x += mouse.x - s.x;
            v.pan.y += mouse.y - s.y;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !v.menuOpen) {
            v.focused = true;
            int pin = pinAt(mouse);
            int node = pin >= 0 ? pin : nodeAt(mouse);
            if (pin >= 0) {
                v.linkFrom = pin;
                v.linkFromPort = pinPort;
                if (v.selNode != pin) ev.selectionChanged = true;
                v.selNode = pin;
                v.selLink = -1;
            } else if (node >= 0) {
                if (v.selNode != node || v.selLink >= 0) ev.selectionChanged = true;
                v.selNode = node;
                v.selLink = -1;
                const Node* n = FindNode(nodes, node);
                if (n && n->movable) {
                    v.dragNode = node;
                    Vector2 g = ToGraph(v, r, mouse);
                    v.grab = {g.x - n->pos.x, g.y - n->pos.y};
                }
                if (DoubleClicked(r)) ev.doubleClickNode = node;
            } else {
                int link = linkAt(mouse);
                if (v.selLink != link || v.selNode >= 0) ev.selectionChanged = true;
                v.selLink = link;
                v.selNode = -1;
                if (link < 0) v.panning = true;
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) v.panning = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            v.rightPending = true;
            v.rightDown = mouse;
        }
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !menuHit && !v.menuOpen) {
        if (!Hovered(r)) v.focused = false;
    }

    Vector2 delta = GetMouseDelta();
    if (v.dragNode >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        for (auto& n : nodes) {
            if (n.id != v.dragNode) continue;
            Vector2 g = ToGraph(v, r, mouse);
            Vector2 np{g.x - v.grab.x, g.y - v.grab.y};
            if (np.x != n.pos.x || np.y != n.pos.y) ev.moved = true;
            n.pos = np;
        }
    }
    if (v.panning && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))) {
        v.pan.x += delta.x;
        v.pan.y += delta.y;
    }
    if (v.rightPending && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        float dx = mouse.x - v.rightDown.x, dy = mouse.y - v.rightDown.y;
        if (dx * dx + dy * dy > 16) {
            v.panning = true;
            v.moved = true;
        }
        if (v.moved) {
            v.pan.x += delta.x;
            v.pan.y += delta.y;
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && v.rightPending) {
        if (!v.moved) {
            ev.context = true;
            ev.contextNode = nodeAt(mouse);
            ev.contextLink = ev.contextNode >= 0 ? -1 : linkAt(mouse);
            ev.contextGraphPos = ToGraph(v, r, mouse);
            if (ev.contextNode >= 0 && v.selNode != ev.contextNode) v.selNode = ev.contextNode, v.selLink = -1, ev.selectionChanged = true;
            if (ev.contextLink >= 0 && v.selLink != ev.contextLink) v.selLink = ev.contextLink, v.selNode = -1, ev.selectionChanged = true;
        }
        v.rightPending = false;
        v.moved = false;
        v.panning = false;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (v.linkFrom >= 0) {
            int target = nodeAt(mouse);
            const Node* t = FindNode(nodes, target);
            if (target >= 0 && target != v.linkFrom && t && t->in && over) ev.linkFrom = v.linkFrom, ev.linkTo = target, ev.linkFromPort = v.linkFromPort;
            v.linkFrom = -1;
            v.linkFromPort = -1;
        }
        v.dragNode = -1;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) v.panning = false;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) v.panning = false;
    if (v.focused && !KeyboardCaptured() && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && (v.selNode >= 0 || v.selLink >= 0))
        ev.deleteKey = true;

    // ---- drawing ----
    PushClip(r);
    Fill(r, Color{17, 20, 26, 255});
    {
        // grid: minor every 24 units, major every 120
        float minor = 24 * v.zoom, major = 120 * v.zoom;
        if (minor >= 6) {
            float ox = fmodf(v.pan.x, minor), oy = fmodf(v.pan.y, minor);
            for (float x = r.x + ox; x < r.x + r.width; x += minor) DrawLine((int)x, (int)r.y, (int)x, (int)(r.y + r.height), Color{255, 255, 255, 8});
            for (float y = r.y + oy; y < r.y + r.height; y += minor) DrawLine((int)r.x, (int)y, (int)(r.x + r.width), (int)y, Color{255, 255, 255, 8});
        }
        float ox = fmodf(v.pan.x, major), oy = fmodf(v.pan.y, major);
        for (float x = r.x + ox; x < r.x + r.width; x += major) DrawLine((int)x, (int)r.y, (int)x, (int)(r.y + r.height), Color{255, 255, 255, 16});
        for (float y = r.y + oy; y < r.y + r.height; y += major) DrawLine((int)r.x, (int)y, (int)(r.x + r.width), (int)y, Color{255, 255, 255, 16});
    }

    float z = v.zoom;
    for (size_t i = 0; i < links.size(); ++i) {
        if (!valid[i]) continue;
        const Link& l = links[i];
        bool sel = l.id == v.selLink;
        bool hot = over && v.dragNode < 0 && v.linkFrom < 0 && !v.panning && sel == false && linkAt(mouse) == l.id && nodeAt(mouse) < 0;
        Color c = sel ? theme.accent : (hot ? Color{235, 240, 250, 255} : l.color);
        float th = (sel ? 3.0f : 2.0f) * std::max(0.7f, z);
        const Segment& s = segs[i];
        DrawLineEx(s.a, s.b, th, c);
        Vector2 d{s.b.x - s.a.x, s.b.y - s.a.y};
        float len = sqrtf(d.x * d.x + d.y * d.y);
        if (len > 1e-3f) {
            d = {d.x / len, d.y / len};
            DrawArrowHead(s.b, d, 11 * std::max(0.7f, z), c);
            if (l.marker >= 0) {
                float t = Clampf(l.marker, 0, 1);
                DrawCircleV({s.a.x + (s.b.x - s.a.x) * t, s.a.y + (s.b.y - s.a.y) * t}, 4.5f * std::max(0.8f, z), Color{120, 230, 140, 255});
            }
        }
    }

    int hotNode = over && v.dragNode < 0 && !v.panning ? nodeAt(mouse) : -1;
    int fontSize = std::max(7, (int)roundf(theme.fontSize * z));
    for (auto& n : nodes) {
        Rectangle nr = NodeRect(v, r, n);
        if (nr.x > r.x + r.width || nr.y > r.y + r.height || nr.x + nr.width < r.x || nr.y + nr.height < r.y) continue;
        DrawRectangleRounded({nr.x + 2, nr.y + 3, nr.width, nr.height}, 0.25f, 8, Color{0, 0, 0, 90});
        Color c = n.color;
        if (n.id == hotNode) c = {(unsigned char)std::min(255, c.r + 16), (unsigned char)std::min(255, c.g + 16), (unsigned char)std::min(255, c.b + 16), c.a};
        DrawRectangleRounded(nr, 0.25f, 8, c);
        if (n.active) DrawRectangleRoundedLinesEx(nr, 0.25f, 8, 3, Color{120, 230, 140, 255});
        if (n.id == v.selNode) DrawRectangleRoundedLinesEx(nr, 0.25f, 8, 2, Color{255, 255, 255, 235});
        else if (!n.active) DrawRectangleRoundedLinesEx(nr, 0.25f, 8, 1, Color{255, 255, 255, 50});
        bool sub = !n.subtitle.empty() && z >= 0.55f;
        float th = (float)fontSize;
        float ty = nr.y + (sub ? nr.height * 0.5f - th - 1 : (nr.height - th) * 0.5f - 1);
        Text(Ellipsize(n.title, nr.width - 12, fontSize), nr.x + (nr.width - std::min(TextWidth(n.title, fontSize, "ui_bold"), nr.width - 12)) * 0.5f, ty, WHITE, fontSize, "ui_bold");
        if (sub) {
            int ss = std::max(7, (int)roundf(11 * z));
            std::string s = Ellipsize(n.subtitle, nr.width - 12, ss);
            Text(s, nr.x + (nr.width - TextWidth(s, ss)) * 0.5f, nr.y + nr.height * 0.5f + 2, Color{255, 255, 255, 170}, ss);
        }
        if (!n.badge.empty() && z >= 0.55f) {
            int bs = std::max(7, (int)roundf(10 * z));
            Text(n.badge, nr.x + 6 * z, nr.y + 3 * z, Color{255, 255, 255, 150}, bs);
        }
        if (n.out && n.ports.size() >= 2) {
            // every port has its dot and its label
            int ls = std::max(7, (int)roundf(10 * z));
            for (size_t k = 0; k < n.ports.size(); ++k) {
                Vector2 pp = PortPos(nr, n, (int)k);
                bool active = n.id == v.linkFrom && (int)k == v.linkFromPort;
                float rad = 5.5f * std::max(0.8f, z);
                DrawCircleV(pp, rad + 1.5f, Color{20, 24, 32, 255});
                DrawCircleV(pp, rad, active ? theme.accent : Color{200, 205, 215, 255});
                if (z >= 0.55f && !n.ports[k].empty()) Text(n.ports[k], pp.x - rad - 6 - TextWidth(n.ports[k], ls), pp.y - ls * 0.5f - 1, Color{255, 255, 255, 190}, ls);
            }
        } else if (n.out && (n.id == hotNode || n.id == v.selNode || n.id == v.linkFrom)) {
            Vector2 pp = n.ports.empty() ? PinPos(nr) : PortPos(nr, n, 0);
            DrawCircleV(pp, kPinRadius * std::max(0.8f, z), Color{20, 24, 32, 255});
            DrawCircleV(pp, kPinRadius * std::max(0.8f, z) - 2, n.id == v.linkFrom ? theme.accent : Color{200, 205, 215, 255});
        }
    }

    if (v.linkFrom >= 0) {
        if (const Node* a = FindNode(nodes, v.linkFrom)) {
            Vector2 p = a->ports.empty() ? PinPos(NodeRect(v, r, *a)) : PortPos(NodeRect(v, r, *a), *a, v.linkFromPort);
            int target = nodeAt(mouse);
            const Node* t = FindNode(nodes, target);
            bool ok = t && t->in && target != v.linkFrom;
            Color c = ok ? Color{120, 230, 140, 255} : theme.accent;
            DrawLineEx(p, mouse, 2.5f, c);
            Vector2 d{mouse.x - p.x, mouse.y - p.y};
            float len = sqrtf(d.x * d.x + d.y * d.y);
            if (len > 12) DrawArrowHead(mouse, {d.x / len, d.y / len}, 10, c);
        }
    }
    PopClip();
    Outline(r, theme.border);

    // ---- context menu (on top of everything in the canvas) ----
    if (v.menuOpen) {
        Rectangle m = MenuRect(v);
        Block(m);
        Fill({m.x + 3, m.y + 3, m.width, m.height}, Color{0, 0, 0, 110});
        Fill(m, theme.panelAlt);
        Outline(m, theme.border);
        auto entries = MenuEntries(v);
        for (size_t i = 0; i < entries.size(); ++i) {
            Rectangle ir{m.x + 2, m.y + 3 + i * 22.0f, m.width - 4, 22};
            if (CheckCollisionPointRec(mouse, ir)) Fill(ir, theme.accentDim);
            Text(entries[i].label, ir.x + 8, ir.y + 4, entries[i].back ? theme.textDim : theme.text);
            if (entries[i].category) Text(">", ir.x + ir.width - 14, ir.y + 4, theme.textDim);
        }
    }
    (void)id;
    return ev;
}

}  // namespace graph
}  // namespace ui
}  // namespace rbs
