// Session Studio -- the editing document: skeleton, original animation of a move, edited clip,
// pending changes, undo/redo. No dependency on the interface: tested outside the game by asn_tests.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../../SessionAnimLoader/Native/src/clip.h"
#include "../../SessionAnimLoader/Native/src/figure.h"
#include "../../SessionAnimLoader/Native/src/mirror.h"
#include "../../SessionAnimLoader/Native/src/pelvis.h"
#include "../../SessionAnimLoader/Native/src/skeleton.h"
#include "../../SessionAnimLoader/Native/src/trick.h"

// Legacy figure numbers (P4-P5): kept for older callers and tests. P6 works with FigureVariant.
enum LegacyFigure { FIG_Manual = 0, FIG_NoseManual = 1, FIG_Kickflip = 2, FIG_Count = 3 };
FigureVariant LegacyVariant(int figure);   // Manual / NoseManual / KickFlip, normal stance, regular feet
// the variant a clip was made for (first state, its trick, its first stance, its feet ; feet Any -> regular)
FigureVariant VariantOfClip(const Clip& c);
// trigger of a new clip made for this variant: only this variant (state, trick, stance, feet), no goofy mirror
void SetClipVariant(Clip& c, const FigureVariant& v);

// Editable bones: the upper body (Spine1-3, Neck, Head, Clavicle, UpperArm, Forearm, Hand, fingers).
bool IsEditableBone(const std::string& name);
// 2026-11-25 landing sync: the frames of a trick figure after the ground contact (phase 5), as a figure of their own
// starting at 0 (the base of a landing clip). false if the figure has no ground tail (old recording).
bool GroundTailFigure(const FigureRec& trick, FigureRec& out);
// the old lot 5 landing part of a trick clip split in two: air = the clip up to its ground (landing fields dropped),
// land = the keys after the ground, shifted to start at 0, as a landing clip of the same trick, stance, feet and mode.
// false if the clip has no landing part.
bool SplitLandingPart(const Clip& trick, Clip& air, Clip& land);
// one-time conversion of a trick clip to the landing sync (asn_tests --convert-landing): air = the clip with the markers
// of its figure (m, may be !ok), cut at the ground; land = the keys that were after the ground (old landing part, or a
// clip longer than its air part), hasLand = there were such keys. false if nothing changes (not a trick clip, or
// already converted with the same markers and nothing after the ground).
bool ConvertForLandingSync(const Clip& in, const TrickMarkers& m, Clip& air, Clip& land, bool& hasLand);
// lot 5: clear name of a bone for the timeline ("AMXX_L_Forearm" -> "L Forearm", "AMXX_Spine3" -> "Upper back")
std::string BoneLabel(const std::string& name);
// 2026-11-26: every variant (stance x feet) of a trick clip has an enabled landing clip of that trick among `clips`
// (a generic landing clip does not count). missing: the variants without one ("switch regular, ..."), "" if none.
bool LandingCoversAllVariants(const Clip& trick, const std::vector<const Clip*>& clips, std::string& missing);

// IK handle: hand or foot, elbow or knee, shoulder
enum class IkJoint { None, End, Mid, Root };

class EditorDoc {
public:
    // chargement
    bool LoadSkeleton(const std::string& json, std::string& err);
    // sets the clip trigger to the move; clip without tracks: all NewClip settings (name and file kept);
    // clip with tracks and a trick move: loop = false (duration and fades kept)
    bool LoadFigure(const std::string& json, const FigureVariant& v, std::string& err);
    bool LoadFigure(const std::string& json, std::string& err);   // legacy: the variant is taken from the file's figure name
    void ClearFigure(const FigureVariant& v);        // move not recorded (yet): origin = identity; same clip settings as LoadFigure
    void ClearFigure(int figure) { ClearFigure(LegacyVariant(figure)); }
    const FigureVariant& Variant() const { return variant_; }
    // P6: local position recorded for this bone at t (pelvis, board); false if the figure has none
    bool OriginPos(int bone, float t, float out[3]) const;
    void OriginPose(float tOrigin, std::vector<FTr>& out) const;   // original animation at tOrigin (time), component space
    // 2026-11-27: the game's visible board, recorded with the move; false when the recording (or its dismount part)
    // has none. OriginBoard follows the time rule of OriginPose, BoardAt the one of ComponentPose.
    bool HasRealBoard() const { return HasFigure() && !fig_.board.empty() && fig_.board.size() == fig_.times.size(); }
    bool OriginBoard(float tOrigin, BoardPose& out) const;
    bool BoardAt(float t, BoardPose& out) const;
    // The deck can be turned round by an earlier trick (a shove-it): the game keeps it so, the skeleton's board bone does
    // not turn. The drawn board only differs by its nose mark, so the mark goes to the end the animation calls the front
    // on the first frame, once for the whole recording. Done by LoadFigure; to be done on a recording before cutting it.
    void FaceBoardForward(FigureRec& rec) const;
    bool HasSkeleton() const { return !skel_.names.empty(); }
    bool HasFigure() const { return fig_.bones > 0; }
    int FigureIndex() const;   // legacy number of the variant, -1 if it is none of the three old figures
    float FigureDuration() const { return fig_.times.empty() ? 0.f : fig_.times.back(); }
    int BoneCount() const { return (int)skel_.names.size(); }
    // upper body, and the legs in a trick clip; in a looping clip, the legs stay locked
    bool IsEditable(int bone) const {
        return bone >= 0 && bone < (int)editable_.size() && (editable_[bone] || (leg_[bone] && LegsEditable()));
    }
    // 2026-11-25: the first key put after t = 0 also keys the start with the game's pose (off only in old tests)
    bool autoStartKey = true;
    bool IsTrickClip() const { return (clip_.states & ST_Trick) != 0; }
    // 2026-11-25 landing sync: a landing clip (state Landing; named = the landing of one trick, edited over that
    // trick's recorded ground tail). Legs are editable in trick and landing clips.
    bool IsLandingClip() const { return (clip_.states & ST_Landing) != 0 && !IsTrickClip(); }
    bool LegsEditable() const { return IsTrickClip() || IsLandingClip(); }
    // held pose (play.hold): one pose (the keys at t = 0) applied all the time over the game's own animation; the
    // timeline only scrubs the original animation to preview it. Not for one-shot clips (tricks, landing).
    bool CanHold() const { return !IsTrickClip() && !(clip_.states & ST_Landing); }
    bool IsHold() const { return clip_.hold && CanHold(); }
    // on: every track keeps one key at t = 0, the pose the clip had at t (same for the pelvis); off: keys kept
    void SetHold(bool on, float t);
    // instant where the keys are read: 0 for a held pose; in a time loop the end is the start again (keys at the
    // last instant go to 0, as the player sees them)
    float KeyTime(float t) const;
    // step cycle (2026-11-25): one stride [CycleStart, CycleEnd) found in the recorded figure (feet crossing), the
    // same rule as the player's live tracker; with drive Cycle, clip time t shows the figure at FigTime(t)
    bool HasCycle() const { return hasCycle_; }
    float CycleStart() const { return cycStart_; }
    float CycleEnd() const { return cycEnd_; }
    float FigTime(float t) const;
    bool LoopsInTime() const;   // keys wrap at the end (time loop or cycle), not for held / one-shot clips
    // drive actually followed: a trick clip is always driven by time from the pop (as in the player)
    Drive EffectiveDrive() const { return IsTrickClip() ? Drive::Time : clip_.drive; }
    // trick steps: one value per frame of the loaded move, empty if absent (raw values, not clamped)
    const std::vector<int>& Phases() const { return fig_.phases; }
    float PhaseStart(int phase) const;   // time of the 1st frame of this phase (clamped to 0..4), -1 if absent
    void SetLegsAfterCatch(bool on);     // v0.28, undoable
    bool LegsReleased() const { return !clip_.legsAfterCatch; }   // a trick clip gives its legs back at the catch
    float LegWeightAt(float t) const;    // leg weight of a trick clip: 1 before the catch, then 0 within 0.12 s; 1 without phases
    // 2026-11-26: a leg bone of a trick clip from the catch on: the game takes the legs back there, so it is not posed
    bool LegFrozenAt(int bone, float t) const {
        return IsTrickClip() && bone >= 0 && bone < (int)leg_.size() && leg_[bone] && LegWeightAt(t) < 0.999f;
    }
    const SkeletonFile& skeleton() const { return skel_; }
    const MirrorMap& mirror() const { return map_; }

    // clip
    void NewClip(const FigureVariant& v);
    void NewClip(int figure) { NewClip(LegacyVariant(figure)); }
    bool OpenClip(const std::string& json, const std::string& file, std::string& err);
    std::string SaveText(std::string& err) const;   // "" if the player would refuse the file
    Clip& clip() { return clip_; }
    const Clip& clip() const { return clip_; }
    bool HasLockedTracks() const;
    const Track* TrackOf(int bone) const;
    std::vector<float> KeyTimes(int bone /* -1 = tous */) const;
    void SetKeySmooth(float t, int bone /* -1 = tous */, bool smooth);

    // pose
    void SetSimulatedRatio(float r) { simRatio_ = r; }   // for a clip driven by ManualRatio
    float SimulatedRatio() const { return simRatio_; }
    Quat Origin(int bone, float t) const;            // original animation at t (looped over the recorded duration)
    Quat OriginAtRatio(int bone, float ratio) const; // original frame of the nearest ManualRatio
    Quat Edited(int bone, float t) const;            // edited pose (pending > key > origin)
    Quat OriginForClip(int bone, float t) const { return OriginFor(bone, t); }   // origin according to the clip drive
    // ghost: original animation only; raw: without the trick leg weight (base of what will become pending)
    // pelvis = false: without the pelvis offset and its leg re-solve (feet kept). Leg edits (rings, leg IK) are computed
    // on that pose: the re-solve rewrites thigh and calf, so measured after it a gesture fought it (2026-11-25 fix)
    void ComponentPose(float t, std::vector<FTr>& out, bool ghost, bool raw = false, bool pelvis = true) const;
    // edited pose without the trick leg weight: the value a key stores; for anything that becomes a pending change
    Quat EditedRaw(int bone, float t) const;

    // editing
    bool SetPending(int bone, Quat local);           // false if the bone is locked
    bool Pending(int bone, Quat* q) const;
    bool HasPending() const { return !pending_.empty() || pelPending_; }
    void DiscardPending() { pending_.clear(); pelPending_ = false; }
    void CommitKey(float t);
    void DeleteKeysAt(float t, int bone /* -1 = tous */);
    bool MoveKeys(float from, float to);
    void TakeOriginPose(float tOrigin, int root = -1);   // root: the bone and its descendants; -1 = the whole upper body
    void MirrorCopy(int bone, float t);
    void SetMode(Mode m);
    // P6: the clip now targets another variant (stance / feet / trick). When the body side changes (regular <-> goofy
    // looking), every track is mirrored to the other side, like the player does. The figure shown is the caller's job.
    void RetargetVariant(const FigureVariant& v);
    // P6: an empty trick clip gets a key on every editable bone at the start, the flip and the landing of the original
    // animation (the game's pose there). Nothing for a loop clip, a clip with keys, or without a figure. Returns the key count.
    int SeedKeys();

    // hand IK: UpperArm -> Forearm -> Hand chain on the side of the given bone (arm, hand or finger)
    // also Thigh -> Calf -> Foot for a leg bone (hand = the foot), only if the foot is editable
    bool IkChain(int bone, int& upper, int& fore, int& hand) const;
    // sets as pending the rotations that bring the hand to target (UE component space); false if impossible
    // A gesture (dragging arrows or the elbow slider): IkBegin captures the base pose, each IkMoveHand starts again
    // from that base (no drift with the mouse path), IkEnd ends the gesture. Without IkBegin, the current pose is the base.
    // bone can be any bone with a handle (IkHandle); the base also covers the clavicle for the shoulder
    void IkBegin(int bone, float t);
    void IkEnd() { ikBaseHand_ = -1; ikKind_ = IkJoint::None; }
    // IK handle of the selected bone (hand/fingers, foot/toes -> End; Forearm, Calf -> Mid; UpperArm, Clavicle -> Root;
    // the rest, and any locked bone -> None). Chain root (clavicle, -1 except Root) -> upper -> fore -> end.
    IkJoint IkHandle(int bone, int& root, int& upper, int& fore, int& end) const;
    // a gesture on the handle of bone (same cycle as IkMoveHand). keepEndInPlace: the hand/foot stays in place;
    // keepEndOrientation: it keeps its orientation (component space). false if impossible (nothing becomes pending).
    bool IkMoveJoint(int bone, const float target[3], bool keepEndInPlace, bool keepEndOrientation, float t);
    // sets as pending the rotations that bring the hand to target (UE component space); elbowDeltaDeg: elbow rotation
    // around the shoulder -> target axis, relative to the gesture; false if impossible
    bool IkMoveHand(int hand, const float target[3], float elbowDeltaDeg, bool keepHandWorld, float t);

    // P6 lot 2: pelvis offset (cm, UE component space): the pelvis and the upper body move, the feet stay (legs re-solved)
    int PelvisBone() const { return rig_.Ok() ? rig_.pelvis : -1; }
    void PelvisOffset(float t, float out[3]) const;        // pending offset, else the clip's pelvis keys at t
    bool SetPendingPelvis(const float v[3]);               // false without a pelvis rig
    bool PendingPelvis(float out[3]) const;
    // P6 lot 2: arm IK whose spine and clavicle follow when the target is out of reach (hand handle only)
    bool IkMoveHandBody(int hand, const float target[3], bool keepHandWorld, float t);

    // per-bone timeline: one row per animated bone, hands + fingers grouped
    struct KeyRow { std::string label; std::vector<int> bones; int primary; };
    std::vector<KeyRow> KeyRows(bool allEditable) const;
    std::vector<float> KeyTimesBones(const std::vector<int>& bones) const;
    bool MoveKeysBones(const std::vector<int>& bones, float from, float to);   // false on collision or out of the duration
    void DeleteKeysBones(const std::vector<int>& bones, float t);

    // ---------------------------------------------------------------- lot 5
    // a new trick figure goes on 2 s after the ground (phase 5; 0.8 s before 2026-11-26): that tail is what landing clips are edited over
    float GroundAt() const;               // figure time of the ground contact, -1 if the figure has no ground part
    float GroundTail() const;             // seconds recorded after the ground (0 if none)
    // ---------------------------------------------------------------- landing sync (2026-11-25)
    // markers of the game's events in the loaded trick figure (written into the clip at save, see trick_sync.h)
    TrickMarkers Markers() const;
    // a clip saved with the old lot 5 landing part (play.landing): the editor no longer edits it, see SplitLandingPart
    bool HasOldLandingPart() const { return IsTrickClip() && clip_.landing > 0.f; }
    // an empty landing clip starts where the trick clip ends: start keys = the trick clip at its ground marker (same
    // mode), end keys = the game's pose (the landing fades back to the game). keepKeys false: the keys are replaced.
    // Returns the key count (0: nothing to take, or the clip has keys and keepKeys).
    int SeedLandingFromTrick(const Clip& trick, bool keepKeys = true);
    // interpolation of a key: 0 smooth, 1 linear, 2 constant (step)
    enum Interp { IP_Smooth = 0, IP_Linear = 1, IP_Step = 2 };
    void SetKeyInterp(float t, int bone /* -1 = all */, int interp);
    int KeyInterp(float t, int bone /* -1 = first found */) const;   // -1 if no key there
    // timeline rows grouped by limb (Head, Chest, L Arm, L Hand..., L Leg..., Pelvis): a group row shows the keys of
    // all its bones; its children are one row per bone (fingers: one row per finger), with clear names
    struct KeyGroup { std::string id, label; std::vector<int> bones; std::vector<KeyRow> children; };
    std::vector<KeyGroup> KeyGroups(bool allEditable) const;
    // 2026-11-27 (one timeline, two files): the rows of both parts as one list, the same whichever part is open, so the
    // timeline does not change when the playhead crosses the ground contact. Groups in the usual order, rows by bone.
    static std::vector<KeyGroup> MergeKeyGroups(const std::vector<KeyGroup>& a, const std::vector<KeyGroup>& b);
    std::string GroupOfBone(int bone) const;   // id of the group holding this bone ("armL", "pelvis"...), "" if none

    // ---------------------------------------------------------------- dismount part of a riding clip (2026-11-25)
    // the game's dismount (figure Dismount_<stance>_<feet>, recorded the first time the feet leave the board); shown
    // after the loop of a riding clip that has a dismount part
    bool LoadDismountFigure(const std::string& json, std::string& err);
    void ClearDismountFigure() { dis_ = FigureRec{}; }
    bool HasDismountFigure() const { return dis_.bones > 0 && !dis_.times.empty(); }
    float DismountFigureDuration() const { return dis_.times.empty() ? 0.f : dis_.times.back(); }
    bool CanDismount() const { return (clip_.states & ST_Riding) != 0 && !IsTrickClip(); }
    bool HasDismount() const { return CanDismount() && clip_.dismount > 0.f && clip_.dismount < clip_.duration; }
    bool InDismount(float t) const { return HasDismount() && t >= LoopLength(clip_) - 1e-3f; }
    // on: the clip gets a dismount part of len s after its loop (start key = the loop's pose at 0, end key = the game's
    // pose, so by default the riding pose fades out over the dismount); off: the part and its keys are removed.
    // false if on without a recorded dismount.
    bool SetDismountPart(bool on, float len);

    // ---------------------------------------------------------------- shoulder (2026-11-25)
    // The game mesh has no twist bones: an upper arm raised far above the shoulder by itself, or rolled on its own axis,
    // crushes the shoulder skin in game (the editor only shows bones). Measured on the edited pose at t, side 0 L / 1 R.
    struct ShoulderInfo {
        bool ok = false;
        float elevation = 0.f;    // deg between the upper arm and the torso's down axis (0 hanging, 90 level, 180 up); the warning counts it minus the clavicle raise
        float twistUpper = 0.f;   // deg of roll of the upper arm on its own axis, relative to the game's pose
        float twistFore = 0.f;    // same for the forearm
        float clavRaise = 0.f;    // deg the clavicle is raised relative to the game's pose
        bool warn = false;        // likely to deform in game
    };
    static constexpr float kShoulderStart = 60.f;   // deg of elevation from which the clavicle takes a share
    static constexpr float kShoulderShare = 0.35f;  // share of the elevation above kShoulderStart given to the clavicle
    static constexpr float kShoulderMax = 30.f;     // deg, clavicle raise cap
    static constexpr float kWarnElevation = 100.f, kWarnTwist = 35.f;
    ShoulderInfo Shoulder(int side, float t) const;
    int ArmSide(int bone) const;   // 0 / 1 for a clavicle, arm, forearm, hand or finger bone, -1 otherwise
    // Puts in pending: the clavicle raised (natural shoulder rhythm) and, with reduceTwist, the elbow turned around the
    // shoulder -> hand axis to the place where the upper arm rolls least, and half the forearm roll moved to the wrist.
    // The hand keeps its position and orientation. false if the arm cannot be solved (nothing changed).
    bool FixShoulder(int side, float t, bool clavicle, bool reduceTwist);

    // ---------------------------------------------------------------- copy / paste (2026-11-26), inside the open clip
    // The clipboard holds MY CHANGE (key values), not the game's pose. It is emptied when another clip or variant is
    // opened, or when the mode changes.
    struct KeyRef { std::vector<int> bones; float t; };   // a timeline key: bones of its row (empty = "All"), time
    struct PasteResult { int pasted = 0, outOfClip = 0, refused = 0; };
    // the change at t of the part root and what it carries (-1 = whole body, pelvis offset included; the pelvis bone
    // = the pelvis offset only). Only parts with keys (or a pending change) are taken. Returns the parts copied.
    int CopyPose(float t, int root);
    // the keys at those rows and times (values and interpolation). Returns the keys copied.
    int CopyKeys(const std::vector<KeyRef>& keys);
    // a pose is keyed at t like a normal key (other keyed instants keep their pose); keys keep their spacing and
    // interpolation, the first one at t, snapped to frames, replacing keys there; keys past the end are skipped.
    // One undo step if something changed. Nothing is pasted while a change is pending (refused = everything).
    PasteResult Paste(float t);
    bool CanPaste() const { return clipKind_ != CB_None; }
    bool ClipboardHasKeys() const { return clipKind_ == CB_Keys; }
    void ClearClipboard() { clipKind_ = CB_None; cbPose_.clear(); cbKeys_.clear(); cbHasPel_ = false; }

    // ---------------------------------------------------------------- ends of trick and landing clips (2026-11-26)
    // Add mode trick and landing clips only (Replace, loops, held poses, dismount parts: never an issue).
    // A part "differs from the game's pose" by more than kEndDeg degrees, the pelvis by more than kEndCm cm.
    static constexpr float kEndDeg = 3.f, kEndCm = 1.f;
    struct EndIssue {
        enum Kind { TrickEndNoLanding, LandingEndNotBack, LandingStartMismatch, StartNotGame /* LockPreview only */ };
        Kind kind = TrickEndNoLanding;
        std::vector<int> bones;   // the parts concerned (bone indices)
        bool pelvis = false;      // the pelvis offset too
        float maxDeg = 0.f, maxCm = 0.f;
        float t = 0.f;            // the instant concerned (end of the clip, or 0 for the landing start)
    };
    // trick clip: its end, unless the trick has a landing for all its variants (legs ignored: the game takes them back
    // 0.12 s after the catch). Landing clip: its end, and its start against linkedTrick's end (its trick clip, may be null).
    std::vector<EndIssue> EndIssues(const Clip* linkedTrick, bool hasLandingForAllVariants) const;
    // end issues: a game-pose key at the end for the listed parts; start mismatch: the trick's end for the listed parts.
    // One undo step. false if nothing changed.
    bool FixEndIssue(const EndIssue& e, const Clip* linkedTrick);

    // ---------------------------------------------------------------- locked start and end (2026-11-26, task 3)
    // Add mode trick and landing clips (LockAvailable). The switch is the extra root field "lockEnds" of the file
    // (missing = off: clips made before; new trick and landing clips: on). Locked instant = the game's pose there.
    //   trick:   start locked; end locked unless the trick has a landing clip for all its variants (SetHasLandingCover)
    //   landing: start free (it continues the trick); end locked
    // A trick that loses its landing cover while its end is posed keeps that pose (nothing is erased, the end warning
    // shows); its end locks again once it is back within kEndDeg / kEndCm (the fix button does that).
    // Legs are never locked at the end of a trick clip (the game takes them back from the catch).
    bool LockEnds() const;
    // one undo step. on: the locked instants are reset to the game's pose (LockPreview tells what changes)
    void SetLockEnds(bool on);
    bool LockAvailable() const { return clip_.mode == Mode::Additive && (IsTrickClip() || IsLandingClip()); }
    void SetHasLandingCover(bool on) { landCover_ = on; }   // changes no key
    bool HasLandingCover() const { return landCover_; }
    bool StartLocked() const;
    bool EndLocked() const;
    bool LockedAt(float t) const;     // t is a locked instant (start or end)
    // what turning the lock on would reset: StartNotGame (t = 0) and the end kind (t = end), the parts that are posed there
    std::vector<EndIssue> LockPreview() const;
    // locked instants back to the game's pose. Every key writer calls it last (inside the caller's undo step).
    void ApplyEndLock();
    // clip length (>= 0.1 s); with the end locked, the end key moves with the end (no zero key left in the middle)
    void SetDuration(float d);

    // undo / redo (at most 200 steps)
    struct State { Clip clip; std::map<int, Quat> pending; bool pelPending = false; float pel[3] = {0, 0, 0}; };
    State SaveState() const { State s{clip_, pending_, pelPending_}; for (int k = 0; k < 3; k++) s.pel[k] = pel_[k]; return s; }
    // pushes a state captured BEFORE a change (an ImGui slider changes the value on the very activation frame)
    void CheckpointState(const State& before, const char* label);
    void Checkpoint(const char* label);
    bool Undo();
    bool Redo();
    const char* UndoLabel() const;
    const char* RedoLabel() const;   // UI Phase 1: shown on the Redo button ("" if nothing to redo)
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

private:
    void MergeLoopEnd();
    void ComputeCycle();
    bool hasCycle_ = false;
    float cycStart_ = 0.f, cycEnd_ = 0.f;   // time loop: keys at the last instant go to 0 (the same instant of the loop)
    struct Snap { Clip clip; std::map<int, Quat> pending; std::string label; bool pelPending = false; float pel[3] = {0, 0, 0}; };
    Snap MakeSnap(const char* label) const;
    void Restore(Snap& s);
    Quat OriginFor(int bone, float t) const;   // origin according to the clip drive (time or simulated ManualRatio)
    int TrackIndex(int bone) const;
    Quat PoseNoPending(int bone, float t, const Quat& origin) const;   // raw edited pose, without pendings, for a given origin
    Quat KeyFor(int bone, int track, Quat pose, float t) const;        // key that gives pose back (track = -1: weight 1)
    Quat KeyForOrigin(const Quat& origin, int track, Quat pose) const;
    void SetClipForFigure();   // rules shared by LoadFigure and ClearFigure
    void ApplyFigureDefaults(Clip& c, const FigureVariant& v) const;   // trigger, mode, loop, duration, fades of the move
    bool EditableName(const std::string& n) const { return IsEditableBone(n) || (IsLegBone(n) && LegsEditable()); }

    SkeletonFile skel_;
    MirrorMap map_;
    std::vector<bool> editable_;
    std::vector<bool> leg_;               // leg bones
    FigureRec fig_;
    FigureRec dis_;                       // 2026-11-25: the game's dismount (riding clips)
    Quat OriginDismount(int bone, float u) const;
    bool DismountPos(int bone, float u, float out[3]) const;
    FigureVariant variant_;
    Clip clip_;
    std::map<int, Quat> pending_;
    PelvisRig rig_;                       // P6 lot 2
    bool pelPending_ = false;
    float pel_[3] = {0, 0, 0};
    std::vector<FTr> ikBody_;             // P6 lot 2: whole pose at the start of an arm gesture (spine follow)
    std::map<int, Quat> ikPendBase_;      // P6 lot 2: pending changes at the start of that gesture
    float simRatio_ = 0.5f;
    int ikBaseHand_ = -1;                 // running IK gesture
    FTr ikBase_[4] = {};                  // arm parent, arm, forearm, hand: pose at the start of the gesture
    IkJoint ikKind_ = IkJoint::None;      // handle of the current gesture
    FTr ikBaseRoot_[2] = {};              // clavicle parent, clavicle: pose at the start of a shoulder gesture
    std::vector<Snap> undo_, redo_;
    // clipboard (copy / paste)
    enum ClipKind { CB_None, CB_Pose, CB_Keys };
    ClipKind clipKind_ = CB_None;
    std::map<int, Quat> cbPose_;          // pose: key value of each bone
    bool cbHasPel_ = false;               // pose: pelvis offset copied
    float cbPel_[3] = {0, 0, 0};
    struct CbKey { float dt; std::vector<std::pair<int, Key>> bones; bool hasPel = false; PosKey pel{}; };
    std::vector<CbKey> cbKeys_;           // keys: one entry per copied (row, time), dt from the first one
    // locked ends
    bool landCover_ = false;              // the trick has a landing clip for all its variants (given by the app)
    void LockKeysAt(float t, bool skipLegs);
    bool EndLockedRule() const;           // the end lock rule alone (without the "trick end still posed" exception)
    void ApplyEndLockFrom(bool endLockedBefore);   // the end is locked if it was before the change, or is now
    struct LockGuard {
        EditorDoc& d; bool end;
        explicit LockGuard(EditorDoc& x) : d(x), end(x.EndLocked()) {}
        ~LockGuard() { d.ApplyEndLockFrom(end); }
    };
};
// 2026-11-26: a bool extra root field of a clip ("lockEnds"): set or replaced, read (missing = def)
void SetClipExtraBool(Clip& c, const char* name, bool v);
bool ClipExtraBool(const Clip& c, const char* name, bool def);
