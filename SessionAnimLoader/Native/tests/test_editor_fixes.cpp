// Tests of the editing model fixes
#include "check.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../src/figure.h"
#include "../src/player.h"

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
int Idx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }
std::string Skel() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// 31 frames: R_UpperArm (37) turns from 0 to 30 degrees (Z); ratio = i/30
std::string Fig() {
    FigureRec r; r.figure = "Manual"; r.bones = 70;
    for (int i = 0; i < 31; i++) {
        r.times.push_back(i / 30.f); r.ratios.push_back(i / 30.f);
        for (int b = 0; b < 70; b++) r.frames.push_back(b == 37 ? QFromRotatorDeg(0, 30.f * i / 30.f, 0) : QIdent);
    }
    return FigureToJson(r);
}
EditorDoc Ready(Mode m) {
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(Skel(), err));
    CHECK(d.LoadFigure(Fig(), err));
    d.NewClip(FIG_Manual);
    d.SetMode(m);
    return d;
}
void SetWeight(EditorDoc& d, int bone, float w) {
    for (auto& t : d.clip().tracks) if (t.bone == RefNames()[bone]) t.weight = w;
}
} // namespace

TEST(fix_weight_preview_matches_game_formula) {   // I4
    for (Mode m : {Mode::Replace, Mode::Additive}) {
        EditorDoc d = Ready(m);
        d.SetPending(37, QFromRotatorDeg(10, 50, 0));
        d.CommitKey(0.5f);
        SetWeight(d, 37, 0.5f);
        const Quat o = d.Origin(37, 0.5f);
        const Quat k = d.clip().tracks[0].keys[0].q;
        const Quat game = (m == Mode::Replace) ? QSlerp(o, k, 0.5f) : QMul(o, QSlerp(QIdent, k, 0.5f));
        CHECK(AngDeg(d.Edited(37, 0.5f), game) < 0.02f);   // the preview applies the weight like the player
    }
}

TEST(fix_weighted_commit_reproduces_wanted) {   // with a weight < 1, the stored key gives the wanted pose
    for (Mode m : {Mode::Replace, Mode::Additive}) {
        EditorDoc d = Ready(m);
        d.SetPending(37, QFromRotatorDeg(0, 5, 0)); d.CommitKey(0.5f);
        SetWeight(d, 37, 0.5f);
        const Quat want = QFromRotatorDeg(12, 40, -8);
        d.SetPending(37, want);
        d.CommitKey(0.5f);
        CHECK(AngDeg(d.Edited(37, 0.5f), want) < 0.05f);
    }
}

TEST(fix_set_mode_ratio_clip_uses_each_key_origin) {   // I5
    EditorDoc d = Ready(Mode::Replace);
    d.clip().drive = Drive::ManualRatio;
    d.clip().r0 = 0.f; d.clip().r1 = 1.f; d.clip().duration = 1.f;
    const Quat w0 = QFromRotatorDeg(0, 60, 10), w1 = QFromRotatorDeg(20, -20, 0);
    d.SetSimulatedRatio(0.f); d.SetPending(37, w0); d.CommitKey(0.f);
    d.SetSimulatedRatio(1.f); d.SetPending(37, w1); d.CommitKey(1.f);
    d.SetSimulatedRatio(0.5f);   // the slider is on neither of the two keys
    d.SetMode(Mode::Additive);
    d.SetSimulatedRatio(0.f);
    CHECK(AngDeg(d.Edited(37, 0.f), w0) < 0.05f);
    d.SetSimulatedRatio(1.f);
    CHECK(AngDeg(d.Edited(37, 1.f), w1) < 0.05f);
}

TEST(fix_checkpoint_state_restores_value_before_edit) {   // m2
    EditorDoc d = Ready(Mode::Replace);
    d.SetPending(37, QFromRotatorDeg(0, 5, 0)); d.CommitKey(0.f);
    const EditorDoc::State before = d.SaveState();
    SetWeight(d, 37, 0.2f);                  // the slider has already changed the value
    d.CheckpointState(before, "weight");      // the interface pushes the state from BEFORE
    CHECK(d.Undo());
    CHECK_NEAR(d.clip().tracks[0].weight, 1.0, 1e-6);
}

TEST(fix_delete_all_keys_spares_locked_tracks) {   // m7
    EditorDoc d = Ready(Mode::Replace);
    std::string err;
    CHECK(d.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1},\"tracks\":{\"AMXX_L_Thigh\":[{\"t\":0,\"q\":[0,0,0.1,1]}],\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,1]}]}}", "z.sanim", err));
    d.DeleteKeysAt(0.f, -1);
    CHECK(d.clip().tracks.size() == 1 && d.clip().tracks[0].bone == "AMXX_L_Thigh");
}

TEST(fix_take_origin_pose_subtree_only) {   // m13
    EditorDoc d = Ready(Mode::Replace);
    const int arm = Idx("AMXX_R_UpperArm");
    d.TakeOriginPose(1.0f, arm);
    CHECK(d.Pending(arm, nullptr));
    CHECK(d.Pending(Idx("AMXX_R_Hand"), nullptr));
    CHECK(!d.Pending(Idx("AMXX_Head"), nullptr));
    CHECK(!d.Pending(Idx("AMXX_L_UpperArm"), nullptr));
    EditorDoc e = Ready(Mode::Replace);
    e.TakeOriginPose(1.0f, -1);
    CHECK(e.Pending(Idx("AMXX_Head"), nullptr));   // no bone picked: the whole upper body
}

TEST(fix_set_mode_keeps_pose_with_weight) {   // mode conversion
    EditorDoc d = Ready(Mode::Replace);
    d.SetPending(37, QFromRotatorDeg(5, 45, -10)); d.CommitKey(0.5f);
    SetWeight(d, 37, 0.4f);
    const Quat before = d.Edited(37, 0.5f);
    d.SetMode(Mode::Additive);
    CHECK(AngDeg(d.Edited(37, 0.5f), before) < 0.05f);
    d.SetMode(Mode::Replace);
    CHECK(AngDeg(d.Edited(37, 0.5f), before) < 0.05f);
}
