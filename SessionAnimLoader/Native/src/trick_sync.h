// Trick clips synced to the real jump (2026-11-25, LANDING-SYNC-design.md).
// A trick clip carries the instants of the game's own events in the recorded figure (markers: airborne, catch,
// landing preparation = IsLanding, ground contact). Live, the clip time is warped so that each marker falls on the
// same live event: pop -> air -> catch play at real speed, the hang time (catch -> IsLanding) and the last part
// (IsLanding -> ground) stretch or shrink with the jump height, predicted from the vertical speed. The clip ends at
// the ground contact; the landing is a clip of its own (state Landing + trick name).
#pragma once
#include <vector>

struct TrickMarkers {
    bool ok = false;
    float air = 0.f, catchT = 0.f, land = 0.f, ground = 0.f;   // clip seconds, 0 <= air <= catch <= land <= ground
};

// Markers of a recorded trick figure (phase per frame: 0 pop, 1 air, 2 flip, 3 catch, 4 IsLanding, 5 ground).
// Missing events: air -> first event after the pop (or 0), catch -> air (no catch: the hang starts at takeoff),
// land -> ground, ground -> last frame. ok = false without phases.
TrickMarkers MarkersFromPhases(const std::vector<int>& phases, const std::vector<float>& times);

// Seconds until the skater is back at the takeoff height (flat ground): vz cm/s (up +), dz cm above the takeoff, g cm/s^2.
// 0 if already below, -1 if g is not usable.
float PredictGround(float vz, float dz, float g);

// Gravity measured from the vertical speed while airborne (the game's own value, gravity scale included).
class GravityMeter {
public:
    static constexpr float kDefault = 980.f;
    void Reset() { have_ = false; }
    void Feed(float vz, float dt, bool airborne);
    float G() const { return g_; }
    int Samples() const { return n_; }
private:
    float g_ = kDefault, prevVz_ = 0.f;
    bool have_ = false;
    int n_ = 0;
};

// What the game really does after IsLanding, learned per trick (2026-11-26, first test: the ballistic prediction is
// right for ollies but about twice too long for flip tricks once IsLanding is up; IsLanding -> ground lasts 0.10-0.23 s
// on flips, 0.30-0.40 s on ollies). lead = live seconds from IsLanding to the ground signal, factor = real / predicted
// at IsLanding (applied to the prediction from IsLanding on). Plain data.
struct LandLearn {
    float lead = -1.f, factor = 1.f;
    int n = 0;
    void Learn(float realLead, float predAtLand);
};

// Clip time of one trick layer. Plain data (copied with the layer).
struct TrickClock {
    static constexpr float kMinRate = 0.25f;   // the game is late: the clip creeps, it never freezes
    static constexpr float kMaxRate = 3.0f;    // the game is early: catch up at most 3x
    static constexpr float kFinish = 0.10f;    // s: at the ground contact the clip runs to its ground marker in this time
    float t = 0.f;           // clip time
    float live = 0.f;        // live seconds since the pop
    float segStart = 0.f;    // live time when the current section started
    int seg = 0;             // live section: 0 pop->air, 1 air->catch, 2 catch->land (hang), 3 land->ground, 4 on the ground
    // One frame. phase = live trick phase (0..4), ground = the trick is back on the ground, predGround = live seconds to
    // the contact (-1 unknown). Without markers: real time, held at the end. Returns the clip time.
    // landLead = learned live IsLanding -> ground (s, -1: the recording's own length), see LandLearn.
    float Step(const TrickMarkers& m, float duration, int phase, bool ground, float predGround, float dt, float landLead = -1.f);
};
