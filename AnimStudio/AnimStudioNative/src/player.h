// The clip player: picks the winning clip, fading layers, applied to the finished pose (component space).
// Called from the FinalizeBoneTransform hook: no UObject read, no waiting.
#pragma once
#include <cstdint>
#include <utility>
#include "clip.h"
#include "mirror.h"
#include "pelvis.h"
#include "cycle.h"
#include "net.h"

struct GameState {
    bool manual, noseManual, goofy, sw; float manualRatio;
    bool kickflip = false;      // a flip trick is on, from the pop to the landing (historical name: any trick)
    uint32_t popSerial = 0;     // pop number (changes with every trick)
    int trickPhase = 0;         // 0..4: pop, takeoff, flip, catch, landing
    uint32_t trickHash = 0;     // P6: TrickHash of the trick base name; 0 = old caller (kickflip was the only trick)
    int stance = 0;             // P6: stance of the trick (Stance)
    bool grind = false, powerslide = false, grab = false;   // P6: looping states, like manuals
    uint32_t grindHash = 0;     // TrickHash of the name of the current grind (0 = unknown: only "every grind" clips)
    int figId = 0;              // P6: current move (variant), 0 = none (number given by the game thread)
    bool figWanted = false;     // P6: its original animation has to be recorded
    bool figShort = false;      // P6 lot 4: a short state (crouch, landing): 0.5 s is enough to keep a recording
    bool onFoot = false, riding = false, crouch = false, landing = false;   // P6 lot 4: base poses (exclusive)
    bool walking = false, pushing = false;
    bool running = false;       // 2026-11-25: off the board, running (the speed-based "mounting" of lot 5 was really running)
    bool dismount = false;      // 2026-11-25: the feet just left the board (window of kDismountWindow s, see main.cpp)
    int groundStance = -1;      // P6 lot 4: stance of the ground states (-1: from sw)
    // 2026-11-25 landing sync: live seconds to the ground contact (ballistic, -1 unknown), and the trick a landing comes
    // from (its own landing clip: state Landing + trick name; 0 = a landing with no trick before it)
    float predGround = -1.f;
    float landLead = -1.f;      // 2026-11-26: learned live IsLanding -> ground of this trick (LandLearn), -1 unknown
    uint32_t landTrickHash = 0;
    int landStance = 0;
};

// Set of clips published by ClipLibrary: clips already bound to the skeleton, sorted by file name. Read only.
struct ClipSet {
    std::vector<Clip> clips;
    uint64_t generation = 0;
    std::vector<std::pair<int, int>> convOverride;   // _studio.json "mirrorConv" : (index d'os, convention)
    uint64_t mirrorStamp = 0;                        // changes when the mirror overrides change
    bool legPostPass = true;                         // P6: _studio.json "legPostPass" (leg post pass after the other mods)
};

class Player {
public:
    static constexpr int kMaxBones = 256;
    static constexpr int kMaxLayers = 4;

    void SetRig(const int* parent, int n, MirrorMap map);
    void SetLegBones(const std::vector<bool>& isLeg);   // one entry per rig bone; call after SetRig
    // P6: reference local positions (3 floats per bone, as in _skeleton.json); after SetRig. Legs written by our clips
    // use these lengths instead of the game's (the game stretches the shin in the air)
    void SetRefPositions(const std::vector<float>& xyz);
    // P6 lot 2: pelvis offset keys move the pelvis, the legs are re-solved so the feet stay (after SetRig)
    void SetPelvisRig(PelvisRig r) { rig_ = std::move(r); }
    float LegWeight() const { return legW_; }           // leg weight of trick clips (log, tests)
    void SetMaster(bool on) { masterTarget_ = on ? 1.f : 0.f; }   // 0.25 s fade (starts at 1); writing a float: no lock
    // One frame: picks the clip, advances fades and clocks, writes into cs[]. Returns the number of rewritten bones.
    int Apply(FTr* cs, int n, const ClipSet* set, const GameState& gs, float dt);
    // P6: call on the SAME pose after the other mods (ExpandedTricks, SessionTweaks, the engine): the legs that
    // our clips wrote this frame get back the pose we gave them (rotations AND bone lengths), at the clip
    // weight. Without this, ExpandedTricks leg IK ("legs reach", planted feet) started again from our knee
    // and foot and stretched or twisted the shin. Returns the number of rewritten bones (0 if nothing to redo).
    int PostApply(FTr* cs, int n);
    // Game local rotations (before our layer) read on the last applied frame.
    void SnapshotGameLocals(Quat* out, int n) const;
    // P6: game local positions (before our layer) on the last frame: 3 floats per bone
    void SnapshotGamePositions(float (*out)[3], int n) const;
    // 2026-11-26 multiplayer. ExportNet (after Apply, same thread): our layers as sent to the other players.
    // ApplyRemote: on a PEER's skater, play the layers that peer sent (clips found by NetId + content hash in `set`),
    // `age` = seconds since the frame arrived (clip time and weight extrapolated; faded out when the stream stops).
    // `missing` (optional) gets the number of layers whose clip is not here. Returns the number of bones rewritten.
    void ExportNet(NetFrame& out, float dt);
    int ApplyRemote(FTr* cs, int n, const ClipSet* set, const NetFrame& f, float age, float dt, int* missing = nullptr);
    static constexpr float kNetStale = 0.5f, kNetStaleFade = 0.3f;   // s
    const char* CurrentClipName() const;             // internal copy: never a pointer into a Clip; "" if none
    int LayerCount() const { return nLayers_; }
    int RigBones() const { return n_; }
    const CycleTracker& Cycle() const { return cyc_; }   // 2026-11-25: the game's step cycle (journal)

private:
    struct Layer {
        char file[260];
        char name[128];
        const Clip* clip;          // nullptr if frozen (clip gone, changed, or stance changed)
        uint64_t stamp, mirrorStamp;
        bool mirrored;             // fixed when the layer starts
        bool trick;                // trick clip (ST_Trick): does not loop, leg weight
        bool dis;                  // 2026-11-25: plays the dismount part of a riding clip (once, from the clip's loop end)
        bool hold;                 // clock not advanced on the start frame (clock 0 at the pop)
        float clock, w, target, rate;
        float tPlay;               // clip time of a trick layer (TrickClock: warped onto the live jump)
        TrickClock tc;             // 2026-11-25 landing sync
        bool grounded;             // its trick is back on the ground
        float legW;                // leg weight of the layer (kept when frozen)
        int nKeys;                 // last evaluated keys (used when the layer is frozen)
        int bone[kMaxBones];
        float boneW[kMaxBones];
        Quat key[kMaxBones];
        float pel[3];              // P6 lot 2: pelvis offset of the clip (as written, mirrored when applied)
        bool hasPel;
        Mode mode;
        // 2026-11-26 multiplayer
        uint32_t netId, netHash;   // of the clip the layer started with (kept when frozen)
        float tEval;               // clip time of the last EvalLayer (sent)
        float netT, netW, netSpeed, netWSpeed;   // previous export (speeds)
        bool netHave;
        float tFixed, wFixed, legFixed;           // remote layer (ApplyRemote): fixed time / final weight / leg weight; -1 = local
    };
    int BlendLayers(FTr* cs, const ClipSet* set, const GameState& gs, float dt, bool newPop, bool remote);
    void Rebind(const ClipSet* set);
    int FindLiveLayer(const Clip* c, bool mirrored, bool dis = false) const;
    void Freeze(Layer& L);
    void StartLayer(const Clip* c, bool mirrored, uint64_t mirrorStamp, bool keepPhase, bool dis = false);
    // 2026-11-25: biggest bone gap (deg) between what two layers write (nullptr = the game's pose), for distance fades
    float PoseGap(const Layer* a, const Layer* b);
    Layer gapLayer_{};                // scratch: the incoming clip evaluated once to measure the gap
    Quat gapA_[kMaxBones] = {}, gapB_[kMaxBones] = {};
    void EvalLayer(Layer& L, const GameState& gs);
    const Clip* Choose(const ClipSet* set, const GameState& gs, bool trickOn) const;

    int n_ = 0;
    int parent_[kMaxBones] = {};
    MirrorMap map_;
    std::vector<int> conv_;
    volatile float masterTarget_ = 1.f;
    float master_ = 1.f;
    uint64_t gen_ = 0;
    bool haveGen_ = false;
    int nLayers_ = 0;
    Layer layers_[kMaxLayers];
    Quat game_[kMaxBones] = {};
    Quat local_[kMaxBones] = {};
    float localP_[kMaxBones][3] = {};
    bool dirty_[kMaxBones] = {};
    bool leg_[kMaxBones] = {};   // leg bone (SetLegBones); none until it is called
    int legRoot_[kMaxBones] = {};   // P6: first leg bone of the chain (the thigh); -1 outside the legs
    float legWrote_[kMaxBones] = {};   // P6: weight written by our clips on each leg chain (index = its thigh)
    bool postReady_ = false;
    float outP_[kMaxBones][3] = {};   // P6: local positions we compose with (game ones, own lengths for the legs we write)
    float refP_[kMaxBones][3] = {};   // P6: reference local positions (bone lengths), SetRefPositions
    bool haveRef_ = false;
    // P6 anti-stretch: for kAntiHold s after our clips last wrote a leg, PostApply keeps the thigh and shin at their
    // own length: the knee is re-solved (two-bone IK) so that it follows the foot, whoever placed the foot.
    static constexpr float kAntiHold = 0.8f;   // s
    static constexpr float kAntiFade = 0.3f;   // the last 0.3 s fade it out
    float antiT_ = 0.f;
    int chainMid_[kMaxBones] = {};   // leg root (thigh) -> its calf, -1 if none
    int chainEnd_[kMaxBones] = {};   // leg root (thigh) -> its foot, -1 if none
    Quat postQ_[kMaxBones] = {};
    float postP_[kMaxBones][3] = {};
    PelvisRig rig_;                  // P6 lot 2
    CycleTracker cyc_;               // 2026-11-25: game's step cycle, fed with the game's feet every frame
    float legW_ = 1.f;
    uint32_t pop_ = 0;
    bool havePop_ = false;
    bool trickDone_ = false;   // the trick clip of the current pop played to its end: faded out, not restarted before the next pop
    // lot 5: smoother hand-overs between base-pose clips
    static constexpr float kDebounce = 0.10f;   // s: a new base clip must win this long before it replaces the current one
    static constexpr float kGap = 0.20f;        // s: a gap (no clip) shorter than this keeps the current base clip
    static constexpr float kMinCross = 0.12f;   // s: shortest cross-fade between two clips
    // 2026-11-25: a fade between base poses lasts at least gap / kFadeSpeed (a 75 deg arm offset gone in 0.25 s read as a jump)
    static constexpr float kFadeSpeed = 180.f;   // deg / s
    static constexpr float kFadeMax = 0.8f;      // s
    char lastRide_[260] = {};                    // file of the last riding clip that played (its dismount part)
    bool prevDis_ = false, disOn_ = false;
    const Clip* pend_ = nullptr;
    float pendT_ = 0.f, gapT_ = 0.f;
    bool prevKick_ = false;
};
