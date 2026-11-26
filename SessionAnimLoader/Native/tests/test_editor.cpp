// The editing model (EditorDoc)
#include "check.h"
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
int Idx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }

std::string SkelJson() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// synthetic move: 31 frames, R_UpperArm (37) turns from 0 to 30 degrees around Z, the rest stays identity
std::string FigJson(const char* fig = "NoseManual", int bones = 70, int frames = 31, bool flatRatio = false) {
    FigureRec r; r.figure = fig; r.bones = bones;
    for (int i = 0; i < frames; i++) {
        r.times.push_back(i / 30.f);
        r.ratios.push_back(flatRatio ? 0.5f : i / 30.f);
        for (int b = 0; b < bones; b++) r.frames.push_back(b == 37 ? QFromRotatorDeg(0, 30.f * i / (frames - 1), 0) : QIdent);
    }
    return FigureToJson(r);
}
EditorDoc Ready(Mode m = Mode::Replace) {
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkelJson(), err));
    CHECK(d.LoadFigure(FigJson(), err));
    d.NewClip(FIG_NoseManual);
    d.SetMode(m);
    return d;
}
} // namespace

TEST(editor_new_clip_defaults) {
    EditorDoc d = Ready();
    const Clip& c = d.clip();
    CHECK(c.mode == Mode::Replace && c.loop && c.enabled && c.drive == Drive::Time && !c.mirror);   // P6: a new clip is made for one variant: no goofy mirror
    CHECK_NEAR(c.duration, 3.0, 1e-6); CHECK_NEAR(c.blendIn, 0.25, 1e-6); CHECK_NEAR(c.blendOut, 0.30, 1e-6);
    CHECK(c.priority == 0 && c.states == ST_NoseManual && c.tracks.empty());
    CHECK(c.stances == 1 && c.feet == Feet::Regular);
}

TEST(editor_locked_bones_refuse_edit) {
    EditorDoc d = Ready();
    CHECK(!d.SetPending(Idx("AMXX_L_Thigh"), QFromRotatorDeg(0, 10, 0)));
    CHECK(!d.SetPending(Idx("AMXX_Pelvis"), QFromRotatorDeg(0, 10, 0)));
    CHECK(!d.SetPending(Idx("SKXX_SkateSkel_Root"), QFromRotatorDeg(0, 10, 0)));
    CHECK(d.SetPending(Idx("AMXX_R_UpperArm"), QFromRotatorDeg(0, 10, 0)));
    CHECK(IsEditableBone("AMXX_L_Hand_Xtra") && IsEditableBone("AMXX_Spine1") && IsEditableBone("AMXX_Head") && IsEditableBone("AMXX_R_Finger42"));
    CHECK(!IsEditableBone("AMXX_R_Calf") && !IsEditableBone("AMXX_Root") && !IsEditableBone("AMXX_R_Calf_IK_UPNODE"));
}

TEST(editor_replace_commit_then_edited_equals_key) {
    EditorDoc d = Ready();
    const Quat want = QFromRotatorDeg(20, -35, 10);
    CHECK(d.SetPending(37, want));
    d.CommitKey(0.4f);
    CHECK(!d.HasPending());
    CHECK(AngDeg(d.Edited(37, 0.4f), want) < 0.01f);
    CHECK(d.clip().tracks.size() == 1 && d.clip().tracks[0].bone == "AMXX_R_UpperArm");
}

TEST(editor_additive_commit_reproduces_wanted) {
    EditorDoc d = Ready(Mode::Additive);
    const Quat want = QFromRotatorDeg(20, -35, 10);
    CHECK(d.SetPending(37, want));
    d.CommitKey(0.5f);                       // the origin of bone 37 at 0.5 s is 15 degrees around Z
    CHECK(AngDeg(d.Edited(37, 0.5f), want) < 0.02f);
    CHECK(AngDeg(d.clip().tracks[0].keys[0].q, QMul(QInv(QFromRotatorDeg(0, 15, 0)), want)) < 0.05f);
}

TEST(editor_untracked_bone_follows_origin) {
    EditorDoc d = Ready();
    CHECK(AngDeg(d.Edited(37, 0.5f), QFromRotatorDeg(0, 15, 0)) < 0.05f);
    CHECK(AngDeg(d.Edited(37, 1.0f), QFromRotatorDeg(0, 30, 0)) < 0.05f);
    CHECK(AngDeg(d.Origin(37, 1.25f), QFromRotatorDeg(0, 7.5f, 0)) < 0.05f);   // loops over the recorded duration (1 s)
}

TEST(editor_set_mode_conversion_keeps_pose) {
    EditorDoc d = Ready();
    const Quat want = QFromRotatorDeg(5, 40, -12);
    d.SetPending(37, want); d.CommitKey(0.5f);
    d.SetMode(Mode::Additive);
    CHECK(d.clip().mode == Mode::Additive);
    CHECK(AngDeg(d.Edited(37, 0.5f), want) < 0.02f);
    d.SetMode(Mode::Replace);
    CHECK(AngDeg(d.Edited(37, 0.5f), want) < 0.02f);
}

TEST(editor_mirror_copy_matches_p3_mirror) {
    EditorDoc d = Ready();
    const int r = Idx("AMXX_R_UpperArm"), l = Idx("AMXX_L_UpperArm"), rh = Idx("AMXX_R_Hand"), lh = Idx("AMXX_L_Hand");
    d.SetPending(r, QFromRotatorDeg(10, 20, 30));
    d.SetPending(rh, QFromRotatorDeg(-5, 15, 25));
    d.MirrorCopy(r, 0.f);
    const MirrorMap m = RefMirror();
    Quat q;
    CHECK(d.Pending(l, &q));
    CHECK(AngDeg(q, MirrorKey(QFromRotatorDeg(10, 20, 30), m.conv[RefParents()[r]], m.conv[r])) < 0.01f);
    CHECK(d.Pending(lh, &q));
    CHECK(AngDeg(q, MirrorKey(QFromRotatorDeg(-5, 15, 25), m.conv[RefParents()[rh]], m.conv[rh])) < 0.01f);
    CHECK(d.Pending(Idx("AMXX_L_Forearm"), nullptr));     // descendant copied too (from its edited pose)
    CHECK(d.Pending(Idx("AMXX_L_Finger42"), nullptr));
}

TEST(editor_undo_redo_200) {
    EditorDoc d = Ready();
    for (int i = 1; i <= 205; i++) {
        d.Checkpoint("rotation");
        d.SetPending(37, QFromRotatorDeg(0, (float)i * 0.5f, 0));
        d.CommitKey(0.f);
    }
    auto yaw = [&]() { return AngDeg(d.Edited(37, 0.f), QIdent); };
    CHECK_NEAR(yaw(), 102.5, 0.05);
    int undone = 0;
    while (d.Undo()) undone++;
    CHECK(undone == 200);
    CHECK_NEAR(yaw(), 2.5, 0.05);               // state after action 5
    CHECK(d.Redo());
    CHECK_NEAR(yaw(), 3.0, 0.05);               // state after action 6
    CHECK(std::string(d.UndoLabel()) == "rotation");
}

TEST(editor_save_roundtrip) {
    EditorDoc d = Ready(Mode::Additive);
    d.SetPending(37, QFromRotatorDeg(3, 4, 5)); d.CommitKey(0.2f);
    d.SetPending(37, QFromRotatorDeg(6, 7, 8)); d.CommitKey(1.0f);
    std::string err;
    const std::string text = d.SaveText(err);
    CHECK(!text.empty() && err.empty());
    EditorDoc e = Ready();
    CHECK(e.OpenClip(text, "x.sanim", err));
    CHECK(e.clip().mode == Mode::Additive && e.clip().tracks.size() == 1 && e.clip().tracks[0].keys.size() == 2);
    CHECK(AngDeg(e.Edited(37, 1.0f), d.Edited(37, 1.0f)) < 0.02f);
    // unknown fields kept
    CHECK(e.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1},\"tracks\":{\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,1]}]},\"source\":\"capture\"}", "z.sanim", err));
    CHECK(e.SaveText(err).find("\"source\": \"capture\"") != std::string::npos);
}

TEST(editor_empty_clip_cannot_be_saved) {
    EditorDoc d = Ready();
    std::string err;
    CHECK(d.SaveText(err).empty());
    CHECK(!err.empty());
}

TEST(editor_keeps_locked_tracks) {
    EditorDoc d = Ready();
    std::string err;
    CHECK(d.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1},\"tracks\":{\"AMXX_L_Thigh\":[{\"t\":0,\"q\":[0,0,0.1,1]}],\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,1]}]}}", "z.sanim", err));
    CHECK(d.HasLockedTracks());
    CHECK(d.SaveText(err).find("AMXX_L_Thigh") != std::string::npos);
}

TEST(editor_bad_figure_or_skeleton) {
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(!d.LoadSkeleton("", err)); CHECK(!err.empty());
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv; s.version = 2;
    err.clear(); CHECK(!d.LoadSkeleton(SkeletonToJson(s), err)); CHECK(!err.empty());
    CHECK(d.LoadSkeleton(SkelJson(), err));
    err.clear(); CHECK(!d.LoadFigure(FigJson("NoseManual", 69), err)); CHECK(!err.empty());
    err.clear(); CHECK(!d.LoadFigure("{}", err)); CHECK(!err.empty());
    CHECK(!d.HasFigure());
    // without a recorded move, the editor stays usable: origin = identity
    d.NewClip(FIG_Manual);
    CHECK(std::fabs(QDot(d.Edited(37, 0.3f), d.Edited(37, 0.3f)) - 1.f) < 1e-4f);
}

TEST(editor_degenerate_figure) {
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkelJson(), err));
    CHECK(d.LoadFigure(FigJson("Manual", 70, 2, true), err));
    for (float t : {0.f, 0.01f, 0.5f, 3.f}) CHECK(std::fabs(std::sqrt(QDot(d.Origin(37, t), d.Origin(37, t))) - 1.f) < 1e-4f);
    for (float r : {-1.f, 0.5f, 2.f}) CHECK(std::fabs(std::sqrt(QDot(d.OriginAtRatio(37, r), d.OriginAtRatio(37, r))) - 1.f) < 1e-4f);
}

TEST(editor_pending_blocks_nothing_silently) {
    EditorDoc d = Ready();
    CHECK(!d.HasPending());
    d.SetPending(37, QFromRotatorDeg(0, 5, 0));
    CHECK(d.HasPending());
    d.CommitKey(0.f);
    CHECK(!d.HasPending());
    d.SetPending(37, QFromRotatorDeg(0, 9, 0));
    d.DiscardPending();
    CHECK(!d.HasPending());
    CHECK(AngDeg(d.Edited(37, 0.f), QFromRotatorDeg(0, 5, 0)) < 0.01f);
}

TEST(editor_keys_delete_and_move) {
    EditorDoc d = Ready();
    d.SetPending(37, QFromRotatorDeg(0, 5, 0)); d.CommitKey(0.2f);
    d.SetPending(37, QFromRotatorDeg(0, 9, 0)); d.CommitKey(0.8f);
    CHECK(!d.MoveKeys(0.2f, 0.8f));            // collision
    CHECK(!d.MoveKeys(0.2f, 5.0f));            // out of the duration
    CHECK(d.MoveKeys(0.2f, 0.4f));
    CHECK(d.KeyTimes(37).size() == 2 && std::fabs(d.KeyTimes(37)[0] - 0.4f) < 1e-4f);
    d.DeleteKeysAt(0.4f, -1);
    CHECK(d.KeyTimes(37).size() == 1);
    d.DeleteKeysAt(0.8f, 37);
    CHECK(d.clip().tracks.empty());
}

TEST(editor_take_origin_pose) {
    EditorDoc d = Ready();
    d.TakeOriginPose(1.0f);
    Quat q;
    CHECK(d.Pending(37, &q));
    CHECK(AngDeg(q, QFromRotatorDeg(0, 30, 0)) < 0.05f);
    CHECK(!d.Pending(Idx("AMXX_L_Thigh"), nullptr));   // locked bone: untouched
}

TEST(editor_component_pose_ghost_vs_edited) {
    EditorDoc d = Ready();
    d.SetPending(37, QFromRotatorDeg(0, 90, 0)); d.CommitKey(0.f);
    std::vector<FTr> ed, gh;
    d.ComponentPose(0.f, ed, false);
    d.ComponentPose(0.f, gh, true);
    CHECK(ed.size() == 70 && gh.size() == 70);
    const int hand = Idx("AMXX_R_Hand");
    const float dx = ed[hand].px - gh[hand].px, dy = ed[hand].py - gh[hand].py, dz = ed[hand].pz - gh[hand].pz;
    CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) > 10.f);   // the hand moved with the arm
    CHECK(std::fabs(ed[2].pz - gh[2].pz) < 1e-4f);          // not the pelvis
}

// --write-test-data <folder>: test data to run the editor without the game
#include <windows.h>
#include <cstdio>
int WriteTestDataMain(const char* dir) {
    auto put = [](const std::string& path, const std::string& text) {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        return true;
    };
    const std::string d = dir;
    CreateDirectoryA(d.c_str(), nullptr);
    CreateDirectoryA((d + "\\_figures").c_str(), nullptr);
    bool ok = put(d + "\\_skeleton.json", SkelJson());
    ok = put(d + "\\_figures\\NoseManual.json", FigJson("NoseManual")) && ok;
    ok = put(d + "\\_figures\\Manual.json", FigJson("Manual")) && ok;
    std::printf("%s\n", ok ? "test data written" : "cannot write");
    return ok ? 0 : 1;
}

// P6: copy a clip to another variant: same body side -> same tracks; other side -> mirrored to the other arm
TEST(p6_editor_retarget_variant) {
    EditorDoc d = Ready();
    const int ru = Idx("AMXX_R_UpperArm"), lu = Idx("AMXX_L_UpperArm");
    const Quat q = QFromRotatorDeg(10, 40, 5);
    CHECK(d.SetPending(ru, q));
    d.CommitKey(0.5f);
    FigureVariant v = d.Variant();
    v.stance = STANCE_Fakie;   // fakie: same body side
    d.RetargetVariant(v);
    CHECK(d.TrackOf(ru) != nullptr && d.TrackOf(lu) == nullptr && d.Variant() == v);
    CHECK(d.clip().stances == (1 << STANCE_Fakie) && d.clip().feet == Feet::Regular);
    v.goofy = true;            // goofy: the other side
    d.RetargetVariant(v);
    CHECK(d.TrackOf(lu) != nullptr && d.TrackOf(ru) == nullptr && d.clip().feet == Feet::Goofy);
    // mirror back to regular: the original key again
    v.goofy = false;
    d.RetargetVariant(v);
    const Track* t = d.TrackOf(ru);
    CHECK(t && !t->keys.empty() && AngDeg(t->keys.back().q, q) < 0.01f);
    // the json the game reads holds the variant
    std::string err;
    const std::string js = d.SaveText(err);
    CHECK(js.find("\"stances\"") != std::string::npos && js.find("\"feet\": \"regular\"") != std::string::npos);
}
