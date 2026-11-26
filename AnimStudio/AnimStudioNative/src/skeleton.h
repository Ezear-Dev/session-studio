// Skeleton file (clips\_skeleton.json): written by the DLL, read by the editor.
#pragma once
#include <string>
#include <vector>

struct SkeletonFile {
    int version = 1;
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<float> refPos;   // 3 floats per bone: reference local position (cm)
    std::vector<int> conv;       // mirror convention per bone (0 I, 1 X, 2 Y, 3 Z), _studio.json overrides included
};

// Leg bones: the name holds Thigh, Calf, Foot or Toe, except IK_UPNODE, SKXX_ and Anchor (shared DLL / editor).
bool IsLegBone(const std::string& name);

std::string SkeletonToJson(const SkeletonFile& s);
// Refused: version != 1, inconsistent sizes, parent >= index (or < -1), conv outside 0..3, unreadable JSON.
bool ParseSkeleton(const std::string& text, SkeletonFile& out, std::string& err);
