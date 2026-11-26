// AnimStudio Editor -- the editing document: skeleton, original animation of a move, edited clip,
// pending changes, undo/redo. No dependency on the interface: tested outside the game by asn_tests.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../../AnimStudioNative/src/clip.h"
#include "../../AnimStudioNative/src/figure.h"
#include "../../AnimStudioNative/src/mirror.h"
#include "../../AnimStudioNative/src/pelvis.h"
#include "../../AnimStudioNative/src/skeleton.h"
#include "../../AnimStudioNative/src/trick.h"

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
    float LegWeightAt(float t) const;    // leg weight of a trick clip: 1 before the catch, then 0 within 0.12 s; 1 without phases
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
    // a new trick figure goes on 0.8 s after the ground (phase 5): that tail is what landing clips are edited over
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
};
