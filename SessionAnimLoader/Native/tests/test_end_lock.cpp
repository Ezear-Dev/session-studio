// Editor: locked start and end of trick and landing clips (PLAN-EDITOR-3-ADDITIONS task 3).
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <algorithm>

namespace {
std::string SkelL() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// a kickflip figure: 0.8 s in the air (phases 0..4), then 0.8 s on the ground (phase 5)
FigureRec FigL() {
    FigureRec f; f.figure = "KickFlip_RGS_regular"; f.bones = (int)RefNames().size();
    for (int i = 0; i < 48; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        f.phases.push_back(i >= 24 ? FigureRecorder::kGroundPhase : i < 1 ? 0 : i < 6 ? 1 : i < 14 ? 2 : i < 18 ? 3 : 4);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QFromRotatorDeg(i * 1.f, (float)(b % 9), 0.f));
    }
    return f;
}
int B(const EditorDoc& d, const char* n) {
    for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == n) return b;
    return -1;
}
const FigureVariant kKF{ST_Trick, "KickFlip", STANCE_Normal, false};
const FigureVariant kLand{ST_Landing, "KickFlip", STANCE_Normal, false};

void Trick(EditorDoc& d) {
    std::string err;
    CHECK(d.LoadSkeleton(SkelL(), err));
    CHECK(d.LoadFigure(FigureToJson(FigL()), kKF, err));
    d.NewClip(kKF);
}
void Landing(EditorDoc& d) {
    std::string err;
    CHECK(d.LoadSkeleton(SkelL(), err));
    FigureRec tail;
    CHECK(GroundTailFigure(FigL(), tail));
    tail.figure = "Landing_KickFlip_RGS_regular";
    CHECK(d.LoadFigure(FigureToJson(tail), kLand, err));
    d.NewClip(kLand);
}
float Deg(const Quat& q) {
    const Quat n = QNorm(q);
    const float w = std::min(1.f, std::fabs(n.w));
    return 2.f * std::acos(w) * 57.29578f;
}
const Key* KeyAt(const EditorDoc& d, int bone, float t) {
    const Track* tr = d.TrackOf(bone);
    if (!tr) return nullptr;
    for (auto& k : tr->keys) if (std::fabs(k.t - t) < 1e-3f) return &k;
    return nullptr;
}
bool AtGame(const EditorDoc& d, int bone, float t) {   // a key there, at the game's pose
    const Key* k = KeyAt(d, bone, t);
    return k && Deg(k->q) < 0.01f;
}
bool PelAt(const EditorDoc& d, float t, float* x = nullptr) {
    for (auto& k : d.clip().pelvis) if (std::fabs(k.t - t) < 1e-3f) { if (x) *x = k.v[0]; return true; }
    return false;
}
// poses a bone (deg of pitch over what it shows) and keys it at t
void Pose(EditorDoc& d, int bone, float t, float deg) {
    CHECK(d.SetPending(bone, QMul(d.EditedRaw(bone, t), QFromRotatorDeg(deg, 0, 0))));
    d.CommitKey(t);
}
void PutKey(EditorDoc& d, int bone, float t, float deg) {   // raw key, no writer (the lock does not run)
    Track* tr = nullptr;
    for (auto& x : d.clip().tracks) if (x.bone == d.skeleton().names[bone]) tr = &x;
    if (!tr) { Track n; n.bone = d.skeleton().names[bone]; n.index = bone; d.clip().tracks.push_back(n); tr = &d.clip().tracks.back(); }
    tr->keys.erase(std::remove_if(tr->keys.begin(), tr->keys.end(), [&](const Key& k) { return std::fabs(k.t - t) < 1e-3f; }), tr->keys.end());
    tr->keys.push_back(Key{t, QFromRotatorDeg(deg, 0, 0), true});
    std::sort(tr->keys.begin(), tr->keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
}
} // namespace

TEST(endlock_rules_table) {
    EditorDoc d; Trick(d);
    CHECK(d.LockEnds() && d.LockAvailable());                  // new trick clip in Add mode: on
    CHECK(d.StartLocked() && d.EndLocked());                   // no landing for its variants
    CHECK(d.LockedAt(0.f) && d.LockedAt(d.clip().duration) && !d.LockedAt(0.4f));
    d.SetHasLandingCover(true);
    CHECK(d.StartLocked() && !d.EndLocked());                  // a landing for all its variants: end free
    EditorDoc l; Landing(l);
    CHECK(l.LockEnds() && !l.StartLocked() && l.EndLocked());  // landing: start continues the trick
    d.SetMode(Mode::Replace);                                  // Replace: lock inactive, flag kept
    CHECK(d.LockEnds() && !d.LockAvailable() && !d.StartLocked() && !d.EndLocked());
}

TEST(endlock_loops_and_off_clips_unaffected) {
    EditorDoc d;
    std::string err;
    CHECK(d.LoadSkeleton(SkelL(), err));
    d.NewClip(FigureVariant{ST_Manual, "", STANCE_Normal, false});
    CHECK(!d.LockEnds() && !d.StartLocked() && !d.EndLocked());
    const int arm = B(d, "AMXX_R_UpperArm");
    Pose(d, arm, 1.f, 30.f);
    CHECK(!KeyAt(d, arm, d.clip().duration));
    EditorDoc t; Trick(t);
    t.SetLockEnds(false);
    CHECK(!t.LockEnds() && !t.EndLocked());
    Pose(t, arm, 0.4f, 30.f);
    CHECK(!KeyAt(t, arm, t.clip().duration));                  // unlocked: no end key added
}

TEST(endlock_commit_keeps_ends_at_game_pose) {
    EditorDoc d; Trick(d);
    const float end = d.clip().duration;
    const int arm = B(d, "AMXX_R_UpperArm");
    Pose(d, arm, 0.4f, 30.f);
    CHECK(AtGame(d, arm, 0.f) && AtGame(d, arm, end));
    CHECK(KeyAt(d, arm, 0.4f) && Deg(KeyAt(d, arm, 0.4f)->q) > 10.f);
    Pose(d, arm, end, 40.f);                                   // posing the end: the lock wins
    CHECK(AtGame(d, arm, end));
    Pose(d, arm, 0.f, 40.f);
    CHECK(AtGame(d, arm, 0.f));
}

TEST(endlock_trick_end_leaves_the_legs) {
    EditorDoc d; Trick(d);
    const int thigh = B(d, "AMXX_L_Thigh");
    CHECK(thigh >= 0 && d.IsEditable(thigh));
    Pose(d, thigh, 0.2f, 20.f);                                // before the catch
    CHECK(AtGame(d, thigh, 0.f));                              // start: locked like the rest
    CHECK(!KeyAt(d, thigh, d.clip().duration));                // end: legs are the game's from the catch on
}

TEST(endlock_other_writers) {
    EditorDoc d; Trick(d);
    const float end = d.clip().duration;
    const int arm = B(d, "AMXX_R_UpperArm"), head = B(d, "AMXX_Head");
    Pose(d, arm, 0.4f, 30.f);
    // paste a pose on the end
    CHECK(d.CopyPose(0.4f, -1) > 0);
    d.Paste(end);
    CHECK(AtGame(d, arm, end));
    // move a key onto the end or away from the start: refused
    CHECK(!d.MoveKeys(0.4f, end));
    CHECK(!d.MoveKeys(0.f, 0.2f));
    CHECK(!d.MoveKeysBones({arm}, 0.4f, end));
    CHECK(d.MoveKeys(0.4f, 0.5f));
    // deleting the locked keys does not remove them
    d.DeleteKeysAt(end, -1);
    CHECK(AtGame(d, arm, end));
    d.DeleteKeysBones({arm}, 0.f);
    CHECK(AtGame(d, arm, 0.f));
    // keys written with no lock, then a writer: the locked instants are put back (a trick end within the limits;
    // a trick end posed beyond them stays as it is, see endlock_losing_landing_cover_erases_nothing)
    PutKey(d, head, end, 2.f);
    PutKey(d, head, 0.f, 25.f);
    PutKey(d, head, 0.3f, 25.f);
    d.DeleteKeysAt(0.6f, -1);                                  // any writer
    CHECK(AtGame(d, head, end) && AtGame(d, head, 0.f));
    // mirror: the other arm keyed, its ends locked too
    d.MirrorCopy(arm, 0.5f);
    d.CommitKey(0.5f);
    const int armL = B(d, "AMXX_L_UpperArm");
    CHECK(AtGame(d, armL, 0.f) && AtGame(d, armL, end));
    // copy to another variant (retarget, mirrored body): still locked
    d.RetargetVariant(FigureVariant{ST_Trick, "KickFlip", STANCE_Normal, true});
    CHECK(AtGame(d, arm, end) && AtGame(d, armL, end) && AtGame(d, arm, 0.f));
}

TEST(endlock_pelvis) {
    EditorDoc d; Trick(d);
    const float end = d.clip().duration;
    const float v[3] = {5.f, 0.f, 0.f};
    CHECK(d.SetPendingPelvis(v));
    d.CommitKey(0.4f);
    float x = 1.f;
    CHECK(PelAt(d, 0.f, &x) && std::fabs(x) < 1e-4f);
    x = 1.f;
    CHECK(PelAt(d, end, &x) && std::fabs(x) < 1e-4f);
    CHECK(PelAt(d, 0.4f, &x) && std::fabs(x - 5.f) < 1e-3f);
}

TEST(endlock_landing_start_untouched) {
    EditorDoc l; Landing(l);
    const float end = l.clip().duration;
    const int arm = B(l, "AMXX_R_UpperArm");
    PutKey(l, arm, 0.f, 35.f);                                 // the trick's end, taken by the landing start
    Pose(l, arm, 0.3f, 10.f);
    CHECK(KeyAt(l, arm, 0.f) && std::fabs(Deg(KeyAt(l, arm, 0.f)->q) - 35.f) < 0.1f);
    CHECK(AtGame(l, arm, end));
    // seeding from a trick: start = the trick's end, end = the game's pose
    EditorDoc t; Trick(t);
    Clip trick = t.clip();
    trick.tracks.clear();
    Track tr; tr.bone = "AMXX_R_UpperArm"; tr.index = arm;
    tr.keys = {Key{0.f, QIdent, true}, Key{trick.duration, QFromRotatorDeg(20, 0, 0), true}};
    trick.tracks.push_back(tr);
    EditorDoc l2; Landing(l2);
    CHECK(l2.SeedLandingFromTrick(trick, false) > 0);
    CHECK(KeyAt(l2, arm, 0.f) && Deg(KeyAt(l2, arm, 0.f)->q) > 10.f);
    CHECK(AtGame(l2, arm, l2.clip().duration));
}

TEST(endlock_length_change_moves_the_end_key) {
    EditorDoc d; Trick(d);
    const float end = d.clip().duration;
    const int arm = B(d, "AMXX_R_UpperArm");
    Pose(d, arm, 0.4f, 30.f);
    d.SetDuration(end + 0.3f);
    CHECK(std::fabs(d.clip().duration - (end + 0.3f)) < 1e-4f);
    CHECK(AtGame(d, arm, end + 0.3f) && !KeyAt(d, arm, end));   // no zero key left in the middle
    d.SetDuration(0.6f);
    CHECK(AtGame(d, arm, 0.6f) && !KeyAt(d, arm, end + 0.3f));
    CHECK(KeyAt(d, arm, 0.4f) && Deg(KeyAt(d, arm, 0.4f)->q) > 10.f);
    // unlocked: the length changes and no key moves
    d.SetLockEnds(false);
    d.SetDuration(0.9f);
    CHECK(AtGame(d, arm, 0.6f) && !KeyAt(d, arm, 0.9f));
}

TEST(endlock_file_field) {
    // an older clip (no field) loads unlocked and saves the same text
    EditorDoc t; Trick(t);
    t.SetLockEnds(false);
    Pose(t, B(t, "AMXX_R_UpperArm"), 0.4f, 30.f);
    Clip old = t.clip();
    old.extra.erase(std::remove_if(old.extra.begin(), old.extra.end(), [](const auto& kv) { return kv.first == "lockEnds"; }), old.extra.end());
    const std::string oldText = ClipToJson(old);
    CHECK(oldText.find("lockEnds") == std::string::npos);
    EditorDoc d;
    std::string err;
    CHECK(d.LoadSkeleton(SkelL(), err));
    CHECK(d.OpenClip(oldText, "old.sanim", err));
    CHECK(!d.LockEnds() && !d.EndLocked());
    const std::string s1 = d.SaveText(err);
    CHECK(!s1.empty() && s1.find("lockEnds") == std::string::npos);
    EditorDoc d2;
    CHECK(d2.LoadSkeleton(SkelL(), err) && d2.OpenClip(s1, "old.sanim", err));
    CHECK(d2.SaveText(err) == s1);
    // the flag survives save / reload, and the player's parser reads the clip
    d.SetLockEnds(true);
    const std::string s2 = d.SaveText(err);
    CHECK(!s2.empty() && s2.find("lockEnds") != std::string::npos);
    Clip c;
    CHECK(ParseClip(s2, "x.sanim", c, err));
    EditorDoc d3;
    CHECK(d3.LoadSkeleton(SkelL(), err) && d3.OpenClip(s2, "x.sanim", err));
    CHECK(d3.LockEnds() && d3.StartLocked() && d3.EndLocked());
}

TEST(endlock_turning_on_confirm_then_undo) {
    EditorDoc d; Trick(d);
    d.SetLockEnds(false);
    const float end = d.clip().duration;
    const int arm = B(d, "AMXX_R_UpperArm"), head = B(d, "AMXX_Head");
    PutKey(d, arm, 0.f, 15.f); PutKey(d, arm, 0.4f, 30.f); PutKey(d, arm, end, 25.f);
    PutKey(d, head, 0.f, 0.f); PutKey(d, head, end, 0.f);
    const auto prev = d.LockPreview();
    int starts = 0, ends = 0;
    for (auto& e : prev) {
        const bool hasArm = std::find(e.bones.begin(), e.bones.end(), arm) != e.bones.end();
        const bool hasHead = std::find(e.bones.begin(), e.bones.end(), head) != e.bones.end();
        CHECK(hasArm && !hasHead);
        if (e.kind == EditorDoc::EndIssue::StartNotGame) starts++; else ends++;
    }
    CHECK(starts == 1 && ends == 1);
    const Clip before = d.clip();
    d.SetLockEnds(true);
    CHECK(AtGame(d, arm, 0.f) && AtGame(d, arm, end));
    CHECK(d.LockPreview().empty());
    CHECK(d.Undo());
    CHECK(!d.LockEnds());
    CHECK(KeyAt(d, arm, end) && std::fabs(Deg(KeyAt(d, arm, end)->q) - 25.f) < 0.1f);
    CHECK(ClipToJson(d.clip()) == ClipToJson(before));
}

TEST(endlock_losing_landing_cover_erases_nothing) {
    EditorDoc d; Trick(d);
    d.SetHasLandingCover(true);
    const float end = d.clip().duration;
    const int arm = B(d, "AMXX_R_UpperArm");
    Pose(d, arm, end, 20.f);                                   // end free: posed
    CHECK(KeyAt(d, arm, end) && Deg(KeyAt(d, arm, end)->q) > 10.f);
    d.SetHasLandingCover(false);                               // the landing clip was disabled
    const std::string keep = ClipToJson(d.clip());
    CHECK(!d.EndLocked());                                     // still posed: not locked, the warning shows
    CHECK(d.EndIssues(nullptr, false).size() == 1u);
    Pose(d, arm, 0.4f, 10.f);                                  // another edit keeps that end
    CHECK(KeyAt(d, arm, end) && Deg(KeyAt(d, arm, end)->q) > 10.f);
    (void)keep;
    const auto iss = d.EndIssues(nullptr, false);
    CHECK(!iss.empty() && d.FixEndIssue(iss[0], nullptr));    // the fix button
    CHECK(d.EndLocked() && AtGame(d, arm, end));
}

TEST(endlock_seed_keys_on_a_locked_trick) {
    EditorDoc d; Trick(d);
    CHECK(d.SeedKeys() > 0);
    const int arm = B(d, "AMXX_R_UpperArm");
    CHECK(AtGame(d, arm, 0.f) && AtGame(d, arm, d.clip().duration));
}
