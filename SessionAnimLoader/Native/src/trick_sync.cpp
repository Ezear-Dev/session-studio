#include "trick_sync.h"
#include <cmath>

TrickMarkers MarkersFromPhases(const std::vector<int>& phases, const std::vector<float>& times) {
    TrickMarkers m;
    const size_t n = phases.size() < times.size() ? phases.size() : times.size();
    if (n < 2) return m;
    float air = -1.f, cat = -1.f, land = -1.f, ground = -1.f;
    for (size_t i = 0; i < n; i++) {
        const int p = phases[i];
        if (p >= 1 && air < 0.f) air = times[i];
        if (p == 3 && cat < 0.f) cat = times[i];
        if (p == 4 && land < 0.f) land = times[i];
        if (p == 5 && ground < 0.f) ground = times[i];
    }
    if (air < 0.f) return m;   // never left the ground: no trick recorded
    if (ground < 0.f) ground = times[n - 1];
    if (cat < 0.f) cat = air;          // no catch (ollie): the hang time starts at the takeoff
    if (land < 0.f) land = ground;
    if (cat < air) cat = air;
    if (land < cat) land = cat;
    if (ground < land) ground = land;
    m.ok = ground > 0.f;
    m.air = air; m.catchT = cat; m.land = land; m.ground = ground;
    return m;
}

float PredictGround(float vz, float dz, float g) {
    if (!(g > 100.f) || !std::isfinite(vz) || !std::isfinite(dz)) return -1.f;
    const float disc = vz * vz + 2.f * g * dz;
    if (disc <= 0.f) return 0.f;
    const float t = (vz + std::sqrt(disc)) / g;
    return t > 0.f ? t : 0.f;
}

void GravityMeter::Feed(float vz, float dt, bool airborne) {
    if (!airborne || !std::isfinite(vz)) { have_ = false; return; }
    if (!have_) { prevVz_ = vz; have_ = true; return; }
    if (dt > 1e-3f) {
        const float gi = (prevVz_ - vz) / dt;
        if (gi > 200.f && gi < 8000.f) {   // a repeated sample (0) or a hit (huge) is not gravity
            g_ = n_ == 0 ? gi : g_ + 0.2f * (gi - g_);
            n_++;
        }
    }
    prevVz_ = vz;
}

void LandLearn::Learn(float realLead, float predAtLand) {
    if (!(realLead >= 0.02f && realLead <= 1.0f)) return;   // no IsLanding, a pause, a drop that never raised it...
    lead = n == 0 ? realLead : lead + 0.3f * (realLead - lead);
    if (predAtLand > 0.02f) {
        float r = realLead / predAtLand;
        r = r < 0.3f ? 0.3f : r > 1.5f ? 1.5f : r;
        factor = n == 0 ? r : factor + 0.3f * (r - factor);
    }
    n++;
}

float TrickClock::Step(const TrickMarkers& m, float duration, int phase, bool ground, float predGround, float dt, float landLead) {
    if (!(dt > 0.f) || !std::isfinite(dt)) dt = 0.f;
    live += dt;
    if (!m.ok) {   // old clip without markers: real time from the pop, held at the end
        t += dt;
        if (t > duration) t = duration;
        return t;
    }
    int ls = ground ? 4 : phase >= 4 ? 3 : phase >= 3 ? 2 : phase >= 1 ? 1 : 0;
    if (ls == 1 && m.catchT <= m.air + 1e-4f) ls = 2;   // a trick with no catch: the hang starts at the takeoff
    if (ls > seg) { seg = ls; segStart = live - dt; }
    const float mk[5] = {0.f, m.air, m.catchT, m.land, m.ground};
    float rate = 1.f;
    if (seg >= 4) {   // on the ground: reach the ground pose quickly (the landing clip cross-fades in meanwhile)
        if (t < m.ground) { rate = (m.ground - t) / kFinish; if (rate < 1.f) rate = 1.f; }
        t += rate * dt;
    } else {
        const float left = mk[seg + 1] - t;
        float R = (mk[seg + 1] - mk[seg]) - (live - segStart);   // as long as in the recording
        if (seg == 2 && predGround >= 0.f) R = predGround - (landLead >= 0.f ? landLead : m.ground - m.land);   // hang time: ends at IsLanding
        else if (seg == 3 && predGround >= 0.f) R = predGround;
        if (left <= 1e-4f) rate = kMinRate;   // the game is late: creep
        else if (R <= 1e-3f) rate = kMaxRate;
        else { rate = left / R; rate = rate < kMinRate ? kMinRate : rate > kMaxRate ? kMaxRate : rate; }
        t += rate * dt;
        if (t > m.ground) t = m.ground;   // never past the ground pose while in the air
    }
    if (t > duration) t = duration;
    return t;
}
