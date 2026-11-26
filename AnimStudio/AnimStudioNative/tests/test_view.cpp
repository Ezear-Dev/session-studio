// Camera, projection, bone picking; clips folder
#include "check.h"
#include "../../AnimStudioEditor/model/view_math.h"
#include "../../AnimStudioEditor/model/paths.h"
#include <windows.h>

TEST(view_project_target_is_center) {
    OrbitCamera c;
    float sx, sy;
    CHECK(Project(c, 1600.f / 900.f, c.target, 1600, 900, sx, sy));
    CHECK_NEAR(sx, 800, 0.5); CHECK_NEAR(sy, 450, 0.5);
    const float up[3] = {c.target[0], c.target[1], c.target[2] + 20.f};   // Z up: higher on screen
    CHECK(Project(c, 1600.f / 900.f, up, 1600, 900, sx, sy));
    CHECK(sy < 450.f);
}

TEST(view_point_behind_is_rejected) {
    OrbitCamera c;
    float eye[3];
    c.Eye(eye);
    const float behind[3] = {eye[0] + (eye[0] - c.target[0]), eye[1] + (eye[1] - c.target[1]), eye[2] + (eye[2] - c.target[2])};
    float sx, sy;
    CHECK(!Project(c, 1.f, behind, 800, 800, sx, sy));
}

TEST(view_pick_nearest_selectable) {
    OrbitCamera c;
    std::vector<FTr> cs(3, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    for (int i = 0; i < 3; i++) { cs[i].px = c.target[0]; cs[i].py = c.target[1]; cs[i].pz = c.target[2]; }
    cs[1].pz += 2.f;                      // right above the centre
    cs[2].pz += 60.f;                     // loin
    std::vector<bool> sel = {false, true, true};
    CHECK(PickBone(c, 1.f, cs, sel, 800, 800, 400, 400) == 1);   // bone 0 is closer but not pickable
    float sx, sy;
    const float p2[3] = {cs[2].px, cs[2].py, cs[2].pz};
    CHECK(Project(c, 1.f, p2, 800, 800, sx, sy));
    CHECK(PickBone(c, 1.f, cs, sel, 800, 800, sx + 3, sy) == 2);
    CHECK(PickBone(c, 1.f, cs, sel, 800, 800, 40, 40) == -1);
}

TEST(view_bone_matrix_roundtrip) {
    FTr t{0, 0, 0, 1, 10, 20, 30, 0, 1, 1, 1, 0};
    const Quat q = QFromRotatorDeg(25, -40, 70);
    t.qx = q.x; t.qy = q.y; t.qz = q.z; t.qw = q.w;
    float m[16];
    BoneMatrix(t, m);
    CHECK_NEAR(m[12], 10, 1e-5); CHECK_NEAR(m[13], 20, 1e-5); CHECK_NEAR(m[14], 30, 1e-5);
    Quat r;
    CHECK(MatrixRotation(m, r));
    CHECK(std::fabs(QDot(r, q)) > 0.99999f);
    m[0] *= 1.01f;                        // slight numerical drift of ImGuizmo: orthonormalised
    CHECK(MatrixRotation(m, r));
    CHECK(std::fabs(QDot(r, q)) > 0.9999f);
}

TEST(paths_parse_vdf) {
    const char* vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"label\"\t\t\"\"\n\t}\n"
                      "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n";
    const auto v = SteamLibraryPaths(vdf);
    CHECK(v.size() == 2);
    if (v.size() == 2) { CHECK(v[0] == L"C:\\Program Files (x86)\\Steam"); CHECK(v[1] == L"D:\\SteamLibrary"); }
}

TEST(paths_clips_dir_layout) {
    CHECK(ClipsDirIn(L"D:\\SteamLibrary") == L"D:\\SteamLibrary\\steamapps\\common\\Session\\SessionGame\\Binaries\\Win64\\Mods\\AnimStudio\\clips");
}

TEST(paths_settings_roundtrip) {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    const std::wstring dir = std::wstring(base) + L"asn_tests_paths_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring settings = dir + L"\\AnimStudioEditor.ini";
    CHECK(SaveClipsDir(settings, dir));
    CHECK(FindClipsDir(settings) == dir);   // the saved setting exists: it comes first
    CHECK(SaveClipsDir(settings, dir + L"\\inexistant"));
    CHECK(FindClipsDir(settings) != dir + L"\\inexistant");   // a saved folder that disappeared is ignored
    DeleteFileW(settings.c_str());
    RemoveDirectoryW(dir.c_str());
}

TEST(view_rotator_roundtrip) {
    for (float p : {-60.f, 0.f, 35.f}) for (float y : {-150.f, 0.f, 80.f}) for (float r : {-45.f, 10.f, 170.f}) {
        float pp, yy, rr;
        QToRotatorDeg(QFromRotatorDeg(p, y, r), pp, yy, rr);
        const Quat a = QFromRotatorDeg(p, y, r), b = QFromRotatorDeg(pp, yy, rr);
        CHECK(std::fabs(QDot(a, b)) > 0.99999f);
        CHECK_NEAR(pp, p, 0.01);
    }
}
