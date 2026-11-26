// Session Studio -- analytic "two bone" IK (arm -> forearm -> hand), in UE component space.
// Method: always start from the BASE pose of the gesture;
//   1) the forearm bends around its hinge (arm x forearm axis) to the angle given by the law of cosines;
//   2) the whole chain swings by the shortest arc towards the target;
//   3) elbowDeltaDeg turns the elbow around the shoulder -> target axis (relative to the gesture).
// So the forearm only moves as a hinge, without twist, and no fixed reference can flip.
#pragma once
#include "../../SessionAnimLoader/Native/src/math.h"

// shortest rotation that brings direction a onto direction b (any non-zero vectors)
Quat ShortestArc(const float a[3], const float b[3]);
// rotation of angle deg around the (unit) axis
Quat AxisAngleDeg(const float axis[3], float deg);

// New rotations IN COMPONENT SPACE. upper/fore/hand: base pose of the gesture. The target is clamped to the reach.
// keepHandWorld: the hand keeps its orientation, else it follows the forearm.
// elbow delta (degrees, around the upper -> end axis) that brings the elbow (fore) closest to the target;
// NaN if degenerate (straight arm, or target on the axis). Same direction as elbowDeltaDeg of SolveTwoBoneIK.
float SwivelToward(const FTr& upper, const FTr& fore, const FTr& end, const float target[3]);
// rotation IN COMPONENT SPACE of bone, by the shortest arc, that points bone -> child at the target; false if degenerate
bool AimBone(const FTr& bone, const FTr& child, const float target[3], Quat& outBone);

bool SolveTwoBoneIK(const FTr& upper, const FTr& fore, const FTr& hand, const float target[3], float elbowDeltaDeg,
                    bool keepHandWorld, Quat& outUpper, Quat& outFore, Quat& outHand);
