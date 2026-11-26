// 2026-11-25 (user report): turning a thigh with the rings of a trick clip spins it in every direction.
// Before the fix the ring was put on the pose after the pelvis offset, whose leg re-solve rewrote the thigh: with the
// Ollie clip (pelvis keys) the leg drifted up to 1.15 deg per 3-degree step and spun in the editor.
// Replays the editor's rotation gesture (app.cpp, ROTATE branch) frame by frame on the user's real recordings:
// each frame the ring adds a small turn about the bone's own red axis to the bone shown, the editor turns it into a
// local rotation under the parent, and the next frame shows the bone again. The bone shown must follow the ring.
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../src/math.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../../../SessionStudio/model/view_math.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {
FTr DispG(const FTr& t) { FTr d = t; d.py = -t.py; d.qx = -t.qx; d.qz = -t.qz; return d; }
Quat UndispG(Quat q) { return Quat{-q.x, q.y, -q.z, q.w}; }
float AngleDeg(Quat a, Quat b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.f * std::acos(std::fmin(1.f, d)) * 57.29578f;
}
bool ReadAllG(const std::string& p, std::string& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t k;
    out.clear();
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    return true;
}
}

TEST(gizmo_rotation_follows_the_ring_on_real_legs) {
    const std::string dir = "C:/Program Files (x86)/Steam/steamapps/common/Session/SessionGame/Binaries/Win64/Mods/SessionAnimLoader/clips/";
    std::string skel, fig, clipText, err;
    if (!ReadAllG(dir + "_skeleton.json", skel) || !ReadAllG(dir + "_figures/KickFlip_RGS_regular.json", fig)) return;
    for (const char* clipName : {"", "KickFlip_RGS_regular.sanim", "Ollie_RGS_regular.sanim"}) {
        EditorDoc d;
        CHECK(d.LoadSkeleton(skel, err));
        FigureVariant v;
        CHECK(ParseFigureKey("KickFlip_RGS_regular", v));
        CHECK(d.LoadFigure(fig, v, err));
        d.NewClip(v);
        if (*clipName && ReadAllG(dir + clipName, clipText)) { if (!d.OpenClip(clipText, clipName, err)) { printf("gizmo: %s %s\n", clipName, err.c_str()); continue; } }
        const SkeletonFile& s = d.skeleton();
        for (const char* boneName : {"AMXX_L_Thigh", "AMXX_L_Calf", "AMXX_R_Thigh", "AMXX_L_UpperArm"}) {
            int b = -1;
            for (int i = 0; i < (int)s.names.size(); i++) if (s.names[i] == boneName) b = i;
            if (b < 0 || !d.IsEditable(b)) { printf("gizmo: %s not editable\n", boneName); continue; }
            for (float t : {0.1f, 0.3f, 0.5f}) {
                d.DiscardPending();
                float worst = 0.f;
                std::vector<FTr> cs;
                for (int frame = 0; frame < 30; frame++) {
                    d.ComponentPose(t, cs, false, true, false);   // the pose the ring turns: before the pelvis offset (app.cpp csR)
                    float m[16];
                    BoneMatrix(DispG(cs[b]), m);
                    Quat shown;
                    CHECK(MatrixRotation(m, shown));
                    // the ring (LOCAL, red axis = the bone's own X) adds 3 degrees
                    const Quat want = QNorm(QMul(shown, Quat{std::sin(1.5f / 57.29578f), 0.f, 0.f, std::cos(1.5f / 57.29578f)}));
                    const Quat q = UndispG(want);
                    const int p = s.parents[b];
                    const Quat parentQ = p >= 0 ? TrQ(cs[p]) : QIdent;
                    d.SetPending(b, QMul(QInv(parentQ), q));
                    d.ComponentPose(t, cs, false, true, false);
                    const float e = AngleDeg(TrQ(cs[b]), q);
                    if (e > worst) worst = e;
                }
                printf("gizmo: clip '%s' %s t=%.1f worst %.2f deg\n", clipName, boneName, t, worst);
                CHECK(worst < 0.5f);
            }
        }
    }
}

// the same with the IK arrows on a foot, on a clip that moves the pelvis (Ollie: -16.7 cm at 1 s): the foot shown
// goes where the arrow is, without a jump when the gesture starts
TEST(leg_ik_follows_the_arrow_with_a_pelvis_offset) {
    const std::string dir = "C:/Program Files (x86)/Steam/steamapps/common/Session/SessionGame/Binaries/Win64/Mods/SessionAnimLoader/clips/";
    std::string skel, fig, clipText, err;
    if (!ReadAllG(dir + "_skeleton.json", skel) || !ReadAllG(dir + "_figures/KickFlip_RGS_regular.json", fig) || !ReadAllG(dir + "Ollie_RGS_regular.sanim", clipText)) return;
    EditorDoc d;
    CHECK(d.LoadSkeleton(skel, err));
    FigureVariant v;
    CHECK(ParseFigureKey("KickFlip_RGS_regular", v));
    CHECK(d.LoadFigure(fig, v, err));
    CHECK(d.OpenClip(clipText, "Ollie_RGS_regular.sanim", err));
    const SkeletonFile& s = d.skeleton();
    int foot = -1;
    for (int i = 0; i < (int)s.names.size(); i++) if (s.names[i] == "AMXX_L_Foot") foot = i;
    CHECK(foot >= 0);
    if (foot < 0) return;
    for (float t : {0.5f, 1.0f, 1.2f}) {
        d.DiscardPending();
        std::vector<FTr> cs;
        d.ComponentPose(t, cs, false, true);   // shown (with the offset)
        const float start[3] = {cs[foot].px, cs[foot].py, cs[foot].pz};
        d.IkBegin(foot, t);
        float worst = 0.f;
        for (int frame = 1; frame <= 20; frame++) {
            const float target[3] = {start[0] + 0.4f * frame, start[1], start[2] + 0.3f * frame};
            d.IkMoveJoint(foot, target, true, false, t);
            d.ComponentPose(t, cs, false, true);
            const float e = std::sqrt((cs[foot].px - target[0]) * (cs[foot].px - target[0]) + (cs[foot].py - target[1]) * (cs[foot].py - target[1]) + (cs[foot].pz - target[2]) * (cs[foot].pz - target[2]));
            if (e > worst) worst = e;
        }
        d.IkEnd();
        printf("gizmo: leg IK with pelvis offset t=%.1f worst %.2f cm\n", t, worst);
        CHECK(worst < 1.f);
    }
}
