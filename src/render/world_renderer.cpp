#include "render/world_renderer.h"

#include <map>
#include <rlgl.h>

namespace rbs {

void WorldRenderer::Unload() {
    for (auto& b : m_batches) UnloadMesh(b.mesh);
    m_batches.clear();
    if (m_material.maps) {
        RL_FREE(m_material.maps);
        m_material.maps = nullptr;
    }
}

void WorldRenderer::Build(const Map& map, const Lightmap* lightmap, bool showTools) {
    Unload();
    m_lightmap = (lightmap && lightmap->Valid()) ? lightmap->Texture() : gRenderer.Grey();
    float lmW = lightmap && lightmap->Valid() ? (float)lightmap->Texture().width : 1.0f;
    float lmH = lightmap && lightmap->Valid() ? (float)lightmap->Texture().height : 1.0f;
    bool useLm = lightmap && lightmap->Valid();

    struct Accum {
        std::vector<float> pos, uv, uv2, nrm;
    };
    std::map<std::pair<int, std::string>, Accum> groups;
    for (const Brush& b : map.brushes) {
        for (const BrushFace& f : b.faces) {
            if (f.poly.size() < 3) continue;
            bool tool = assets::IsToolTexture(f.tex.name);
            if (tool && !showTools) continue;
            TextureInfo& ti = assets::GetTexture(f.tex.name);
            Accum& acc = groups[{b.entity, f.tex.name}];
            auto emit = [&](Vector3 p) {
                acc.pos.insert(acc.pos.end(), {p.x, p.y, p.z});
                acc.nrm.insert(acc.nrm.end(), {f.plane.n.x, f.plane.n.y, f.plane.n.z});
                Vector2 uv = FaceUV(f, p, ti.tex.width, ti.tex.height);
                acc.uv.insert(acc.uv.end(), {uv.x, uv.y});
                if (useLm && f.lmValid) {
                    float s, t;
                    ProjectST(p, f.lmAxis, s, t);
                    float px = f.lmX + 1 + (s / f.lmLuxel - f.lmSMin) + 0.5f;
                    float py = f.lmY + 1 + (t / f.lmLuxel - f.lmTMin) + 0.5f;
                    acc.uv2.insert(acc.uv2.end(), {px / lmW, py / lmH});
                } else {
                    acc.uv2.insert(acc.uv2.end(), {0.5f / lmW, 0.5f / lmH});
                }
            };
            for (size_t i = 1; i + 1 < f.poly.size(); ++i) {
                emit(f.poly[0]);
                emit(f.poly[i]);
                emit(f.poly[i + 1]);
            }
        }
    }
    for (auto& kv : groups) {
        Accum& a = kv.second;
        if (a.pos.empty()) continue;
        Batch batch;
        batch.entity = kv.first.first;
        batch.tex = kv.first.second;
        batch.sky = batch.tex == "sky";
        batch.scroll = assets::GetTexture(batch.tex).scroll;
        Mesh& m = batch.mesh;
        m.vertexCount = (int)(a.pos.size() / 3);
        m.triangleCount = m.vertexCount / 3;
        m.vertices = (float*)RL_MALLOC(a.pos.size() * sizeof(float));
        m.normals = (float*)RL_MALLOC(a.nrm.size() * sizeof(float));
        m.texcoords = (float*)RL_MALLOC(a.uv.size() * sizeof(float));
        m.texcoords2 = (float*)RL_MALLOC(a.uv2.size() * sizeof(float));
        memcpy(m.vertices, a.pos.data(), a.pos.size() * sizeof(float));
        memcpy(m.normals, a.nrm.data(), a.nrm.size() * sizeof(float));
        memcpy(m.texcoords, a.uv.data(), a.uv.size() * sizeof(float));
        memcpy(m.texcoords2, a.uv2.data(), a.uv2.size() * sizeof(float));
        UploadMesh(&m, false);
        m_batches.push_back(batch);
    }
    m_material.maps = (MaterialMap*)RL_CALLOC(12, sizeof(MaterialMap));  // 12 = raylib MAX_MATERIAL_MAPS
}

void WorldRenderer::Draw(const std::vector<Vector3>* offsets, const std::vector<char>* hidden, Color tint) const {
    if (m_batches.empty()) return;
    Material mat = m_material;
    for (const Batch& b : m_batches) {
        if (b.entity >= 0 && hidden && b.entity < (int)hidden->size() && (*hidden)[b.entity]) continue;
        Vector3 off{0, 0, 0};
        if (b.entity >= 0 && offsets && b.entity < (int)offsets->size()) off = (*offsets)[b.entity];
        Matrix xf = MatrixTranslate(off.x, off.y, off.z);
        if (b.sky) {
            mat.shader = gRenderer.sky.shader;
            mat.maps[0].texture = gRenderer.White();
            mat.maps[1].texture = Texture2D{};
        } else {
            mat.shader = gRenderer.world.shader;
            mat.maps[0].texture = assets::GetTexture(b.tex).tex;
            mat.maps[1].texture = m_lightmap;
            SetShaderValue(gRenderer.world.shader, gRenderer.worldUvScroll, &b.scroll, SHADER_UNIFORM_VEC2);
        }
        mat.maps[0].color = tint;
        DrawMesh(b.mesh, mat, xf);
    }
}

}  // namespace rbs
