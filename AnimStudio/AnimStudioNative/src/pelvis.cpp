#include "pelvis.h"
#include <cmath>

namespace {
float Len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
Quat ArcQ(const float a[3], const float b[3]) {   // shortest rotation taking direction a to direction b
    const float la = Len3(a), lb = Len3(b);
    if (la < 1e-6f || lb < 1e-6f) return QIdent;
    const float ax = a[0] / la, ay = a[1] / la, az = a[2] / la, bx = b[0] / lb, by = b[1] / lb, bz = b[2] / lb;
    const float d = ax * bx + ay * by + az * bz;
    if (d < -0.99999f) {
        float px = -ay, py = ax, pz = 0.f;
        if (std::fabs(az) > 0.9f) { px = 0.f; py = -az; pz = ay; }
        const float lp = std::sqrt(px * px + py * py + pz * pz);
        return Quat{px / lp, py / lp, pz / lp, 0.f};
    }
    return QNorm(Quat{ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx, 1.f + d});
}
void SetQ(FTr& t, Quat q) { t.qx = q.x; t.qy = q.y; t.qz = q.z; t.qw = q.w; }
} // namespace

PelvisRig BuildPelvisRig(const std::vector<std::string>& names, const int* parents, int n) {
    PelvisRig r;
    if (n <= 0 || (int)names.size() < n) return r;
    auto find = [&](const char* s) { for (int i = 0; i < n; i++) if (names[i] == s) return i; return -1; };
    r.pelvis = find("AMXX_Pelvis");
    r.thigh[0] = find("AMXX_L_Thigh"); r.calf[0] = find("AMXX_L_Calf"); r.foot[0] = find("AMXX_L_Foot");
    r.thigh[1] = find("AMXX_R_Thigh"); r.calf[1] = find("AMXX_R_Calf"); r.foot[1] = find("AMXX_R_Foot");
    if (!r.Ok()) return PelvisRig{};
    for (int s = 0; s < 2; s++)   // the chain must be thigh -> calf -> foot, under the pelvis
        if (parents[r.calf[s]] != r.thigh[s] || parents[r.foot[s]] != r.calf[s] || parents[r.thigh[s]] != r.pelvis) return PelvisRig{};
    r.moves.assign(n, 0);
    r.footSide.assign(n, -1);
    for (int i = 0; i < n; i++) {
        const int p = parents[i];
        if (i == r.foot[0]) r.footSide[i] = 0;
        else if (i == r.foot[1]) r.footSide[i] = 1;
        else if (p >= 0 && p < i) r.footSide[i] = r.footSide[p];
        const bool under = i == r.pelvis || (p >= 0 && p < i && r.moves[p]);
        r.moves[i] = under && r.footSide[i] < 0;
    }
    return r;
}

bool HipAxis(const FTr* cs, const PelvisRig& r, float out[3]) {
    if (!r.Ok() || !cs) return false;
    const FTr &L = cs[r.thigh[0]], &R = cs[r.thigh[1]];
    out[0] = L.px - R.px; out[1] = L.py - R.py; out[2] = L.pz - R.pz;
    const float l = Len3(out);
    if (l < 1e-3f) return false;
    for (int k = 0; k < 3; k++) out[k] /= l;
    return true;
}

void MirrorOffset(const float a[3], float v[3]) {
    const float d = v[0] * a[0] + v[1] * a[1] + v[2] * a[2];
    for (int k = 0; k < 3; k++) v[k] -= 2.f * d * a[k];
}

float PelvisReachScale(const FTr* cs, const PelvisRig& r, const float o[3]) {
    // largest s in [0, 1] such that, the pelvis moved by s * o, each foot stays within its leg's reach (feet planted)
    const float oo = o[0] * o[0] + o[1] * o[1] + o[2] * o[2];
    if (!(oo > 1e-8f)) return 1.f;
    float s = 1.f;
    for (int k = 0; k < 2; k++) {
        const FTr &T = cs[r.thigh[k]], &K = cs[r.calf[k]], &F = cs[r.foot[k]];
        const float hk[3] = {K.px - T.px, K.py - T.py, K.pz - T.pz}, kf[3] = {F.px - K.px, F.py - K.py, F.pz - K.pz};
        const float L1 = Len3(hk), L2 = Len3(kf);
        if (L1 < 1.f || L2 < 1.f) continue;
        const float hi = L1 + L2 - 0.5f;   // a hair short of a locked knee
        const float w[3] = {F.px - T.px, F.py - T.py, F.pz - T.pz};
        const float wo = w[0] * o[0] + w[1] * o[1] + w[2] * o[2], ww = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
        float sk;
        if (ww >= hi * hi) sk = wo > 0.f ? 1.f : 0.f;   // already stretched by the game: only toward the foot
        else sk = (wo + std::sqrt(wo * wo - oo * (ww - hi * hi))) / oo;
        if (sk < s) s = sk < 0.f ? 0.f : sk;
    }
    return s;
}

bool ApplyPelvisOffset(FTr* cs, int n, const PelvisRig& r, const float offIn[3]) {
    if (!cs || !r.Ok() || (int)r.moves.size() != n) return false;
    if (!(Len3(offIn) > 1e-3f) || !std::isfinite(offIn[0] + offIn[1] + offIn[2])) return false;
    // 2026-11-25: the pelvis never goes past the legs' reach (a crouch raised by 35 cm lifted the feet off the board)
    const float sc = PelvisReachScale(cs, r, offIn);
    const float off[3] = {offIn[0] * sc, offIn[1] * sc, offIn[2] * sc};
    if (!(Len3(off) > 1e-3f)) return false;
    float kOld[2][3], fPos[2][3];
    for (int s = 0; s < 2; s++) {
        const FTr &K = cs[r.calf[s]], &F = cs[r.foot[s]];
        kOld[s][0] = K.px; kOld[s][1] = K.py; kOld[s][2] = K.pz;
        fPos[s][0] = F.px; fPos[s][1] = F.py; fPos[s][2] = F.pz;
    }
    for (int i = 0; i < n; i++) if (r.moves[i]) { cs[i].px += off[0]; cs[i].py += off[1]; cs[i].pz += off[2]; }
    for (int s = 0; s < 2; s++) {
        FTr &T = cs[r.thigh[s]], &K = cs[r.calf[s]];
        const float H[3] = {T.px, T.py, T.pz}, Kp[3] = {K.px, K.py, K.pz};
        const float* F = fPos[s];
        const float hk[3] = {Kp[0] - H[0], Kp[1] - H[1], Kp[2] - H[2]};
        const float kf[3] = {F[0] - kOld[s][0], F[1] - kOld[s][1], F[2] - kOld[s][2]};
        const float L1 = Len3(hk), L2 = Len3(kf);
        float v[3] = {F[0] - H[0], F[1] - H[1], F[2] - H[2]};
        const float d = Len3(v);
        if (L1 < 1.f || L2 < 1.f || d < 1e-3f) continue;
        const float u[3] = {v[0] / d, v[1] / d, v[2] / d};
        const float lo = std::fabs(L1 - L2) + 0.01f, hi = L1 + L2 - 0.01f;
        const float dc = d < lo ? lo : d > hi ? hi : d;
        const float a = (L1 * L1 - L2 * L2 + dc * dc) / (2.f * dc);
        const float hgt = std::sqrt(L1 * L1 - a * a > 0.f ? L1 * L1 - a * a : 0.f);
        float kp[3] = {hk[0], hk[1], hk[2]};   // the knee keeps its side
        const float ku = kp[0] * u[0] + kp[1] * u[1] + kp[2] * u[2];
        for (int k = 0; k < 3; k++) kp[k] -= ku * u[k];
        float lk = Len3(kp);
        if (lk < 1e-4f) {   // straight leg: bend toward the thigh's local +Y
            kp[0] = 0.f; kp[1] = 1.f; kp[2] = 0.f;
            QRotVec(TrQ(T), kp[0], kp[1], kp[2]);
            const float k2 = kp[0] * u[0] + kp[1] * u[1] + kp[2] * u[2];
            for (int k = 0; k < 3; k++) kp[k] -= k2 * u[k];
            lk = Len3(kp);
            if (lk < 1e-4f) continue;
        }
        float Kn[3], Fn[3];
        for (int k = 0; k < 3; k++) { Kn[k] = H[k] + u[k] * a + kp[k] / lk * hgt; Fn[k] = H[k] + u[k] * dc; }
        const float hk1[3] = {Kn[0] - H[0], Kn[1] - H[1], Kn[2] - H[2]};
        const Quat qa = ArcQ(hk, hk1);
        const Quat qT2 = QNorm(QMul(qa, TrQ(T)));
        // the calf pointed at where the foot would be if it had moved with the pelvis
        float kf0[3] = {F[0] + off[0] - Kp[0], F[1] + off[1] - Kp[1], F[2] + off[2] - Kp[2]};
        QRotVec(qa, kf0[0], kf0[1], kf0[2]);
        const float kf1[3] = {Fn[0] - Kn[0], Fn[1] - Kn[1], Fn[2] - Kn[2]};
        const Quat qC2 = QNorm(QMul(ArcQ(kf0, kf1), QMul(qa, TrQ(K))));
        SetQ(T, qT2);
        SetQ(K, qC2);
        K.px = Kn[0]; K.py = Kn[1]; K.pz = Kn[2];
        // the foot keeps its orientation; out of reach, it (and its toes) follow the straight leg
        const float sh[3] = {Fn[0] - F[0], Fn[1] - F[1], Fn[2] - F[2]};
        if (Len3(sh) > 1e-4f)
            for (int i = 0; i < n; i++) if (r.footSide[i] == s) { cs[i].px += sh[0]; cs[i].py += sh[1]; cs[i].pz += sh[2]; }
    }
    return true;
}
