// P6 lot 2: pelvis offset. The pelvis (and everything it carries) is moved by an offset in component space, but the
// feet stay where they were: each leg is re-solved with a two-bone IK (bone lengths kept, the knee keeps its side,
// the foot keeps its orientation). Shared by the player (in game) and the editor (preview), so both show the same.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "math.h"

struct PelvisRig {
    int pelvis = -1;
    int thigh[2] = {-1, -1}, calf[2] = {-1, -1}, foot[2] = {-1, -1};   // [left, right]
    std::vector<uint8_t> moves;    // 1: moved rigidly with the pelvis (pelvis subtree minus the feet subtrees)
    std::vector<int8_t> footSide;  // 0 / 1: bone in the subtree of that foot (foot included), -1 otherwise
    bool Ok() const { return pelvis >= 0 && thigh[0] >= 0 && thigh[1] >= 0 && calf[0] >= 0 && calf[1] >= 0 && foot[0] >= 0 && foot[1] >= 0; }
};

// by bone names (AMXX_Pelvis, AMXX_L_Thigh...); parents before children. Not Ok() if a bone is missing.
PelvisRig BuildPelvisRig(const std::vector<std::string>& names, const int* parents, int n);
// hip axis (unit, left thigh minus right thigh) of a pose: the axis the regular <-> goofy mirror flips
bool HipAxis(const FTr* cs, const PelvisRig& r, float out[3]);
// offset written for the other body side: its component along the hip axis changes sign
void MirrorOffset(const float axis[3], float v[3]);
// largest s in [0, 1]: the pelvis moved by s * off keeps both feet within reach (ApplyPelvisOffset uses it)
float PelvisReachScale(const FTr* cs, const PelvisRig& r, const float off[3]);
// moves the pelvis by off (shortened to the legs' reach), feet kept. False if nothing was done (rig not Ok, offset ~0).
bool ApplyPelvisOffset(FTr* cs, int n, const PelvisRig& r, const float off[3]);
