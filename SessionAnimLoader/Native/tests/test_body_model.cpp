// Editor: the optional skinned body read from a .glb file (model/body_model). The test builds its own tiny model:
// two joints and a few vertices, no file from anywhere.
#include "check.h"
#include "../src/figure.h"
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../../../SessionStudio/model/body_model.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
void Put32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }
std::string Glb(std::string json, std::string bin) {
    while (json.size() % 4) json += ' ';
    while (bin.size() % 4) bin += '\0';
    std::string out;
    Put32(out, 0x46546C67u); Put32(out, 2); Put32(out, (uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
    Put32(out, (uint32_t)json.size()); Put32(out, 0x4E4F534Au); out += json;
    Put32(out, (uint32_t)bin.size()); Put32(out, 0x004E4942u); out += bin;
    return out;
}
template <class T> void Add(std::string& b, std::initializer_list<T> v) { for (T x : v) b.append((const char*)&x, sizeof(T)); }

struct Options {
    const char* nameB = "Test_L_UpperArm";
    uint8_t badJoint = 0;          // added to the first vertex's joint number
    uint16_t badIndex = 0;         // added to the first index
    int positionCount = 3;
    bool skin = true;
    bool weightOnB = true;
    const char* material = "";     // name of the opaque material
};

// glTF space: metres, Y up. Joint A at the origin; joint B one metre up, turned 90 degrees about +Y.
// UE space after loading: cm, Z up (y and z swapped): B at (0, 0, 100), its X axis pointing to -Y.
std::string TestGlb(const Options& o = {}) {
    std::string bin;
    // view 0: positions and normals interleaved (stride 24), three vertices
    Add<float>(bin, {0.10f, 1.50f, 0.20f, 1, 0, 0});   // v0: all on B
    Add<float>(bin, {0.00f, 1.00f, 0.00f, 0, 1, 0});   // v1: half A, half B (it sits on B's joint)
    Add<float>(bin, {0.00f, 0.00f, 0.30f, 0, 0, 1});   // v2: all on A
    const size_t jAt = bin.size();
    const uint8_t b = o.weightOnB ? 1 : 0;
    Add<uint8_t>(bin, {(uint8_t)(b + o.badJoint), 0, 0, 0, 0, b, 0, 0, 0, 0, 0, 0});
    const size_t wAt = bin.size();
    Add<float>(bin, {1, 0, 0, 0, 0.5f, 0.5f, 0, 0, 1, 0, 0, 0});
    const size_t iAt = bin.size();
    Add<uint16_t>(bin, {(uint16_t)(0 + o.badIndex), 1, 2});
    Add<uint16_t>(bin, {0});
    const size_t mAt = bin.size();
    Add<float>(bin, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});            // inverse bind of A: identity
    Add<float>(bin, {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, -1, 0, 1});          // of B: inverse(T(0,1,0) * Ry(90))
    auto view = [](size_t at, size_t len, int stride) {
        return "{\"buffer\":0,\"byteOffset\":" + std::to_string(at) + ",\"byteLength\":" + std::to_string(len) +
               (stride ? ",\"byteStride\":" + std::to_string(stride) : std::string()) + "}";
    };
    auto acc = [](int v, int off, int type, int count, const char* kind) {
        return "{\"bufferView\":" + std::to_string(v) + ",\"byteOffset\":" + std::to_string(off) + ",\"componentType\":" +
               std::to_string(type) + ",\"count\":" + std::to_string(count) + ",\"type\":\"" + kind + "\"}";
    };
    const std::string prim = "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":4,\"material\":";
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}]," +
        "\"bufferViews\":[" + view(0, jAt, 24) + "," + view(jAt, wAt - jAt, 0) + "," + view(wAt, iAt - wAt, 0) + "," +
        view(iAt, 6, 0) + "," + view(mAt, 128, 0) + "]," +
        "\"accessors\":[" + acc(0, 0, 5126, o.positionCount, "VEC3") + "," + acc(0, 12, 5126, 3, "VEC3") + "," +
        acc(1, 0, 5121, 3, "VEC4") + "," + acc(2, 0, 5126, 3, "VEC4") + "," + acc(3, 0, 5123, 3, "SCALAR") + "," +
        acc(4, 0, 5126, 2, "MAT4") + "]," +
        "\"materials\":[{\"name\":\"" + o.material + "\"},{\"alphaMode\":\"BLEND\"}]," +
        // an opaque part, a see-through one (left out), a second opaque part (its indices must be shifted)
        "\"meshes\":[{\"primitives\":[" + prim + "0}," + prim + "1}," + prim + "0}]}]," +
        "\"nodes\":[{\"name\":\"Test_Root\",\"children\":[1]},{\"name\":\"" + o.nameB + "\"},{\"mesh\":0" +
        (o.skin ? ",\"skin\":0" : "") + "}]" +
        (o.skin ? ",\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":5}]" : "") + "}";
    return Glb(json, bin);
}
SkeletonFile Skel() {
    SkeletonFile s;
    s.names = {"Test_Other", "Test_Root", "Test_L_UpperArm"};   // not the model's order: matched by name
    s.parents = {-1, 0, 1};
    s.refPos.assign(9, 0.f);
    return s;
}
FTr Tr(float x, float y, float z, Quat q = QIdent) { return FTr{q.x, q.y, q.z, q.w, x, y, z, 0, 1, 1, 1, 0}; }
std::vector<FTr> BindPose(const BodyModel& m) { return {Tr(500, 0, 0), m.bind[0], m.bind[1]}; }
bool Near(const float* p, float x, float y, float z, float eps = 0.01f) {
    return std::fabs(p[0] - x) < eps && std::fabs(p[1] - y) < eps && std::fabs(p[2] - z) < eps;
}
bool Loads(const std::string& bytes) { BodyModel m; std::string err; return LoadBodyModel(bytes, m, err); }
} // namespace

TEST(body_model_loads_in_ue_axes) {
    BodyModel m; std::string err;
    CHECK(LoadBodyModel(TestGlb(), m, err));
    if (m.verts.size() != 6 || m.bind.size() != 2) { CHECK(m.verts.size() == 6 && m.bind.size() == 2); return; }
    CHECK(m.idx == (std::vector<uint32_t>{0, 1, 2, 3, 4, 5}));   // the see-through part is gone, the third is shifted
    CHECK(Near(m.verts[0].pos, 10, 20, 150));
    CHECK(Near(m.verts[0].nrm, 1, 0, 0));
    CHECK(Near(m.verts[2].nrm, 0, 1, 0));
    CHECK(m.joints == (std::vector<std::string>{"Test_Root", "Test_L_UpperArm"}));
    CHECK_NEAR(m.bind[1].px, 0, 0.01); CHECK_NEAR(m.bind[1].py, 0, 0.01); CHECK_NEAR(m.bind[1].pz, 100, 0.01);
    float x = 1, y = 0, z = 0;
    QRotVec(TrQ(m.bind[1]), x, y, z);
    const float a[3] = {x, y, z};
    CHECK(Near(a, 0, -1, 0));
}

TEST(body_model_refuses_bad_files) {
    CHECK(!Loads(""));
    CHECK(!Loads("this is not a model, only some text that is long enough"));
    const std::string good = TestGlb();
    CHECK(Loads(good));
    CHECK(!Loads(good.substr(0, good.size() - 40)));   // cut short
    Options o;
    o.positionCount = 4000; CHECK(!Loads(TestGlb(o)));   // data past the end of the buffer
    o = {}; o.badJoint = 9; CHECK(!Loads(TestGlb(o)));
    o = {}; o.badIndex = 50; CHECK(!Loads(TestGlb(o)));
    o = {}; o.skin = false; CHECK(!Loads(TestGlb(o)));
}

TEST(body_model_joints_are_matched_by_name) {
    BodyModel m; BodyRig r; std::string err;
    CHECK(LoadBodyModel(TestGlb(), m, err));
    CHECK(BindBodyModel(m, Skel(), r, err) && r.ok);
    CHECK(r.bone == (std::vector<int>{1, 2}));
    Options o; o.nameB = "Test_Tail";
    CHECK(LoadBodyModel(TestGlb(o), m, err));
    err.clear();
    CHECK(!BindBodyModel(m, Skel(), r, err) && !r.ok && err.find("Test_Tail") != std::string::npos);
    o.weightOnB = false;   // the unknown joint moves nothing: fine
    CHECK(LoadBodyModel(TestGlb(o), m, err));
    CHECK(BindBodyModel(m, Skel(), r, err) && r.bone == (std::vector<int>{1, -1}));
}

TEST(body_model_in_its_bind_pose_is_unchanged) {
    BodyModel m; BodyRig r; std::string err; Mesh out;
    CHECK(LoadBodyModel(TestGlb(), m, err) && BindBodyModel(m, Skel(), r, err));
    if (!r.ok) return;
    SkinBodyModel(m, r, BindPose(m), {}, out);
    CHECK(out.verts.size() == m.verts.size() && out.idx == m.idx && out.triBone.size() == 2);
    for (size_t i = 0; i < out.verts.size() && i < m.verts.size(); i++) {
        CHECK(Near(out.verts[i].pos, m.verts[i].pos[0], m.verts[i].pos[1], m.verts[i].pos[2]));
        CHECK(Near(out.verts[i].nrm, m.verts[i].nrm[0], m.verts[i].nrm[1], m.verts[i].nrm[2]));
    }
}

TEST(body_model_follows_its_bones) {
    BodyModel m; BodyRig r; std::string err; Mesh out;
    CHECK(LoadBodyModel(TestGlb(), m, err) && BindBodyModel(m, Skel(), r, err));
    if (!r.ok) return;
    std::vector<FTr> cs = BindPose(m);
    const Quat turnX = {0.70710678f, 0, 0, 0.70710678f};   // 90 degrees about component X, around B's joint
    cs[2] = Tr(0, 0, 100, QMul(turnX, TrQ(m.bind[1])));
    SkinBodyModel(m, r, cs, {}, out);
    if (out.verts.size() < 3) { CHECK(out.verts.size() >= 3); return; }
    CHECK(Near(out.verts[0].pos, 10, -50, 120));   // all on B: turns with it
    CHECK(Near(out.verts[1].pos, 0, 0, 100));      // on the joint: stays
    CHECK(Near(out.verts[2].pos, 0, 30, 0));       // all on A: stays
    cs[2] = Tr(0, 0, 130, TrQ(m.bind[1]));         // B moved 30 cm up
    SkinBodyModel(m, r, cs, {}, out);
    CHECK(Near(out.verts[0].pos, 10, 20, 180));
    CHECK(Near(out.verts[1].pos, 0, 0, 115));      // half and half
}

TEST(body_model_mixes_bone_colours_by_weight) {
    BodyModel m; BodyRig r; std::string err; Mesh out;
    CHECK(LoadBodyModel(TestGlb(), m, err) && BindBodyModel(m, Skel(), r, err));
    if (!r.ok) return;
    SkinBodyModel(m, r, BindPose(m), {0xFF000000u, 0xFF000000u, 0xFF0000C8u}, out);   // bone 2 red 200, bone 1 black
    if (out.verts.size() < 3) { CHECK(out.verts.size() >= 3); return; }
    CHECK(out.verts[0].rgba == 0xFF0000C8u);
    CHECK(out.verts[1].rgba == 0xFF000064u);
    CHECK(out.verts[2].rgba == 0xFF000000u);
    CHECK(out.triBone[0] == 2);                    // the bone of a triangle: its first vertex's main bone
}

TEST(body_model_parts_get_a_colour_from_their_material_name) {
    auto colourOf = [](const char* material) {
        Options o; o.material = material;
        BodyModel m; std::string err;
        return LoadBodyModel(TestGlb(o), m, err) && !m.verts.empty() ? m.verts[0].rgba : 0u;
    };
    CHECK(colourOf("X_Body") == kBodySkin && colourOf("x_head_01") == kBodySkin);
    CHECK(colourOf("X_Hoodie") == kBodyTop);
    CHECK(colourOf("X_Cargo") == kBodyTrousers && colourOf("Blue Pants") == kBodyTrousers);
    CHECK(colourOf("X_Cap") == kBodyHat);
    CHECK(colourOf("X_EyeBlend") == kBodyDark && colourOf("Hair") == kBodyDark);
    CHECK(colourOf("") == kBodyOther && colourOf("03 - Default") == kBodyOther);
    CHECK(kBodySkin != kBodyTop && kBodyTop != kBodyTrousers && kBodyTrousers != kBodyOther);
}

TEST(body_model_tints_lie_over_the_part_colour) {
    Options o; o.material = "X_Hoodie";
    BodyModel m; BodyRig r; std::string err; Mesh out;
    CHECK(LoadBodyModel(TestGlb(o), m, err) && BindBodyModel(m, Skel(), r, err));
    if (!r.ok) return;
    SkinBodyModel(m, r, BindPose(m), {}, out);
    if (out.verts.size() < 3) { CHECK(out.verts.size() >= 3); return; }
    CHECK(out.verts[0].rgba == kBodyTop);                        // no tint: the part's own colour
    SkinBodyModel(m, r, BindPose(m), {0u, 0u, 0x000000FFu}, out);  // opacity 0: nothing changes
    CHECK(out.verts[0].rgba == kBodyTop);
    SkinBodyModel(m, r, BindPose(m), {0u, 0u, 0x80000000u}, out);  // black at half strength on bone 2
    const uint32_t full = out.verts[0].rgba, half = out.verts[1].rgba;
    for (int k = 0; k < 3; k++) {
        const double base = (double)((kBodyTop >> (8 * k)) & 255u);
        CHECK_NEAR((full >> (8 * k)) & 255u, base * (1.0 - 128.0 / 255.0), 1.0);
        CHECK_NEAR((half >> (8 * k)) & 255u, base * (1.0 - 64.0 / 255.0), 1.0);   // a vertex half on that bone
    }
    CHECK((full >> 24) == 255u && out.verts[2].rgba == kBodyTop);
}

TEST(body_model_stripe_marks_the_limbs_plus_y_side) {
    BodyModel m; BodyRig r; std::string err; Mesh out;
    CHECK(LoadBodyModel(TestGlb(), m, err) && BindBodyModel(m, Skel(), r, err));
    if (!r.ok) return;
    // B's +Y axis is +X in component space (UE axes): vertex 0's normal (1,0,0) faces it
    SkinBodyModel(m, r, BindPose(m), {}, out, 2);
    if (out.verts.size() < 3) { CHECK(out.verts.size() >= 3); return; }
    CHECK(out.verts[0].rgba == kMannequinStripe);
    CHECK(out.verts[1].rgba != kMannequinStripe);  // its normal faces another way
    CHECK(out.verts[2].rgba != kMannequinStripe);  // not on that bone
    SkinBodyModel(m, r, BindPose(m), {}, out);
    CHECK(out.verts[0].rgba != kMannequinStripe);  // no stripe unless asked
    SkinBodyModel(m, r, BindPose(m), {}, out, 1);
    CHECK(out.verts[2].rgba != kMannequinStripe);  // Test_Root is not a limb
}

TEST(body_model_leaves_out_what_has_no_pose) {
    BodyModel m; BodyRig r; std::string err; Mesh out;
    CHECK(LoadBodyModel(TestGlb(), m, err) && BindBodyModel(m, Skel(), r, err));
    if (!r.ok) return;
    std::vector<FTr> cs = BindPose(m);
    cs[2].px = std::nanf("");
    SkinBodyModel(m, r, cs, {}, out);
    CHECK(out.idx.empty() && out.triBone.empty());
    SkinBodyModel(m, r, std::vector<FTr>(1, Tr(0, 0, 0)), {}, out);   // a pose of another size
    CHECK(out.idx.empty());
}

// asn_tests --check-body <model.glb> <clips folder>: a report on a model of your own against the recorded skeleton and a
// standing recording. Nothing of the model is written anywhere.
int CheckBodyMain(const char* glbPath, const char* clipsDir) {
    auto read = [](const std::string& path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    std::string bytes, skelText, figText, err;
    const std::string dir = clipsDir;
    if (!read(glbPath, bytes) || !read(dir + "/_skeleton.json", skelText) || !read(dir + "/_figures/OnFoot_RGS_regular.json", figText)) {
        std::printf("cannot read the model, _skeleton.json or the OnFoot recording\n"); return 1;
    }
    BodyModel m; BodyRig r; FigureVariant v; EditorDoc doc; SkeletonFile s;
    if (!LoadBodyModel(bytes, m, err)) { std::printf("model: %s\n", err.c_str()); return 1; }
    if (!ParseSkeleton(skelText, s, err) || !BindBodyModel(m, s, r, err)) { std::printf("skeleton: %s\n", err.c_str()); return 1; }
    std::printf("model: %zu vertices, %zu triangles, %zu joints, all matched\n", m.verts.size(), m.idx.size() / 3, m.joints.size());
    if (!ParseFigureKey("OnFoot_RGS_regular", v) || !doc.LoadSkeleton(skelText, err) || !doc.LoadFigure(figText, v, err)) { std::printf("recording: %s\n", err.c_str()); return 1; }
    std::vector<FTr> cs; doc.OriginPose(0.f, cs);
    Mesh out;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; i++) SkinBodyModel(m, r, cs, {}, out);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 100.0;
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const MeshVertex& p : out.verts) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], p.pos[k]); hi[k] = std::max(hi[k], p.pos[k]); }
    for (size_t i = 0; i < s.names.size(); i++)
        if (s.names[i] == "AMXX_Head" || s.names[i] == "AMXX_L_Foot")
            std::printf("recorded %s height %.1f cm\n", s.names[i].c_str(), cs[i].pz);
    std::printf("standing: %zu triangles drawn, height %.1f to %.1f cm, width %.1f x %.1f cm\n", out.idx.size() / 3, lo[2], hi[2], hi[0] - lo[0], hi[1] - lo[1]);
    std::printf("skinning: %.2f ms per frame\n", ms);
    return 0;
}
