#include "game/animator.h"
#include "core/log.h"
#include "core/vfs.h"

namespace rbs {

int AnimatorAsset::FindState(const std::string& name) const {
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == name) return (int)i;
    return -1;
}

namespace animator {

const std::vector<std::string> kAutoParams = {"speed", "vertical_speed", "on_ground", "dead", "health"};
const std::vector<std::string> kOps = {">", "<", ">=", "<=", "==", "!=", "true", "false", "trigger"};

static std::map<std::string, std::shared_ptr<const AnimatorAsset>> g_cache;

static std::string Str(const json& j, const char* key, const std::string& def = "") {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}

static float Num(const json& j, const char* key, float def) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_number()) return it->get<float>();
    if (it->is_boolean()) return it->get<bool>() ? 1.0f : 0.0f;
    return def;
}

static bool Bool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number()) return it->get<float>() != 0;
    return def;
}

static AnimOp ParseOp(const std::string& s) {
    for (size_t i = 0; i < kOps.size(); ++i)
        if (kOps[i] == s) return (AnimOp)i;
    return AnimOp::True;
}

AnimatorAsset Parse(const json& j, std::string* error) {
    AnimatorAsset a;
    if (!j.is_object()) {
        if (error) *error = "not a JSON object";
        return a;
    }
    if (j.contains("parameters") && j["parameters"].is_array()) {
        for (auto& p : j["parameters"]) {
            if (!p.is_object() || Str(p, "name").empty()) continue;
            AnimParamDef d;
            d.name = Str(p, "name");
            d.type = Str(p, "type", "float");
            d.value = Num(p, "value", 0);
            a.params.push_back(d);
        }
    }
    if (j.contains("states") && j["states"].is_array()) {
        for (auto& s : j["states"]) {
            if (!s.is_object() || Str(s, "name").empty()) continue;
            AnimStateDef d;
            d.name = Str(s, "name");
            d.clip = Str(s, "clip", d.name);
            d.loop = Bool(s, "loop", true);
            d.speed = Num(s, "speed", 1);
            d.speedParam = Str(s, "speed_param");
            d.speedRef = std::max(0.001f, Num(s, "speed_ref", 1));
            d.blendParam = Str(s, "blend_param");
            if (s.contains("blend") && s["blend"].is_array())
                for (auto& m : s["blend"])
                    if (m.is_object() && !Str(m, "clip").empty()) d.blend.push_back({Str(m, "clip"), Num(m, "at", 0)});
            std::sort(d.blend.begin(), d.blend.end(), [](const AnimMotion& x, const AnimMotion& y) { return x.at < y.at; });
            if (s.contains("events") && s["events"].is_array())
                for (auto& e : s["events"])
                    if (e.is_object()) d.events.push_back({Num(e, "time", 0), Str(e, "name"), Str(e, "sound"), Num(e, "volume", 1)});
            a.states.push_back(d);
        }
    }
    if (a.states.empty()) {
        if (error) *error = "no states";
        return a;
    }
    a.defaultState = std::max(0, a.FindState(Str(j, "default", a.states[0].name)));
    if (j.contains("transitions") && j["transitions"].is_array()) {
        for (auto& t : j["transitions"]) {
            if (!t.is_object()) continue;
            AnimTransitionDef d;
            std::string from = Str(t, "from"), to = Str(t, "to");
            d.from = from == "*" || from == "any" ? -1 : a.FindState(from);
            d.to = a.FindState(to);
            if (d.to < 0 || (d.from < 0 && from != "*" && from != "any")) continue;  // links to missing states
            d.duration = std::max(0.0f, Num(t, "duration", 0.15f));
            d.exitTime = t.contains("exit_time") && t["exit_time"].is_number() ? t["exit_time"].get<float>() : -1;
            if (t.contains("conditions") && t["conditions"].is_array())
                for (auto& c : t["conditions"])
                    if (c.is_object() && !Str(c, "param").empty()) d.conditions.push_back({Str(c, "param"), ParseOp(Str(c, "op", "true")), Num(c, "value", 0)});
            a.transitions.push_back(d);
        }
    }
    return a;
}

std::shared_ptr<const AnimatorAsset> Load(const std::string& path) {
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return it->second;
    std::shared_ptr<const AnimatorAsset> result;
    std::string text, error;
    if (!vfs::ReadText(path, text)) {
        LOG_WARN("Missing animator: %s", path.c_str());
    } else {
        json j = json::parse(text, nullptr, false, true);
        auto a = std::make_shared<AnimatorAsset>(Parse(j, &error));
        if (a->states.empty()) LOG_WARN("Invalid animator %s: %s", path.c_str(), error.empty() ? "invalid JSON" : error.c_str());
        else result = a;
    }
    g_cache[path] = result;
    return result;
}

void ClearCache() { g_cache.clear(); }

std::shared_ptr<AnimatorInstance> Create(const std::string& path) {
    auto asset = Load(path);
    if (!asset) return nullptr;
    auto a = std::make_shared<AnimatorInstance>();
    a->path = path;
    a->asset = asset;
    for (auto& p : asset->params) a->params[p.name] = p.value;
    return a;
}

static float Param(const AnimatorInstance& a, const std::string& name) {
    auto it = a.params.find(name);
    return it == a.params.end() ? 0.0f : it->second;
}

// clip index of a name (model animation names are stored lower case)
static int Clip(const ModelAsset& m, const std::string& name) { return m.FindAnim(ToLower(name)); }

// blend state: the two clips around the parameter value and the weight of the second
static void BlendPair(const AnimatorInstance& a, const AnimStateDef& s, int& i0, int& i1, float& w) {
    float v = Param(a, s.blendParam);
    int n = (int)s.blend.size();
    i0 = i1 = 0;
    w = 0;
    if (v <= s.blend[0].at) return;
    if (v >= s.blend[n - 1].at) {
        i0 = i1 = n - 1;
        return;
    }
    for (int i = 0; i + 1 < n; ++i) {
        if (v <= s.blend[i + 1].at) {
            i0 = i, i1 = i + 1;
            float span = s.blend[i1].at - s.blend[i0].at;
            w = span > 0 ? (v - s.blend[i0].at) / span : 0;
            return;
        }
    }
}

// seconds one pass of the state takes at speed 1
static float StateLength(const AnimatorInstance& a, const ModelAsset& m, const AnimStateDef& s) {
    float len;
    if (!s.blend.empty()) {
        int i0, i1;
        float w;
        BlendPair(a, s, i0, i1, w);
        len = Lerpf(anim::ClipLength(m, Clip(m, s.blend[i0].clip)), anim::ClipLength(m, Clip(m, s.blend[i1].clip)), w);
    } else {
        len = anim::ClipLength(m, Clip(m, s.clip));
    }
    return len > 0.001f ? len : 1.0f;
}

static float StateSpeed(const AnimatorInstance& a, const AnimStateDef& s) {
    float speed = s.speed;
    if (!s.speedParam.empty()) speed *= Param(a, s.speedParam) / s.speedRef;
    return speed;
}

static bool ConditionsMet(const AnimatorInstance& a, const AnimTransitionDef& t) {
    for (auto& c : t.conditions) {
        float v = Param(a, c.param);
        bool ok = true;
        switch (c.op) {
            case AnimOp::Greater: ok = v > c.value; break;
            case AnimOp::Less: ok = v < c.value; break;
            case AnimOp::GreaterEq: ok = v >= c.value; break;
            case AnimOp::LessEq: ok = v <= c.value; break;
            case AnimOp::Equal: ok = fabsf(v - c.value) < 1e-4f; break;
            case AnimOp::NotEqual: ok = fabsf(v - c.value) >= 1e-4f; break;
            case AnimOp::True: ok = v != 0 || a.triggers.count(c.param); break;
            case AnimOp::False: ok = v == 0; break;
            case AnimOp::Trigger: ok = a.triggers.count(c.param) > 0; break;
        }
        if (!ok) return false;
    }
    return true;
}

static bool UsesTrigger(const AnimTransitionDef& t) {
    for (auto& c : t.conditions)
        if (c.op == AnimOp::Trigger || c.op == AnimOp::True) return true;
    return false;
}

static void Enter(AnimatorInstance& a, int state, float fade) {
    if (a.state >= 0 && fade > 0) {
        a.prev = a.state;
        a.prevNorm = a.norm;
        a.fade = 0;
        a.fadeTime = fade;
    } else {
        a.prev = -1;
        a.fade = 1;
    }
    a.state = state;
    a.norm = 0;
    a.lastNorm = -1e-4f;  // events at time 0 fire on entry
}

void Update(AnimatorInstance& a, const ModelAsset& m, float dt, std::vector<const AnimEventDef*>& fired) {
    const AnimatorAsset* asset = a.asset.get();
    if (!asset || asset->states.empty()) return;
    if (a.state < 0 || a.state >= (int)asset->states.size()) Enter(a, asset->defaultState, 0);

    // transitions: Any State first (death, hit reactions...), then the current state's own
    bool entered = false;
    for (int pass = 0; pass < 2 && !entered; ++pass) {
        for (auto& t : asset->transitions) {
            if (pass == 0 ? t.from != -1 : t.from != a.state) continue;
            // a state restarts itself only from Any State, on a trigger (hit again while in pain)
            if (t.to == a.state && (pass == 1 || !UsesTrigger(t))) continue;
            if (t.exitTime >= 0 && a.norm < t.exitTime) continue;
            if (!ConditionsMet(a, t)) continue;
            for (auto& c : t.conditions) a.triggers.erase(c.param);
            Enter(a, t.to, t.duration);
            entered = true;
            break;
        }
    }

    // time
    const AnimStateDef& s = asset->states[a.state];
    if (!entered) a.lastNorm = a.norm;
    a.norm += dt * StateSpeed(a, s) / StateLength(a, m, s);
    if (!s.loop) a.norm = std::min(a.norm, 1.0f);
    if (a.prev >= 0) {
        const AnimStateDef& p = asset->states[a.prev];
        a.prevNorm += dt * StateSpeed(a, p) / StateLength(a, m, p);
        if (!p.loop) a.prevNorm = std::min(a.prevNorm, 1.0f);
        a.fade = a.fadeTime > 0 ? a.fade + dt / a.fadeTime : 1;
        if (a.fade >= 1) {
            a.fade = 1;
            a.prev = -1;
        }
    }

    // events passed this frame (a looping clip can pass the same event once per loop)
    for (auto& e : s.events) {
        int first = (int)floorf(std::max(0.0f, a.lastNorm)), last = (int)floorf(a.norm);
        if (!s.loop) first = last = 0;
        for (int k = first; k <= last; ++k) {
            float t = k + e.time;
            if (t > a.lastNorm && t <= a.norm) fired.push_back(&e);
        }
    }
    a.triggers.clear();
}

static bool SampleState(const AnimatorInstance& a, const ModelAsset& m, const AnimStateDef& s, float norm, anim::Pose& out) {
    auto timeIn = [&](int clip) {
        float len = anim::ClipLength(m, clip);
        float n = s.loop ? norm - floorf(norm) : std::min(norm, 1.0f);
        return n * len;
    };
    if (!s.blend.empty()) {
        int i0, i1;
        float w;
        BlendPair(a, s, i0, i1, w);
        int c0 = Clip(m, s.blend[i0].clip), c1 = Clip(m, s.blend[i1].clip);
        if (!anim::Sample(m, c0, timeIn(c0), s.loop, out)) return false;
        if (i1 != i0 && w > 0) {
            static anim::Pose other;
            if (anim::Sample(m, c1, timeIn(c1), s.loop, other)) anim::Blend(out, other, w);
        }
        return true;
    }
    int c = Clip(m, s.clip);
    return anim::Sample(m, c, timeIn(c), s.loop, out);
}

bool BuildPose(const AnimatorInstance& a, const ModelAsset& m, anim::Pose& out) {
    const AnimatorAsset* asset = a.asset.get();
    if (!asset || a.state < 0 || a.state >= (int)asset->states.size()) return false;
    bool ok = SampleState(a, m, asset->states[a.state], a.norm, out);
    if (a.prev >= 0 && a.fade < 1) {
        static anim::Pose from;
        if (SampleState(a, m, asset->states[a.prev], a.prevNorm, from)) {
            if (!ok) {
                out = from;
                return true;
            }
            anim::Blend(from, out, a.fade);
            out.swap(from);
            ok = true;
        }
    }
    return ok;
}

const AnimStateDef* CurrentState(const AnimatorInstance& a) {
    if (!a.asset || a.state < 0 || a.state >= (int)a.asset->states.size()) return nullptr;
    return &a.asset->states[a.state];
}

}  // namespace animator
}  // namespace rbs
