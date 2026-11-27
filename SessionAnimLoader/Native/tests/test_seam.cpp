// 2026-11-27 (user: "if I change the end of the trick, the start of the landing must be identical to it"): on the
// two-part timeline the landing's start is linked to the trick's end. And the clipboard crosses the two parts.
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <cmath>

namespace {
const float kF = 1.f / 30.f;
const FigureVariant kTrick{ST_Trick, "KickFlip", STANCE_Normal, false}, kLand{ST_Landing, "KickFlip", STANCE_Normal, false};

// 24 frames in the air (catch from frame 14), then the ground; the game's pose turns a little every frame
FigureRec Fig() {
    FigureRec f; f.figure = "KickFlip_RGS_regular"; f.bones = (int)RefNames().size();
    for (int i = 0; i < 60; i++) {
        f.times.push_back(i * kF); f.ratios.push_back(0.f);
        f.phases.push_back(i >= 24 ? FigureRecorder::kGroundPhase : i < 1 ? 0 : i < 6 ? 1 : i < 14 ? 2 : i < 18 ? 3 : 4);
        for (int b = 0; b < f.bones; b++) f.frames.push_back(QFromRotatorDeg(i * 1.f, (float)(b % 7), 0.f));
    }
    return f;
}
int Bone(const EditorDoc& d, const char* n) {
    for (int b = 0; b < d.BoneCount(); b++) if (d.skeleton().names[b] == n) return b;
    return -1;
}
// the trick (open on the first part) and its new landing, as the application builds them
void Setup(EditorDoc& trick, EditorDoc& land) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    std::string err;
    CHECK(trick.LoadSkeleton(SkeletonToJson(s), err));
    CHECK(trick.LoadFigure(FigureToJson(Fig()), kTrick, err));
    trick.NewClip(kTrick);
    trick.SetDuration(24 * kF);
    trick.SetHasLandingCover(true);   // as the application does: your landing takes over, the trick's end is free
    land = trick;
    FigureRec tail;
    CHECK(GroundTailFigure(Fig(), tail));
    tail.figure = "KickFlip_landing";
    land.NewClip(kLand);
    CHECK(land.LoadFigure(FigureToJson(tail), kLand, err));
    land.SetDuration(30 * kF);
}
void Pose(EditorDoc& d, int bone, float t, float yaw) {
    d.Checkpoint("pose");
    d.SetPending(bone, QNorm(QMul(d.EditedRaw(bone, t), QFromRotatorDeg(0.f, yaw, 0.f))));
    d.CommitKey(t);
}
float Deg(const Quat& a, const Quat& b) {
    const Quat d = QNorm(QMul(QInv(a), b));
    return 2.f * std::atan2(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), std::fabs(d.w)) * 57.29578f;
}
float SeamGap(const EditorDoc& trick, const EditorDoc& land, int bone) { return Deg(trick.Edited(bone, trick.clip().duration), land.Edited(bone, 0.f)); }
float PelvisGap(const EditorDoc& trick, const EditorDoc& land) {
    float a[3], b[3];
    trick.PelvisOffset(trick.clip().duration, a);
    land.PelvisOffset(0.f, b);
    return std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
}
}

TEST(seam_the_landing_starts_where_the_edited_trick_ends) {
    EditorDoc trick, land; Setup(trick, land);
    const int arm = Bone(trick, "AMXX_R_UpperArm"), head = Bone(trick, "AMXX_Head");
    CHECK(!land.LinkStartToTrick(trick));          // nothing changed on either side: nothing written
    CHECK(land.clip().tracks.empty() && land.clip().pelvis.empty());
    Pose(trick, arm, trick.clip().duration, 40.f);
    const float pel[3] = {1.f, 2.f, -6.f};
    trick.SetPendingPelvis(pel); trick.CommitKey(trick.clip().duration);
    CHECK(SeamGap(trick, land, arm) > 30.f);
    CHECK(land.LinkStartToTrick(trick));
    CHECK(SeamGap(trick, land, arm) < 0.1f);
    CHECK(SeamGap(trick, land, head) < 0.1f);
    CHECK(PelvisGap(trick, land) < 0.01f);
    // the landing hands back to the game at its end
    CHECK(Deg(land.Edited(arm, land.clip().duration), land.OriginForClip(arm, land.clip().duration)) < 0.1f);
    float end[3]; land.PelvisOffset(land.clip().duration, end);
    CHECK_NEAR(end[2], 0.0, 1e-4);
    // nothing more to do: not "changed" again, also after the file is written and read
    CHECK(!land.LinkStartToTrick(trick));
    std::string err;
    const std::string text = land.SaveText(err);
    CHECK(!text.empty());
    EditorDoc again = land;
    CHECK(again.OpenClip(text, "x.sanim", err));
    CHECK(!again.LinkStartToTrick(trick));
}

TEST(seam_follows_every_change_and_undo_of_the_trick) {
    EditorDoc trick, land; Setup(trick, land);
    const int arm = Bone(trick, "AMXX_R_UpperArm");
    Pose(trick, arm, trick.clip().duration, 40.f);
    CHECK(land.LinkStartToTrick(trick));
    Pose(trick, arm, trick.clip().duration, -15.f);
    CHECK(land.LinkStartToTrick(trick));
    CHECK(SeamGap(trick, land, arm) < 0.1f);
    CHECK(trick.Undo());
    CHECK(trick.Undo());
    CHECK(land.LinkStartToTrick(trick));
    CHECK(SeamGap(trick, land, arm) < 0.1f);
    CHECK(land.clip().tracks.empty());             // the part the link had added is gone again
    // a change not keyed yet is not carried over
    trick.SetPending(arm, QFromRotatorDeg(0.f, 70.f, 0.f));
    CHECK(!land.LinkStartToTrick(trick));
    trick.DiscardPending();
}

TEST(seam_keeps_the_rest_of_the_landing) {
    EditorDoc trick, land; Setup(trick, land);
    const int arm = Bone(trick, "AMXX_R_UpperArm");
    Pose(land, arm, 12 * kF, 25.f);                // my landing pose, later in the clip
    const Quat mine = land.Edited(arm, 12 * kF);
    Pose(trick, arm, trick.clip().duration, 40.f);
    CHECK(land.LinkStartToTrick(trick));
    CHECK(SeamGap(trick, land, arm) < 0.1f);
    CHECK(Deg(land.Edited(arm, 12 * kF), mine) < 0.1f);
}

TEST(seam_uses_what_is_seen_at_the_end_of_the_trick) {
    EditorDoc trick, land; Setup(trick, land);
    const int thigh = Bone(trick, "AMXX_R_Thigh");
    CHECK(thigh >= 0 && trick.IsEditable(thigh));
    Pose(trick, thigh, 10 * kF, 30.f);             // before the catch: the game takes the legs back after it
    land.LinkStartToTrick(trick);
    CHECK(Deg(land.Edited(thigh, 0.f), land.OriginForClip(thigh, 0.f)) < 0.1f);
    CHECK(SeamGap(trick, land, thigh) < 0.1f);
    trick.SetLegsAfterCatch(true);                 // now the keys move the legs to the end
    CHECK(land.LinkStartToTrick(trick));
    CHECK(SeamGap(trick, land, thigh) < 0.1f);
    CHECK(Deg(land.Edited(thigh, 0.f), land.OriginForClip(thigh, 0.f)) > 20.f);
}

TEST(seam_start_is_locked_while_linked) {
    EditorDoc trick, land; Setup(trick, land);
    const int arm = Bone(trick, "AMXX_R_UpperArm");
    CHECK(!land.LockedAt(0.f));
    land.LinkStartToTrick(trick);
    CHECK(land.StartLinked() && land.LockedAt(0.f));
    Pose(trick, arm, trick.clip().duration, 40.f);
    land.LinkStartToTrick(trick);
    CHECK(!land.MoveKeys(0.f, 5 * kF));            // the linked key does not move
    land.UnlinkStart();
    CHECK(!land.StartLinked() && !land.LockedAt(0.f));
    trick.LinkStartToTrick(trick);                 // not a landing: ignored
    CHECK(!trick.StartLinked());
}

TEST(clipboard_crosses_from_one_part_to_the_other) {
    EditorDoc trick, land; Setup(trick, land);
    const int arm = Bone(trick, "AMXX_R_UpperArm");
    Pose(trick, arm, 10 * kF, 30.f);
    CHECK(trick.CopyPose(10 * kF, arm) > 0);
    CHECK(!land.CanPaste());
    land.TakeClipboard(trick);
    CHECK(land.CanPaste() && !trick.CanPaste());
    CHECK(land.Paste(10 * kF).pasted > 0);
    CHECK(land.TrackOf(arm) != nullptr);
    land.TakeClipboard(trick);                     // an empty clipboard takes nothing away
    CHECK(land.CanPaste());
}
