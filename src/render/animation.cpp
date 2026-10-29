#include "render/animation.h"

#include <rlgl.h>

namespace rbs {
namespace anim {

static const float kFps = 60.0f;  // the engine plays raylib's sampled clips at 60 frames per second

float ClipLength(const ModelAsset& m, int clip) {
    if (clip < 0 || clip >= m.animCount) return 0;
    return (float)m.anims[clip].frameCount / kFps;
}

bool Sample(const ModelAsset& m, int clip, float time, bool loop, Pose& out) {
    if (clip < 0 || clip >= m.animCount) return false;
    const ModelAnimation& a = m.anims[clip];
    if (a.frameCount <= 0 || !a.framePoses || a.boneCount <= 0) return false;
    float f = time * kFps;
    int n = a.frameCount;
    if (loop) {
        f = fmodf(f, (float)n);
        if (f < 0) f += (float)n;
    } else {
        f = Clampf(f, 0, (float)(n - 1));
    }
    int i0 = (int)f;
    if (i0 >= n) i0 = n - 1;
    int i1 = i0 + 1;
    if (i1 >= n) i1 = loop ? 0 : n - 1;
    float t = f - (float)i0;
    out.resize(a.boneCount);
    const Transform* p0 = a.framePoses[i0];
    const Transform* p1 = a.framePoses[i1];
    for (int b = 0; b < a.boneCount; ++b) {
        out[b].translation = Vector3Lerp(p0[b].translation, p1[b].translation, t);
        out[b].rotation = QuaternionSlerp(p0[b].rotation, p1[b].rotation, t);
        out[b].scale = Vector3Lerp(p0[b].scale, p1[b].scale, t);
    }
    return true;
}

void Blend(Pose& a, const Pose& b, float w) {
    if (w <= 0) return;
    if (w >= 1 || a.size() != b.size()) {
        a = b;
        return;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        a[i].translation = Vector3Lerp(a[i].translation, b[i].translation, w);
        a[i].rotation = QuaternionSlerp(a[i].rotation, b[i].rotation, w);
        a[i].scale = Vector3Lerp(a[i].scale, b[i].scale, w);
    }
}

void Apply(ModelAsset& m, const Pose& pose) {
    Model& model = m.model;
    if (!model.bindPose || pose.empty()) return;
    // bone matrices: from the bind pose to the animated pose (as raylib's UpdateModelAnimationBones)
    static std::vector<Matrix> bones;
    int count = std::min((int)pose.size(), model.boneCount);
    bones.resize(count);
    for (int b = 0; b < count; ++b) {
        const Transform& in = model.bindPose[b];
        const Transform& out = pose[b];
        Quaternion invRotation = QuaternionInvert(in.rotation);
        Vector3 invTranslation = Vector3RotateByQuaternion(Vector3Negate(in.translation), invRotation);
        Vector3 invScale = Vector3Divide(Vector3{1, 1, 1}, in.scale);
        Vector3 t = Vector3Add(Vector3RotateByQuaternion(Vector3Multiply(out.scale, invTranslation), out.rotation), out.translation);
        Quaternion r = QuaternionMultiply(out.rotation, invRotation);
        Vector3 s = Vector3Multiply(out.scale, invScale);
        bones[b] = MatrixMultiply(MatrixMultiply(QuaternionToMatrix(r), MatrixTranslate(t.x, t.y, t.z)), MatrixScale(s.x, s.y, s.z));
    }
    // CPU skinning, 4 weights per vertex
    for (int mi = 0; mi < model.meshCount; ++mi) {
        Mesh& mesh = model.meshes[mi];
        if (!mesh.boneWeights || !mesh.boneIds || !mesh.animVertices || !mesh.vertices) continue;
        if (mesh.boneMatrices)
            for (int b = 0; b < std::min(count, mesh.boneCount); ++b) mesh.boneMatrices[b] = bones[b];
        int counter = 0;
        for (int v = 0; v < mesh.vertexCount * 3; v += 3) {
            Vector3 pos{0, 0, 0}, nrm{0, 0, 0};
            Vector3 vp{mesh.vertices[v], mesh.vertices[v + 1], mesh.vertices[v + 2]};
            Vector3 vn = mesh.normals ? Vector3{mesh.normals[v], mesh.normals[v + 1], mesh.normals[v + 2]} : Vector3{0, 1, 0};
            float total = 0;
            for (int j = 0; j < 4; ++j, ++counter) {
                float w = mesh.boneWeights[counter];
                int id = mesh.boneIds[counter];
                if (w == 0.0f || id >= count) continue;
                pos += Vector3Transform(vp, bones[id]) * w;
                // normals: rotation/scale only
                Matrix rm = bones[id];
                rm.m12 = rm.m13 = rm.m14 = 0;
                nrm += Vector3Transform(vn, rm) * w;
                total += w;
            }
            if (total <= 0) pos = vp, nrm = vn;  // unweighted vertices stay where they are
            mesh.animVertices[v] = pos.x;
            mesh.animVertices[v + 1] = pos.y;
            mesh.animVertices[v + 2] = pos.z;
            if (mesh.animNormals) {
                mesh.animNormals[v] = nrm.x;
                mesh.animNormals[v + 1] = nrm.y;
                mesh.animNormals[v + 2] = nrm.z;
            }
        }
        rlUpdateVertexBuffer(mesh.vboId[0], mesh.animVertices, mesh.vertexCount * 3 * sizeof(float), 0);
        if (mesh.animNormals) rlUpdateVertexBuffer(mesh.vboId[2], mesh.animNormals, mesh.vertexCount * 3 * sizeof(float), 0);
    }
}

}  // namespace anim
}  // namespace rbs
