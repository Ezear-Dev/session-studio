// Lot 5 (editor): landing part of a trick clip, key interpolation, grouped timeline rows and bone names.
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <algorithm>

namespace {
std::string Skel5() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// a kickflip figure: 0.8 s in the air (phases 0..4), then `tail` frames on the ground (phase 5)
std::string Fig5(int tail) {
    FigureRec f; f.figure = "KickFlip_RGS_regular"; f.bones = (int)RefNames().size();
    const int air = 24;
    for (int i = 0; i < air + tail; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        f.phases.push_back(i >= air ? FigureRecorder::kGroundPhase : i < 1 ? 0 : i < 6 ? 1 : i < 14 ? 2 : i < 18 ? 3 : 4);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QFromRotatorDeg(i * 1.f, (float)(b % 9), 0.f));
    }
    return FigureToJson(f);
}
int BoneNamed(const EditorDoc& d, const char* n) {
    for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == n) return b;
    return -1;
}
const FigureVariant kKF{ST_Trick, "KickFlip", STANCE_Normal, false};
}

TEST(lot5_bone_labels) {
    CHECK(BoneLabel("AMXX_L_Forearm") == "L Forearm");
    CHECK(BoneLabel("AMXX_R_UpperArm") == "R Arm");
    CHECK(BoneLabel("AMXX_R_Clavicle") == "R Shoulder");
    CHECK(BoneLabel("AMXX_Spine3") == "Upper back");
    CHECK(BoneLabel("AMXX_Head") == "Head");
    CHECK(BoneLabel("AMXX_L_Finger21") == "L Middle 2");
    CHECK(BoneLabel("AMXX_R_Finger0") == "R Thumb");
    CHECK(BoneLabel("AMXX_L_Hand_Xtra") == "L Hand (extra)");
    CHECK(BoneLabel("AMXX_R_Calf") == "R Calf");
    CHECK(BoneLabel("AMXX_L_Toe0") == "L Toes");
}

TEST(lot5_editor_new_trick_clip_is_the_air_part) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(Skel5(), err));
    CHECK(d.LoadFigure(Fig5(24), kKF, err));
    d.NewClip(kKF);
    CHECK_NEAR(d.GroundAt(), 24 / 30.f, 1e-4);
    CHECK_NEAR(d.GroundTail(), 23 / 30.f, 1e-4);
    CHECK_NEAR(d.clip().duration, 24 / 30.f, 1e-4);   // not the recorded tail
    CHECK(!d.HasOldLandingPart());
}

// 2026-11-25 landing sync (editor): markers saved with the trick clip, landing clips over the trick's ground tail
TEST(sync_editor_trick_clip_saves_its_markers) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(Skel5(), err));
    CHECK(d.LoadFigure(Fig5(24), kKF, err));
    d.NewClip(kKF);
    CHECK(d.SeedKeys() > 0);
    const TrickMarkers m = d.Markers();
    CHECK(m.ok);
    CHECK_NEAR(m.air, 1 / 30.0, 1e-4);
    CHECK_NEAR(m.catchT, 14 / 30.0, 1e-4);
    CHECK_NEAR(m.land, 18 / 30.0, 1e-4);
    CHECK_NEAR(m.ground, 24 / 30.0, 1e-4);
    const std::string text = d.SaveText(err);
    Clip back;
    CHECK(!text.empty() && ParseClip(text, "kf", back, err));
    CHECK(back.markers.ok && std::fabs(back.markers.land - 18 / 30.f) < 1e-4f && std::fabs(back.markers.ground - 24 / 30.f) < 1e-4f);
    // a clip cut before the ground: the markers stop at its end
    d.clip().duration = 0.5f;
    for (auto& tr : d.clip().tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.t > 0.5f; }), tr.keys.end());
    CHECK(ParseClip(d.SaveText(err), "kf", back, err) && back.markers.ground <= 0.5f + 1e-4f && back.markers.land <= back.markers.ground);
}

TEST(sync_editor_ground_tail_figure) {
    FigureRec f, tail;
    std::string err;
    CHECK(ParseFigure(Fig5(24), f, err));
    CHECK(GroundTailFigure(f, tail));
    CHECK(tail.times.size() == 24 && std::fabs(tail.times.front()) < 1e-6f && std::fabs(tail.times.back() - 23 / 30.f) < 1e-4f);
    CHECK(tail.frames.size() == (size_t)24 * f.bones);
    CHECK(std::fabs(QDot(tail.frames[0], f.frames[(size_t)24 * f.bones])) > 0.9999f);   // first frame = the ground contact
    CHECK(tail.phases.empty());
    FigureRec old;
    CHECK(ParseFigure(Fig5(0), old, err) && !GroundTailFigure(old, tail));   // old recording: no tail
}

TEST(sync_editor_landing_clip_of_a_trick) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(Skel5(), err));
    // the trick clip: a key on the left hand and the left calf, 30 deg at the ground
    CHECK(d.LoadFigure(Fig5(24), kKF, err));
    d.NewClip(kKF);
    const int hand = BoneNamed(d, "AMXX_L_Hand"), calf = BoneNamed(d, "AMXX_L_Calf");
    Clip trick = d.clip();
    trick.markers = d.Markers();
    for (const char* bn : {"AMXX_L_Hand", "AMXX_L_Calf"}) {
        Track t; t.bone = bn;
        t.keys.push_back(Key{0.f, QIdent, true});
        t.keys.push_back(Key{24 / 30.f, QFromRotatorDeg(0, 0, 30), true});
        trick.tracks.push_back(t);
    }
    // the landing variant shows the trick's tail
    FigureRec f, tail;
    CHECK(ParseFigure(Fig5(24), f, err) && GroundTailFigure(f, tail));
    FigureVariant lv = kKF; lv.state = ST_Landing;
    tail.figure = FigureKey(lv);
    CHECK(tail.figure == "Landing-KickFlip_RGS_regular");
    CHECK(d.LoadFigure(FigureToJson(tail), lv, err));
    d.NewClip(lv);
    CHECK(d.IsLandingClip() && !d.IsTrickClip());
    CHECK(d.clip().trick == "KickFlip" && !d.clip().loop);
    CHECK_NEAR(d.clip().duration, 23 / 30.f, 1e-4);
    CHECK_NEAR(d.clip().blendIn, 0.12, 1e-6);
    CHECK(d.IsEditable(calf));   // legs are editable in a landing clip
    CHECK(d.SeedLandingFromTrick(trick) == 4);
    const Track* th = d.TrackOf(hand);
    CHECK(th && th->keys.size() == 2);
    CHECK(th && std::fabs(QDot(th->keys[0].q, QFromRotatorDeg(0, 0, 30))) > 0.9999f);   // starts where the trick ends
    CHECK(th && std::fabs(th->keys[1].q.w) > 0.99999f);                              // ends on the game's pose (additive)
    CHECK(d.SeedLandingFromTrick(trick) == 0);   // keys already there: kept
    const std::string text = d.SaveText(err);
    Clip back;
    CHECK(!text.empty() && ParseClip(text, "l", back, err));
    CHECK(back.states == ST_Landing && back.trick == "KickFlip" && back.trickHash == TrickHash("KickFlip") && !back.markers.ok);
}

TEST(sync_editor_split_old_landing_part) {
    Clip c; c.name = "tre"; c.states = ST_Trick; c.trick = "Treflip"; c.trickHash = TrickHash("Treflip"); c.loop = false;
    c.stances = 1u << STANCE_Normal; c.feet = Feet::Regular; c.mirror = false; c.mode = Mode::Additive;
    c.groundAt = 0.8f; c.landing = 0.5f; c.duration = 1.3f;
    Track t; t.bone = "AMXX_L_Hand";
    t.keys.push_back(Key{0.f, QIdent, true});
    t.keys.push_back(Key{0.5f, QFromRotatorDeg(0, 0, 20), true});
    t.keys.push_back(Key{1.1f, QFromRotatorDeg(0, 0, 40), true});
    c.tracks.push_back(t);
    c.pelvis.push_back(PosKey{0.f, {0, 0, 0}, true});
    c.pelvis.push_back(PosKey{1.3f, {0, 0, -10}, true});
    Clip air, land;
    CHECK(SplitLandingPart(c, air, land));
    CHECK_NEAR(air.duration, 0.8, 1e-5);
    CHECK(air.landing == 0.f && air.groundAt == 0.f);
    CHECK(air.tracks.size() == 1 && air.tracks[0].keys.size() == 3 && std::fabs(air.tracks[0].keys.back().t - 0.8f) < 1e-4f);
    CHECK(std::fabs(QDot(air.tracks[0].keys.back().q, EvalTrack(t, 0.8f, 1.3f, false))) > 0.9999f);   // nothing moves at the cut
    CHECK(land.states == ST_Landing && land.trick == "Treflip" && land.stances == c.stances && land.feet == c.feet);
    CHECK_NEAR(land.duration, 0.5, 1e-5);
    CHECK(land.tracks.size() == 1 && land.tracks[0].keys.size() == 2 && std::fabs(land.tracks[0].keys[1].t - 0.3f) < 1e-4f);
    CHECK(land.pelvis.size() == 2 && std::fabs(land.pelvis[0].v[2] + 10.f * 0.8f / 1.3f) < 0.6f);
    std::string err; Clip back;
    CHECK(ParseClip(ClipToJson(air), "a", back, err));
    CHECK(ParseClip(ClipToJson(land), "l", back, err));
    Clip plain = air;
    CHECK(!SplitLandingPart(plain, air, land));   // no landing part
}

TEST(lot5_editor_key_interp) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(Skel5(), err));
    CHECK(d.LoadFigure(Fig5(24), kKF, err));
    d.NewClip(kKF);
    CHECK(d.SeedKeys() > 0);
    const int head = BoneNamed(d, "AMXX_Head"), hand = BoneNamed(d, "AMXX_L_Hand");
    CHECK(d.KeyInterp(0.f, head) == EditorDoc::IP_Smooth);
    d.SetKeyInterp(0.f, head, EditorDoc::IP_Step);
    CHECK(d.KeyInterp(0.f, head) == EditorDoc::IP_Step);
    CHECK(d.KeyInterp(0.f, hand) == EditorDoc::IP_Smooth);   // only that bone
    d.SetKeyInterp(0.f, -1, EditorDoc::IP_Linear);
    CHECK(d.KeyInterp(0.f, head) == EditorDoc::IP_Linear && d.KeyInterp(0.f, hand) == EditorDoc::IP_Linear);
    d.SetKeySmooth(0.f, hand, true);   // old API still works
    CHECK(d.KeyInterp(0.f, hand) == EditorDoc::IP_Smooth);
    CHECK(d.KeyInterp(0.33f, hand) == -1);
}

TEST(lot5_editor_key_groups) {
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(Skel5(), err));
    CHECK(d.LoadFigure(Fig5(24), kKF, err));
    d.NewClip(kKF);
    CHECK(d.KeyGroups(false).empty());   // no track yet
    const auto all = d.KeyGroups(true);
    std::vector<std::string> ids;
    for (auto& g : all) ids.push_back(g.id);
    for (const char* id : {"head", "chest", "armL", "handL", "armR", "handR", "legL", "legR", "pelvis"})
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
    for (auto& g : all) {
        if (g.id == "handL") {   // the hand + one row per finger (3 segments each)
            int fingers = 0;
            for (auto& r : g.children) if (r.bones.size() == 3) fingers++;
            CHECK(fingers == 5);
            CHECK(std::find_if(g.children.begin(), g.children.end(), [](const EditorDoc::KeyRow& r) { return r.label == "L Index"; }) != g.children.end());
        }
        if (g.id == "armR") {
            CHECK(g.children.size() == 3);
            if (g.children.size() == 3) CHECK(g.children[0].label == "R Shoulder" && g.children[1].label == "R Arm" && g.children[2].label == "R Forearm");
        }
        if (g.id == "pelvis") CHECK(g.children.empty() && g.label == "Pelvis");
    }
    CHECK(d.GroupOfBone(BoneNamed(d, "AMXX_R_Forearm")) == "armR");
    CHECK(d.GroupOfBone(BoneNamed(d, "AMXX_L_Finger32")) == "handL");
    CHECK(d.GroupOfBone(BoneNamed(d, "AMXX_Spine2")) == "chest");
    CHECK(d.GroupOfBone(d.PelvisBone()) == "pelvis");
    CHECK(d.GroupOfBone(BoneNamed(d, "SKXX_SkateSkel_Root")) == "");
    // with keys: only the groups that hold them
    CHECK(d.SeedKeys() > 0);
    const auto keyed = d.KeyGroups(false);
    CHECK(!keyed.empty());
    bool pel = false;
    for (auto& g : keyed) if (g.id == "pelvis") pel = true;
    CHECK(!pel);   // no pelvis keys
}

TEST(sync_editor_convert_for_landing_sync) {
    TrickMarkers m; m.ok = true; m.air = 0.167f; m.catchT = 0.5f; m.land = 0.567f; m.ground = 0.633f;
    Clip c; c.name = "kf"; c.states = ST_Trick; c.trick = "KickFlip"; c.trickHash = TrickHash("KickFlip"); c.loop = false;
    c.duration = 0.767f; c.mode = Mode::Additive;
    Track t; t.bone = "AMXX_L_Hand";
    t.keys.push_back(Key{0.f, QIdent, true});
    t.keys.push_back(Key{0.4f, QFromRotatorDeg(0, 0, 20), true});
    t.keys.push_back(Key{0.767f, QFromRotatorDeg(0, 0, 35), true});
    c.tracks.push_back(t);
    Clip air, land; bool hasLand = false;
    CHECK(ConvertForLandingSync(c, m, air, land, hasLand));
    CHECK(hasLand && air.markers.ok && std::fabs(air.duration - 0.633f) < 1e-4f && std::fabs(air.markers.ground - 0.633f) < 1e-4f);
    CHECK(land.states == ST_Landing && land.trick == "KickFlip" && std::fabs(land.duration - 0.134f) < 1e-3f);
    Clip again, land2; bool h2 = false;
    CHECK(!ConvertForLandingSync(air, m, again, land2, h2));   // already converted: nothing to do
    // a clip that ends at the ground (Ollie 0.9 / 0.9): markers only
    Clip o = c; o.duration = 0.633f; o.tracks[0].keys.pop_back();
    CHECK(ConvertForLandingSync(o, m, air, land, hasLand) && !hasLand && air.markers.ok);
    Clip noTrick; noTrick.states = ST_Riding;
    CHECK(!ConvertForLandingSync(noTrick, m, air, land, hasLand));
}
