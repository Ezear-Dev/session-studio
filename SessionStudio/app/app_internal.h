// Session Studio -- helpers shared by the App panel files (app_*.cpp). Redesign step 0 (2026-11-26):
// app.cpp was split by panel; these were its file-local helpers, now inline in one namespace.
#pragma once
#include "app.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include "imgui.h"
#include "ImGuizmo.h"
#include "paths.h"
#include "ik.h"
#include "workspace.h"
#include "stage_renderer.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include <ctime>
#include <shellapi.h>
#include <shobjidl.h>

namespace app_detail {

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
inline bool ReadFileText(const std::wstring& path, std::string& out) {
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    return true;
}
// .tmp then rename; the game rereads the clips every second: retry if the file is open at that moment
inline bool WriteFileAtomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfsopen(tmp.c_str(), L"wb", _SH_DENYNO);
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    for (int i = 0; i < 20; i++) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        Sleep(10);
    }
    DeleteFileW(tmp.c_str());
    return false;
}
inline bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline uint64_t FileStamp(const std::wstring& p) {   // modification date + size (0 if missing)
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    const uint64_t t = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    return t ^ (((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow) * 0x9E3779B97F4A7C15ull;
}
inline std::string Short(const std::string& n) { return n.rfind("AMXX_", 0) == 0 ? n.substr(5) : n; }
// UI Phase 4b: labels built outside the UI, in the chosen language
inline std::string VariantTitleUi(const FigureVariant& v) { return VariantTitle(v); }
inline std::string BoneLabelUi(const std::string& n) { return BoneLabel(n); }

// UE component space is left-handed (X forward, Y right, Z up). The 3D view is right-handed: Y is mirrored for display
// (position (x,-y,z), rotation (-qx, qy, -qz, qw)), otherwise the skater would appear mirrored.
inline FTr Disp(const FTr& t) { FTr d = t; d.py = -t.py; d.qx = -t.qx; d.qz = -t.qz; return d; }
inline Quat UndispQ(Quat q) { return Quat{-q.x, q.y, -q.z, q.w}; }

inline const ImU32 kColBone = IM_COL32(230, 230, 235, 255);
inline const ImU32 kColLocked = IM_COL32(95, 95, 105, 255);
inline const ImU32 kColSel = IM_COL32(255, 107, 26, 255);   // theme::Accent
inline const ImU32 kColGhost = IM_COL32(91, 140, 255, 90);   // theme::Ghost
inline const ImU32 kColTrack = IM_COL32(61, 190, 124, 255);   // theme::Good
// redesign step 1: the old names now point at the theme palette (ui_theme.h)
inline const ImVec4 kOk = theme::Good, kWarn = theme::Warn, kDim = theme::Text3;
inline constexpr float kFrame = 1.f / 30.f;

inline const char* StateTitle(uint32_t s) {
    switch (s) {
        case ST_Manual: return "Manual";
        case ST_NoseManual: return "Nose manual";
        case ST_Grind: return "Grind";
        case ST_Powerslide: return "Powerslide";
        case ST_Grab: return "Grab";
        case ST_OnFoot: return "On foot (standing)";
        case ST_Walking: return "Walking";
        case ST_Running: return "Running";
        case ST_Pushing: return "Pushing";
        case ST_Riding: return "Riding (standing)";
        case ST_Crouch: return "Crouch";
        case ST_Landing: return "Landing";
        default: return "Trick";
    }
}
// local direction (bone axes) -> UE component space
inline void Xf(const FTr& t, float x, float y, float z, float out[3]) {
    float v[3] = {x, y, z};
    QRotVec(TrQ(t), v[0], v[1], v[2]);
    out[0] = t.px + v[0]; out[1] = t.py + v[1]; out[2] = t.pz + v[2];
}
// half width of the limb volume drawn for a bone (cm); 0 = none
inline float LimbHalf(const std::string& n) {
    struct W { const char* part; float w; };
    static const W k[] = {{"Thigh", 7.f}, {"Calf", 5.5f}, {"Foot", 4.5f}, {"UpperArm", 5.f}, {"Forearm", 4.f}, {"_Hand", 3.f},
                          {"Spine1", 12.f}, {"Spine2", 12.f}, {"Spine3", 13.f}, {"Pelvis", 13.f}, {"Neck", 4.5f}, {"Head", 9.f}, {"Clavicle", 3.5f}};
    if (n.find("Finger") != std::string::npos || n.find("Xtra") != std::string::npos || n.find("IK_") != std::string::npos) return 0.f;
    for (auto& w : k) if (n.find(w.part) != std::string::npos) return w.w;
    return 0.f;
}


// ------------------------------------------------------------------ layout
inline const ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
inline void Place(float x, float y, float w, float h) { ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h)); }
inline constexpr float kDefLeft = 17.f, kDefRight = 20.f, kDefBottom = 15.f;   // font-size units
inline const ImVec4 kAccent = theme::Accent, kGood = theme::Good, kAlert = theme::Warn, kBad = theme::Bad, kMuted = theme::Text2;
inline ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
// redesign step 1: drawn by the theme widgets (ui_widgets.h)
inline void Badge(const char* text, const ImVec4& col) { ui::Chip(text, col); }
inline void Dot(const ImVec4& col) { ui::StatusDot(col); }
// a panel window whose title uses the title font (Segoe UI Semibold); the content keeps the body font
inline bool BeginPanel(const char* name, ImGuiWindowFlags flags) {
    ImFont* f = theme::Strong();
    if (f) ImGui::PushFont(f, 0.f);
    const bool r = ImGui::Begin(name, nullptr, flags);
    if (f) ImGui::PopFont();
    return r;
}

// ------------------------------------------------------------------ P6 lot 2: limbs (ideas from Skate Style's limb wheel)
inline const char* kLimbBoneName[7] = {"AMXX_L_Hand", "AMXX_R_Hand", "AMXX_L_Foot", "AMXX_R_Foot", "AMXX_Pelvis", "AMXX_Head", "AMXX_Spine2"};
inline const char* kLimbLabel[7] = {"L Hand", "R Hand", "L Foot", "R Foot", "Pelvis", "Head", "Chest"};
inline const char* kLimbLong[7] = {"Left hand", "Right hand", "Left foot", "Right foot", "Pelvis", "Head", "Chest"};
inline const ImU32 kLimbCol[7] = {IM_COL32(54, 197, 240, 220), IM_COL32(54, 197, 240, 220), IM_COL32(95, 211, 138, 220), IM_COL32(95, 211, 138, 220),
                                    IM_COL32(255, 148, 66, 230), IM_COL32(180, 140, 255, 220), IM_COL32(240, 210, 74, 220)};   // limb colours of the spec

} // namespace app_detail
