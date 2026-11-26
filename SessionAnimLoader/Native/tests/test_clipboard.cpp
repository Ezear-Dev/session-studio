// Editor: copy / paste of a pose and of keys, inside the open clip (PLAN-EDITOR-3-ADDITIONS task 1).
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <algorithm>
#include <array>

namespace {
std::string SkelC() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    return SkeletonToJson(s);
}
// a kickflip figure: 0.8 s in the air (phases 0..4), then 0.8 s on the ground (phase 5)
std::string FigC() {
    FigureRec f; f.figure = "KickFlip_RGS_regular"; f.bones = (int)RefNames().size();
    for (int i = 0; i < 48; i++) {
        f.times.push_back(i / 30.f); f.ratios.push_back(0.f);
        f.phases.push_back(i >= 24 ? FigureRecorder::kGroundPhase : i < 1 ? 0 : i < 6 ? 1 : i < 14 ? 2 : i < 18 ? 3 : 4);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QFromRotatorDeg(i * 1.f, (float)(b % 9), 0.f));
    }
    return FigureToJson(f);
}
int Bone(const EditorDoc& d, const char* n) {
    for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == n) return b;
    return -1;
}
const FigureVariant kKF{ST_Trick, "KickFlip", STANCE_Normal, false};
const float kF = 1.f / 30.f;

void Setup(EditorDoc& d) {
    std::string err;
    CHECK(d.LoadSkeleton(SkelC(), err));
    CHECK(d.LoadFigure(FigC(), kKF, err));
    d.NewClip(kKF);
    CHECK(d.clip().mode == Mode::Additive);
}
// key value of a bone at t (raw track value), identity without a track
Quat KeyVal(const EditorDoc& d, int bone, float t) {
    const Track* tr = d.TrackOf(bone);
    if (!tr || tr->keys.empty()) return QIdent;
    return EvalClipTrack(d.clip(), *tr, t, false);
}
bool QNear(const Quat& a, const Quat& b, float eps = 1e-4f) {
    const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return dot > 1.f - eps;
}
// sets a pose change (a rotation added to the game's pose) on a bone and keys it
void KeyBone(EditorDoc& d, int bone, float t, float pitch) {
    d.SetPending(bone, QNorm(QMul(d.EditedRaw(bone, t), QFromRotatorDeg(pitch, 0.f, 0.f))));
    d.CommitKey(t);
}
const Key* KeyAt(const EditorDoc& d, int bone, float t) {
    const Track* tr = d.TrackOf(bone);
    if (!tr) return nullptr;
    for (auto& k : tr->keys) if (std::fabs(k.t - t) < 1e-3f) return &k;
    return nullptr;
}
}

TEST(clipboard_pose_copied_at_a_pasted_at_b) {
    EditorDoc d; Setup(d);
    const int arm = Bone(d, "AMXX_R_UpperArm"), fore = Bone(d, "AMXX_R_Forearm"), head = Bone(d, "AMXX_Head");
    KeyBone(d, arm, 10 * kF, 30.f);
    KeyBone(d, fore, 10 * kF, 20.f);
    KeyBone(d, head, 20 * kF, 15.f);
    float pel[3] = {2.f, 0.f, -3.f};
    d.SetPendingPelvis(pel); d.CommitKey(5 * kF);
    // values before the paste at every keyed instant
    std::vector<float> times = d.KeyTimes(-1);
    std::vector<std::vector<Quat>> before;
    for (float t : times) { std::vector<Quat> row; for (int b = 0; b < d.BoneCount(); b++) row.push_back(KeyVal(d, b, t)); before.push_back(row); }
    std::vector<std::array<float, 3>> pelBefore;
    for (float t : times) { std::array<float, 3> p; EvalClipPelvis(d.clip(), t, false, p.data()); pelBefore.push_back(p); }

    const float A = 10 * kF, B = 15 * kF;
    CHECK(d.CopyPose(A, -1) > 0);
    CHECK(d.CanPaste());
    const EditorDoc::PasteResult r = d.Paste(B);
    CHECK(r.pasted > 0 && r.outOfClip == 0 && r.refused == 0);
    for (int b = 0; b < d.BoneCount(); b++) CHECK(QNear(KeyVal(d, b, B), KeyVal(d, b, A)));
    float pa[3], pb[3];
    EvalClipPelvis(d.clip(), A, false, pa); EvalClipPelvis(d.clip(), B, false, pb);
    for (int k = 0; k < 3; k++) CHECK_NEAR(pb[k], pa[k], 1e-3);
    // every other keyed instant evaluates as before
    for (size_t i = 0; i < times.size(); i++) {
        for (int b = 0; b < d.BoneCount(); b++) CHECK(QNear(KeyVal(d, b, times[i]), before[i][b]));
        float p[3]; EvalClipPelvis(d.clip(), times[i], false, p);
        for (int k = 0; k < 3; k++) CHECK_NEAR(p[k], pelBefore[i][k], 1e-3);
    }
}

TEST(clipboard_pose_of_a_part_copies_it_and_its_children_only) {
    EditorDoc d; Setup(d);
    const int arm = Bone(d, "AMXX_R_UpperArm"), fore = Bone(d, "AMXX_R_Forearm"), head = Bone(d, "AMXX_Head");
    KeyBone(d, arm, 10 * kF, 30.f);
    KeyBone(d, fore, 10 * kF, 20.f);
    KeyBone(d, head, 10 * kF, 15.f);
    float pel[3] = {2.f, 0.f, 0.f};
    d.SetPendingPelvis(pel); d.CommitKey(10 * kF);
    const int n = d.CopyPose(10 * kF, arm);
    CHECK(n == 2);   // the upper arm and the forearm (the only keyed parts it carries)
    const Quat headBefore = KeyVal(d, head, 20 * kF);
    float pBefore[3]; EvalClipPelvis(d.clip(), 20 * kF, false, pBefore);
    d.Paste(20 * kF);
    CHECK(QNear(KeyVal(d, arm, 20 * kF), KeyVal(d, arm, 10 * kF)));
    CHECK(QNear(KeyVal(d, fore, 20 * kF), KeyVal(d, fore, 10 * kF)));
    CHECK(QNear(KeyVal(d, head, 20 * kF), headBefore));   // not copied
    CHECK(KeyAt(d, head, 20 * kF) == nullptr);
    float p[3]; EvalClipPelvis(d.clip(), 20 * kF, false, p);
    for (int k = 0; k < 3; k++) CHECK_NEAR(p[k], pBefore[k], 1e-4);
}

TEST(clipboard_keys_keep_spacing_and_interpolation) {
    EditorDoc d; Setup(d);
    const int arm = Bone(d, "AMXX_R_UpperArm");
    KeyBone(d, arm, 3 * kF, 10.f);
    KeyBone(d, arm, 5 * kF, 20.f);
    KeyBone(d, arm, 8 * kF, 30.f);
    d.SetKeyInterp(5 * kF, arm, EditorDoc::IP_Step);
    d.SetKeyInterp(8 * kF, arm, EditorDoc::IP_Linear);
    std::vector<EditorDoc::KeyRef> refs{{{arm}, 3 * kF}, {{arm}, 5 * kF}, {{arm}, 8 * kF}};
    CHECK(d.CopyKeys(refs) == 3);
    // paste at a time off the frame grid: snapped to frame 12
    const EditorDoc::PasteResult r = d.Paste(12 * kF + 0.004f);
    CHECK(r.pasted == 3 && r.outOfClip == 0);
    for (int i : {0, 2, 5}) {
        const Key* src = KeyAt(d, arm, (3 + i) * kF);
        const Key* dst = KeyAt(d, arm, (12 + i) * kF);
        CHECK(src && dst);
        if (src && dst) { CHECK(QNear(src->q, dst->q)); CHECK(src->smooth == dst->smooth && src->step == dst->step); }
    }
}

TEST(clipboard_keys_collision_replaces_and_end_skips) {
    EditorDoc d; Setup(d);
    const int arm = Bone(d, "AMXX_R_UpperArm");
    KeyBone(d, arm, 3 * kF, 10.f);
    KeyBone(d, arm, 5 * kF, 20.f);
    KeyBone(d, arm, 8 * kF, 30.f);
    std::vector<EditorDoc::KeyRef> refs{{{arm}, 3 * kF}};
    const Quat src = KeyAt(d, arm, 3 * kF)->q;
    CHECK(d.CopyKeys(refs) == 1);
    CHECK(d.Paste(8 * kF).pasted == 1);   // on the key at 8: replaced
    CHECK(QNear(KeyAt(d, arm, 8 * kF)->q, src));
    int at8 = 0; for (auto& k : d.TrackOf(arm)->keys) if (std::fabs(k.t - 8 * kF) < 1e-3f) at8++;
    CHECK(at8 == 1);
    // three keys pasted near the end: two do not fit
    std::vector<EditorDoc::KeyRef> three{{{arm}, 3 * kF}, {{arm}, 5 * kF}, {{arm}, 8 * kF}};
    d.CopyKeys(three);
    const float dur = d.clip().duration;   // 24 frames
    const EditorDoc::PasteResult r = d.Paste(dur - 1 * kF);
    CHECK(r.pasted == 1 && r.outOfClip == 2);
}

TEST(clipboard_paste_on_a_part_without_track_keeps_the_start) {
    EditorDoc d; Setup(d);
    const int arm = Bone(d, "AMXX_R_UpperArm"), head = Bone(d, "AMXX_Head");
    auto clearHead = [&]() { for (float t : d.KeyTimes(head)) d.DeleteKeysBones({head}, t); };
    KeyBone(d, arm, 6 * kF, 30.f);
    // pose: the head keyed, copied, then all its keys removed; pasted in the middle
    KeyBone(d, head, 12 * kF, 20.f);
    CHECK(d.CopyPose(12 * kF, head) == 1);
    clearHead();
    CHECK(d.TrackOf(head) == nullptr);
    d.Paste(15 * kF);
    CHECK(d.TrackOf(head) != nullptr);
    CHECK(KeyAt(d, head, 0.f) != nullptr);
    CHECK(QNear(KeyVal(d, head, 0.f), QIdent));          // the start stays at the game's pose
    CHECK(!QNear(KeyVal(d, head, 15 * kF), QIdent));
    // keys: the same with a pasted key on a bone with no track
    std::vector<EditorDoc::KeyRef> hk{{{head}, 15 * kF}};
    CHECK(d.CopyKeys(hk) == 1);
    clearHead();
    CHECK(d.TrackOf(head) == nullptr);
    CHECK(d.Paste(18 * kF).pasted == 1);
    CHECK(KeyAt(d, head, 0.f) != nullptr);
    CHECK(QNear(KeyVal(d, head, 0.f), QIdent));
    CHECK(!QNear(KeyVal(d, head, 18 * kF), QIdent));
}

TEST(clipboard_one_undo_and_cleared_on_another_clip) {
    EditorDoc d; Setup(d);
    const int arm = Bone(d, "AMXX_R_UpperArm");
    KeyBone(d, arm, 6 * kF, 30.f);
    const std::string before = ClipToJson(d.clip());
    d.CopyPose(6 * kF, -1);
    d.Paste(14 * kF);
    CHECK(ClipToJson(d.clip()) != before);
    CHECK(d.Undo());
    CHECK(ClipToJson(d.clip()) == before);
    // the clipboard survives undo, is emptied by another clip
    CHECK(d.CanPaste());
    d.NewClip(kKF);
    CHECK(!d.CanPaste());
    // and by a mode change
    KeyBone(d, arm, 6 * kF, 30.f);
    d.CopyPose(6 * kF, -1);
    d.SetMode(Mode::Replace);
    CHECK(!d.CanPaste());
    d.CopyPose(6 * kF, -1);
    d.ClearClipboard();
    CHECK(!d.CanPaste());
    // nothing to paste: no undo step
    d.Checkpoint("mark");
    const std::string mark = d.UndoLabel();
    CHECK(d.Paste(10 * kF).pasted == 0);
    CHECK(std::string(d.UndoLabel()) == mark);
}

TEST(clipboard_locked_tracks_unchanged) {
    EditorDoc d; Setup(d);
    // a locked track (leg of a clip that loops: not editable) put in the file by hand
    FigureVariant man{ST_Manual, "", STANCE_Normal, false};
    d.NewClip(man);
    d.clip().mode = Mode::Additive;
    const int thigh = Bone(d, "AMXX_L_Thigh"), arm = Bone(d, "AMXX_R_UpperArm");
    CHECK(!d.IsEditable(thigh));
    Track lt; lt.bone = d.skeleton().names[thigh]; lt.index = thigh;
    lt.keys.push_back(Key{0.5f, QFromRotatorDeg(10, 0, 0), true});
    d.clip().tracks.push_back(lt);
    CHECK(d.HasLockedTracks());
    KeyBone(d, arm, 0.5f, 20.f);
    d.CopyPose(0.5f, -1);
    std::vector<EditorDoc::KeyRef> all{{{}, 0.5f}};
    d.Paste(1.5f);
    const Track* t = d.TrackOf(thigh);
    CHECK(t && t->keys.size() == 1u && std::fabs(t->keys[0].t - 0.5f) < 1e-4f);
    CHECK(d.CopyKeys(all) >= 1);
    d.Paste(2.0f);
    t = d.TrackOf(thigh);
    CHECK(t && t->keys.size() == 1u);
}
