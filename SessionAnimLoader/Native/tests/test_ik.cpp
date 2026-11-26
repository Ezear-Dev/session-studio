// Hand IK (two bones) and per-bone timeline rows
#include "check.h"
#include "../../../SessionStudio/model/ik.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../src/figure.h"

namespace {
float AngDeg(Quat a, Quat b) {
    const Quat d = QMul(QInv(QNorm(a)), QNorm(b));
    double s = std::sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    if (s > 1.0) s = 1.0;
    double ang = 2.0 * std::asin(s) * 57.29577951308232;
    if (d.w < 0) ang = 360.0 - ang;
    if (ang > 180.0) ang = 360.0 - ang;
    return (float)ang;
}
float Dist(const FTr& a, const float p[3]) { const float x = a.px - p[0], y = a.py - p[1], z = a.pz - p[2]; return std::sqrt(x * x + y * y + z * z); }
float Dist(const FTr& a, const FTr& b) { const float p[3] = {b.px, b.py, b.pz}; return Dist(a, p); }
int Idx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }

EditorDoc Ready() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    FigureRec r; r.figure = "Manual"; r.bones = 70;
    for (int i = 0; i < 31; i++) {
        r.times.push_back(i / 30.f); r.ratios.push_back(i / 30.f);
        for (int b = 0; b < 70; b++) r.frames.push_back(QFromRotatorDeg((float)(b % 5) * 4.f, (float)(b % 7) * 3.f, (float)(b % 3) * 5.f));
    }
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkeletonToJson(s), err));
    CHECK(d.LoadFigure(FigureToJson(r), err));
    d.NewClip(FIG_Manual);
    return d;
}
// reachable target: 30 % of the way hand -> shoulder, then 6 cm lower
void Reachable(const std::vector<FTr>& cs, int hand, int up, float out[3]) {
    out[0] = cs[hand].px + 0.3f * (cs[up].px - cs[hand].px);
    out[1] = cs[hand].py + 0.3f * (cs[up].py - cs[hand].py);
    out[2] = cs[hand].pz + 0.3f * (cs[up].pz - cs[hand].pz) - 6.f;
}
} // namespace

TEST(ik_shortest_arc_maps_a_to_b) {
    const float a[3] = {1, 0, 0}, b[3] = {0, 0.6f, 0.8f};
    const Quat q = ShortestArc(a, b);
    float x = 1, y = 0, z = 0;
    QRotVec(q, x, y, z);
    CHECK_NEAR(x, 0, 1e-5); CHECK_NEAR(y, 0.6, 1e-5); CHECK_NEAR(z, 0.8, 1e-5);
    const float c[3] = {-1, 0, 0};                       // opposite: 180 degrees, without NaN
    const Quat r = ShortestArc(a, c);
    float u = 1, v = 0, w = 0;
    QRotVec(r, u, v, w);
    CHECK_NEAR(u, -1, 1e-4);
}

TEST(ik_hand_reaches_reachable_target) {
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_R_Hand"), up = Idx("AMXX_R_UpperArm"), fo = Idx("AMXX_R_Forearm");
    std::vector<FTr> before;
    d.ComponentPose(0.f, before, false);
    float target[3];
    Reachable(before, hand, up, target);
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHand(hand, target, 0.f, true, 0.f));
    d.IkEnd();
    std::vector<FTr> after;
    d.ComponentPose(0.f, after, false);
    CHECK(Dist(after[hand], target) < 0.05f);
    CHECK(AngDeg(TrQ(after[hand]), TrQ(before[hand])) < 0.05f);   // the hand keeps its orientation
    CHECK_NEAR(Dist(after[up], after[fo]), Dist(before[up], before[fo]), 1e-3);   // lengths kept
    CHECK_NEAR(Dist(after[fo], after[hand]), Dist(before[fo], before[hand]), 1e-3);
    for (const char* n : {"AMXX_R_Clavicle", "AMXX_Spine3", "AMXX_L_Hand", "AMXX_Head"}) {   // outside the chain: unchanged
        const int b = Idx(n);
        CHECK(Dist(after[b], before[b]) < 1e-4f);
    }
}

TEST(ik_hand_follows_forearm_when_not_kept) {
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_R_Hand"), up = Idx("AMXX_R_UpperArm");
    std::vector<FTr> before;
    d.ComponentPose(0.f, before, false);
    float target[3];
    Reachable(before, hand, up, target);
    CHECK(d.IkMoveHand(hand, target, 0.f, false, 0.f));   // without IkBegin: the current pose is the base
    d.IkEnd();
    CHECK(!d.Pending(hand, nullptr));                        // the local rotation of the hand does not change
    std::vector<FTr> after;
    d.ComponentPose(0.f, after, false);
    CHECK(Dist(after[hand], target) < 0.05f);
}

TEST(ik_unreachable_target_stretches_without_nan) {
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_L_Hand"), up = Idx("AMXX_L_UpperArm"), fo = Idx("AMXX_L_Forearm");
    std::vector<FTr> before;
    d.ComponentPose(0.f, before, false);
    const float target[3] = {before[up].px + 500.f, before[up].py, before[up].pz};
    CHECK(d.IkMoveHand(hand, target, 0.f, true, 0.f));
    d.IkEnd();
    std::vector<FTr> after;
    d.ComponentPose(0.f, after, false);
    for (auto& t : after) CHECK(std::isfinite(t.px) && std::isfinite(t.qw));
    const float reach = Dist(before[up], before[fo]) + Dist(before[fo], before[hand]);
    CHECK(Dist(after[up], after[hand]) > reach * 0.99f);
    CHECK(after[hand].px > after[up].px + reach * 0.98f);
}

TEST(ik_elbow_delta_rotates_elbow_not_hand) {
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_R_Hand"), up = Idx("AMXX_R_UpperArm"), fo = Idx("AMXX_R_Forearm");
    std::vector<FTr> base;
    d.ComponentPose(0.f, base, false);
    float target[3];
    Reachable(base, hand, up, target);
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHand(hand, target, 0.f, true, 0.f));
    std::vector<FTr> ref;
    d.ComponentPose(0.f, ref, false);
    for (float a : {-120.f, -30.f, 45.f, 150.f}) {
        CHECK(d.IkMoveHand(hand, target, a, true, 0.f));   // same gesture: the base does not change
        std::vector<FTr> cs;
        d.ComponentPose(0.f, cs, false);
        CHECK(Dist(cs[hand], target) < 0.05f);             // the hand does not move
        // the elbow turned by |a| around the shoulder -> hand axis
        float ax[3] = {target[0] - cs[up].px, target[1] - cs[up].py, target[2] - cs[up].pz};
        const float n = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        for (float& v : ax) v /= n;
        auto perp = [&](const FTr& e, float o[3]) {
            float v[3] = {e.px - cs[up].px, e.py - cs[up].py, e.pz - cs[up].pz};
            const float k = v[0] * ax[0] + v[1] * ax[1] + v[2] * ax[2];
            for (int i = 0; i < 3; i++) o[i] = v[i] - k * ax[i];
        };
        float p0[3], p1[3];
        perp(ref[fo], p0); perp(cs[fo], p1);
        const float c = (p0[0] * p1[0] + p0[1] * p1[1] + p0[2] * p1[2]) /
                        (std::sqrt(p0[0] * p0[0] + p0[1] * p0[1] + p0[2] * p0[2]) * std::sqrt(p1[0] * p1[0] + p1[1] * p1[1] + p1[2] * p1[2]));
        CHECK_NEAR(std::acos(std::fmax(-1.f, std::fmin(1.f, c))) * 57.29578f, std::fabs(a), 0.5);
    }
    d.IkEnd();
}

TEST(ik_closed_loop_drag_returns_to_start) {   // a mouse loop brings back exactly the start pose
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_R_Hand"), up = Idx("AMXX_R_UpperArm"), fo = Idx("AMXX_R_Forearm");
    std::vector<FTr> base;
    d.ComponentPose(0.f, base, false);
    float c[3];
    Reachable(base, hand, up, c);
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHand(hand, c, 0.f, true, 0.f));
    std::vector<FTr> start;
    d.ComponentPose(0.f, start, false);
    for (int i = 1; i <= 108; i++) {   // 3 turns of 12 cm diameter
        const float a = i * 2.f * 3.14159265f / 36.f;
        const float p[3] = {c[0] + 6.f * std::cos(a) - 6.f, c[1] + 6.f * std::sin(a), c[2] + 3.f * std::sin(a)};
        CHECK(d.IkMoveHand(hand, p, 0.f, true, 0.f));
    }
    CHECK(d.IkMoveHand(hand, c, 0.f, true, 0.f));
    std::vector<FTr> end;
    d.ComponentPose(0.f, end, false);
    CHECK(AngDeg(TrQ(end[up]), TrQ(start[up])) < 0.05f);
    CHECK(AngDeg(TrQ(end[fo]), TrQ(start[fo])) < 0.05f);
    d.IkEnd();
}

TEST(ik_forearm_moves_as_a_pure_hinge) {   // relecture I3
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_R_Hand"), up = Idx("AMXX_R_UpperArm"), fo = Idx("AMXX_R_Forearm");
    std::vector<FTr> base;
    d.ComponentPose(0.f, base, false);
    // start hinge, in the arm local frame
    float uf[3] = {base[fo].px - base[up].px, base[fo].py - base[up].py, base[fo].pz - base[up].pz};
    float fh[3] = {base[hand].px - base[fo].px, base[hand].py - base[fo].py, base[hand].pz - base[fo].pz};
    float n[3] = {uf[1] * fh[2] - uf[2] * fh[1], uf[2] * fh[0] - uf[0] * fh[2], uf[0] * fh[1] - uf[1] * fh[0]};
    const float nn = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    CHECK(nn > 1e-3f);
    for (float& v : n) v /= nn;
    QRotVec(QInv(TrQ(base[up])), n[0], n[1], n[2]);
    const Quat oldLocal = QMul(QInv(TrQ(base[up])), TrQ(base[fo]));
    float target[3];
    Reachable(base, hand, up, target);
    target[0] += 4.f; target[1] += 5.f;
    CHECK(d.IkMoveHand(hand, target, 25.f, true, 0.f));
    d.IkEnd();
    Quat newLocal;
    CHECK(d.Pending(fo, &newLocal));
    Quat delta = QNorm(QMul(newLocal, QInv(oldLocal)));   // forearm rotation in the arm frame
    if (delta.w < 0) delta = Quat{-delta.x, -delta.y, -delta.z, -delta.w};
    const float s = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    CHECK(s > 1e-3f);   // it did bend
    if (s > 1e-3f) CHECK(std::fabs(delta.x / s * n[0] + delta.y / s * n[1] + delta.z / s * n[2]) > 0.9999f);   // around the hinge
}

TEST(ik_hand_passing_under_the_shoulder_does_not_flip_the_elbow) {   // relecture I1
    EditorDoc d = Ready();
    const int hand = Idx("AMXX_R_Hand"), up = Idx("AMXX_R_UpperArm"), fo = Idx("AMXX_R_Forearm");
    std::vector<FTr> cs;
    d.ComponentPose(0.f, cs, false);
    const float hang[3] = {cs[up].px - 2.5f, cs[up].py, cs[up].pz - 40.f};
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHand(hand, hang, 0.f, true, 0.f));   // arm hanging, right next to the vertical
    d.IkEnd();
    d.IkBegin(hand, 0.f);
    std::vector<FTr> prev;
    d.ComponentPose(0.f, prev, false);
    float worst = 0.f;
    for (int i = 0; i <= 50; i++) {   // the hand passes under the shoulder, from -2.5 to +2.5 cm
        const float p[3] = {cs[up].px - 2.5f + 0.1f * i, cs[up].py, cs[up].pz - 40.f};
        CHECK(d.IkMoveHand(hand, p, 0.f, true, 0.f));
        std::vector<FTr> now;
        d.ComponentPose(0.f, now, false);
        worst = std::fmax(worst, Dist(now[fo], prev[fo]));
        prev = now;
    }
    d.IkEnd();
    CHECK(worst < 1.f);   // the elbow slides, it never jumps to the other side
}

TEST(ik_chain_from_any_arm_bone) {
    EditorDoc d = Ready();
    int u, f, h;
    CHECK(d.IkChain(Idx("AMXX_R_Finger21"), u, f, h) && h == Idx("AMXX_R_Hand") && u == Idx("AMXX_R_UpperArm"));
    CHECK(d.IkChain(Idx("AMXX_L_Forearm"), u, f, h) && h == Idx("AMXX_L_Hand"));
    CHECK(!d.IkChain(Idx("AMXX_Head"), u, f, h));
    CHECK(!d.IkChain(Idx("AMXX_R_Clavicle"), u, f, h));
}

// ------------------------------------------------------------------ per-bone timeline rows
TEST(rows_group_hands_and_order) {
    EditorDoc d = Ready();
    for (const char* n : {"AMXX_R_UpperArm", "AMXX_R_Finger21", "AMXX_R_Finger0", "AMXX_Spine3", "AMXX_L_Hand"}) d.SetPending(Idx(n), QFromRotatorDeg(0, 5, 0));
    d.CommitKey(0.5f);
    const auto rows = d.KeyRows(false);
    std::vector<std::string> labels;
    for (auto& r : rows) labels.push_back(r.label);
    CHECK(labels.size() == 4);   // Spine3, L Hand, R_UpperArm, R Hand (in skeleton order)
    if (labels.size() == 4) { CHECK(labels[0] == "Spine3"); CHECK(labels[1] == "L Hand"); CHECK(labels[2] == "R_UpperArm"); CHECK(labels[3] == "R Hand"); }
    for (auto& r : rows) if (r.label == "R Hand") { CHECK(r.primary == Idx("AMXX_R_Hand")); CHECK(r.bones.size() > 10); }
    const auto all = d.KeyRows(true);
    CHECK(all.size() > rows.size());
}

TEST(rows_move_and_delete_are_limited_to_the_row) {
    EditorDoc d = Ready();
    const int arm = Idx("AMXX_R_UpperArm"), head = Idx("AMXX_Head"), f1 = Idx("AMXX_R_Finger1"), f2 = Idx("AMXX_R_Finger2");
    for (int b : {arm, head, f1, f2}) d.SetPending(b, QFromRotatorDeg(0, 5, 0));
    d.CommitKey(0.5f);
    d.SetPending(head, QFromRotatorDeg(0, 9, 0));
    d.CommitKey(1.0f);
    CHECK(d.MoveKeysBones({arm}, 0.5f, 0.8f));
    CHECK(d.KeyTimes(arm).size() == 1 && std::fabs(d.KeyTimes(arm)[0] - 0.8f) < 1e-4f);
    CHECK(std::fabs(d.KeyTimes(head)[0] - 0.5f) < 1e-4f);         // the head did not move
    CHECK(!d.MoveKeysBones({head}, 0.5f, 1.0f));                  // collision on the head row
    CHECK(d.MoveKeysBones({f1, f2}, 0.5f, 0.6f));                 // "R Hand" row: its fingers together
    CHECK(std::fabs(d.KeyTimes(f2)[0] - 0.6f) < 1e-4f);
    d.DeleteKeysBones({head}, 0.5f);
    CHECK(d.KeyTimes(head).size() == 1);
    CHECK(d.KeyTimesBones({arm, head}).size() == 2);
}

TEST(rows_never_touch_locked_tracks) {   // relecture m2
    EditorDoc d = Ready();
    std::string err;
    CHECK(d.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":2},\"tracks\":{\"AMXX_L_Thigh\":[{\"t\":0.5,\"q\":[0,0,0.1,1]}],\"AMXX_Head\":[{\"t\":0.5,\"q\":[0,0,0,1]}]}}", "z.sanim", err));
    const int thigh = Idx("AMXX_L_Thigh");
    CHECK(!d.MoveKeysBones({thigh}, 0.5f, 1.0f));
    d.DeleteKeysBones({thigh}, 0.5f);
    CHECK(d.KeyTimes(thigh).size() == 1 && std::fabs(d.KeyTimes(thigh)[0] - 0.5f) < 1e-4f);
}
