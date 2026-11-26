// Goofy mirror: left/right bone pairing and frame convention of each bone.
//
// Model: the mirrored skeleton satisfies F'_other(i) = M F_i C_i, where M is the reflection through the body's
// middle plane and C_i a per-bone reflection (diagonal of signs, determinant -1), the same for i and its
// twin. We write C = -R(c), with c a 180 degree rotation around a local axis, or the identity:
//   conv 0 = I (C = -I), 1 = X (C = diag(-1,1,1)), 2 = Y (C = diag(1,-1,1)), 3 = Z (C = diag(1,1,-1)).
// Then a local rotation L of bone i (parent p) becomes C_p L C_i on the twin bone:
//   replace / capture: k' = c_p k c_i^-1        additive (bone offset on its own axes): k' = c_i k c_i^-1
// The conventions are deduced from the reference positions (local offset of the children): p_child_twin = C_p p_child.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "math.h"

struct MirrorMap {
    std::vector<int> other;     // other[i]: the twin bone (i itself in the centre or without a twin)
    std::vector<bool> center;   // true if the name holds neither "_L_" nor "_R_"
    std::vector<int> parent;    // copy of the parents (for the replace formula)
    std::vector<int> conv;      // convention of each bone (0 I, 1 X, 2 Y, 3 Z), deduced from the reference positions
};
// refPos: 3 floats per bone (reference local position); empty -> default conventions (side I, centre Z).
MirrorMap BuildMirrorMap(const std::vector<std::string>& names, const std::vector<int>& parents, const std::vector<float>& refPos);

// Effective conventions: those of the map, then the overrides (bone index, conv) applied to the bone and its twin.
std::vector<int> EffectiveConv(const MirrorMap& map, const std::vector<std::pair<int, int>>& overrides);

// k' = c_parent * k * c_bone^-1
Quat MirrorKey(Quat k, int convParent, int convBone);
// "I", "X", "Y", "Z" -> 0..3; -1 if unknown
int ConvFromName(const std::string& s);
