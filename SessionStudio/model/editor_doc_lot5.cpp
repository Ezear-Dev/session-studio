// Session Studio -- EditorDoc, lot 5: landing part of a trick clip, key interpolation (smooth / linear / step),
// timeline rows grouped by limb with clear bone names.
#include "editor_doc.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kKeyEps = 1e-3f;
bool Has(const std::string& n, const char* s) { return n.find(s) != std::string::npos; }
int SideOf(const std::string& n) { return Has(n, "_L_") ? 0 : Has(n, "_R_") ? 1 : -1; }
// finger number (0 thumb .. 4 little) of "AMXX_L_Finger21" -> 2, segment -> 1; -1 if not a finger
int FingerOf(const std::string& n, int* segment = nullptr) {
    const size_t p = n.find("Finger");
    if (p == std::string::npos || p + 6 >= n.size()) return -1;
    const char f = n[p + 6];
    if (f < '0' || f > '4') return -1;
    if (segment) *segment = (p + 7 < n.size() && n[p + 7] >= '0' && n[p + 7] <= '9') ? n[p + 7] - '0' : 0;
    return f - '0';
}
// limb group of a bone: 0 head, 1 chest, 2 arm L, 3 hand L, 4 arm R, 5 hand R, 6 leg L, 7 leg R; -1 none
int GroupIndex(const std::string& n) {
    if (Has(n, "SKXX_") || Has(n, "IK_UPNODE")) return -1;
    if (Has(n, "_Neck") || Has(n, "_Head")) return 0;
    if (Has(n, "Spine")) return 1;
    const int s = SideOf(n);
    if (s < 0) return -1;
    if (Has(n, "Clavicle") || Has(n, "UpperArm") || Has(n, "Forearm")) return 2 + s * 2;
    if (Has(n, "_Hand") || Has(n, "Finger")) return 3 + s * 2;
    if (IsLegBone(n)) return 6 + s;
    return -1;
}
const char* kGroupId[8] = {"head", "chest", "armL", "handL", "armR", "handR", "legL", "legR"};
const char* kGroupLabel[8] = {"Head", "Chest", "L Arm", "L Hand", "R Arm", "R Hand", "L Leg", "R Leg"};
const char* kFinger[5] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
} // namespace

std::string BoneLabel(const std::string& n) {
    const int s = SideOf(n);
    const std::string side = s == 0 ? "L " : s == 1 ? "R " : "";
    int seg = 0;
    const int f = FingerOf(n, &seg);
    if (f >= 0) return side + kFinger[f] + (seg ? " " + std::to_string(seg + 1) : std::string());
    struct P { const char* part; const char* label; };
    static const P kParts[] = {{"Spine1", "Lower back"}, {"Spine2", "Mid back"}, {"Spine3", "Upper back"}, {"_Neck", "Neck"},
                               {"_Head", "Head"}, {"Clavicle", "Shoulder"}, {"UpperArm", "Arm"}, {"Forearm", "Forearm"},
                               {"Hand_Xtra", "Hand (extra)"}, {"_Hand", "Hand"}, {"Thigh", "Thigh"}, {"Calf", "Calf"},
                               {"Foot", "Foot"}, {"Toe", "Toes"}, {"Pelvis", "Pelvis"}};
    for (const P& p : kParts) if (Has(n, p.part)) return side + p.label;
    return n.rfind("AMXX_", 0) == 0 ? n.substr(5) : n;
}

// ------------------------------------------------------------------ landing part
float EditorDoc::GroundAt() const {
    const float t = PhaseStart(FigureRecorder::kGroundPhase);
    return t > 0.f ? t : -1.f;
}

float EditorDoc::GroundTail() const {
    const float g = GroundAt();
    return g > 0.f ? std::max(0.f, FigureDuration() - g) : 0.f;
}

// ------------------------------------------------------------------ interpolation
void EditorDoc::SetKeyInterp(float t, int bone, int interp) {
    auto apply = [&](bool& smooth, bool& step) { smooth = interp == IP_Smooth; step = interp == IP_Step; };
    const int only = bone >= 0 ? TrackIndex(bone) : -1;
    for (size_t i = 0; i < clip_.tracks.size(); i++) {
        if (bone >= 0 && (int)i != only) continue;
        for (auto& k : clip_.tracks[i].keys) if (std::fabs(k.t - t) < kKeyEps) apply(k.smooth, k.step);
    }
    if (bone < 0 || bone == PelvisBone()) for (auto& k : clip_.pelvis) if (std::fabs(k.t - t) < kKeyEps) apply(k.smooth, k.step);
}

int EditorDoc::KeyInterp(float t, int bone) const {
    auto of = [](bool smooth, bool step) { return step ? IP_Step : smooth ? IP_Smooth : IP_Linear; };
    const int only = bone >= 0 ? TrackIndex(bone) : -1;
    for (size_t i = 0; i < clip_.tracks.size(); i++) {
        if (bone >= 0 && (int)i != only) continue;
        for (auto& k : clip_.tracks[i].keys) if (std::fabs(k.t - t) < kKeyEps) return of(k.smooth, k.step);
    }
    if (bone < 0 || bone == PelvisBone()) for (auto& k : clip_.pelvis) if (std::fabs(k.t - t) < kKeyEps) return of(k.smooth, k.step);
    return -1;
}

// ------------------------------------------------------------------ grouped rows
std::vector<EditorDoc::KeyGroup> EditorDoc::KeyGroups(bool allEditable) const {
    std::vector<KeyGroup> groups(8);
    for (int g = 0; g < 8; g++) { groups[g].id = kGroupId[g]; groups[g].label = kGroupLabel[g]; }
    const int n = BoneCount();
    for (int b = 0; b < n; b++) {
        const std::string& name = skel_.names[b];
        const int g = GroupIndex(name);
        if (g < 0) continue;
        const bool shown = TrackOf(b) != nullptr || (allEditable && IsEditable(b));
        if (!shown) continue;
        KeyGroup& G = groups[g];
        G.bones.push_back(b);
        const int f = FingerOf(name);
        if (f >= 0) {   // one row per finger (its 3 segments)
            const std::string label = std::string(SideOf(name) == 0 ? "L " : "R ") + kFinger[f];
            auto it = std::find_if(G.children.begin(), G.children.end(), [&](const KeyRow& r) { return r.label == label; });
            if (it == G.children.end()) G.children.push_back(KeyRow{label, {b}, b});
            else it->bones.push_back(b);
        } else G.children.push_back(KeyRow{BoneLabel(name), {b}, b});
    }
    std::vector<KeyGroup> out;
    for (auto& G : groups) if (!G.bones.empty()) out.push_back(std::move(G));
    const int pel = PelvisBone();
    if (pel >= 0 && (!clip_.pelvis.empty() || allEditable)) {
        KeyGroup G;
        G.id = "pelvis"; G.label = "Pelvis"; G.bones = {pel};
        out.push_back(std::move(G));   // no children: one row is enough
    }
    return out;
}

std::string EditorDoc::GroupOfBone(int bone) const {
    if (bone < 0 || bone >= BoneCount()) return "";
    if (bone == PelvisBone()) return "pelvis";
    const int g = GroupIndex(skel_.names[bone]);
    return g < 0 ? std::string() : std::string(kGroupId[g]);
}
