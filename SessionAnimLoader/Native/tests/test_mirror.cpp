#include "check.h"
#include "../src/mirror.h"

namespace {
float AngDeg(Quat a, Quat b) {
    const Quat d = QMul(QInv(QNorm(a)), QNorm(b));
    double s = std::sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    if (s > 1.0) s = 1.0;
    double ang = 2.0 * std::asin(s) * 57.29577951308232;
    if (d.w < 0) ang = 360.0 - ang;
    if (ang > 180.0) ang = 360.0 - ang;
    return (float)ang;
}
// 3x3 matrix of a unit quaternion
void Mat(Quat q, double m[3][3]) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y - z * w);     m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w);     m[2][1] = 2 * (y * z + x * w);     m[2][2] = 1 - 2 * (x * x + y * y);
}
// reflection C (diagonal, det -1) matching a convention: I -> -I, X -> diag(-1,1,1)... see mirror.h
void Refl(int conv, double s[3]) {
    s[0] = s[1] = s[2] = -1.0;
    if (conv >= 1 && conv <= 3) { s[0] = s[1] = s[2] = 1.0; s[conv - 1] = -1.0; }
}
} // namespace

TEST(mirror_map_pairs_left_right) {
    MirrorMap m = RefMirror();
    CHECK(m.other.size() == 70 && m.center.size() == 70 && m.conv.size() == 70 && m.parent.size() == 70);
    CHECK(m.other[15] == 37 && m.other[37] == 15);   // L_UpperArm <-> R_UpperArm
    CHECK(m.other[3] == 7 && m.other[7] == 3);       // R_Thigh <-> L_Thigh
    CHECK(m.other[13] == 13 && m.center[13]);        // Spine3 in the centre
    CHECK(!m.center[15]);
    for (int i = 0; i < 70; i++) CHECK(m.other[m.other[i]] == i);   // involution
}

TEST(mirror_conventions_from_reference_offsets) {
    // deduced from the reference bone offsets
    MirrorMap m = RefMirror();
    auto idx = [](const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; };
    CHECK(m.conv[idx("AMXX_Pelvis")] == 3);
    CHECK(m.conv[idx("AMXX_Spine3")] == 3);
    CHECK(m.conv[idx("AMXX_Spine1")] == 3);      // inherited (no paired children)
    CHECK(m.conv[idx("AMXX_Head")] == 3);
    CHECK(m.conv[idx("AMXX_L_Clavicle")] == 0);
    CHECK(m.conv[idx("AMXX_L_UpperArm")] == 0);
    CHECK(m.conv[idx("AMXX_R_Forearm")] == 0);
    CHECK(m.conv[idx("AMXX_L_Hand")] == 3);      // fingers offset in Z only
    CHECK(m.conv[idx("AMXX_R_Hand")] == 3);
    CHECK(m.conv[idx("AMXX_L_Finger42")] == 3);  // leaf: inherits from its parent
    CHECK(m.conv[idx("AMXX_L_Foot")] == 0);
    CHECK(m.conv[idx("AMXX_R_Thigh")] == 0);
    for (int i = 0; i < 70; i++) CHECK(m.conv[i] == m.conv[m.other[i]]);
}

TEST(mirror_without_ref_positions_falls_back) {
    MirrorMap m = BuildMirrorMap({"AMXX_Root", "AMXX_L_Solo"}, {-1, 0}, {});
    CHECK(m.other[1] == 1 && !m.center[1]);
    CHECK(m.conv[0] == 3 && m.conv[1] == 0);
}

TEST(mirror_key_matches_reflection_algebra) {
    // L' = C_p L C_c (matrices): the mirrored key must be the same rotation
    const Quat k = QFromRotatorDeg(17, -41, 63);
    double L[3][3];
    Mat(k, L);
    for (int cp = 0; cp < 4; cp++)
        for (int cc = 0; cc < 4; cc++) {
            double sp[3], sc[3], E[3][3], G[3][3];
            Refl(cp, sp); Refl(cc, sc);
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) E[r][c] = sp[r] * L[r][c] * sc[c];
            Mat(MirrorKey(k, cp, cc), G);
            double err = 0;
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) err += std::fabs(E[r][c] - G[r][c]);
            CHECK(err < 1e-4);
        }
}

TEST(mirror_key_is_involution) {
    const Quat q = QFromRotatorDeg(12, -33, 47);
    for (int cp = 0; cp < 4; cp++)
        for (int cc = 0; cc < 4; cc++) CHECK(AngDeg(MirrorKey(MirrorKey(q, cp, cc), cp, cc), q) < 0.01f);
}

TEST(mirror_identity_convention_keeps_key) {
    const Quat q = QFromRotatorDeg(10, 20, 30);
    CHECK(AngDeg(MirrorKey(q, 0, 0), q) < 0.01f);
    CHECK(AngDeg(MirrorKey(QFromRotatorDeg(0, 30, 0), 3, 3), QFromRotatorDeg(0, 30, 0)) < 0.01f);   // Z keeps a yaw
    CHECK(AngDeg(MirrorKey(QFromRotatorDeg(0, 0, 30), 3, 3), QFromRotatorDeg(0, 0, -30)) < 0.01f);  // and reverses a roll
}

TEST(mirror_effective_conv_applies_overrides_to_both_sides) {
    MirrorMap m = RefMirror();
    std::vector<int> e = EffectiveConv(m, {{15, 2}});
    CHECK(e[15] == 2 && e[37] == 2);
    CHECK(e[17] == 3);
}
