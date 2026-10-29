// RBS Engine - renderer: shaders, 3D setup, lit model drawing, billboards, post process.
#pragma once

#include "render/assets.h"
#include "world/map.h"

namespace rbs {

struct DynamicLight {
    Vector3 pos{0, 0, 0};
    Vector3 color{1, 1, 1};
    float radius = 200.0f;
};

struct ShaderSet {
    Shader shader{};
    int viewPos = -1, fogColor = -1, fogDensity = -1, time = -1;
    int dlPosRad = -1, dlColor = -1, dlCount = -1;
    bool ok = false;
};

class Renderer {
public:
    bool Init();
    void Shutdown();
    bool ReloadShaders();

    // frame state
    void SetFrame(Vector3 viewPos, const MapSettings& ms, float time);
    void SetDynamicLights(const std::vector<DynamicLight>& lights, Vector3 viewPos);
    void SetEditorShading(bool on, float lightScale = 2.0f);

    // depthMin/Max let the view weapon use its own slice of the depth buffer
    void Begin3D(const Camera3D& cam, float aspect, float nearZ, float farZ, float depthMin = 0.02f, float depthMax = 1.0f);
    void End3D();

    void DrawModelLit(ModelAsset& m, Matrix transform, Vector3 ambient, Vector3 dirColor, Vector3 dir, Color tint = WHITE,
                      float flash = 0.0f, bool fullbright = false);

    // Immediate mode transparent geometry (particles, sprites, decals)
    void BeginUnlit(bool additive);
    void EndUnlit();
    void Billboard(const Camera3D& cam, Texture2D tex, Vector3 pos, float size, Color c, float rotation = 0.0f, Rectangle src = {0, 0, 1, 1});
    void Quad(Texture2D tex, const Vector3 p[4], const Vector2 uv[4], Color c);
    void Beam(const Camera3D& cam, Texture2D tex, Vector3 a, Vector3 b, float width, Color c);

    // Scene render target + post processing
    void BeginScene(int screenW, int screenH);
    void EndScene();
    void Present(Rectangle dest, Vector4 flash);
    Vector2 SceneSize() const { return {(float)m_scene.texture.width, (float)m_scene.texture.height}; }

    Texture2D White() const { return m_white; }
    Texture2D Grey() const { return m_grey; }

    ShaderSet world, model, sky, unlit;
    Shader post{};
    int worldUvScroll = -1, worldLightScale = -1, worldEditorLight = -1;
    int modelAmbient = -1, modelLightColor = -1, modelLightDir = -1, modelFlash = -1, modelFullbright = -1;
    int skyZenith = -1, skyHorizon = -1, skyClouds = -1, skySunDir = -1, skySunColor = -1;
    int unlitAdditive = -1;
    int postRes = -1, postPosterize = -1, postDither = -1, postFlash = -1, postVignette = -1, postGamma = -1, postBrightness = -1;

private:
    Texture2D m_white{}, m_grey{};
    RenderTexture2D m_scene{};
    bool m_additive = false;
    bool LoadSet(ShaderSet& s, const char* vs, const char* fs);
};

extern Renderer gRenderer;

}  // namespace rbs
