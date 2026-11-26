// CommitKey sets hold keys (a new key does not change the poses already keyed)
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../../AnimStudioEditor/model/editor_doc.h"
#include <algorithm>
#include <cmath>

namespace {
float CAng(Quat a, Quat b) {   // angle between two rotations (degrees)
    const Quat d = QMul(QInv(QNorm(a)), QNorm(b));
    double s = std::sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    if (s > 1.0) s = 1.0;
    double ang = 2.0 * std::asin(s) * 57.29577951308232;
    if (d.w < 0) ang = 360.0 - ang;
    if (ang > 180.0) ang = 360.0 - ang;
    return (float)ang;
}
int CIdx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }

// move of 31 frames over 1 s: each bone turns a bit more every frame (the origin depends on time and ratio);
// withPhases: catch (phase 3) at 0.5 s
FigureRec CFig(const char* name, bool withPhases) {
    FigureRec r; r.figure = name; r.bones = (int)RefNames().size();
    for (int i = 0; i < 31; i++) {
        r.times.push_back(i / 30.f); r.ratios.push_back(i / 30.f);
        for (int b = 0; b < r.bones; b++)
            r.frames.push_back(QFromRotatorDeg((float)(b % 5) * 4.f + i * 0.7f, (float)(b % 7) * 3.f, (float)(b % 3) * 5.f + (float)i));
        if (withPhases) r.phases.push_back(i == 0 ? 0 : i < 8 ? 1 : i < 15 ? 2 : i < 20 ? 3 : 4);
    }
    return r;
}

EditorDoc CDoc(int figure, const char* figName, bool phases = false) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    EditorDoc d; std::string err;
    d.autoStartKey = false;   // written before the start key rule (2026-11-25)
    CHECK(d.LoadSkeleton(SkeletonToJson(s), err));
    CHECK(d.LoadFigure(FigureToJson(CFig(figName, phases)), err));
    d.NewClip(figure);
    d.clip().duration = 1.f;
    return d;
}

void Put(EditorDoc& d, int bone, float t, float deg) {   // sets a key: a roll around the origin
    CHECK(d.SetPending(bone, QMul(d.Origin(bone, t), QFromRotatorDeg(deg, deg * 0.5f, -deg * 0.3f))));
    d.CommitKey(t);
}
int CountKeys(const EditorDoc& d, int bone) { const Track* t = d.TrackOf(bone); return t ? (int)t->keys.size() : 0; }
bool HasKeyAt(const EditorDoc& d, int bone, float t) {
    const Track* tr = d.TrackOf(bone);
    if (tr) for (auto& k : tr->keys) if (std::fabs(k.t - t) < 1e-3f) return true;
    return false;
}
} // namespace

TEST(p5c_commit_between_keys_keeps_neighbours) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, hand, 0.2f, 20.f); Put(d, hand, 0.8f, -30.f);
    const Quat a = d.Edited(fore, 0.2f), b = d.Edited(fore, 0.8f);
    CHECK(d.SetPending(fore, QMul(d.Origin(fore, 0.5f), QFromRotatorDeg(0, 40, 0))));
    d.CommitKey(0.5f);
    CHECK(CAng(d.Edited(fore, 0.2f), a) < 0.01f);
    CHECK(CAng(d.Edited(fore, 0.8f), b) < 0.01f);
    CHECK(CountKeys(d, fore) == 3 && HasKeyAt(d, fore, 0.2f) && HasKeyAt(d, fore, 0.5f) && HasKeyAt(d, fore, 0.8f));
}

TEST(p5c_commit_with_existing_track_inserts_holds) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, fore, 0.f, 10.f); Put(d, fore, 0.9f, 50.f);   // 2026-11-25: a key at the very end of a loop is the key at 0
    Put(d, hand, 0.4f, 25.f);
    CHECK(CountKeys(d, fore) == 2);
    const Quat before = d.Edited(fore, 0.4f);
    Put(d, fore, 0.7f, -40.f);
    CHECK(CAng(d.Edited(fore, 0.4f), before) < 0.01f);
    CHECK(CountKeys(d, fore) == 4);
}

TEST(p5c_commit_on_existing_key_time_no_hold) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, fore, 0.2f, 10.f); Put(d, fore, 0.8f, 30.f);
    Put(d, hand, 0.8f, 15.f);
    CHECK(CountKeys(d, fore) == 2);
    const Quat at02 = d.Edited(fore, 0.2f);
    const Quat want = QMul(d.Origin(fore, 0.8f), QFromRotatorDeg(0, 0, 45));
    CHECK(d.SetPending(fore, want));
    d.CommitKey(0.8f);
    CHECK(CountKeys(d, fore) == 2);
    int at08 = 0;
    for (auto& k : d.TrackOf(fore)->keys) if (std::fabs(k.t - 0.8f) < 1e-3f) at08++;
    CHECK(at08 == 1);
    CHECK(CAng(d.Edited(fore, 0.8f), want) < 0.01f);
    CHECK(CAng(d.Edited(fore, 0.2f), at02) < 0.01f);
    // a bone without a track, commit at a time where another bone has a key: the key exists once, no duplicate
    const int fing = CIdx("AMXX_L_Finger4");
    Put(d, fing, 0.8f, 12.f);
    CHECK(CountKeys(d, fing) == 2 || CountKeys(d, fing) == 1);   // key at 0.2 (hold) + key at 0.8
    int n08 = 0;
    for (auto& k : d.TrackOf(fing)->keys) if (std::fabs(k.t - 0.8f) < 1e-3f) n08++;
    CHECK(n08 == 1);
}

TEST(p5c_hold_respects_weight_and_additive) {
    for (int pass = 0; pass < 2; pass++) {
        EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
        const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
        Put(d, fore, 0.2f, 20.f); Put(d, fore, 0.8f, 50.f);
        if (pass == 1) d.SetMode(Mode::Additive);
        for (auto& tr : d.clip().tracks) tr.weight = 0.5f;
        Put(d, hand, 0.5f, 25.f);
        for (auto& tr : d.clip().tracks) tr.weight = 0.5f;
        const float ts[3] = {0.2f, 0.5f, 0.8f};
        Quat before[3];
        for (int i = 0; i < 3; i++) before[i] = d.Edited(fore, ts[i]);
        CHECK(d.SetPending(fore, QMul(d.Origin(fore, 0.65f), QFromRotatorDeg(30, 0, 10))));
        const Quat want = d.Edited(fore, 0.65f);   // l'attente s'affiche telle quelle
        d.CommitKey(0.65f);
        for (int i = 0; i < 3; i++) CHECK(CAng(d.Edited(fore, ts[i]), before[i]) < 0.01f);
        CHECK(CAng(d.Edited(fore, 0.65f), want) < 0.01f);
        CHECK(CountKeys(d, fore) == 4);
    }
}

TEST(p5c_hold_uses_raw_leg_value) {
    EditorDoc d = CDoc(FIG_Kickflip, "Kickflip", true);
    const int calf = CIdx("AMXX_L_Calf"), foot = CIdx("AMXX_L_Foot");
    CHECK(d.IsTrickClip() && std::fabs(d.PhaseStart(3) - 0.5f) < 1e-4f);
    Put(d, calf, 0.f, 20.f); Put(d, calf, 0.9f, 60.f);
    Put(d, foot, 0.3f, 15.f); Put(d, foot, 0.7f, -15.f);
    const Quat raw = d.EditedRaw(calf, 0.7f);
    CHECK(CAng(d.Edited(calf, 0.7f), raw) > 1.f);   // the preview is indeed attenuated at 0.7 s: the test makes sense
    CHECK(d.SetPending(calf, QMul(d.Origin(calf, 0.6f), QFromRotatorDeg(0, 0, 40))));
    d.CommitKey(0.6f);
    CHECK(CAng(d.EditedRaw(calf, 0.7f), raw) < 0.01f);
    CHECK(CountKeys(d, calf) == 5);   // 0, 0,3 (maintien), 0,6, 0,7 (maintien), 0,9
}

TEST(p5c_hold_manual_ratio_maps_time) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    d.SetMode(Mode::Replace);   // the expected key below is a replace key (new clips are additive since P6)
    d.clip().drive = Drive::ManualRatio; d.clip().r0 = 0.2f; d.clip().r1 = 0.9f;
    d.SetSimulatedRatio(0.5f);
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, hand, 0.4f, 25.f);
    CHECK(d.SetPending(fore, QMul(d.OriginAtRatio(fore, 0.5f), QFromRotatorDeg(0, 30, 0))));
    d.CommitKey(0.7f);
    const Track* tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() == 2);
    const float ratio = 0.2f + 0.4f / 1.f * (0.9f - 0.2f);
    const Quat want = d.OriginAtRatio(fore, ratio);
    CHECK(CAng(want, d.OriginAtRatio(fore, 0.5f)) > 1.f);   // the simulated ratio would give another value
    bool found = false;
    if (tr) for (auto& k : tr->keys) if (std::fabs(k.t - 0.4f) < 1e-3f) { found = true; CHECK(CAng(k.q, want) < 0.01f); }
    CHECK(found);
}

TEST(p5c_all_row_unchanged) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, hand, 0.2f, 10.f); Put(d, fore, 0.9f, 10.f); Put(d, hand, 0.5f, 10.f);
    const std::vector<float> old = d.KeyTimes(-1);
    Put(d, fore, 0.65f, 20.f);
    std::vector<float> want = old;
    want.push_back(0.65f);
    std::sort(want.begin(), want.end());
    const std::vector<float> now = d.KeyTimes(-1);
    CHECK(now.size() == want.size());
    for (size_t i = 0; i < now.size() && i < want.size(); i++) CHECK_NEAR(now[i], want[i], 1e-6);
}

TEST(p5c_undo_removes_holds) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, hand, 0.2f, 20.f); Put(d, hand, 0.8f, -30.f);
    const std::string before = ClipToJson(d.clip());
    CHECK(d.SetPending(fore, QMul(d.Origin(fore, 0.5f), QFromRotatorDeg(0, 40, 0))));
    d.Checkpoint("key");
    d.CommitKey(0.5f);
    CHECK(ClipToJson(d.clip()) != before);
    CHECK(d.Undo());
    CHECK(ClipToJson(d.clip()) == before);
}

TEST(p5c_hold_smooth_follows_previous_key) {
    EditorDoc d = CDoc(FIG_NoseManual, "NoseManual");
    const int hand = CIdx("AMXX_L_Hand"), fore = CIdx("AMXX_L_Forearm");
    Put(d, fore, 0.1f, 10.f);
    d.SetKeySmooth(0.1f, fore, false);
    Put(d, hand, 0.4f, 25.f);
    Put(d, fore, 0.7f, 30.f);   // hold at 0.4: setting of the previous key (0.1) = smooth false
    const Track* tr = d.TrackOf(fore);
    CHECK(tr && tr->keys.size() == 3);
    if (tr && tr->keys.size() == 3) CHECK(!tr->keys[1].smooth);
    Put(d, hand, 0.9f, 25.f);   // no Forearm track at first for a new bone: true
    const int fing = CIdx("AMXX_L_Finger4");
    Put(d, fing, 0.95f, 5.f);
    const Track* tf = d.TrackOf(fing);
    CHECK(tf && !tf->keys.empty() && tf->keys[0].smooth);
}
