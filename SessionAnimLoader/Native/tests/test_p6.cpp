// P6: every flip trick, per stance and feet; figure variants; leg post-pass after the other mods.
#include "check.h"
#include "../src/player.h"
#include "../src/trick.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <cmath>
#include <memory>

TEST(p6_trick_names) {
    std::string b; int s = -1;
    CHECK(SplitTrickName("RGS_KickFlip", b, s) && b == "KickFlip" && s == STANCE_Normal);
    CHECK(SplitTrickName("TRICK_FKS_heelflip", b, s) && b == "heelflip" && s == STANCE_Fakie);
    CHECK(SplitTrickName("NLS_Late_RF_NollieSouth", b, s) && b == "Late_RF_NollieSouth" && s == STANCE_Nollie);
    CHECK(SplitTrickName("SWS_BS_ShoveIt", b, s) && s == STANCE_Switch);
    CHECK(!SplitTrickName("Kickflip", b, s) && !SplitTrickName("XYZ_Kickflip", b, s) && !SplitTrickName("RGS_", b, s));
    CHECK(TrickHash("KickFlip") == TrickHash("kickflip") && TrickHash("KickFlip") != 0 && TrickHash("") == 0);
    CHECK(TrickHash("KickFlip") != TrickHash("HeelFlip"));
    CHECK(TrickCatalog().size() > 40);
    for (auto& t : TrickCatalog()) CHECK(SplitTrickName(t.full, b, s));
}

TEST(p6_figure_keys) {
    FigureVariant v; v.state = ST_Trick; v.trick = "KickFlip"; v.stance = STANCE_Switch; v.goofy = true;
    CHECK(FigureKey(v) == "KickFlip_SWS_goofy");
    FigureVariant w;
    CHECK(ParseFigureKey("KickFlip_SWS_goofy", w) && w == v);
    CHECK(ParseFigureKey("BS_ShoveIt_360_FKS_regular", w) && w.trick == "BS_ShoveIt_360" && w.stance == STANCE_Fakie && !w.goofy);
    CHECK(ParseFigureKey("Manual_RGS_regular", w) && w.state == ST_Manual && w.trick.empty());
    CHECK(ParseFigureKey("Grind_SWS_regular", w) && w.state == ST_Grind && w.stance == STANCE_Switch);
    CHECK(!ParseFigureKey("Manual", w) && !ParseFigureKey("Manual_XXX_regular", w) && !ParseFigureKey("Manual_RGS_left", w));
    FigureVariant m; m.state = ST_NoseManual;
    CHECK(FigureKey(m) == "NoseManual_RGS_regular");
}

namespace {
Clip Parse(const char* trig) {
    std::string txt = std::string(R"({"format":"sanim","version":1,"name":"c","trigger":)") + trig +
                      R"(,"play":{"duration":1,"loop":false},"mode":"replace","tracks":{"AMXX_L_Calf":[{"t":0,"q":[0,0,0,1]}]}})";
    Clip c; std::string err;
    if (!ParseClip(txt, "c.sanim", c, err)) c.name = "REFUSED:" + err;
    return c;
}
}

TEST(p6_clip_trigger_fields) {
    Clip c = Parse(R"({"states":["Trick"],"trick":"HeelFlip","stances":["switch","fakie"],"feet":"goofy"})");
    CHECK(c.name == "c" && c.states == ST_Trick && c.trick == "HeelFlip" && c.trickHash == TrickHash("heelflip"));
    CHECK(c.stances == ((1 << STANCE_Switch) | (1 << STANCE_Fakie)) && c.feet == Feet::Goofy);
    Clip d; std::string err;
    CHECK(ParseClip(ClipToJson(c), "c.sanim", d, err) && d.trick == "HeelFlip" && d.stances == c.stances && d.feet == Feet::Goofy);
    Clip k = Parse(R"({"states":["Kickflip"]})");   // v0.7 clip
    CHECK(k.states == ST_Trick && k.trick == "KickFlip" && k.stances == 3 && k.feet == Feet::Any);
    Clip any = Parse(R"({"states":["Trick"]})");
    CHECK(any.trickHash == 0 && any.stances == kAllStances);
    CHECK(Parse(R"({"states":["Manual"],"trick":"KickFlip"})").name.rfind("REFUSED", 0) == 0);
    CHECK(Parse(R"({"states":["Trick"],"stances":["sideways"]})").name.rfind("REFUSED", 0) == 0);
    CHECK(Parse(R"({"states":["Trick"],"feet":"left"})").name.rfind("REFUSED", 0) == 0);
    CHECK(Parse(R"({"states":["Grind","Powerslide","Grab"]})").states == (ST_Grind | ST_Powerslide | ST_Grab));
}

TEST(p6_author_mirrored) {
    Clip c; c.stances = kAllStances; c.feet = Feet::Any;
    CHECK(!AuthorMirrored(c));
    c.feet = Feet::Goofy; CHECK(AuthorMirrored(c));
    c.feet = Feet::Regular; c.stances = 1 << STANCE_Switch; CHECK(AuthorMirrored(c));
    c.feet = Feet::Goofy; CHECK(!AuthorMirrored(c));
    c.feet = Feet::Regular; c.stances = 1 << STANCE_Fakie; CHECK(!AuthorMirrored(c));
}

// ------------------------------------------------------------------ player on a small leg rig
namespace {
// 0 root, 1 pelvis, 2 L_Thigh, 3 L_Calf, 4 L_Foot, 5 L_UpperArm
constexpr int kN = 6;
const int kPar[kN] = {-1, 0, 1, 2, 3, 1};
std::vector<std::string> Names() { return {"Root", "AMXX_Pelvis", "AMXX_L_Thigh", "AMXX_L_Calf", "AMXX_L_Foot", "AMXX_L_UpperArm"}; }

void Pose(FTr* cs) {   // straight leg down the Z axis, arm sideways
    const float p[kN][3] = {{0, 0, 0}, {0, 0, 90}, {0, 10, 85}, {0, 10, 45}, {0, 10, 5}, {0, -20, 140}};
    for (int i = 0; i < kN; i++) cs[i] = FTr{0, 0, 0, 1, p[i][0], p[i][1], p[i][2], 0, 1, 1, 1, 0};
}

std::unique_ptr<Player> MakePlayer() {
    auto pl = std::make_unique<Player>();
    const auto names = Names();
    std::vector<int> par(kPar, kPar + kN);
    pl->SetRig(kPar, kN, BuildMirrorMap(names, par, {}));
    std::vector<bool> leg(kN);
    for (int i = 0; i < kN; i++) leg[i] = IsLegBone(names[i]);
    pl->SetLegBones(leg);
    return pl;
}

Clip TrickClip(const char* file, const char* trick, uint8_t stances, Feet feet, int bone, Quat q, int prio = 0) {
    Clip c; c.file = file; c.name = file; c.states = ST_Trick; c.trick = trick; c.trickHash = TrickHash(trick);
    c.stances = stances; c.feet = feet; c.priority = prio; c.loop = false; c.duration = 1.f; c.blendIn = 0.f; c.blendOut = 0.f;
    c.mode = Mode::Replace; c.mirror = false;
    Track t; t.bone = Names()[bone]; t.index = bone; t.keys.push_back(Key{0.f, q, true});
    c.tracks.push_back(t);
    return c;
}

GameState Trick(const char* base, int stance, bool goofy, uint32_t pop = 1) {
    GameState g{false, false, goofy, stance == STANCE_Switch, 0.f};
    g.kickflip = true; g.popSerial = pop; g.trickPhase = 1; g.trickHash = TrickHash(base); g.stance = stance;
    return g;
}

float AngleOf(const FTr& a, const FTr& b) {   // angle between two component rotations, degrees
    const float d = std::fabs(QDot(TrQ(a), TrQ(b)));
    return 2.f * std::acos(d > 1.f ? 1.f : d) * 57.2958f;
}
}

TEST(p6_player_picks_trick_stance_feet) {
    auto pl = MakePlayer();
    ClipSet set; set.generation = 1;
    const Quat qa = QFromRotatorDeg(30, 0, 0), qb = QFromRotatorDeg(0, 40, 0), qc = QFromRotatorDeg(0, 0, 50);
    set.clips.push_back(TrickClip("a_any.sanim", "", kAllStances, Feet::Any, 5, qa));                    // any trick
    set.clips.push_back(TrickClip("b_kick_normal.sanim", "KickFlip", 1 << STANCE_Normal, Feet::Regular, 5, qb));
    set.clips.push_back(TrickClip("c_kick_fakie.sanim", "kickflip", 1 << STANCE_Fakie, Feet::Any, 5, qc));
    FTr cs[kN], ref[kN];
    Pose(ref);
    auto run = [&](const GameState& g) { Pose(cs); pl->Apply(cs, kN, &set, g, 1.f / 60.f); return std::string(pl->CurrentClipName()); };
    CHECK(run(Trick("KickFlip", STANCE_Normal, false, 1)) == "b_kick_normal.sanim");
    CHECK(run(Trick("KickFlip", STANCE_Fakie, false, 2)) == "c_kick_fakie.sanim");
    CHECK(run(Trick("KickFlip", STANCE_Normal, true, 3)) == "a_any.sanim");    // goofy feet: the regular clip is out
    CHECK(run(Trick("HeelFlip", STANCE_Switch, false, 4)) == "a_any.sanim");
    set.clips[0].enabled = false; set.generation++;
    CHECK(run(Trick("HeelFlip", STANCE_Switch, false, 5)) == "");
}

TEST(p6_player_state_clips_by_stance) {
    auto pl = MakePlayer();
    ClipSet set; set.generation = 1;
    Clip g = TrickClip("grind_switch.sanim", "", 1 << STANCE_Switch, Feet::Any, 5, QFromRotatorDeg(20, 0, 0));
    g.states = ST_Grind; g.trick.clear(); g.trickHash = 0; g.loop = true;
    set.clips.push_back(g);
    FTr cs[kN];
    GameState st{false, false, false, false, 0.f};
    st.grind = true;
    Pose(cs); pl->Apply(cs, kN, &set, st, 1.f / 60.f);
    CHECK(std::string(pl->CurrentClipName()) == "");
    st.sw = true;
    Pose(cs); pl->Apply(cs, kN, &set, st, 1.f / 60.f);
    CHECK(std::string(pl->CurrentClipName()) == "grind_switch.sanim");
}

TEST(p6_leg_post_pass_restores_our_legs) {
    auto pl = MakePlayer();
    ClipSet set; set.generation = 1;
    const Quat bend = QFromRotatorDeg(0, 0, 60);   // bend the knee
    set.clips.push_back(TrickClip("k.sanim", "KickFlip", kAllStances, Feet::Any, 3, bend));
    FTr cs[kN];
    Pose(cs);
    pl->Apply(cs, kN, &set, Trick("KickFlip", 0, false), 1.f / 60.f);
    FTr ours[kN];
    for (int i = 0; i < kN; i++) ours[i] = cs[i];
    // another mod moves the foot alone, stretching the shin, and turns the arm
    cs[4].pz -= 15.f; cs[4].px += 7.f;
    const Quat arm = QFromRotatorDeg(45, 0, 0);
    cs[5].qx = arm.x; cs[5].qy = arm.y; cs[5].qz = arm.z; cs[5].qw = arm.w;
    const int k = pl->PostApply(cs, kN);
    CHECK(k == 3);   // thigh, calf, foot
    for (int c = 0; c < 3; c++) CHECK_NEAR((&cs[4].px)[c], (&ours[4].px)[c], 1e-3);   // foot back where we put it
    CHECK(AngleOf(cs[3], ours[3]) < 0.01f);
    CHECK(AngleOf(cs[5], ours[5]) > 44.f);   // the arm keeps the other mod's work
    CHECK(pl->PostApply(cs, kN) == 0);        // once per Apply
}

TEST(p6_leg_post_pass_off_and_no_legs) {
    auto pl = MakePlayer();
    ClipSet set; set.generation = 1; set.legPostPass = false;
    set.clips.push_back(TrickClip("k.sanim", "KickFlip", kAllStances, Feet::Any, 3, QFromRotatorDeg(0, 0, 60)));
    FTr cs[kN];
    Pose(cs);
    pl->Apply(cs, kN, &set, Trick("KickFlip", 0, false), 1.f / 60.f);
    CHECK(pl->PostApply(cs, kN) == 0);
    auto p2 = MakePlayer();
    ClipSet arms; arms.generation = 1;
    arms.clips.push_back(TrickClip("a.sanim", "KickFlip", kAllStances, Feet::Any, 5, QFromRotatorDeg(0, 0, 60)));
    Pose(cs);
    p2->Apply(cs, kN, &arms, Trick("KickFlip", 0, false), 1.f / 60.f);
    CHECK(p2->PostApply(cs, kN) == 0);   // only legs are replayed
}

TEST(p6_mirror_only_when_bodies_differ) {
    auto pl = MakePlayer();
    ClipSet set; set.generation = 1;
    Clip c = TrickClip("m.sanim", "KickFlip", kAllStances, Feet::Any, 5, QFromRotatorDeg(0, 0, 60));
    c.mirror = true;
    set.clips.push_back(c);
    FTr cs[kN], base[kN];
    Pose(base);
    // regular body: the left arm is written. Goofy body: mirrored to the right arm, which this rig lacks -> other bone
    Pose(cs); pl->Apply(cs, kN, &set, Trick("KickFlip", 0, false, 1), 1.f / 60.f);
    CHECK(AngleOf(cs[5], base[5]) > 59.f);
}

// ------------------------------------------------------------------ editor: variants
TEST(p6_editor_variant_of_clip) {
    FigureVariant v; v.state = ST_Trick; v.trick = "HeelFlip"; v.stance = STANCE_Nollie; v.goofy = true;
    Clip c;
    SetClipVariant(c, v);
    CHECK(c.states == ST_Trick && c.trick == "HeelFlip" && c.stances == (1 << STANCE_Nollie) && c.feet == Feet::Goofy && !c.mirror);
    CHECK(VariantOfClip(c) == v);
    EditorDoc d;
    d.NewClip(v);
    CHECK(d.Variant() == v && d.IsTrickClip() && !d.clip().loop && d.clip().trick == "HeelFlip");
}

namespace {
float Dist(const FTr& a, const FTr& b) { return std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) + (a.pz - b.pz) * (a.pz - b.pz)); }
}

// P6: after our legs are given back, another mod moves the foot and stretches the shin: the knee follows the foot,
// thigh and shin keep their length, the foot stays where the other mod put it (it is reachable)
TEST(p6_anti_stretch_knee_follows_foot) {
    auto pl = MakePlayer();
    ClipSet set; set.generation = 1;
    set.clips.push_back(TrickClip("k.sanim", "KickFlip", kAllStances, Feet::Any, 3, QFromRotatorDeg(0, 0, 20)));
    FTr cs[kN];
    Pose(cs);
    pl->Apply(cs, kN, &set, Trick("KickFlip", 0, false), 1.f / 60.f);
    pl->PostApply(cs, kN);
    set.clips[0].enabled = false; set.generation++;   // our clip stops writing the legs
    GameState g = Trick("KickFlip", 0, false);
    for (int i = 0; i < 3; i++) { Pose(cs); pl->Apply(cs, kN, &set, g, 1.f / 60.f); pl->PostApply(cs, kN); }
    Pose(cs);
    pl->Apply(cs, kN, &set, g, 1.f / 60.f);
    // the other mod: shin stretched to 52 cm, foot pushed forward
    cs[4].px = 20.f; cs[4].py = 10.f; cs[4].pz = 15.f;   // 72.8 cm from the hip: reachable
    const FTr target = cs[4];
    CHECK(pl->PostApply(cs, kN) >= 3);
    CHECK_NEAR(Dist(cs[2], cs[3]), 40.0, 0.05);   // thigh
    CHECK_NEAR(Dist(cs[3], cs[4]), 40.0, 0.05);   // shin
    CHECK(Dist(cs[4], target) < 0.05f);           // the foot is where the other mod wanted it
    // out of reach: the leg is straight toward the target, never longer
    Pose(cs);
    pl->Apply(cs, kN, &set, g, 1.f / 60.f);
    cs[4].pz = -20.f;
    pl->PostApply(cs, kN);
    CHECK_NEAR(Dist(cs[3], cs[4]), 40.0, 0.05);
    CHECK(Dist(cs[2], cs[4]) < 80.f);
    // long after: nothing is touched any more
    for (int i = 0; i < 60; i++) { Pose(cs); pl->Apply(cs, kN, &set, g, 1.f / 60.f); pl->PostApply(cs, kN); }
    Pose(cs);
    pl->Apply(cs, kN, &set, g, 1.f / 60.f);
    cs[4].pz = -3.f;
    CHECK(pl->PostApply(cs, kN) == 0);
}

// P6: seeded keys of a new trick clip
namespace {
std::string SeedSkel() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
}
TEST(p6_editor_seed_keys) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(SeedSkel(), err));
    FigureRec f; f.figure = "KickFlip_RGS_regular"; f.bones = 70;
    for (int i = 0; i < 30; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        f.phases.push_back(i < 1 ? 0 : i < 6 ? 1 : i < 18 ? 2 : i < 24 ? 3 : 4);
        for (int b = 0; b < 70; b++) f.frames.push_back(QFromRotatorDeg(i * 2.f, (float)b, 0.f));
    }
    FigureVariant v = LegacyVariant(FIG_Kickflip);
    CHECK(d.LoadFigure(FigureToJson(f), v, err));
    d.NewClip(v);
    d.SetLockEnds(false);   // seeding alone (the locked ends: test_end_lock.cpp)
    const int n = d.SeedKeys();
    int editable = 0;
    for (int b = 0; b < d.BoneCount(); b++) if (d.IsEditable(b)) editable++;
    CHECK(editable > 30 && n == editable * 3);
    const auto times = d.KeyTimes(-1);
    CHECK(times.size() == 3);
    if (times.size() == 3) { CHECK_NEAR(times[1], 6 / 30.f, 1e-4); CHECK_NEAR(times[2], 24 / 30.f, 1e-4); }
    // replace mode: the seeded pose is the game's pose at those instants
    const int calf = [&] { for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == "AMXX_L_Calf") return b; return -1; }();
    CHECK(calf >= 0 && d.TrackOf(calf) != nullptr);
    if (calf >= 0) CHECK(std::fabs(QDot(d.Edited(calf, 6 / 30.f), d.OriginForClip(calf, 6 / 30.f))) > 0.9999f);
    CHECK(d.SeedKeys() == 0);   // only once
    d.NewClip(LegacyVariant(FIG_Manual));
    CHECK(d.SeedKeys() == 0);   // not for loops
}

// P6: the game stretches the shin in the air; a leg our clip writes gets its own bone lengths back (as in the editor)
TEST(p6_written_leg_uses_reference_lengths) {
    auto pl = MakePlayer();
    // reference local positions of the rig (Pose(): thigh 40 cm, shin 40 cm)
    const std::vector<float> ref = {0, 0, 0, 0, 0, 90, 0, 10, -5, 0, 0, -40, 0, 0, -40, 0, -20, 50};
    pl->SetRefPositions(ref);
    ClipSet set; set.generation = 1;
    set.clips.push_back(TrickClip("k.sanim", "KickFlip", kAllStances, Feet::Any, 3, QFromRotatorDeg(0, 0, 30)));
    FTr cs[kN];
    Pose(cs);
    cs[4].pz = -2.f;   // the game: shin 47 cm
    pl->Apply(cs, kN, &set, Trick("KickFlip", 0, false), 1.f / 60.f);
    CHECK_NEAR(Dist(cs[3], cs[4]), 40.0, 0.01);
    CHECK_NEAR(Dist(cs[2], cs[3]), 40.0, 0.01);
    // arms only: the game's stretch is left alone
    auto p2 = MakePlayer();
    p2->SetRefPositions(ref);
    ClipSet arms; arms.generation = 1;
    arms.clips.push_back(TrickClip("a.sanim", "KickFlip", kAllStances, Feet::Any, 5, QFromRotatorDeg(0, 0, 30)));
    Pose(cs);
    cs[4].pz = -2.f;
    p2->Apply(cs, kN, &arms, Trick("KickFlip", 0, false), 1.f / 60.f);
    CHECK_NEAR(Dist(cs[3], cs[4]), 47.0, 0.01);
}

static float AngDeg(Quat a, Quat b) { const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w); return 2.f * std::acos(d > 1.f ? 1.f : d) * 57.2958f; }

// P6: smooth keys follow a C1 spline: the motion keeps going through a middle key (no stop = no stutter)
TEST(p6_smooth_keys_do_not_stop) {
    Track tr;
    tr.bone = "b";
    tr.keys.push_back(Key{0.f, QIdent, true});
    tr.keys.push_back(Key{0.5f, QFromRotatorDeg(0, 0, 30), true});
    tr.keys.push_back(Key{1.0f, QFromRotatorDeg(0, 0, 60), true});
    const float e = 0.01f;
    // angular speed just around the middle key: about 60 deg/s on both sides (the old smoothstep gave ~0 there)
    const float before = AngDeg(EvalTrack(tr, 0.5f - e, 1.f, false), EvalTrack(tr, 0.5f, 1.f, false)) / e;
    const float after = AngDeg(EvalTrack(tr, 0.5f, 1.f, false), EvalTrack(tr, 0.5f + e, 1.f, false)) / e;
    CHECK(before > 40.f && after > 40.f);
    CHECK_NEAR(before, after, 3.0);
    // keys are still hit exactly, the ends still ease in and out
    CHECK(AngDeg(EvalTrack(tr, 0.5f, 1.f, false), tr.keys[1].q) < 0.01f);
    CHECK(AngDeg(EvalTrack(tr, 0.f, 1.f, false), EvalTrack(tr, e, 1.f, false)) / e < 10.f);
    // loop: continuous through the wrap (last key -> first key)
    Track lp = tr;
    lp.keys[2].q = QFromRotatorDeg(0, 0, 10);
    const Quat a = EvalTrack(lp, 2.f - 1e-4f, 2.f, true), b = EvalTrack(lp, 0.f, 2.f, true);
    CHECK(AngDeg(a, b) < 0.1f);
    // linear keys stay linear
    Track ln = tr;
    for (auto& k : ln.keys) k.smooth = false;
    CHECK_NEAR(AngDeg(EvalTrack(ln, 0.25f, 1.f, false), QIdent), 15.0, 0.05);
}

// P6: a new clip adds to the game's animation by default
TEST(p6_new_clip_is_additive) {
    EditorDoc d;
    d.NewClip(FigureVariant{ST_Trick, "KickFlip", STANCE_Normal, false});
    CHECK(d.clip().mode == Mode::Additive);
}
