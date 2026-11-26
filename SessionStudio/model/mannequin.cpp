// Session Studio -- the 3D mannequin geometry (PLAN-MANNEQUIN task 1), see mannequin.h
#include "mannequin.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr int kSegs = 22;   // around a tube (2026-11-26 second look: smoother, no visible facets)

struct V { float x = 0, y = 0, z = 0; };
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V a) { return std::sqrt(Dot(a, a)); }
bool Fin(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
bool Unit(V a, V& out) {
    const float l = Len(a);
    if (!(l > 1e-6f) || !std::isfinite(l)) return false;
    out = a * (1.f / l);
    return true;
}
V Rot(const Quat& q, V v) { QRotVec(q, v.x, v.y, v.z); return v; }
V Lerp(V a, V b, float t) { return a + (b - a) * t; }
// e1 = hint made perpendicular to axis d (fallbacks if parallel), e2 = d x e1
void Frame(V d, V hint, V& e1, V& e2) {
    V h = hint - d * Dot(hint, d);
    if (!Unit(h, e1)) {
        const V alt = std::fabs(d.z) < 0.9f ? V{0, 0, 1} : V{1, 0, 0};
        Unit(alt - d * Dot(alt, d), e1);
    }
    e2 = Cross(d, e1);
}

struct Builder {
    Mesh& m;
    int stripeBone = -2;   // the stripe goes on this bone only (-1: none, -2: every limb)
    uint32_t Vert(V p, V n, uint32_t c) {
        V u;
        if (!Unit(n, u)) u = {0, 0, 1};
        m.verts.push_back(MeshVertex{{p.x, p.y, p.z}, {u.x, u.y, u.z}, c});
        return (uint32_t)m.verts.size() - 1;
    }
    void Tri(uint32_t a, uint32_t b, uint32_t c, int bone) {
        m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
        m.triBone.push_back((int16_t)bone);
    }
    // a tube through sections: centre c[k], ellipse semi axes ra[k] along e1[k], rb[k] along e2[k]
    void Loft(const V* c, const V* e1, const V* e2, const float* ra, const float* rb, int n, const int* bone, uint32_t col, int segs = kSegs) {
        const uint32_t base = (uint32_t)m.verts.size();
        for (int k = 0; k < n; k++)
            for (int i = 0; i < segs; i++) {
                const float a = 2.f * kPi * (float)i / (float)segs, ca = std::cos(a), sa = std::sin(a);
                const V p = c[k] + e1[k] * (ca * ra[k]) + e2[k] * (sa * rb[k]);
                const V nn = e1[k] * (ca / std::max(ra[k], 1e-3f)) + e2[k] * (sa / std::max(rb[k], 1e-3f));
                Vert(p, nn, col);
            }
        for (int k = 0; k + 1 < n; k++)
            for (int i = 0; i < segs; i++) {
                const uint32_t a0 = base + k * segs + i, a1 = base + k * segs + (i + 1) % segs;
                const uint32_t b0 = a0 + segs, b1 = a1 + segs;
                Tri(a0, b0, a1, bone[k]);
                Tri(a1, b0, b1, bone[k]);
            }
    }
    // the stripe on the e1 side of a limb, a thin strip just outside it
    void Stripe(V a, V b, V e1a, V e2a, V e1b, V e2b, float ra, float rb, int bone) {
        const float wa = ra * 0.13f, wb = rb * 0.13f;   // a thin line
        const uint32_t p0 = Vert(a + e1a * (ra * 1.04f) - e2a * wa, e1a, kMannequinStripe);
        const uint32_t p1 = Vert(a + e1a * (ra * 1.04f) + e2a * wa, e1a, kMannequinStripe);
        const uint32_t p2 = Vert(b + e1b * (rb * 1.04f) - e2b * wb, e1b, kMannequinStripe);
        const uint32_t p3 = Vert(b + e1b * (rb * 1.04f) + e2b * wb, e1b, kMannequinStripe);
        Tri(p0, p2, p1, bone);
        Tri(p1, p2, p3, bone);
    }
    // tapered limb from a to b, its stripe on the side of `yAxis` (the bone's +Y)
    // bulge: a muscle swelling of `bulge` x the radius centred at `at` (0..1 along the limb); flat: depth / width
    void Limb(V a, V b, float ra, float rb, V yAxis, int bone, uint32_t col, float bulge = 0.f, float at = 0.4f, float flat = 1.f) {
        V d;
        if (!Unit(b - a, d)) return;
        V e1, e2;
        Frame(d, yAxis, e1, e2);
        constexpr int kR = 7;
        V c[kR], E1[kR], E2[kR];
        float r1[kR], r2[kR];
        int bn[kR];
        for (int k = 0; k < kR; k++) {
            const float t = (float)k / (kR - 1);
            const float g = (t - at) / 0.3f;
            const float r = (ra + (rb - ra) * t) * (1.f + bulge * std::exp(-g * g));
            c[k] = Lerp(a, b, t); E1[k] = e1; E2[k] = e2; r1[k] = r; r2[k] = r * flat; bn[k] = bone;
        }
        Loft(c, E1, E2, r1, r2, kR, bn, col);
        if (stripeBone == -2 || stripeBone == bone) Stripe(a, b, e1, e2, e1, e2, ra, rb, bone);
    }
    // a finger segment: a thin tube (few sides) with a round knuckle at its start; tip: a round end too
    void Phalanx(V a, V b, float ra, float rb, int bone, uint32_t col, bool tip) {
        V d;
        if (!Unit(b - a, d)) return;
        V e1, e2;
        Frame(d, V{0, 0, 1}, e1, e2);
        const V c[2] = {a, b}, E1[2] = {e1, e1}, E2[2] = {e2, e2};
        const float r[2] = {ra, rb};
        const int bn[2] = {bone, bone};
        Loft(c, E1, E2, r, r, 2, bn, col, 8);
        Ellipsoid(a, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, ra, ra, ra, bone, col, 4, 8);
        if (tip) Ellipsoid(b, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, rb, rb, rb, bone, col, 4, 8);
    }
    // ellipsoid on axes x, y, z (unit) with radii; squashBelow < 1 flattens the -z half (a sole)
    // flatBelow (-1..1): the part under that height (in radii) is cut flat (a sole), its normal straight down
    void Ellipsoid(V c, V ax, V ay, V az, float rx, float ry, float rz, int bone, uint32_t col, int lat = 12, int lon = 22, float squashBelow = 1.f, float flatBelow = -2.f) {
        const uint32_t base = (uint32_t)m.verts.size();
        for (int i = 0; i <= lat; i++) {
            const float th = kPi * (float)i / (float)lat, st = std::sin(th), ct = std::cos(th);
            for (int j = 0; j < lon; j++) {
                const float ph = 2.f * kPi * (float)j / (float)lon;
                const float x = st * std::cos(ph), y = st * std::sin(ph);
                float z = ct;
                const float kz = z < 0.f ? squashBelow : 1.f;
                const bool flat = z < flatBelow;
                if (flat) z = flatBelow;
                const V p = c + ax * (x * rx) + ay * (y * ry) + az * (z * rz * kz);
                const V nn = flat ? az * -1.f : ax * (x / rx) + ay * (y / ry) + az * (z / (rz * kz));
                Vert(p, nn, col);
            }
        }
        for (int i = 0; i < lat; i++)
            for (int j = 0; j < lon; j++) {
                const uint32_t a0 = base + i * lon + j, a1 = base + i * lon + (j + 1) % lon;
                const uint32_t b0 = a0 + lon, b1 = a1 + lon;
                if (i > 0) Tri(a0, b0, a1, bone);
                if (i + 1 < lat) Tri(a1, b0, b1, bone);
            }
    }
    void Sphere(V c, float r, int bone, uint32_t col) { Ellipsoid(c, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, r, r, r, bone, col, 9, 18); }
};

int Find(const SkeletonFile& s, const char* n) {
    for (size_t i = 0; i < s.names.size(); i++) if (s.names[i] == n) return (int)i;
    return -1;
}
// a usable bone of the pose: in range, finite position and rotation
bool Ok(const std::vector<FTr>& cs, int b) {
    if (b < 0 || b >= (int)cs.size()) return false;
    const FTr& t = cs[b];
    return std::isfinite(t.px) && std::isfinite(t.py) && std::isfinite(t.pz) && std::isfinite(t.qx) && std::isfinite(t.qy) &&
           std::isfinite(t.qz) && std::isfinite(t.qw) && std::fabs(t.px) < 1e5f && std::fabs(t.py) < 1e5f && std::fabs(t.pz) < 1e5f;
}
V P(const std::vector<FTr>& cs, int b) { return {cs[b].px, cs[b].py, cs[b].pz}; }
Quat Q(const std::vector<FTr>& cs, int b) { return QNorm(TrQ(cs[b])); }
V BoneY(const std::vector<FTr>& cs, int b) { return Rot(Q(cs, b), V{0, 1, 0}); }

// unit axes with front = sign * (up x side)
bool Axes(const MannequinRig& r, const std::vector<FTr>& cs, float sign, V& side, V& up, V& front) {
    if (!Ok(cs, r.pelvis)) return false;
    const int top = Ok(cs, r.neck) ? r.neck : Ok(cs, r.spine3) ? r.spine3 : Ok(cs, r.spine2) ? r.spine2 : -1;
    if (top < 0 || !Unit(P(cs, top) - P(cs, r.pelvis), up)) return false;
    V sv{0, 0, 0};
    bool have = false;
    if (Ok(cs, r.upper[0]) && Ok(cs, r.upper[1])) { sv = P(cs, r.upper[0]) - P(cs, r.upper[1]); have = Len(sv) > 1e-3f; }
    if (!have && Ok(cs, r.thigh[0]) && Ok(cs, r.thigh[1])) { sv = P(cs, r.thigh[0]) - P(cs, r.thigh[1]); have = Len(sv) > 1e-3f; }
    if (!have && Ok(cs, r.clav[0]) && Ok(cs, r.clav[1])) { sv = P(cs, r.clav[0]) - P(cs, r.clav[1]); have = Len(sv) > 1e-3f; }
    if (!have || !Unit(sv - up * Dot(sv, up), side)) return false;
    front = Cross(up, side) * sign;
    return true;
}
} // namespace

MannequinRig BuildMannequinRig(const SkeletonFile& s, const std::vector<FTr>& cal) {
    MannequinRig r;
    r.pelvis = Find(s, "AMXX_Pelvis");
    r.spine1 = Find(s, "AMXX_Spine1"); r.spine2 = Find(s, "AMXX_Spine2"); r.spine3 = Find(s, "AMXX_Spine3");
    r.neck = Find(s, "AMXX_Neck"); r.head = Find(s, "AMXX_Head");
    const char* sd[2] = {"L", "R"};
    for (int k = 0; k < 2; k++) {
        auto f = [&](const char* part) { return Find(s, (std::string("AMXX_") + sd[k] + "_" + part).c_str()); };
        r.clav[k] = f("Clavicle"); r.upper[k] = f("UpperArm"); r.fore[k] = f("Forearm"); r.hand[k] = f("Hand");
        r.thigh[k] = f("Thigh"); r.calf[k] = f("Calf"); r.foot[k] = f("Foot"); r.toe[k] = f("Toe0");
        r.thumb[k] = f("Finger0"); r.thumbTip[k] = f("Finger02");
        if (r.thumbTip[k] < 0) r.thumbTip[k] = f("Finger01");
        r.index[k] = f("Finger1"); r.middle[k] = f("Finger2"); r.pinky[k] = f("Finger4");
        for (int fi = 0; fi < 5; fi++) {
            const std::string base = "Finger" + std::to_string(fi);
            r.finger[k][fi][0] = f(base.c_str());
            r.finger[k][fi][1] = f((base + "1").c_str());
            r.finger[k][fi][2] = f((base + "2").c_str());
        }
    }
    r.ok = r.pelvis >= 0 && r.head >= 0 && (r.neck >= 0 || r.spine3 >= 0 || r.spine2 >= 0);
    if (!r.ok) return r;
    V side, up, front;
    if (!Axes(r, cal, 1.f, side, up, front)) return r;
    // toes point to the front: the sign of up x side is fixed on the calibration pose
    V toes{0, 0, 0};
    for (int k = 0; k < 2; k++)
        if (Ok(cal, r.toe[k]) && Ok(cal, r.foot[k])) toes = toes + (P(cal, r.toe[k]) - P(cal, r.foot[k]));
    toes = toes - up * Dot(toes, up);
    if (Len(toes) > 1e-3f) r.frontSign = Dot(toes, front) >= 0.f ? 1.f : -1.f;
    front = front * r.frontSign;
    // the sole: on the calibration pose the feet stand on the ground, so the sole is horizontal there
    for (int k = 0; k < 2; k++) {
        if (!Ok(cal, r.foot[k]) || !Ok(cal, r.toe[k])) continue;
        const V d = P(cal, r.toe[k]) - P(cal, r.foot[k]);
        V fw;
        if (!Unit(d - up * Dot(d, up), fw)) continue;
        const Quat inv = QInv(Q(cal, r.foot[k]));
        const V lf = Rot(inv, fw), lu = Rot(inv, up);
        r.shoeFwd[k][0] = lf.x; r.shoeFwd[k][1] = lf.y; r.shoeFwd[k][2] = lf.z;
        r.shoeUp[k][0] = lu.x; r.shoeUp[k][1] = lu.y; r.shoeUp[k][2] = lu.z;
        r.ankleH[k] = std::clamp(-Dot(d, up), 5.f, 16.f);
        r.footLen[k] = std::clamp(Dot(d, fw), 8.f, 22.f);
        r.shoeOk[k] = true;
    }
    if (Ok(cal, r.head)) {
        const Quat inv = QInv(Q(cal, r.head));
        const V hf = Rot(inv, front), hu = Rot(inv, up);
        r.headFront[0] = hf.x; r.headFront[1] = hf.y; r.headFront[2] = hf.z;
        r.headUp[0] = hu.x; r.headUp[1] = hu.y; r.headUp[2] = hu.z;
    }
    return r;
}

bool TorsoAxes(const MannequinRig& r, const std::vector<FTr>& cs, float side[3], float up[3], float front[3]) {
    V s, u, f;
    if (!r.ok || !Axes(r, cs, r.frontSign, s, u, f)) return false;
    side[0] = s.x; side[1] = s.y; side[2] = s.z;
    up[0] = u.x; up[1] = u.y; up[2] = u.z;
    front[0] = f.x; front[1] = f.y; front[2] = f.z;
    return true;
}


// 2026-11-26 second look (user: the first one was ugly): one clay colour, smooth shapes with some muscle, joints
// melted into the limbs, a plain head with a direction marker (no face), the stripe only where the caller wants it.
void BuildMannequinMesh(const MannequinRig& r, const std::vector<FTr>& cs, const std::vector<uint32_t>& boneRgba, Mesh& out, int stripeBone) {
    out.Clear();
    if (!r.ok) return;
    V side, up, front;
    if (!Axes(r, cs, r.frontSign, side, up, front)) return;
    Builder B{out, stripeBone};
    const int nb = (int)cs.size();
    auto colOf = [&](int b) { return (b >= 0 && b < (int)boneRgba.size()) ? boneRgba[b] : 0xFFC8C8C8u; };
    auto ok = [&](int b) { return b >= 0 && b < nb && Ok(cs, b); };
    auto dist = [&](int a, int b) { return Len(P(cs, a) - P(cs, b)); };

    // ---- torso: one smooth tube pelvis -> waist -> chest -> shoulders -> neck base, elliptical sections
    float hip = 13.f, sh = 14.f;
    if (ok(r.thigh[0]) && ok(r.thigh[1])) hip = std::clamp(dist(r.thigh[0], r.thigh[1]) * 0.5f + 5.f, 9.f, 20.f);
    if (ok(r.upper[0]) && ok(r.upper[1])) sh = std::clamp(dist(r.upper[0], r.upper[1]) * 0.5f * 0.8f, 9.f, 22.f);
    V hipSide = side, shSide = side;
    if (ok(r.thigh[0]) && ok(r.thigh[1])) { V t = P(cs, r.thigh[0]) - P(cs, r.thigh[1]); Unit(t - up * Dot(t, up), hipSide); }
    if (ok(r.upper[0]) && ok(r.upper[1])) { V t = P(cs, r.upper[0]) - P(cs, r.upper[1]); Unit(t - up * Dot(t, up), shSide); }
    {
        const int chain[5] = {r.pelvis, r.spine1, r.spine2, r.spine3, r.neck};
        const float wv[5] = {hip, hip * 0.8f, sh * 0.9f, sh * 0.96f, sh * 0.5f};   // hips, waist, chest, shoulders, base of the neck (wide: trapezius)
        const float dv[5] = {0.7f, 0.72f, 0.8f, 0.64f, 0.9f};                      // depth / width
        // 2026-11-26 (user: torso): the spine runs along the BACK of a body, so the volume sits in front of it
        // (chest most, belly less, hips a little); seen from the side this gives a chest and a flat back
        const float fwd[5] = {0.5f, 2.2f, 3.6f, 1.6f, 0.8f};
        V pc[5]; int pb[5]; float pw[5], pd[5], pt[5];
        int n = 0;
        for (int k = 0; k < 5; k++) {
            if (!ok(chain[k])) continue;
            pc[n] = P(cs, chain[k]) + (k == 0 ? up * 3.f : V{}) + front * fwd[k];
            pb[n] = chain[k]; pw[n] = wv[k]; pd[n] = wv[k] * dv[k]; pt[n] = (float)k / 4.f;
            n++;
        }
        if (n >= 2) {
            // a middle section between each pair: a smoother outline
            V c[9], e1[9], e2[9]; float w[9], dp[9], tt[9]; int bones[9];
            int m = 0;
            for (int k = 0; k < n && m < 9; k++) {
                c[m] = pc[k]; w[m] = pw[k]; dp[m] = pd[k]; tt[m] = pt[k]; bones[m] = pb[k]; m++;
                if (k + 1 < n && m < 9) {
                    c[m] = Lerp(pc[k], pc[k + 1], 0.5f); w[m] = (pw[k] + pw[k + 1]) * 0.5f; dp[m] = (pd[k] + pd[k + 1]) * 0.5f;
                    tt[m] = (pt[k] + pt[k + 1]) * 0.5f; bones[m] = pb[k]; m++;
                }
            }
            for (int k = 0; k < m; k++) {
                V d, sdir;
                if (!Unit(c[k + 1 < m ? k + 1 : m - 1] - c[k > 0 ? k - 1 : 0], d)) d = up;
                if (!Unit(Lerp(hipSide, shSide, tt[k]), sdir)) sdir = side;
                Frame(d, sdir, e1[k], e2[k]);
            }
            B.Loft(c, e1, e2, w, dp, m, bones, colOf(r.spine2 >= 0 ? r.spine2 : r.pelvis));
            const size_t first = out.verts.size() - (size_t)m * kSegs;   // each section takes its bone's colour
            for (int k = 0; k < m; k++) for (int i = 0; i < kSegs; i++) out.verts[first + k * kSegs + i].rgba = colOf(bones[k]);
        }
        if (ok(r.pelvis)) {   // hips
            V ctr = P(cs, r.pelvis);
            if (ok(r.thigh[0]) && ok(r.thigh[1])) ctr = (P(cs, r.thigh[0]) + P(cs, r.thigh[1])) * 0.5f + up * 3.f;
            V f1, f2;
            Frame(up, hipSide, f1, f2);
            B.Ellipsoid(ctr + front * 0.5f, f1, f2, up, hip * 0.98f, hip * 0.7f, 9.f, r.pelvis, colOf(r.pelvis));
        }
        if (ok(r.neck)) {   // closes the top of the torso around the base of the neck (no shoulder pads)
            V f1, f2;
            Frame(up, shSide, f1, f2);
            B.Ellipsoid(P(cs, r.neck) + front * 0.8f, f1, f2, up, sh * 0.5f, sh * 0.45f, 3.f, r.neck, colOf(r.neck));
        }
    }

    // ---- neck and head: a plain head (skull, jaw, nose, ears) so the facing reads without looking odd
    // 2026-11-26 third look (user: giraffe neck, no face): a thick short neck, the head sitting low on it, no face, only a
    // blue marker on the forehead showing where the head looks
    // fourth look (user: neck still too long and too thick): thinner, and the head lower so less of it shows
    if (ok(r.neck) && ok(r.head)) B.Limb(P(cs, r.neck), P(cs, r.head), 4.9f, 4.4f, BoneY(cs, r.neck), r.neck, colOf(r.neck), 0.f, 0.5f, 0.95f);
    if (ok(r.head)) {
        const Quat qh = Q(cs, r.head);
        V hu = Rot(qh, V{r.headUp[0], r.headUp[1], r.headUp[2]}), hf = Rot(qh, V{r.headFront[0], r.headFront[1], r.headFront[2]});
        V u2, f2, s2;
        if (!Unit(hu, u2)) u2 = up;
        if (!Unit(hf - u2 * Dot(hf, u2), f2)) f2 = front;
        s2 = Cross(u2, f2);
        const uint32_t hc = colOf(r.head);
        const V skull = P(cs, r.head) + u2 * 4.5f + f2 * 1.0f;   // its bottom covers the upper half of the neck
        B.Ellipsoid(skull, s2, f2, u2, 8.6f, 9.6f, 11.0f, r.head, hc);
        B.Ellipsoid(skull + f2 * 8.9f + u2 * 1.5f, s2, f2, u2, 3.4f, 1.9f, 1.4f, r.head, kMannequinLook, 6, 12);   // direction marker
    }

    // ---- arms
    for (int k = 0; k < 2; k++) {
        const int cl = r.clav[k], ua = r.upper[k], fa = r.fore[k], ha = r.hand[k];
        if (ok(cl) && ok(ua)) B.Limb(P(cs, cl), P(cs, ua), 5.6f, 5.8f, BoneY(cs, cl), cl, colOf(cl), 0.f, 0.5f, 0.85f);
        if (ok(ua)) B.Sphere(P(cs, ua), 5.6f, ua, colOf(ua));
        if (ok(ua) && ok(fa)) B.Limb(P(cs, ua), P(cs, fa), 5.3f, 4.0f, BoneY(cs, ua), ua, colOf(ua), 0.12f, 0.45f, 0.9f);
        if (ok(fa)) B.Sphere(P(cs, fa), 4.0f, fa, colOf(fa));
        if (ok(fa) && ok(ha)) B.Limb(P(cs, fa), P(cs, ha), 4.2f, 2.9f, BoneY(cs, fa), fa, colOf(fa), 0.15f, 0.25f, 0.82f);
        if (!ok(ha)) continue;
        B.Sphere(P(cs, ha), 2.9f, ha, colOf(ha));
        // 2026-11-26 (user): a palm and real fingers (three segments each, round knuckles and tips)
        V knuck{0, 0, 0};
        int nk = 0;
        for (int fi = 1; fi < 5; fi++) if (ok(r.finger[k][fi][0])) { knuck = knuck + P(cs, r.finger[k][fi][0]); nk++; }
        V dir;
        if (nk > 0 && Unit(knuck * (1.f / nk) - P(cs, ha), dir)) {
            const float L = std::clamp(Len(knuck * (1.f / nk) - P(cs, ha)), 4.f, 14.f);
            V wv = BoneY(cs, ha);
            float half = 3.6f;
            if (ok(r.index[k]) && ok(r.pinky[k])) { wv = P(cs, r.index[k]) - P(cs, r.pinky[k]); half = std::clamp(Len(wv) * 0.5f + 1.1f, 2.5f, 5.f); }
            V e1, e2;
            Frame(dir, wv, e1, e2);
            B.Ellipsoid(P(cs, ha) + dir * (L * 0.55f), e1, e2, dir, half, 1.5f, L * 0.55f, ha, colOf(ha));   // palm
        }
        for (int fi = 0; fi < 5; fi++) {
            const int* j = r.finger[k][fi];
            if (!ok(j[0]) || !ok(j[1])) continue;
            const float s = fi == 0 ? 1.25f : fi == 4 ? 0.85f : 1.f;   // thumb thicker, little finger thinner
            V pts[4] = {P(cs, j[0]), P(cs, j[1]), V{}, V{}};
            int np = 2;
            if (ok(j[2])) { pts[2] = P(cs, j[2]); np = 3; }
            pts[np] = pts[np - 1] + (pts[np - 1] - pts[np - 2]) * 0.85f;   // the tip: no bone, about the last segment's length
            np++;
            const float rad[4] = {1.0f * s, 0.88f * s, 0.78f * s, 0.66f * s};
            for (int q = 0; q + 1 < np; q++) B.Phalanx(pts[q], pts[q + 1], rad[q], rad[q + 1], j[std::min(q, 2)], colOf(j[std::min(q, 2)]), q + 2 == np);
        }
    }

    // ---- legs
    for (int k = 0; k < 2; k++) {
        const int th = r.thigh[k], ca = r.calf[k], fo = r.foot[k], to = r.toe[k];
        if (ok(th)) B.Sphere(P(cs, th), 7.2f, th, colOf(th));
        if (ok(th) && ok(ca)) B.Limb(P(cs, th), P(cs, ca), 7.6f, 5.2f, BoneY(cs, th), th, colOf(th), 0.08f, 0.35f, 0.92f);
        if (ok(ca)) B.Sphere(P(cs, ca), 5.2f, ca, colOf(ca));
        if (ok(ca) && ok(fo)) B.Limb(P(cs, ca), P(cs, fo), 5.0f, 3.6f, BoneY(cs, ca), ca, colOf(ca), 0.18f, 0.28f, 0.9f);
        if (!ok(fo)) continue;
        B.Sphere(P(cs, fo), 3.6f, fo, colOf(fo));
        // shoe (2026-11-26, user: feet not flat): it follows the foot bone with the sole calibrated on the standing
        // pose: heel under the ankle, flat sole ankleH below it, toe cap past the toe bone
        V f;
        if (r.shoeOk[k]) {
            const Quat qf = Q(cs, fo);
            V su, sf;
            if (Unit(Rot(qf, V{r.shoeUp[k][0], r.shoeUp[k][1], r.shoeUp[k][2]}), su)) {
                const V f0 = Rot(qf, V{r.shoeFwd[k][0], r.shoeFwd[k][1], r.shoeFwd[k][2]});
                if (Unit(f0 - su * Dot(f0, su), sf)) {
                    const V across = Cross(su, sf);
                    // sixth look (user: feet too big, the earlier ones were fine): the earlier slim size (4.6 wide,
                    // 4 high), kept flat: sole ankleH under the ankle
                    const float H = r.ankleH[k], half = std::clamp((r.footLen[k] + 8.f) * 0.5f, 7.f, 15.f), rz = 4.0f;
                    const V ctr = P(cs, fo) + sf * (r.footLen[k] * 0.5f + 0.5f) - su * (H - 0.8f * rz);
                    B.Ellipsoid(ctr, across, sf, su, 4.6f, half, rz, fo, colOf(fo), 10, 22, 1.f, -0.8f);
                }
            }
        } else if (ok(to) && Unit(P(cs, to) - P(cs, fo), f)) {   // no calibration: the old shoe along ankle -> toe
            const float L = std::clamp(dist(to, fo), 6.f, 30.f);
            V legUp = up;
            if (ok(ca)) Unit(P(cs, ca) - P(cs, fo), legUp);
            V e1, e2;
            Frame(f, legUp, e1, e2);   // e1: up the leg, e2: across the foot
            const V ctr = P(cs, fo) + f * (L * 0.55f) - e1 * 1.5f;
            B.Ellipsoid(ctr, e2, f, e1, 4.6f, L * 0.78f, 4.0f, fo, colOf(fo), 10, 20, 0.45f);
        }
    }
}

float GroundHeight(const MannequinRig& r, const std::vector<std::vector<FTr>>& poses) {
    float g = 0.f;
    Mesh m;
    for (const std::vector<FTr>& cs : poses) {
        BuildMannequinMesh(r, cs, std::vector<uint32_t>(cs.size(), 0xFFC0C0C0u), m, -1);
        for (const MeshVertex& v : m.verts) if (std::isfinite(v.pos[2])) g = std::min(g, v.pos[2]);
    }
    return g;
}
