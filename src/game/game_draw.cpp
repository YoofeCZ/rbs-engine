// RBS Engine - game rendering: world, entities, view weapon, HUD.
#include "core/cvar.h"
#include "game/game.h"

#include <rlgl.h>

namespace rbs {

static Cvar* hud_debug = nullptr;
static Cvar* r_drawviewmodel = nullptr;
static Cvar* r_viewmodelfov = nullptr;

static Matrix BasisMatrix(Vector3 x, Vector3 y, Vector3 z, Vector3 pos) {
    Matrix m = MatrixIdentity();
    m.m0 = x.x, m.m1 = x.y, m.m2 = x.z;
    m.m4 = y.x, m.m5 = y.y, m.m6 = y.z;
    m.m8 = z.x, m.m9 = z.y, m.m10 = z.z;
    m.m12 = pos.x, m.m13 = pos.y, m.m14 = pos.z;
    return m;
}

void Game::DrawEntities(const Camera3D& cam) {
    // models
    for (auto& up : m_entities) {
        Entity* e = up.get();
        if (e->removed || !e->visible || e == m_player || e->model.empty() || e->mapIndex >= 0) continue;
        UpdateEntityLight(e);
        ModelAsset& m = assets::GetModel(e->model);
        if (!e->anim.empty() && m.animCount > 0) {
            int ai = m.FindAnim(e->anim);
            if (ai >= 0) {
                int frame = (int)(e->animTime * 60.0f);
                frame = std::max(0, std::min(frame, m.anims[ai].frameCount - 1));
                UpdateModelAnimation(m.model, m.anims[ai], frame);
            }
        }
        float bob = e->bobAmount > 0 ? sinf(m_time * 2.5f + e->id) * e->bobAmount : 0.0f;
        Vector3 p = e->pos + Vector3{0, e->mins.y + bob, 0} + e->modelOffset;
        Matrix xf = MatrixMultiply(MatrixScale(e->modelScale, e->modelScale, e->modelScale),
                                   MatrixMultiply(MatrixRotateZ(e->angles.z * DEG2RAD), MatrixRotateY((e->angles.y + 90.0f) * DEG2RAD)));
        xf = MatrixMultiply(xf, MatrixTranslate(p.x, p.y, p.z));
        gRenderer.DrawModelLit(m, xf, e->lightAmbient, e->lightDirColor, e->lightDir, e->tint, e->flash * 0.6f, e->fullbright);
    }
    // sprites
    for (int pass = 0; pass < 2; ++pass) {
        bool additive = pass == 1;
        bool begun = false;
        for (auto& up : m_entities) {
            Entity* e = up.get();
            if (e->removed || !e->visible || e->sprite.empty() || e->spriteAdditive != additive) continue;
            if (!begun) {
                gRenderer.BeginUnlit(additive);
                begun = true;
            }
            Texture2D t = assets::GetTexturePath(e->sprite);
            int frames = 1;
            float fps = 10;
            auto it = e->props.find("frames");
            if (it != e->props.end() && it->is_number()) frames = std::max(1, it->get<int>());
            it = e->props.find("fps");
            if (it != e->props.end() && it->is_number()) fps = it->get<float>();
            int f = frames > 1 ? (int)(m_time * fps + e->id) % frames : 0;
            Rectangle src{(float)f / frames, 0, 1.0f / frames, 1};
            Color c = e->tint;
            if (!additive && !e->fullbright) {
                UpdateEntityLight(e);
                Vector3 l = VMin(e->lightAmbient + e->lightDirColor * 0.5f, Vector3{1.5f, 1.5f, 1.5f});
                c = ColorFromVec(Mul(VecFromColor(c), l), c.a / 255.0f);
            }
            gRenderer.Billboard(cam, t, e->pos, e->spriteSize, c, 0, src);
        }
        if (begun) gRenderer.EndUnlit();
    }
}

void Game::DrawViewWeapon(float aspect) {
    if (!r_drawviewmodel->i || !m_player || m_player->dead || m_ps.weapon < 0 || m_ps.weapon >= (int)m_script.weapons.size()) return;
    const WeaponDef& w = m_script.weapons[m_ps.weapon];
    if (w.model.empty()) return;
    ModelAsset& m = assets::GetModel(w.model);
    UpdateEntityLight(m_player);

    Camera3D vc = m_camera;
    vc.fovy = r_viewmodelfov->f;
    Vector3 fwd = Norm(m_camera.target - m_camera.position);
    Vector3 right = Norm(Cross(fwd, m_camera.up));
    Vector3 up = Cross(right, fwd);
    float bt = m_ps.bobTime * 0.5f;
    float bobX = cosf(bt) * 1.1f * m_ps.bobAmount * cvars::GetF("cl_bob", 1);
    float bobY = fabsf(sinf(bt)) * 1.0f * m_ps.bobAmount * cvars::GetF("cl_bob", 1);
    Vector3 off = w.viewOffset;
    Vector3 pos = m_camera.position + right * (off.x + bobX - m_ps.swayX) +
                  up * (off.y - bobY - m_ps.switchAnim * 20.0f + m_ps.swayY - m_ps.kick * 0.6f * w.recoil) +
                  fwd * (off.z - m_ps.kick * w.recoil * 3.5f);
    Matrix local = MatrixMultiply(MatrixRotateXYZ({w.viewRot.x * DEG2RAD, w.viewRot.y * DEG2RAD, w.viewRot.z * DEG2RAD}),
                                  MatrixRotateX(-m_ps.kick * w.recoil * 7.0f * DEG2RAD));
    Matrix basis = BasisMatrix(-right, up, fwd, pos);
    Matrix xf = MatrixMultiply(MatrixMultiply(MatrixScale(w.viewScale, w.viewScale, w.viewScale), local), basis);

    gRenderer.Begin3D(vc, aspect, 0.5f, 600.0f, 0.0f, 0.02f);
    Vector3 amb = m_player->lightAmbient + Vector3{0.05f, 0.05f, 0.05f};
    float flash = m_ps.muzzleFlash > 0 ? 0.15f : 0.0f;
    gRenderer.DrawModelLit(m, xf, amb, m_player->lightDirColor, m_player->lightDir, WHITE, flash);
    if (m_ps.muzzleFlash > 0 && w.flashSize > 0) {
        Vector3 mp = Vector3Transform(w.muzzle, xf);
        gRenderer.BeginUnlit(true);
        Color fc = ColorFromVec(w.flashColor);
        gRenderer.Billboard(vc, assets::GetTexturePath("sprites/muzzle.png"), mp, w.flashSize * w.viewScale, fc, RandRange(0, 6.28f));
        gRenderer.EndUnlit();
    }
    gRenderer.End3D();
}

void Game::DrawWorld3D(const Camera3D& cam, float aspect, bool viewWeapon) {
    gRenderer.Begin3D(cam, aspect, 2.0f, 20000.0f);
    m_worldRenderer.Draw(&m_brushOffsets, &m_hiddenBrushEnts);
    DrawEntities(cam);
    m_fx.DrawDecals();
    m_fx.Draw(cam);
    gRenderer.End3D();
    if (viewWeapon) DrawViewWeapon(aspect);
}

void Game::Draw(int w, int h) {
    if (!hud_debug) {
        hud_debug = cvars::Register("hud_debug", "1", true, "show movement/position/fps debug text");
        r_drawviewmodel = cvars::Register("r_drawviewmodel", "1", true, "draw the first person weapon");
        r_viewmodelfov = cvars::Register("r_viewmodelfov", "62", true, "vertical field of view of the weapon model");
    }
    if (!m_active) return;
    std::vector<DynamicLight> dl;
    m_fx.CollectLights(dl);
    for (auto& e : m_entities)
        if (!e->removed && e->lightRadius > 0) dl.push_back({e->Center(), e->lightColor, e->lightRadius});

    // picture in picture mission camera
    Entity* pip = m_pipCamera >= 0 ? Find(m_pipCamera) : nullptr;
    if (pip) {
        if (m_pipTarget.id == 0) m_pipTarget = LoadRenderTexture(384, 216);
        Camera3D pc{};
        pc.position = pip->pos;
        pc.target = pip->pos + AnglesForward(pip->angles.x, pip->angles.y);
        pc.up = {0, 1, 0};
        pc.fovy = 60;
        pc.projection = CAMERA_PERSPECTIVE;
        gRenderer.SetFrame(pc.position, m_map.settings, m_time);
        gRenderer.SetDynamicLights(dl, pc.position);
        BeginTextureMode(m_pipTarget);
        ClearBackground(BLACK);
        DrawWorld3D(pc, 384.0f / 216.0f, false);
        EndTextureMode();
    }

    gRenderer.SetFrame(m_camera.position, m_map.settings, m_time);
    gRenderer.SetDynamicLights(dl, m_camera.position);
    gRenderer.BeginScene(w, h);
    ClearBackground(ColorFromVec(m_map.settings.fogDensity > 0 ? m_map.settings.fogColor : Vector3{0, 0, 0}));
    Vector2 ss = gRenderer.SceneSize();
    DrawWorld3D(m_camera, ss.x / ss.y, true);
    gRenderer.EndScene();

    Vector4 flash{0, 0, 0, 0};
    if (m_ps.damageFlash > 0) flash = {0.7f, 0.0f, 0.0f, fminf(0.45f, m_ps.damageFlash * 0.5f)};
    else if (m_ps.pickupFlash > 0) flash = {1.0f, 0.85f, 0.4f, fminf(0.25f, m_ps.pickupFlash * 0.5f)};
    if (m_player && m_player->dead) flash = {0.35f, 0.0f, 0.0f, 0.35f};
    gRenderer.Present({0, 0, (float)w, (float)h}, flash);

    if (pip) {
        float pw = w * 0.22f, ph = pw * 9.0f / 16.0f;
        Rectangle r{w - pw - 16, h - ph - 16, pw, ph};
        DrawRectangle((int)r.x - 4, (int)r.y - 4, (int)r.width + 8, (int)r.height + 8, Color{20, 18, 16, 230});
        DrawTexturePro(m_pipTarget.texture, {0, 0, 384, -216}, r, {0, 0}, 0, WHITE);
        DrawRectangleLinesEx({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, 2, Color{150, 120, 70, 255});
        if (fmodf(m_time, 1.0f) < 0.6f) DrawCircle((int)(r.x + 12), (int)(r.y + 12), 4, RED);
        DrawText("CAM", (int)r.x + 20, (int)r.y + 6, 10, RAYWHITE);
    }
    DrawHud(w, h);
}

std::string Game::DebugText() const {
    if (!m_player) return "";
    float hs = sqrtf(m_player->vel.x * m_player->vel.x + m_player->vel.z * m_player->vel.z);
    std::string wname = "-";
    int slot = 0;
    if (m_ps.weapon >= 0 && m_ps.weapon < (int)m_script.weapons.size()) {
        wname = m_script.weapons[m_ps.weapon].name;
        slot = m_script.weapons[m_ps.weapon].slot;
    }
    return StrFormat("%s\n%d u/s\n%d, %d, %d\n%d fps\n%d / %s", m_ps.noclip ? "noclip" : (m_player->onGround ? "grounded" : "airborne"), (int)hs,
                     (int)m_player->pos.x, (int)m_player->pos.y, (int)m_player->pos.z, GetFPS(), slot, wname.c_str());
}

void Game::DrawFallbackHud(int w, int h) {
    if (!m_player) return;
    DrawText(TextFormat("HEALTH %d", (int)m_player->health), 20, h - 40, 20, RAYWHITE);
    DrawLine(w / 2 - 6, h / 2, w / 2 + 6, h / 2, WHITE);
    DrawLine(w / 2, h / 2 - 6, w / 2, h / 2 + 6, WHITE);
}

void Game::DrawHud(int w, int h) {
    if (!m_active) return;
    if (!m_script.DrawHud(w, h)) DrawFallbackHud(w, h);
    if (m_messageTime > 0 && !m_message.empty()) {
        Font f = GetFontDefault();
        float size = 20;
        Vector2 ms = MeasureTextEx(f, m_message.c_str(), size, 2);
        Color c{255, 230, 180, (unsigned char)(255 * Clampf(m_messageTime, 0, 1))};
        DrawTextEx(f, m_message.c_str(), {w * 0.5f - ms.x * 0.5f + 2, h * 0.3f + 2}, size, 2, Color{0, 0, 0, c.a});
        DrawTextEx(f, m_message.c_str(), {w * 0.5f - ms.x * 0.5f, h * 0.3f}, size, 2, c);
    }
    if (hud_debug && hud_debug->i) {
        std::string t = DebugText();
        DrawText(t.c_str(), 9, 9, 10, Color{0, 0, 0, 160});
        DrawText(t.c_str(), 8, 8, 10, RAYWHITE);
    }
}

}  // namespace rbs
