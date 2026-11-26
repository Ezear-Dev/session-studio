// IK pulled by the elbow, the shoulder and the knee (IkHandle / IkMoveJoint)
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <cmath>

namespace {
int JIdx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }

FigureRec JFig(const char* name, bool withPhases) {
    FigureRec r; r.figure = name; r.bones = (int)RefNames().size();
    for (int i = 0; i < 31; i++) {
        r.times.push_back(i / 30.f); r.ratios.push_back(i / 30.f);
        for (int b = 0; b < r.bones; b++)
            r.frames.push_back(QFromRotatorDeg((float)(b % 5) * 4.f + i * 0.7f, (float)(b % 7) * 3.f + 10.f, (float)(b % 3) * 5.f + (float)i));
        if (withPhases) r.phases.push_back(i == 0 ? 0 : i < 8 ? 1 : i < 15 ? 2 : i < 20 ? 3 : 4);
    }
    return r;
}

EditorDoc JDoc(int figure, const char* figName, bool phases = false) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(SkeletonToJson(s), err));
    CHECK(d.LoadFigure(FigureToJson(JFig(figName, phases)), err));
    d.NewClip(figure);
    d.clip().duration = 1.f;
    return d;
}

struct V { float x, y, z; };
V P(const EditorDoc& d, int b, float t) { std::vector<FTr> cs; d.ComponentPose(t, cs, false, true); return V{cs[b].px, cs[b].py, cs[b].pz}; }
V Sub(V a, V b) { return V{a.x - b.x, a.y - b.y, a.z - b.z}; }
V Add(V a, V b) { return V{a.x + b.x, a.y + b.y, a.z + b.z}; }
V Mul(V a, float k) { return V{a.x * k, a.y * k, a.z * k}; }
float L(V a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
float Dist(V a, V b) { return L(Sub(a, b)); }
float Ang(V a, V b) { const float c = (a.x * b.x + a.y * b.y + a.z * b.z) / (L(a) * L(b)); return std::acos(c > 1.f ? 1.f : c < -1.f ? -1.f : c) * 57.2957795f; }
bool Finite(Quat q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
const float kT = 0.5f;
} // namespace

TEST(p5c_handle_mapping) {
    EditorDoc d = JDoc(FIG_NoseManual, "NoseManual");
    int r, u, f, e;
    CHECK(d.IkHandle(JIdx("AMXX_L_Hand"), r, u, f, e) == IkJoint::End && e == JIdx("AMXX_L_Hand") && r == -1);
    CHECK(d.IkHandle(JIdx("AMXX_L_Forearm"), r, u, f, e) == IkJoint::Mid && f == JIdx("AMXX_L_Forearm"));
    CHECK(d.IkHandle(JIdx("AMXX_L_UpperArm"), r, u, f, e) == IkJoint::Root && r == JIdx("AMXX_L_Clavicle") && u == JIdx("AMXX_L_UpperArm"));
    CHECK(d.IkHandle(JIdx("AMXX_R_Clavicle"), r, u, f, e) == IkJoint::Root && r == JIdx("AMXX_R_Clavicle") && e == JIdx("AMXX_R_Hand"));
    // legs: locked outside a Kickflip clip
    CHECK(d.IkHandle(JIdx("AMXX_L_Calf"), r, u, f, e) == IkJoint::None);
    CHECK(d.IkHandle(JIdx("AMXX_L_Foot"), r, u, f, e) == IkJoint::None);
    CHECK(d.IkHandle(JIdx("AMXX_Head"), r, u, f, e) == IkJoint::None);
    EditorDoc k = JDoc(FIG_Kickflip, "Kickflip", true);
    CHECK(k.IkHandle(JIdx("AMXX_L_Foot"), r, u, f, e) == IkJoint::End && e == JIdx("AMXX_L_Foot"));
    CHECK(k.IkHandle(JIdx("AMXX_L_Calf"), r, u, f, e) == IkJoint::Mid && u == JIdx("AMXX_L_Thigh"));
    CHECK(k.IkHandle(JIdx("AMXX_L_Thigh"), r, u, f, e) == IkJoint::None);   // hip = pelvis, later
    // IkChain keeps its contract
    CHECK(!d.IkChain(JIdx("AMXX_R_Clavicle"), u, f, e));
}

TEST(p5c_elbow_swivel_keeps_hand) {
    EditorDoc d = JDoc(FIG_NoseManual, "NoseManual");
    const int up = JIdx("AMXX_L_UpperArm"), fo = JIdx("AMXX_L_Forearm"), ha = JIdx("AMXX_L_Hand");
    const V s0 = P(d, up, kT), e0 = P(d, fo, kT), h0 = P(d, ha, kT);
    const V target = Add(e0, V{6.f, -8.f, 9.f});
    const float before = Dist(e0, target);
    const float tg[3] = {target.x, target.y, target.z};
    CHECK(d.IkMoveJoint(fo, tg, true, true, kT));
    d.IkEnd();
    CHECK(Dist(P(d, ha, kT), h0) < 0.05f);
    CHECK(Dist(P(d, up, kT), s0) < 1e-3f);
    CHECK(Dist(P(d, fo, kT), target) < before - 0.5f);
}

TEST(p5c_elbow_free_moves_hand) {
    EditorDoc d = JDoc(FIG_NoseManual, "NoseManual");
    const int up = JIdx("AMXX_L_UpperArm"), fo = JIdx("AMXX_L_Forearm"), ha = JIdx("AMXX_L_Hand");
    const V s0 = P(d, up, kT), e0 = P(d, fo, kT), h0 = P(d, ha, kT);
    const Quat foreLocal = d.EditedRaw(fo, kT);
    const float a = Dist(s0, e0);
    const V target = Add(e0, V{10.f, 5.f, -7.f});
    const float tg[3] = {target.x, target.y, target.z};
    CHECK(d.IkMoveJoint(fo, tg, false, false, kT));
    d.IkEnd();
    const V e1 = P(d, fo, kT);
    const V want = Add(s0, Mul(Sub(target, s0), a / Dist(target, s0)));
    CHECK(Dist(e1, want) < 0.1f);
    const Quat f1 = d.EditedRaw(fo, kT);
    CHECK(std::fabs(f1.x - foreLocal.x) + std::fabs(f1.y - foreLocal.y) + std::fabs(f1.z - foreLocal.z) + std::fabs(f1.w - foreLocal.w) < 1e-4f);
    CHECK(Dist(P(d, ha, kT), h0) > 1.f);   // the hand follows
}

TEST(p5c_shoulder_keeps_hand) {
    EditorDoc d = JDoc(FIG_NoseManual, "NoseManual");
    const int cl = JIdx("AMXX_L_Clavicle"), up = JIdx("AMXX_L_UpperArm"), fo = JIdx("AMXX_L_Forearm"), ha = JIdx("AMXX_L_Hand");
    // arm bent first (hand at 65 % of the reach): the shoulder can move without putting the hand out of reach
    const V s0 = P(d, up, kT);
    const float reach = Dist(s0, P(d, fo, kT)) + Dist(P(d, fo, kT), P(d, ha, kT));
    const V hand = Add(s0, Mul(V{0.3f, 0.5f, -0.81f}, reach * 0.65f));
    const float ht[3] = {hand.x, hand.y, hand.z};
    CHECK(d.IkMoveHand(ha, ht, 0.f, true, kT));
    d.IkEnd();
    d.CommitKey(kT);
    const V h0 = P(d, ha, kT);
    const V target = Add(s0, V{0.f, 0.f, 6.f});
    const float tg[3] = {target.x, target.y, target.z};
    CHECK(d.IkMoveJoint(cl, tg, true, true, kT));
    d.IkEnd();
    CHECK(Dist(P(d, up, kT), target) < Dist(s0, target) - 1.f);
    CHECK(Dist(P(d, ha, kT), h0) < 0.05f);
    CHECK(d.Pending(cl, nullptr) && d.Pending(up, nullptr) && d.Pending(fo, nullptr));
}

TEST(p5c_shoulder_hand_out_of_reach_no_nan) {
    EditorDoc d = JDoc(FIG_NoseManual, "NoseManual");
    const int cl = JIdx("AMXX_L_Clavicle"), up = JIdx("AMXX_L_UpperArm"), fo = JIdx("AMXX_L_Forearm"), ha = JIdx("AMXX_L_Hand");
    // arm almost straight first (hand at 98 % of the reach), then shoulder pulled away from the hand
    const V s0 = P(d, up, kT);
    const float reach = Dist(s0, P(d, fo, kT)) + Dist(P(d, fo, kT), P(d, ha, kT));
    const V dir = V{0.f, 0.6f, -0.8f};
    const V hand = Add(s0, Mul(dir, reach * 0.98f));
    const float ht[3] = {hand.x, hand.y, hand.z};
    CHECK(d.IkMoveHand(ha, ht, 0.f, true, kT));
    d.IkEnd();
    d.CommitKey(kT);
    const V h0 = P(d, ha, kT);
    const V target = Add(P(d, up, kT), Mul(dir, -20.f));
    const float tg[3] = {target.x, target.y, target.z};
    CHECK(d.IkMoveJoint(cl, tg, true, true, kT));
    d.IkEnd();
    for (int b : {cl, up, fo, ha}) { Quat q; if (d.Pending(b, &q)) CHECK(Finite(q)); }
    const V s1 = P(d, up, kT), h1 = P(d, ha, kT);
    CHECK(Dist(s1, h0) > reach);                 // the hand is indeed out of reach
    CHECK(Ang(Sub(h1, s1), Sub(h0, s1)) < 1.f);  // the arm is stretched towards the hand
}

TEST(p5c_knee_swivel_in_kickflip_keeps_foot) {
    EditorDoc d = JDoc(FIG_Kickflip, "Kickflip", true);
    const int th = JIdx("AMXX_L_Thigh"), ca = JIdx("AMXX_L_Calf"), fo = JIdx("AMXX_L_Foot");
    const V hip0 = P(d, th, 0.2f), k0 = P(d, ca, 0.2f), f0 = P(d, fo, 0.2f);
    const V target = Add(k0, V{8.f, 6.f, 0.f});
    const float before = Dist(k0, target);
    const float tg[3] = {target.x, target.y, target.z};
    CHECK(d.IkMoveJoint(ca, tg, true, true, 0.2f));
    d.IkEnd();
    CHECK(Dist(P(d, fo, 0.2f), f0) < 0.05f);
    CHECK(Dist(P(d, th, 0.2f), hip0) < 1e-3f);
    CHECK(Dist(P(d, ca, 0.2f), target) < before - 0.5f);
}

TEST(p5c_degenerate_returns_false) {
    EditorDoc d = JDoc(FIG_NoseManual, "NoseManual");
    const int up = JIdx("AMXX_L_UpperArm"), fo = JIdx("AMXX_L_Forearm"), ha = JIdx("AMXX_L_Hand");
    const V mid = Mul(Add(P(d, up, kT), P(d, ha, kT)), 0.5f);   // on the shoulder -> hand axis
    const float tg[3] = {mid.x, mid.y, mid.z};
    CHECK(!d.IkMoveJoint(fo, tg, true, true, kT));
    d.IkEnd();
    CHECK(!d.HasPending());
}

TEST(p5c_joint_gesture_path_independent) {
    const int fo = JIdx("AMXX_L_Forearm"), up = JIdx("AMXX_L_UpperArm"), ha = JIdx("AMXX_L_Hand");
    EditorDoc a = JDoc(FIG_NoseManual, "NoseManual"), b = JDoc(FIG_NoseManual, "NoseManual");
    const V e0 = P(a, fo, kT);
    const V t1 = Add(e0, V{-9.f, 4.f, 3.f}), t2 = Add(e0, V{5.f, -6.f, 8.f});
    const float g1[3] = {t1.x, t1.y, t1.z}, g2[3] = {t2.x, t2.y, t2.z};
    a.IkBegin(fo, kT);
    CHECK(a.IkMoveJoint(fo, g1, true, true, kT));
    CHECK(a.IkMoveJoint(fo, g2, true, true, kT));
    a.IkEnd();
    b.IkBegin(fo, kT);
    CHECK(b.IkMoveJoint(fo, g2, true, true, kT));
    b.IkEnd();
    for (int bone : {up, fo, ha}) {
        Quat qa = a.EditedRaw(bone, kT), qb = b.EditedRaw(bone, kT);
        CHECK(std::fabs(qa.x - qb.x) + std::fabs(qa.y - qb.y) + std::fabs(qa.z - qb.z) + std::fabs(qa.w - qb.w) < 1e-4f);
    }
}
