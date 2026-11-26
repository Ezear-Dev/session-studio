// Session Anim Loader -- how long the "landing" base state lasts after the ground contact (2026-11-26).
// Before: always 0.8 s, so a landing clip longer than that was cut. Now: as long as the landing clip that plays, from
// kLandHoldMin to kLandHoldMax. Past kLandHoldMin, any other action ends the landing for good.
#pragma once

constexpr double kLandHoldMin = 0.8;   // the landing always lasts this long (the behaviour before 2026-11-26)
constexpr double kLandHoldMax = 2.0;   // a landing clip plays up to this long (user, 2026-11-26)

// landing state length for a landing clip of clipLen seconds (0 or less: no landing clip)
double LandingHold(double clipLen);

class LandingWindow {
public:
    void Start(double now) { at_ = now; }
    void Stop() { at_ = -100.0; }
    double Since(double now) const { return now - at_; }
    // is the landing still on? hold: LandingHold of the clip. other: another action this frame (trick, crouch, push,
    // manual, grind, powerslide, grab, off the board). Before kLandHoldMin the caller decides what wins, as before;
    // from kLandHoldMin on, another action stops the landing (it does not come back when the action ends).
    bool Active(double now, double hold, bool other);

private:
    double at_ = -100.0;
};
