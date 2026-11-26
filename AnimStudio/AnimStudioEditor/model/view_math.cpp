#include "view_math.h"
#include <cmath>

namespace {
constexpr float kDeg = 3.14159265358979f / 180.f;
void Mul(const float a[16], const float b[16], float o[16]) {   // o = a * b (colonne-major)
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0.f;
            for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            o[c * 4 + r] = s;
        }
}
void Norm3(float v[3]) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-12f) { v[0] /= n; v[1] /= n; v[2] /= n; }
}
void Cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
} // namespace

void OrbitCamera::Eye(float out[3]) const {
    const float cp = std::cos(pitchDeg * kDeg), sp = std::sin(pitchDeg * kDeg);
    out[0] = target[0] + dist * cp * std::cos(yawDeg * kDeg);
    out[1] = target[1] + dist * cp * std::sin(yawDeg * kDeg);
    out[2] = target[2] + dist * sp;
}

void OrbitCamera::View(float m[16]) const {   // right-handed lookAt, Z up
    float eye[3];
    Eye(eye);
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    Norm3(f);
    const float up[3] = {0.f, 0.f, 1.f};
    float s[3];
    Cross(f, up, s);
    Norm3(s);
    float u[3];
    Cross(s, f, u);
    const float v[16] = {s[0], u[0], -f[0], 0.f,
                         s[1], u[1], -f[1], 0.f,
                         s[2], u[2], -f[2], 0.f,
                         -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]),
                         -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
                         (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]), 1.f};
    for (int i = 0; i < 16; i++) m[i] = v[i];
}

void OrbitCamera::Proj(float aspect, float m[16]) const {   // perspective OpenGL (profondeur -1..1)
    const float n = 1.f, fz = 5000.f;
    const float t = 1.f / std::tan(fovDeg * kDeg * 0.5f);
    for (int i = 0; i < 16; i++) m[i] = 0.f;
    m[0] = t / (aspect > 1e-6f ? aspect : 1.f);
    m[5] = t;
    m[10] = -(fz + n) / (fz - n);
    m[11] = -1.f;
    m[14] = -(2.f * fz * n) / (fz - n);
}

bool Project(const OrbitCamera& c, float aspect, const float p[3], float vpW, float vpH, float& sx, float& sy) {
    float v[16], pr[16], vp[16];
    c.View(v);
    c.Proj(aspect, pr);
    Mul(pr, v, vp);
    const float x = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
    const float y = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
    const float w = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
    if (w <= 1e-4f) return false;
    sx = (x / w * 0.5f + 0.5f) * vpW;
    sy = (1.f - (y / w * 0.5f + 0.5f)) * vpH;
    return true;
}

int PickBone(const OrbitCamera& c, float aspect, const std::vector<FTr>& cs, const std::vector<bool>& selectable,
             float vpW, float vpH, float mx, float my, float radiusPx) {
    int best = -1;
    float bestD = radiusPx * radiusPx;
    for (size_t i = 0; i < cs.size(); i++) {
        if (i >= selectable.size() || !selectable[i]) continue;
        const float p[3] = {cs[i].px, cs[i].py, cs[i].pz};
        float sx, sy;
        if (!Project(c, aspect, p, vpW, vpH, sx, sy)) continue;
        const float d = (sx - mx) * (sx - mx) + (sy - my) * (sy - my);
        if (d <= bestD) { bestD = d; best = (int)i; }
    }
    return best;
}

void BoneMatrix(const FTr& t, float m[16]) {
    const Quat q = QNorm(TrQ(t));
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w);     m[2] = 2 * (x * z - y * w);     m[3] = 0;
    m[4] = 2 * (x * y - z * w);     m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z + x * w);     m[7] = 0;
    m[8] = 2 * (x * z + y * w);     m[9] = 2 * (y * z - x * w);     m[10] = 1 - 2 * (x * x + y * y); m[11] = 0;
    m[12] = t.px; m[13] = t.py; m[14] = t.pz; m[15] = 1;
}

bool MatrixRotation(const float m[16], Quat& q) {
    float X[3] = {m[0], m[1], m[2]}, Y[3] = {m[4], m[5], m[6]};
    Norm3(X);
    const float d = X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2];
    Y[0] -= d * X[0]; Y[1] -= d * X[1]; Y[2] -= d * X[2];
    Norm3(Y);
    float Z[3];
    Cross(X, Y, Z);
    const float lx = X[0] * X[0] + X[1] * X[1] + X[2] * X[2];
    if (!(lx > 0.5f)) return false;
    // matrice -> quaternion (colonnes X, Y, Z)
    const float tr = X[0] + Y[1] + Z[2];
    Quat r;
    if (tr > 0.f) {
        const float s = std::sqrt(tr + 1.f) * 2.f;
        r = Quat{(Y[2] - Z[1]) / s, (Z[0] - X[2]) / s, (X[1] - Y[0]) / s, 0.25f * s};
    } else if (X[0] > Y[1] && X[0] > Z[2]) {
        const float s = std::sqrt(1.f + X[0] - Y[1] - Z[2]) * 2.f;
        r = Quat{0.25f * s, (Y[0] + X[1]) / s, (Z[0] + X[2]) / s, (Y[2] - Z[1]) / s};
    } else if (Y[1] > Z[2]) {
        const float s = std::sqrt(1.f + Y[1] - X[0] - Z[2]) * 2.f;
        r = Quat{(Y[0] + X[1]) / s, 0.25f * s, (Z[1] + Y[2]) / s, (Z[0] - X[2]) / s};
    } else {
        const float s = std::sqrt(1.f + Z[2] - X[0] - Y[1]) * 2.f;
        r = Quat{(Z[0] + X[2]) / s, (Z[1] + Y[2]) / s, 0.25f * s, (X[1] - Y[0]) / s};
    }
    q = QNorm(r);
    return true;
}

void QToRotatorDeg(Quat q, float& pitch, float& yaw, float& roll) {   // FQuat::Rotator d'UE4
    q = QNorm(q);
    const float R2D = 180.f / 3.14159265358979f;
    const float sing = q.z * q.x - q.w * q.y;
    const float yawY = 2.f * (q.w * q.z + q.x * q.y);
    const float yawX = 1.f - 2.f * (q.y * q.y + q.z * q.z);
    const float kT = 0.4999995f;
    auto norm = [](float a) { a = std::fmod(a + 180.f, 360.f); if (a < 0.f) a += 360.f; return a - 180.f; };
    if (sing < -kT) {
        pitch = -90.f;
        yaw = std::atan2(yawY, yawX) * R2D;
        roll = norm(-yaw - 2.f * std::atan2(q.x, q.w) * R2D);
    } else if (sing > kT) {
        pitch = 90.f;
        yaw = std::atan2(yawY, yawX) * R2D;
        roll = norm(yaw - 2.f * std::atan2(q.x, q.w) * R2D);
    } else {
        pitch = std::asin(2.f * sing) * R2D;
        yaw = std::atan2(yawY, yawX) * R2D;
        roll = std::atan2(-2.f * (q.w * q.x + q.y * q.z), 1.f - 2.f * (q.x * q.x + q.y * q.y)) * R2D;
    }
}

void StageViewProj(const OrbitCamera& c, float aspect, float out[16]) {
    float v[16], pr[16], vp[16];
    c.View(v);
    c.Proj(aspect, pr);
    Mul(pr, v, vp);
    // point (x, y, z) taken as (x, -y, z): negate the second column
    for (int r = 0; r < 4; r++) vp[4 + r] = -vp[4 + r];
    // D3D depth: z' = (z + w) / 2
    for (int col = 0; col < 4; col++) vp[col * 4 + 2] = 0.5f * (vp[col * 4 + 2] + vp[col * 4 + 3]);
    for (int i = 0; i < 16; i++) out[i] = vp[i];
}
