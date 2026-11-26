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
inline const ImU32 kColSel = IM_COL32(255, 160, 40, 255);
inline const ImU32 kColGhost = IM_COL32(120, 170, 255, 90);
inline const ImU32 kColTrack = IM_COL32(120, 220, 140, 255);
inline const ImVec4 kOk(0.45f, 0.85f, 0.5f, 1.f), kWarn(1.f, 0.6f, 0.35f, 1.f), kDim(0.6f, 0.6f, 0.65f, 1.f);
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
inline const ImVec4 kAccent(0.22f, 0.52f, 0.95f, 1.f), kGood(0.40f, 0.80f, 0.48f, 1.f), kAlert(1.f, 0.62f, 0.28f, 1.f), kBad(1.f, 0.42f, 0.38f, 1.f), kMuted(0.62f, 0.63f, 0.68f, 1.f);
inline ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
// a coloured pill with a label, laid out like a text item (hover works with IsItemHovered)
inline void Badge(const char* text, const ImVec4& col) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float px = ImGui::GetFontSize() * 0.5f, py = ImGui::GetStyle().FramePadding.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + px * 2.f, ts.y + py * 2.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), U32(ImVec4(col.x, col.y, col.z, 0.22f)), sz.y * 0.5f);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), U32(ImVec4(col.x, col.y, col.z, 0.85f)), sz.y * 0.5f);
    dl->AddText(ImVec2(p.x + px, p.y + py), U32(col), text);
    ImGui::Dummy(sz);
}
// a dot then a text (status strip item)
inline void Dot(const ImVec4& col) {
    const float r = ImGui::GetFontSize() * 0.22f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, U32(col));
    ImGui::Dummy(ImVec2(r * 2.f, h));
    ImGui::SameLine(0.f, r * 1.5f);
}

// ------------------------------------------------------------------ P6 lot 2: limbs (ideas from Skate Style's limb wheel)
inline const char* kLimbBoneName[7] = {"AMXX_L_Hand", "AMXX_R_Hand", "AMXX_L_Foot", "AMXX_R_Foot", "AMXX_Pelvis", "AMXX_Head", "AMXX_Spine2"};
inline const char* kLimbLabel[7] = {"L Hand", "R Hand", "L Foot", "R Foot", "Pelvis", "Head", "Chest"};
inline const char* kLimbLong[7] = {"Left hand", "Right hand", "Left foot", "Right foot", "Pelvis", "Head", "Chest"};
inline const ImU32 kLimbCol[7] = {IM_COL32(90, 200, 255, 200), IM_COL32(90, 200, 255, 200), IM_COL32(120, 230, 120, 200), IM_COL32(120, 230, 120, 200),
                                    IM_COL32(255, 150, 60, 220), IM_COL32(200, 140, 255, 200), IM_COL32(240, 220, 90, 200)};

} // namespace app_detail
