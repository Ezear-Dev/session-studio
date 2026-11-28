// Session Studio -- an optional skinned body for the 3D view, read from a .glb file (binary glTF 2.0) that lies next to
// the editor. The file is never part of the repository. Without it (or if it does not fit the skeleton) the 3D view
// draws the generated mannequin (mannequin.h). The pose is always the editor's own: this only deforms the mesh.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "mannequin.h"

struct BodyVertex {
    float pos[3], nrm[3];   // bind pose, UE component space (cm)
    int joint[4];           // index into BodyModel::joints
    float weight[4];        // sum 1
    uint32_t rgba;          // the colour of its part
};
// the colours of the parts (R | G<<8 | B<<16 | A<<24), chosen from words in the material's name: the files read so far
// hold no colour for the clothes. A name without a known word (shoes, anything else) is kBodyOther.
constexpr uint32_t kBodySkin = 0xFF82A0CDu;       // R 205, G 160, B 130
constexpr uint32_t kBodyTop = 0xFF8C6E5Au;        // R 90, G 110, B 140
constexpr uint32_t kBodyTrousers = 0xFF50696Eu;   // R 110, G 105, B 80
constexpr uint32_t kBodyHat = 0xFF323C96u;        // R 150, G 60, B 50
constexpr uint32_t kBodyDark = 0xFF232D3Cu;       // R 60, G 45, B 35: hair and eyes
constexpr uint32_t kBodyOther = 0xFFD2D2D2u;      // R 210, G 210, B 210
struct BodyModel {
    std::vector<BodyVertex> verts;
    std::vector<uint32_t> idx;
    std::vector<std::string> joints;
    std::vector<FTr> bind;  // each joint's bind pose, UE component space
};

// The skinned mesh of a .glb file given as bytes, converted to UE axes and cm. All its primitives are joined; see-through
// ones (alphaMode BLEND) are left out, a single colour would make them opaque. false + err: not a usable file.
bool LoadBodyModel(const std::string& bytes, BodyModel& out, std::string& err);

struct BodyRig {
    std::vector<int> bone;        // skeleton bone of each joint (-1: a joint no vertex uses and the skeleton lacks)
    std::vector<char> striped;    // per skeleton bone: a limb that can carry the twist stripe
    bool ok = false;
};
// joints are matched to bones by name; false + err when a joint that moves vertices has no bone
bool BindBodyModel(const BodyModel& m, const SkeletonFile& s, BodyRig& out, std::string& err);

// The body for pose cs (component space). boneTint: one tint per skeleton bone, its alpha = how much it covers the part's
// colour (0 or a missing entry: not at all); a vertex mixes the tints of its bones by weight. stripeBone: on that limb, the surface facing the bone's +Y gets the yellow
// stripe, so a twist stays visible. Triangles that need a bone with no usable pose are left out.
void SkinBodyModel(const BodyModel& m, const BodyRig& r, const std::vector<FTr>& cs, const std::vector<uint32_t>& boneTint, Mesh& out, int stripeBone = -1);
