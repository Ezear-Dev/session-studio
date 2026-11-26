// Editor: the skateboard of the 3D view (PLAN-MANNEQUIN task 3) and the flat shoes of the mannequin.
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../../AnimStudioEditor/model/board_mesh.h"
#include "../../AnimStudioEditor/model/editor_doc.h"
#include "../../AnimStudioEditor/model/mannequin.h"
#include <cstdio>
#include <string>

namespace {
SkeletonFile SkelB() { SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); return s; }
int IdxB(const SkeletonFile& s, const char* n) { for (size_t i = 0; i < s.names.size(); i++) if (s.names[i] == n) return (int)i; return -1; }
std::vector<FTr> Flat(const SkeletonFile& s) { return std::vector<FTr>(s.names.size(), FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0}); }
// mean normal z and centroid of the vertices of one colour
bool Stat(const Mesh& m, uint32_t col, float& nz, float c[3], float& zmin) {
    int k = 0; nz = 0; c[0] = c[1] = c[2] = 0; zmin = 1e9f;
    for (auto& v : m.verts) if (v.rgba == col) { nz += v.nrm[2]; for (int j = 0; j < 3; j++) c[j] += v.pos[j]; zmin = std::min(zmin, v.pos[2]); k++; }
    if (!k) return false;
    nz /= k; for (int j = 0; j < 3; j++) c[j] /= k;
    return true;
}
bool Finite(const Mesh& m) {
    for (auto& v : m.verts) for (int j = 0; j < 3; j++) if (!std::isfinite(v.pos[j]) || !std::isfinite(v.nrm[j])) return false;
    for (uint32_t i : m.idx) if (i >= m.verts.size()) return false;
    return m.triBone.size() * 3 == m.idx.size();
}
bool ReadB(const std::string& p, std::string& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[65536]; size_t k; out.clear();
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    return true;
}
} // namespace

TEST(board_follows_the_deck_bone) {
    const SkeletonFile s = SkelB();
    const BoardRig r = BuildBoardRig(s);
    CHECK(r.ok && r.deck == IdxB(s, "SKXX_SkateSkel_Flipper") && r.wheel[3] == IdxB(s, "SKXX_SkateSkel_Wheel_BR"));
    std::vector<FTr> cs = Flat(s);
    cs[r.deck].pz = 5.4f;
    Mesh m;
    CHECK(BuildBoardMesh(r, cs, false, m) && Finite(m) && !m.verts.empty());
    float nz, c[3], zmin;
    CHECK(Stat(m, kBoardGrip, nz, c, zmin) && nz > 0.9f);               // grip up
    CHECK(std::fabs(c[2] - (5.4f + kDeckTop)) < 1.5f);
    CHECK(Stat(m, kBoardWheel, nz, c, zmin) && std::fabs(zmin) < 0.2f); // wheels on the ground
    CHECK(Stat(m, kBoardNose, nz, c, zmin) && c[0] > 25.f);              // nose mark towards +X
    // moved 10 cm along X (positions recorded): everything follows
    cs[r.deck].px = 10.f;
    for (int w : r.wheel) { cs[w].px = 10.f + (w == r.wheel[0] || w == r.wheel[1] ? 23.4f : -23.4f); cs[w].pz = 2.7f; cs[w].py = (w == r.wheel[0] || w == r.wheel[2]) ? -8.2f : 8.2f; }
    CHECK(BuildBoardMesh(r, cs, true, m));
    CHECK(Stat(m, kBoardGrip, nz, c, zmin) && std::fabs(c[0] - 10.f) < 1.5f);
    // flipped 180 degrees about its length: the grip faces down
    cs[r.deck].qx = 1.f; cs[r.deck].qw = 0.f; cs[r.deck].pz = 30.f;
    CHECK(BuildBoardMesh(r, cs, true, m));
    CHECK(Stat(m, kBoardGrip, nz, c, zmin) && nz < -0.9f);
}

TEST(board_without_bones_or_with_bad_values) {
    SkeletonFile s = SkelB();
    std::vector<FTr> cs = Flat(s);
    Mesh m;
    SkeletonFile s2 = s;
    for (auto& nm : s2.names) if (nm.rfind("SKXX_", 0) == 0) nm += "_gone";
    const BoardRig r2 = BuildBoardRig(s2);
    CHECK(!r2.ok && !BuildBoardMesh(r2, cs, true, m) && m.verts.empty());
    const BoardRig r = BuildBoardRig(s);
    cs[r.deck].pz = std::nanf("");
    CHECK(!BuildBoardMesh(r, cs, true, m) && m.verts.empty());
    cs.resize(20);   // pose shorter than the skeleton
    CHECK(!BuildBoardMesh(r, cs, true, m));
}

// the real figures (skipped when absent): the board under the feet while riding, wheels on the ground; the manual figure
// (no recorded board positions) set on its wheels; the shoes flat on the ground when standing
TEST(board_and_shoes_on_the_real_figures) {
    const std::string dir = "C:/Program Files (x86)/Steam/steamapps/common/Session/SessionGame/Binaries/Win64/Mods/AnimStudio/clips/";
    std::string skel, err;
    if (!ReadB(dir + "_skeleton.json", skel)) return;
    auto pose = [&](const char* key, EditorDoc& d, std::vector<FTr>& cs) {
        std::string fig;
        FigureVariant v;
        if (!ReadB(dir + "_figures/" + key + ".json", fig) || !d.LoadSkeleton(skel, err) || !ParseFigureKey(key, v) || !d.LoadFigure(fig, v, err)) return false;
        d.OriginPose(0.f, cs);
        return true;
    };
    EditorDoc d;
    std::vector<FTr> cs;
    if (pose("Riding_RGS_regular", d, cs)) {
        const BoardRig r = BuildBoardRig(d.skeleton());
        float tmp[3];
        const bool rec = d.OriginPos(r.deck, 0.f, tmp);
        CHECK(rec);
        Mesh m;
        CHECK(BuildBoardMesh(r, cs, rec, m) && Finite(m));
        float nz, c[3], zmin;
        CHECK(Stat(m, kBoardWheel, nz, c, zmin) && zmin > -1.f && zmin < 1.5f);
        const float top = cs[r.deck].pz + kDeckTop;
        for (const char* toe : {"AMXX_L_Toe0", "AMXX_R_Toe0"}) {
            const int b = IdxB(d.skeleton(), toe);
            std::printf("board: riding %s %.1f cm above the grip\n", toe, cs[b].pz - top);
            CHECK(cs[b].pz - top > -1.5f && cs[b].pz - top < 4.f);   // tolerance measured on the user's figure: -0.6 and +2.4
        }
    }
    EditorDoc dm;
    if (pose("Manual_RGS_regular", dm, cs)) {
        const BoardRig r = BuildBoardRig(dm.skeleton());
        float tmp[3];
        const bool rec = dm.OriginPos(r.deck, 0.f, tmp);
        Mesh m;
        CHECK(BuildBoardMesh(r, cs, rec, m) && Finite(m));
        float nz, c[3], zmin;
        CHECK(Stat(m, kBoardWheel, nz, c, zmin) && zmin > -1.f && zmin < 1.5f);
        std::printf("board: manual figure, positions recorded %d, lowest wheel point %.1f cm\n", rec ? 1 : 0, zmin);
    }
    EditorDoc ds;
    if (pose("OnFoot_RGS_regular", ds, cs)) {
        const MannequinRig mr = BuildMannequinRig(ds.skeleton(), cs);
        CHECK(mr.shoeOk[0] && mr.shoeOk[1]);
        Mesh m;
        BuildMannequinMesh(mr, cs, std::vector<uint32_t>(ds.skeleton().names.size(), 0xFFC0C0C0u), m);
        for (int k = 0; k < 2; k++) {
            const int foot = mr.foot[k];
            float zmin = 1e9f, zminFar = 1e9f;
            for (size_t t = 0; t < m.triBone.size(); t++) {
                if (m.triBone[t] != foot) continue;
                for (int j = 0; j < 3; j++) {
                    const MeshVertex& v = m.verts[m.idx[t * 3 + j]];
                    zmin = std::min(zmin, v.pos[2]);
                }
            }
            // flat: the sole's lowest points under the heel and under the toes are at the same height
            const FTr& an = cs[foot];
            for (size_t t = 0; t < m.triBone.size(); t++) {
                if (m.triBone[t] != foot) continue;
                for (int j = 0; j < 3; j++) {
                    const MeshVertex& v = m.verts[m.idx[t * 3 + j]];
                    const float dx = v.pos[0] - an.px, dy = v.pos[1] - an.py;
                    if (dx * dx + dy * dy < 9.f) zminFar = std::min(zminFar, v.pos[2]);   // within 3 cm of the ankle: the heel
                }
            }
            std::printf("board: standing shoe %c sole %.1f cm, heel %.1f cm\n", k ? 'R' : 'L', zmin, zminFar);
            CHECK(zmin > -1.5f && zmin < 1.5f);
            CHECK(std::fabs(zminFar - zmin) < 1.5f);
        }
    }
}
