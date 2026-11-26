// The format (Kickflip state, phases in recorded moves, leg bones)
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../src/player.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <cmath>
#include <cstring>
#include <memory>

TEST(p5b_clip_kickflip_state_roundtrip) {
    const char* txt = R"({"format":"sanim","version":1,"name":"kf","trigger":{"states":["Kickflip"],"priority":3},
        "play":{"duration":1.0,"loop":false},"mode":"replace","tracks":{"AMXX_L_Thigh":[{"t":0,"q":[0,0,0,1]}]}})";
    Clip c; std::string err;
    CHECK(ParseClip(txt, "kf.sanim", c, err));
    CHECK(c.states == ST_Kickflip && ST_Kickflip == 4);
    Clip d;
    CHECK(ParseClip(ClipToJson(c), "kf.sanim", d, err));
    CHECK(d.states == ST_Kickflip);
    Clip m = c; m.states = ST_Manual | ST_Kickflip;
    CHECK(ParseClip(ClipToJson(m), "kf.sanim", d, err) && d.states == (ST_Manual | ST_Kickflip));
}

TEST(p5b_figure_phases_roundtrip) {
    FigureRec a;
    a.figure = "Kickflip"; a.bones = 2;
    a.times = {0.f, 1.f / 30.f, 2.f / 30.f}; a.ratios = {0, 0, 0};
    a.frames.assign(6, QIdent);
    a.phases = {1, 2, 3};
    FigureRec b; std::string err;
    CHECK(ParseFigure(FigureToJson(a), b, err));
    CHECK(b.figure == "Kickflip" && b.phases == a.phases);
    FigureRec bad = a; bad.phases = {1, 2};   // length != frame count
    err.clear(); CHECK(!ParseFigure(FigureToJson(bad), b, err)); CHECK(!err.empty());
}

TEST(p5b_figure_without_phases_still_parses) {
    FigureRec a;
    a.figure = "Manual"; a.bones = 1;
    a.times = {0.f, 0.1f}; a.ratios = {0, 0};
    a.frames.assign(2, QIdent);
    const std::string js = FigureToJson(a);
    CHECK(js.find("phases") == std::string::npos);   // written only if not empty
    FigureRec b; std::string err;
    CHECK(ParseFigure(js, b, err) && b.phases.empty());
}

TEST(p5b_figure_legacy_and_loops) {
    CHECK(FIG_Kickflip == 2 && FIG_Count == 3);
    CHECK(LegacyVariant(FIG_Kickflip).state == ST_Kickflip && LegacyVariant(FIG_Kickflip).trick == "KickFlip");
    CHECK(StateLoops(ST_Manual) && StateLoops(ST_NoseManual) && !StateLoops(ST_Trick));
}

TEST(p5b_gamestate_defaults) {
    GameState gs{true, false, false, false, 0.5f};   // 5 fields: must compile
    CHECK(!gs.kickflip && gs.popSerial == 0 && gs.trickPhase == 0 && gs.trickHash == 0 && gs.figId == 0 && !gs.figWanted);
}

TEST(p5b_leg_bone_names) {
    CHECK(IsLegBone("AMXX_L_Thigh") && IsLegBone("AMXX_R_Calf") && IsLegBone("AMXX_L_Foot") && IsLegBone("AMXX_R_Toe0"));
    CHECK(IsLegBone("L_Thigh") && IsLegBone("R_Calf") && IsLegBone("L_Foot") && IsLegBone("R_Toe0"));
    CHECK(!IsLegBone("AMXX_R_Calf_IK_UPNODE") && !IsLegBone("R_Calf_IK_UPNODE"));
    CHECK(!IsLegBone("SKXX_SkateSkel_Foot_Anchor_L") && !IsLegBone("Pelvis") && !IsLegBone(""));
}

// ------------------------------------------------------------------ trick figure recording (P5b, generalised in P6)
namespace {
constexpr int kRecBones = 8;

// 1/60 s steps; phase follows phaseAt(t); the trick stays on until `until` seconds. figure 5 = the trick's variant
template <class F> void RunKF(FigureRecorder& r, uint32_t pop, float until, float total, F phaseAt, bool wanted = true, int fig = 5) {
    std::vector<Quat> q(kRecBones, QIdent);
    const int n = (int)(total * 60.f + 0.5f);
    for (int i = 0; i < n; i++) {
        const float t = (float)i / 60.f;
        GameState g{false, false, false, false, 0.f};
        g.kickflip = t < until; g.popSerial = pop; g.trickPhase = phaseAt(t);
        g.figId = g.kickflip ? fig : 0; g.figWanted = g.kickflip && wanted;
        r.Tick(g, q.data(), nullptr, kRecBones, 1.f / 60.f);
    }
}
int PhaseAt(float t) { return t < 0.2f ? 1 : t < 0.4f ? 2 : t < 0.6f ? 3 : 4; }
}

TEST(p5b_recorder_kickflip_one_shot) {
    FigureRecorder r; r.Reset(kRecBones);
    std::vector<Quat> q(kRecBones, QIdent);
    GameState g0{false, false, false, false, 0.f};   // ground, popSerial 0: nothing starts
    r.Tick(g0, q.data(), nullptr, kRecBones, 1.f / 60.f);
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    RunKF(r, 1, 0.8f, 3.0f, PhaseAt);   // pop (serial 0 -> 1) then ground at 0.8 s, then the 2 s ground tail
    CHECK(r.TakeFinished(out, id));
    CHECK(id == 5);
    CHECK(out.times.size() >= 24 + FigureRecorder::kGroundTail && out.times.size() <= 26 + FigureRecorder::kGroundTail);
    CHECK(out.phases.size() == out.times.size());
    CHECK(out.frames.size() == out.times.size() * kRecBones);
    if (!out.phases.empty()) {
        CHECK(out.phases.front() == 1 && out.phases.back() == FigureRecorder::kGroundPhase);
        bool up = true; for (size_t i = 1; i < out.phases.size(); i++) if (out.phases[i] < out.phases[i - 1]) up = false;
        CHECK(up);
    }
    CHECK(!r.TakeFinished(out, id));
}

TEST(p5b_recorder_kickflip_first_tick_with_kickflip_starts) {
    FigureRecorder r; r.Reset(kRecBones);
    RunKF(r, 7, 0.8f, 3.0f, PhaseAt);
    FigureRec out; int id = 0;
    CHECK(r.TakeFinished(out, id));
}

TEST(p6_recorder_trick_not_wanted) {
    FigureRecorder r; r.Reset(kRecBones);
    RunKF(r, 1, 0.8f, 3.0f, PhaseAt, false);
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    RunKF(r, 2, 0.8f, 3.0f, PhaseAt, true, 9);
    CHECK(r.TakeFinished(out, id) && id == 9);
}

TEST(p5b_recorder_kickflip_caps) {
    FigureRecorder r; r.Reset(kRecBones);
    RunKF(r, 1, 5.f, 3.f, [](float) { return 9; });   // phase out of range, trick held 3 s
    FigureRec out; int id = 0;
    CHECK(r.TakeFinished(out, id));
    CHECK((int)out.times.size() == FigureRecorder::kKickMax && out.phases.size() == out.times.size());
    if (!out.phases.empty()) CHECK(out.phases[0] == 4 && out.phases.back() == 4);   // clamped to 0..4
}

TEST(p5b_recorder_short_kickflip_discarded) {
    FigureRecorder r; r.Reset(kRecBones);
    RunKF(r, 1, 0.4f, 1.f, PhaseAt);   // 0.4 s: ~13 frames
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    RunKF(r, 2, 0.8f, 3.0f, PhaseAt);
    CHECK(r.TakeFinished(out, id));
}

TEST(p5b_recorder_new_pop_mid_recording) {
    FigureRecorder r; r.Reset(kRecBones);
    std::vector<Quat> q(kRecBones, QIdent);
    auto tick = [&](uint32_t pop, int n) {
        for (int i = 0; i < n; i++) {
            GameState g{false, false, false, false, 0.f}; g.kickflip = true; g.popSerial = pop; g.trickPhase = 1;
            g.figId = 5; g.figWanted = true;
            r.Tick(g, q.data(), nullptr, kRecBones, 1.f / 60.f);
        }
    };
    FigureRec out; int id = 0;
    tick(1, 20);            // ~10 frames
    tick(2, 2);             // new pop: the first one is dropped (< 16), the new trick starts at once (P6)
    CHECK(!r.TakeFinished(out, id));
    tick(2, 38);            // ~20 frames of pop 2
    tick(3, 1);             // new pop: finished (>= 16)
    CHECK(r.TakeFinished(out, id));
    CHECK(out.times.size() >= 19 && out.times.size() <= 21);
}

TEST(p6_recorder_trick_from_manual) {   // a pop during a manual ends the manual recording and records the trick
    FigureRecorder r; r.Reset(kRecBones);
    std::vector<Quat> q(kRecBones, QIdent);
    for (int i = 0; i < 60 * 4; i++) {
        GameState g{i < 30, false, false, false, 0.f};
        g.kickflip = i >= 30 && i < 90; g.popSerial = i >= 30 ? 1 : 0; g.trickPhase = 1;
        g.figId = g.kickflip ? 5 : g.manual ? 1 : 0; g.figWanted = g.figId != 0;
        r.Tick(g, q.data(), nullptr, kRecBones, 1.f / 60.f);
    }
    FigureRec out; int id = 0;
    CHECK(r.TakeFinished(out, id) && id == 5 && !out.phases.empty());
}

// ------------------------------------------------------------------ the player (trigger, pop clock, leg weight)
namespace {

float KAng(Quat a, Quat b) {   // angle between two rotations (degrees)
    const Quat d = QMul(QInv(QNorm(a)), QNorm(b));
    double s = std::sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    if (s > 1.0) s = 1.0;
    double ang = 2.0 * std::asin(s) * 57.29577951308232;
    if (d.w < 0) ang = 360.0 - ang;
    if (ang > 180.0) ang = 360.0 - ang;
    return (float)ang;
}

struct KRig { int n = 0; std::vector<int> parent; std::vector<FTr> ref; };

const KRig& KR() {   // reference skeleton, non-trivial and fixed game pose
    static KRig r = [] {
        KRig g;
        const auto& s = RefSkeleton();
        g.n = (int)s.size();
        g.parent.resize(g.n);
        std::vector<Quat> lq(g.n);
        std::vector<float> lp(g.n * 3);
        for (int i = 0; i < g.n; i++) {
            g.parent[i] = s[i].parent;
            lq[i] = QFromRotatorDeg((float)(i % 7) * 3.f, (float)(i % 5) * 5.f, (float)(i % 3) * 2.f);
            lp[i * 3] = s[i].x; lp[i * 3 + 1] = s[i].y; lp[i * 3 + 2] = s[i].z;
        }
        g.ref.assign(g.n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
        std::vector<char> dirty(g.n, 1);
        ToComponent(g.ref.data(), g.parent.data(), g.n, lq.data(), (const float (*)[3])lp.data(), (const bool*)dirty.data());
        return g;
    }();
    return r;
}

Quat KLocal(const std::vector<FTr>& cs, int bone) {
    const KRig& g = KR();
    std::vector<Quat> lq(g.n);
    std::vector<float> lp(g.n * 3);
    ToLocal(cs.data(), g.parent.data(), g.n, lq.data(), (float (*)[3])lp.data());
    return lq[bone];
}

constexpr float kKDt = 1.f / 60.f;
constexpr int kRThigh = 3, kLThigh = 7, kLUp = 15, kLHand = 17, kRUp = 37;

Track Ramp(int bone, float deg) {   // from 0 to deg (roll) between 0 and 1 s, linear
    Track t; t.bone = RefNames()[bone]; t.index = bone;
    t.keys.push_back(Key{0.f, QIdent, false});
    t.keys.push_back(Key{1.f, QFromRotatorDeg(0, 0, deg), false});
    return t;
}

Clip Trick(float in, float out) {
    Clip c; c.file = "kf"; c.name = "kf"; c.states = ST_Kickflip; c.priority = 3;
    c.mode = Mode::Additive; c.loop = false; c.duration = 1.f; c.blendIn = in; c.blendOut = out;
    return c;
}

std::unique_ptr<Player> KPlayer() {
    auto p = std::make_unique<Player>();
    p->SetRig(KR().parent.data(), KR().n, RefMirror());
    std::vector<bool> leg(KR().n);
    const auto names = RefNames();
    for (int i = 0; i < KR().n; i++) leg[i] = IsLegBone(names[i]);
    p->SetLegBones(leg);
    return p;
}

GameState KF(bool kf, uint32_t pop, int phase, bool goofy = false, bool sw = false) {
    GameState g{false, false, goofy, sw, 0.5f};
    g.kickflip = kf; g.popSerial = pop; g.trickPhase = phase;
    return g;
}

struct KRun {
    Player& p; const ClipSet& set;
    std::vector<FTr> cs, prev;
    float worst = 0.f;   // biggest step of the left hand (component space) from one frame to the next
    int Step(const GameState& g) {
        cs = KR().ref;
        const int r = p.Apply(cs.data(), KR().n, &set, g, kKDt);
        if (!prev.empty()) { const float a = KAng(TrQ(cs[kLHand]), TrQ(prev[kLHand])); if (a > worst) worst = a; }
        prev = cs;
        return r;
    }
    float Off(int bone) const { return KAng(KLocal(cs, bone), KLocal(KR().ref, bone)); }   // gap to the game pose
};

} // namespace

TEST(p5b_trick_clip_plays_from_pop) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLUp, 60.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    int changed = 0;
    for (int f = 0; f < 20; f++) changed += r.Step(KF(false, 0, 0));
    CHECK(changed == 0);
    CHECK(std::string(p->CurrentClipName()).empty());
    CHECK(r.Step(KF(true, 1, 0)) > 0);   // pop frame (still on the ground)
    CHECK(std::string(p->CurrentClipName()) == "kf");
    for (int f = 1; f <= 30; f++) r.Step(KF(true, 1, 1));
    CHECK_NEAR(r.Off(kLUp), 30.0, 0.3);   // clock = 0.5 s at 0.5 s after the pop
    for (int f = 31; f <= 60; f++) r.Step(KF(true, 1, 2));
    CHECK_NEAR(r.Off(kLUp), 60.0, 0.3);   // 1.0 s: does not loop, on the last key
    for (int f = 61; f <= 90; f++) r.Step(KF(true, 1, 2));
    CHECK_NEAR(r.Off(kLUp), 60.0, 0.3);   // 1,5 s : lot 5, the last pose is held until the ground (no mid-air fade)
    for (int f = 0; f < 30; f++) r.Step(KF(false, 1, 0));   // ground: fades out
    CHECK(r.Off(kLUp) < 0.01f);
}

// the clip fades out at its end if the trick lasts longer, without starting from 0 before the next pop
TEST(p5b_trick_clip_fades_out_at_clip_end) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.duration = 0.5f; c.tracks.push_back(Ramp(kLUp, 40.f));   // 20 degrees at 0.5 s
    set.clips.push_back(c);
    KRun r{*p, set};
    CHECK(r.Step(KF(true, 1, 0)) > 0);
    // lot 5: the clip ends at 0.5 s, the trick lasts 1.0 s: the last pose (20 deg) is held until the ground
    bool held = true;
    for (int f = 1; f <= 60; f++) {   // 1.0 s trick
        r.Step(KF(true, 1, f < 30 ? 1 : 2));
        if (f > 32 && std::fabs(r.Off(kLUp) - 20.f) > 0.3f) held = false;
    }
    CHECK(held);
    CHECK(std::string(p->CurrentClipName()) == "kf");
    int firstZero = -1;
    for (int f = 1; f <= 30; f++) if (r.Step(KF(false, 1, 0)) == 0 && firstZero < 0) firstZero = f;   // ground
    CHECK(firstZero > 0 && firstZero <= 12);      // blend out 0.15 s + margin
    CHECK(std::string(p->CurrentClipName()).empty());
    CHECK(r.Step(KF(true, 2, 0)) > 0);            // the next pop replays from 0
    CHECK(std::string(p->CurrentClipName()) == "kf");
    for (int f = 1; f <= 15; f++) r.Step(KF(true, 2, 1));
    CHECK_NEAR(r.Off(kLUp), 10.0, 0.3);           // horloge 0,25 s
}

// during a kickflip, the Kickflip clip comes before a Manual clip (even at the pop from a manual)
TEST(p5b_kickflip_clip_preferred_over_manual) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip m; m.file = "a_man"; m.name = "a_man"; m.states = ST_Manual; m.priority = 5; m.mode = Mode::Additive; m.loop = true;
    m.duration = 1.f; m.blendIn = 0.05f; m.blendOut = 0.15f; m.tracks.push_back(Ramp(kLHand, 20.f));
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLUp, 40.f));
    set.clips.push_back(m); set.clips.push_back(c);   // file order: the manual first
    KRun r{*p, set};
    for (int f = 0; f < 10; f++) { GameState g = KF(false, 0, 0); g.manual = true; r.Step(g); }
    CHECK(std::string(p->CurrentClipName()) == "a_man");
    GameState g = KF(true, 1, 0); g.manual = true;
    r.Step(g);
    CHECK(std::string(p->CurrentClipName()) == "kf");
    set.clips[1].priority = 5; set.generation = 2;   // equal priority
    for (int f = 0; f < 5; f++) { GameState h = KF(true, 1, 1); h.manual = true; r.Step(h); }
    CHECK(std::string(p->CurrentClipName()) == "kf");
}

TEST(p5b_new_pop_restarts_without_jump) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.1f, 0.15f); c.tracks.push_back(Ramp(kLUp, 15.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    for (int f = 0; f < 24; f++) r.Step(KF(true, 1, f ? 1 : 0));
    CHECK(r.Off(kLUp) > 5.f);   // 0,4 s : ~5,75°
    for (int f = 0; f <= 30; f++) r.Step(KF(true, 2, f ? 1 : 0));   // new pop at 0.4 s
    CHECK(r.worst < 3.f);
    CHECK_NEAR(r.Off(kLUp), 7.5, 0.3);   // the clock started again from 0
    CHECK(p->LegWeight() == 1.f);
    CHECK(p->LayerCount() <= 4);
}

TEST(p5b_new_pop_replace_clip_restarts_without_jump) {   // real settings of a Kickflip clip: replace, 0.05 / 0.15
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.mode = Mode::Replace;
    const Quat g = KLocal(KR().ref, kLUp);
    Track t; t.bone = RefNames()[kLUp]; t.index = kLUp;
    t.keys.push_back(Key{0.f, g, false});
    t.keys.push_back(Key{1.f, QMul(g, QFromRotatorDeg(0, 0, 32.f)), false});   // ~12.8 degrees of gap at 0.4 s
    c.tracks.push_back(t);
    set.clips.push_back(c);
    KRun r{*p, set};
    for (int f = 0; f < 24; f++) r.Step(KF(true, 1, f ? 1 : 0));
    CHECK(r.Off(kLUp) > 12.f);
    r.worst = 0.f;
    for (int f = 0; f <= 30; f++) r.Step(KF(true, 2, f ? 1 : 0));
    CHECK(r.worst < 3.f);
    CHECK_NEAR(r.Off(kLUp), 16.0, 0.3);   // clock restarted from 0: 0.5 s
}

TEST(p5b_legs_fade_after_catch) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLThigh, 40.f)); c.tracks.push_back(Ramp(kLUp, 40.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    r.Step(KF(true, 1, 0));
    for (int f = 1; f < 30; f++) r.Step(KF(true, 1, f < 20 ? 1 : 2));
    CHECK(r.Off(kLThigh) > 15.f);   // before the catch: the thigh follows the clip
    CHECK(p->LegWeight() == 1.f);
    for (int f = 0; f < 9; f++) r.Step(KF(true, 1, 3));   // 0.15 s after entering phase 3
    CHECK(r.Off(kLThigh) < 0.1f);
    CHECK(r.Off(kLUp) > 20.f);   // the arm stays applied
    CHECK(p->LegWeight() == 0.f);
}

TEST(p5b_leg_weight_resets_on_next_pop) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLThigh, 40.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    r.Step(KF(true, 1, 0));
    for (int f = 0; f < 20; f++) r.Step(KF(true, 1, 99));   // phase out of bounds: counts as "catch or later"
    CHECK(p->LegWeight() == 0.f);
    for (int f = 0; f < 20; f++) r.Step(KF(false, 1, 0));   // back to the ground
    CHECK(p->LegWeight() == 0.f);   // only goes back up at the next pop
    r.Step(KF(true, 2, 0));
    CHECK(p->LegWeight() == 1.f);
    for (int f = 1; f <= 30; f++) r.Step(KF(true, 2, 1));
    CHECK_NEAR(r.Off(kLThigh), 20.0, 0.3);
}

TEST(p5b_trick_clip_ignores_other_states) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLUp, 40.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    int changed = 0;
    for (int f = 0; f < 40; f++) {
        GameState g = KF(false, (uint32_t)(f / 10), 1);   // the pop number changes, but Kickflip is not raised
        g.manual = true;
        changed += r.Step(g);
    }
    CHECK(changed == 0);
    CHECK(std::string(p->CurrentClipName()).empty());
}

TEST(p5b_failed_trick_fades_out) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLUp, 30.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    r.Step(KF(true, 1, 0));
    for (int f = 1; f < 20; f++) r.Step(KF(true, 1, f < 10 ? 1 : 2));
    CHECK(r.Off(kLUp) > 8.f);
    r.worst = 0.f;
    int changed = -1, frames = 0;
    for (int f = 0; f < 30 && changed != 0; f++, frames++) changed = r.Step(KF(false, 1, 2));   // the bit drops in the middle of phase 2
    CHECK(changed == 0);
    CHECK(frames >= 8 && frames <= 12);   // ~0,15 s
    CHECK(r.worst < 3.f);
}

TEST(p5b_switch_kickflip_mirrors) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c = Trick(0.05f, 0.15f); c.tracks.push_back(Ramp(kLThigh, 40.f)); c.tracks.push_back(Ramp(kLUp, 40.f));
    set.clips.push_back(c);
    KRun r{*p, set};
    r.Step(KF(true, 1, 0, false, true));
    for (int f = 1; f < 30; f++) r.Step(KF(true, 1, 1, false, true));
    CHECK(std::memcmp(&r.cs[kLThigh], &KR().ref[kLThigh], sizeof(FTr)) == 0);
    CHECK(std::memcmp(&r.cs[kLUp], &KR().ref[kLUp], sizeof(FTr)) == 0);
    CHECK(r.Off(kRThigh) > 15.f);
    CHECK(r.Off(kRUp) > 15.f);
    for (int f = 0; f < 9; f++) r.Step(KF(true, 1, 3, false, true));
    CHECK(r.Off(kRThigh) < 0.1f);   // the mirrored leg also follows the weight rule
    CHECK(r.Off(kRUp) > 20.f);
}

TEST(p5b_manual_clip_keeps_leg_tracks) {
    auto p = KPlayer();
    ClipSet set; set.generation = 1;
    Clip c; c.file = "man"; c.name = "man"; c.states = ST_Manual; c.mode = Mode::Additive; c.loop = true;
    c.blendIn = 0.05f; c.blendOut = 0.15f;
    Track t; t.bone = RefNames()[kLThigh]; t.index = kLThigh; t.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, 20), true});
    c.tracks.push_back(t);
    set.clips.push_back(c);
    KRun r{*p, set};
    for (int f = 0; f < 40; f++) { GameState g = KF(false, 1, 3); g.manual = true; r.Step(g); }
    CHECK_NEAR(r.Off(kLThigh), 20.0, 0.05);   // looping clip: the leg applies fully, even in phase 3
}

// ------------------------------------------------------------------ the editor model
namespace {
int EIdx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }
float EDist(const FTr& a, const float p[3]) { const float x = a.px - p[0], y = a.py - p[1], z = a.pz - p[2]; return std::sqrt(x * x + y * y + z * z); }

// Kickflip move of 30 frames (30 fps): 0 at the pop, 1 airborne, 2 flip, 3 catch from frame 18 (0.6 s), 4 landing
FigureRec KFig(bool withPhases, const char* name = "Kickflip") {
    FigureRec r; r.figure = name; r.bones = (int)RefNames().size();
    for (int i = 0; i < 30; i++) {
        r.times.push_back(i / 30.f); r.ratios.push_back(0.f);
        for (int b = 0; b < r.bones; b++) r.frames.push_back(QFromRotatorDeg((float)(b % 5) * 4.f, (float)(b % 7) * 3.f, (float)(b % 3) * 5.f));
        if (withPhases) r.phases.push_back(i == 0 ? 0 : i < 10 ? 1 : i < 18 ? 2 : i < 24 ? 3 : 4);
    }
    return r;
}

EditorDoc KDoc(const FigureRec* fig) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkeletonToJson(s), err));
    if (fig) CHECK(d.LoadFigure(FigureToJson(*fig), err));
    d.NewClip(FIG_Kickflip);
    d.SetLockEnds(false);     // written before the locked start and end (2026-11-26)
    return d;
}
} // namespace

TEST(p5b_editor_legs_editable_only_in_trick_clip) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    const int th = EIdx("AMXX_L_Thigh"), ca = EIdx("AMXX_R_Calf"), fo = EIdx("AMXX_L_Foot"), toe = EIdx("AMXX_R_Toe0");
    const int up = EIdx("AMXX_L_Calf_IK_UPNODE"), hand = EIdx("AMXX_L_Hand");
    CHECK(d.IsTrickClip());
    CHECK(d.IsEditable(th) && d.IsEditable(ca) && d.IsEditable(fo) && d.IsEditable(toe) && d.IsEditable(hand));
    CHECK(!d.IsEditable(up) && !d.IsEditable(EIdx("SKXX_SkateSkel_Foot_Anchor_L")));
    CHECK(!IsEditableBone("AMXX_R_Calf"));   // the upper body rule does not change
    CHECK(d.SetPending(th, QFromRotatorDeg(0, 0, 10)));
    d.CommitKey(0.2f);
    CHECK(d.TrackOf(th) != nullptr && !d.HasLockedTracks());
    // the Kickflip state is removed: the legs lock again, their track stays
    d.clip().states = ST_Manual;
    CHECK(!d.IsTrickClip() && !d.IsEditable(th) && d.IsEditable(hand));
    CHECK(!d.SetPending(th, QIdent));
    CHECK(d.HasLockedTracks() && d.TrackOf(th) != nullptr);
    d.DeleteKeysAt(0.2f, -1);
    CHECK(d.TrackOf(th) != nullptr);   // "all" does not touch a locked track
    // looping clip: legs locked
    d.NewClip(FIG_Manual);
    CHECK(!d.IsTrickClip() && !d.IsEditable(th) && !d.SetPending(th, QIdent));
    // trick clip: "all" also deletes the legs
    d.NewClip(FIG_Kickflip);
    CHECK(d.SetPending(th, QFromRotatorDeg(0, 0, 10)));
    d.CommitKey(0.2f);
    d.DeleteKeysAt(0.2f, -1);
    CHECK(d.TrackOf(th) == nullptr);
}

TEST(p5b_editor_new_kickflip_clip_defaults) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    const Clip& c = d.clip();
    CHECK(c.states == ST_Kickflip && c.mode == Mode::Additive && !c.loop);
    CHECK_NEAR(c.duration, 29.f / 30.f, 1e-5);
    CHECK_NEAR(c.blendIn, 0.05, 1e-6);
    CHECK_NEAR(c.blendOut, 0.15, 1e-6);
    CHECK(d.FigureIndex() == FIG_Kickflip);
    EditorDoc e = KDoc(nullptr);   // no move: 1.0 s
    CHECK_NEAR(e.clip().duration, 1.0, 1e-6);
    CHECK(!e.clip().loop && e.clip().states == ST_Kickflip);
    const FigureRec m = KFig(false, "Manual");   // the loaded move is not a kickflip: 1.0 s
    EditorDoc g = KDoc(&m);
    CHECK_NEAR(g.clip().duration, 1.0, 1e-6);
    g.NewClip(FIG_Manual);   // looping moves keep their settings
    CHECK(g.clip().loop && g.clip().duration == 3.0f && g.clip().states == ST_Manual);
}

TEST(p5b_editor_leg_weight_follows_recorded_catch) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    CHECK(d.Phases().size() == 30 && d.Phases()[0] == 0);
    CHECK_NEAR(d.PhaseStart(0), 0.0, 1e-6);
    CHECK_NEAR(d.PhaseStart(1), 1.f / 30.f, 1e-6);
    CHECK_NEAR(d.PhaseStart(3), 0.6, 1e-5);
    CHECK_NEAR(d.PhaseStart(4), 0.8, 1e-5);
    CHECK(d.PhaseStart(5) == -1.f && d.PhaseStart(-1) == -1.f);
    CHECK(d.LegWeightAt(0.f) == 1.f && d.LegWeightAt(0.6f) == 1.f);
    CHECK_NEAR(d.LegWeightAt(0.66f), 0.5, 1e-4);
    CHECK(d.LegWeightAt(0.72f) == 0.f && d.LegWeightAt(0.95f) == 0.f);
    // the preview: the leg follows the player rule, the arm does not
    const int th = EIdx("AMXX_L_Thigh"), ua = EIdx("AMXX_L_UpperArm");
    const Quat kt = QFromRotatorDeg(0, 0, 40), ka = QFromRotatorDeg(0, 0, 30);
    CHECK(d.SetPending(th, kt) && d.SetPending(ua, ka));
    CHECK(QDot(d.Edited(th, 0.66f), kt) > 0.99999f);   // pending: the requested value
    d.CommitKey(0.f);
    CHECK(d.SetPending(th, kt) && d.SetPending(ua, ka));
    d.CommitKey(0.9f);   // constant keys from 0 to 0.9 s
    CHECK(KAng(d.Edited(th, 0.3f), kt) < 0.01f);
    const Quat half = QSlerp(d.Origin(th, 0.66f), kt, EaseSine(0.5f));
    CHECK(KAng(d.Edited(th, 0.66f), half) < 0.01f);
    CHECK(KAng(d.Edited(th, 0.8f), d.Origin(th, 0.8f)) < 0.01f);
    CHECK(KAng(d.Edited(ua, 0.8f), ka) < 0.01f);
    // without a recorded phase 3, the landing (4) counts as the catch
    FigureRec g = KFig(true);
    for (int& p : g.phases) if (p == 3) p = 2;
    EditorDoc e = KDoc(&g);
    CHECK(e.PhaseStart(3) == -1.f);
    CHECK(e.LegWeightAt(0.79f) == 1.f && e.LegWeightAt(0.92f) == 0.f);
    // a looping clip: the legs are never weighted (locked, tracks applied as they are)
    std::string err;
    CHECK(e.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1},\"tracks\":{\"AMXX_L_Thigh\":[{\"t\":0,\"q\":[0,0,0.2,1]}]}}", "z.sanim", err));
    CHECK(KAng(e.Edited(th, 0.95f), e.Origin(th, 0.95f)) > 20.f);   // ~22.6 degrees: no fade after the catch
}

TEST(p5b_editor_figure_without_phases) {   // Kickflip move without phases (old file)
    const FigureRec f = KFig(false);
    EditorDoc d = KDoc(&f);
    CHECK(d.Phases().empty());
    for (int p = 0; p <= 4; p++) CHECK(d.PhaseStart(p) == -1.f);
    CHECK(d.LegWeightAt(0.f) == 1.f && d.LegWeightAt(0.7f) == 1.f && d.LegWeightAt(5.f) == 1.f);
    const int th = EIdx("AMXX_L_Thigh");
    const Quat kt = QFromRotatorDeg(0, 0, 40);
    CHECK(d.IsEditable(th) && d.SetPending(th, kt));
    d.CommitKey(0.5f);
    CHECK(KAng(d.Edited(th, 0.9f), kt) < 0.01f);   // weight 1 all along
    // phase values out of bounds: clamped to 0..4 (like the recorder)
    FigureRec g = KFig(true);
    for (int i = 20; i < 30; i++) g.phases[i] = 9;
    EditorDoc e = KDoc(&g);
    CHECK_NEAR(e.PhaseStart(3), 0.6, 1e-5);
    CHECK_NEAR(e.PhaseStart(4), 20.f / 30.f, 1e-5);
}

TEST(p5b_editor_leg_ik_chain) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    const int th = EIdx("AMXX_L_Thigh"), ca = EIdx("AMXX_L_Calf"), fo = EIdx("AMXX_L_Foot"), toe = EIdx("AMXX_L_Toe0");
    int u = -1, m = -1, h = -1;
    CHECK(d.IkChain(fo, u, m, h) && u == th && m == ca && h == fo);
    u = m = h = -1;
    CHECK(d.IkChain(th, u, m, h) && h == fo);
    CHECK(d.IkChain(toe, u, m, h) && h == fo);
    CHECK(d.IkChain(EIdx("AMXX_R_Calf"), u, m, h) && h == EIdx("AMXX_R_Foot") && u == EIdx("AMXX_R_Thigh"));
    CHECK(!d.IkChain(EIdx("AMXX_L_Calf_IK_UPNODE"), u, m, h));
    CHECK(d.IkChain(EIdx("AMXX_L_Hand"), u, m, h) && h == EIdx("AMXX_L_Hand"));   // the arms do not change
    // the foot goes to the target
    std::vector<FTr> before;
    d.ComponentPose(0.3f, before, false);
    float target[3] = {before[fo].px + 0.3f * (before[th].px - before[fo].px), before[fo].py + 0.3f * (before[th].py - before[fo].py) + 5.f,
                       before[fo].pz + 0.3f * (before[th].pz - before[fo].pz)};
    d.IkBegin(fo, 0.3f);
    CHECK(d.IkMoveHand(fo, target, 0.f, true, 0.3f));
    d.IkEnd();
    std::vector<FTr> after;
    d.ComponentPose(0.3f, after, false);
    CHECK(EDist(after[fo], target) < 0.05f);
    CHECK(KAng(TrQ(after[fo]), TrQ(before[fo])) < 0.05f);
    // looping clip: no leg chain
    d.NewClip(FIG_Manual);
    CHECK(!d.IkChain(fo, u, m, h));
    CHECK(!d.IkMoveHand(fo, target, 0.f, true, 0.3f));
}

// a trick clip never loops in the preview, even with loop = true
TEST(p5b_editor_trick_clip_never_loops_in_preview) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    const int ua = EIdx("AMXX_L_UpperArm");
    const Quat a = QFromRotatorDeg(0, 0, 10), b = QFromRotatorDeg(0, 0, 50);
    CHECK(d.SetPending(ua, a)); d.CommitKey(0.f);
    CHECK(d.SetPending(ua, b)); d.CommitKey(0.5f);
    d.clip().loop = true;
    CHECK(KAng(d.Edited(ua, 0.9f), b) < 0.01f);   // after the last key: the last key
    d.clip().states = ST_Manual;                  // the same clip, looping: goes back towards the 1st key
    CHECK(KAng(d.Edited(ua, 0.9f), b) > 5.f);
}

// picking a move sets an empty clip like NewClip
TEST(p5b_editor_load_figure_applies_figure_defaults) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkeletonToJson(s), err));
    d.NewClip(FIG_NoseManual);   // as when the editor starts
    d.clip().name = "mon_clip";
    CHECK(d.LoadFigure(FigureToJson(KFig(true)), err));
    const Clip& c = d.clip();
    CHECK(c.states == ST_Kickflip && !c.loop && c.mode == Mode::Additive && c.name == "mon_clip");
    CHECK_NEAR(c.duration, 29.f / 30.f, 1e-5);
    CHECK_NEAR(c.blendIn, 0.05, 1e-6);
    CHECK_NEAR(c.blendOut, 0.15, 1e-6);
    CHECK(d.LoadFigure(FigureToJson(KFig(false, "Manual")), err));   // back to a looping move
    CHECK(c.states == ST_Manual && c.loop && c.duration == 3.0f);
    CHECK_NEAR(c.blendIn, 0.25, 1e-6);
    CHECK_NEAR(c.blendOut, 0.30, 1e-6);
    // clip with tracks: tracks, duration and fades kept, but a trick does not loop
    CHECK(d.SetPending(EIdx("AMXX_L_Hand"), QFromRotatorDeg(0, 0, 10)));
    d.CommitKey(0.5f);
    CHECK(d.LoadFigure(FigureToJson(KFig(true)), err));
    CHECK(c.states == ST_Kickflip && !c.loop && c.duration == 3.0f && c.tracks.size() == 1);
    CHECK_NEAR(c.blendIn, 0.25, 1e-6);
}

// move missing on disk = same clip settings as LoadFigure (trick: a single pass, 1.0 s)
TEST(p5b_editor_clear_figure_applies_figure_defaults) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkeletonToJson(s), err));
    d.NewClip(FIG_NoseManual);
    d.ClearFigure(FIG_Kickflip);
    const Clip& c = d.clip();
    CHECK(c.states == ST_Kickflip && !c.loop && c.duration == 1.0f && !d.HasFigure() && d.FigureIndex() == FIG_Kickflip);
    CHECK_NEAR(c.blendIn, 0.05, 1e-6);
    d.ClearFigure(FIG_Manual);
    CHECK(c.states == ST_Manual && c.loop && c.duration == 3.0f);
}

// a trick clip opens and saves without loop
TEST(p5b_editor_trick_clip_saved_without_loop) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    CHECK(d.SetPending(EIdx("AMXX_L_Hand"), QFromRotatorDeg(0, 0, 10)));
    d.CommitKey(0.5f);
    d.clip().loop = true;   // forced
    std::string err;
    const std::string text = d.SaveText(err);
    CHECK(!text.empty());
    Clip back;
    CHECK(ParseClip(text, "x.sanim", back, err) && !back.loop);
    EditorDoc e = KDoc(nullptr);
    CHECK(e.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Kickflip\"]},\"play\":{\"duration\":1,\"loop\":true},\"tracks\":{\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,1]}]}}", "z.sanim", err));
    CHECK(!e.clip().loop);
}

// mirroring a leg does not attenuate it twice
TEST(p5b_editor_mirror_copy_leg_unweighted) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    const int lt = EIdx("AMXX_L_Thigh"), rt = EIdx("AMXX_R_Thigh");
    const Quat kt = QFromRotatorDeg(0, 0, 40);
    CHECK(d.SetPending(lt, kt)); d.CommitKey(0.f);
    CHECK(d.SetPending(lt, kt)); d.CommitKey(0.9f);   // constant key
    d.MirrorCopy(lt, 0.3f);   // before the catch: weight 1
    Quat before, after;
    CHECK(d.Pending(rt, &before));
    d.DiscardPending();
    d.MirrorCopy(lt, 0.66f);  // weight 0.5: the copy must stay the whole key
    CHECK(d.Pending(rt, &after));
    CHECK(KAng(before, after) < 0.01f);
}

// a leg IK gesture after the catch starts from the unweighted pose; the key is not attenuated twice
TEST(p5b_editor_leg_ik_after_catch_uses_raw_base) {
    const FigureRec f = KFig(true);
    EditorDoc d = KDoc(&f);
    const int th = EIdx("AMXX_L_Thigh"), fo = EIdx("AMXX_L_Foot");
    const Quat kt = QMul(d.Origin(th, 0.f), QFromRotatorDeg(0, 0, 40));
    CHECK(d.SetPending(th, kt)); d.CommitKey(0.f);
    CHECK(d.SetPending(th, kt)); d.CommitKey(0.9f);   // constant key on the thigh
    const float t = 0.66f;
    CHECK_NEAR(d.LegWeightAt(t), 0.5, 1e-4);
    std::vector<FTr> raw, shown;
    d.ComponentPose(t, raw, false, true);
    d.ComponentPose(t, shown, false);
    CHECK(KAng(TrQ(raw[fo]), TrQ(shown[fo])) > 5.f);   // the preview is indeed attenuated at this time
    // null gesture: the target is the foot (unweighted); the key must stay the previous one
    const float same[3] = {raw[fo].px, raw[fo].py, raw[fo].pz};
    d.IkBegin(fo, t);
    CHECK(d.IkMoveHand(fo, same, 0.f, true, t));
    d.IkEnd();
    d.CommitKey(t);
    CHECK(KAng(d.EditedRaw(th, t), kt) < 0.05f);
    // real gesture: the foot reaches the target and keeps its unweighted orientation
    const float target[3] = {raw[fo].px + 0.3f * (raw[th].px - raw[fo].px), raw[fo].py + 0.3f * (raw[th].py - raw[fo].py) + 5.f,
                             raw[fo].pz + 0.3f * (raw[th].pz - raw[fo].pz)};
    d.IkBegin(fo, t);
    CHECK(d.IkMoveHand(fo, target, 0.f, true, t));
    d.IkEnd();
    d.CommitKey(t);
    std::vector<FTr> after;
    d.ComponentPose(t, after, false, true);
    CHECK(EDist(after[fo], target) < 0.05f);
    CHECK(KAng(TrQ(after[fo]), TrQ(raw[fo])) < 0.05f);
}

namespace {
FigureRec KFigMoving() {   // like KFig, but each frame turns a bit more (the origin depends on time)
    FigureRec f = KFig(true);
    for (int i = 0; i < 30; i++)
        for (int b = 0; b < f.bones; b++) f.frames[(size_t)i * f.bones + b] = QMul(f.frames[(size_t)i * f.bones + b], QFromRotatorDeg(0, 0, (float)i * 2.f));
    return f;
}
}

// a trick clip is driven by time (like the player), even when set to ManualRatio
TEST(p5b_editor_trick_clip_is_time_driven) {
    const FigureRec f = KFigMoving();
    EditorDoc d = KDoc(&f);
    const int ua = EIdx("AMXX_L_UpperArm");
    d.clip().drive = Drive::ManualRatio; d.clip().r0 = 0.f; d.clip().r1 = 1.f;
    CHECK(d.EffectiveDrive() == Drive::Time);
    CHECK(KAng(d.Edited(ua, 0.5f), d.Origin(ua, 0.5f)) < 0.01f);   // origin at the time, not at the simulated ratio
    CHECK(d.SetPending(EIdx("AMXX_L_Hand"), QFromRotatorDeg(0, 0, 10)));
    d.CommitKey(0.5f);
    std::string err;
    const std::string text = d.SaveText(err);
    Clip back;
    CHECK(!text.empty() && ParseClip(text, "x.sanim", back, err) && back.drive == Drive::Time);
    EditorDoc e = KDoc(nullptr);
    CHECK(e.OpenClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"z\",\"trigger\":{\"states\":[\"Kickflip\"]},\"play\":{\"duration\":1,\"drive\":\"ManualRatio\",\"ratioRange\":[0,1]},\"tracks\":{\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,1]}]}}", "z.sanim", err));
    CHECK(e.clip().drive == Drive::Time);
    d.clip().states = ST_Manual;   // the same clip outside a trick: ManualRatio is still respected
    CHECK(d.EffectiveDrive() == Drive::ManualRatio);
}

// for a trick clip longer than the move, the origin stays on the last frame (no jump back to the start)
TEST(p5b_editor_trick_origin_clamps_past_figure) {
    const FigureRec f = KFigMoving();
    EditorDoc d = KDoc(&f);
    const int ua = EIdx("AMXX_L_UpperArm");
    const float end = 29.f / 30.f;
    CHECK(KAng(d.Origin(ua, 1.5f), d.Origin(ua, end)) < 0.01f);
    CHECK(KAng(d.Origin(ua, -0.2f), d.Origin(ua, 0.f)) < 0.01f);
    d.clip().states = ST_Manual;   // looping move: the origin still loops
    CHECK(KAng(d.Origin(ua, end + 0.5f), d.Origin(ua, 0.5f)) < 0.01f);
}
