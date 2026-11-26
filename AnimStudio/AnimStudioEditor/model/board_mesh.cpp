// AnimStudio Editor -- the skateboard of the 3D view (PLAN-MANNEQUIN stage 2), see board_mesh.h
#include "board_mesh.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.14159265358979f;
struct V { float x = 0, y = 0, z = 0; };
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V a) { return std::sqrt(Dot(a, a)); }
bool Unit(V a, V& out) {
    const float l = Len(a);
    if (!(l > 1e-6f) || !std::isfinite(l)) return false;
    out = a * (1.f / l);
    return true;
}
V Rot(const Quat& q, V v) { QRotVec(q, v.x, v.y, v.z); return v; }

struct Out {
    Mesh& m;
    int bone;
    uint32_t Vert(V p, V n, uint32_t c) {
        V u;
        if (!Unit(n, u)) u = {0, 0, 1};
        m.verts.push_back(MeshVertex{{p.x, p.y, p.z}, {u.x, u.y, u.z}, c});
        return (uint32_t)m.verts.size() - 1;
    }
    void Tri(uint32_t a, uint32_t b, uint32_t c) {
        m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
        m.triBone.push_back((int16_t)bone);
    }
    void Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { Tri(a, b, c); Tri(a, c, d); }
    void Ellipsoid(V c, V ax, V ay, V az, float rx, float ry, float rz, uint32_t col, int lat = 8, int lon = 16) {
        const uint32_t base = (uint32_t)m.verts.size();
        for (int i = 0; i <= lat; i++) {
            const float th = kPi * (float)i / (float)lat, st = std::sin(th), ct = std::cos(th);
            for (int j = 0; j < lon; j++) {
                const float ph = 2.f * kPi * (float)j / (float)lon, x = st * std::cos(ph), y = st * std::sin(ph);
                Vert(c + ax * (x * rx) + ay * (y * ry) + az * (ct * rz), ax * (x / rx) + ay * (y / ry) + az * (ct / rz), col);
            }
        }
        for (int i = 0; i < lat; i++)
            for (int j = 0; j < lon; j++) {
                const uint32_t a0 = base + i * lon + j, a1 = base + i * lon + (j + 1) % lon, b0 = a0 + lon, b1 = a1 + lon;
                if (i > 0) Tri(a0, b0, a1);
                if (i + 1 < lat) Tri(a1, b0, b1);
            }
    }
    void Tube(V a, V b, float r, uint32_t col, int segs = 10) {
        V d;
        if (!Unit(b - a, d)) return;
        V e1;
        const V h = std::fabs(d.z) < 0.9f ? V{0, 0, 1} : V{1, 0, 0};
        if (!Unit(h - d * Dot(h, d), e1)) return;
        const V e2 = Cross(d, e1);
        const uint32_t base = (uint32_t)m.verts.size();
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < segs; i++) {
                const float t = 2.f * kPi * (float)i / (float)segs;
                const V n = e1 * std::cos(t) + e2 * std::sin(t);
                Vert((k ? b : a) + n * r, n, col);
            }
        for (int i = 0; i < segs; i++) {
            const uint32_t a0 = base + i, a1 = base + (i + 1) % segs;
            Quad(a0, a0 + segs, a1 + segs, a1);
        }
    }
};

bool Fin(const FTr& t) {
    return std::isfinite(t.px) && std::isfinite(t.py) && std::isfinite(t.pz) && std::isfinite(t.qx) && std::isfinite(t.qy) &&
           std::isfinite(t.qz) && std::isfinite(t.qw) && std::fabs(t.px) < 1e5f && std::fabs(t.py) < 1e5f && std::fabs(t.pz) < 1e5f;
}
float Kick(float x) {   // height of the deck over the flat part: nose and tail rise past the trucks
    const float e = std::max(0.f, std::fabs(x) - 22.f);
    return 0.36f * (e < 6.f ? e * e / 12.f : e - 3.f);
}
float KickSlope(float x) {
    const float e = std::max(0.f, std::fabs(x) - 22.f);
    const float s = 0.36f * (e < 6.f ? e / 6.f : 1.f);
    return x < 0.f ? -s : s;
}
float HalfWidth(float x) {
    const float ax = std::fabs(x), flat = kDeckHalfLength - kDeckHalfWidth;
    if (ax <= flat) return kDeckHalfWidth;
    const float d = ax - flat;
    return std::sqrt(std::max(0.f, kDeckHalfWidth * kDeckHalfWidth - d * d));
}
float HalfWidthSlope(float x) {   // d(HalfWidth)/dx
    const float ax = std::fabs(x), flat = kDeckHalfLength - kDeckHalfWidth;
    if (ax <= flat) return 0.f;
    const float d = ax - flat, w = std::max(0.3f, HalfWidth(x));
    return (x < 0.f ? 1.f : -1.f) * d / w;
}
} // namespace

BoardRig BuildBoardRig(const SkeletonFile& s) {
    BoardRig r;
    auto f = [&](const char* n) {
        const std::string full = std::string("SKXX_SkateSkel_") + n;
        for (size_t i = 0; i < s.names.size(); i++) if (s.names[i] == full) return (int)i;
        return -1;
    };
    r.root = f("Root"); r.deck = f("Flipper");
    r.truck[0] = f("Truck_Front"); r.truck[1] = f("Truck_Back");
    r.wheel[0] = f("Wheel_FL"); r.wheel[1] = f("Wheel_FR"); r.wheel[2] = f("Wheel_BL"); r.wheel[3] = f("Wheel_BR");
    r.ok = r.deck >= 0;
    return r;
}

bool BuildBoardMesh(const BoardRig& r, const std::vector<FTr>& cs, bool positionsRecorded, Mesh& out) {
    out.Clear();
    const int n = (int)cs.size();
    if (!r.ok || r.deck >= n || !Fin(cs[r.deck])) return false;
    const Quat q = QNorm(TrQ(cs[r.deck]));
    V pos{cs[r.deck].px, cs[r.deck].py, cs[r.deck].pz};
    const V ax = Rot(q, V{1, 0, 0}), ay = Rot(q, V{0, 1, 0}), az = Rot(q, V{0, 0, 1});
    auto L2W = [&](float x, float y, float z) { return pos + ax * x + ay * y + az * z; };
    // the rigid layout of the wheels and trucks in deck space (riding figures, see board_mesh.h)
    const V wheelLocal[4] = {{23.4f, -8.2f, -2.7f}, {23.4f, 8.2f, -2.7f}, {-23.4f, -8.2f, -2.7f}, {-23.4f, 8.2f, -2.7f}};
    const V truckLocal[2] = {{21.3f, 0.f, -1.0f}, {-21.3f, 0.f, -1.0f}};
    if (!positionsRecorded) {   // old figure: set the board on its lowest wheel
        float lowest = 1e9f;
        for (const V& w : wheelLocal) lowest = std::min(lowest, (pos + ax * w.x + ay * w.y + az * w.z).z);
        pos.z += kWheelRadius - lowest;
    }
    auto boneOr = [&](int b, V rigid) {   // the recorded bone position when it is plausible, else the rigid one
        const V rw = pos + ax * rigid.x + ay * rigid.y + az * rigid.z;
        if (!positionsRecorded || b < 0 || b >= n || !Fin(cs[b])) return rw;
        const V p{cs[b].px, cs[b].py, cs[b].pz};
        return Len(p - rw) < 6.f ? p : rw;
    };
    V wheel[4], truck[2];
    for (int k = 0; k < 4; k++) wheel[k] = boneOr(r.wheel[k], wheelLocal[k]);
    for (int k = 0; k < 2; k++) truck[k] = boneOr(r.truck[k], truckLocal[k]);
    Out o{out, r.deck};

    // ---- deck: top (grip), bottom, edges; nose (+X) and tail kicks
    {
        constexpr int N = 64;
        uint32_t tl[N + 1], tr[N + 1], bl[N + 1], br[N + 1], sl0[N + 1], sl1[N + 1], sr0[N + 1], sr1[N + 1];
        for (int i = 0; i <= N; i++) {
            const float x = -kDeckHalfLength + 2.f * kDeckHalfLength * (float)i / N;
            const float w = HalfWidth(x), k = Kick(x), ks = KickSlope(x), ws = HalfWidthSlope(x);
            const float zt = kDeckTop + k, zb = kDeckTop - kDeckThick + k;
            const V nTop = ax * (-ks) + az, nBot = (ax * (-ks) + az) * -1.f;
            tl[i] = o.Vert(L2W(x, -w, zt), nTop, kBoardGrip);
            tr[i] = o.Vert(L2W(x, w, zt), nTop, kBoardGrip);
            bl[i] = o.Vert(L2W(x, -w, zb), nBot, kBoardUnder);
            br[i] = o.Vert(L2W(x, w, zb), nBot, kBoardUnder);
            const V nl = ax * ws + ay * -1.f, nr = ax * (-ws) + ay;   // outward normals of the outline
            sl0[i] = o.Vert(L2W(x, -w, zt), nl, kBoardWood); sl1[i] = o.Vert(L2W(x, -w, zb), nl, kBoardWood);
            sr0[i] = o.Vert(L2W(x, w, zt), nr, kBoardWood); sr1[i] = o.Vert(L2W(x, w, zb), nr, kBoardWood);
        }
        for (int i = 0; i < N; i++) {
            o.Quad(tl[i], tl[i + 1], tr[i + 1], tr[i]);
            o.Quad(bl[i], br[i], br[i + 1], bl[i + 1]);
            o.Quad(sl0[i], sl1[i], sl1[i + 1], sl0[i + 1]);
            o.Quad(sr0[i], sr0[i + 1], sr1[i + 1], sr1[i]);
        }
        // nose mark: a light arrow on the grip pointing to the nose
        const float zx = kDeckTop + 0.08f;
        const uint32_t a = o.Vert(L2W(36.f, 0.f, zx + Kick(36.f)), az, kBoardNose);
        const uint32_t b = o.Vert(L2W(29.f, -3.2f, zx + Kick(29.f)), az, kBoardNose);
        const uint32_t c = o.Vert(L2W(29.f, 3.2f, zx + Kick(29.f)), az, kBoardNose);
        o.Tri(a, b, c);
    }
    // ---- trucks and wheels
    for (int t = 0; t < 2; t++) {
        const V wa = wheel[t * 2], wb = wheel[t * 2 + 1];
        V axle;
        if (!Unit(wb - wa, axle)) axle = ay;
        const V mid = (wa + wb) * 0.5f;
        const float xt = t == 0 ? 21.3f : -21.3f;
        const V base = L2W(xt, 0.f, kDeckTop - kDeckThick);
        o.Tube(wa + axle * 1.2f, wb - axle * 1.2f, 0.7f, kBoardMetal);                                     // axle
        V e1, e2;
        {
            V h = az - axle * Dot(az, axle);
            if (!Unit(h, e1)) e1 = az;
            e2 = Cross(axle, e1);
        }
        o.Ellipsoid(mid + e1 * 0.6f, axle, e2, e1, 6.0f, 1.6f, 1.8f, kBoardMetal, 6, 14);                    // hanger
        o.Tube(mid + e1 * 0.6f, base, 1.3f, kBoardMetal);                                                   // kingpin / riser
        o.Ellipsoid(base - az * 0.4f, ax, ay, az, 3.6f, 3.4f, 0.5f, kBoardMetal, 4, 12);                    // baseplate
        (void)truck;
        for (int s = 0; s < 2; s++) {
            const V w = wheel[t * 2 + s];
            V p1;
            const V h = std::fabs(Dot(az, axle)) < 0.9f ? az : ax;
            if (!Unit(h - axle * Dot(h, axle), p1)) continue;
            o.Ellipsoid(w, p1, Cross(axle, p1), axle, kWheelRadius, kWheelRadius, 1.7f, kBoardWheel, 8, 18);
        }
    }
    for (auto& v : out.verts)
        for (float f : v.pos) if (!std::isfinite(f)) { out.Clear(); return false; }
    return true;
}
