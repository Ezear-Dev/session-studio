// 2026-11-26: a trick's landing clip may last up to 2 s in game (it was cut at 0.8 s), and the recording keeps 2 s
// on the ground after a trick so the editor can show it.
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/landing_hold.h"
#include "../src/player.h"
#include "../src/trick.h"

namespace {
Clip Land(const char* trick, float dur, uint8_t stances = 0xFF) {
    Clip c;
    c.name = std::string("land_") + trick;
    c.enabled = true;
    c.states = ST_Landing;
    c.trick = trick;
    c.trickHash = trick[0] ? TrickHash(trick) : 0u;
    c.stances = stances;
    c.feet = Feet::Any;
    c.loop = false;
    c.duration = dur;
    return c;
}
GameState AfterTrick(const char* trick) {
    GameState g{false, false, false, false, 0.f};
    g.landTrickHash = TrickHash(trick);
    g.landStance = STANCE_Normal;
    g.groundStance = STANCE_Normal;
    return g;
}
} // namespace

TEST(landhold_length_follows_the_clip) {
    CHECK_NEAR(LandingHold(0.0), kLandHoldMin, 1e-9);    // no landing clip: as before
    CHECK_NEAR(LandingHold(0.5), kLandHoldMin, 1e-9);    // a short clip: never shorter than before
    CHECK_NEAR(LandingHold(1.5), 1.5, 1e-9);
    CHECK_NEAR(LandingHold(3.0), kLandHoldMax, 1e-9);    // capped at 2 s
    CHECK_NEAR(LandingHold(-1.0), kLandHoldMin, 1e-9);
    CHECK(kLandHoldMax >= 2.0 - 1e-9);
}

TEST(landhold_window_before_the_minimum_is_unchanged) {
    LandingWindow w;
    CHECK(!w.Active(0.0, 1.5, false));        // never started
    w.Start(10.0);
    CHECK(w.Active(10.1, 1.5, false));
    CHECK(w.Active(10.3, 1.5, true));         // another action before 0.8 s: the window stays (caller decides, as before)
    CHECK(w.Active(10.5, 1.5, false));        // and the landing comes back after it
    CHECK(w.Active(10.79, kLandHoldMin, false));
    CHECK(!w.Active(10.81, kLandHoldMin, false));   // a 0.8 s hold ends at 0.8 s, as before
}

TEST(landhold_window_long_clip_and_interruption) {
    LandingWindow w;
    w.Start(0.0);
    CHECK(w.Active(1.2, 1.5, false));         // past 0.8 s: still on with a 1.5 s clip
    CHECK(w.Active(1.49, 1.5, false));
    CHECK(!w.Active(1.51, 1.5, false));       // ends with the clip
    w.Start(5.0);
    CHECK(w.Active(5.9, 2.0, false));
    CHECK(!w.Active(6.0, 2.0, true));         // a push (any other action) past 0.8 s stops it...
    CHECK(!w.Active(6.1, 2.0, false));        // ...for good
    w.Start(8.0);
    CHECK(w.Active(8.1, 2.0, false));         // a new landing starts again
}

TEST(landhold_player_landing_clip_length) {
    ClipSet set;
    set.clips.push_back(Land("KickFlip", 1.6f));
    set.clips.push_back(Land("Ollie", 0.5f));
    Player p;
    CHECK_NEAR(p.LandingClipLength(&set, AfterTrick("KickFlip")), 1.6, 1e-6);
    CHECK_NEAR(p.LandingClipLength(&set, AfterTrick("Ollie")), 0.5, 1e-6);
    CHECK_NEAR(p.LandingClipLength(&set, AfterTrick("HeelFlip")), 0.0, 1e-6);   // no landing clip of that trick
    CHECK_NEAR(p.LandingClipLength(nullptr, AfterTrick("KickFlip")), 0.0, 1e-6);
    set.clips[0].enabled = false;
    CHECK_NEAR(p.LandingClipLength(&set, AfterTrick("KickFlip")), 0.0, 1e-6);   // disabled: does not count
    set.clips.push_back(Land("", 1.2f));                                        // a landing clip for every landing
    CHECK_NEAR(p.LandingClipLength(&set, AfterTrick("KickFlip")), 1.2, 1e-6);
    set.clips.clear();
    set.clips.push_back(Land("KickFlip", 1.6f, (uint8_t)(1u << STANCE_Switch)));   // switch only
    CHECK_NEAR(p.LandingClipLength(&set, AfterTrick("KickFlip")), 0.0, 1e-6);
    GameState g = AfterTrick("KickFlip");
    g.landStance = STANCE_Switch;
    CHECK_NEAR(p.LandingClipLength(&set, g), 1.6, 1e-6);
}

namespace {
constexpr int kBones = 4;
// a trick in the air for `air` s, then on the ground until `total` s (60 frames a second)
int RecordTrick(FigureRecorder& r, float air, float total) {
    std::vector<Quat> q(kBones, QIdent);
    for (int i = 0; i < (int)(total * 60.f); i++) {
        const float t = (float)i / 60.f;
        GameState g{false, false, false, false, 0.f};
        g.kickflip = t < air; g.popSerial = 1; g.trickPhase = 2;
        g.figId = 3; g.figWanted = true;
        r.Tick(g, q.data(), nullptr, kBones, 1.f / 60.f);
    }
    FigureRec out; int id = 0;
    if (!r.TakeFinished(out, id)) return -1;
    int tail = 0;
    for (int ph : out.phases) if (ph == FigureRecorder::kGroundPhase) tail++;
    return tail * 1000 + (int)out.times.size();
}
} // namespace

TEST(landhold_recorder_keeps_2s_on_the_ground) {
    CHECK(FigureRecorder::kGroundTail >= 60);   // 2 s at 30 frames a second
    FigureRecorder r; r.Reset(kBones);
    const int got = RecordTrick(r, 0.8f, 3.5f);
    CHECK(got > 0);
    const int tail = got / 1000, frames = got % 1000;
    CHECK(tail == FigureRecorder::kGroundTail);
    CHECK(frames >= 24 + FigureRecorder::kGroundTail && frames <= 26 + FigureRecorder::kGroundTail);
}

TEST(landhold_recorder_long_trick_and_full_tail_fit) {
    FigureRecorder r; r.Reset(kBones);
    const int got = RecordTrick(r, 1.9f, 4.5f);   // nearly 2 s in the air, then the whole ground tail
    CHECK(got > 0);
    CHECK(got / 1000 == FigureRecorder::kGroundTail);
    CHECK(got % 1000 <= FigureRecorder::kKickMax + FigureRecorder::kGroundTail);
}
