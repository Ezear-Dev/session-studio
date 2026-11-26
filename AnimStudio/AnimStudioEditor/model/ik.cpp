#include "ik.h"
#include <cmath>

namespace {
constexpr float kRad = 3.14159265358979f / 180.f;
float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void Cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
float Len(const float a[3]) { return std::sqrt(Dot(a, a)); }
bool Norm(float a[3]) {
    const float n = Len(a);
    if (!(n > 1e-9f)) return false;
    a[0] /= n; a[1] /= n; a[2] /= n;
    return true;
}
} // namespace

Quat ShortestArc(const float a0[3], const float b0[3]) {
    float a[3] = {a0[0], a0[1], a0[2]}, b[3] = {b0[0], b0[1], b0[2]};
    if (!Norm(a) || !Norm(b)) return QIdent;
    const float d = Dot(a, b);
    if (d < -0.999999f) {   // opposite: 180 degrees around any perpendicular axis
        float axis[3];
        const float x[3] = {1.f, 0.f, 0.f}, y[3] = {0.f, 1.f, 0.f};
        Cross(a, std::fabs(a[0]) < 0.9f ? x : y, axis);
        Norm(axis);
        return Quat{axis[0], axis[1], axis[2], 0.f};
    }
    float c[3];
    Cross(a, b, c);
    return QNorm(Quat{c[0], c[1], c[2], 1.f + d});
}

Quat AxisAngleDeg(const float axis[3], float deg) {
    const float h = deg * kRad * 0.5f, s = std::sin(h);
    return QNorm(Quat{axis[0] * s, axis[1] * s, axis[2] * s, std::cos(h)});
}

bool SolveTwoBoneIK(const FTr& u, const FTr& f, const FTr& h, const float target[3], float elbowDeltaDeg,
                    bool keepHandWorld, Quat& outU, Quat& outF, Quat& outH) {
    const float pu[3] = {u.px, u.py, u.pz};
    const float uf[3] = {f.px - u.px, f.py - u.py, f.pz - u.pz};
    const float fh[3] = {h.px - f.px, h.py - f.py, h.pz - f.pz};
    const float a = Len(uf), b = Len(fh);
    if (!(a > 1e-4f) || !(b > 1e-4f) || !std::isfinite(target[0] + target[1] + target[2]) || !std::isfinite(elbowDeltaDeg)) return false;
    // shoulder -> target distance, clamped to the reach
    float dir[3] = {target[0] - pu[0], target[1] - pu[1], target[2] - pu[2]};
    float dist = Len(dir);
    if (!Norm(dir)) {   // target on the shoulder: current hand direction
        dir[0] = uf[0] + fh[0]; dir[1] = uf[1] + fh[1]; dir[2] = uf[2] + fh[2];
        if (!Norm(dir)) return false;
        dist = 0.f;
    }
    const float lo = std::fabs(a - b) * 1.001f + 1e-3f, hi = (a + b) * 0.999f;
    dist = dist < lo ? lo : dist > hi ? hi : dist;
    // 1) elbow hinge: arm x forearm axis (fallback if the arm is straight)
    float hinge[3];
    Cross(uf, fh, hinge);
    if (Len(hinge) < 1e-3f * a * b) {
        const float down[3] = {0.f, 0.f, -1.f}, x[3] = {1.f, 0.f, 0.f};
        Cross(uf, down, hinge);
        if (Len(hinge) < 1e-3f * a) Cross(uf, x, hinge);
    }
    if (!Norm(hinge)) return false;
    float c = (a * a + b * b - dist * dist) / (2.f * a * b);
    c = c < -1.f ? -1.f : c > 1.f ? 1.f : c;
    const float bendWanted = 3.14159265358979f - std::acos(c);            // 0 = straight arm
    float cr[3];
    Cross(uf, fh, cr);
    const float bendNow = std::atan2(Dot(cr, hinge), Dot(uf, fh));          // signed angle around the hinge
    const Quat rh = AxisAngleDeg(hinge, (bendWanted - bendNow) / kRad);
    float fh2[3] = {fh[0], fh[1], fh[2]};
    QRotVec(rh, fh2[0], fh2[1], fh2[2]);
    // 2) the whole chain swings towards the target; 3) then turns around the shoulder -> target axis
    const float chain[3] = {uf[0] + fh2[0], uf[1] + fh2[1], uf[2] + fh2[2]};
    const float goal[3] = {dir[0] * dist, dir[1] * dist, dir[2] * dist};
    const Quat rs = ShortestArc(chain, goal);
    const Quat rt = AxisAngleDeg(dir, elbowDeltaDeg);
    const Quat r = QMul(rt, rs);
    outU = QNorm(QMul(r, TrQ(u)));
    outF = QNorm(QMul(r, QMul(rh, TrQ(f))));
    outH = keepHandWorld ? QNorm(TrQ(h)) : QNorm(QMul(r, QMul(rh, TrQ(h))));
    return true;
}

float SwivelToward(const FTr& u, const FTr& f, const FTr& e, const float target[3]) {
    const float nan = std::nanf("");
    float axis[3] = {e.px - u.px, e.py - u.py, e.pz - u.pz};
    if (!Norm(axis) || !std::isfinite(target[0] + target[1] + target[2])) return nan;
    float v[3] = {f.px - u.px, f.py - u.py, f.pz - u.pz};
    float w[3] = {target[0] - u.px, target[1] - u.py, target[2] - u.pz};
    const float dv = Dot(v, axis), dw = Dot(w, axis);
    for (int k = 0; k < 3; k++) { v[k] -= dv * axis[k]; w[k] -= dw * axis[k]; }   // projections in the plane perpendicular to the axis
    const float lv = Len(v);
    if (!(lv > 1e-3f) || !(Len(w) > 1e-3f * (lv > 1.f ? lv : 1.f))) return nan;
    float c[3];
    Cross(v, w, c);
    return std::atan2(Dot(c, axis), Dot(v, w)) / kRad;
}

bool AimBone(const FTr& b, const FTr& child, const float target[3], Quat& out) {
    const float cur[3] = {child.px - b.px, child.py - b.py, child.pz - b.pz};
    const float want[3] = {target[0] - b.px, target[1] - b.py, target[2] - b.pz};
    if (!(Len(cur) > 1e-4f) || !(Len(want) > 1e-4f) || !std::isfinite(want[0] + want[1] + want[2])) return false;
    out = QNorm(QMul(ShortestArc(cur, want), TrQ(b)));
    return true;
}
