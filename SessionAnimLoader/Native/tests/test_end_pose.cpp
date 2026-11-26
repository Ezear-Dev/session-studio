// Editor: "this part has not come back" warning at the ends of trick and landing clips (PLAN-EDITOR-3-ADDITIONS task 2).
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/player.h"
#include "../src/skeleton.h"
#include "../src/trick_sync.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <algorithm>
#include <memory>

namespace {
std::string SkelE() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// a kickflip figure: 0.8 s in the air (phases 0..4), then 0.8 s on the ground (phase 5)
FigureRec FigE() {
    FigureRec f; f.figure = "KickFlip_RGS_regular"; f.bones = (int)RefNames().size();
    for (int i = 0; i < 48; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        f.phases.push_back(i >= 24 ? FigureRecorder::kGroundPhase : i < 1 ? 0 : i < 6 ? 1 : i < 14 ? 2 : i < 18 ? 3 : 4);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QFromRotatorDeg(i * 1.f, (float)(b % 9), 0.f));
    }
    return f;
}
int Bone(const EditorDoc& d, const char* n) {
    for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == n) return b;
    return -1;
}
const FigureVariant kKF{ST_Trick, "KickFlip", STANCE_Normal, false};
const FigureVariant kLand{ST_Landing, "KickFlip", STANCE_Normal, false};
const float kF = 1.f / 30.f;

void SetupTrick(EditorDoc& d) {
    std::string err;
    CHECK(d.LoadSkeleton(SkelE(), err));
    CHECK(d.LoadFigure(FigureToJson(FigE()), kKF, err));
    d.NewClip(kKF);
    CHECK(d.clip().mode == Mode::Additive);
}
void SetupLanding(EditorDoc& d) {
    std::string err;
    CHECK(d.LoadSkeleton(SkelE(), err));
    FigureRec tail;
    CHECK(GroundTailFigure(FigE(), tail));
    tail.figure = "Landing_KickFlip_RGS_regular";
    CHECK(d.LoadFigure(FigureToJson(tail), kLand, err));
    d.NewClip(kLand);
    CHECK(d.IsLandingClip() && d.clip().mode == Mode::Additive);
}
// puts a key of `deg` degrees of change (pitch) on a bone at t
void KeyDeg(EditorDoc& d, int bone, float t, float deg) {
    Track* tr = nullptr;
    for (auto& x : d.clip().tracks) if (x.bone == d.skeleton().names[bone]) tr = &x;
    if (!tr) { Track n; n.bone = d.skeleton().names[bone]; n.index = bone; d.clip().tracks.push_back(n); tr = &d.clip().tracks.back(); }
    tr->keys.erase(std::remove_if(tr->keys.begin(), tr->keys.end(), [&](const Key& k) { return std::fabs(k.t - t) < 1e-3f; }), tr->keys.end());
    tr->keys.push_back(Key{t, QFromRotatorDeg(deg, 0, 0), true});
    std::sort(tr->keys.begin(), tr->keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
}
void KeyPel(EditorDoc& d, float t, float x) {
    auto& p = d.clip().pelvis;
    p.erase(std::remove_if(p.begin(), p.end(), [&](const PosKey& k) { return std::fabs(k.t - t) < 1e-3f; }), p.end());
    p.push_back(PosKey{t, {x, 0, 0}, true});
    std::sort(p.begin(), p.end(), [](const PosKey& a, const PosKey& b) { return a.t < b.t; });
}
bool HasBone(const EditorDoc::EndIssue& e, int b) { return std::find(e.bones.begin(), e.bones.end(), b) != e.bones.end(); }
int CountKind(const std::vector<EditorDoc::EndIssue>& v, EditorDoc::EndIssue::Kind k) {
    int n = 0; for (auto& e : v) if (e.kind == k) n++; return n;
}

// player rig: one bone under a root
float RollDeg(const Quat& q) { const float w = std::fabs(q.w) > 1.f ? 1.f : std::fabs(q.w); return 2.f * std::acos(w) * 57.29578f; }
}

// What happens today at touchdown with a trick clip that has no landing clip and does not come back to the game's pose:
// the trick layer fades out over its blend out from the ground contact (a fast change), the limb is not left bent.
TEST(endpose_player_trick_without_landing_fades_at_touchdown) {
    auto p = std::make_unique<Player>();
    const int par[2] = {-1, 0};
    p->SetRig(par, 2, MirrorMap{});
    ClipSet set; set.generation = 1;
    Clip c; c.file = "kf"; c.name = "kf"; c.states = ST_Trick; c.trick = "KickFlip"; c.trickHash = TrickHash("KickFlip");
    c.loop = false; c.mirror = false; c.blendIn = 0.f; c.blendOut = 0.15f; c.duration = 0.8f; c.mode = Mode::Additive;
    c.markers = MarkersFromPhases(FigE().phases, FigE().times);
    Track t; t.bone = "b"; t.index = 1; t.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, 30), true});   // 30 deg all along
    c.tracks.push_back(t);
    set.clips.push_back(c);
    float deg = 0.f;
    auto step = [&](const GameState& g) {
        FTr cs[2] = {FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0}, FTr{0, 0, 0, 1, 10, 0, 0, 0, 1, 1, 1, 0}};
        p->Apply(cs, 2, &set, g, 1.f / 60.f);
        deg = RollDeg(TrQ(cs[1]));
    };
    GameState a{}; a.kickflip = true; a.popSerial = 1; a.trickPhase = 1; a.trickHash = TrickHash("KickFlip");
    for (int i = 0; i < 40; i++) step(a);
    CHECK_NEAR(deg, 30.0, 0.5);   // in the air: the clip's 30 deg
    GameState g{}; g.landing = true; g.popSerial = 1; g.landTrickHash = TrickHash("KickFlip"); g.landStance = STANCE_Normal;
    step(g);
    CHECK(deg > 20.f);            // just at the contact: still bent
    for (int i = 0; i < 6; i++) step(g);   // 0.1 s later
    CHECK(deg < 20.f);            // fading fast
    for (int i = 0; i < 6; i++) step(g);   // 0.2 s after the contact
    CHECK(deg < 1.f);             // back to the game's pose: nothing stays bent
}

TEST(endpose_trick_end_thresholds) {
    EditorDoc d; SetupTrick(d);
    const float end = d.clip().duration;
    const int arm = Bone(d, "AMXX_R_UpperArm"), head = Bone(d, "AMXX_Head");
    KeyDeg(d, arm, 0.f, 0.f); KeyDeg(d, arm, end, 5.f);
    KeyDeg(d, head, 0.f, 0.f); KeyDeg(d, head, end, 2.f);
    auto v = d.EndIssues(nullptr, false);
    CHECK(v.size() == 1u);
    if (v.size() == 1u) {
        CHECK(v[0].kind == EditorDoc::EndIssue::TrickEndNoLanding);
        CHECK(HasBone(v[0], arm) && !HasBone(v[0], head) && !v[0].pelvis);
        CHECK_NEAR(v[0].maxDeg, 5.0, 0.1);
    }
    // a pelvis offset of 1.5 cm left at the end
    KeyDeg(d, arm, end, 0.f);
    KeyPel(d, 0.f, 0.f); KeyPel(d, end, 1.5f);
    v = d.EndIssues(nullptr, false);
    CHECK(v.size() == 1u && v[0].pelvis && v[0].bones.empty());
    if (!v.empty()) CHECK_NEAR(v[0].maxCm, 1.5, 0.01);
    KeyPel(d, end, 0.8f);
    CHECK(d.EndIssues(nullptr, false).empty());
    // the same clip with a landing for all its variants: its end is free
    KeyPel(d, end, 1.5f);
    CHECK(d.EndIssues(nullptr, true).empty());
}

TEST(endpose_landing_cover_of_all_variants) {
    Clip trick; trick.states = ST_Trick; trick.trick = "KickFlip"; trick.trickHash = TrickHash("KickFlip");
    trick.stances = (1u << STANCE_Normal) | (1u << STANCE_Switch); trick.feet = Feet::Regular;
    auto land = [](uint8_t stances, Feet f, const char* tr) {
        Clip c; c.states = ST_Landing; c.trick = tr; c.trickHash = TrickHash(tr); c.stances = stances; c.feet = f; c.enabled = true;
        return c;
    };
    std::string missing;
    const Clip ln = land(1u << STANCE_Normal, Feet::Regular, "KickFlip");
    const Clip ls = land(1u << STANCE_Switch, Feet::Any, "KickFlip");
    const Clip generic = land(0xF, Feet::Any, "");
    Clip off = ls; off.enabled = false;
    const Clip other = land(0xF, Feet::Any, "Ollie");
    CHECK(LandingCoversAllVariants(trick, {&ln, &ls}, missing) && missing.empty());
    CHECK(!LandingCoversAllVariants(trick, {&ln, &generic, &off, &other}, missing));   // generic, disabled, other trick: no
    CHECK(missing.find("Switch") != std::string::npos || missing.find("switch") != std::string::npos);
    CHECK(missing.find("Normal") == std::string::npos && missing.find("normal") == std::string::npos);
    // both feet: the goofy variants need a landing too
    trick.feet = Feet::Any;
    CHECK(!LandingCoversAllVariants(trick, {&ln, &ls}, missing));
    CHECK(missing.find("goofy") != std::string::npos);
}

TEST(legs_frozen_from_the_catch_in_trick_clips) {
    EditorDoc d; SetupTrick(d);
    const int foot = Bone(d, "AMXX_R_Foot"), arm = Bone(d, "AMXX_R_UpperArm");
    CHECK(foot >= 0 && d.IsEditable(foot));
    CHECK(!d.LegFrozenAt(foot, 0.30f));   // before the catch (frame 14 = 0.467 s)
    CHECK(d.LegFrozenAt(foot, 0.53f));    // after it: the game takes the legs back
    CHECK(!d.LegFrozenAt(arm, 0.53f));
    EditorDoc l; SetupLanding(l);
    CHECK(!l.LegFrozenAt(foot, 0.1f));    // landing clips: legs free
}

TEST(endpose_trick_legs_ignored_after_the_catch) {
    EditorDoc d; SetupTrick(d);
    const int calf = Bone(d, "AMXX_L_Calf");
    CHECK(d.IsEditable(calf));
    KeyDeg(d, calf, 0.f, 0.f); KeyDeg(d, calf, 10 * kF, 40.f);   // a leg key before the catch, held to the end
    CHECK(d.EndIssues(nullptr, false).empty());
}

TEST(endpose_landing_end_and_start) {
    // the trick: 20 deg on the arm and 10 deg on the head at its end, pelvis 3 cm
    EditorDoc t; SetupTrick(t);
    const int arm = Bone(t, "AMXX_R_UpperArm"), head = Bone(t, "AMXX_Head"), spine = Bone(t, "AMXX_Spine2");
    const float te = t.clip().duration;
    KeyDeg(t, arm, 0.f, 0.f); KeyDeg(t, arm, te, 20.f);
    KeyDeg(t, head, 0.f, 0.f); KeyDeg(t, head, te, 10.f);
    KeyPel(t, 0.f, 0.f); KeyPel(t, te, 3.f);
    t.clip().markers = t.Markers();
    const Clip trick = t.clip();

    EditorDoc d; SetupLanding(d);
    CHECK(d.SeedLandingFromTrick(trick) > 0);
    const float le = d.clip().duration;
    CHECK(d.EndIssues(&trick, false).empty());   // seeded: starts at the trick's end, ends at the game's pose
    // end not back: 6 deg left on the arm
    KeyDeg(d, arm, le, 6.f);
    auto v = d.EndIssues(&trick, false);
    CHECK(v.size() == 1u && v[0].kind == EditorDoc::EndIssue::LandingEndNotBack && HasBone(v[0], arm));
    KeyDeg(d, arm, le, 0.f);
    // start different from the trick's end: only the differing parts
    KeyDeg(d, head, 0.f, 25.f);
    KeyDeg(d, spine, 0.f, 0.f); KeyDeg(d, spine, le, 0.f);   // same as the trick (nothing): not listed
    v = d.EndIssues(&trick, false);
    CHECK(v.size() == 1u);
    if (v.size() == 1u) {
        CHECK(v[0].kind == EditorDoc::EndIssue::LandingStartMismatch);
        CHECK(HasBone(v[0], head) && !HasBone(v[0], arm) && !HasBone(v[0], spine) && !v[0].pelvis);
        CHECK_NEAR(v[0].maxDeg, 15.0, 0.2);
    }
    if (v.size() != 1u) return;
    // the fix takes the end of the trick for the head only; one undo restores
    const std::string before = ClipToJson(d.clip());
    const Quat armStart = d.TrackOf(arm)->keys.front().q;
    CHECK(d.FixEndIssue(v[0], &trick));
    CHECK(d.EndIssues(&trick, false).empty());
    CHECK(std::fabs(std::fabs(d.TrackOf(arm)->keys.front().q.w) - std::fabs(armStart.w)) < 1e-6f);
    CHECK(d.Undo());
    CHECK(ClipToJson(d.clip()) == before);
}

TEST(endpose_fix_then_recheck_and_undo) {
    EditorDoc d; SetupTrick(d);
    const float end = d.clip().duration;
    const int arm = Bone(d, "AMXX_R_UpperArm"), head = Bone(d, "AMXX_Head");
    KeyDeg(d, arm, 0.f, 0.f); KeyDeg(d, arm, 10 * kF, 30.f);   // the last key in the middle: held to the end
    KeyDeg(d, head, 0.f, 0.f); KeyDeg(d, head, end, 8.f);
    KeyPel(d, 0.f, 0.f); KeyPel(d, 12 * kF, 4.f);
    auto v = d.EndIssues(nullptr, false);
    CHECK(v.size() == 1u && v[0].bones.size() == 2u && v[0].pelvis);
    if (v.size() != 1u) return;
    const std::string before = ClipToJson(d.clip());
    CHECK(d.FixEndIssue(v[0], nullptr));
    CHECK(d.EndIssues(nullptr, false).empty());
    // the key in the middle is kept
    bool mid = false; for (auto& k : d.TrackOf(arm)->keys) if (std::fabs(k.t - 10 * kF) < 1e-3f) mid = true;
    CHECK(mid);
    CHECK(d.Undo());
    CHECK(ClipToJson(d.clip()) == before);
}

TEST(endpose_replace_and_loop_clips_have_none) {
    EditorDoc d; SetupTrick(d);
    const int arm = Bone(d, "AMXX_R_UpperArm");
    KeyDeg(d, arm, 0.f, 0.f); KeyDeg(d, arm, d.clip().duration, 30.f);
    CHECK(!d.EndIssues(nullptr, false).empty());
    d.clip().mode = Mode::Replace;
    CHECK(d.EndIssues(nullptr, false).empty());
    FigureVariant man{ST_Manual, "", STANCE_Normal, false};
    d.NewClip(man);
    KeyDeg(d, arm, 0.f, 30.f); KeyDeg(d, arm, 1.f, 30.f);
    CHECK(d.EndIssues(nullptr, false).empty());
}
