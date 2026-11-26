// SessionAnimLoaderNative maths: quaternions (UE FQuat convention: a*b = apply b then a), UE4.27 FTransform,
// local space <-> component space conversion. No dependency on UE4SS.
#pragma once
#include <cmath>

struct Quat { float x, y, z, w; };
constexpr Quat QIdent{0.f, 0.f, 0.f, 1.f};

struct alignas(16) FTr {          // FTransform UE4.27 (float) : FQuat, FVector + pad, FVector + pad
    float qx, qy, qz, qw;
    float px, py, pz, p_pad;
    float sx, sy, sz, s_pad;
};
static_assert(sizeof(FTr) == 48, "FTransform");

inline Quat QMul(Quat a, Quat b) {
    return Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat QInv(Quat q) { return Quat{-q.x, -q.y, -q.z, q.w}; }   // quaternions unitaires
inline float QDot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat QNorm(Quat q) {
    const float n = std::sqrt(QDot(q, q));
    if (!(n > 1e-12f)) return QIdent;
    const float i = 1.f / n;
    return Quat{q.x * i, q.y * i, q.z * i, q.w * i};
}

// slerp along the shortest path (q and -q are the same rotation)
inline Quat QSlerp(Quat a, Quat b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return (QDot(a, b) < 0.f) ? Quat{-b.x, -b.y, -b.z, -b.w} : b;
    float d = QDot(a, b);
    if (d < 0.f) { b = Quat{-b.x, -b.y, -b.z, -b.w}; d = -d; }
    float ka, kb;
    if (d > 0.9995f) { ka = 1.f - t; kb = t; }
    else {
        const float th = std::acos(d), s = std::sin(th);
        ka = std::sin((1.f - t) * th) / s;
        kb = std::sin(t * th) / s;
    }
    return QNorm(Quat{a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb});
}

// UE4 FRotator::Quaternion() (degrees)
inline Quat QFromRotatorDeg(float pitch, float yaw, float roll) {
    const float k = 3.14159265358979f / 360.f;
    const float SP = std::sin(pitch * k), CP = std::cos(pitch * k);
    const float SY = std::sin(yaw * k), CY = std::cos(yaw * k);
    const float SR = std::sin(roll * k), CR = std::cos(roll * k);
    return Quat{CR * SP * SY - SR * CP * CY,
                -CR * SP * CY - SR * CP * SY,
                CR * CP * SY - SR * SP * CY,
                CR * CP * CY + SR * SP * SY};
}

// v' = v + 2w(u x v) + 2 u x (u x v)
inline void QRotVec(Quat q, float& vx, float& vy, float& vz) {
    const float cx = q.y * vz - q.z * vy, cy = q.z * vx - q.x * vz, cz = q.x * vy - q.y * vx;
    const float dx = q.y * cz - q.z * cy, dy = q.z * cx - q.x * cz, dz = q.x * cy - q.y * cx;
    vx += 2.f * (q.w * cx + dx);
    vy += 2.f * (q.w * cy + dy);
    vz += 2.f * (q.w * cz + dz);
}

inline float Smooth(float a) { return a * a * (3.f - 2.f * a); }
inline float EaseSine(float a) { return 0.5f - 0.5f * std::cos(a * 3.14159265358979f); }

inline Quat TrQ(const FTr& t) { return Quat{t.qx, t.qy, t.qz, t.qw}; }

// Component space -> local (rotation and position; the scale of each bone is never touched).
// parent[i] < i for every bone (parent before child), -1 for the root.
inline void ToLocal(const FTr* cs, const int* parent, int n, Quat* outLocalQ, float (*outLocalP)[3]) {
    for (int i = 0; i < n; i++) {
        const int p = parent[i];
        if (p < 0) {
            outLocalQ[i] = TrQ(cs[i]);
            outLocalP[i][0] = cs[i].px; outLocalP[i][1] = cs[i].py; outLocalP[i][2] = cs[i].pz;
            continue;
        }
        const Quat ip = QInv(TrQ(cs[p]));
        outLocalQ[i] = QMul(ip, TrQ(cs[i]));
        float vx = cs[i].px - cs[p].px, vy = cs[i].py - cs[p].py, vz = cs[i].pz - cs[p].pz;
        QRotVec(ip, vx, vy, vz);
        outLocalP[i][0] = (cs[p].sx != 0.f) ? vx / cs[p].sx : vx;
        outLocalP[i][1] = (cs[p].sy != 0.f) ? vy / cs[p].sy : vy;
        outLocalP[i][2] = (cs[p].sz != 0.f) ? vz / cs[p].sz : vz;
    }
}

// Local -> component space, only for dirty bones (the bone or an ancestor changed), from parent to child.
inline void ToComponent(FTr* cs, const int* parent, int n, const Quat* localQ, const float (*localP)[3], const bool* dirty) {
    for (int i = 0; i < n; i++) {
        if (!dirty[i]) continue;
        const int p = parent[i];
        Quat q;
        float x = localP[i][0], y = localP[i][1], z = localP[i][2];
        if (p < 0) q = localQ[i];
        else {
            const Quat pq = TrQ(cs[p]);
            q = QMul(pq, localQ[i]);
            x *= cs[p].sx; y *= cs[p].sy; z *= cs[p].sz;
            QRotVec(pq, x, y, z);
            x += cs[p].px; y += cs[p].py; z += cs[p].pz;
        }
        q = QNorm(q);
        cs[i].qx = q.x; cs[i].qy = q.y; cs[i].qz = q.z; cs[i].qw = q.w;
        cs[i].px = x; cs[i].py = y; cs[i].pz = z;
    }
}
