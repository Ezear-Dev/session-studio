#include "check.h"
#include "../src/clipset.h"
#include <windows.h>
#include <cstdio>

namespace {

std::wstring TempDir(const wchar_t* tag) {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    std::wstring d = std::wstring(base) + L"asn_tests_" + tag + L"_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(d.c_str(), nullptr);
    // empties the folder of a previous run
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((d + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return d;
}

void Put(const std::wstring& dir, const wchar_t* name, const std::string& text) {
    FILE* f = _wfopen((dir + L"\\" + name).c_str(), L"wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

std::string ClipText(const char* name, const char* bone, int deg) {
    char b[512];
    std::snprintf(b, sizeof(b), "{\"format\":\"sanim\",\"version\":1,\"name\":\"%s\",\"trigger\":{\"states\":[\"Manual\"]},\"play\":{\"duration\":1},\"mode\":\"additive\",\"tracks\":{\"%s\":[{\"t\":0,\"rot\":[0,0,%d]}]}}", name, bone, deg);
    return b;
}

bool AnyLine(const std::vector<std::string>& lines, const char* a, const char* b = nullptr) {
    for (auto& l : lines) if (l.find(a) != std::string::npos && (!b || l.find(b) != std::string::npos)) return true;
    return false;
}

const Clip* Find(const ClipSet* s, const char* name) {
    for (auto& c : s->clips) if (c.name == name) return &c;
    return nullptr;
}

} // namespace

TEST(clipset_empty_without_rig_then_loaded) {
    const std::wstring d = TempDir(L"rig");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    Put(d, L"b.sanim", ClipText("b", "AMXX_R_UpperArm", 10));
    ClipLibrary lib(d);
    lib.Scan();
    CHECK(lib.Current() != nullptr && lib.Current()->clips.empty());
    lib.SetRig(RefNames());
    CHECK(lib.Current()->clips.size() == 2);
    CHECK(lib.Current()->clips[0].file == "a.sanim");   // sorted by file name
    CHECK(lib.Current()->clips[0].tracks[0].index == 15);
}

// 2026-11-26 v0.21: identity by content, clips of other players in clips\others\ (sub-folders too)
TEST(clipset_others_folder_and_content_identity) {
    const std::wstring d = TempDir(L"others");
    CreateDirectoryW((d + L"\\others").c_str(), nullptr);
    CreateDirectoryW((d + L"\\others\\Bob").c_str(), nullptr);
    const std::string mine = ClipText("KickA", "AMXX_L_UpperArm", 10);
    const std::string theirs = ClipText("KickB", "AMXX_L_UpperArm", 40);
    Put(d, L"KickFlip_RGS_regular.sanim", mine);
    Put(d + L"\\others\\Bob", L"KickFlip_RGS_regular.sanim", theirs);   // same file name as mine: no clash
    Put(d + L"\\others", L"any name.sanim", mine);                       // a copy of mine under another name
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    const auto lines = lib.Scan();
    const ClipSet* s = lib.Current();
    CHECK(s->clips.size() == 3);
    const Clip *a = nullptr, *b = nullptr, *copy = nullptr;
    for (auto& c : s->clips) {
        if (c.file == "KickFlip_RGS_regular.sanim") a = &c;
        if (c.file == "others\\Bob\\KickFlip_RGS_regular.sanim") b = &c;
        if (c.file == "others\\any name.sanim") copy = &c;
    }
    CHECK(a && b && copy);
    if (!a || !b || !copy) return;
    CHECK(!a->foreign && b->foreign && copy->foreign);
    CHECK(a->netId != b->netId || a->netHash != b->netHash);       // same name, other content: other clip
    CHECK(a->netId == copy->netId && a->netHash == copy->netHash); // other name, same content: same clip
    const uint64_t id = ContentId(mine);
    CHECK(a->netId == (uint32_t)(id >> 32) && a->netHash == (uint32_t)id);
    CHECK(AnyLine(lines, "others\\Bob\\KickFlip_RGS_regular.sanim", "their skater only"));
    // removing a file of others\ is seen like any other
    DeleteFileW((d + L"\\others\\any name.sanim").c_str());
    CHECK(AnyLine(lib.Scan(), "others\\any name.sanim", "removed"));
    CHECK(lib.Current()->clips.size() == 2);
}

TEST(clipset_broken_file_is_logged_others_load) {
    const std::wstring d = TempDir(L"broken");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    Put(d, L"b.sanim", ClipText("b", "AMXX_R_UpperArm", 10));
    Put(d, L"c.sanim", "{\"format\":\"sanim\",");
    Put(d, L"notes.txt", "not a clip");
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    const auto lines = lib.Scan();
    CHECK(lib.Current()->clips.size() == 2);
    CHECK(AnyLine(lines, "c.sanim", "refus"));
    CHECK(lib.Scan().empty());   // nothing changed: not read again
}

TEST(clipset_keeps_last_good_version_then_reloads_fix) {
    const std::wstring d = TempDir(L"keep");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    CHECK(Find(lib.Current(), "a"));
    Put(d, L"a.sanim", "{\"format\":\"sanim\",\"version\":1,\"na");   // being written / truncated
    auto lines = lib.Scan();
    CHECK(Find(lib.Current(), "a") != nullptr);
    CHECK(AnyLine(lines, "a.sanim", "previous version"));
    Put(d, L"a.sanim", ClipText("a2", "AMXX_L_UpperArm", 30));
    lines = lib.Scan();
    CHECK(Find(lib.Current(), "a2") != nullptr && Find(lib.Current(), "a") == nullptr);
    CHECK(AnyLine(lines, "a.sanim", "loaded"));
}

TEST(clipset_deleted_file_is_removed) {
    const std::wstring d = TempDir(L"del");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    Put(d, L"b.sanim", ClipText("b", "AMXX_R_UpperArm", 10));
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    const uint64_t g = lib.Current()->generation;
    DeleteFileW((d + L"\\b.sanim").c_str());
    const auto lines = lib.Scan();
    CHECK(lib.Current()->clips.size() == 1 && Find(lib.Current(), "b") == nullptr);
    CHECK(lib.Current()->generation > g);
    CHECK(AnyLine(lines, "b.sanim", "removed"));
}

TEST(clipset_rig_missing_bone_refuses_that_clip) {
    const std::wstring d = TempDir(L"missing");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    Put(d, L"b.sanim", ClipText("b", "AMXX_Head", 10));
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    CHECK(lib.Current()->clips.size() == 2);
    std::vector<std::string> names = RefNames();
    names[35] = "AMXX_Head_renomme";
    const auto lines = lib.SetRig(names);
    CHECK(lib.Current()->clips.size() == 1 && Find(lib.Current(), "a"));
    CHECK(AnyLine(lines, "b.sanim", "AMXX_Head"));
}

TEST(clipset_studio_settings_override_mirror_conventions) {
    const std::wstring d = TempDir(L"studio");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    Put(d, L"_studio.json", "{ \"mirrorConv\": { \"AMXX_L_UpperArm\": \"Y\", \"AMXX_Head\": \"I\" } }");
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    const auto& ov = lib.Current()->convOverride;
    CHECK(ov.size() == 2);
    bool arm = false, head = false;
    for (auto& p : ov) { if (p.first == 15 && p.second == 2) arm = true; if (p.first == 35 && p.second == 0) head = true; }
    CHECK(arm && head);
    const uint64_t stamp = lib.Current()->mirrorStamp;
    Put(d, L"_studio.json", "{ \"mirrorConv\": { \"AMXX_L_UpperArm\": \"W\" } }");
    const auto lines = lib.Scan();
    CHECK(lib.Current()->convOverride.size() == 2);   // refused value: the previous settings are kept
    CHECK(lib.Current()->mirrorStamp == stamp);
    CHECK(AnyLine(lines, "_studio.json"));
    Put(d, L"_studio.json", "{ \"mirrorConv\": { \"AMXX_Inconnu\": \"X\" } }");
    const auto lines2 = lib.Scan();
    CHECK(AnyLine(lines2, "AMXX_Inconnu"));
}

TEST(clipset_locked_file_is_retried_next_scan) {
    const std::wstring d = TempDir(L"locked");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    HANDLE h = CreateFileW((d + L"\\a.sanim").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);   // an editor holds the file
    CHECK(h != INVALID_HANDLE_VALUE);
    lib.Scan();
    CHECK(Find(lib.Current(), "a") == nullptr);
    CloseHandle(h);
    lib.Scan();
    CHECK(Find(lib.Current(), "a") != nullptr);
}

TEST(clipset_clip_stamp_changes_only_when_content_changes) {
    const std::wstring d = TempDir(L"stamp");
    Put(d, L"a.sanim", ClipText("a", "AMXX_L_UpperArm", 10));
    Put(d, L"b.sanim", ClipText("b", "AMXX_R_UpperArm", 10));
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    const uint64_t sa = Find(lib.Current(), "a")->stamp, sb = Find(lib.Current(), "b")->stamp;
    Put(d, L"b.sanim", ClipText("b", "AMXX_R_UpperArm", 40));
    lib.Scan();
    CHECK(Find(lib.Current(), "a")->stamp == sa);
    CHECK(Find(lib.Current(), "b")->stamp != sb);
}

TEST(clipset_captures_subfolder_is_ignored) {
    const std::wstring d = TempDir(L"sub");
    CreateDirectoryW((d + L"\\captures").c_str(), nullptr);
    Put(d + L"\\captures", L"x.sanim", ClipText("x", "AMXX_L_UpperArm", 10));
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    CHECK(lib.Current()->clips.empty());
    DeleteFileW((d + L"\\captures\\x.sanim").c_str());
}

// --check-clips <folder>: loads a clips folder with the reference skeleton
int CheckClipsMain(const char* dir) {
    std::wstring w;
    for (const char* p = dir; *p; p++) w += (wchar_t)(unsigned char)*p;
    ClipLibrary lib(w);
    lib.SetRig(RefNames());
    int bad = 0;
    for (auto& l : lib.Scan()) {
        std::printf("%s\n", l.c_str());
        if (l.find("refused") != std::string::npos || l.find("previous") != std::string::npos) bad++;
    }
    std::printf("%d clip(s) loaded, %d refused\n", (int)lib.Current()->clips.size(), bad);
    return bad ? 1 : 0;
}
