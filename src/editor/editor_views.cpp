// RBS Engine - editor viewports: top/front/side orthographic views and the 3D view.
#include "editor/editor_impl.h"
#include "core/log.h"
#include "core/vfs.h"
#include "render/assets.h"
#include "ui/fonts.h"
#include "ui/imgui.h"

#include <rlgl.h>

namespace rbs {

static const Color kBrushCol{110, 150, 235, 255};
static const Color kSelCol{255, 70, 50, 255};
static const Color kSelCol2{255, 200, 60, 255};

int Editor::Impl::AxisU(const EditorView& v) const { return v.kind == ViewKind::Side ? 2 : 0; }
int Editor::Impl::AxisV(const EditorView& v) const { return v.kind == ViewKind::Top ? 2 : 1; }
int Editor::Impl::AxisDepth(const EditorView& v) const { return v.kind == ViewKind::Top ? 1 : (v.kind == ViewKind::Front ? 2 : 0); }

Vector2 Editor::Impl::WorldToScreen2D(const EditorView& v, Vector3 p) const {
    float u = Comp(p, AxisU(v)), w = Comp(p, AxisV(v));
    bool flip = v.kind != ViewKind::Top;
    float sx = v.rect.x + v.rect.width * 0.5f + (u - v.center.x) * v.zoom;
    float sy = v.rect.y + v.rect.height * 0.5f + (flip ? -(w - v.center.y) : (w - v.center.y)) * v.zoom;
    return {sx, sy};
}

Vector3 Editor::Impl::ScreenToWorld2D(const EditorView& v, Vector2 s, Vector3 keep) const {
    bool flip = v.kind != ViewKind::Top;
    float u = v.center.x + (s.x - (v.rect.x + v.rect.width * 0.5f)) / v.zoom;
    float dy = (s.y - (v.rect.y + v.rect.height * 0.5f)) / v.zoom;
    float w = v.center.y + (flip ? -dy : dy);
    Vector3 p = keep;
    SetComp(p, AxisU(v), u);
    SetComp(p, AxisV(v), w);
    return p;
}

void Editor::Impl::FitViews() {
    BoundingBox bb{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (auto& b : map.brushes) bb.min = VMin(bb.min, b.bounds.min), bb.max = VMax(bb.max, b.bounds.max);
    for (size_t i = 0; i < map.entities.size(); ++i) bb.min = VMin(bb.min, map.entities[i].origin), bb.max = VMax(bb.max, map.entities[i].origin);
    if (bb.min.x > bb.max.x) bb = {{-512, -64, -512}, {512, 256, 512}};
    Vector3 c = (bb.min + bb.max) * 0.5f, size = bb.max - bb.min;
    for (auto& v : views) {
        if (v.kind == ViewKind::Persp || v.rect.width <= 0) continue;
        v.center = {Comp(c, AxisU(v)), Comp(c, AxisV(v))};
        float zu = v.rect.width / std::max(64.0f, Comp(size, AxisU(v)) * 1.1f);
        float zv = v.rect.height / std::max(64.0f, Comp(size, AxisV(v)) * 1.1f);
        v.zoom = Clampf(std::min(zu, zv), 0.01f, 4.0f);
    }
}

void Editor::Impl::LayoutViews(Rectangle a) {
    if (maximized >= 0) {
        for (int i = 0; i < 4; ++i) views[i].rect = i == maximized ? a : Rectangle{0, 0, 0, 0};
        return;
    }
    float hw = floorf(a.width * 0.5f), hh = floorf(a.height * 0.5f);
    views[0].rect = {a.x, a.y, hw - 1, hh - 1};
    views[1].rect = {a.x + hw + 1, a.y, a.width - hw - 1, hh - 1};
    views[2].rect = {a.x, a.y + hh + 1, hw - 1, a.height - hh - 1};
    views[3].rect = {a.x + hw + 1, a.y + hh + 1, a.width - hw - 1, a.height - hh - 1};
}

// ---------------------------------------------------------------------------------
// picking
// ---------------------------------------------------------------------------------
static bool RayBrush(Ray ray, const Brush& b, float* tHit, int* faceHit) {
    float tEnter = -1e30f, tExit = 1e30f;
    int face = -1;
    for (size_t i = 0; i < b.faces.size(); ++i) {
        const Plane& p = b.faces[i].plane;
        float denom = Dot(p.n, ray.direction);
        float dist = p.d - Dot(p.n, ray.position);
        if (fabsf(denom) < 1e-7f) {
            if (dist < 0) return false;
            continue;
        }
        float t = dist / denom;
        if (denom < 0) {
            if (t > tEnter) tEnter = t, face = (int)i;
        } else if (t < tExit) {
            tExit = t;
        }
        if (tEnter > tExit) return false;
    }
    if (tExit < 0 || face < 0) return false;
    *tHit = std::max(0.0f, tEnter);
    *faceHit = face;
    return true;
}

bool Editor::Impl::PickRay(Ray ray, int* brush, int* face, int* ent, float* dist) const {
    float best = 1e30f;
    *brush = *face = *ent = -1;
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        float t;
        int f;
        if (RayBrush(ray, map.brushes[i], &t, &f) && t < best) {
            best = t;
            *brush = (int)i;
            *face = f;
        }
    }
    for (size_t i = 0; i < map.entities.size(); ++i) {
        bool isBrushEnt = false;
        for (auto& b : map.brushes)
            if (b.entity == (int)i) isBrushEnt = true;
        if (isBrushEnt) continue;
        RayCollision rc = GetRayCollisionBox(ray, EntityBox((int)i));
        if (rc.hit && rc.distance < best) {
            best = rc.distance;
            *ent = (int)i;
            *brush = *face = -1;
        }
    }
    if (dist) *dist = best;
    return best < 1e29f;
}

static std::vector<Vector2> Hull(std::vector<Vector2> p) {
    if (p.size() < 3) return p;
    std::sort(p.begin(), p.end(), [](Vector2 a, Vector2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Vector2> h(p.size() * 2);
    size_t k = 0;
    auto cross = [](Vector2 o, Vector2 a, Vector2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) k--;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) k--;
        h[k++] = p[i - 1];
    }
    h.resize(k > 0 ? k - 1 : 0);
    return h;
}

static bool PointInHull(const std::vector<Vector2>& h, Vector2 p) {
    if (h.size() < 3) return false;
    for (size_t i = 0; i < h.size(); ++i) {
        Vector2 a = h[i], b = h[(i + 1) % h.size()];
        if ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) < 0) return false;
    }
    return true;
}

bool Editor::Impl::Pick2D(int vi, Vector2 mouse, bool add) {
    const EditorView& v = views[vi];
    int bestEnt = -1, bestBrush = -1;
    float bestEntArea = 1e30f, bestBrushArea = 1e30f;
    for (size_t i = 0; i < map.entities.size(); ++i) {
        BoundingBox bb = EntityBox((int)i);
        bool isBrushEnt = false;
        for (auto& b : map.brushes)
            if (b.entity == (int)i) isBrushEnt = true;
        if (isBrushEnt) continue;
        Vector2 a = WorldToScreen2D(v, bb.min), b = WorldToScreen2D(v, bb.max);
        Rectangle r{std::min(a.x, b.x) - 3, std::min(a.y, b.y) - 3, fabsf(b.x - a.x) + 6, fabsf(b.y - a.y) + 6};
        if (CheckCollisionPointRec(mouse, r) && r.width * r.height < bestEntArea) {
            bestEntArea = r.width * r.height;
            bestEnt = (int)i;
        }
    }
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        std::vector<Vector2> pts;
        for (auto& f : map.brushes[i].faces)
            for (auto& p : f.poly) pts.push_back(WorldToScreen2D(v, p));
        auto h = Hull(pts);
        if (!PointInHull(h, mouse)) continue;
        Vector2 a = WorldToScreen2D(v, map.brushes[i].bounds.min), b = WorldToScreen2D(v, map.brushes[i].bounds.max);
        float area = fabsf(b.x - a.x) * fabsf(b.y - a.y);
        if (area < bestBrushArea) {
            bestBrushArea = area;
            bestBrush = (int)i;
        }
    }
    if (!add) ClearSelection();
    if (bestEnt < 0 && bestBrush < 0) return false;
    if (bestEnt >= 0) {
        if (add && selEnts.count(bestEnt)) selEnts.erase(bestEnt);
        else selEnts.insert(bestEnt);
    } else if (bestBrush >= 0) {
        int owner = map.brushes[bestBrush].entity;
        if (owner >= 0 && !IsKeyDown(KEY_LEFT_ALT)) {
            SelectAllOfEntity(owner);
        } else if (add && selBrushes.count(bestBrush)) {
            selBrushes.erase(bestBrush);
        } else {
            selBrushes.insert(bestBrush);
        }
    }
    return true;
}

Vector3 Editor::Impl::PlacementPoint(int vi, Vector2 mouse, const ClassInfo* ci) {
    EditorView& v = views[vi];
    Vector3 mins = ci ? ci->mins : Vector3{-8, -8, -8};
    if (v.kind == ViewKind::Persp) {
        Vector2 local{mouse.x - v.rect.x, mouse.y - v.rect.y};
        Ray ray = GetScreenToWorldRayEx(local, v.Camera(), (int)v.rect.width, (int)v.rect.height);
        int b, f, e;
        float d;
        if (PickRay(ray, &b, &f, &e, &d) && d < 1e29f) {
            Vector3 hit = ray.position + ray.direction * d;
            Vector3 n = (b >= 0 && f >= 0) ? map.brushes[b].faces[f].plane.n : Vector3{0, 1, 0};
            Vector3 p = hit;
            // push the box out of the surface
            p.x -= n.x > 0.5f ? mins.x : (n.x < -0.5f ? (ci ? ci->maxs.x : 8) : 0);
            p.y -= n.y > 0.5f ? mins.y : (n.y < -0.5f ? (ci ? ci->maxs.y : 8) : 0);
            p.z -= n.z > 0.5f ? mins.z : (n.z < -0.5f ? (ci ? ci->maxs.z : 8) : 0);
            p.x = Snap(p.x, grid);
            p.z = Snap(p.z, grid);
            return p;
        }
        return SnapV(v.camPos + AnglesForward(v.pitch, v.yaw) * 256.0f);
    }
    Vector3 keep{0, -mins.y, 0};
    BoundingBox bb;
    if (SelectionBounds(bb)) keep = (bb.min + bb.max) * 0.5f;
    return SnapV(ScreenToWorld2D(v, mouse, keep));
}

void Editor::Impl::HandleDrop(int vi) {
    if (!ui::DragReleased() || vi < 0) return;
    Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, views[vi].rect)) return;
    DropAsset(vi, mouse, ui::DragType(), ui::DragPayload());
}

// Places an asset dropped into a view: from the Project panel or imported from the OS.
void Editor::Impl::DropAsset(int vi, Vector2 mouse, const std::string& type, const std::string& payload) {
    EditorView& v = views[vi];
    if (type == "texture") {
        if (v.kind == ViewKind::Persp) {
            Ray ray = GetScreenToWorldRayEx({mouse.x - v.rect.x, mouse.y - v.rect.y}, v.Camera(), (int)v.rect.width, (int)v.rect.height);
            int b, f, e;
            if (PickRay(ray, &b, &f, &e, nullptr) && b >= 0) {
                Snapshot();
                ApplyTexture(payload, b, IsKeyDown(KEY_LEFT_SHIFT) ? -1 : f);
            }
        }
        curTexture = payload;
        return;
    }
    if (type == "class") {
        curClass = payload;
        int e = CreateEntity(payload, PlacementPoint(vi, mouse, ClassFor(payload)));
        if (e >= 0) {
            ClearSelection();
            SelectAllOfEntity(e);
        }
        return;
    }
    if (type == "model" || type == "sprite" || type == "sound") {
        const char* cls = type == "model" ? "prop_model" : (type == "sprite" ? "sprite" : "ambient_sound");
        int e = CreateEntity(cls, PlacementPoint(vi, mouse, ClassFor(cls)));
        if (e >= 0) {
            map.entities[e].props[type == "model" ? "model" : (type == "sprite" ? "texture" : "sound")] = payload;
            if (type == "model") {
                // stand the model on the surface: its origin is at the feet
                ModelAsset& ma = assets::GetModel(payload);
                map.entities[e].origin.y += -ClassFor(cls)->mins.y + ma.bounds.min.y * 0;
            }
            ClearSelection();
            selEnts.insert(e);
            Changed(false);
        }
        return;
    }
    if (type == "prefab") {
        InsertPrefab(payload, PlacementPoint(vi, mouse, nullptr));
        return;
    }
    if (type == "component") {
        int target = -1;
        if (v.kind == ViewKind::Persp) {
            Ray ray = GetScreenToWorldRayEx({mouse.x - v.rect.x, mouse.y - v.rect.y}, v.Camera(), (int)v.rect.width, (int)v.rect.height);
            int b, f, e;
            if (PickRay(ray, &b, &f, &e, nullptr)) target = e >= 0 ? e : (b >= 0 ? map.brushes[b].entity : -1);
        } else if (selEnts.size() == 1) {
            target = *selEnts.begin();
        }
        if (target >= 0) {
            Snapshot();
            map.entities[target].components.push_back({{"type", payload}, {"props", json::object()}});
            ClearSelection();
            selEnts.insert(target);
            Changed(false);
            SetStatus("Added component " + payload);
        } else {
            SetStatus("Drop components onto an entity in the 3D view");
        }
    }
}

// ---------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------
void Editor::Impl::UpdateViews(bool keyboardFree) {
    Vector2 mouse = GetMousePosition();
    if (drag == Drag::None) {
        hotView = -1;
        if (!ui::MouseOverUI())
            for (int i = 0; i < 4; ++i)
                if (views[i].rect.width > 0 && CheckCollisionPointRec(mouse, views[i].rect)) hotView = i;
    }
    int vi = drag != Drag::None ? dragView : hotView;
    if (vi < 0) return;
    if (views[vi].kind == ViewKind::Persp) UpdateView3D(vi, keyboardFree);
    else UpdateView2D(vi);
}

static const int kHandleNone = -1;

static std::vector<Vector2> HandlePositions(Rectangle r) {
    return {{r.x, r.y}, {r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, {r.x, r.y + r.height},
            {r.x + r.width * 0.5f, r.y}, {r.x + r.width, r.y + r.height * 0.5f}, {r.x + r.width * 0.5f, r.y + r.height}, {r.x, r.y + r.height * 0.5f}};
}

void Editor::Impl::UpdateView2D(int vi) {
    EditorView& v = views[vi];
    Vector2 mouse = GetMousePosition();
    // zoom around the cursor
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && drag == Drag::None) {
        Vector3 before = ScreenToWorld2D(v, mouse, {0, 0, 0});
        v.zoom = Clampf(v.zoom * powf(1.18f, wheel), 0.01f, 16.0f);
        Vector3 after = ScreenToWorld2D(v, mouse, {0, 0, 0});
        v.center.x += Comp(before, AxisU(v)) - Comp(after, AxisU(v));
        v.center.y += Comp(before, AxisV(v)) - Comp(after, AxisV(v));
    }
    // pan
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && drag == Drag::None)) {
        drag = Drag::Pan;
        dragView = vi;
    }
    if (drag == Drag::Pan) {
        Vector2 d = GetMouseDelta();
        v.center.x -= d.x / v.zoom;
        v.center.y -= (v.kind == ViewKind::Top ? d.y : -d.y) / v.zoom;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) drag = Drag::None;
        return;
    }

    Vector3 keep{0, 0, 0};
    BoundingBox selBB;
    bool haveSel = SelectionBounds(selBB);
    if (haveSel) keep = (selBB.min + selBB.max) * 0.5f;
    Vector3 world = ScreenToWorld2D(v, mouse, keep);
    int au = AxisU(v), av = AxisV(v), ad = AxisDepth(v);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // selection handle under the mouse
    int hoverHandle = kHandleNone;
    Rectangle selRect{};
    if (haveSel && (tool == Tool::Select || tool == Tool::Texture || tool == Tool::Face)) {
        Vector2 a = WorldToScreen2D(v, selBB.min), b = WorldToScreen2D(v, selBB.max);
        selRect = {std::min(a.x, b.x), std::min(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y)};
        auto hp = HandlePositions(selRect);
        for (int i = 0; i < 8; ++i)
            if (fabsf(mouse.x - hp[i].x) < 6 && fabsf(mouse.y - hp[i].y) < 6) hoverHandle = i;
        if (hoverHandle >= 0) SetMouseCursor(hoverHandle >= 4 ? (hoverHandle % 2 == 0 ? MOUSE_CURSOR_RESIZE_NS : MOUSE_CURSOR_RESIZE_EW) : MOUSE_CURSOR_RESIZE_ALL);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && drag == Drag::None) {
        dragView = vi;
        dragStartMouse = mouse;
        dragStartWorld = world;
        dragLastApplied = {0, 0, 0};
        switch (tool) {
            case Tool::Select:
            case Tool::Texture:
            case Tool::Face: {
                if (hoverHandle >= 0) {
                    drag = Drag::Resize;
                    resizeHandle = hoverHandle;
                    dragStartBounds = selBB;
                    Snapshot();
                } else if (haveSel && CheckCollisionPointRec(mouse, selRect) && !shift) {
                    drag = Drag::Move;
                    Snapshot();
                } else {
                    bool hit = Pick2D(vi, mouse, shift);
                    if (!hit) {
                        drag = Drag::Marquee;
                    } else if (!shift) {
                        drag = Drag::Move;
                        Snapshot();
                    }
                }
                break;
            }
            case Tool::Brush: drag = Drag::Create; break;
            case Tool::Entity: {
                int e = CreateEntity(curClass, PlacementPoint(vi, mouse, ClassFor(curClass)));
                if (e >= 0) {
                    ClearSelection();
                    SelectAllOfEntity(e);
                    SetStatus("Placed " + curClass);
                }
                break;
            }
            case Tool::Clip: {
                if (clipView != vi) clipPoints.clear();
                clipView = vi;
                if (clipPoints.size() >= 2) clipPoints.clear();
                clipPoints.push_back(SnapV(world));
                break;
            }
        }
    }

    if (drag == Drag::Move) {
        Vector3 d = world - dragStartWorld;
        Vector3 snapped{0, 0, 0};
        SetComp(snapped, au, Snap(Comp(d, au), grid));
        SetComp(snapped, av, Snap(Comp(d, av), grid));
        Vector3 inc = snapped - dragLastApplied;
        if (Len(inc) > 0) {
            MoveSelection(inc);
            dragLastApplied = snapped;
        }
    } else if (drag == Drag::Resize) {
        BoundingBox nb = dragStartBounds;
        // handle -> which edges move. corners: 0 TL,1 TR,2 BR,3 BL ; edges: 4 T,5 R,6 B,7 L (screen space)
        bool left = resizeHandle == 0 || resizeHandle == 3 || resizeHandle == 7;
        bool right = resizeHandle == 1 || resizeHandle == 2 || resizeHandle == 5;
        bool top = resizeHandle == 0 || resizeHandle == 1 || resizeHandle == 4;
        bool bottom = resizeHandle == 2 || resizeHandle == 3 || resizeHandle == 6;
        float wu = Snap(Comp(world, au), grid), wv = Snap(Comp(world, av), grid);
        bool flip = v.kind != ViewKind::Top;  // screen "top" = max V for front/side views
        if (left) SetComp(nb.min, au, std::min(wu, Comp(nb.max, au) - grid));
        if (right) SetComp(nb.max, au, std::max(wu, Comp(nb.min, au) + grid));
        if (top) {
            if (flip) SetComp(nb.max, av, std::max(wv, Comp(nb.min, av) + grid));
            else SetComp(nb.min, av, std::min(wv, Comp(nb.max, av) - grid));
        }
        if (bottom) {
            if (flip) SetComp(nb.min, av, std::min(wv, Comp(nb.max, av) - grid));
            else SetComp(nb.max, av, std::max(wv, Comp(nb.min, av) + grid));
        }
        BoundingBox cur;
        SelectionBounds(cur);
        if (Len(nb.min - cur.min) > 0.01f || Len(nb.max - cur.max) > 0.01f) ResizeSelection(cur, nb);
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && drag != Drag::None) {
        if (drag == Drag::Move && Len(dragLastApplied) < 0.001f && !undo.empty()) undo.pop_back();  // nothing moved
        if (drag == Drag::Marquee) {
            Rectangle mr{std::min(dragStartMouse.x, mouse.x), std::min(dragStartMouse.y, mouse.y), fabsf(mouse.x - dragStartMouse.x),
                         fabsf(mouse.y - dragStartMouse.y)};
            if (mr.width > 4 && mr.height > 4) {
                if (!shift) ClearSelection();
                auto inside = [&](BoundingBox bb) {
                    Vector2 a = WorldToScreen2D(v, bb.min), b = WorldToScreen2D(v, bb.max);
                    return std::min(a.x, b.x) >= mr.x && std::max(a.x, b.x) <= mr.x + mr.width && std::min(a.y, b.y) >= mr.y &&
                           std::max(a.y, b.y) <= mr.y + mr.height;
                };
                for (size_t i = 0; i < map.brushes.size(); ++i)
                    if (inside(map.brushes[i].bounds)) selBrushes.insert((int)i);
                for (size_t i = 0; i < map.entities.size(); ++i)
                    if (inside(EntityBox((int)i))) selEnts.insert((int)i);
            }
        }
        if (drag == Drag::Create) {
            Vector3 a = SnapV(dragStartWorld), b = SnapV(world);
            if (fabsf(Comp(a, au) - Comp(b, au)) >= 1 && fabsf(Comp(a, av) - Comp(b, av)) >= 1) {
                float d0 = newBrushMin, d1 = newBrushMax;
                if (haveSel) {
                    d0 = Comp(selBB.min, ad);
                    d1 = Comp(selBB.max, ad);
                    if (d1 - d0 < 1) d1 = d0 + 64;
                } else if (v.kind != ViewKind::Top) {
                    d0 = -64;
                    d1 = 64;
                }
                SetComp(a, ad, d0);
                SetComp(b, ad, d1);
                int nb = CreateBrush(a, b);
                if (nb >= 0) {
                    ClearSelection();
                    selBrushes.insert(nb);
                    SetStatus("Brush created");
                }
            } else {
                Pick2D(vi, mouse, shift);
            }
        }
        drag = Drag::None;
    }
}

void Editor::Impl::UpdateView3D(int vi, bool keyboardFree) {
    EditorView& v = views[vi];
    Vector2 mouse = GetMousePosition();
    float dt = GetFrameTime();
    // fly camera while RMB is held
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && drag == Drag::None) {
        drag = Drag::Look;
        dragView = vi;
        DisableCursor();
    }
    if (drag == Drag::Look) {
        Vector2 d = GetMouseDelta();
        v.yaw -= d.x * 0.15f;
        v.pitch = Clampf(v.pitch - d.y * 0.15f, -89, 89);
        float speed = (IsKeyDown(KEY_LEFT_SHIFT) ? 1500.0f : 500.0f) * dt;
        Vector3 f = AnglesForward(v.pitch, v.yaw), r = YawRight(v.yaw);
        if (IsKeyDown(KEY_W)) v.camPos += f * speed;
        if (IsKeyDown(KEY_S)) v.camPos -= f * speed;
        if (IsKeyDown(KEY_D)) v.camPos += r * speed;
        if (IsKeyDown(KEY_A)) v.camPos -= r * speed;
        if (IsKeyDown(KEY_E)) v.camPos.y += speed;
        if (IsKeyDown(KEY_Q)) v.camPos.y -= speed;
        v.camPos += f * GetMouseWheelMove() * 64.0f;
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            drag = Drag::None;
            EnableCursor();
        }
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && drag == Drag::None) {
        drag = Drag::Pan;
        dragView = vi;
    }
    if (drag == Drag::Pan) {
        Vector2 d = GetMouseDelta();
        v.camPos -= YawRight(v.yaw) * d.x * 1.5f;
        v.camPos.y += d.y * 1.5f;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) drag = Drag::None;
        return;
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && drag == Drag::None) v.camPos += AnglesForward(v.pitch, v.yaw) * wheel * 96.0f;

    Camera3D cam = v.Camera();
    Vector2 local{mouse.x - v.rect.x, mouse.y - v.rect.y};
    Ray ray = GetScreenToWorldRayEx(local, cam, (int)v.rect.width, (int)v.rect.height);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // gizmo hover
    BoundingBox bb;
    bool haveSel = SelectionBounds(bb) && tool == Tool::Select;
    int hoverAxis = -1;
    Vector3 gc = (bb.min + bb.max) * 0.5f;
    float gl = Vector3Distance(cam.position, gc) * 0.18f;
    Vector2 sc{}, se[3]{};
    if (haveSel) {
        sc = GetWorldToScreenEx(gc, cam, (int)v.rect.width, (int)v.rect.height);
        for (int a = 0; a < 3; ++a) {
            Vector3 ax{0, 0, 0};
            SetComp(ax, a, gl);
            se[a] = GetWorldToScreenEx(gc + ax, cam, (int)v.rect.width, (int)v.rect.height);
            Vector2 ab = Vector2Subtract(se[a], sc), am = Vector2Subtract(local, sc);
            float t = Clampf(Vector2DotProduct(am, ab) / fmaxf(Vector2DotProduct(ab, ab), 1e-3f), 0, 1);
            Vector2 cp = Vector2Add(sc, Vector2Scale(ab, t));
            if (Vector2Distance(cp, local) < 8 && t > 0.15f) hoverAxis = a;
        }
    }
    if (drag == Drag::None) gizmoAxis = hoverAxis;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && drag == Drag::None) {
        dragView = vi;
        if (haveSel && hoverAxis >= 0) {
            drag = Drag::Gizmo;
            gizmoAxis = hoverAxis;
            gizmoAccum = {0, 0, 0};
            dragLastApplied = {0, 0, 0};
            dragStartMouse = local;
            Snapshot();
            return;
        }
        int b, f, e;
        float dist;
        bool hit = PickRay(ray, &b, &f, &e, &dist);
        switch (tool) {
            case Tool::Texture:
                if (hit && b >= 0) {
                    if (IsKeyDown(KEY_LEFT_ALT)) {
                        curTexture = map.brushes[b].faces[f].tex.name;
                        SetStatus("Picked texture " + curTexture);
                    } else {
                        Snapshot();
                        ApplyTexture(curTexture, b, shift ? -1 : f);
                    }
                }
                break;
            case Tool::Face:
                if (!shift) selFaces.clear();
                if (hit && b >= 0) {
                    FaceRef fr{b, f};
                    if (selFaces.count(fr)) selFaces.erase(fr);
                    else selFaces.insert(fr);
                }
                break;
            case Tool::Entity: {
                int ne = CreateEntity(curClass, PlacementPoint(vi, mouse, ClassFor(curClass)));
                if (ne >= 0) {
                    ClearSelection();
                    SelectAllOfEntity(ne);
                    SetStatus("Placed " + curClass);
                }
                break;
            }
            case Tool::Brush:
                if (hit) {
                    Vector3 p = SnapV(ray.position + ray.direction * dist);
                    Vector3 n = b >= 0 ? map.brushes[b].faces[f].plane.n : Vector3{0, 1, 0};
                    Vector3 mn = p - Vector3{32, 0, 32}, mx = p + Vector3{32, 64, 32};
                    if (n.y < -0.5f) mn.y = p.y - 64, mx.y = p.y;
                    else if (fabsf(n.y) < 0.5f) {
                        mn = p - Vector3{32, 32, 32} + n * 32;
                        mx = p + Vector3{32, 32, 32} + n * 32;
                    }
                    int nb = CreateBrush(mn, mx);
                    if (nb >= 0) {
                        ClearSelection();
                        selBrushes.insert(nb);
                    }
                }
                break;
            default:
                if (!shift) ClearSelection();
                if (hit) {
                    if (e >= 0) {
                        if (shift && selEnts.count(e)) selEnts.erase(e);
                        else selEnts.insert(e);
                    } else if (b >= 0) {
                        int owner = map.brushes[b].entity;
                        if (owner >= 0 && !IsKeyDown(KEY_LEFT_ALT)) SelectAllOfEntity(owner);
                        else if (shift && selBrushes.count(b)) selBrushes.erase(b);
                        else selBrushes.insert(b);
                    }
                }
                break;
        }
    }

    if (drag == Drag::Gizmo) {
        int a = gizmoAxis;
        Vector2 ab = Vector2Subtract(se[a], sc);
        float len = Vector2Length(ab);
        if (len > 1) {
            Vector2 d = Vector2Subtract(local, dragStartMouse);
            float worldMove = Vector2DotProduct(d, Vector2Scale(ab, 1.0f / len)) / len * gl;
            float snapped = Snap(worldMove, grid);
            float inc = snapped - Comp(dragLastApplied, a);
            if (fabsf(inc) > 0.001f) {
                Vector3 dv{0, 0, 0};
                SetComp(dv, a, inc);
                MoveSelection(dv);
                SetComp(dragLastApplied, a, snapped);
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (Len(dragLastApplied) < 0.001f && !undo.empty()) undo.pop_back();
            drag = Drag::None;
        }
        return;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) drag = Drag::None;
    (void)keyboardFree;
}

// ---------------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------------
static void DrawGrid2D(const EditorView& v, float grid, bool flip, int au, int av) {
    Rectangle r = v.rect;
    float step = grid;
    while (step * v.zoom < 7) step *= 2;
    float halfW = r.width * 0.5f / v.zoom, halfH = r.height * 0.5f / v.zoom;
    float u0 = floorf((v.center.x - halfW) / step) * step, u1 = v.center.x + halfW;
    float w0 = floorf((v.center.y - halfH) / step) * step, w1 = v.center.y + halfH;
    Color minor{34, 39, 49, 255}, major{46, 53, 66, 255}, axis{70, 82, 104, 255};
    float majorStep = std::max(step * 8, 256.0f);
    for (float u = u0; u <= u1; u += step) {
        float sx = r.x + r.width * 0.5f + (u - v.center.x) * v.zoom;
        Color c = fabsf(u) < 0.001f ? axis : (fabsf(fmodf(u, majorStep)) < 0.001f ? major : minor);
        DrawLineV({sx, r.y}, {sx, r.y + r.height}, c);
    }
    for (float w = w0; w <= w1; w += step) {
        float sy = r.y + r.height * 0.5f + (flip ? -(w - v.center.y) : (w - v.center.y)) * v.zoom;
        Color c = fabsf(w) < 0.001f ? axis : (fabsf(fmodf(w, majorStep)) < 0.001f ? major : minor);
        DrawLineV({r.x, sy}, {r.x + r.width, sy}, c);
    }
    (void)au;
    (void)av;
}

static const char* ViewName(ViewKind k) {
    switch (k) {
        case ViewKind::Top: return "Top (X/Z)";
        case ViewKind::Front: return "Front (X/Y)";
        case ViewKind::Side: return "Side (Z/Y)";
        default: return "Perspective";
    }
}

void Editor::Impl::DrawView2D(int vi) {
    EditorView& v = views[vi];
    Rectangle r = v.rect;
    ui::Fill(r, Color{18, 21, 27, 255});
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    bool flip = v.kind != ViewKind::Top;
    DrawGrid2D(v, grid, flip, AxisU(v), AxisV(v));

    // brushes
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        const Brush& b = map.brushes[i];
        bool sel = selBrushes.count((int)i) > 0;
        Color c = kBrushCol;
        if (b.entity >= 0) {
            const ClassInfo* ci = ClassFor(map.entities[b.entity].classname);
            c = ci ? ci->color : Color{200, 200, 200, 255};
        } else if (!b.faces.empty() && b.faces[0].tex.name == "sky") {
            c = Color{90, 170, 255, 255};
        } else if (!b.faces.empty() && assets::IsToolTexture(b.faces[0].tex.name)) {
            c = Color{200, 120, 255, 255};
        }
        if (sel) c = kSelCol;
        std::vector<Vector2> pts;
        for (auto& f : b.faces)
            for (auto& p : f.poly) pts.push_back(WorldToScreen2D(v, p));
        auto h = Hull(pts);
        if (h.size() >= 3) {
            Color fill = c;
            fill.a = sel ? 50 : 22;
            // triangle fan (raylib wants counter clockwise order in screen space)
            std::vector<Vector2> rev(h.rbegin(), h.rend());
            DrawTriangleFan(rev.data(), (int)rev.size(), fill);
        }
        for (auto& f : b.faces) {
            for (size_t k = 0; k < f.poly.size(); ++k) {
                Vector2 a = WorldToScreen2D(v, f.poly[k]), bb = WorldToScreen2D(v, f.poly[(k + 1) % f.poly.size()]);
                DrawLineV(a, bb, c);
            }
        }
    }
    // entities
    for (size_t i = 0; i < map.entities.size(); ++i) {
        const MapEntity& e = map.entities[i];
        bool brushEnt = false;
        for (auto& b : map.brushes)
            if (b.entity == (int)i) brushEnt = true;
        if (brushEnt) continue;
        const ClassInfo* ci = ClassFor(e.classname);
        Color c = ci ? ci->color : Color{200, 200, 200, 255};
        bool sel = selEnts.count((int)i) > 0;
        BoundingBox bb = EntityBox((int)i);
        Vector2 a = WorldToScreen2D(v, bb.min), b = WorldToScreen2D(v, bb.max);
        Rectangle er{std::min(a.x, b.x), std::min(a.y, b.y), std::max(3.0f, fabsf(b.x - a.x)), std::max(3.0f, fabsf(b.y - a.y))};
        Color fill = c;
        fill.a = 70;
        DrawRectangleRec(er, fill);
        DrawRectangleLinesEx(er, sel ? 2.0f : 1.0f, sel ? kSelCol2 : c);
        Vector2 o = WorldToScreen2D(v, e.origin);
        if (e.classname == "light" || e.classname == "light_spot") {
            DrawCircleV(o, 3, Color{255, 230, 120, 255});
            if (sel) {
                float rad = e.Num("radius", 300) * v.zoom;
                if (v.kind == ViewKind::Top || true) DrawCircleLinesV(o, rad, Color{255, 220, 90, 120});
            }
        }
        if (v.kind == ViewKind::Top && (e.classname == "player_start" || (ci && ci->category == "monsters"))) {
            Vector3 f = AnglesForward(0, e.angle);
            Vector2 t = WorldToScreen2D(v, e.origin + f * 24.0f);
            DrawLineEx(o, t, 2, c);
        }
        if (v.zoom > 0.12f) fonts::Text("ui", e.classname.c_str(), er.x + er.width + 3, er.y - 2, 11, Color{200, 205, 215, 200});
    }
    // selection handles
    BoundingBox sb;
    if (SelectionBounds(sb) && (tool == Tool::Select || tool == Tool::Texture || tool == Tool::Face)) {
        Vector2 a = WorldToScreen2D(v, sb.min), b = WorldToScreen2D(v, sb.max);
        Rectangle sr{std::min(a.x, b.x), std::min(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y)};
        DrawRectangleLinesEx(sr, 1, Color{255, 200, 60, 120});
        for (auto& hp : HandlePositions(sr)) {
            DrawRectangle((int)hp.x - 4, (int)hp.y - 4, 8, 8, Color{255, 170, 40, 255});
            DrawRectangleLines((int)hp.x - 4, (int)hp.y - 4, 8, 8, Color{40, 20, 0, 255});
        }
        // size readout
        Vector3 size = sb.max - sb.min;
        fonts::Text("ui", TextFormat("%g x %g", Comp(size, AxisU(v)), Comp(size, AxisV(v))), sr.x, sr.y - 16, 12, Color{255, 200, 90, 255});
    }
    // brush being created
    if (drag == Drag::Create && dragView == vi) {
        Vector3 keep{0, 0, 0};
        Vector3 w = ScreenToWorld2D(v, GetMousePosition(), keep);
        Vector2 a = WorldToScreen2D(v, SnapV(dragStartWorld)), b = WorldToScreen2D(v, SnapV(w));
        Rectangle cr{std::min(a.x, b.x), std::min(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y)};
        DrawRectangleRec(cr, Color{80, 160, 255, 50});
        DrawRectangleLinesEx(cr, 1, Color{120, 190, 255, 255});
        Vector3 s = SnapV(w) - SnapV(dragStartWorld);
        fonts::Text("ui", TextFormat("%g x %g", fabsf(Comp(s, AxisU(v))), fabsf(Comp(s, AxisV(v)))), cr.x, cr.y - 16, 12, Color{140, 200, 255, 255});
    }
    if (drag == Drag::Marquee && dragView == vi) {
        Vector2 m = GetMousePosition();
        Rectangle mr{std::min(dragStartMouse.x, m.x), std::min(dragStartMouse.y, m.y), fabsf(m.x - dragStartMouse.x), fabsf(m.y - dragStartMouse.y)};
        DrawRectangleRec(mr, Color{80, 160, 255, 30});
        DrawRectangleLinesEx(mr, 1, Color{120, 190, 255, 200});
    }
    // clip tool
    if (tool == Tool::Clip && clipView == vi) {
        for (auto& p : clipPoints) DrawCircleV(WorldToScreen2D(v, p), 5, Color{255, 80, 220, 255});
        if (clipPoints.size() == 2) {
            Vector2 a = WorldToScreen2D(v, clipPoints[0]), b = WorldToScreen2D(v, clipPoints[1]);
            Vector2 d = Vector2Normalize(Vector2Subtract(b, a));
            DrawLineEx(Vector2Subtract(a, Vector2Scale(d, 4000)), Vector2Add(b, Vector2Scale(d, 4000)), 2, Color{255, 80, 220, 255});
            const char* modes[] = {"keep both", "keep front", "keep back"};
            fonts::Text("ui", TextFormat("ENTER = clip (%s)  TAB = change", modes[clipMode]), r.x + 8, r.y + r.height - 20, 12, Color{255, 120, 230, 255});
        }
    }
    EndScissorMode();
    bool active = hotView == vi;
    DrawRectangleLinesEx(r, 1, active ? ui::theme.accent : ui::theme.border);
    fonts::Text("ui", TextFormat("%s  -  grid %g  -  zoom %d%%", ViewName(v.kind), grid, (int)(v.zoom * 100)), r.x + 6, r.y + 4, 12,
                ui::theme.textDim);
    HandleDrop(vi);
}

static void DrawArrow3D(Vector3 from, Vector3 to, float radius, Color c) {
    Vector3 d = Norm(to - from);
    float len = Len(to - from);
    DrawCylinderEx(from, from + d * (len * 0.8f), radius, radius, 6, c);
    DrawCylinderEx(from + d * (len * 0.8f), to, radius * 3.0f, 0.0f, 8, c);
}

void Editor::Impl::DrawView3D(int vi) {
    EditorView& v = views[vi];
    Rectangle r = v.rect;
    int w = std::max(16, (int)r.width), h = std::max(16, (int)r.height);
    if (v.rt.id == 0 || v.rt.texture.width != w || v.rt.texture.height != h) {
        if (v.rt.id) UnloadRenderTexture(v.rt);
        v.rt = LoadRenderTexture(w, h);
    }
    Camera3D cam = v.Camera();
    MapSettings ms = map.settings;
    ms.fogDensity *= 0.5f;
    gRenderer.SetFrame(cam.position, ms, (float)GetTime());
    gRenderer.SetDynamicLights({}, cam.position);
    gRenderer.SetEditorShading(!litPreview);
    BeginTextureMode(v.rt);
    ClearBackground(Color{30, 34, 42, 255});
    gRenderer.Begin3D(cam, (float)w / h, 1.0f, 30000.0f);
    if (litPreview) litWorld.Draw(nullptr, nullptr);
    else world.Draw(nullptr, nullptr);

    // ground grid
    rlBegin(RL_LINES);
    for (int i = -32; i <= 32; ++i) {
        Color c = i == 0 ? Color{90, 100, 130, 255} : Color{60, 66, 80, 120};
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlVertex3f(i * 64.0f, 0.1f, -2048);
        rlVertex3f(i * 64.0f, 0.1f, 2048);
        rlVertex3f(-2048, 0.1f, i * 64.0f);
        rlVertex3f(2048, 0.1f, i * 64.0f);
    }
    rlEnd();

    // entities
    for (size_t i = 0; i < map.entities.size(); ++i) {
        const MapEntity& e = map.entities[i];
        bool brushEnt = false;
        for (auto& b : map.brushes)
            if (b.entity == (int)i) brushEnt = true;
        if (brushEnt) continue;
        const ClassInfo* ci = ClassFor(e.classname);
        Color c = ci ? ci->color : Color{200, 200, 200, 255};
        bool sel = selEnts.count((int)i) > 0;
        BoundingBox bb = EntityBox((int)i);
        std::string model = e.Str("model", ci ? ci->model : "");
        if (e.classname != "prop_model" && ci) model = ci->model;
        if (e.classname == "prop_model") model = e.Str("model", "");
        if (!model.empty() && vfs::Exists(model)) {
            ModelAsset& ma = assets::GetModel(model);
            float scale = e.Num("scale", 1.0f);
            Vector3 base = e.origin + Vector3{0, ci && e.classname != "prop_model" ? ci->mins.y : 0, 0};
            Matrix xf = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateY((e.angle + 90) * DEG2RAD)),
                                       MatrixTranslate(base.x, base.y, base.z));
            gRenderer.DrawModelLit(ma, xf, {0.75f, 0.75f, 0.75f}, {0.5f, 0.5f, 0.45f}, Norm({0.3f, 0.8f, 0.4f}));
        } else if (e.classname == "sprite" || e.classname == "fire") {
            Texture2D t = assets::GetTexturePath(e.classname == "fire" ? "sprites/fire.png" : e.Str("texture", "sprites/flare.png"));
            DrawBillboard(cam, t, e.origin, e.Num("size", 32), WHITE);
        } else if (e.classname == "light" || e.classname == "light_spot") {
            Vector3 col = e.Vec("color", {255, 230, 200});
            if (col.x > 1 || col.y > 1 || col.z > 1) col = col / 255.0f;
            DrawSphereEx(e.origin, 5, 6, 6, ColorFromVec(col));
        } else {
            Color fill = c;
            fill.a = 110;
            DrawCubeV((bb.min + bb.max) * 0.5f, bb.max - bb.min, fill);
        }
        DrawBoundingBox(bb, sel ? kSelCol2 : Color{c.r, c.g, c.b, 160});
        if (e.classname == "player_start" || (ci && ci->category == "monsters")) {
            Vector3 f = AnglesForward(0, e.angle);
            DrawLine3D(e.origin, e.origin + f * 40.0f, sel ? kSelCol2 : c);
        }
        if (sel && (e.classname == "light" || e.classname == "light_spot")) {
            float rad = e.Num("radius", 300);
            DrawCircle3D(e.origin, rad, {1, 0, 0}, 90, Color{255, 220, 90, 120});
            DrawCircle3D(e.origin, rad, {0, 1, 0}, 90, Color{255, 220, 90, 120});
            DrawCircle3D(e.origin, rad, {0, 0, 1}, 0, Color{255, 220, 90, 120});
        }
    }
    // selected brushes
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (int bi : selBrushes) {
        if (bi < 0 || bi >= (int)map.brushes.size()) continue;
        for (auto& f : map.brushes[bi].faces)
            for (size_t k = 0; k < f.poly.size(); ++k) DrawLine3D(f.poly[k], f.poly[(k + 1) % f.poly.size()], kSelCol);
    }
    for (auto& fr : selFaces) {
        if (fr.brush < 0 || fr.brush >= (int)map.brushes.size() || fr.face >= (int)map.brushes[fr.brush].faces.size()) continue;
        const auto& poly = map.brushes[fr.brush].faces[fr.face].poly;
        for (size_t k = 0; k < poly.size(); ++k) DrawLine3D(poly[k], poly[(k + 1) % poly.size()], kSelCol2);
        Vector3 n = map.brushes[fr.brush].faces[fr.face].plane.n * 0.3f;
        for (size_t k = 1; k + 1 < poly.size(); ++k) DrawTriangle3D(poly[0] + n, poly[k] + n, poly[k + 1] + n, Color{255, 200, 60, 70});
    }
    // gizmo
    BoundingBox sb;
    if (tool == Tool::Select && SelectionBounds(sb)) {
        Vector3 gc = (sb.min + sb.max) * 0.5f;
        float gl = Vector3Distance(cam.position, gc) * 0.18f;
        Color cols[3] = {{230, 60, 60, 255}, {80, 220, 80, 255}, {70, 120, 255, 255}};
        for (int a = 0; a < 3; ++a) {
            Vector3 ax{0, 0, 0};
            SetComp(ax, a, gl);
            Color c = gizmoAxis == a ? Color{255, 240, 120, 255} : cols[a];
            DrawArrow3D(gc, gc + ax, gl * 0.02f, c);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    gRenderer.End3D();
    EndTextureMode();
    DrawTexturePro(v.rt.texture, {0, 0, (float)w, -(float)h}, r, {0, 0}, 0, WHITE);
    bool active = hotView == vi;
    DrawRectangleLinesEx(r, 1, active ? ui::theme.accent : ui::theme.border);
    fonts::Text("ui", litPreview ? "Perspective  -  baked lighting" : "Perspective", r.x + 6, r.y + 4, 12, ui::theme.textDim);
    fonts::Text("ui", "RMB + WASD/QE fly  -  MMB pan  -  wheel dolly", r.x + 6, r.y + r.height - 18, 11, Color{120, 128, 145, 200});
    HandleDrop(vi);
}

void Editor::Impl::DrawViews() {
    if (fitPending && views[0].rect.width > 0) {
        FitViews();
        fitPending = false;
    }
    for (int i = 0; i < 4; ++i) {
        if (views[i].rect.width <= 0) continue;
        if (views[i].kind == ViewKind::Persp) DrawView3D(i);
        else DrawView2D(i);
    }
}

}  // namespace rbs
