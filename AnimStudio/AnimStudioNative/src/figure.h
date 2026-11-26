// Original animations of figures (clips\_figures\<key>.json): file format and automatic recorder.
// P6: one figure per variant (trick or state x stance x feet, see trick.h), recorded as the body really is (no mirror).
// The recorder runs inside the FBT hook: it only copies into preallocated buffers (no allocation in Tick).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "math.h"
#include "player.h"

struct FigureRec {
    std::string figure;        // figure key (FigureKey) or a legacy name ("Manual", "NoseManual", "Kickflip")
    int version = 1;
    float fps = 30.f;
    int bones = 0;
    std::vector<float> times, ratios;
    std::vector<int> phases;    // trickPhase per frame (tricks); empty for looping figures
    std::vector<Quat> frames;   // frames[i * bones + b]: local rotation of bone b at frame i (game pose, before our layer)
    // P6 (optional): local positions of a few bones (pelvis, board) per frame: positions[(i * posBones.size() + k) * 3 + c]
    std::vector<int> posBones;
    std::vector<float> positions;
};
std::string FigureToJson(const FigureRec& r);
// Rejects: empty figure name, bones <= 0, fewer than 2 frames, inconsistent sizes, unreadable JSON.
bool ParseFigure(const std::string& text, FigureRec& out, std::string& err);

// bones whose local position is recorded: the pelvis and the board skeleton (SKXX_*)
bool IsPositionBone(const std::string& name);

class FigureRecorder {
public:
    static constexpr int kMaxFrames = 91;     // 3.0 s at 30 fps (long enough to make a loop)
    static constexpr int kMinFrames = 91;     // 3.0 s: a shorter hold is dropped, retried next time
    static constexpr int kKickMax = 61;       // trick (one pass): 2.0 s at most in the air
    static constexpr int kGroundTail = 24;    // lot 5: then 0.8 s on the ground (phase 5), for the landing part of trick clips
    static constexpr int kGroundPhase = 5;    // phase value of the frames recorded after the ground contact
    static constexpr int kKickMin = 16;       // 0.5 s at least, otherwise dropped
    static constexpr int kMaxBones = 256;
    static constexpr int kMaxPos = 32;

    // Tick: the hook. TakeFinished: the game thread. Reset: under the player's lock.
    void Reset(int bones, const std::vector<int>& posBones = {});   // new skeleton: drops recordings in progress / unread
    // gs.figId / gs.figWanted say which figure is on and whether it must be recorded. gamePos may be null.
    void Tick(const GameState& gs, const Quat* gameLocals, const float (*gamePos)[3], int bones, float dt);
    // copies a finished recording (out.figure is empty: figId tells which one); false if none
    bool TakeFinished(FigureRec& out, int& figId);
    bool Recording() const { return active_ != 0; }

private:
    void Sample(const GameState& gs, const Quat* locals, const float (*pos)[3], int phase = -1);
    void Finish();
    void Start(const GameState& gs, bool loops, const Quat* locals, const float (*pos)[3]);

    std::atomic<bool> finished_{false};
    int bones_ = 0;
    int active_ = 0;           // figId being recorded (0 = none)
    bool activeLoops_ = false;
    bool activeShort_ = false;
    bool wantStart_ = false;     // the figure id changed: start a loop recording as soon as the recorder is free   // P6 lot 4: short state (crouch, landing): kKickMin frames are enough
    int finishedId_ = 0;
    bool finishedLoops_ = false;
    int frames_ = 0;
    int tail_ = -1;            // lot 5: frames recorded on the ground after a trick (-1: still in the air)
    float accum_ = 0.f;
    int prevFig_ = 0;
    bool havePop_ = false;     // like the player: the first pop number seen only counts if a trick is on
    uint32_t pop_ = 0;
    int phases_[kMaxFrames] = {};
    std::vector<Quat> buf_;    // kMaxFrames * bones_
    std::vector<int> posBones_;
    std::vector<float> posBuf_; // kMaxFrames * posBones_ * 3
    float times_[kMaxFrames] = {};
    float ratios_[kMaxFrames] = {};
};
