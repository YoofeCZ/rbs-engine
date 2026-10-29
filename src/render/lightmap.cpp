#include "render/lightmap.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/vfs.h"
#include "render/assets.h"

#include <filesystem>
#include <fstream>

namespace rbs {

static constexpr int kMaxAtlas = 4096;
static constexpr int kMaxFaceLuxels = 384;
static constexpr uint32_t kCacheVersion = 3;

static Cvar* lm_bounce_samples = nullptr;
static Cvar* lm_ao_samples = nullptr;
static Cvar* lm_threads = nullptr;

static void RegisterCvars() {
    if (lm_bounce_samples) return;
    lm_bounce_samples = cvars::Register("lm_bounce_samples", "32", true, "rays per luxel for bounce/sky light");
    lm_ao_samples = cvars::Register("lm_ao_samples", "12", true, "rays per luxel for ambient occlusion");
    lm_threads = cvars::Register("lm_threads", "0", true, "lightmap baking threads (0 = all cores)");
}

static uint32_t Hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static float Rand01(uint32_t& s) {
    s = Hash32(s + 0x9e3779b9u);
    return (s & 0xffffff) / 16777216.0f;
}

static void Basis(Vector3 n, Vector3& t, Vector3& b) {
    Vector3 up = fabsf(n.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    t = Norm(Cross(up, n));
    b = Cross(n, t);
}

static Vector3 CosineSample(Vector3 n, uint32_t& seed) {
    float r1 = Rand01(seed), r2 = Rand01(seed);
    float phi = 2 * kPi * r1;
    float r = sqrtf(r2);
    Vector3 t, b;
    Basis(n, t, b);
    return Norm(t * (cosf(phi) * r) + b * (sinf(phi) * r) + n * sqrtf(fmaxf(0, 1 - r2)));
}

Lightmap::~Lightmap() {
    if (m_master.joinable()) m_master.join();
    Unload();
}

void Lightmap::Unload() {
    if (m_texture.id) UnloadTexture(m_texture);
    m_texture = Texture2D{};
}

std::vector<BakeLight> Lightmap::CollectLights(const Map& map) {
    std::vector<BakeLight> out;
    for (auto& e : map.entities) {
        if (e.classname != "light" && e.classname != "light_spot") continue;
        if (e.Num("dynamic", 0) != 0) continue;  // runtime-only light
        BakeLight l;
        l.pos = e.origin;
        Vector3 c = e.Vec("color", {255, 230, 200});
        if (c.x > 1.0f || c.y > 1.0f || c.z > 1.0f) c = c / 255.0f;
        l.color = c;
        l.intensity = e.Num("intensity", 1.0f);
        l.radius = e.Num("radius", 300.0f);
        float cone = e.Num("spot", e.classname == "light_spot" ? 45.0f : 0.0f);
        if (cone > 0) {
            l.spot = true;
            float pitch = e.Num("pitch", -90.0f);
            l.dir = AnglesForward(pitch, e.angle);
            l.coneCos = cosf(cone * 0.5f * DEG2RAD);
            l.coneOuterCos = cosf(fminf(cone * 0.5f + e.Num("spot_soft", 15.0f), 179.0f) * DEG2RAD);
        }
        out.push_back(l);
    }
    return out;
}

static bool FaceNeedsLightmap(const BrushFace& f) {
    if (f.poly.size() < 3) return false;
    if (f.tex.name == "sky" || assets::IsToolTexture(f.tex.name)) return false;
    return true;
}

bool Lightmap::Allocate(Map& map, float luxel) {
    struct Item {
        int brush, face, w, h;
    };
    std::vector<Item> items;
    for (size_t b = 0; b < map.brushes.size(); ++b) {
        for (size_t fi = 0; fi < map.brushes[b].faces.size(); ++fi) {
            BrushFace& f = map.brushes[b].faces[fi];
            f.lmValid = false;
            if (!FaceNeedsLightmap(f)) continue;
            int axis = DominantAxis(f.plane.n);
            float smin = 1e30f, smax = -1e30f, tmin = 1e30f, tmax = -1e30f;
            for (auto& v : f.poly) {
                float s, t;
                ProjectST(v, axis, s, t);
                smin = fminf(smin, s), smax = fmaxf(smax, s);
                tmin = fminf(tmin, t), tmax = fmaxf(tmax, t);
            }
            float ls = luxel;
            for (int iter = 0; iter < 8; ++iter) {
                int w = (int)(ceilf(smax / ls) - floorf(smin / ls)) + 1;
                int h = (int)(ceilf(tmax / ls) - floorf(tmin / ls)) + 1;
                if (w <= kMaxFaceLuxels && h <= kMaxFaceLuxels) break;
                ls *= 2;
            }
            f.lmAxis = axis;
            f.lmLuxel = ls;
            f.lmSMin = floorf(smin / ls);
            f.lmTMin = floorf(tmin / ls);
            f.lmW = (int)(ceilf(smax / ls) - f.lmSMin) + 1;
            f.lmH = (int)(ceilf(tmax / ls) - f.lmTMin) + 1;
            items.push_back({(int)b, (int)fi, f.lmW + 2, f.lmH + 2});
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.h > b.h; });
    long long area = 0;
    for (auto& it : items) area += (long long)it.w * it.h;
    int width = 256;
    while ((long long)width * width < area * 1.3 && width < kMaxAtlas) width *= 2;
    for (;;) {
        int x = 0, y = 0, rowH = 0;
        bool ok = true;
        for (auto& it : items) {
            if (it.w > width) { ok = false; break; }
            if (x + it.w > width) {
                x = 0;
                y += rowH;
                rowH = 0;
            }
            BrushFace& f = map.brushes[it.brush].faces[it.face];
            f.lmX = x;
            f.lmY = y;
            f.lmValid = true;
            x += it.w;
            rowH = std::max(rowH, it.h);
        }
        int height = y + rowH;
        if (ok && height <= kMaxAtlas) {
            m_width = width;
            m_height = std::max(4, height);
            m_faces.clear();
            for (auto& it : items) m_faces.push_back({it.brush, it.face});
            return true;
        }
        if (width >= kMaxAtlas) return false;
        width *= 2;
    }
}

Vector3 Lightmap::LuxelWorldPos(const BrushFace& f, int i, int j) const {
    float s = (f.lmSMin + i) * f.lmLuxel, t = (f.lmTMin + j) * f.lmLuxel;
    Vector3 n = f.plane.n;
    float d = f.plane.d;
    Vector3 p;
    if (f.lmAxis == 0) p = {(d - n.y * t - n.z * s) / n.x, t, s};
    else if (f.lmAxis == 1) p = {s, (d - n.x * s - n.z * t) / n.y, t};
    else p = {s, t, (d - n.x * s - n.y * t) / n.z};
    // pull points outside the polygon back inside so corners don't sample solid space
    const auto& poly = f.poly;
    bool inside = true;
    for (size_t k = 0; k < poly.size(); ++k) {
        Vector3 a = poly[k], b = poly[(k + 1) % poly.size()];
        if (Dot(Cross(b - a, p - a), n) < 0) { inside = false; break; }
    }
    if (!inside) {
        float best = 1e30f;
        Vector3 bp = p;
        for (size_t k = 0; k < poly.size(); ++k) {
            Vector3 a = poly[k], b = poly[(k + 1) % poly.size()];
            Vector3 ab = b - a;
            float tt = Clampf(Dot(p - a, ab) / fmaxf(Dot(ab, ab), 1e-6f), 0, 1);
            Vector3 c = a + ab * tt;
            float dd = Vector3DistanceSqr(c, p);
            if (dd < best) best = dd, bp = c;
        }
        Vector3 centroid{0, 0, 0};
        for (auto& v : poly) centroid += v;
        centroid = centroid / (float)poly.size();
        Vector3 toC = centroid - bp;
        float l = Len(toC);
        p = bp + (l > 1.0f ? toC / l : toC) * fminf(1.0f, l);
    }
    return p;
}

Vector3 Lightmap::DirectLight(Vector3 p, Vector3 n, uint32_t seed) const {
    const MapSettings& ms = m_map->settings;
    Vector3 origin = p + n * 0.75f;
    Vector3 total{0, 0, 0};
    for (const BakeLight& l : m_lights) {
        Vector3 L = l.pos - p;
        float dist = Len(L);
        if (dist > l.radius || dist < 0.001f) continue;
        Vector3 Ln = L / dist;
        float ndotl = Dot(n, Ln);
        if (ndotl <= 0) continue;
        float atten = 1.0f - (dist * dist) / (l.radius * l.radius);
        atten *= atten;
        float spot = 1.0f;
        if (l.spot) {
            float c = Dot(-Ln, l.dir);
            if (c <= l.coneOuterCos) continue;
            spot = Clampf((c - l.coneOuterCos) / fmaxf(l.coneCos - l.coneOuterCos, 0.0001f), 0, 1);
            spot = spot * spot * (3 - 2 * spot);
        }
        TraceResult tr = m_occluders.TraceRay(origin, l.pos);
        if (tr.fraction < 0.999f) continue;
        total += l.color * (l.intensity * atten * spot * (ndotl * 0.75f + 0.25f));
    }
    if (ms.sunIntensity > 0) {
        Vector3 L = Norm(-ms.sunDir);
        float ndotl = Dot(n, L);
        if (ndotl > 0) {
            TraceResult tr = m_occluders.TraceRay(origin, origin + L * 32768.0f);
            bool lit = !tr.Hit();
            if (!lit && tr.brush >= 0 && tr.face >= 0) lit = m_map->brushes[tr.brush].faces[tr.face].tex.name == "sky";
            if (lit) total += ms.sunColor * (ms.sunIntensity * ndotl);
        }
    }
    return total;
}

Vector3 Lightmap::SampleDirect(int brush, int face, Vector3 p) const {
    const BrushFace& f = m_map->brushes[brush].faces[face];
    if (!f.lmValid) return {0, 0, 0};
    float s, t;
    ProjectST(p, f.lmAxis, s, t);
    float u = Clampf(s / f.lmLuxel - f.lmSMin, 0, (float)(f.lmW - 1));
    float v = Clampf(t / f.lmLuxel - f.lmTMin, 0, (float)(f.lmH - 1));
    int i0 = (int)u, j0 = (int)v;
    int i1 = std::min(i0 + 1, f.lmW - 1), j1 = std::min(j0 + 1, f.lmH - 1);
    float fu = u - i0, fv = v - j0;
    auto at = [&](int i, int j) { return m_direct[(size_t)(f.lmY + 1 + j) * m_width + (f.lmX + 1 + i)]; };
    Vector3 a = at(i0, j0) * (1 - fu) + at(i1, j0) * fu;
    Vector3 b = at(i0, j1) * (1 - fu) + at(i1, j1) * fu;
    return a * (1 - fv) + b * fv;
}

Vector3 Lightmap::Gather(Vector3 p, Vector3 n, uint32_t seed) const {
    const MapSettings& ms = m_map->settings;
    int samples = std::max(4, lm_bounce_samples->i);
    Vector3 origin = p + n * 0.75f;
    Vector3 sum{0, 0, 0};
    for (int k = 0; k < samples; ++k) {
        Vector3 dir = CosineSample(n, seed);
        TraceResult tr = m_occluders.TraceRay(origin, origin + dir * 2048.0f);
        bool sky = !tr.Hit();
        if (!sky && tr.brush >= 0 && tr.face >= 0) {
            const BrushFace& hf = m_map->brushes[tr.brush].faces[tr.face];
            if (hf.tex.name == "sky") {
                sky = true;
            } else if (!tr.startSolid) {
                auto it = m_albedo.find(hf.tex.name);
                Vector3 albedo = it != m_albedo.end() ? it->second : Vector3{0.5f, 0.5f, 0.5f};
                if (IsFullbright(hf.tex.name)) sum += albedo * 1.5f;
                else sum += Mul(SampleDirect(tr.brush, tr.face, tr.endpos), albedo) * ms.bounce;
            }
        }
        if (sky && ms.skyLight > 0) {
            float h = Clampf(dir.y, 0, 1);
            Vector3 skyCol = ms.skyHorizon * (1 - h) + ms.skyZenith * h;
            sum += skyCol * ms.skyLight;
        }
    }
    return sum / (float)samples;
}

void Lightmap::Worker(int phase) {
    const MapSettings& ms = m_map->settings;
    int aoSamples = ms.ao > 0 ? std::max(0, lm_ao_samples->i) : 0;
    for (;;) {
        int idx = m_next.fetch_add(1);
        if (idx >= (int)m_faces.size()) break;
        const FaceRef& fr = m_faces[idx];
        const BrushFace& f = m_map->brushes[fr.brush].faces[fr.face];
        Vector3 n = f.plane.n;
        bool fullbright = IsFullbright(f.tex.name);
        for (int j = 0; j < f.lmH; ++j) {
            for (int i = 0; i < f.lmW; ++i) {
                size_t pix = (size_t)(f.lmY + 1 + j) * m_width + (f.lmX + 1 + i);
                uint32_t seed = Hash32((uint32_t)idx * 7919u + (uint32_t)(j * 4099 + i));
                Vector3 p = LuxelWorldPos(f, i, j);
                if (phase == 0) {
                    if (fullbright) {
                        m_direct[pix] = {1, 1, 1};
                        continue;
                    }
                    m_direct[pix] = DirectLight(p, n, seed);
                } else {
                    if (fullbright) {
                        m_final[pix] = {1, 1, 1};
                        continue;
                    }
                    Vector3 indirect = ms.ambient;
                    if (ms.bounce > 0 || ms.skyLight > 0) indirect += Gather(p, n, seed);
                    float ao = 1.0f;
                    if (aoSamples > 0) {
                        int occluded = 0;
                        Vector3 origin = p + n * 0.75f;
                        for (int k = 0; k < aoSamples; ++k) {
                            Vector3 dir = CosineSample(n, seed);
                            TraceResult tr = m_occluders.TraceRay(origin, origin + dir * 40.0f);
                            if (tr.Hit() && !tr.startSolid) occluded++;
                        }
                        ao = 1.0f - ms.ao * (float)occluded / (float)aoSamples;
                    }
                    m_final[pix] = m_direct[pix] * (0.75f + 0.25f * ao) + indirect * ao;
                }
            }
        }
        m_doneCount.fetch_add(1);
    }
}

void Lightmap::RunPhases() {
    int threads = lm_threads->i > 0 ? lm_threads->i : (int)std::thread::hardware_concurrency();
    threads = std::max(1, std::min(threads, 32));
    for (int phase = 0; phase < 2; ++phase) {
        m_next = 0;
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) pool.emplace_back(&Lightmap::Worker, this, phase);
        for (auto& t : pool) t.join();
        m_phase = phase + 1;
    }
    // to 8 bit with 2x overbright + edge padding
    m_pixels.assign((size_t)m_width * m_height, Color{0, 0, 0, 255});
    for (auto& fr : m_faces) {
        const BrushFace& f = m_map->brushes[fr.brush].faces[fr.face];
        for (int j = -1; j <= f.lmH; ++j) {
            for (int i = -1; i <= f.lmW; ++i) {
                int si = std::max(0, std::min(i, f.lmW - 1));
                int sj = std::max(0, std::min(j, f.lmH - 1));
                Vector3 c = m_final[(size_t)(f.lmY + 1 + sj) * m_width + (f.lmX + 1 + si)] * 0.5f;
                m_pixels[(size_t)(f.lmY + 1 + j) * m_width + (f.lmX + 1 + i)] = ColorFromVec(c);
            }
        }
    }
    SaveCache();
    m_phase = 3;
}

static uint64_t Fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

void Lightmap::Start(Map& map, const std::string& cacheName) {
    RegisterCvars();
    if (m_master.joinable()) m_master.join();
    Unload();
    m_map = &map;
    m_fromCache = false;
    m_phase = 0;
    m_doneCount = 0;
    m_lights = CollectLights(map);
    float luxel = std::max(1.0f, map.settings.lightmapScale);
    while (!Allocate(map, luxel)) luxel *= 1.5f;
    m_total = (int)m_faces.size() * 2;

    // occluders: everything except clip/trigger-only brushes
    m_occluders.Build(map, [](const Brush& b) {
        for (auto& f : b.faces)
            if (f.tex.name != "clip" && f.tex.name != "trigger") return true;
        return false;
    });

    m_hash = Fnv1a(map.ToJson().dump()) ^ (uint64_t)kCacheVersion ^
             ((uint64_t)lm_bounce_samples->i << 40) ^ ((uint64_t)lm_ao_samples->i << 48);
    m_cacheFile.clear();
    if (!cacheName.empty()) {
        m_cacheFile = (std::filesystem::path(vfs::WriteDir()) / "cache" / (cacheName + ".lmcache")).string();
        if (LoadCache()) {
            m_fromCache = true;
            m_phase = 3;
            return;
        }
    }
    // texture albedo for bounce light (loaded on the main thread before workers start)
    m_albedo.clear();
    m_fullbright.clear();
    for (auto& b : map.brushes) {
        for (auto& f : b.faces) {
            if (m_albedo.count(f.tex.name)) continue;
            Vector3 avg{0.5f, 0.5f, 0.5f};
            for (const char* ext : {".png", ".tga", ".jpg", ".bmp"}) {
                std::string path = vfs::Resolve("textures/" + f.tex.name + ext);
                if (path.empty()) continue;
                Image img = LoadImage(path.c_str());
                if (img.data) {
                    ImageResize(&img, 8, 8);
                    Color* px = LoadImageColors(img);
                    Vector3 s{0, 0, 0};
                    for (int i = 0; i < 64; ++i) s += VecFromColor(px[i]);
                    avg = s / 64.0f;
                    UnloadImageColors(px);
                    UnloadImage(img);
                }
                break;
            }
            m_fullbright[f.tex.name] = assets::GetTexture(f.tex.name).fullbright;
            m_albedo[f.tex.name] = avg;
        }
    }
    m_direct.assign((size_t)m_width * m_height, Vector3{0, 0, 0});
    m_final.assign((size_t)m_width * m_height, Vector3{0, 0, 0});
    LOG_INFO("Baking lightmap %dx%d, %d faces, %d lights...", m_width, m_height, (int)m_faces.size(), (int)m_lights.size());
    m_master = std::thread(&Lightmap::RunPhases, this);
}

float Lightmap::Progress() const {
    if (m_phase >= 3) return 1.0f;
    return m_total > 0 ? (float)m_doneCount.load() / (float)m_total : 0.0f;
}

bool Lightmap::Done() const { return m_phase >= 3; }

bool Lightmap::Finish() {
    if (m_master.joinable()) m_master.join();
    if (m_pixels.empty()) return false;
    Image img{m_pixels.data(), m_width, m_height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Unload();
    m_texture = LoadTextureFromImage(img);
    SetTextureFilter(m_texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(m_texture, TEXTURE_WRAP_CLAMP);
    m_direct.clear();
    m_direct.shrink_to_fit();
    m_final.clear();
    m_final.shrink_to_fit();
    if (!m_fromCache) LOG_INFO("Lightmap baked.");
    return true;
}

void Lightmap::MakeFullbright(Map& map) {
    if (m_master.joinable()) m_master.join();
    Unload();
    m_map = &map;
    m_lights.clear();
    float luxel = 64.0f;
    while (!Allocate(map, luxel)) luxel *= 2;
    m_pixels.assign((size_t)m_width * m_height, Color{128, 128, 128, 255});
    Image img{m_pixels.data(), m_width, m_height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    m_texture = LoadTextureFromImage(img);
    m_phase = 3;
}

bool Lightmap::LoadCache() {
    std::ifstream f(m_cacheFile, std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t ver = 0;
    uint64_t hash = 0;
    int32_t w = 0, h = 0;
    f.read(magic, 4);
    f.read((char*)&ver, 4);
    f.read((char*)&hash, 8);
    f.read((char*)&w, 4);
    f.read((char*)&h, 4);
    if (!f || memcmp(magic, "RBSL", 4) != 0 || ver != kCacheVersion || hash != m_hash || w != m_width || h != m_height) return false;
    m_pixels.resize((size_t)w * h);
    f.read((char*)m_pixels.data(), (std::streamsize)m_pixels.size() * 4);
    if (!f) {
        m_pixels.clear();
        return false;
    }
    LOG_INFO("Lightmap loaded from cache (%dx%d)", w, h);
    return true;
}

void Lightmap::SaveCache() {
    if (m_cacheFile.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_cacheFile).parent_path(), ec);
    std::ofstream f(m_cacheFile, std::ios::binary);
    if (!f) return;
    int32_t w = m_width, h = m_height;
    f.write("RBSL", 4);
    f.write((const char*)&kCacheVersion, 4);
    f.write((const char*)&m_hash, 8);
    f.write((const char*)&w, 4);
    f.write((const char*)&h, 4);
    f.write((const char*)m_pixels.data(), (std::streamsize)m_pixels.size() * 4);
}

Vector3 Lightmap::SampleFace(const Map& map, int brush, int face, Vector3 p) const {
    if (brush < 0 || brush >= (int)map.brushes.size() || m_pixels.empty()) return {1, 1, 1};
    if (face < 0 || face >= (int)map.brushes[brush].faces.size()) return {1, 1, 1};
    const BrushFace& f = map.brushes[brush].faces[face];
    if (!f.lmValid) return {1, 1, 1};
    float s, t;
    ProjectST(p, f.lmAxis, s, t);
    int i = (int)Clampf(roundf(s / f.lmLuxel - f.lmSMin), 0, (float)(f.lmW - 1));
    int j = (int)Clampf(roundf(t / f.lmLuxel - f.lmTMin), 0, (float)(f.lmH - 1));
    Color c = m_pixels[(size_t)(f.lmY + 1 + j) * m_width + (f.lmX + 1 + i)];
    return VecFromColor(c) * 2.0f;
}

void Lightmap::LightAtPoint(const Map& map, const CollisionWorld& world, Vector3 p, Vector3& ambient, Vector3& dirColor, Vector3& dir) const {
    ambient = map.settings.ambient;
    dirColor = {0, 0, 0};
    dir = {0, 1, 0};
    if (m_lights.empty() && m_pixels.empty()) {
        ambient = {1, 1, 1};
        return;
    }
    TraceResult tr = world.TraceRay(p, p - Vector3{0, 512, 0});
    if (tr.Hit() && !tr.startSolid && tr.brush >= 0) ambient = SampleFace(map, tr.brush, tr.face, tr.endpos) * 0.8f;
    Vector3 accDir{0, 0, 0};
    for (const BakeLight& l : m_lights) {
        Vector3 L = l.pos - p;
        float dist = Len(L);
        if (dist > l.radius || dist < 0.001f) continue;
        float atten = 1.0f - (dist * dist) / (l.radius * l.radius);
        atten *= atten;
        if (l.spot && Dot(-(L / dist), l.dir) <= l.coneOuterCos) continue;
        TraceResult t2 = world.TraceRay(p, l.pos);
        if (t2.fraction < 0.98f) continue;
        Vector3 c = l.color * (l.intensity * atten);
        dirColor += c;
        accDir += (L / dist) * (c.x + c.y + c.z);
    }
    const MapSettings& ms = map.settings;
    if (ms.sunIntensity > 0) {
        Vector3 L = Norm(-ms.sunDir);
        TraceResult t2 = world.TraceRay(p, p + L * 32768.0f);
        bool lit = !t2.Hit();
        if (!lit && t2.brush >= 0 && t2.face >= 0 && t2.brush < (int)map.brushes.size()) lit = map.brushes[t2.brush].faces[t2.face].tex.name == "sky";
        if (lit) {
            Vector3 c = ms.sunColor * ms.sunIntensity;
            dirColor += c;
            accDir += L * (c.x + c.y + c.z);
        }
    }
    if (Len(accDir) > 0.0001f) dir = Norm(accDir);
    dirColor = dirColor * 0.6f;
}

}  // namespace rbs
