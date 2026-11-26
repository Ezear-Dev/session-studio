// 2026-11-25: held pose (play.hold) for the base poses, loop seam (a key at the end of a loop is the key at 0),
// and the format round trip.
#include "check.h"
#include "../src/clip.h"
#include "../src/skeleton.h"
#include "../src/figure.h"
#include "../../AnimStudioEditor/model/editor_doc.h"
#include <cmath>
#include <cstdio>

namespace {
int HIdx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }
float Ang(const Quat& a, const Quat& b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.f * std::acos(d > 1.f ? 1.f : d) * 57.2958f;
}
std::string SkelH() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// 3 s figure where the right upper arm swings (so the original animation moves under a held pose)
std::string FigH(const char* name) {
    FigureRec r; r.figure = name; r.bones = (int)RefNames().size();
    const int arm = HIdx("AMXX_R_UpperArm");
    for (int i = 0; i <= 90; i++) {
        r.times.push_back(i / 30.f);
        r.ratios.push_back(i / 90.f);
        for (int b = 0; b < r.bones; b++) r.frames.push_back(b == arm ? QFromRotatorDeg(0, 0, 30.f * std::sin(i / 30.f * 3.f)) : QIdent);
    }
    return FigureToJson(r);
}
bool SetupH(EditorDoc& d, uint32_t state, const char* figName) {
    std::string err;
    if (!d.LoadSkeleton(SkelH(), err)) { printf("skel: %s\n", err.c_str()); return false; }
    FigureVariant v; v.state = state;
    if (!d.LoadFigure(FigH(figName), v, err)) { printf("fig: %s\n", err.c_str()); return false; }
    d.NewClip(v);
    return true;
}
} // namespace

TEST(hold_format_roundtrip) {
    Clip c;
    c.name = "h"; c.states = ST_Riding; c.duration = 3.f; c.hold = true;
    Track tr; tr.bone = "AMXX_R_UpperArm"; tr.keys.push_back(Key{0.f, QFromRotatorDeg(0, 20, 0), true});
    c.tracks.push_back(tr);
    const std::string js = ClipToJson(c);
    CHECK(js.find("\"hold\"") != std::string::npos);
    Clip back; std::string err;
    CHECK(ParseClip(js, "h", back, err));
    CHECK(back.hold);
    CHECK(ClipToJson(back) == js);
    // a clip without the field is not held, and does not write it
    c.hold = false;
    CHECK(ClipToJson(c).find("\"hold\"") == std::string::npos);
    // the player reads a held clip at t = 0, whatever the time or the ratio
    c.hold = true;
    CHECK_NEAR(ClipTime(c, 2.5f, 0.7f), 0.0, 1e-6);
    c.drive = Drive::ManualRatio;
    CHECK_NEAR(ClipTime(c, 2.5f, 0.7f), 0.0, 1e-6);
}

TEST(hold_default_for_base_poses) {
    for (uint32_t st : {ST_OnFoot, ST_Riding, ST_Crouch, ST_Walking, ST_Pushing}) {
        EditorDoc d;
        CHECK(SetupH(d, st, "Riding_RGS_regular"));
        CHECK(!d.IsHold() && d.CanHold());   // animated loop by default, hold is an option
    }
    for (uint32_t st : {ST_Manual, ST_Landing}) {
        EditorDoc d;
        CHECK(SetupH(d, st, "Manual_RGS_regular"));
        CHECK(!d.IsHold());
    }
    EditorDoc d;
    CHECK(SetupH(d, ST_Landing, "Landing_RGS_regular"));
    CHECK(!d.CanHold());
    d.SetHold(true, 0.f);
    CHECK(!d.clip().hold);   // one shot clips never hold
}

TEST(hold_key_applies_all_the_time) {
    EditorDoc d;
    CHECK(SetupH(d, ST_Walking, "Walking_RGS_regular"));
    d.SetHold(true, 0.f);
    const int arm = HIdx("AMXX_R_UpperArm");
    const int fore = HIdx("AMXX_R_Forearm");
    CHECK(arm >= 0 && fore >= 0);
    // key at 2.9 s (the "last second"): it lands at 0 and shows at every time
    const Quat bend = QFromRotatorDeg(0, 35, 0);
    CHECK(d.SetPending(fore, QMul(d.Origin(fore, 2.9f), bend)));
    d.CommitKey(2.9f);
    const Track* tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() == 1);
    if (tr && !tr->keys.empty()) CHECK_NEAR(tr->keys[0].t, 0.0, 1e-6);
    for (float t : {0.f, 1.f, 2.9f, 3.f}) CHECK(Ang(d.Edited(fore, t), QMul(d.Origin(fore, t), bend)) < 0.5f);
    // a second edit elsewhere replaces the same single key (no hold keys, nothing to loop)
    const Quat lift = QFromRotatorDeg(0, 0, 15);
    CHECK(d.SetPending(arm, QMul(d.Origin(arm, 1.2f), lift)));
    d.CommitKey(1.2f);
    CHECK(d.KeyTimes(-1).size() == 1);
    // additive: the offset follows the game's swing
    for (float t : {0.3f, 1.7f, 2.6f}) CHECK(Ang(d.Edited(arm, t), QMul(d.Origin(arm, t), lift)) < 0.5f);
    // saved, reloaded, still held
    std::string err;
    const std::string js = d.SaveText(err);
    CHECK(!js.empty());
    CHECK(js.find("\"hold\"") != std::string::npos);
}

TEST(hold_toggle_collapses_keys) {
    EditorDoc d;
    CHECK(SetupH(d, ST_Riding, "Riding_RGS_regular"));
    d.SetHold(false, 0.f);
    CHECK(!d.IsHold());
    const int fore = HIdx("AMXX_R_Forearm");
    CHECK(d.SetPending(fore, QFromRotatorDeg(0, 10, 0))); d.CommitKey(0.f);
    CHECK(d.SetPending(fore, QFromRotatorDeg(0, 50, 0))); d.CommitKey(1.5f);
    const Quat at15 = d.Edited(fore, 1.5f);
    d.SetHold(true, 1.5f);   // the pose at the playhead becomes the held pose
    const Track* tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() == 1);
    for (float t : {0.f, 0.7f, 2.2f}) CHECK(Ang(d.Edited(fore, t), at15) < 0.5f);   // Origin is identity on the forearm
    d.Undo();
    CHECK(!d.clip().hold);
    tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() >= 2);
}

TEST(loop_end_key_is_the_start_key) {
    EditorDoc d;
    CHECK(SetupH(d, ST_Manual, "Manual_RGS_regular"));
    CHECK(!d.IsHold() && d.clip().loop);
    const int fore = HIdx("AMXX_R_Forearm");
    CHECK(d.SetPending(fore, QFromRotatorDeg(0, 10, 0))); d.CommitKey(0.f);
    CHECK(d.SetPending(fore, QFromRotatorDeg(0, 40, 0))); d.CommitKey(1.5f);
    // editing at the last instant edits the start of the loop: what is shown at the end changes
    CHECK(d.SetPending(fore, QFromRotatorDeg(0, 25, 0)));
    d.CommitKey(d.clip().duration);
    const Track* tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() == 2);
    CHECK(Ang(d.Edited(fore, d.clip().duration), QFromRotatorDeg(0, 25, 0)) < 0.5f);
    CHECK(Ang(d.Edited(fore, 0.f), QFromRotatorDeg(0, 25, 0)) < 0.5f);
    // an old clip with a key at the end: on open, it goes to 0 when 0 has none, else it is dropped
    Clip c = d.clip();
    for (auto& t2 : c.tracks) if (t2.index == fore) { t2.keys.clear(); t2.keys.push_back(Key{1.f, QFromRotatorDeg(0, 5, 0), true}); t2.keys.push_back(Key{c.duration, QFromRotatorDeg(0, 60, 0), true}); }
    std::string err;
    CHECK(d.OpenClip(ClipToJson(c), "x", err));
    tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() == 2);
    if (tr && tr->keys.size() == 2) { CHECK_NEAR(tr->keys[0].t, 0.0, 1e-6); CHECK(Ang(tr->keys[0].q, QFromRotatorDeg(0, 60, 0)) < 0.5f); }
}

// 2026-11-25 (user report): a pelvis-only crouch clip with keys at 0 / 1 / 2 s got "hold" from the figure defaults
// (it looked empty: no bone track), so every edit went to the key at 0.
TEST(hold_not_forced_on_pelvis_only_clip) {
    EditorDoc d;
    CHECK(SetupH(d, ST_Crouch, "Crouch_RGS_regular"));
    Clip c = d.clip();
    c.hold = false;
    c.tracks.clear();
    c.pelvis = {PosKey{0.f, {0, 0, 30}, true}, PosKey{1.f, {4, 3, 34}, true}, PosKey{2.f, {4, 3, 33}, true}};
    std::string err;
    CHECK(d.OpenClip(ClipToJson(c), "c", err));
    FigureVariant v; v.state = ST_Crouch;
    CHECK(d.LoadFigure(FigH("Crouch_RGS_regular"), v, err));   // picking the variant again in the editor
    CHECK(!d.IsHold());
    CHECK(d.KeyTimes(-1).size() == 3);
    // a file that says hold but has keys elsewhere: the keys win
    c.hold = true;
    CHECK(d.OpenClip(ClipToJson(c), "c", err));
    CHECK(!d.IsHold());
    const int fore = HIdx("AMXX_R_Forearm");
    CHECK(d.SetPending(fore, QFromRotatorDeg(0, 30, 0)));
    d.CommitKey(1.f);
    const Track* tr = d.TrackOf(fore);
    bool at1 = false;
    if (tr) for (auto& k : tr->keys) if (std::fabs(k.t - 1.f) < 1e-3f) at1 = true;
    CHECK(at1);
}

// 2026-11-25 (user report, screenshot): crouch clip, one pelvis key at 1.5 s -> the start of the clip was moved too
// (a lone key applies everywhere), and the pelvis raised 35 cm lifted the feet: skeleton in the air.
TEST(first_key_mid_clip_keeps_the_start) {
    EditorDoc d;
    CHECK(SetupH(d, ST_Crouch, "Crouch_RGS_regular"));
    d.SetHold(false, 0.f);
    const int fore = HIdx("AMXX_R_Forearm");
    CHECK(d.SetPending(fore, QMul(d.Origin(fore, 1.5f), QFromRotatorDeg(0, 40, 0))));
    const float pel[3] = {0, 0, -8};
    CHECK(d.SetPendingPelvis(pel));
    d.CommitKey(1.5f);
    CHECK(Ang(d.Edited(fore, 0.f), d.Origin(fore, 0.f)) < 0.5f);   // the start is the game's pose
    CHECK(Ang(d.Edited(fore, 1.5f), QMul(d.Origin(fore, 1.5f), QFromRotatorDeg(0, 40, 0))) < 0.5f);
    float o0[3], o15[3];
    d.PelvisOffset(0.f, o0);
    d.PelvisOffset(1.5f, o15);
    CHECK_NEAR(o0[2], 0.0, 1e-4);
    CHECK_NEAR(o15[2], -8.0, 1e-4);
}

TEST(pelvis_offset_stops_at_leg_reach) {
    EditorDoc d;
    CHECK(SetupH(d, ST_Crouch, "Crouch_RGS_regular"));
    std::vector<FTr> before, after;
    d.ComponentPose(0.f, before, true);   // the game's pose
    const float up[3] = {0, 0, 60};
    CHECK(d.SetPendingPelvis(up));
    d.ComponentPose(0.f, after, false);
    const int lf = HIdx("AMXX_L_Foot"), rf = HIdx("AMXX_R_Foot"), pv = HIdx("AMXX_Pelvis");
    auto dist = [](const FTr& a, const FTr& b) { return std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) + (a.pz - b.pz) * (a.pz - b.pz)); };
    CHECK(dist(after[lf], before[lf]) < 0.05f);   // feet stay planted
    CHECK(dist(after[rf], before[rf]) < 0.05f);
    CHECK(after[pv].pz - before[pv].pz < 60.f - 1.f);   // the pelvis stopped short
}
