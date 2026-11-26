// 2026-11-25 landing sync: trick clips follow the real jump (markers + time warp), the landing is a clip of its own.
#include "check.h"
#include "../src/clip.h"
#include "../src/player.h"
#include "../src/trick.h"
#include "../src/trick_sync.h"
#include <memory>

namespace {
constexpr float kDt = 1.f / 60.f;
std::vector<int> Phases(std::initializer_list<std::pair<int, int>> marks, int frames) {   // (frame, phase) from that frame on
    std::vector<int> p(frames, 0);
    for (auto& m : marks) for (int i = m.first; i < frames; i++) p[i] = m.second;
    return p;
}
std::vector<float> Times(int frames) { std::vector<float> t(frames); for (int i = 0; i < frames; i++) t[i] = i / 30.f; return t; }
TrickMarkers Ollie() { return MarkersFromPhases(Phases({{5, 1}, {13, 4}, {27, 5}}, 51), Times(51)); }

// one simulated jump: takeoff at 0.167 s, contact at T, IsLanding (ground - land) before the contact as recorded
struct Jump { float tAtLand = -1.f, tAtGround = -1.f, maxStep = 0.f; bool monotone = true; };
Jump Simulate(const TrickMarkers& m, float T, bool predict) {
    TrickClock c;
    Jump j;
    const float landAt = T - (m.ground - m.land);
    float prev = 0.f;
    for (float live = 0.f; live < T + 0.3f; live += kDt) {
        const bool ground = live >= T;
        const int phase = live >= landAt && live >= m.air ? 4 : live >= m.air ? 1 : 0;
        const float pred = predict && live >= m.air && !ground ? T - live : -1.f;
        const float t = c.Step(m, m.ground, phase, ground, pred, kDt);
        if (t < prev - 1e-6f) j.monotone = false;
        if (!ground && t - prev > j.maxStep) j.maxStep = t - prev;
        prev = t;
        if (j.tAtLand < 0.f && live >= landAt && live >= m.air) j.tAtLand = t;
        if (j.tAtGround < 0.f && ground) j.tAtGround = t;
    }
    return j;
}
float RollDeg(const Quat& q) { const float w = std::fabs(q.w) > 1.f ? 1.f : std::fabs(q.w); return 2.f * std::acos(w) * 57.29578f; }
struct Rig2 {
    std::unique_ptr<Player> p = std::make_unique<Player>();
    ClipSet set;
    float deg = 0.f;
    Rig2() { const int par[2] = {-1, 0}; p->SetRig(par, 2, MirrorMap{}); set.generation = 1; }
    std::string Step(const GameState& g) {
        FTr cs[2] = {FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0}, FTr{0, 0, 0, 1, 10, 0, 0, 0, 1, 1, 1, 0}};
        p->Apply(cs, 2, &set, g, kDt);
        deg = RollDeg(TrQ(cs[1]));
        return p->CurrentClipName();
    }
    std::string Run(const GameState& g, int frames) { std::string s; for (int i = 0; i < frames; i++) s = Step(g); return s; }
};
Clip OneKey(const char* f, uint32_t st, const char* trick, float deg) {
    Clip c; c.file = f; c.name = f; c.states = st; c.trick = trick; c.trickHash = TrickHash(trick); c.duration = 0.8f;
    c.loop = false; c.mirror = false; c.blendIn = 0.f;
    Track t; t.bone = "b"; t.index = 1; t.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, deg), true});
    c.tracks.push_back(t);
    return c;
}
} // namespace

TEST(sync_markers_from_real_figures) {
    const TrickMarkers o = Ollie();   // Ollie_RGS_regular: air 5, IsLanding 13, ground 27 (30 fps)
    CHECK(o.ok);
    CHECK_NEAR(o.air, 5 / 30.0, 1e-4);
    CHECK_NEAR(o.catchT, o.air, 1e-6);   // no catch: the hang starts at the takeoff
    CHECK_NEAR(o.land, 13 / 30.0, 1e-4);
    CHECK_NEAR(o.ground, 0.9, 1e-4);
    // KickFlip_RGS_regular: 1 at 5, 2 at 6, 3 at 15, 4 at 17, 5 at 19
    const TrickMarkers k = MarkersFromPhases(Phases({{5, 1}, {6, 2}, {15, 3}, {17, 4}, {19, 5}}, 43), Times(43));
    CHECK(k.ok && std::fabs(k.catchT - 0.5f) < 1e-4f && std::fabs(k.land - 17 / 30.f) < 1e-4f && std::fabs(k.ground - 19 / 30.f) < 1e-4f);
    // Treflip_RGS_regular: no phase 1 (straight to the flip at 7)
    const TrickMarkers t = MarkersFromPhases(Phases({{7, 2}, {17, 3}, {19, 4}, {24, 5}}, 48), Times(48));
    CHECK(t.ok && std::fabs(t.air - 7 / 30.f) < 1e-4f && std::fabs(t.ground - 0.8f) < 1e-4f);
    CHECK(!MarkersFromPhases({}, {}).ok);
    CHECK(!MarkersFromPhases(std::vector<int>(10, 0), Times(10)).ok);   // never in the air
    // no ground tail (old figure): ground = last frame
    const TrickMarkers old = MarkersFromPhases(Phases({{5, 1}, {13, 4}}, 20), Times(20));
    CHECK(old.ok && std::fabs(old.ground - 19 / 30.f) < 1e-4f);
}

TEST(sync_markers_format) {
    Clip c; c.name = "o"; c.states = ST_Trick; c.trick = "Ollie"; c.duration = 0.9f; c.loop = false;
    c.markers = Ollie();
    Track t; t.bone = "b"; t.keys.push_back(Key{0.f, QIdent, true}); c.tracks.push_back(t);
    const std::string js = ClipToJson(c);
    CHECK(js.find("\"markers\"") != std::string::npos);
    Clip back; std::string err;
    CHECK(ParseClip(js, "o", back, err));
    CHECK(back.markers.ok && std::fabs(back.markers.land - c.markers.land) < 1e-5f && std::fabs(back.markers.ground - 0.9f) < 1e-5f);
    CHECK(ClipToJson(back) == js);
    Clip bad = c; bad.markers.land = 0.1f;   // land before catch
    CHECK(!ParseClip(ClipToJson(bad), "o", back, err));
    bad = c; bad.markers.ground = 1.5f;      // past the end
    CHECK(!ParseClip(ClipToJson(bad), "o", back, err));
    Clip plain = c; plain.markers = TrickMarkers{};
    CHECK(ClipToJson(plain).find("markers") == std::string::npos);
    // a landing clip may name its trick; a riding clip may not
    Clip land = OneKey("l", ST_Landing, "Ollie", 10);
    CHECK(ParseClip(ClipToJson(land), "l", back, err) && back.trick == "Ollie" && back.trickHash == TrickHash("Ollie"));
    CHECK(ClipSpecificity(back) >= 100);
    Clip ride = OneKey("r", ST_Riding, "", 10);
    std::string js2 = ClipToJson(ride);
    js2.replace(js2.find("\"states\""), 8, "\"trick\": \"Ollie\", \"states\"");
    CHECK(!ParseClip(js2, "r", back, err));
}

TEST(sync_landing_figure_key) {
    FigureVariant v; v.state = ST_Landing; v.trick = "Ollie"; v.stance = STANCE_Switch; v.goofy = true;
    CHECK(FigureKey(v) == "Landing-Ollie_SWS_goofy");
    FigureVariant back;
    CHECK(ParseFigureKey(FigureKey(v), back) && back == v);
    FigureVariant g; g.state = ST_Landing;
    CHECK(FigureKey(g) == "Landing_RGS_regular" && ParseFigureKey("Landing_RGS_regular", back) && back.state == ST_Landing && back.trick.empty());
    CHECK(StateNamed(ST_Landing));
}

TEST(sync_predict_and_gravity) {
    CHECK_NEAR(PredictGround(300.f, 0.f, 1000.f), 0.6, 1e-4);     // up at 3 m/s: back down in 0.6 s
    CHECK_NEAR(PredictGround(-200.f, 20.f, 1000.f), 0.0828, 1e-3); // falling, 20 cm above
    CHECK_NEAR(PredictGround(-50.f, -5.f, 1000.f), 0.0, 1e-6);     // already below the takeoff
    CHECK(PredictGround(100.f, 0.f, 0.f) < 0.f);
    GravityMeter g;
    float vz = 400.f;
    for (int i = 0; i < 30; i++) { g.Feed(vz, kDt, true); vz -= 1960.f * kDt; }
    CHECK_NEAR(g.G(), 1960.0, 5.0);
    g.Feed(vz, kDt, true);   // a repeated sample (no change) is ignored
    g.Feed(vz, kDt, true);
    CHECK_NEAR(g.G(), 1960.0, 5.0);
}

TEST(sync_clock_follows_low_and_high_jumps) {
    const TrickMarkers m = Ollie();
    for (float T : {0.75f, 0.9f, 1.4f}) {   // lower, same as, higher than the recording
        const Jump j = Simulate(m, T, true);
        CHECK(j.monotone);
        CHECK_NEAR(j.tAtLand, m.land, 0.05);       // the pre-landing keys start on the game's IsLanding
        CHECK_NEAR(j.tAtGround, m.ground, 0.06);   // the clip reaches its ground pose at the contact
        CHECK(j.maxStep <= TrickClock::kMaxRate * kDt + 1e-4f);   // never faster than 3x in the air
    }
}

TEST(sync_clock_without_prediction_never_freezes_nor_jumps) {
    const TrickMarkers m = Ollie();
    const Jump hi = Simulate(m, 1.4f, false);
    CHECK(hi.monotone);
    CHECK(hi.maxStep <= TrickClock::kMaxRate * kDt + 1e-4f);
    CHECK(hi.tAtGround > m.land);   // still on its way to the ground pose, never past it in the air
    TrickClock c;
    float prev = 0.f; bool moving = true;
    for (int i = 0; i < 120; i++) {   // stays in the air 2 s without IsLanding: creeps (never stops before the ground pose)
        const float t = c.Step(m, m.ground, 1, false, -1.f, kDt);
        if (t < m.ground - 1e-4f && !(t > prev)) moving = false;
        prev = t;
    }
    CHECK(moving && prev <= m.ground + 1e-5f);
    TrickClock plain;   // no markers: real time, held at the end
    for (int i = 0; i < 120; i++) plain.Step(TrickMarkers{}, 0.5f, 1, false, -1.f, kDt);
    CHECK_NEAR(plain.t, 0.5, 1e-5);
}

TEST(sync_player_trick_then_its_landing_clip) {
    Rig2 r;
    Clip trick = OneKey("ollie", ST_Trick, "Ollie", 0); trick.duration = 0.9f; trick.markers = Ollie(); trick.blendIn = 0.05f;
    trick.stances = 1u << STANCE_Normal;
    r.set.clips.push_back(trick);
    r.set.clips.push_back(OneKey("land_ollie", ST_Landing, "Ollie", 30));
    r.set.clips.push_back(OneKey("land_any", ST_Landing, "", -30));
    Clip landSw = OneKey("land_ollie_sw", ST_Landing, "Ollie", 60); landSw.stances = 1u << STANCE_Switch;
    r.set.clips.push_back(landSw);
    GameState a{}; a.kickflip = true; a.popSerial = 1; a.trickPhase = 1; a.trickHash = TrickHash("Ollie");
    CHECK(r.Run(a, 40) == "ollie");
    GameState g{}; g.landing = true; g.popSerial = 1; g.landTrickHash = TrickHash("Ollie"); g.landStance = STANCE_Normal;
    CHECK(r.Run(g, 30) == "land_ollie");   // its own landing clip, in the trick's stance
    CHECK_NEAR(r.deg, 30.0, 0.5);
    // a landing after another trick: the generic landing clip
    GameState a2 = a; a2.popSerial = 2; a2.trickHash = TrickHash("KickFlip");
    r.Run(a2, 40);
    GameState g2 = g; g2.popSerial = 2; g2.landTrickHash = TrickHash("KickFlip");
    CHECK(r.Run(g2, 30) == "land_any");
    // a landing with no trick before it: generic too
    GameState g3 = g; g3.landTrickHash = 0;
    CHECK(r.Run(g3, 30) == "land_any");
    // the trick's landing in switch
    GameState g4 = g; g4.landStance = STANCE_Switch;
    CHECK(r.Run(g4, 30) == "land_ollie_sw");
}

TEST(sync_player_trick_clip_time_follows_the_hang) {
    // a trick clip 0 deg until IsLanding, then 0 -> 40 deg to the ground: on a high jump the bend waits for IsLanding
    Rig2 r;
    Clip c; c.file = "o"; c.name = "o"; c.states = ST_Trick; c.trick = "Ollie"; c.trickHash = TrickHash("Ollie"); c.loop = false;
    c.mirror = false; c.blendIn = 0.f; c.duration = 0.9f; c.markers = Ollie();
    Track t; t.bone = "b"; t.index = 1;
    t.keys.push_back(Key{0.f, QIdent, false});
    t.keys.push_back(Key{c.markers.land, QIdent, false});
    t.keys.push_back(Key{0.9f, QFromRotatorDeg(0, 0, 40), false});
    c.tracks.push_back(t);
    r.set.clips.push_back(c);
    GameState a{}; a.kickflip = true; a.popSerial = 1; a.trickHash = TrickHash("Ollie");
    const float T = 1.4f;
    float atLand = -1.f;
    for (float live = 0.f; live < T; live += kDt) {
        a.trickPhase = live >= T - 0.467f ? 4 : live >= 0.167f ? 1 : 0;
        a.predGround = live >= 0.167f ? T - live : -1.f;
        r.Step(a);
        if (live < T - 0.5f) CHECK(r.deg < 1.0f);   // still straight in the hang time (the old clock bent at 0.43 s)
        if (atLand < 0.f && a.trickPhase == 4) atLand = r.deg;
    }
    CHECK(atLand < 3.0f);
    CHECK(r.deg > 30.f);   // bent by the contact
}

TEST(sync_land_learn) {
    LandLearn l;
    l.Learn(-1.f, 0.3f);   // no IsLanding: nothing learned
    CHECK(l.n == 0 && l.lead < 0.f);
    l.Learn(0.15f, 0.33f);   // flip trick (first test, 2026-11-26)
    CHECK(l.n == 1 && std::fabs(l.lead - 0.15f) < 1e-6f && std::fabs(l.factor - 0.15f / 0.33f) < 1e-4f);
    for (int i = 0; i < 20; i++) l.Learn(0.20f, 0.33f);
    CHECK_NEAR(l.lead, 0.20, 0.002);
    CHECK_NEAR(l.factor, 0.20 / 0.33, 0.005);
    l.Learn(56.f, 0.3f);   // a pause in the air: ignored
    CHECK_NEAR(l.lead, 0.20, 0.002);
    LandLearn z; z.Learn(0.2f, 0.f);   // no prediction: lead only
    CHECK(z.n == 1 && z.factor == 1.f);
}

TEST(sync_clock_uses_the_learned_lead) {
    // Ollie: recorded IsLanding -> ground 0.467 s, live 0.38 s. With the learned lead, the clip reaches its IsLanding
    // marker when the game raises IsLanding.
    const TrickMarkers m = Ollie();
    const float T = 0.81f, lead = 0.38f, landAt = T - lead;
    TrickClock c;
    float atLand = -1.f;
    for (float live = 0.f; live < T; live += kDt) {
        const int phase = live >= landAt ? 4 : live >= m.air ? 1 : 0;
        const float t = c.Step(m, m.ground, phase, false, live >= m.air ? T - live : -1.f, kDt, lead);
        if (atLand < 0.f && phase == 4) atLand = t;
    }
    CHECK_NEAR(atLand, m.land, 0.03);
}
