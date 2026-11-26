// Session Anim Loader -- landing state length (2026-11-26), see landing_hold.h
#include "landing_hold.h"

double LandingHold(double clipLen) {
    if (!(clipLen > kLandHoldMin)) return kLandHoldMin;   // also NaN
    return clipLen < kLandHoldMax ? clipLen : kLandHoldMax;
}

bool LandingWindow::Active(double now, double hold, bool other) {
    const double e = now - at_;
    if (e < 0.0 || e >= hold) return false;
    if (e >= kLandHoldMin && other) { Stop(); return false; }
    return true;
}
