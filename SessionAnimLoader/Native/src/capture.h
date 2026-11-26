// Pose capture: the game's pose (local rotations) written as a `replace` .sanim clip, in the regular stance.
#pragma once
#include <string>
#include <vector>
#include "math.h"
#include "mirror.h"

// mirrored: the capture was taken in a mirrored stance (goofy XOR switch); it then goes through the same mirror as the player
// (replace formula, conv = effective conventions, see EffectiveConv).
std::string CaptureToJson(const Quat* local, const std::vector<std::string>& names, bool mirrored,
                          const MirrorMap& map, const std::vector<int>& conv, const std::string& name);
