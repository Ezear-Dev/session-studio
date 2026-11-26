// P6 lot 4: base poses (on foot, riding, crouch, landing)
#include "check.h"
#include "../src/clip.h"
#include "../src/player.h"
#include "../src/figure.h"
#include "../src/trick.h"
#include <memory>

TEST(lot4_state_names) {
    for (uint32_t b : kStateOrder) CHECK(StateFromName(StateName(b)) == b);
    CHECK(StateFromName("Riding") == ST_Riding && StateFromName("Crouch") == ST_Crouch);
    CHECK(StateLoops(ST_Riding) && StateLoops(ST_OnFoot) && StateLoops(ST_Crouch) && !StateLoops(ST_Landing) && !StateLoops(ST_Trick));
    CHECK(StateAllStances(ST_Crouch) && !StateAllStances(ST_Riding) && !StateAllStances(ST_Landing) && !StateAllStances(ST_Manual) && !StateAllStances(ST_OnFoot));
    FigureVariant v; v.state = ST_Crouch; v.stance = STANCE_Fakie; v.goofy = true;
    FigureVariant back;
    CHECK(FigureKey(v) == "Crouch_FKS_goofy" && ParseFigureKey(FigureKey(v), back) && back == v);
}

TEST(lot4_clip_states_roundtrip) {
    Clip c;
    c.name = "r"; c.states = ST_Riding | ST_Crouch; c.duration = 1.f;
    Track t; t.bone = "AMXX_Head"; t.keys.push_back(Key{0.f, QIdent, true});
    c.tracks.push_back(t);
    Clip back;
    std::string err;
    CHECK(ParseClip(ClipToJson(c), "r", back, err));
    CHECK(back.states == (ST_Riding | ST_Crouch));
    CHECK(ClipToJson(back) == ClipToJson(c));
}

TEST(lot4_player_picks_base_pose) {
    const int n = 2;
    const int par[n] = {-1, 0};
    auto pl = std::make_unique<Player>();
    pl->SetRig(par, n, MirrorMap{});
    auto mk = [](const char* f, uint32_t st) {
        Clip c; c.file = f; c.name = f; c.states = st; c.duration = 1.f; c.loop = true; c.mirror = false; c.blendIn = 0.f;
        Track t; t.bone = "b"; t.index = 1; t.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, 20), true});
        c.tracks.push_back(t);
        return c;
    };
    ClipSet set;
    set.clips.push_back(mk("ride", ST_Riding));
    set.clips.push_back(mk("crouch", ST_Crouch));
    set.clips.push_back(mk("foot", ST_OnFoot));
    set.clips.push_back(mk("man", ST_Manual));
    set.clips.push_back(mk("walk", ST_Walking));
    set.clips.push_back(mk("push", ST_Pushing));
    set.generation = 1;
    FTr cs[n] = {FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0}, FTr{0, 0, 0, 1, 10, 0, 0, 0, 1, 1, 1, 0}};
    auto run = [&](const GameState& g) { for (int f = 0; f < 12; f++) { FTr c2[n] = {cs[0], cs[1]}; pl->Apply(c2, n, &set, g, 1.f / 60.f); } return std::string(pl->CurrentClipName()); };
    GameState g{};
    g.riding = true;
    CHECK(run(g) == "ride");
    g.riding = false; g.crouch = true;
    CHECK(run(g) == "crouch");
    g.crouch = false; g.onFoot = true;
    CHECK(run(g) == "foot");
    g.onFoot = false; g.walking = true;
    CHECK(run(g) == "walk");
    g.walking = false; g.pushing = true;
    CHECK(run(g) == "push");
    g.pushing = false;
    g.onFoot = false; g.manual = true;
    CHECK(run(g) == "man");
    // stance of the ground states: a fakie-only clip plays only in fakie
    Clip fk = mk("ride_fakie", ST_Riding);
    fk.stances = 1u << STANCE_Fakie; fk.priority = 1;
    set.clips.push_back(fk);
    set.generation = 2;
    g = GameState{};
    g.riding = true;
    CHECK(run(g) == "ride");
    g.groundStance = STANCE_Fakie;
    CHECK(run(g) == "ride_fakie");
}

TEST(lot4_short_states_keep_short_recordings) {
    FigureRecorder rec;
    rec.Reset(2, {});
    Quat q[2] = {QIdent, QIdent};
    GameState g{};
    g.crouch = true; g.figId = 7; g.figWanted = true; g.figShort = true;
    for (int i = 0; i < 30; i++) rec.Tick(g, q, nullptr, 2, 1.f / 30.f);   // 1 s of crouch
    g = GameState{}; g.riding = true; g.figId = 8;                          // then riding
    rec.Tick(g, q, nullptr, 2, 1.f / 30.f);
    FigureRec out;
    int id = 0;
    CHECK(rec.TakeFinished(out, id) && id == 7 && out.times.size() >= 16);
    // a long state (riding) still needs its 3 s
    FigureRecorder r2;
    r2.Reset(2, {});
    g = GameState{}; g.riding = true; g.figId = 8; g.figWanted = true;
    for (int i = 0; i < 30; i++) r2.Tick(g, q, nullptr, 2, 1.f / 30.f);
    g.figId = 9;
    r2.Tick(g, q, nullptr, 2, 1.f / 30.f);
    CHECK(!r2.TakeFinished(out, id));
}

// the first pose right after another recording (landing -> on foot) is recorded too, once the recorder is free
TEST(lot4_recorder_starts_after_previous_taken) {
    FigureRecorder rec;
    rec.Reset(2, {});
    Quat q[2] = {QIdent, QIdent};
    GameState g{};
    g.landing = true; g.figId = 3; g.figWanted = true; g.figShort = true;
    for (int i = 0; i < 24; i++) rec.Tick(g, q, nullptr, 2, 1.f / 30.f);
    g = GameState{}; g.onFoot = true; g.figId = 4; g.figWanted = true;
    rec.Tick(g, q, nullptr, 2, 1.f / 30.f);   // the landing ends here; not taken yet
    rec.Tick(g, q, nullptr, 2, 1.f / 30.f);
    FigureRec out;
    int id = 0;
    CHECK(rec.TakeFinished(out, id) && id == 3);
    for (int i = 0; i < 100; i++) rec.Tick(g, q, nullptr, 2, 1.f / 30.f);
    CHECK(rec.TakeFinished(out, id) && id == 4 && out.times.size() == 91);
}
