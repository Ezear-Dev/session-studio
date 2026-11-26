// 2026-11-25: step cycle sync (the idea of Unreal's sync markers). A walking clip that loops on its own clock drifts
// against the game's steps (an arm swing ends up out of time with the legs). Instead, the clip time follows the game's
// own cycle: the marker is the moment the LEFT foot passes the RIGHT one going forward (the signal "left foot minus
// right foot" along the walking axis crosses zero upward). One cycle = one full stride (left step + right step).
// Shared by the player (live, from the game's pose) and the editor (cut of the recorded figure), so both agree.
#pragma once
#include <vector>

// The signal: left foot minus right foot, in component space, on X and Y (the axis is picked by variance).
struct FootSample { float t; float lx, ly, rx, ry; };

// Live tracker (player). Step() once per frame with the game's feet; Phase() in [0, 1).
class CycleTracker {
public:
    void Reset();
    // dt in seconds; returns the phase after this frame
    float Step(float lx, float ly, float rx, float ry, float dt);
    float Phase() const { return phase_; }
    float Period() const { return period_; }
    int Markers() const { return markers_; }   // markers seen since Reset (journal)
    bool Locked() const { return markers_ >= 2; }

private:
    float phase_ = 0.f, period_ = 1.f;
    float meanX_ = 0.f, meanY_ = 0.f, varX_ = 0.f, varY_ = 0.f, amp_ = 0.f;
    float prevS_ = 0.f, since_ = 0.f, pendingCorr_ = 0.f;
    bool armed_ = false, havePrev_ = false, primed_ = false;
    int markers_ = 0;
};

// Offline cut (editor): markers (times) of a recorded walk, same rule as the tracker (global mean / axis).
std::vector<float> CycleMarkers(const std::vector<FootSample>& s);
// First full cycle [start, end) of a recording; false if fewer than two markers
bool FirstCycle(const std::vector<FootSample>& s, float& start, float& end);
