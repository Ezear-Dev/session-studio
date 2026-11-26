// AnimStudio Editor -- EditorDoc: shoulder check and fix (2026-11-25, user report: scratching the head deforms the shoulder).
// The game's mesh has no twist bones and weights the shoulder between Spine3, Clavicle and UpperArm. A real shoulder
// lifts the clavicle / scapula once the arm goes above ~60-90 deg; an upper arm raised 110 deg by itself, rolled 54 deg on
// its own axis, crushes the skin. Fix: give a share of the elevation to the clavicle, pick the elbow swivel where the
// upper arm rolls least, move half the forearm roll to the wrist. The hand stays where the user put it.
#include "editor_doc.h"
#include "ik.h"
#include <cmath>

namespace {
constexpr float kDeg = 57.2957795f;
float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void Cross3(const float a[3], const float b[3], float o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
bool Unit(float a[3]) { const float l = std::sqrt(Dot3(a, a)); if (!(l > 1e-6f)) return false; for (int k = 0; k < 3; k++) a[k] /= l; return true; }
// roll (deg) of rotation q around the unit axis a (swing-twist decomposition)
float TwistDeg(Quat q, const float a[3]) {
    float t = 2.f * std::atan2(q.x * a[0] + q.y * a[1] + q.z * a[2], q.w) * kDeg;
    while (t > 180.f) t -= 360.f;
    while (t < -180.f) t += 360.f;
    return t;
}
void Sub(const FTr& a, const FTr& b, float o[3]) { o[0] = a.px - b.px; o[1] = a.py - b.py; o[2] = a.pz - b.pz; }
float AngleDeg(const float a[3], const float b[3]) {
    float x[3] = {a[0], a[1], a[2]}, y[3] = {b[0], b[1], b[2]};
    if (!Unit(x) || !Unit(y)) return 0.f;
    const float d = Dot3(x, y);
    return std::acos(d < -1.f ? -1.f : d > 1.f ? 1.f : d) * kDeg;
}
} // namespace

int EditorDoc::ArmSide(int bone) const {
    if (bone < 0 || bone >= BoneCount()) return -1;
    const std::string& n = skel_.names[bone];
    const bool arm = n.find("Clavicle") != std::string::npos || n.find("UpperArm") != std::string::npos ||
                     n.find("Forearm") != std::string::npos || n.find("_Hand") != std::string::npos || n.find("Finger") != std::string::npos;
    if (!arm) return -1;
    return n.find("_L_") != std::string::npos ? 0 : n.find("_R_") != std::string::npos ? 1 : -1;
}

namespace {
struct ArmIdx { int c = -1, u = -1, f = -1, h = -1, s3 = -1, pel = -1; bool Ok() const { return c >= 0 && u >= 0 && f >= 0 && h >= 0 && s3 >= 0 && pel >= 0; } };
ArmIdx FindArm(const std::vector<std::string>& names, int side) {
    ArmIdx a;
    const std::string p = side == 0 ? "AMXX_L_" : "AMXX_R_";
    for (int i = 0; i < (int)names.size(); i++) {
        const std::string& n = names[i];
        if (n == p + "Clavicle") a.c = i;
        else if (n == p + "UpperArm") a.u = i;
        else if (n == p + "Forearm") a.f = i;
        else if (n == p + "Hand") a.h = i;
        else if (n == "AMXX_Spine3") a.s3 = i;
        else if (n == "AMXX_Pelvis") a.pel = i;
    }
    return a;
}
} // namespace

EditorDoc::ShoulderInfo EditorDoc::Shoulder(int side, float t) const {
    ShoulderInfo s;
    const ArmIdx a = FindArm(skel_.names, side);
    if (!a.Ok() || skel_.refPos.size() < (size_t)BoneCount() * 3) return s;
    std::vector<FTr> cs;
    ComponentPose(t, cs, false);
    float down[3], arm[3];
    Sub(cs[a.pel], cs[a.s3], down);
    Sub(cs[a.f], cs[a.u], arm);
    s.elevation = AngleDeg(arm, down);
    auto axisOf = [&](int child) { float v[3] = {skel_.refPos[(size_t)child * 3], skel_.refPos[(size_t)child * 3 + 1], skel_.refPos[(size_t)child * 3 + 2]}; Unit(v); return std::vector<float>(v, v + 3); };
    const std::vector<float> au = axisOf(a.f), af = axisOf(a.h);
    s.twistUpper = TwistDeg(QNorm(QMul(QInv(OriginFor(a.u, t)), Edited(a.u, t))), au.data());
    s.twistFore = TwistDeg(QNorm(QMul(QInv(OriginFor(a.f, t)), Edited(a.f, t))), af.data());
    // clavicle raise: its direction (toward the upper arm) with the game's local rotation vs the edited one
    const int cp = skel_.parents[a.c];
    const Quat pq = cp >= 0 ? TrQ(cs[cp]) : QIdent;
    const std::vector<float> ac = axisOf(a.u);
    float dO[3] = {ac[0], ac[1], ac[2]}, dE[3] = {ac[0], ac[1], ac[2]};
    const Quat qO = QMul(pq, OriginFor(a.c, t)), qE = TrQ(cs[a.c]);
    QRotVec(qO, dO[0], dO[1], dO[2]);
    QRotVec(qE, dE[0], dE[1], dE[2]);
    const float up[3] = {-down[0], -down[1], -down[2]};
    s.clavRaise = AngleDeg(dO, up) - AngleDeg(dE, up);   // + = raised
    s.warn = s.elevation - std::fmax(0.f, s.clavRaise) > kWarnElevation || std::fabs(s.twistUpper) > kWarnTwist || std::fabs(s.twistFore) > kWarnTwist * 1.5f;
    s.ok = true;
    return s;
}

bool EditorDoc::FixShoulder(int side, float t, bool clavicle, bool reduceTwist) {
    const ArmIdx a = FindArm(skel_.names, side);
    if (!a.Ok() || skel_.refPos.size() < (size_t)BoneCount() * 3) return false;
    if (!IsEditable(a.c) || !IsEditable(a.u) || !IsEditable(a.f) || !IsEditable(a.h)) return false;
    std::vector<FTr> cs;
    ComponentPose(t, cs, false);
    const float target[3] = {cs[a.h].px, cs[a.h].py, cs[a.h].pz};
    const Quat handQ = TrQ(cs[a.h]);
    float down[3], arm[3];
    Sub(cs[a.pel], cs[a.s3], down);
    if (!Unit(down)) return false;
    const float up[3] = {-down[0], -down[1], -down[2]};
    Sub(cs[a.f], cs[a.u], arm);
    const float elev = AngleDeg(arm, down);
    // 1) clavicle: a share of the elevation above kShoulderStart (never lowers what the user already raised)
    const ShoulderInfo info = Shoulder(side, t);
    float raise = 0.f;
    if (clavicle) {
        const float want = std::fmin(kShoulderMax, std::fmax(0.f, (elev - kShoulderStart) * kShoulderShare));
        if (want > info.clavRaise + 0.5f) raise = want - info.clavRaise;
    }
    Quat rc = QIdent;
    if (raise > 0.f) {
        float d[3]; Sub(cs[a.u], cs[a.c], d);
        float axis[3]; Cross3(d, up, axis);   // rotating d around d x up moves it toward up
        if (Unit(axis)) rc = AxisAngleDeg(axis, raise);
    }
    const float pc[3] = {cs[a.c].px, cs[a.c].py, cs[a.c].pz};
    auto carry = [&](FTr x) {   // rigid rotation of a bone of the arm around the clavicle's root
        float v[3] = {x.px - pc[0], x.py - pc[1], x.pz - pc[2]};
        QRotVec(rc, v[0], v[1], v[2]);
        x.px = pc[0] + v[0]; x.py = pc[1] + v[1]; x.pz = pc[2] + v[2];
        const Quat q = QNorm(QMul(rc, TrQ(x)));
        x.qx = q.x; x.qy = q.y; x.qz = q.z; x.qw = q.w;
        return x;
    };
    const Quat clavQ = QNorm(QMul(rc, TrQ(cs[a.c])));
    const FTr u = carry(cs[a.u]), f = carry(cs[a.f]), h = carry(cs[a.h]);
    // 2) the arm reaches the same hand again; with reduceTwist, the swivel where the upper arm rolls least
    float au[3] = {skel_.refPos[(size_t)a.f * 3], skel_.refPos[(size_t)a.f * 3 + 1], skel_.refPos[(size_t)a.f * 3 + 2]};
    float af[3] = {skel_.refPos[(size_t)a.h * 3], skel_.refPos[(size_t)a.h * 3 + 1], skel_.refPos[(size_t)a.h * 3 + 2]};
    if (!Unit(au) || !Unit(af)) return false;
    const Quat oU = OriginFor(a.u, t), oF = OriginFor(a.f, t);
    Quat bu, bf, bh;
    float bestCost = 1e30f;
    bool found = false;
    for (int phi = reduceTwist ? -90 : 0; phi <= (reduceTwist ? 90 : 0); phi++) {
        Quat qu, qf, qh;
        if (!SolveTwoBoneIK(u, f, h, target, (float)phi, true, qu, qf, qh)) continue;
        const Quat lu = QNorm(QMul(QInv(clavQ), qu));
        const float tw = TwistDeg(QNorm(QMul(QInv(oU), lu)), au);
        const float cost = std::fabs(tw) + 0.05f * std::fabs((float)phi);   // stay near the user's elbow when it barely helps
        if (cost < bestCost) { bestCost = cost; bu = qu; bf = qf; bh = qh; found = true; }
    }
    if (!found) return false;
    (void)bh;
    Quat lf = QNorm(QMul(QInv(bu), bf));
    if (reduceTwist) {   // 3) half the forearm roll goes to the wrist (the hand lies on the forearm axis: it does not move)
        const float tf = TwistDeg(QNorm(QMul(QInv(oF), lf)), af);
        lf = QNorm(QMul(lf, AxisAngleDeg(af, -tf * 0.5f)));
    }
    const Quat qf2 = QNorm(QMul(bu, lf));
    const int cp = skel_.parents[a.c];
    const Quat pq = cp >= 0 ? TrQ(cs[cp]) : QIdent;
    if (raise > 0.f) SetPending(a.c, QMul(QInv(pq), clavQ));
    SetPending(a.u, QMul(QInv(clavQ), bu));
    SetPending(a.f, lf);
    SetPending(a.h, QMul(QInv(qf2), handQ));
    return true;
}
