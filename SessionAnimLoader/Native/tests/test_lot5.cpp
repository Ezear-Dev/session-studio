// Lot 5: step keys, landing part of a trick clip, fallback / debounce / synchronised cross-fade between clips.
#include "check.h"
#include "../src/clip.h"
#include "../src/player.h"
#include "../src/trick.h"
#include <memory>

namespace {
constexpr float kDt = 1.f / 60.f;
float RollDeg(const Quat& q) {   // angle of a rotation (degrees)
    const float w = std::fabs(q.w) > 1.f ? 1.f : std::fabs(q.w);
    return 2.f * std::acos(w) * 57.29578f;
}
Clip Base(const char* f, uint32_t st, float deg) {
    Clip c; c.file = f; c.name = f; c.states = st; c.duration = 1.f; c.loop = true; c.mirror = false; c.blendIn = 0.f;
    Track t; t.bone = "b"; t.index = 1; t.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, deg), true});
    c.tracks.push_back(t);
    return c;
}
struct Rig2 {
    std::unique_ptr<Player> p = std::make_unique<Player>();
    ClipSet set;
    float deg = 0.f;
    Rig2() { const int par[2] = {-1, 0}; p->SetRig(par, 2, MirrorMap{}); set.generation = 1; }
    std::string Step(const GameState& g, float dt = kDt) {
        FTr cs[2] = {FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0}, FTr{0, 0, 0, 1, 10, 0, 0, 0, 1, 1, 1, 0}};
        p->Apply(cs, 2, &set, g, dt);
        deg = RollDeg(TrQ(cs[1]));
        return p->CurrentClipName();
    }
    std::string Run(const GameState& g, int frames) { std::string s; for (int i = 0; i < frames; i++) s = Step(g); return s; }
};
GameState Ride() { GameState g{}; g.riding = true; return g; }
GameState Air(uint32_t pop) { GameState g{}; g.kickflip = true; g.popSerial = pop; g.trickPhase = 1; return g; }
} // namespace

TEST(lot5_step_keys) {
    Track tr; tr.bone = "b";
    tr.keys.push_back(Key{0.f, QIdent, false, true});
    tr.keys.push_back(Key{1.f, QFromRotatorDeg(0, 0, 40), true});
    CHECK_NEAR(RollDeg(EvalTrack(tr, 0.5f, 2.f, false)), 0.0, 1e-3);    // constant until the next key
    CHECK_NEAR(RollDeg(EvalTrack(tr, 0.999f, 2.f, false)), 0.0, 1e-3);
    CHECK_NEAR(RollDeg(EvalTrack(tr, 1.0f, 2.f, false)), 40.0, 1e-2);
    Clip c; c.name = "s"; c.states = ST_Riding; c.duration = 2.f;
    c.tracks.push_back(tr);
    c.pelvis.push_back(PosKey{0.f, {0, 0, 0}, false, true});
    c.pelvis.push_back(PosKey{1.f, {0, 0, -10}, true});
    float o[3];
    EvalPelvis(c.pelvis, 0.6f, 2.f, false, o);
    CHECK_NEAR(o[2], 0.0, 1e-4);
    const std::string js = ClipToJson(c);
    CHECK(js.find("\"step\"") != std::string::npos);
    Clip back; std::string err;
    CHECK(ParseClip(js, "s", back, err));
    CHECK(back.tracks.size() == 1 && back.tracks[0].keys[0].step && !back.tracks[0].keys[0].smooth && !back.tracks[0].keys[1].step);
    CHECK(back.pelvis.size() == 2 && back.pelvis[0].step);
    CHECK(ClipToJson(back) == js);
}

TEST(lot5_landing_fields) {
    Clip c; c.name = "kf"; c.states = ST_Trick; c.trick = "KickFlip"; c.duration = 1.2f; c.loop = false;
    c.landing = 0.5f; c.groundAt = 0.7f;
    Track t; t.bone = "b"; t.keys.push_back(Key{0.f, QIdent, true}); c.tracks.push_back(t);
    const std::string js = ClipToJson(c);
    Clip back; std::string err;
    CHECK(ParseClip(js, "kf", back, err));
    CHECK_NEAR(back.landing, 0.5, 1e-6);
    CHECK_NEAR(back.groundAt, 0.7, 1e-6);
    c.duration = 1.0f;   // groundAt + landing > duration
    CHECK(!ParseClip(ClipToJson(c), "kf", back, err));
    Clip plain = c; plain.landing = 0.f; plain.groundAt = 0.f;
    CHECK(ClipToJson(plain).find("landing") == std::string::npos);   // old files unchanged
}

TEST(lot5_fallback_to_riding) {
    Rig2 r;
    r.set.clips.push_back(Base("ride", ST_Riding, 20));
    CHECK(r.Run(Ride(), 5) == "ride");
    GameState g{}; g.pushing = true;
    CHECK(r.Run(g, 30) == "ride");
    g = GameState{}; g.crouch = true; g.groundStance = STANCE_Fakie;   // riding has no fakie: normal stance used
    CHECK(r.Run(g, 30) == "ride");
    g = GameState{}; g.landing = true;
    CHECK(r.Run(g, 30) == "ride");
    g = GameState{}; g.running = true;   // off the board: never the riding clip
    CHECK(r.Run(g, 60) == "");
    g = GameState{}; g.onFoot = true;
    CHECK(r.Run(g, 60) == "");   // no fallback off the board
}

TEST(lot5_debounce_and_gap) {
    Rig2 r;
    r.set.clips.push_back(Base("ride", ST_Riding, 20));
    r.set.clips.push_back(Base("push", ST_Pushing, -20));
    CHECK(r.Run(Ride(), 5) == "ride");
    GameState g{}; g.pushing = true;
    CHECK(r.Run(g, 3) == "ride");        // 0.05 s: not yet
    CHECK(r.Run(Ride(), 1) == "ride");   // the flicker is gone: nothing happened
    CHECK_NEAR(r.deg, 20.0, 0.1);
    CHECK(r.Run(g, 12) == "push");       // 0.2 s: switched
    // gap: a state with no clip at all (and no fallback) for 0.1 s keeps the clip
    r.set.clips.pop_back(); r.set.generation = 2;
    CHECK(r.Run(Ride(), 30) == "ride");
    CHECK(r.Run(GameState{}, 6) == "ride");
    CHECK(r.Run(Ride(), 1) == "ride");
    CHECK_NEAR(r.deg, 20.0, 0.1);
    CHECK(r.Run(GameState{}, 30) == "");   // a real gap: fades out
}

TEST(lot5_synchronised_crossfade_has_no_dip) {
    Rig2 r;
    Clip a = Base("ride", ST_Riding, 20); a.blendOut = 0.6f;      // uneven own blends: the cross-fade ignores them
    Clip b = Base("crouch", ST_Crouch, 20); b.blendIn = 0.2f;
    r.set.clips.push_back(a); r.set.clips.push_back(b);
    r.Run(Ride(), 30);
    CHECK_NEAR(r.deg, 20.0, 0.1);
    GameState g{}; g.crouch = true;
    float lo = 99.f, hi = -99.f;
    for (int i = 0; i < 40; i++) { r.Step(g); lo = std::fmin(lo, r.deg); hi = std::fmax(hi, r.deg); }
    CHECK(r.p->CurrentClipName() == std::string("crouch"));
    CHECK(lo > 19.5f && hi < 20.5f);   // same pose in both clips: the body never goes back toward the game pose
}

namespace {
Clip LandTrick(float groundAt, float landing) {   // 0 deg in the air, 0 -> 40 deg over the landing part
    Clip c; c.file = "kf"; c.name = "kf"; c.states = ST_Trick; c.priority = 3; c.loop = false; c.mirror = false;
    c.blendIn = 0.05f; c.blendOut = 0.15f; c.groundAt = groundAt; c.landing = landing; c.duration = groundAt + landing;
    Track t; t.bone = "b"; t.index = 1;
    t.keys.push_back(Key{0.f, QIdent, false});
    t.keys.push_back(Key{groundAt, QIdent, false});
    t.keys.push_back(Key{groundAt + landing, QFromRotatorDeg(0, 0, 40), false});
    c.tracks.push_back(t);
    return c;
}
} // namespace

// 2026-11-25: the lot 5 landing part (hold at groundAt, 3x catch-up) is gone: see test_landing_sync.cpp

TEST(lot5_landing_part_gives_way_to_a_manual) {
    Rig2 r;
    r.set.clips.push_back(Base("man", ST_Manual, 10));
    r.set.clips.push_back(LandTrick(0.5f, 0.5f));
    r.Run(Air(1), 30);
    GameState g{}; g.manual = true; g.popSerial = 1;
    CHECK(r.Run(g, 30) == "man");
}

TEST(running_state) {
    CHECK(StateFromName("Running") == ST_Running && StateLoops(ST_Running) && !StateAllStances(ST_Running));
    FigureVariant v; v.state = ST_Running; FigureVariant back;
    CHECK(FigureKey(v) == "Running_RGS_regular" && ParseFigureKey(FigureKey(v), back) && back == v);
    Rig2 r;
    r.set.clips.push_back(Base("walk", ST_Walking, 10));
    r.set.clips.push_back(Base("run", ST_Running, 30));
    r.set.clips.push_back(Base("ride", ST_Riding, 0));
    GameState g{}; g.walking = true;
    CHECK(r.Run(g, 20) == "walk");
    g = GameState{}; g.running = true;
    CHECK(r.Run(g, 20) == "run");
    g = GameState{}; g.riding = true;
    CHECK(r.Run(g, 20) == "ride");
}
