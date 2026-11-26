// P6 lot 2 (ideas from Skate Style's studio): pelvis offset (feet kept), the body follows the hand
#include "check.h"
#include "../src/clip.h"
#include "../src/pelvis.h"
#include "../src/player.h"
#include "../src/skeleton.h"
#include "../src/figure.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
int RIdx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }
float D(const FTr& a, const FTr& b) { return std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) + (a.pz - b.pz) * (a.pz - b.pz)); }
// reference skeleton, both knees bent 40 degrees (so that the legs can fold)
std::vector<FTr> BentPose() {
    const auto par = RefParents();
    const auto pos = RefPositions();
    const int n = (int)par.size();
    std::vector<Quat> lq(n, QIdent);
    lq[RIdx("AMXX_L_Calf")] = QFromRotatorDeg(0, 40, 0);
    lq[RIdx("AMXX_R_Calf")] = QFromRotatorDeg(0, 40, 0);
    std::vector<bool> dirty(n, true);
    std::unique_ptr<bool[]> dd(new bool[n]);
    for (int i = 0; i < n; i++) dd[i] = true;
    std::vector<FTr> cs(n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    ToComponent(cs.data(), par.data(), n, lq.data(), (const float (*)[3])pos.data(), dd.get());
    return cs;
}
std::string SkelJson2() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
std::string FigJson2() {
    FigureRec r; r.figure = "NoseManual"; r.bones = 70;
    for (int i = 0; i < 31; i++) {
        r.times.push_back(i / 30.f);
        r.ratios.push_back(i / 30.f);
        for (int b = 0; b < 70; b++) r.frames.push_back(QIdent);
    }
    return FigureToJson(r);
}
} // namespace

TEST(lot2_clip_pelvis_roundtrip) {
    Clip c;
    c.name = "p"; c.states = ST_Manual; c.duration = 1.f;
    c.pelvis.push_back(PosKey{0.f, {0, 0, 0}, true});
    c.pelvis.push_back(PosKey{0.5f, {1.5f, -2.f, -10.f}, false});
    Clip back;
    std::string err;
    CHECK(ParseClip(ClipToJson(c), "p", back, err));   // a clip with only pelvis keys is valid
    CHECK(back.tracks.empty() && back.pelvis.size() == 2);
    if (back.pelvis.size() == 2) {
        CHECK_NEAR(back.pelvis[1].v[2], -10.0, 1e-5);
        CHECK_NEAR(back.pelvis[1].v[0], 1.5, 1e-5);
        CHECK(!back.pelvis[1].smooth && back.pelvis[0].smooth);
    }
    CHECK(ClipToJson(back) == ClipToJson(c));
    const std::string bad = "{\"format\":\"sanim\",\"version\":1,\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1},"
                            "\"pelvis\":[{\"t\":0,\"offset\":[1,2]}]}";
    CHECK(!ParseClip(bad, "b", back, err) && err.find("pelvis") != std::string::npos);
    const std::string none = "{\"format\":\"sanim\",\"version\":1,\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1}}";
    CHECK(!ParseClip(none, "n", back, err));   // still at least one track or one pelvis key
}

TEST(lot2_eval_pelvis) {
    std::vector<PosKey> k{{0.f, {0, 0, 0}, false}, {1.f, {0, 0, -10}, false}};
    float v[3];
    EvalPelvis(k, 0.5f, 1.f, false, v);
    CHECK_NEAR(v[2], -5.0, 1e-4);
    EvalPelvis(k, 2.f, 1.f, false, v);
    CHECK_NEAR(v[2], -10.0, 1e-4);
    EvalPelvis({}, 0.3f, 1.f, false, v);
    CHECK(v[0] == 0.f && v[1] == 0.f && v[2] == 0.f);
    // smooth: no stop at the middle key
    std::vector<PosKey> s{{0.f, {0, 0, 0}, true}, {0.5f, {0, 0, -5}, true}, {1.f, {0, 0, -10}, true}};
    float a[3], b[3];
    EvalPelvis(s, 0.49f, 1.f, false, a);
    EvalPelvis(s, 0.51f, 1.f, false, b);
    CHECK((a[2] - b[2]) / 0.02f > 6.f);
}

TEST(lot2_pelvis_offset_keeps_feet) {
    const auto par = RefParents();
    const int n = (int)par.size();
    const PelvisRig rig = BuildPelvisRig(RefNames(), par.data(), n);
    CHECK(rig.Ok());
    const std::vector<FTr> base = BentPose();
    std::vector<FTr> cs = base;
    const float off[3] = {3.f, 2.f, -10.f};
    CHECK(ApplyPelvisOffset(cs.data(), n, rig, off));
    const int pel = rig.pelvis, head = RIdx("AMXX_Head"), toe = RIdx("AMXX_L_Toe0");
    CHECK_NEAR(cs[pel].pz, base[pel].pz - 10.f, 1e-3);
    CHECK_NEAR(cs[head].px, base[head].px + 3.f, 1e-3);   // the upper body moves with the pelvis
    for (int s = 0; s < 2; s++) {
        const int t = rig.thigh[s], c = rig.calf[s], f = rig.foot[s];
        CHECK_NEAR(D(cs[f], base[f]), 0.0, 0.01);                       // the foot stays
        CHECK(std::fabs(cs[f].qx - base[f].qx) + std::fabs(cs[f].qw - base[f].qw) < 1e-5f);   // and keeps its orientation
        CHECK_NEAR(D(cs[t], cs[c]), D(base[t], base[c]), 0.01);        // bone lengths kept
        CHECK_NEAR(D(cs[c], cs[f]), D(base[c], base[f]), 0.01);
        // the calf points at the foot (its child is where the bone says)
        const FTr& C = cs[c];
        const auto pos = RefPositions();
        float v[3] = {pos[f * 3], pos[f * 3 + 1], pos[f * 3 + 2]};
        QRotVec(TrQ(C), v[0], v[1], v[2]);
        CHECK_NEAR(C.px + v[0], cs[f].px, 0.02);
        CHECK_NEAR(C.pz + v[2], cs[f].pz, 0.02);
    }
    CHECK_NEAR(D(cs[toe], base[toe]), 0.0, 0.01);   // toes stay with the foot
    float axis[3];
    CHECK(HipAxis(cs.data(), rig, axis));
    float m[3] = {axis[0] * 4.f, axis[1] * 4.f, axis[2] * 4.f};
    MirrorOffset(axis, m);
    CHECK_NEAR(m[0], -axis[0] * 4.f, 1e-4);
}

TEST(lot2_player_pelvis_offset) {
    const auto par = RefParents();
    const auto names = RefNames();
    const int n = (int)par.size();
    auto pl = std::make_unique<Player>();
    pl->SetRig(par.data(), n, RefMirror());
    pl->SetRefPositions(RefPositions());
    std::vector<bool> isLeg(n);
    for (int i = 0; i < n; i++) isLeg[i] = IsLegBone(names[i]);
    pl->SetLegBones(isLeg);
    pl->SetPelvisRig(BuildPelvisRig(names, par.data(), n));
    ClipSet set;
    Clip c;
    c.file = "pel"; c.name = "pel"; c.states = ST_Manual; c.duration = 1.f; c.loop = true; c.blendIn = 0.1f; c.mirror = false;
    c.pelvis.push_back(PosKey{0.f, {0, 0, -10}, true});
    set.clips.push_back(c);
    set.generation = 1;
    GameState gs{};
    gs.manual = true;
    const std::vector<FTr> base = BentPose();
    std::vector<FTr> cs;
    for (int i = 0; i < 60; i++) { cs = base; pl->Apply(cs.data(), n, &set, gs, 1.f / 60.f); }
    const int pel = RIdx("AMXX_Pelvis"), lf = RIdx("AMXX_L_Foot"), rf = RIdx("AMXX_R_Foot");
    CHECK_NEAR(cs[pel].pz, base[pel].pz - 10.f, 0.01);
    CHECK_NEAR(D(cs[lf], base[lf]), 0.0, 0.02);
    CHECK_NEAR(D(cs[rf], base[rf]), 0.0, 0.02);
    // no manual any more: the offset fades out
    gs.manual = false;
    for (int i = 0; i < 60; i++) { cs = base; pl->Apply(cs.data(), n, &set, gs, 1.f / 60.f); }
    CHECK_NEAR(cs[pel].pz, base[pel].pz, 0.01);
}

TEST(lot2_editor_pelvis_key) {
    EditorDoc d;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    std::string err;
    CHECK(d.LoadSkeleton(SkelJson2(), err));
    CHECK(d.LoadFigure(FigJson2(), err));
    d.NewClip(FIG_NoseManual);
    const int pel = d.PelvisBone();
    CHECK(pel == RIdx("AMXX_Pelvis"));
    std::vector<FTr> ghost, cs;
    d.ComponentPose(0.5f, ghost, true);
    d.Checkpoint("pelvis");
    const float v[3] = {0, 0, -8};
    CHECK(d.SetPendingPelvis(v) && d.HasPending());
    d.CommitKey(0.5f);
    CHECK(!d.HasPending() && d.clip().pelvis.size() == 1);
    d.ComponentPose(0.5f, cs, false);
    // 2026-11-25: this synthetic figure has straight horizontal legs, a pelvis move down would stretch them: it stays
    CHECK_NEAR(cs[pel].pz, ghost[pel].pz, 0.01);
    const int lf = RIdx("AMXX_L_Foot");
    CHECK_NEAR(D(cs[lf], ghost[lf]), 0.0, 0.5);   // straight legs in this figure: out of reach by a few mm
    CHECK(d.KeyTimes(pel).size() == 1 && d.KeyTimes(-1).size() == 1);
    bool row = false;
    for (auto& r : d.KeyRows(false)) if (r.primary == pel) row = true;
    CHECK(row);
    std::string text = d.SaveText(err);
    CHECK(!text.empty());   // a clip with only a pelvis key can be saved
    CHECK(d.MoveKeysBones({pel}, 0.5f, 0.7f) && d.clip().pelvis[0].t > 0.69f);
    CHECK(d.Undo() && d.clip().pelvis.empty());   // undo goes back before the key
    CHECK(d.Redo() && d.clip().pelvis.size() == 1);
    d.DeleteKeysAt(0.7f, pel);
    CHECK(d.clip().pelvis.empty());
}

TEST(lot2_body_follows_hand) {
    EditorDoc d;
    std::string err;
    CHECK(d.LoadSkeleton(SkelJson2(), err));
    CHECK(d.LoadFigure(FigJson2(), err));
    d.NewClip(FIG_NoseManual);
    const int hand = RIdx("AMXX_L_Hand"), up = RIdx("AMXX_L_UpperArm"), fore = RIdx("AMXX_L_Forearm"), clav = RIdx("AMXX_L_Clavicle");
    const int sp3 = RIdx("AMXX_Spine3");
    std::vector<FTr> cs;
    d.ComponentPose(0.f, cs, false, true);
    const float L = D(cs[up], cs[fore]) + D(cs[fore], cs[hand]);
    // a target in reach: the body does not move
    float dir[3] = {cs[hand].px - cs[up].px, cs[hand].py - cs[up].py, cs[hand].pz - cs[up].pz};
    const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (float& x : dir) x /= dl;
    const float near[3] = {cs[up].px + dir[0] * L * 0.6f, cs[up].py + dir[1] * L * 0.6f, cs[up].pz + (dir[2] * L * 0.6f) + 5.f};
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHandBody(hand, near, true, 0.f));
    CHECK(!d.Pending(clav, nullptr) && !d.Pending(sp3, nullptr));
    // far: the clavicle and the spine turn, the hand gets closer than with the arm alone
    // forward reach (across the arm, like reaching for the board): the spine can bring the shoulder closer
    float side[3] = {dir[1], -dir[0], 0.f};
    const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1]);
    if (sl > 1e-3f) { side[0] /= sl; side[1] /= sl; } else { side[0] = 1.f; side[1] = 0.f; }
    const float far[3] = {cs[up].px + side[0] * L * 1.25f, cs[up].py + side[1] * L * 1.25f, cs[up].pz - 10.f};
    CHECK(d.IkMoveHandBody(hand, far, true, 0.f));
    CHECK(d.Pending(clav, nullptr) && d.Pending(sp3, nullptr));
    d.ComponentPose(0.f, cs, false, true);
    const float withBody = std::sqrt((cs[hand].px - far[0]) * (cs[hand].px - far[0]) + (cs[hand].py - far[1]) * (cs[hand].py - far[1]) + (cs[hand].pz - far[2]) * (cs[hand].pz - far[2]));
    d.IkEnd();
    d.DiscardPending();
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHand(hand, far, 0.f, true, 0.f));
    d.ComponentPose(0.f, cs, false, true);
    const float armOnly = std::sqrt((cs[hand].px - far[0]) * (cs[hand].px - far[0]) + (cs[hand].py - far[1]) * (cs[hand].py - far[1]) + (cs[hand].pz - far[2]) * (cs[hand].pz - far[2]));
    CHECK(withBody < armOnly - 3.f);
    // back in reach during the same gesture: the body is given back
    d.IkEnd();
    d.DiscardPending();
    d.IkBegin(hand, 0.f);
    CHECK(d.IkMoveHandBody(hand, far, true, 0.f));
    CHECK(d.IkMoveHandBody(hand, near, true, 0.f));
    CHECK(!d.Pending(clav, nullptr) && !d.Pending(sp3, nullptr));
}
