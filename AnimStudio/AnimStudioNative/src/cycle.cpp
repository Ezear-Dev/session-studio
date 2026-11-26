#include "cycle.h"
#include <cmath>

namespace {
constexpr float kAvg = 1.5f;        // s, time constant of the running mean / variance / amplitude
constexpr float kMinAmp = 2.f;      // cm: below this mean |signal| the feet do not alternate (standing): no marker
constexpr float kMinPeriod = 0.35f, kMaxPeriod = 3.f;   // s, a full stride
constexpr float kCorrTime = 0.2f;   // s, a phase error found at a marker is taken back over this time (no jump)
float Hyst(float amp) { const float h = 0.4f * amp; return h > 1.f ? h : 1.f; }
float Wrap01(float p) { p -= std::floor(p); return p >= 1.f ? 0.f : p; }
float WrapHalf(float e) { e -= std::floor(e + 0.5f); return e; }   // [-0.5, 0.5)
} // namespace

void CycleTracker::Reset() { *this = CycleTracker{}; }

float CycleTracker::Step(float lx, float ly, float rx, float ry, float dt) {
    if (!std::isfinite(lx + ly + rx + ry)) return phase_;
    if (!std::isfinite(dt) || dt < 0.f || dt > 0.1f) dt = 0.f;
    const float dx = lx - rx, dy = ly - ry;
    if (!primed_) { primed_ = true; meanX_ = dx; meanY_ = dy; }
    const float a = dt > 0.f ? (dt / kAvg < 1.f ? dt / kAvg : 1.f) : 0.f;
    meanX_ += a * (dx - meanX_);
    meanY_ += a * (dy - meanY_);
    varX_ += a * ((dx - meanX_) * (dx - meanX_) - varX_);
    varY_ += a * ((dy - meanY_) * (dy - meanY_) - varY_);
    const float s = varX_ >= varY_ ? dx - meanX_ : dy - meanY_;
    amp_ += a * (std::fabs(s) - amp_);

    since_ += dt;
    phase_ += dt / period_;
    if (s < -Hyst(amp_)) armed_ = true;
    if (armed_ && havePrev_ && prevS_ < 0.f && s >= 0.f && amp_ >= kMinAmp) {
        const float frac = prevS_ / (prevS_ - s);       // where in this frame the zero was crossed
        const float age = dt * (1.f - frac);            // time since the crossing
        const float measured = since_ - age;
        if (markers_ >= 1 && measured >= kMinPeriod && measured <= kMaxPeriod)
            period_ = markers_ == 1 ? measured : period_ + 0.5f * (measured - period_);
        markers_++;
        since_ = age;
        armed_ = false;
        const float target = age / period_;
        if (markers_ <= 2) { phase_ = target; pendingCorr_ = 0.f; }   // first lock: straight to the marker
        else pendingCorr_ += -WrapHalf(phase_ - target);
    }
    if (pendingCorr_ != 0.f && dt > 0.f) {
        const float c = pendingCorr_ * (dt / kCorrTime < 1.f ? dt / kCorrTime : 1.f);
        phase_ += c;
        pendingCorr_ -= c;
    }
    phase_ = Wrap01(phase_);
    prevS_ = s;
    havePrev_ = true;
    return phase_;
}

std::vector<float> CycleMarkers(const std::vector<FootSample>& v) {
    std::vector<float> out;
    const int n = (int)v.size();
    if (n < 3) return out;
    double mx = 0, my = 0;
    for (auto& f : v) { mx += f.lx - f.rx; my += f.ly - f.ry; }
    mx /= n; my /= n;
    double vx = 0, vy = 0;
    for (auto& f : v) { vx += (f.lx - f.rx - mx) * (f.lx - f.rx - mx); vy += (f.ly - f.ry - my) * (f.ly - f.ry - my); }
    const bool useX = vx >= vy;
    std::vector<float> s(n);
    double amp = 0;
    for (int i = 0; i < n; i++) {
        s[i] = useX ? (float)(v[i].lx - v[i].rx - mx) : (float)(v[i].ly - v[i].ry - my);
        amp += std::fabs(s[i]);
    }
    amp /= n;
    if (amp < kMinAmp) return out;
    const float h = Hyst((float)amp);
    bool armed = false;
    for (int i = 0; i < n; i++) {
        if (s[i] < -h) armed = true;
        if (armed && i > 0 && s[i - 1] < 0.f && s[i] >= 0.f) {
            const float frac = s[i - 1] / (s[i - 1] - s[i]);
            out.push_back(v[i - 1].t + frac * (v[i].t - v[i - 1].t));
            armed = false;
        }
    }
    return out;
}

bool FirstCycle(const std::vector<FootSample>& s, float& start, float& end) {
    const std::vector<float> m = CycleMarkers(s);
    for (size_t i = 0; i + 1 < m.size(); i++) {
        const float d = m[i + 1] - m[i];
        if (d >= kMinPeriod && d <= kMaxPeriod) { start = m[i]; end = m[i + 1]; return true; }
    }
    return false;
}
