// UI redesign Phase 2 (2026-11-25): clip file actions of the Library (SessionStudio/model/workspace).
#include "check.h"
#include <windows.h>
#include <cstdio>
#include "../src/clip.h"
#include "../../../SessionStudio/model/workspace.h"

namespace {
std::wstring Tmp(const wchar_t* name) {
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    std::wstring d = std::wstring(t) + L"asn_lib_" + name + L"_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
void Put(const std::wstring& p, const std::string& s) { FILE* f = _wfsopen(p.c_str(), L"wb", _SH_DENYNO); if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); } }
std::string Get(const std::wstring& p) {
    std::string s; FILE* f = _wfsopen(p.c_str(), L"rb", _SH_DENYNO); if (!f) return s;
    char b[4096]; size_t k; while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k); fclose(f); return s;
}
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
void Wipe(const std::wstring& d) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
             if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Wipe(d + L"\\" + n); else DeleteFileW((d + L"\\" + n).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}
// the user's real walking clip of 2026-11-25 (one identity key on the right hand: it changed nothing in game)
const char* kWalk = R"({"format":"sanim","version":1,"name":"Walking_RGS_regular","enabled":true,
 "trigger":{"states":["Walking"],"stances":["normal"],"feet":"regular","priority":0},
 "play":{"drive":"cycle","duration":1.25371289,"loop":true},"blend":{"in":0.25,"out":0.3},"mode":"additive",
 "mirrorForGoofy":false,"tracks":{"AMXX_R_Hand":[{"t":0,"q":[0,-1.86264493e-09,-9.31322464e-10,1],"interp":"smooth"}]},
 "myExtra":{"keep":1}})";
} // namespace

TEST(lib_normalize_name) {
    std::string n = "  mon clip  ", err;
    CHECK(NormalizeClipName(n, err));
    CHECK(n == "mon clip.sanim");
    n = "a.sanim"; CHECK(NormalizeClipName(n, err)); CHECK(n == "a.sanim");
    n = "x."; CHECK(NormalizeClipName(n, err)); CHECK(n == "x.sanim");
    n = ""; CHECK(!NormalizeClipName(n, err)); CHECK(!err.empty());
    n = "a/b"; CHECK(!NormalizeClipName(n, err));
    n = "a:b"; CHECK(!NormalizeClipName(n, err));
    n = "_studio"; CHECK(!NormalizeClipName(n, err));
    n = ".sanim"; CHECK(!NormalizeClipName(n, err));
}

TEST(lib_no_effect) {
    Clip c; std::string err;
    CHECK(ParseClip(kWalk, "w.sanim", c, err));
    CHECK(ClipHasNoEffect(c));
    Clip d = c;
    d.tracks[0].keys[0].q = QFromRotatorDeg(0.f, 0.f, 10.f);
    CHECK(!ClipHasNoEffect(d));
    d.tracks[0].weight = 0.f;   // a weight-0 track does nothing
    CHECK(ClipHasNoEffect(d));
    Clip r = c; r.mode = Mode::Replace;   // replace imposes a pose: never "no effect"
    CHECK(!ClipHasNoEffect(r));
    Clip p = c; p.pelvis.push_back(PosKey{0.f, {0.f, 0.f, -8.f}, true});
    CHECK(!ClipHasNoEffect(p));
    Clip z = c; z.pelvis.push_back(PosKey{0.f, {0.f, 0.02f, 0.f}, true});
    CHECK(ClipHasNoEffect(z));
}

TEST(lib_file_actions) {
    const std::wstring d = Tmp(L"act"), bk = d + L"\\backups";
    const std::wstring a = d + L"\\Walking_RGS_regular.sanim";
    Put(a, kWalk);
    std::string err;
    // enable / disable: only "enabled" changes, unknown fields stay, previous version backed up
    CHECK(SetClipEnabledFile(a, false, bk, err));
    Clip c;
    CHECK(ParseClip(Get(a), "a", c, err));
    CHECK(!c.enabled);
    CHECK(c.name == "Walking_RGS_regular");
    CHECK(c.tracks.size() == 1 && c.states == ST_Walking);
    CHECK(Get(a).find("myExtra") != std::string::npos);
    CHECK(ListBackups(bk, "Walking_RGS_regular").size() == 1);
    CHECK(SetClipEnabledFile(a, true, bk, err));
    ParseClip(Get(a), "a", c, err);
    CHECK(c.enabled);
    // a broken file is refused, not rewritten
    const std::wstring broken = d + L"\\Broken.sanim";
    Put(broken, "{ nope");
    CHECK(!SetClipEnabledFile(broken, false, bk, err));
    CHECK(Get(broken) == "{ nope");
    // unique names
    CHECK(UniqueClipName(d, "Walking_RGS_regular", "_copie") == "Walking_RGS_regular_copie.sanim");
    // duplicate: disabled, named after its file
    const std::wstring dup = d + L"\\Walking_RGS_regular_copie.sanim";
    CHECK(DuplicateClipFile(a, dup, err));
    Clip k;
    CHECK(ParseClip(Get(dup), "dup", k, err));
    CHECK(!k.enabled);
    CHECK(k.name == "Walking_RGS_regular_copie");
    CHECK(k.tracks.size() == 1);
    CHECK(UniqueClipName(d, "Walking_RGS_regular", "_copie") == "Walking_RGS_regular_copie2.sanim");
    CHECK(!DuplicateClipFile(a, dup, err));   // never replaces a file
    // rename: refuses to replace, keeps content
    const std::wstring ren = d + L"\\Marche.sanim";
    CHECK(!RenameClipFile(a, dup, err));
    CHECK(RenameClipFile(a, ren, err));
    CHECK(!Exists(a) && Exists(ren));
    CHECK(Get(ren).find("Walking_RGS_regular") != std::string::npos);
    // delete = move to the backups (recoverable)
    std::wstring dest;
    CHECK(MoveToBackups(ren, bk, "supprime", "20261125_183800", &dest, err));
    CHECK(!Exists(ren));
    CHECK(Exists(dest));
    CHECK(dest.find(L"Marche_supprime_20261125_183800.sanim") != std::wstring::npos);
    CHECK(!MoveToBackups(ren, bk, "supprime", "20261125_183801", &dest, err));   // already gone
    Wipe(d);
}
