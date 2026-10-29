// RBS Engine - animators: state machines that pick a model's animations (like Unity's
// Animator). An animator asset (animators/<name>.json) has parameters, states that play a
// clip (or blend clips by a parameter), transitions with conditions and crossfades, and
// animation events. Entities use one through their `animator` property.
#pragma once

#include "core/common.h"
#include "render/animation.h"

#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace rbs {

using json = nlohmann::json;

struct AnimEventDef {
    float time = 0;        // normalized time in the clip (0 = start, 1 = end)
    std::string name;      // passed to the script callback anim_event(self, name)
    std::string sound;     // played at the entity without any code
    float volume = 1;
};

struct AnimMotion {
    std::string clip;
    float at = 0;          // blend parameter value where this clip plays alone
};

struct AnimStateDef {
    std::string name, clip;
    bool loop = true;
    float speed = 1;
    std::string speedParam;  // playback speed is multiplied by param / speedRef
    float speedRef = 1;
    std::string blendParam;  // 1D blend: clips mixed by this parameter (instead of `clip`)
    std::vector<AnimMotion> blend;
    std::vector<AnimEventDef> events;
};

enum class AnimOp { Greater, Less, GreaterEq, LessEq, Equal, NotEqual, True, False, Trigger };

struct AnimCondition {
    std::string param;
    AnimOp op = AnimOp::True;
    float value = 0;
};

struct AnimTransitionDef {
    int from = -1;           // -1 = Any State
    int to = 0;
    float duration = 0.15f;  // crossfade in seconds
    float exitTime = -1;     // >= 0: only once the clip reached this normalized time
    std::vector<AnimCondition> conditions;
};

struct AnimParamDef {
    std::string name, type;  // float, int, bool, trigger
    float value = 0;
};

struct AnimatorAsset {
    std::vector<AnimParamDef> params;
    std::vector<AnimStateDef> states;
    std::vector<AnimTransitionDef> transitions;
    int defaultState = 0;
    int FindState(const std::string& name) const;
};

struct AnimatorInstance {
    std::string path;
    std::shared_ptr<const AnimatorAsset> asset;
    std::map<std::string, float> params;  // bools are 0 / 1
    std::set<std::string> triggers;       // set since the last update
    int state = -1;
    float norm = 0, lastNorm = 0;         // normalized time in the state (1 = one pass of the clip)
    int prev = -1;                        // state faded out during a crossfade
    float prevNorm = 0;
    float fade = 1, fadeTime = 0;
};

namespace animator {

// Parameters the engine sets every frame: speed (horizontal), vertical_speed, on_ground,
// dead, health.
extern const std::vector<std::string> kAutoParams;
extern const std::vector<std::string> kOps;  // condition operators as written in the files

AnimatorAsset Parse(const json& j, std::string* error = nullptr);
// Cached by path; nullptr (with a log message) when the file is missing or invalid.
std::shared_ptr<const AnimatorAsset> Load(const std::string& path);
void ClearCache();
std::shared_ptr<AnimatorInstance> Create(const std::string& path);

// Runs transitions and advances time; adds the animation events passed to `fired` (they
// point into the asset, so keep the instance alive while handling them).
void Update(AnimatorInstance& a, const ModelAsset& m, float dt, std::vector<const AnimEventDef*>& fired);
// Pose to draw (current state, crossfaded from the previous one). False without clips.
bool BuildPose(const AnimatorInstance& a, const ModelAsset& m, anim::Pose& out);
const AnimStateDef* CurrentState(const AnimatorInstance& a);

}  // namespace animator
}  // namespace rbs
