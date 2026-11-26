// 2026-11-25: dismount part of a riding clip, distance-based fades between base poses.
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/player.h"
#include "../src/skeleton.h"
#include "../src/trick.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <memory>

namespace {
constexpr float kDt = 1.f / 60.f;
float Deg(const Quat& q) { const float w = std::fabs(q.w) > 1.f ? 1.f : std::fabs(q.w); return 2.f * std::acos(w) * 57.29578f; }
Track T1(std::initializer_list<std::pair<float, float>> keys) {
    Track t; t.bone = "b"; t.index = 1;
    for (auto& k : keys) t.keys.push_back(Key{k.first, QFromRotatorDeg(0, 0, k.second), false});
    return t;
}
Clip Ride(float dismount) {   // loop 1 s at 20 deg; dismount part from 20 to 0 deg
    Clip c; c.file = "ride"; c.name = "ride"; c.states = ST_Riding; c.loop = true; c.mirror = false; c.blendIn = 0.f;
    c.duration = 1.f + dismount; c.dismount = dismount;
    if (dismount > 0.f) c.tracks.push_back(T1({{0.f, 20.f}, {1.f, 20.f}, {1.f + dismount, 0.f}}));
    else c.tracks.push_back(T1({{0.f, 20.f}}));
    return c;
}
Clip Foot(float deg) { Clip c; c.file = "foot"; c.name = "foot"; c.states = ST_OnFoot; c.loop = true; c.mirror = false; c.blendIn = 0.f; c.duration = 1.f; c.tracks.push_back(T1({{0.f, deg}})); return c; }
struct R2 {
    std::unique_ptr<Player> p = std::make_unique<Player>();
    ClipSet set; float deg = 0.f;
    R2() { const int par[2] = {-1, 0}; p->SetRig(par, 2, MirrorMap{}); set.generation = 1; }
    std::string Step(const GameState& g) {
        FTr cs[2] = {FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0}, FTr{0, 0, 0, 1, 10, 0, 0, 0, 1, 1, 1, 0}};
        p->Apply(cs, 2, &set, g, kDt);
        deg = Deg(TrQ(cs[1]));
        return p->CurrentClipName();
    }
};
}

TEST(dismount_clip_format_and_eval) {
    Clip c = Ride(0.5f);
    const std::string js = ClipToJson(c);
    CHECK(js.find("\"dismount\"") != std::string::npos);
    Clip back; std::string err;
    CHECK(ParseClip(js, "ride", back, err) && std::fabs(back.dismount - 0.5f) < 1e-6f);
    c.dismount = 1.5f;   // longer than the clip
    CHECK(!ParseClip(ClipToJson(c), "ride", back, err));
    c = Ride(0.5f);
    CHECK_NEAR(LoopLength(c), 1.0, 1e-6);
    CHECK_NEAR(Deg(EvalClipTrack(c, c.tracks[0], 0.5f, true)), 20.0, 0.01);    // the loop
    CHECK_NEAR(Deg(EvalClipTrack(c, c.tracks[0], 1.25f, true)), 10.0, 0.05);   // half the dismount part
    CHECK_NEAR(Deg(EvalClipTrack(c, c.tracks[0], 1.5f, true)), 0.0, 0.01);
    CHECK_NEAR(ClipTime(c, 1.2f, 0.f), 0.2, 1e-4);                              // the loop wraps on its own length
}

TEST(dismount_part_plays_once_when_the_feet_leave_the_board) {
    R2 r;
    r.set.clips.push_back(Ride(0.6f));
    r.set.clips.push_back(Foot(0.f));
    GameState g{}; g.riding = true;
    for (int i = 0; i < 30; i++) r.Step(g);
    CHECK_NEAR(r.deg, 20.0, 0.2);
    g = GameState{}; g.onFoot = true; g.dismount = true;
    float prev = r.deg, worst = 0.f;
    std::string name;
    int framesRide = 0;
    for (int i = 0; i < 60; i++) {   // 1 s off the board
        if (i == 50) g.dismount = false;   // the window closes
        name = r.Step(g);
        if (name == "ride") framesRide++;
        worst = std::fmax(worst, std::fabs(r.deg - prev));
        prev = r.deg;
    }
    CHECK(framesRide >= 36 && framesRide <= 46);   // the 0.6 s dismount part (+ the 0.1 s debounce before the next clip)
    CHECK(name == "foot");
    CHECK(worst < 1.5f);                           // smooth: 20 deg over 0.6 s
    CHECK_NEAR(r.deg, 0.0, 0.5);
}

TEST(dismount_needs_the_part_otherwise_normal_switch) {
    R2 r;
    r.set.clips.push_back(Ride(0.f));
    r.set.clips.push_back(Foot(20.f));
    GameState g{}; g.riding = true;
    for (int i = 0; i < 30; i++) r.Step(g);
    g = GameState{}; g.onFoot = true; g.dismount = true;
    std::string name;
    for (int i = 0; i < 20; i++) name = r.Step(g);
    CHECK(name == "foot");
}

TEST(distance_fade_between_base_poses) {
    R2 r;
    r.set.clips.push_back(Ride(0.f));   // 20 deg
    Clip f = Foot(-70.f);               // 90 deg away
    r.set.clips.push_back(f);
    GameState g{}; g.riding = true;
    for (int i = 0; i < 30; i++) r.Step(g);
    g = GameState{}; g.onFoot = true;
    float prev = r.deg, worst = 0.f;
    for (int i = 0; i < 60; i++) {
        r.Step(g);
        worst = std::fmax(worst, std::fabs(r.deg - prev));
        prev = r.deg;
    }
    CHECK_NEAR(r.deg, 70.0, 0.5);
    // 90 deg at 180 deg/s = 0.5 s: mean 3 deg / frame, eased peak about 4.7 (the clip's own 0.12 s would peak near 20)
    CHECK(worst < 6.f);
}

namespace {
std::string SkelD() { SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv; return SkeletonToJson(s); }
std::string FigD(const char* key, float roll, int frames) {
    FigureRec f; f.figure = key; f.bones = (int)RefNames().size();
    for (int i = 0; i < frames; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QFromRotatorDeg(0, 0, roll));
    }
    return FigureToJson(f);
}
}

TEST(dismount_editor_part) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(SkelD(), err));
    const FigureVariant v{ST_Riding, "", STANCE_Normal, false};
    CHECK(d.LoadFigure(FigD("Riding_RGS_regular", 0.f, 91), v, err));
    d.NewClip(v);
    const int head = [&] { for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == "AMXX_Head") return b; return -1; }();
    CHECK(d.SetPending(head, QFromRotatorDeg(0, 0, 30)));
    d.CommitKey(0.f);
    CHECK(d.CanDismount() && !d.HasDismount());
    CHECK(!d.SetDismountPart(true, 0.8f));   // no dismount recorded yet
    CHECK(d.LoadDismountFigure(FigD("Dismount_RGS_regular", 10.f, 31), err));
    const float loop = d.clip().duration;
    CHECK(d.SetDismountPart(true, 0.8f));
    CHECK(d.HasDismount());
    CHECK_NEAR(d.clip().duration, loop + 0.8f, 1e-4);
    CHECK_NEAR(LoopLength(d.clip()), loop, 1e-4);
    // start of the part = the loop's offset (additive: on top of the dismount figure); end = the game's dismount pose
    CHECK(Deg(QMul(QInv(QMul(QInv(d.OriginForClip(head, 0.f)), d.Edited(head, 0.f))), QMul(QInv(d.OriginForClip(head, loop)), d.Edited(head, loop)))) < 0.5f);
    CHECK(Deg(QMul(QInv(d.OriginForClip(head, loop + 0.8f)), d.Edited(head, loop + 0.8f))) < 0.5f);
    CHECK(Deg(QMul(QInv(QFromRotatorDeg(0, 0, 10)), d.OriginForClip(head, loop + 0.4f))) < 0.5f);   // shows the dismount figure
    CHECK(d.KeyTime(loop + 0.4f) > loop);   // keys in the part are not wrapped
    std::string text = d.SaveText(err);
    CHECK(!text.empty());
    Clip back;
    CHECK(ParseClip(text, "r", back, err) && back.dismount > 0.f);
    CHECK(d.SetDismountPart(false, 0.f));
    CHECK(!d.HasDismount());
    CHECK_NEAR(d.clip().duration, loop, 1e-4);
    for (float t : d.KeyTimes(-1)) CHECK(t < loop);
}
