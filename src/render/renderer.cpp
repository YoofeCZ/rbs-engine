#include "render/renderer.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/vfs.h"

#include <rlgl.h>

namespace rbs {

Renderer gRenderer;

static Cvar* r_scale = nullptr;
static Cvar* r_posterize = nullptr;
static Cvar* r_dither = nullptr;
static Cvar* r_vignette = nullptr;
static Cvar* r_gamma = nullptr;
static Cvar* r_brightness = nullptr;
static Cvar* r_pixelate = nullptr;

bool Renderer::LoadSet(ShaderSet& s, const char* vs, const char* fs) {
    std::string vp = vfs::Resolve(std::string("shaders/") + vs);
    std::string fp = vfs::Resolve(std::string("shaders/") + fs);
    if (vp.empty() || fp.empty()) {
        LOG_ERR("Shader files missing: %s / %s", vs, fs);
        return false;
    }
    Shader sh = LoadShader(vp.c_str(), fp.c_str());
    if (!IsShaderValid(sh)) {
        LOG_ERR("Shader failed to compile: %s / %s", vs, fs);
        return false;
    }
    if (s.ok) UnloadShader(s.shader);
    s.shader = sh;
    s.ok = true;
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh, "matModel");
    sh.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(sh, "matNormal");
    s.viewPos = GetShaderLocation(sh, "viewPos");
    s.fogColor = GetShaderLocation(sh, "fogColor");
    s.fogDensity = GetShaderLocation(sh, "fogDensity");
    s.time = GetShaderLocation(sh, "time");
    s.dlPosRad = GetShaderLocation(sh, "dlPosRad");
    s.dlColor = GetShaderLocation(sh, "dlColor");
    s.dlCount = GetShaderLocation(sh, "dlCount");
    return true;
}

bool Renderer::ReloadShaders() {
    bool ok = LoadSet(world, "world.vs", "world.fs");
    ok &= LoadSet(model, "model.vs", "model.fs");
    ok &= LoadSet(sky, "world.vs", "sky.fs");
    ok &= LoadSet(unlit, "unlit.vs", "unlit.fs");
    worldUvScroll = GetShaderLocation(world.shader, "uvScroll");
    worldLightScale = GetShaderLocation(world.shader, "lightScale");
    worldEditorLight = GetShaderLocation(world.shader, "editorLight");
    modelAmbient = GetShaderLocation(model.shader, "ambientColor");
    modelLightColor = GetShaderLocation(model.shader, "lightColor");
    modelLightDir = GetShaderLocation(model.shader, "lightDir");
    modelFlash = GetShaderLocation(model.shader, "flashColor");
    modelFullbright = GetShaderLocation(model.shader, "fullbright");
    skyZenith = GetShaderLocation(sky.shader, "skyZenith");
    skyHorizon = GetShaderLocation(sky.shader, "skyHorizon");
    skyClouds = GetShaderLocation(sky.shader, "skyClouds");
    skySunDir = GetShaderLocation(sky.shader, "sunDir");
    skySunColor = GetShaderLocation(sky.shader, "sunColor");
    unlitAdditive = GetShaderLocation(unlit.shader, "additive");

    std::string pf = vfs::Resolve("shaders/post.fs");
    if (!pf.empty()) {
        Shader p = LoadShader(nullptr, pf.c_str());
        if (IsShaderValid(p)) {
            if (post.id) UnloadShader(post);
            post = p;
        } else {
            ok = false;
        }
    }
    postRes = GetShaderLocation(post, "resolution");
    postPosterize = GetShaderLocation(post, "posterize");
    postDither = GetShaderLocation(post, "dither");
    postFlash = GetShaderLocation(post, "flash");
    postVignette = GetShaderLocation(post, "vignette");
    postGamma = GetShaderLocation(post, "gamma");
    postBrightness = GetShaderLocation(post, "brightness");
    SetEditorShading(false);
    if (model.ok) assets::SetModelShader(model.shader);
    return ok;
}

bool Renderer::Init() {
    r_scale = cvars::Register("r_scale", "1", true, "3D render resolution scale (0.25 - 1). Lower = chunkier pixels");
    r_pixelate = cvars::Register("r_pixelate", "1", true, "use nearest filtering when upscaling a reduced r_scale");
    r_posterize = cvars::Register("r_posterize", "0", true, "color levels per channel for a retro palette look (0 = off, try 24)");
    r_dither = cvars::Register("r_dither", "0.6", true, "ordered dithering amount used with r_posterize");
    r_vignette = cvars::Register("r_vignette", "0.35", true, "screen edge darkening");
    r_gamma = cvars::Register("r_gamma", "1.0", true, "gamma correction");
    r_brightness = cvars::Register("r_brightness", "1.0", true, "overall brightness");
    Image w = GenImageColor(4, 4, WHITE);
    m_white = LoadTextureFromImage(w);
    UnloadImage(w);
    Image g = GenImageColor(4, 4, Color{128, 128, 128, 255});
    m_grey = LoadTextureFromImage(g);
    UnloadImage(g);
    return ReloadShaders();
}

void Renderer::Shutdown() {
    if (world.ok) UnloadShader(world.shader);
    if (model.ok) UnloadShader(model.shader);
    if (sky.ok) UnloadShader(sky.shader);
    if (unlit.ok) UnloadShader(unlit.shader);
    if (post.id) UnloadShader(post);
    if (m_scene.id) UnloadRenderTexture(m_scene);
    UnloadTexture(m_white);
    UnloadTexture(m_grey);
}

void Renderer::SetFrame(Vector3 viewPos, const MapSettings& ms, float time) {
    for (ShaderSet* s : {&world, &model, &sky, &unlit}) {
        if (!s->ok) continue;
        SetShaderValue(s->shader, s->viewPos, &viewPos, SHADER_UNIFORM_VEC3);
        SetShaderValue(s->shader, s->fogColor, &ms.fogColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(s->shader, s->fogDensity, &ms.fogDensity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s->shader, s->time, &time, SHADER_UNIFORM_FLOAT);
    }
    SetShaderValue(sky.shader, skyZenith, &ms.skyZenith, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky.shader, skyHorizon, &ms.skyHorizon, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky.shader, skyClouds, &ms.skyClouds, SHADER_UNIFORM_VEC3);
    Vector3 sd = Norm(ms.sunDir);
    SetShaderValue(sky.shader, skySunDir, &sd, SHADER_UNIFORM_VEC3);
    Vector3 sc = ms.sunIntensity > 0 ? ms.sunColor : Vector3{0, 0, 0};
    SetShaderValue(sky.shader, skySunColor, &sc, SHADER_UNIFORM_VEC3);
}

void Renderer::SetDynamicLights(const std::vector<DynamicLight>& lights, Vector3 viewPos) {
    constexpr int kMax = 16;
    std::vector<const DynamicLight*> sorted;
    for (auto& l : lights) sorted.push_back(&l);
    if ((int)sorted.size() > kMax) {
        std::partial_sort(sorted.begin(), sorted.begin() + kMax, sorted.end(), [&](const DynamicLight* a, const DynamicLight* b) {
            return Vector3DistanceSqr(a->pos, viewPos) - a->radius * a->radius < Vector3DistanceSqr(b->pos, viewPos) - b->radius * b->radius;
        });
        sorted.resize(kMax);
    }
    float posRad[kMax * 4] = {0};
    float col[kMax * 3] = {0};
    int count = (int)sorted.size();
    for (int i = 0; i < count; ++i) {
        posRad[i * 4 + 0] = sorted[i]->pos.x;
        posRad[i * 4 + 1] = sorted[i]->pos.y;
        posRad[i * 4 + 2] = sorted[i]->pos.z;
        posRad[i * 4 + 3] = sorted[i]->radius;
        col[i * 3 + 0] = sorted[i]->color.x;
        col[i * 3 + 1] = sorted[i]->color.y;
        col[i * 3 + 2] = sorted[i]->color.z;
    }
    for (ShaderSet* s : {&world, &model}) {
        if (!s->ok) continue;
        SetShaderValueV(s->shader, s->dlPosRad, posRad, SHADER_UNIFORM_VEC4, kMax);
        SetShaderValueV(s->shader, s->dlColor, col, SHADER_UNIFORM_VEC3, kMax);
        SetShaderValue(s->shader, s->dlCount, &count, SHADER_UNIFORM_INT);
    }
}

void Renderer::SetEditorShading(bool on, float lightScale) {
    Vector3 el{on ? 1.0f : 0.0f, 0, 0};
    SetShaderValue(world.shader, worldEditorLight, &el, SHADER_UNIFORM_VEC3);
    SetShaderValue(world.shader, worldLightScale, &lightScale, SHADER_UNIFORM_FLOAT);
}

void Renderer::Begin3D(const Camera3D& cam, float aspect, float nearZ, float farZ, float depthMin, float depthMax) {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    Matrix proj;
    if (cam.projection == CAMERA_ORTHOGRAPHIC) {
        float top = cam.fovy * 0.5f, right = top * aspect;
        proj = MatrixOrtho(-right, right, -top, top, nearZ, farZ);
    } else {
        proj = MatrixPerspective(cam.fovy * DEG2RAD, aspect, nearZ, farZ);
    }
    float a = depthMax - depthMin, b = depthMax + depthMin - 1.0f;
    proj.m2 = a * proj.m2 + b * proj.m3;
    proj.m6 = a * proj.m6 + b * proj.m7;
    proj.m10 = a * proj.m10 + b * proj.m11;
    proj.m14 = a * proj.m14 + b * proj.m15;
    rlMultMatrixf(MatrixToFloat(proj));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    rlMultMatrixf(MatrixToFloat(view));
    rlEnableDepthTest();
}

void Renderer::End3D() { EndMode3D(); }

void Renderer::DrawModelLit(ModelAsset& m, Matrix transform, Vector3 ambient, Vector3 dirColor, Vector3 dir, Color tint, float flash, bool fullbright) {
    SetShaderValue(model.shader, modelAmbient, &ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue(model.shader, modelLightColor, &dirColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(model.shader, modelLightDir, &dir, SHADER_UNIFORM_VEC3);
    float fl[4] = {1.0f, 1.0f, 1.0f, flash};
    SetShaderValue(model.shader, modelFlash, fl, SHADER_UNIFORM_VEC4);
    float fb = fullbright ? 1.0f : 0.0f;
    SetShaderValue(model.shader, modelFullbright, &fb, SHADER_UNIFORM_FLOAT);
    Model& mdl = m.model;
    Matrix full = MatrixMultiply(mdl.transform, transform);
    for (int i = 0; i < mdl.meshCount; ++i) {
        Material& mat = mdl.materials[mdl.meshMaterial[i]];
        Color old = mat.maps[MATERIAL_MAP_DIFFUSE].color;
        Color c = old;
        c.r = (unsigned char)((int)c.r * tint.r / 255);
        c.g = (unsigned char)((int)c.g * tint.g / 255);
        c.b = (unsigned char)((int)c.b * tint.b / 255);
        c.a = (unsigned char)((int)c.a * tint.a / 255);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
        if (mat.maps[MATERIAL_MAP_DIFFUSE].texture.id == 0) mat.maps[MATERIAL_MAP_DIFFUSE].texture = m_white;
        DrawMesh(mdl.meshes[i], mat, full);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = old;
    }
}

void Renderer::BeginUnlit(bool additive) {
    m_additive = additive;
    BeginShaderMode(unlit.shader);
    float a = additive ? 1.0f : 0.0f;
    SetShaderValue(unlit.shader, unlitAdditive, &a, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(additive ? BLEND_ADDITIVE : BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
}

void Renderer::EndUnlit() {
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    EndShaderMode();
}

void Renderer::Billboard(const Camera3D& cam, Texture2D tex, Vector3 pos, float size, Color c, float rotation, Rectangle src) {
    Vector3 fwd = Norm(cam.target - cam.position);
    Vector3 right = Norm(Cross(fwd, cam.up));
    Vector3 up = Cross(right, fwd);
    if (rotation != 0) {
        float cr = cosf(rotation), sr = sinf(rotation);
        Vector3 r2 = right * cr + up * sr;
        up = up * cr - right * sr;
        right = r2;
    }
    float h = size * 0.5f;
    Vector3 p[4] = {pos - right * h - up * h, pos + right * h - up * h, pos + right * h + up * h, pos - right * h + up * h};
    Vector2 uv[4] = {{src.x, src.y + src.height}, {src.x + src.width, src.y + src.height}, {src.x + src.width, src.y}, {src.x, src.y}};
    Quad(tex, p, uv, c);
}

void Renderer::Quad(Texture2D tex, const Vector3 p[4], const Vector2 uv[4], Color c) {
    rlCheckRenderBatchLimit(4);
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    for (int i = 0; i < 4; ++i) {
        rlTexCoord2f(uv[i].x, uv[i].y);
        rlVertex3f(p[i].x, p[i].y, p[i].z);
    }
    rlEnd();
    rlSetTexture(0);
}

void Renderer::Beam(const Camera3D& cam, Texture2D tex, Vector3 a, Vector3 b, float width, Color c) {
    Vector3 dir = Norm(b - a);
    Vector3 toCam = Norm(cam.position - (a + b) * 0.5f);
    Vector3 side = Norm(Cross(dir, toCam)) * (width * 0.5f);
    Vector3 p[4] = {a - side, b - side, b + side, a + side};
    Vector2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    Quad(tex, p, uv, c);
}

void Renderer::BeginScene(int screenW, int screenH) {
    float s = Clampf(r_scale ? r_scale->f : 1.0f, 0.1f, 1.0f);
    int w = std::max(64, (int)(screenW * s)), h = std::max(48, (int)(screenH * s));
    if (m_scene.id == 0 || m_scene.texture.width != w || m_scene.texture.height != h) {
        if (m_scene.id) UnloadRenderTexture(m_scene);
        m_scene = LoadRenderTexture(w, h);
    }
    SetTextureFilter(m_scene.texture, (s < 0.99f && r_pixelate->i) ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    BeginTextureMode(m_scene);
}

void Renderer::EndScene() { EndTextureMode(); }

void Renderer::Present(Rectangle dest, Vector4 flash) {
    Vector2 res{(float)m_scene.texture.width, (float)m_scene.texture.height};
    SetShaderValue(post, postRes, &res, SHADER_UNIFORM_VEC2);
    float pz = r_posterize->f, di = r_dither->f, vi = r_vignette->f, ga = fmaxf(0.1f, r_gamma->f), br = r_brightness->f;
    SetShaderValue(post, postPosterize, &pz, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, postDither, &di, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, postVignette, &vi, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, postGamma, &ga, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, postBrightness, &br, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, postFlash, &flash, SHADER_UNIFORM_VEC4);
    BeginShaderMode(post);
    DrawTexturePro(m_scene.texture, {0, 0, res.x, -res.y}, dest, {0, 0}, 0, WHITE);
    EndShaderMode();
}

}  // namespace rbs
