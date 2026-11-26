#include "check.h"
#include "../src/math.h"
#include <cstring>

static float QAngleDeg(Quat a, Quat b) {   // angle between two rotations
    float d = std::fabs(QDot(QNorm(a), QNorm(b)));
    if (d > 1.f) d = 1.f;
    return 2.f * std::acos(d) * 57.2957795f;
}

TEST(math_mul_inverse_is_identity) {
    Quat q = QFromRotatorDeg(10, 20, 30);
    Quat r = QMul(q, QInv(q));
    CHECK_NEAR(r.x, 0, 1e-6); CHECK_NEAR(r.y, 0, 1e-6); CHECK_NEAR(r.z, 0, 1e-6); CHECK_NEAR(std::fabs(r.w), 1, 1e-6);
}

TEST(math_rotator_yaw90_turns_x_to_y) {
    Quat q = QFromRotatorDeg(0, 90, 0);
    float x = 1, y = 0, z = 0;
    QRotVec(q, x, y, z);
    CHECK_NEAR(x, 0, 1e-5); CHECK_NEAR(y, 1, 1e-5); CHECK_NEAR(z, 0, 1e-5);
}

TEST(math_slerp_shortest_path_with_negated_quat) {
    Quat a = QFromRotatorDeg(0, 40, 0);
    Quat na{-a.x, -a.y, -a.z, -a.w};
    Quat m = QSlerp(a, na, 0.5f);
    CHECK(QAngleDeg(m, a) < 0.01f);
}

TEST(math_slerp_endpoints) {
    Quat a = QFromRotatorDeg(10, 0, 0), b = QFromRotatorDeg(0, 70, 5);
    CHECK(QAngleDeg(QSlerp(a, b, 0), a) < 0.01f);
    CHECK(QAngleDeg(QSlerp(a, b, 1), b) < 0.01f);
    CHECK_NEAR(QAngleDeg(QSlerp(a, b, 0.5f), a), QAngleDeg(a, b) * 0.5f, 0.05);
}

TEST(math_smooth_and_ease) {
    CHECK_NEAR(Smooth(0), 0, 1e-7); CHECK_NEAR(Smooth(1), 1, 1e-7); CHECK_NEAR(Smooth(0.5f), 0.5, 1e-7);
    CHECK_NEAR(EaseSine(0), 0, 1e-7); CHECK_NEAR(EaseSine(1), 1, 1e-6);
}

static void Chain(FTr* cs, int* parent) {   // racine, enfant +10 X, petit-enfant +10 X
    parent[0] = -1; parent[1] = 0; parent[2] = 1;
    for (int i = 0; i < 3; i++) { cs[i] = FTr{0,0,0,1, 10.f * i,0,0,0, 1,1,1,0}; }
}

TEST(math_local_component_roundtrip) {
    FTr cs[3], orig[3]; int parent[3];
    Chain(cs, parent);
    cs[1].qx = 0; cs[1].qz = std::sin(0.3f); cs[1].qw = std::cos(0.3f);   // a bit of rotation on bone 1
    float vx = 10, vy = 0, vz = 0; QRotVec(Quat{cs[1].qx, cs[1].qy, cs[1].qz, cs[1].qw}, vx, vy, vz);
    cs[2].px = cs[1].px + vx; cs[2].py = cs[1].py + vy; cs[2].pz = cs[1].pz + vz;
    cs[2].qz = cs[1].qz; cs[2].qw = cs[1].qw;
    std::memcpy(orig, cs, sizeof(cs));
    Quat lq[3]; float lp[3][3]; bool dirty[3] = {true, true, true};
    ToLocal(cs, parent, 3, lq, lp);
    CHECK_NEAR(lp[2][0], 10, 1e-4); CHECK_NEAR(lp[2][1], 0, 1e-4);
    ToComponent(cs, parent, 3, lq, lp, dirty);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(cs[i].px, orig[i].px, 1e-4); CHECK_NEAR(cs[i].py, orig[i].py, 1e-4); CHECK_NEAR(cs[i].pz, orig[i].pz, 1e-4);
        CHECK(QAngleDeg(Quat{cs[i].qx, cs[i].qy, cs[i].qz, cs[i].qw}, Quat{orig[i].qx, orig[i].qy, orig[i].qz, orig[i].qw}) < 0.01f);
    }
}

TEST(math_local_rotation_moves_children_only) {
    FTr cs[3]; int parent[3];
    Chain(cs, parent);
    FTr root = cs[0];
    Quat lq[3]; float lp[3][3]; bool dirty[3] = {false, true, true};
    ToLocal(cs, parent, 3, lq, lp);
    lq[1] = QFromRotatorDeg(0, 90, 0);   // 90 degrees around Z
    ToComponent(cs, parent, 3, lq, lp, dirty);
    CHECK_NEAR(cs[2].px, 10, 1e-4); CHECK_NEAR(cs[2].py, 10, 1e-4); CHECK_NEAR(cs[2].pz, 0, 1e-4);
    CHECK(std::memcmp(&cs[0], &root, sizeof(FTr)) == 0);
}
