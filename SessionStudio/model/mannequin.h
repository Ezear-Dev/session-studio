// Session Studio -- the 3D mannequin (PLAN-MANNEQUIN stage 1, 2026-11-26): a solid body generated around the
// 70-bone skeleton, so a pose reads at a glance (what is in front, which way a limb is twisted). Pure geometry, no
// graphics: the renderer (app/stage_renderer) only draws the triangles. Rigid parts follow their bone (no skinning);
// the pose is always the editor's own (EditorDoc::ComponentPose), never computed here.
#pragma once
#include <cstdint>
#include <vector>
#include "../../SessionAnimLoader/Native/src/math.h"
#include "../../SessionAnimLoader/Native/src/skeleton.h"

struct MeshVertex { float pos[3]; float nrm[3]; uint32_t rgba; };   // UE component space (cm); rgba = R | G<<8 | B<<16 | A<<24
struct Mesh {
    std::vector<MeshVertex> verts;
    std::vector<uint32_t> idx;
    std::vector<int16_t> triBone;   // bone of each triangle (idx.size() / 3 entries)
    void Clear() { verts.clear(); idx.clear(); triBone.clear(); }
};

// colour of the stripe drawn on the +Y side of every limb (a twisted bone stays visible)
constexpr uint32_t kMannequinStripe = 0xFF50D8FFu;   // R 255, G 216, B 80
// colour of the marker on the forehead that shows where the head looks (no face: user, 2026-11-26)
constexpr uint32_t kMannequinLook = 0xFFF0A040u;     // R 64, G 160, B 240

struct MannequinRig {
    int pelvis = -1, spine1 = -1, spine2 = -1, spine3 = -1, neck = -1, head = -1;
    int clav[2] = {-1, -1}, upper[2] = {-1, -1}, fore[2] = {-1, -1}, hand[2] = {-1, -1};   // [0] left, [1] right
    int thigh[2] = {-1, -1}, calf[2] = {-1, -1}, foot[2] = {-1, -1}, toe[2] = {-1, -1};
    int thumb[2] = {-1, -1}, thumbTip[2] = {-1, -1};      // Finger0, Finger02 (or Finger01)
    int index[2] = {-1, -1}, middle[2] = {-1, -1}, pinky[2] = {-1, -1};   // Finger1, Finger2, Finger4 bases
    int finger[2][5][3] = {};         // 2026-11-26: every finger joint (FingerN, FingerN1, FingerN2), -1 when absent
    // 2026-11-26 (user: feet not flat): the shoe follows the FOOT BONE, not the ankle -> toe line (the toe bone is at the
    // ball of the foot on the ground, the ankle 10 cm above: that line slopes down). From the standing calibration pose:
    // the sole's forward and up directions in foot bone space, and the ankle height above the sole.
    bool shoeOk[2] = {false, false};
    float shoeFwd[2][3] = {}, shoeUp[2][3] = {};
    float ankleH[2] = {10.f, 10.f}, footLen[2] = {14.f, 14.f};   // footLen: ankle -> toe bone along the sole
    float frontSign = 1.f;            // TorsoAxes: front = frontSign * (up x side), fixed by the calibration pose
    float headFront[3] = {1, 0, 0};   // the face direction in head bone space (from the calibration pose)
    float headUp[3] = {1, 0, 0};      // the top of the head in head bone space
    bool ok = false;                  // pelvis, spine and head found
};

// bones found by name; the calibration pose (a standing or riding frame, else the current pose; may be empty) fixes
// which way the torso and the head face. Toes point along the front more than against it.
MannequinRig BuildMannequinRig(const SkeletonFile& s, const std::vector<FTr>& calibration);
// unit axes of the torso (UE component space): side = towards the skater's left, up, front. false if not computable.
bool TorsoAxes(const MannequinRig& r, const std::vector<FTr>& cs, float side[3], float up[3], float front[3]);
// the body for pose cs (component space). boneRgba: one colour per bone (missing entries: light grey). A part whose
// bones are absent, not finite, or of zero length is skipped. Under 20 000 triangles.
// stripeBone: the yellow stripe goes on that bone only (-1: none, -2: every limb)
void BuildMannequinMesh(const MannequinRig& r, const std::vector<FTr>& cs, const std::vector<uint32_t>& boneRgba, Mesh& out, int stripeBone = -2);
// 2026-11-27 (redesign step 3): height of the editor's floor for these poses (usually samples of the game's animation):
// 0, or lower when the body goes below 0: in tricks the recorded body goes up to ~40 cm below the component origin
// (measured: KickFlip -41, Ollie -30, Landing -16 cm), and the soles sit a little under the toe bones. Never above 0.
float GroundHeight(const MannequinRig& r, const std::vector<std::vector<FTr>>& poses);
