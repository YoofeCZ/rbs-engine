// RBS Engine - skeletal animation poses: sampling clips, blending poses and skinning a
// model with the result. raylib only plays one clip at a time; animators (crossfades,
// blend states) need poses that can be mixed.
#pragma once

#include "render/assets.h"

#include <vector>

namespace rbs {
namespace anim {

// Model-space bone transforms (raylib stores clips this way).
using Pose = std::vector<Transform>;

// Clip length in seconds (clips are sampled at 60 frames per second).
float ClipLength(const ModelAsset& m, int clip);
// Pose of `clip` at `time` seconds; wraps when looping, clamps otherwise. Frames are
// interpolated. False if the clip has no frames.
bool Sample(const ModelAsset& m, int clip, float time, bool loop, Pose& out);
// a = mix of a and b (w = 0 keeps a, 1 gives b).
void Blend(Pose& a, const Pose& b, float w);
// Skins the model's meshes with the pose (CPU, uploaded to the GPU) - call right before
// drawing, as the model is shared by every entity that uses it.
void Apply(ModelAsset& m, const Pose& pose);

}  // namespace anim
}  // namespace rbs
