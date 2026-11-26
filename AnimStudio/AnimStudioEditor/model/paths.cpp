#include "paths.h"
#include "workspace.h"
#include <windows.h>
#include <cstdio>

namespace {
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
bool ReadText(const std::wstring& path, std::string& out) {
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t k;
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    return true;
}
} // namespace

bool DirExists(const std::wstring& dir) {
    const DWORD a = GetFileAttributesW(dir.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> SteamLibraryPaths(const std::string& vdf) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while ((pos = vdf.find("\"path\"", pos)) != std::string::npos) {
        pos += 6;
        const size_t a = vdf.find('"', pos);
        if (a == std::string::npos) break;
        std::string v;
        size_t i = a + 1;
        for (; i < vdf.size() && vdf[i] != '"'; i++) {
            if (vdf[i] == '\\' && i + 1 < vdf.size()) { v += vdf[i + 1]; i++; }
            else v += vdf[i];
        }
        pos = i + 1;
        if (!v.empty()) out.push_back(Widen(v));
    }
    return out;
}

std::wstring ClipsDirIn(const std::wstring& lib) {
    return lib + L"\\steamapps\\common\\Session\\SessionGame\\Binaries\\Win64\\Mods\\AnimStudio\\clips";
}

std::wstring FindClipsDir(const std::wstring& settingsFile) {
    std::string text;
    if (ReadText(settingsFile, text)) {
        const size_t p = text.find("clipsDir=");
        if (p != std::string::npos) {
            std::string v = text.substr(p + 9);
            const size_t e = v.find_first_of("\r\n");
            if (e != std::string::npos) v = v.substr(0, e);
            const std::wstring d = Widen(v);
            if (DirExists(d)) return d;
        }
    }
    std::vector<std::wstring> libs;
    wchar_t steam[MAX_PATH] = {};
    DWORD size = sizeof(steam);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) == ERROR_SUCCESS) {
        std::wstring s = steam;
        for (auto& ch : s) if (ch == L'/') ch = L'\\';
        libs.push_back(s);
        std::string vdf;
        if (ReadText(s + L"\\steamapps\\libraryfolders.vdf", vdf)) for (auto& l : SteamLibraryPaths(vdf)) libs.push_back(l);
    }
    libs.push_back(L"C:\\Program Files (x86)\\Steam");
    for (auto& l : libs) {
        const std::wstring d = ClipsDirIn(l);
        if (DirExists(d)) return d;
    }
    return L"";
}

// UI Phase 1: the settings file also holds the layout, so only this key is replaced
bool SaveClipsDir(const std::wstring& settingsFile, const std::wstring& dir) {
    return SaveSetting(settingsFile, "clipsDir", Narrow(dir));
}
