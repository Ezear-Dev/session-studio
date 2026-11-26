// Shoulder check and fix (2026-11-25): arm raised far above the shoulder / rolled on its own axis deforms the mesh.
//   asn_tests --shoulder <clips dir> <clip.sanim> [--fix]  -> per key: elevation, rolls, clavicle; --fix writes <clip>.fixed
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../../../SessionStudio/model/ik.h"
#include <fstream>
#include <sstream>

namespace {
std::string SkelS() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
std::string IdleFig() {   // standing: every bone at identity, 1 s
    FigureRec f; f.figure = "OnFoot_RGS_regular"; f.bones = (int)RefNames().size();
    for (int i = 0; i < 31; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QIdent);
    }
    return FigureToJson(f);
}
int Bone(const EditorDoc& d, const char* n) { for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == n) return b; return -1; }
float Dist(const FTr& a, const FTr& b) { return std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) + (a.pz - b.pz) * (a.pz - b.pz)); }
}

TEST(shoulder_fix_keeps_the_hand_and_relieves_the_shoulder) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(SkelS(), err));
    const FigureVariant v{ST_OnFoot, "", STANCE_Normal, false};
    CHECK(d.LoadFigure(IdleFig(), v, err));
    d.NewClip(v);
    const int u = Bone(d, "AMXX_R_UpperArm"), h = Bone(d, "AMXX_R_Hand");
    CHECK(d.ArmSide(u) == 1 && d.ArmSide(Bone(d, "AMXX_L_Finger21")) == 0 && d.ArmSide(Bone(d, "AMXX_Head")) == -1);
    const EditorDoc::ShoulderInfo base = d.Shoulder(1, 0.f);
    CHECK(base.ok && !base.warn);
    // raise the arm as far as possible from the reference pose (search), then roll it 50 deg on its own axis
    const float ax[3] = {-1.f, 0.f, 0.f};
    Quat best = QIdent; float bestE = 0.f;
    for (int k = 0; k < 3; k++) for (int s = -1; s <= 1; s += 2) {
        float a[3] = {0, 0, 0}; a[k] = 1.f;
        const Quat q = AxisAngleDeg(a, 120.f * s);
        d.SetPending(u, q);
        const float e = d.Shoulder(1, 0.f).elevation;
        if (e > bestE) { bestE = e; best = q; }
    }
    d.SetPending(u, QMul(best, AxisAngleDeg(ax, 50.f)));
    const EditorDoc::ShoulderInfo bad = d.Shoulder(1, 0.f);
    CHECK(bad.elevation > EditorDoc::kWarnElevation);
    CHECK(std::fabs(bad.twistUpper) > 40.f);
    CHECK(bad.warn);
    std::vector<FTr> before, after;
    d.ComponentPose(0.f, before, false);
    CHECK(d.FixShoulder(1, 0.f, true, true));
    d.ComponentPose(0.f, after, false);
    CHECK(Dist(before[h], after[h]) < 0.5f);                                  // the hand stays
    CHECK(std::fabs(QDot(TrQ(before[h]), TrQ(after[h]))) > 0.9995f);          // and keeps its orientation
    const EditorDoc::ShoulderInfo good = d.Shoulder(1, 0.f);
    CHECK(good.clavRaise > 5.f && good.clavRaise <= EditorDoc::kShoulderMax + 0.5f);
    CHECK(std::fabs(good.twistUpper) < std::fabs(bad.twistUpper) - 10.f);
    std::printf("shoulder test: bad e %.1f tw %.1f cl %.1f | good e %.1f tw %.1f fore %.1f cl %.1f\n", bad.elevation, bad.twistUpper, bad.clavRaise, good.elevation, good.twistUpper, good.twistFore, good.clavRaise);
    // the left arm is untouched
    CHECK(Dist(before[Bone(d, "AMXX_L_Hand")], after[Bone(d, "AMXX_L_Hand")]) < 1e-3f);
}

// ------------------------------------------------------------------ command line
namespace {
bool ReadAll(const std::string& p, std::string& out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::stringstream s; s << f.rdbuf(); out = s.str(); return true; }
}
int ShoulderMain(const char* dir, const char* clipName, bool fix) {
    std::string sk, cl, fg, err;
    const std::string d(dir);
    if (!ReadAll(d + "\\_skeleton.json", sk) || !ReadAll(d + "\\" + clipName, cl)) { std::printf("cannot read\n"); return 2; }
    EditorDoc doc;
    if (!doc.LoadSkeleton(sk, err)) { std::printf("skeleton: %s\n", err.c_str()); return 2; }
    if (!doc.OpenClip(cl, clipName, err)) { std::printf("clip: %s\n", err.c_str()); return 2; }
    const FigureVariant v = doc.Variant();
    if (!ReadAll(d + "\\_figures\\" + FigureKey(v) + ".json", fg) || !doc.LoadFigure(fg, v, err)) { std::printf("move %s: %s\n", FigureKey(v).c_str(), err.c_str()); return 2; }
    const std::vector<float> times = doc.KeyTimes(-1);
    for (int pass = 0; pass < (fix ? 2 : 1); pass++) {
        if (pass == 1) {
            for (float t : times) for (int side = 0; side < 2; side++) {
                bool arm = false;
                for (auto& tr : doc.clip().tracks) if (tr.index >= 0 && doc.ArmSide(tr.index) == side) arm = true;
                if (!arm) continue;
                doc.FixShoulder(side, t, true, true);
                doc.CommitKey(t);
            }
            std::printf("--- after the fix\n");
        }
        for (float t : times) for (int side = 0; side < 2; side++) {
            const EditorDoc::ShoulderInfo s = doc.Shoulder(side, t);
            if (!s.ok) continue;
            std::printf("t %.2f %s : elevation %5.1f, roll upper %+6.1f, forearm %+6.1f, clavicle %+5.1f%s\n", t, side ? "R" : "L",
                        s.elevation, s.twistUpper, s.twistFore, s.clavRaise, s.warn ? "  <-- risk" : "");
        }
    }
    if (fix) {
        const std::string text = doc.SaveText(err);
        if (text.empty()) { std::printf("save refused: %s\n", err.c_str()); return 3; }
        std::ofstream o(d + "\\" + clipName + ".fixed", std::ios::binary);
        o << text;
        std::printf("written %s.fixed\n", clipName);
    }
    return 0;
}
