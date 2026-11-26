// UI redesign Phase 1 (2026-11-25): settings file, clip backups, game log reading (AnimStudioEditor/model/workspace).
#include "check.h"
#include <windows.h>
#include <cstdio>
#include "../../AnimStudioEditor/model/workspace.h"
#include "../../AnimStudioEditor/model/paths.h"

namespace {
std::wstring TempDir(const wchar_t* name) {
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    std::wstring d = std::wstring(t) + L"asn_ws_" + name + L"_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
void WriteText(const std::wstring& p, const std::string& s) {
    FILE* f = _wfsopen(p.c_str(), L"wb", _SH_DENYNO);
    if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}
std::string ReadText(const std::wstring& p) {
    std::string s;
    FILE* f = _wfsopen(p.c_str(), L"rb", _SH_DENYNO);
    if (!f) return s;
    char b[4096]; size_t k;
    while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k);
    fclose(f);
    return s;
}
void RemoveTree(const std::wstring& d) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(d + L"\\" + n); else DeleteFileW((d + L"\\" + n).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}
} // namespace

TEST(ws_settings_roundtrip) {
    Settings s = ParseSettings("clipsDir=C:\\a b\\clips\r\n# comment\nlayout.left=17.5\n");
    CHECK(s.Get("clipsDir") == "C:\\a b\\clips");
    CHECK_NEAR(s.GetFloat("layout.left", 0.f), 17.5, 1e-4);
    CHECK_NEAR(s.GetFloat("missing", 3.f), 3.0, 1e-6);
    CHECK_NEAR(s.GetFloat("clipsDir", 4.f), 4.0, 1e-6);   // not a number -> default
    s.Set("layout.left", "20");
    s.SetFloat("layout.bottom", 12.25f);
    const std::string t = SettingsText(s);
    CHECK(t.find("# comment") != std::string::npos);      // unknown lines kept
    const Settings r = ParseSettings(t);
    CHECK(r.Get("clipsDir") == "C:\\a b\\clips");
    CHECK_NEAR(r.GetFloat("layout.left", 0.f), 20.0, 1e-4);
    CHECK_NEAR(r.GetFloat("layout.bottom", 0.f), 12.25, 1e-3);
}

TEST(ws_save_clips_dir_keeps_layout) {
    const std::wstring d = TempDir(L"ini");
    const std::wstring ini = d + L"\\AnimStudioEditor.ini";
    CHECK(SaveSetting(ini, "layout.left", "21"));
    CHECK(SaveClipsDir(ini, L"C:\\x\\clips"));           // the old writer replaced the whole file
    Settings s;
    CHECK(LoadSettings(ini, s));
    CHECK(s.Get("layout.left") == "21");
    CHECK(s.Get("clipsDir") == "C:\\x\\clips");
    CHECK(SaveSetting(ini, "clipsDir", "D:\\y"));
    LoadSettings(ini, s);
    int n = 0;
    for (auto& kv : s.items) if (kv.first == "clipsDir") n++;
    CHECK(n == 1);
    CHECK(s.Get("clipsDir") == "D:\\y");
    RemoveTree(d);
}

TEST(ws_backup_keeps_newest) {
    const std::wstring d = TempDir(L"bak");
    const std::wstring clip = d + L"\\Kickflip_RGS_regular.sanim";
    const std::wstring root = d + L"\\backups";
    std::wstring w;
    std::string err;
    CHECK(BackupClip(clip, root, "20261125_100000", 3, &w, err));   // no file yet: nothing to do
    CHECK(w.empty());
    for (int i = 0; i < 5; i++) {
        WriteText(clip, "v" + std::to_string(i));
        char st[32]; std::snprintf(st, sizeof(st), "20261125_10000%d", i);
        CHECK(BackupClip(clip, root, st, 3, &w, err));
        CHECK(!w.empty());
        CHECK(ReadText(w) == "v" + std::to_string(i));
    }
    const auto list = ListBackups(root, "Kickflip_RGS_regular");
    CHECK(list.size() == 3);
    if (list.size() == 3) { CHECK(ReadText(list.front()) == "v2"); CHECK(ReadText(list.back()) == "v4"); }
    // the same stamp twice (two saves in one second): a second file, not an overwrite
    WriteText(clip, "v5");
    CHECK(BackupClip(clip, root, "20261125_100004", 10, &w, err));
    CHECK(ReadText(w) == "v5");
    CHECK(ListBackups(root, "Kickflip_RGS_regular").size() == 4);
    CHECK(ReadText(clip) == "v5");   // the clip itself is untouched
    // pruning follows time, not the name's length: "_100004_2" is older than "_100005"
    WriteText(clip, "v6");
    CHECK(BackupClip(clip, root, "20261125_100005", 4, &w, err));
    const auto l2 = ListBackups(root, "Kickflip_RGS_regular");
    CHECK(l2.size() == 4);
    if (l2.size() == 4) { CHECK(ReadText(l2.front()) == "v3"); CHECK(ReadText(l2[2]) == "v5"); CHECK(ReadText(l2.back()) == "v6"); }
    RemoveTree(d);
}

TEST(ws_game_dir) {
    CHECK(GameBinDirFromClips(L"C:\\S\\Win64\\Mods\\AnimStudio\\clips") == L"C:\\S\\Win64");
    CHECK(GameBinDirFromClips(L"C:\\S\\Win64\\mods\\animstudio\\Clips\\") == L"C:\\S\\Win64");
    CHECK(GameBinDirFromClips(L"C:\\elsewhere\\clips").empty());
    CHECK(GameBinDirFromClips(L"").empty());
}

TEST(ws_parse_game_log) {
    // the session header and install lines come from main.cpp, the clip lines from clipset.cpp
    const std::string log =
        "17:32:25.017 ---- AnimStudioNative v0.14 loaded (ExpandedTricksNative ...); clips: C:\\x\n"
        "17:32:38.126 installation: the slot still holds the engine function (exe+2b1aac0): waiting\n"
        "17:32:39.058 installation: installed in the slot @ 00007FF66A8C6378, chained behind 0000\n"
        "17:33:00.000 studio OFF\n"
        "17:33:05.000 studio ON\n"
        "17:43:25.987 clips: Riding_RGS_regular.sanim: removed (file deleted)\n"
        "17:44:53.052 clips: Riding_RGS_regular.sanim: loaded (\"Riding_RGS_regular\", 8 track(s))\n"
        "17:45:00.000 clips: Bad.sanim: invalid JSON line 3 -- refused\n"
        "17:45:01.000 clips: Old.sanim: empty track -- keeping the previous version\n"
        "17:45:02.000 clips: Locked.sanim: cannot read (file open elsewhere?) -- retrying every second\n";
    const GameLogInfo g = ParseGameLog(log);
    CHECK(g.found);
    CHECK(g.version == 14);
    CHECK(g.hooked);
    CHECK(!g.refused);
    CHECK(g.studioOn == 1);
    const ReloadEvent r = FindReload(log, "Riding_RGS_regular.sanim");
    CHECK(r.kind == ReloadKind::Loaded);
    CHECK(r.time == "17:44:53");
    CHECK(FindReload(log, "riding_rgs_regular.sanim").kind == ReloadKind::Loaded);   // Windows names: case-insensitive
    const ReloadEvent b = FindReload(log, "Bad.sanim");
    CHECK(b.kind == ReloadKind::Refused);
    CHECK(b.detail.find("invalid JSON") != std::string::npos);
    CHECK(FindReload(log, "Old.sanim").kind == ReloadKind::KeptPrevious);
    CHECK(FindReload(log, "Locked.sanim").kind == ReloadKind::ReadFailed);
    CHECK(FindReload(log, "Other.sanim").kind == ReloadKind::None);
    CHECK(FindReload(log, "_RGS_regular.sanim").kind == ReloadKind::None);   // whole name only
    // a later session that is still waiting for ExpandedTricks: not hooked yet
    const GameLogInfo w = ParseGameLog("10:00:00.000 ---- AnimStudioNative v0.15 loaded\n10:00:01.000 installation: the slot still holds the engine function\n");
    CHECK(w.found && w.version == 15 && !w.hooked && w.studioOn == -1);
    CHECK(!ParseGameLog("").found);
}

TEST(ws_read_from) {
    const std::wstring d = TempDir(L"log");
    const std::wstring p = d + L"\\AnimStudio.native.log";
    WriteText(p, "line1\nline2\n");
    const unsigned long long s0 = FileSizeOf(p);
    CHECK(s0 == 12);
    std::string out; unsigned long long now = 0;
    WriteText(p, "line1\nline2\nline3\n");
    CHECK(ReadFrom(p, s0, out, &now));
    CHECK(out == "line3\n");
    CHECK(now == 18);
    WriteText(p, "new\n");   // the game restarted: the log is shorter, read it all
    CHECK(ReadFrom(p, now, out, &now));
    CHECK(out == "new\n");
    CHECK(!ReadFrom(d + L"\\missing.log", 0, out, &now));
    RemoveTree(d);
}
