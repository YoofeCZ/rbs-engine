// RBS Engine - visual effects: particles, decals, timed dynamic lights.
#pragma once

#include "render/renderer.h"
#include "world/collision.h"

namespace rbs {

class Lightmap;

struct Particle {
    Vector3 pos, vel;
    Color color;
    float size, life, maxLife;
    float gravity = 1.0f;
    float drag = 0.0f;
    float rot = 0, rotSpeed = 0;
    bool additive = false;
    bool blood = false;   // leaves a decal when it hits the world
    bool collide = false;
    int tex = 0;          // index into Effects::m_particleTex
    float growth = 0;     // size change per second
};

struct Decal {
    std::vector<Vector3> pts;
    std::vector<Vector2> uvs;
    Color color;
    std::string tex;
    float life;  // < 0 = permanent
    float fade;
};

struct TimedLight {
    DynamicLight light;
    float life, maxLife;
};

struct ParticleParams {
    int count = 10;
    Color color = WHITE;
    float speed = 100;
    float spread = 1.0f;   // 0 = along dir, 1 = sphere
    float size = 3;
    float sizeVar = 1;
    float life = 1.0f;
    float gravity = 1.0f;
    float drag = 0.5f;
    bool additive = false;
    bool blood = false;
    bool collide = false;
    std::string texture = "sprites/particle.png";
    float growth = 0;
};

class Effects {
public:
    void Init();
    void Clear();
    void Update(float dt, const CollisionWorld& world, const Map& map, const Lightmap& lm, float gravity);
    void Draw(const Camera3D& cam);
    void DrawDecals();

    void Emit(Vector3 pos, Vector3 dir, const ParticleParams& p);
    void Blood(Vector3 pos, Vector3 dir, int amount, Vector3 light);
    void Spark(Vector3 pos, Vector3 normal, Vector3 light);
    void AddDecal(const CollisionWorld& world, const Map& map, const Lightmap& lm, Vector3 pos, Vector3 normal, int brush,
                  int face, const std::string& tex, float size, Color tint, float life = -1);
    void AddLight(Vector3 pos, Vector3 color, float radius, float life);
    void CollectLights(std::vector<DynamicLight>& out) const;
    size_t ParticleCount() const { return m_particles.size(); }

private:
    std::vector<Particle> m_particles;
    std::vector<Decal> m_decals;
    std::vector<TimedLight> m_lights;
    std::vector<std::string> m_particleTex;
    int TexIndex(const std::string& path);
    struct PendingDecal {
        Vector3 pos, normal;
        int brush, face;
        float size;
    };
    std::vector<PendingDecal> m_pendingBlood;
};

}  // namespace rbs
