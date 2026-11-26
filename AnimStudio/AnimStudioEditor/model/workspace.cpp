#include "workspace.h"
#include "../../AnimStudioNative/src/clip.h"
#include <cmath>
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
bool ReadAll(const std::wstring& path, std::string& out) {
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    return true;
}
bool WriteAll(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfsopen(tmp.c_str(), L"wb", _SH_DENYNO);
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}
std::string LowerA(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }
std::wstring LowerW(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
std::wstring Stem(const std::wstring& path) {
    size_t a = path.find_last_of(L"\\/");
    std::wstring n = a == std::wstring::npos ? path : path.substr(a + 1);
    const size_t dot = n.find_last_of(L'.');
    return dot == std::wstring::npos ? n : n.substr(0, dot);
}
// backups are named <stem>_<YYYYMMDD_HHMMSS>[_<n>].sanim: oldest first by stamp, then by copy number
void SortBackups(std::vector<std::wstring>& v) {
    auto key = [](const std::wstring& path) {
        const std::wstring s = Stem(path);
        // the LAST "_YYYYMMDD_HHMMSS" in the name (the clip's own name may contain digits too)
        for (size_t i = s.size() >= 16 ? s.size() - 16 : 0;; i--) {
            bool ok = i + 16 <= s.size() && s[i] == L'_' && s[i + 9] == L'_';
            for (size_t k = 1; ok && k <= 8; k++) ok = iswdigit(s[i + k]) != 0;
            for (size_t k = 10; ok && k <= 15; k++) ok = iswdigit(s[i + k]) != 0;
            if (ok && (i + 16 == s.size() || s[i + 16] == L'_')) {
                const int n = i + 16 < s.size() ? _wtoi(s.c_str() + i + 17) : 1;
                return std::make_pair(s.substr(i + 1, 15), n);
            }
            if (i == 0) break;
        }
        return std::make_pair(s, 0);
    };
    std::sort(v.begin(), v.end(), [&](const std::wstring& a, const std::wstring& b) { return key(a) < key(b); });
}
} // namespace

// ------------------------------------------------------------------ settings
std::string Settings::Get(const std::string& key, const std::string& def) const {
    for (auto& kv : items) if (kv.first == key) return kv.second;
    return def;
}
float Settings::GetFloat(const std::string& key, float def) const {
    const std::string v = Get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    return (end && *end == 0) ? f : def;
}
void Settings::Set(const std::string& key, const std::string& value) {
    for (auto& kv : items) if (kv.first == key) { kv.second = value; return; }
    items.push_back({key, value});
}
void Settings::SetFloat(const std::string& key, float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    Set(key, b);
}
Settings ParseSettings(const std::string& text) {
    Settings s;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(i, e - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0 && line[0] != '#' && line[0] != ';') s.items.push_back({Trim(line.substr(0, eq)), line.substr(eq + 1)});
        else if (!line.empty()) s.items.push_back({"", line});   // comment or unknown line: kept verbatim
        i = e + 1;
    }
    return s;
}
std::string SettingsText(const Settings& s) {
    std::string t;
    for (auto& kv : s.items) t += (kv.first.empty() ? kv.second : kv.first + "=" + kv.second) + "\n";
    return t;
}
bool LoadSettings(const std::wstring& path, Settings& out) {
    out = Settings{};
    std::string text;
    if (!ReadAll(path, text)) return false;
    out = ParseSettings(text);
    return true;
}
bool SaveSettings(const std::wstring& path, const Settings& s) { return WriteAll(path, SettingsText(s)); }
bool SaveSetting(const std::wstring& path, const std::string& key, const std::string& value) {
    Settings s;
    LoadSettings(path, s);
    s.Set(key, value);
    return SaveSettings(path, s);
}

// ------------------------------------------------------------------ backups
std::wstring DefaultBackupRoot() {
    PWSTR docs = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) r = std::wstring(docs) + L"\\AnimStudio\\backups";
    if (docs) CoTaskMemFree(docs);
    return r;
}
std::string BackupStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char b[32];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &tm);
    return b;
}
std::vector<std::wstring> ListBackups(const std::wstring& root, const std::string& stem) {
    std::vector<std::wstring> v;
    const std::wstring dir = root + L"\\" + Widen(stem);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.sanim").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) v.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    SortBackups(v);
    return v;
}
bool BackupClip(const std::wstring& clipPath, const std::wstring& root, const std::string& stamp, int keep,
                std::wstring* written, std::string& err) {
    if (written) written->clear();
    if (!Exists(clipPath)) return true;
    if (root.empty()) { err = "backups folder unknown"; return false; }
    const std::wstring stem = Stem(clipPath);
    const std::wstring dir = root + L"\\" + stem;
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);   // creates the parents too
    if (!Exists(dir)) { err = "cannot create the backups folder"; return false; }
    std::wstring dst = dir + L"\\" + stem + L"_" + Widen(stamp) + L".sanim";
    for (int i = 2; Exists(dst) && i < 100; i++) dst = dir + L"\\" + stem + L"_" + Widen(stamp) + L"_" + std::to_wstring(i) + L".sanim";
    if (!CopyFileW(clipPath.c_str(), dst.c_str(), TRUE)) { err = "cannot copy the previous version"; return false; }
    if (written) *written = dst;
    if (keep > 0) {
        std::vector<std::wstring> all;
        {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\*.sanim").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) all.push_back(dir + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        SortBackups(all);
        while ((int)all.size() > keep) { DeleteFileW(all.front().c_str()); all.erase(all.begin()); }
    }
    return true;
}

// ------------------------------------------------------------------ game folder and log
std::wstring GameBinDirFromClips(const std::wstring& clipsDir) {
    std::wstring d = clipsDir;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    const std::wstring tail = L"\\mods\\animstudio\\clips";
    if (d.size() <= tail.size()) return L"";
    std::wstring low = LowerW(d);
    for (auto& c : low) if (c == L'/') c = L'\\';
    if (low.compare(low.size() - tail.size(), tail.size(), tail) != 0) return L"";
    return d.substr(0, d.size() - tail.size());
}

bool IsGameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do { if (_wcsicmp(pe.szExeFile, L"SessionGame-Win64-Shipping.exe") == 0) { found = true; break; } } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

ModInstall CheckModInstall(const std::wstring& bin) {
    ModInstall m;
    if (bin.empty()) return m;
    m.native = Exists(bin + L"\\Mods\\AnimStudioNative\\dlls\\main.dll") && Exists(bin + L"\\Mods\\AnimStudioNative\\enabled.txt");
    m.lua = Exists(bin + L"\\Mods\\AnimStudio\\Scripts\\main.lua") && Exists(bin + L"\\Mods\\AnimStudio\\enabled.txt");
    m.ue4ss = Exists(bin + L"\\UE4SS.dll");
    return m;
}

GameLogInfo ParseGameLog(const std::string& text) {
    GameLogInfo g;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(i, e - i);
        i = e + 1;
        size_t p;
        if ((p = line.find("---- AnimStudioNative v0.")) != std::string::npos) {
            g = GameLogInfo{};   // a new session header: only the last session counts
            g.found = true;
            g.version = std::atoi(line.c_str() + p + 25);
        } else if ((p = line.find("installation: ")) != std::string::npos) {
            g.lastInstallLine = Trim(line.substr(p + 14));
            if (line.find("in the slot") != std::string::npos && line.find("refused") == std::string::npos) { g.hooked = true; g.refused = false; }
            else if (line.find("refused") != std::string::npos) g.refused = true;
        } else if (line.find(" studio ON") != std::string::npos) g.studioOn = 1;
        else if (line.find(" studio OFF") != std::string::npos) g.studioOn = 0;
    }
    return g;
}

ReloadEvent FindReload(const std::string& text, const std::string& file) {
    ReloadEvent r;
    const std::string want = LowerA("clips: " + file + ": ");
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(i, e - i);
        i = e + 1;
        const std::string low = LowerA(line);
        const size_t p = low.find(want);
        if (p == std::string::npos) continue;
        // whole file name only: "clips: " must start the message (after the time stamp)
        if (p == 0 || line[p - 1] != ' ') continue;
        const std::string rest = Trim(line.substr(p + want.size()));
        ReloadEvent ev;
        ev.time = line.size() >= 8 ? line.substr(0, 8) : "";
        ev.detail = rest;
        if (rest.rfind("loaded", 0) == 0) ev.kind = ReloadKind::Loaded;
        else if (rest.rfind("removed", 0) == 0) ev.kind = ReloadKind::Removed;
        else if (rest.find("cannot read") != std::string::npos) ev.kind = ReloadKind::ReadFailed;
        else if (rest.find("keeping the previous version") != std::string::npos) ev.kind = ReloadKind::KeptPrevious;
        else if (rest.find("refused") != std::string::npos) ev.kind = ReloadKind::Refused;
        else continue;
        r = ev;   // keep the last one
    }
    return r;
}

unsigned long long FileSizeOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

bool ReadFrom(const std::wstring& path, unsigned long long from, std::string& out, unsigned long long* sizeNow) {
    out.clear();
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    if (size < 0) { fclose(f); return false; }
    if ((unsigned long long)size < from) from = 0;   // the game restarted and rewrote the log
    _fseeki64(f, (long long)from, SEEK_SET);
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    if (sizeNow) *sizeNow = (unsigned long long)size;
    return true;
}

// ------------------------------------------------------------------ clip files (UI Phase 2: Library)

bool NormalizeClipName(std::string& name, std::string& err) {
    std::string n = Trim(name);
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    if (n.size() >= 6 && LowerA(n.substr(n.size() - 6)) == ".sanim") n = Trim(n.substr(0, n.size() - 6));
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    if (n.empty()) { err = "give the file a name"; return false; }
    if (n.find_first_of("\\/:*?\"<>|") != std::string::npos) { err = "invalid file name (no \\ / : * ? \" < > |)"; return false; }
    if (n[0] == '_') { err = "a clip name does not start with \"_\" (reserved for the studio)"; return false; }
    name = n + ".sanim";
    return true;
}

std::string UniqueClipName(const std::wstring& dir, const std::string& stem, const std::string& suffix) {
    for (int i = 1; i < 1000; i++) {
        const std::string n = stem + suffix + (i == 1 ? std::string() : std::to_string(i)) + ".sanim";
        if (!Exists(dir + L"\\" + Widen(n))) return n;
    }
    return stem + suffix + "_x.sanim";
}

bool MoveToBackups(const std::wstring& path, const std::wstring& root, const std::string& tag, const std::string& stamp,
                   std::wstring* dest, std::string& err) {
    if (!Exists(path)) { err = "file not found"; return false; }
    if (root.empty()) { err = "backups folder unknown"; return false; }
    const std::wstring stem = Stem(path);
    const std::wstring dir = root + L"\\" + stem;
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring dst = dir + L"\\" + stem + L"_" + Widen(tag) + L"_" + Widen(stamp) + L".sanim";
    for (int i = 2; Exists(dst) && i < 100; i++) dst = dir + L"\\" + stem + L"_" + Widen(tag) + L"_" + Widen(stamp) + L"_" + std::to_wstring(i) + L".sanim";
    // copy then delete (MoveFile across drives works too, but a copy failure must leave the original in place)
    if (!CopyFileW(path.c_str(), dst.c_str(), TRUE)) { err = "cannot copy to the backups"; return false; }
    if (!DeleteFileW(path.c_str())) { DeleteFileW(dst.c_str()); err = "the file is used by another program"; return false; }
    if (dest) *dest = dst;
    return true;
}

namespace {
std::string StemUtf8(const std::wstring& path);
bool LoadClipFile(const std::wstring& path, Clip& c, std::string& err) {
    std::string text;
    if (!ReadAll(path, text)) { err = "cannot read"; return false; }
    std::string n;
    n = StemUtf8(path) + ".sanim";
    if (!ParseClip(text, n, c, err)) { err = "unreadable file: " + err; return false; }
    return true;
}
std::string StemUtf8(const std::wstring& path) {
    const std::wstring s = Stem(path);
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), o.data(), n, nullptr, nullptr);
    return o;
}
} // namespace

bool SetClipEnabledFile(const std::wstring& path, bool enabled, const std::wstring& backupRoot, std::string& err) {
    Clip c;
    if (!LoadClipFile(path, c, err)) return false;
    if (c.enabled == enabled) return true;
    if (!BackupClip(path, backupRoot, BackupStamp(), 20, nullptr, err)) return false;
    c.enabled = enabled;
    if (!WriteAll(path, ClipToJson(c))) { err = "cannot write (file open elsewhere?)"; return false; }
    return true;
}

bool DuplicateClipFile(const std::wstring& src, const std::wstring& dst, std::string& err) {
    if (Exists(dst)) { err = "a file already has this name"; return false; }
    Clip c;
    if (!LoadClipFile(src, c, err)) return false;
    c.enabled = false;
    c.name = StemUtf8(dst);
    if (!WriteAll(dst, ClipToJson(c))) { err = "cannot write"; return false; }
    return true;
}

bool RenameClipFile(const std::wstring& from, const std::wstring& to, std::string& err) {
    if (!Exists(from)) { err = "file not found"; return false; }
    // same name with another case: allowed (Windows sees one file)
    if (Exists(to) && LowerW(from) != LowerW(to)) { err = "a file already has this name"; return false; }
    if (!MoveFileExW(from.c_str(), to.c_str(), 0)) { err = "cannot rename (file open elsewhere?)"; return false; }
    return true;
}

bool ClipHasNoEffect(const Clip& c) {
    if (c.mode == Mode::Replace) return false;
    const float minW = 0.99999f;   // |w| of a rotation below ~0.5 deg
    for (auto& tr : c.tracks) {
        if (tr.weight <= 0.001f) continue;
        for (auto& k : tr.keys) if (std::fabs(k.q.w) < minW) return false;
    }
    for (auto& p : c.pelvis) if (std::fabs(p.v[0]) > 0.1f || std::fabs(p.v[1]) > 0.1f || std::fabs(p.v[2]) > 0.1f) return false;
    return true;
}
