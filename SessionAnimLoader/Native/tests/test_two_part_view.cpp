// 2026-11-27 (user: the landing lasts too long; the interface jumps at the violet line): the length of a trick's
// landing on the timeline, and one set of rows for the two parts.
#include "check.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../../../SessionStudio/model/two_part_time.h"
#include <algorithm>

TEST(landing_length_keeps_the_trick_and_its_landing_within_two_seconds) {
    TwoPartTime tp;
    tp.first = 0.77f;
    tp.second = TrickLandingLength(tp.first, 1.97f);
    CHECK_NEAR(tp.Total(), 2.0, 1e-4);
    CHECK_NEAR(TrickLandingLength(0.77f, 0.6f), 0.6, 1e-5);    // never longer than the recording
    CHECK_NEAR(TrickLandingLength(1.9f, 1.97f), 0.8, 1e-5);    // a long trick: never shorter than the game's landing
    CHECK_NEAR(TrickLandingLength(0.f, 5.f), 2.0 - 1.0 / 30.0, 1e-4);
    CHECK_NEAR(TrickLandingLength(0.77f, 0.f), 0.1, 1e-5);
}

TEST(rows_of_the_two_parts_are_one_list_in_the_same_order) {
    typedef EditorDoc::KeyGroup G;
    typedef EditorDoc::KeyRow R;
    const std::vector<G> trick = {G{"armL", "L Arm", {10, 11}, {R{"L Upper arm", {10}, 10}, R{"L Forearm", {11}, 11}}},
                                  G{"pelvis", "Pelvis", {2}, {}}};
    const std::vector<G> land = {G{"head", "Head", {30}, {R{"Head", {30}, 30}}},
                                 G{"armL", "L Arm", {9, 11}, {R{"L Clavicle", {9}, 9}, R{"L Forearm", {11}, 11}}}};
    const std::vector<G> a = EditorDoc::MergeKeyGroups(trick, land), b = EditorDoc::MergeKeyGroups(land, trick);
    CHECK(a.size() == 3);
    if (a.size() != 3) return;
    CHECK(a[0].id == "head" && a[1].id == "armL" && a[2].id == "pelvis");
    CHECK(a[1].bones == (std::vector<int>{9, 10, 11}));
    CHECK(a[1].children.size() == 3);
    if (a[1].children.size() == 3) CHECK(a[1].children[0].primary == 9 && a[1].children[1].primary == 10 && a[1].children[2].primary == 11);
    CHECK(a[2].children.empty());
    // the same rows whichever part is open
    CHECK(b.size() == a.size());
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
        CHECK(a[i].id == b[i].id && a[i].bones == b[i].bones && a[i].children.size() == b[i].children.size());
        for (size_t k = 0; k < std::min(a[i].children.size(), b[i].children.size()); k++)
            CHECK(a[i].children[k].label == b[i].children[k].label && a[i].children[k].bones == b[i].children[k].bones);
    }
    CHECK(EditorDoc::MergeKeyGroups(trick, {}).size() == 2);
}

// 2026-11-27: a click in the void deselects; a click on the body does not
#include "../../../SessionStudio/model/view_math.h"

TEST(point_on_the_body_or_in_the_void) {
    OrbitCamera c;
    c.yawDeg = 180.f; c.pitchDeg = 0.f; c.dist = 200.f;
    c.target[0] = 0.f; c.target[1] = 0.f; c.target[2] = 100.f;
    Mesh m;   // one triangle around the point aimed at, in the plane facing the camera
    auto V = [&](float y, float z) { m.verts.push_back(MeshVertex{{0.f, y, z}, {1.f, 0.f, 0.f}, 0}); };
    V(-20.f, 80.f); V(20.f, 80.f); V(0.f, 130.f);
    m.idx = {0, 1, 2};
    CHECK(PointOnMesh(c, 1.f, m, 800.f, 800.f, 400.f, 400.f));
    CHECK(!PointOnMesh(c, 1.f, m, 800.f, 800.f, 40.f, 400.f));
    CHECK(!PointOnMesh(c, 1.f, m, 800.f, 800.f, 400.f, 40.f));
    m.idx = {0, 2, 1};   // either winding
    CHECK(PointOnMesh(c, 1.f, m, 800.f, 800.f, 400.f, 400.f));
    m.idx = {0, 1, 9};   // a bad index is ignored
    CHECK(!PointOnMesh(c, 1.f, m, 800.f, 800.f, 400.f, 400.f));
}
