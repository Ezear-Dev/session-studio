// Editor: the 3D mannequin geometry and the stage projection (PLAN-MANNEQUIN task 1).
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../../AnimStudioEditor/model/editor_doc.h"
#include "../../AnimStudioEditor/model/mannequin.h"
#include "../../AnimStudioEditor/model/view_math.h"
#include <cstdio>
#include <array>
#include <map>
#include <string>

namespace {
SkeletonFile SkelM() {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions();
    return s;
}
int Idx(const SkeletonFile& s, const char* n) {
    for (size_t i = 0; i < s.names.size(); i++) if (s.names[i] == n) return (int)i;
    return -1;
}
// a standing skater facing +X (UE: Y right, so the skater's left is -Y), every bone rotation identity
std::vector<FTr> Stand(const SkeletonFile& s) {
    std::vector<FTr> cs(s.names.size(), FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    const std::map<std::string, std::array<float, 3>> p = {
        {"AMXX_Pelvis", {0, 0, 95}}, {"AMXX_Spine1", {0, 0, 105}}, {"AMXX_Spine2", {0, 0, 118}}, {"AMXX_Spine3", {0, 0, 132}},
        {"AMXX_Neck", {0, 0, 146}}, {"AMXX_Head", {0, 0, 156}},
        {"AMXX_L_Clavicle", {2, -3, 140}}, {"AMXX_L_UpperArm", {0, -18, 140}}, {"AMXX_L_Forearm", {0, -22, 110}},
        {"AMXX_L_Hand", {0, -24, 88}}, {"AMXX_L_Finger0", {3, -24, 85}}, {"AMXX_L_Finger01", {5, -24, 82}}, {"AMXX_L_Finger02", {6, -24, 80}},
        {"AMXX_L_Finger1", {2, -24, 80}}, {"AMXX_L_Finger2", {0, -24, 79}}, {"AMXX_L_Finger4", {-3, -24, 80}},
        {"AMXX_R_Clavicle", {2, 3, 140}}, {"AMXX_R_UpperArm", {0, 18, 140}}, {"AMXX_R_Forearm", {0, 22, 110}},
        {"AMXX_R_Hand", {0, 24, 88}}, {"AMXX_R_Finger0", {3, 24, 85}}, {"AMXX_R_Finger01", {5, 24, 82}}, {"AMXX_R_Finger02", {6, 24, 80}},
        {"AMXX_R_Finger1", {2, 24, 80}}, {"AMXX_R_Finger2", {0, 24, 79}}, {"AMXX_R_Finger4", {-3, 24, 80}},
        {"AMXX_L_Thigh", {0, -9, 90}}, {"AMXX_L_Calf", {2, -10, 49}}, {"AMXX_L_Foot", {0, -10, 8}}, {"AMXX_L_Toe0", {13, -11, 2}},
        {"AMXX_R_Thigh", {0, 9, 90}}, {"AMXX_R_Calf", {2, 10, 49}}, {"AMXX_R_Foot", {0, 10, 8}}, {"AMXX_R_Toe0", {13, 11, 2}},
    };
    for (auto& kv : p) {
        const int i = Idx(s, kv.first.c_str());
        if (i >= 0) { cs[i].px = kv.second[0]; cs[i].py = kv.second[1]; cs[i].pz = kv.second[2]; }
    }
    return cs;
}
float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// distance of point p from segment a-b, and its position along it (0..1, unclamped)
float SegDist(const float p[3], const FTr& a, const FTr& b, float& u) {
    const float d[3] = {b.px - a.px, b.py - a.py, b.pz - a.pz};
    const float v[3] = {p[0] - a.px, p[1] - a.py, p[2] - a.pz};
    const float l2 = Dot(d, d);
    u = l2 > 0 ? Dot(v, d) / l2 : 0.f;
    const float c = u < 0 ? 0 : u > 1 ? 1 : u;
    const float q[3] = {v[0] - d[0] * c, v[1] - d[1] * c, v[2] - d[2] * c};
    return std::sqrt(Dot(q, q));
}
bool AllFinite(const Mesh& m) {
    for (auto& v : m.verts) {
        for (int k = 0; k < 3; k++) if (!std::isfinite(v.pos[k]) || !std::isfinite(v.nrm[k])) return false;
        const float l = std::sqrt(Dot(v.nrm, v.nrm));
        if (std::fabs(l - 1.f) > 1e-3f) return false;
    }
    for (uint32_t i : m.idx) if (i >= m.verts.size()) return false;
    return m.triBone.size() * 3 == m.idx.size();
}
// vertices of the triangles of one bone (indices into verts)
std::vector<uint32_t> VertsOf(const Mesh& m, int bone) {
    std::vector<uint32_t> out;
    for (size_t t = 0; t < m.triBone.size(); t++) if (m.triBone[t] == bone) for (int k = 0; k < 3; k++) out.push_back(m.idx[t * 3 + k]);
    return out;
}
} // namespace

TEST(mannequin_stage_projection_matches_project) {
    for (float yaw : {0.f, 37.f, 200.f}) for (float pitch : {-30.f, 15.f, 70.f}) for (float aspect : {0.6f, 1.7f}) {
        OrbitCamera c; c.yawDeg = yaw; c.pitchDeg = pitch; c.dist = 240.f;
        float m[16];
        StageViewProj(c, aspect, m);
        const float W = 800.f, H = 800.f / aspect;
        const float pts[4][3] = {{0, 0, 100}, {20, -30, 140}, {-15, 25, 10}, {5, 40, 170}};
        float prevDepth = -1.f, prevDist = -1.f;
        float eye[3]; c.Eye(eye);
        float fwd[3] = {c.target[0] - eye[0], c.target[1] - eye[1], c.target[2] - eye[2]};   // depth = distance along the view axis
        { const float l = std::sqrt(Dot(fwd, fwd)); for (float& x : fwd) x /= l; }
        for (auto& p : pts) {
            const float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
            const float y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
            const float z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
            const float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            CHECK(w > 0.f);
            const float mir[3] = {p[0], -p[1], p[2]};   // what the view's PU() projects
            float sx, sy;
            CHECK(Project(c, aspect, mir, W, H, sx, sy));
            CHECK_NEAR((x / w * 0.5f + 0.5f) * W, sx, 0.01);
            CHECK_NEAR((1.f - (y / w * 0.5f + 0.5f)) * H, sy, 0.01);
            const float depth = z / w;
            CHECK(depth >= 0.f && depth <= 1.f);
            const float dv[3] = {mir[0] - eye[0], mir[1] - eye[1], mir[2] - eye[2]};
            const float dist = Dot(dv, fwd);
            if (prevDist >= 0.f) CHECK((dist < prevDist) == (depth < prevDepth));
            prevDepth = depth; prevDist = dist;
        }
    }
}

TEST(mannequin_rig_and_axes) {
    const SkeletonFile s = SkelM();
    const std::vector<FTr> cs = Stand(s);
    const MannequinRig r = BuildMannequinRig(s, cs);
    CHECK(r.ok && r.head >= 0 && r.hand[0] == Idx(s, "AMXX_L_Hand") && r.hand[1] == Idx(s, "AMXX_R_Hand"));
    CHECK(r.thumb[0] == Idx(s, "AMXX_L_Finger0") && r.toe[1] == Idx(s, "AMXX_R_Toe0"));
    float side[3], up[3], front[3];
    CHECK(TorsoAxes(r, cs, side, up, front));
    CHECK(side[1] < -0.9f);   // the skater's left is -Y
    CHECK(up[2] > 0.9f);
    CHECK(front[0] > 0.9f);   // toes point +X: so does the front
    // the head faces the front: head front (head bone space) back to component space
    float hf[3] = {r.headFront[0], r.headFront[1], r.headFront[2]};
    const int h = r.head;
    QRotVec(TrQ(cs[h]), hf[0], hf[1], hf[2]);
    CHECK(hf[0] > 0.9f);
}

TEST(mannequin_mesh_basics) {
    const SkeletonFile s = SkelM();
    const std::vector<FTr> cs = Stand(s);
    const MannequinRig r = BuildMannequinRig(s, cs);
    std::vector<uint32_t> cols(s.names.size(), 0xFFC0C0C0u);
    Mesh m;
    BuildMannequinMesh(r, cs, cols, m);
    CHECK(!m.verts.empty() && AllFinite(m));
    CHECK(m.idx.size() / 3 < 20000);
    for (const char* b : {"AMXX_Pelvis", "AMXX_Spine2", "AMXX_Head", "AMXX_L_UpperArm", "AMXX_R_Forearm", "AMXX_L_Hand",
                          "AMXX_R_Thigh", "AMXX_L_Calf", "AMXX_R_Foot", "AMXX_L_Finger0"})
        CHECK(!VertsOf(m, Idx(s, b)).empty());
    // nothing of the body far from the skater (no triangle shooting across the view)
    for (auto& v : m.verts) CHECK(std::fabs(v.pos[0]) < 60.f && std::fabs(v.pos[1]) < 60.f && v.pos[2] > -10.f && v.pos[2] < 200.f);
    // the nose sits on the front of the head
    const int h = Idx(s, "AMXX_Head");
    float maxFront = -1e9f;
    for (uint32_t i : VertsOf(m, h)) maxFront = std::max(maxFront, m.verts[i].pos[0]);
    float minBack = 1e9f;
    for (uint32_t i : VertsOf(m, h)) minBack = std::min(minBack, m.verts[i].pos[0]);
    CHECK(maxFront > -minBack + 1.f);   // the face sticks out further in front than the back of the head
}

TEST(mannequin_forearm_follows_its_segment_and_stripe_turns) {
    const SkeletonFile s = SkelM();
    std::vector<FTr> cs = Stand(s);
    const MannequinRig r = BuildMannequinRig(s, cs);
    const int fo = Idx(s, "AMXX_L_Forearm"), ha = Idx(s, "AMXX_L_Hand");
    std::vector<uint32_t> cols(s.names.size(), 0xFFC0C0C0u);
    auto check = [&](const std::vector<FTr>& pose, float stripeOut[3]) {
        Mesh m;
        BuildMannequinMesh(r, pose, cols, m);
        CHECK(AllFinite(m));
        float uMin = 1e9f, uMax = -1e9f, worst = 0.f;
        float st[3] = {0, 0, 0}; int ns = 0;
        for (uint32_t i : VertsOf(m, fo)) {
            float u;
            const float d = SegDist(m.verts[i].pos, pose[fo], pose[ha], u);
            worst = std::max(worst, d);
            uMin = std::min(uMin, u); uMax = std::max(uMax, u);
            if (m.verts[i].rgba == kMannequinStripe) { for (int k = 0; k < 3; k++) st[k] += m.verts[i].pos[k]; ns++; }
        }
        CHECK(worst < 6.f);                  // within the forearm's radius (plus the stripe)
        const float L = 22.f;                // about the elbow - wrist length of Stand()
        CHECK(uMin * L < 1.f && uMax * L > L - 1.f);   // reaches both joints (no gap over 1 cm)
        CHECK(ns > 0);
        for (int k = 0; k < 3; k++) stripeOut[k] = ns ? st[k] / ns : 0.f;
    };
    float a[3], b[3];
    check(cs, a);
    // the forearm turned 90 degrees on its own axis (elbow -> wrist), its stripe moves around it
    float ax[3] = {cs[ha].px - cs[fo].px, cs[ha].py - cs[fo].py, cs[ha].pz - cs[fo].pz};
    const float l = std::sqrt(Dot(ax, ax));
    for (float& x : ax) x /= l;
    const float sn = std::sin(0.785398f);
    const Quat q = QNorm(Quat{ax[0] * sn, ax[1] * sn, ax[2] * sn, std::cos(0.785398f)});
    cs[fo].qx = q.x; cs[fo].qy = q.y; cs[fo].qz = q.z; cs[fo].qw = q.w;
    check(cs, b);
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    CHECK(std::sqrt(Dot(d, d)) > 2.f);
}

TEST(mannequin_broken_poses_do_not_break) {
    const SkeletonFile s = SkelM();
    const MannequinRig r = BuildMannequinRig(s, Stand(s));
    std::vector<uint32_t> cols(s.names.size(), 0xFFC0C0C0u);
    // a bent pose: the arm through the chest
    std::vector<FTr> cs = Stand(s);
    const int lf = Idx(s, "AMXX_L_Forearm"), lh = Idx(s, "AMXX_L_Hand"), rc = Idx(s, "AMXX_R_Calf"), rf = Idx(s, "AMXX_R_Foot");
    cs[lh].px = 0; cs[lh].py = 10; cs[lh].pz = 125;
    Mesh m;
    BuildMannequinMesh(r, cs, cols, m);
    CHECK(AllFinite(m));
    // two joints at the same place
    cs = Stand(s);
    cs[rf] = cs[rc];
    BuildMannequinMesh(r, cs, cols, m);
    CHECK(AllFinite(m) && !m.verts.empty());
    // a NaN in one bone: that part skipped, the rest drawn
    cs = Stand(s);
    cs[lf].px = std::nanf("");
    BuildMannequinMesh(r, cs, cols, m);
    CHECK(AllFinite(m) && !m.verts.empty());
    CHECK(VertsOf(m, lf).empty());
    for (auto& v : m.verts) CHECK(std::fabs(v.pos[0]) < 60.f && std::fabs(v.pos[1]) < 60.f);
    // a pose shorter than the skeleton (old file): nothing read out of range
    cs = Stand(s);
    cs.resize(20);
    BuildMannequinMesh(r, cs, cols, m);
    CHECK(AllFinite(m));
    // a skeleton without hand and toe bones
    SkeletonFile s2 = s;
    for (auto& nme : s2.names) if (nme.find("Finger") != std::string::npos || nme.find("Toe") != std::string::npos || nme.find("_Hand") != std::string::npos) nme += "_gone";
    const MannequinRig r2 = BuildMannequinRig(s2, Stand(s));
    CHECK(r2.ok && r2.hand[0] < 0 && r2.toe[0] < 0);
    BuildMannequinMesh(r2, Stand(s), cols, m);
    CHECK(AllFinite(m) && !m.verts.empty());
    // no rig at all
    BuildMannequinMesh(MannequinRig{}, Stand(s), cols, m);
    CHECK(m.verts.empty() && m.idx.empty());
}

// the real files of the game folder (skipped when absent): the front from the standing pose is the way the knees bend
// while riding, and the face looks the way of the chest when standing
TEST(mannequin_on_the_real_figures) {
    const std::string dir = "C:/Program Files (x86)/Steam/steamapps/common/Session/SessionGame/Binaries/Win64/Mods/AnimStudio/clips/";
    auto read = [](const std::string& p, std::string& out) {
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) return false;
        char buf[65536]; size_t k; out.clear();
        while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
        fclose(f);
        return true;
    };
    std::string skel, stand, ride, err;
    if (!read(dir + "_skeleton.json", skel) || !read(dir + "_figures/OnFoot_RGS_regular.json", stand) || !read(dir + "_figures/Riding_RGS_regular.json", ride)) return;
    EditorDoc d;
    CHECK(d.LoadSkeleton(skel, err));
    FigureVariant vs, vr;
    CHECK(ParseFigureKey("OnFoot_RGS_regular", vs) && ParseFigureKey("Riding_RGS_regular", vr));
    CHECK(d.LoadFigure(stand, vs, err));
    std::vector<FTr> st, rd;
    d.OriginPose(0.f, st);
    CHECK(d.LoadFigure(ride, vr, err));
    d.OriginPose(0.f, rd);
    const SkeletonFile& s = d.skeleton();
    const MannequinRig r = BuildMannequinRig(s, st);
    CHECK(r.ok);
    float side[3], up[3], front[3];
    CHECK(TorsoAxes(r, st, side, up, front));
    // standing: the nose of the head in front of the chest
    float hf[3] = {r.headFront[0], r.headFront[1], r.headFront[2]};
    QRotVec(TrQ(st[r.head]), hf[0], hf[1], hf[2]);
    CHECK(Dot(hf, front) > 0.8f);
    // riding: both knees bend towards the front
    CHECK(TorsoAxes(r, rd, side, up, front));
    for (int k = 0; k < 2; k++) {
        const FTr &hp = rd[r.thigh[k]], &kn = rd[r.calf[k]], &an = rd[r.foot[k]];
        const float bend[3] = {kn.px - (hp.px + an.px) * 0.5f, kn.py - (hp.py + an.py) * 0.5f, kn.pz - (hp.pz + an.pz) * 0.5f};
        std::printf("mannequin: riding knee %c bends %.1f cm to the front\n", k ? 'R' : 'L', Dot(bend, front));
        CHECK(Dot(bend, front) > 1.f);
    }
    Mesh m;
    BuildMannequinMesh(r, rd, std::vector<uint32_t>(s.names.size(), 0xFFC0C0C0u), m);
    CHECK(AllFinite(m) && m.idx.size() / 3 < 20000);
    std::printf("mannequin: %zu triangles on the riding pose\n", m.idx.size() / 3);
}
