#include "check.h"
#include "../src/player.h"
#include <cstring>
#include <memory>

namespace {

// angle between two rotations, precise near 0 (asin of the vector part, in double)
float AngDeg(Quat a, Quat b) {
    const Quat d = QMul(QInv(QNorm(a)), QNorm(b));
    double s = std::sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    if (s > 1.0) s = 1.0;
    double ang = 2.0 * std::asin(s) * 57.29577951308232;
    if (d.w < 0) ang = 360.0 - ang;
    if (ang > 180.0) ang = 360.0 - ang;
    return (float)ang;
}

struct Rig {
    int n = 0;
    std::vector<int> parent;
    std::vector<FTr> ref;   // game pose, fixed, in component space
};

// reference skeleton with a deterministic local rotation per bone (non-trivial pose)
const Rig& R() {
    static Rig r = [] {
        Rig g;
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

Quat GameLocal(int bone) {
    const Rig& g = R();
    std::vector<Quat> lq(g.n);
    std::vector<float> lp(g.n * 3);
    ToLocal(g.ref.data(), g.parent.data(), g.n, lq.data(), (float (*)[3])lp.data());
    return lq[bone];
}

Quat LocalOf(const std::vector<FTr>& cs, int bone) {
    const Rig& g = R();
    std::vector<Quat> lq(g.n);
    std::vector<float> lp(g.n * 3);
    ToLocal(cs.data(), g.parent.data(), g.n, lq.data(), (float (*)[3])lp.data());
    return lq[bone];
}

Clip MkClip(const char* file, uint32_t states, Mode mode, int bone, Quat key, float blend = 0.3f, int prio = 0) {
    Clip c;
    c.file = file; c.name = file; c.states = states; c.mode = mode; c.priority = prio;
    c.blendIn = blend; c.blendOut = blend; c.duration = 1.f; c.loop = true;
    Track t; t.bone = RefNames()[bone]; t.index = bone; t.keys.push_back(Key{0.f, key, true});
    c.tracks.push_back(t);
    return c;
}

std::unique_ptr<Player> NewPlayer() {
    auto p = std::make_unique<Player>();
    p->SetRig(R().parent.data(), R().n, RefMirror());
    return p;
}

GameState GS(bool manual, bool nose, bool goofy = false, bool sw = false) { return GameState{manual, nose, goofy, sw, 0.5f}; }

constexpr float kDt = 1.f / 60.f;
constexpr int kHand = 17, kLUpper = 15, kRUpper = 37;

} // namespace

TEST(player_nothing_to_play_leaves_pose_untouched) {
    auto p = NewPlayer();
    ClipSet set; set.generation = 1;
    set.clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 0, 20)));
    std::vector<FTr> cs = R().ref;
    CHECK(p->Apply(cs.data(), R().n, &set, GS(false, false), kDt) == 0);
    CHECK(std::memcmp(cs.data(), R().ref.data(), sizeof(FTr) * R().n) == 0);
    CHECK(p->Apply(cs.data(), R().n, nullptr, GS(true, false), kDt) == 0);
    CHECK(std::memcmp(cs.data(), R().ref.data(), sizeof(FTr) * R().n) == 0);
}

TEST(player_master_off_leaves_pose_untouched) {
    auto p = NewPlayer();
    ClipSet set; set.generation = 1;
    set.clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 0, 20)));
    p->SetMaster(false);
    std::vector<FTr> cs;
    int last = -1;
    for (int f = 0; f < 30; f++) { cs = R().ref; last = p->Apply(cs.data(), R().n, &set, GS(true, false), kDt); }
    CHECK(last == 0);
    CHECK(std::memcmp(cs.data(), R().ref.data(), sizeof(FTr) * R().n) == 0);
}

TEST(player_untracked_bones_are_byte_identical) {
    auto p = NewPlayer();
    ClipSet set; set.generation = 1;
    set.clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 0, 20)));
    std::vector<FTr> cs;
    int changed = 0;
    for (int f = 0; f < 30; f++) { cs = R().ref; changed = p->Apply(cs.data(), R().n, &set, GS(true, false), kDt); }
    CHECK(changed > 0);
    CHECK(std::memcmp(cs.data(), R().ref.data(), sizeof(FTr) * 15) == 0);   // bones 0 to 14, legs 3 to 10 included
    CHECK(std::memcmp(&cs[37], &R().ref[37], sizeof(FTr)) == 0);           // the other arm
    CHECK(AngDeg(TrQ(cs[kLUpper]), TrQ(R().ref[kLUpper])) > 15.f);
}

TEST(player_identity_additive_changes_nothing) {
    auto p = NewPlayer();
    ClipSet set; set.generation = 1;
    set.clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QIdent));
    std::vector<FTr> cs;
    for (int f = 0; f < 30; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &set, GS(true, false), kDt); }
    for (int i = 0; i < R().n; i++) {
        CHECK_NEAR(cs[i].px, R().ref[i].px, 1e-3); CHECK_NEAR(cs[i].py, R().ref[i].py, 1e-3); CHECK_NEAR(cs[i].pz, R().ref[i].pz, 1e-3);
        CHECK(AngDeg(TrQ(cs[i]), TrQ(R().ref[i])) < 1e-2f);
    }
}

TEST(player_replace_full_weight_reaches_key) {
    auto p = NewPlayer();
    const Quat key = QFromRotatorDeg(40, -10, 25);
    ClipSet set; set.generation = 1;
    set.clips.push_back(MkClip("a", ST_NoseManual, Mode::Replace, kLUpper, key));
    std::vector<FTr> cs;
    for (int f = 0; f < 40; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &set, GS(false, true), kDt); }
    CHECK(AngDeg(LocalOf(cs, kLUpper), key) < 0.01f);
    CHECK(std::strcmp(p->CurrentClipName(), "a") == 0);
}

static float MaxHandStep(Player& p, std::vector<ClipSet>& sets, int frames, GameState (*state)(int), int (*setFor)(int)) {
    std::vector<FTr> cs, prev;
    float worst = 0.f;
    for (int f = 0; f < frames; f++) {
        cs = R().ref;
        p.Apply(cs.data(), R().n, &sets[setFor(f)], state(f), kDt);
        if (f) { const float a = AngDeg(TrQ(cs[kHand]), TrQ(prev[kHand])); if (a > worst) worst = a; }
        prev = cs;
    }
    return worst;
}

static std::vector<ClipSet> TwoClips() {
    std::vector<ClipSet> v(2);
    v[0].generation = 1;
    v[0].clips.push_back(MkClip("a_add", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 0, 20)));
    v[0].clips.push_back(MkClip("b_rep", ST_NoseManual, Mode::Replace, kLUpper, QMul(GameLocal(kLUpper), QFromRotatorDeg(0, 20, 0))));
    v[1] = v[0];
    v[1].generation = 2;
    v[1].clips.push_back(MkClip("c_rep", ST_NoseManual, Mode::Replace, kLUpper, QMul(GameLocal(kLUpper), QFromRotatorDeg(20, 0, 0)), 0.3f, 9));
    return v;
}

TEST(player_no_jump_manual_nose_none) {
    auto p = NewPlayer();
    auto sets = TwoClips();
    const float worst = MaxHandStep(*p, sets, 180,
        [](int f) { return f < 60 ? GS(true, false) : f < 120 ? GS(false, true) : GS(false, false); },
        [](int) { return 0; });
    CHECK(worst < 3.f);
    CHECK(worst > 0.2f);   // something did happen
}

TEST(player_no_jump_with_back_and_forth_and_third_clip) {
    auto p = NewPlayer();
    auto sets = TwoClips();
    // back and forth manual / nose every 5 frames, then a 3rd clip (priority in nose) arrives in the middle of a fade
    const float worst = MaxHandStep(*p, sets, 200,
        [](int f) { return ((f / 5) % 2) ? GS(false, true) : GS(true, false); },
        [](int f) { return f < 63 ? 0 : 1; });
    CHECK(worst < 3.f);
    CHECK(p->LayerCount() <= 4);
}

TEST(player_bad_dt_is_treated_as_zero) {
    auto p = NewPlayer();
    auto sets = TwoClips();
    std::vector<FTr> cs;
    for (int f = 0; f < 10; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &sets[0], GS(true, false), kDt); }
    for (float bad : {5.f, -1.f, std::nanf("")}) {
        Player ref = *p;
        std::vector<FTr> a = R().ref, b = R().ref;
        p->Apply(a.data(), R().n, &sets[0], GS(true, false), bad);
        ref.Apply(b.data(), R().n, &sets[0], GS(true, false), 0.f);
        CHECK(std::memcmp(a.data(), b.data(), sizeof(FTr) * R().n) == 0);
    }
}

TEST(player_mirror_moves_other_arm_when_goofy) {
    {
        auto p = NewPlayer();
        ClipSet set; set.generation = 1;
        set.clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 0, 20)));
        std::vector<FTr> cs;
        for (int f = 0; f < 30; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &set, GS(true, false, true, false), kDt); }
        CHECK(std::memcmp(&cs[kLUpper], &R().ref[kLUpper], sizeof(FTr)) == 0);
        CHECK(AngDeg(TrQ(cs[kRUpper]), TrQ(R().ref[kRUpper])) > 15.f);
    }
    {
        auto p = NewPlayer();
        ClipSet set; set.generation = 1;
        set.clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 0, 20)));
        std::vector<FTr> cs;
        for (int f = 0; f < 30; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &set, GS(true, false, true, true), kDt); }   // goofy + switch: no mirror
        CHECK(std::memcmp(&cs[kRUpper], &R().ref[kRUpper], sizeof(FTr)) == 0);
        CHECK(AngDeg(TrQ(cs[kLUpper]), TrQ(R().ref[kLUpper])) > 15.f);
    }
}

TEST(player_removed_clip_freezes_and_fades_out) {
    auto p = NewPlayer();
    auto sets = TwoClips();
    ClipSet empty; empty.generation = 7;
    std::vector<FTr> cs, prev;
    for (int f = 0; f < 40; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &sets[0], GS(true, false), kDt); }
    prev = cs;
    float worst = 0.f;
    int changed = -1, frames = 0;
    for (int f = 0; f < 40 && changed != 0; f++, frames++) {
        cs = R().ref;
        changed = p->Apply(cs.data(), R().n, &empty, GS(true, false), kDt);
        const float a = AngDeg(TrQ(cs[kHand]), TrQ(prev[kHand]));
        if (a > worst) worst = a;
        prev = cs;
    }
    CHECK(worst < 3.f);
    CHECK(changed == 0);
    CHECK(frames >= 17 && frames <= 21);   // ~0.3 s at 60 fps
}

TEST(player_snapshot_is_game_pose_before_layer) {
    auto p = NewPlayer();
    auto sets = TwoClips();
    std::vector<FTr> cs;
    for (int f = 0; f < 30; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &sets[0], GS(true, false), kDt); }
    std::vector<Quat> snap(R().n);
    p->SnapshotGameLocals(snap.data(), R().n);
    CHECK(AngDeg(snap[kLUpper], GameLocal(kLUpper)) < 0.01f);
}

// ------------------------------------------------------------------ fixes
TEST(player_ratio_clip_does_not_wrap_at_top_of_range) {   // I1
    auto p = NewPlayer();
    Clip c = MkClip("r", ST_Manual, Mode::Additive, kLUpper, QIdent);
    c.drive = Drive::ManualRatio; c.loop = true;   // loop by default: must not apply to the ManualRatio drive
    c.tracks[0].keys.clear();
    c.tracks[0].keys.push_back(Key{0.f, QFromRotatorDeg(0, -30, 0), false});
    c.tracks[0].keys.push_back(Key{1.f, QFromRotatorDeg(0, 30, 0), false});
    ClipSet set; set.generation = 1; set.clips.push_back(c);
    std::vector<FTr> cs;
    for (float r : {0.5f, 0.9f, 0.999f, 1.0f, 1.2f}) {
        GameState gs = GS(true, false); gs.manualRatio = r;
        for (int f = 0; f < 40; f++) { cs = R().ref; p->Apply(cs.data(), R().n, &set, gs, kDt); }
        if (r >= 0.999f) CHECK(AngDeg(LocalOf(cs, kLUpper), QMul(GameLocal(kLUpper), QFromRotatorDeg(0, 30, 0))) < 0.2f);
    }
}

TEST(player_hot_reload_of_playing_clip_crossfades) {   // I2
    auto p = NewPlayer();
    std::vector<ClipSet> sets(2);
    sets[0].generation = 1;
    sets[0].clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 10, 0)));
    sets[0].clips[0].stamp = 1;
    sets[1].generation = 2;
    sets[1].clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 30, 0)));
    sets[1].clips[0].stamp = 2;
    const float worst = MaxHandStep(*p, sets, 120, [](int) { return GS(true, false); }, [](int f) { return f < 50 ? 0 : 1; });
    CHECK(worst < 3.f);
    std::vector<FTr> cs = R().ref;
    p->Apply(cs.data(), R().n, &sets[1], GS(true, false), kDt);
    CHECK(AngDeg(LocalOf(cs, kLUpper), QMul(GameLocal(kLUpper), QFromRotatorDeg(0, 30, 0))) < 0.1f);   // the new version took over
    CHECK(p->LayerCount() == 1);
}

TEST(player_stance_flip_during_clip_crossfades) {   // I2
    auto p = NewPlayer();
    std::vector<ClipSet> sets(1);
    sets[0].generation = 1;
    Clip c = MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(6, 6, 8));
    Track spine; spine.bone = "AMXX_Spine3"; spine.index = 13; spine.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, 6), true});
    c.tracks.push_back(spine);
    sets[0].clips.push_back(c);
    const float worst = MaxHandStep(*p, sets, 120, [](int f) { return GS(true, false, f >= 50, false); }, [](int) { return 0; });
    CHECK(worst < 3.f);
}

TEST(player_current_name_survives_freed_clipset) {   // I4
    auto p = NewPlayer();
    auto* set = new ClipSet();
    set->generation = 1;
    set->clips.push_back(MkClip("a", ST_Manual, Mode::Additive, kLUpper, QFromRotatorDeg(0, 20, 0)));
    std::vector<FTr> cs = R().ref;
    for (int f = 0; f < 5; f++) { cs = R().ref; p->Apply(cs.data(), R().n, set, GS(true, false), kDt); }
    set->clips[0].name = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";   // the clip memory changes then is freed
    delete set;
    CHECK(std::strcmp(p->CurrentClipName(), "a") == 0);
}
