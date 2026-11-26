// Session Studio -- the visual theme (redesign step 1, 2026-11-26): palette, ImGui style, fonts and icons.
// One place for every colour of the editor. Spec: notes/REDESIGN-PROPOSAL.md §6 (local).
#pragma once
#include <string>
#include "imgui.h"

namespace theme {

// ---- palette ("night session": dark anthracite, skate orange for what is active or primary)
inline const ImVec4 Bg0(0.043f, 0.051f, 0.067f, 1.f);        // #0B0D11 window background, gaps between panels
inline const ImVec4 Bg1(0.071f, 0.082f, 0.106f, 1.f);        // #12151B panels, bars
inline const ImVec4 Stage(0.082f, 0.094f, 0.118f, 1.f);      // #15181E 3D view background
inline const ImVec4 Surface(0.106f, 0.122f, 0.153f, 1.f);    // #1B1F27 fields, buttons, cards, popups
inline const ImVec4 Raised(0.141f, 0.161f, 0.204f, 1.f);     // #242934 hover
inline const ImVec4 Line(0.173f, 0.196f, 0.239f, 1.f);       // #2C323D 1 px lines, pressed
inline const ImVec4 Text(0.925f, 0.914f, 0.894f, 1.f);       // #ECE9E4 main text (off-white)
inline const ImVec4 Text2(0.557f, 0.584f, 0.631f, 1.f);      // #8E95A1 secondary text
inline const ImVec4 Text3(0.369f, 0.396f, 0.447f, 1.f);      // #5E6572 hints, disabled
inline const ImVec4 Accent(1.f, 0.420f, 0.102f, 1.f);        // #FF6B1A primary action, selection, active tool, playhead
inline const ImVec4 AccentHi(1.f, 0.580f, 0.259f, 1.f);      // #FF9442 hover / orange text
inline const ImVec4 AccentLo(0.851f, 0.384f, 0.043f, 1.f);   // #D9620B pressed
inline const ImVec4 OnAccent(0.114f, 0.051f, 0.008f, 1.f);   // #1D0D02 text on orange
inline const ImVec4 Good(0.239f, 0.745f, 0.486f, 1.f);       // #3DBE7C recorded, saved, reloaded
inline const ImVec4 Warn(0.949f, 0.702f, 0.239f, 1.f);       // #F2B33D warnings
inline const ImVec4 Bad(0.898f, 0.325f, 0.294f, 1.f);        // #E5534B errors
inline const ImVec4 Ghost(0.357f, 0.549f, 1.f, 1.f);         // #5B8CFF what comes from the game (ghost, game row, phases)

inline ImVec4 Alpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }
inline ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImU32 U32(const ImVec4& c, float a) { return ImGui::ColorConvertFloat4ToU32(Alpha(c, a)); }

// ---- fonts
// Segoe UI (body) and Segoe UI Semibold (titles), from C:\Windows\Fonts; the Windows icon font (Segoe Fluent Icons,
// else Segoe MDL2 Assets) is merged into both. Nothing is shipped with the editor. Missing fonts: ImGui's own font,
// and icon buttons show their text label instead.
struct FontReport { bool body = false, strong = false; std::string iconFile; int iconsFound = 0, iconsTotal = 0; };
void LoadFonts(float sizePx);
void ApplyStyle(float dpiScale);
ImFont* Strong();                         // titles; nullptr = the default font
FontReport CheckFonts();                  // after LoadFonts: which fonts loaded, how many icons the icon font has

// ---- icons (Windows icon font code points, the same in Segoe Fluent Icons and Segoe MDL2 Assets)
enum class Icon : unsigned {
    Play = 0xE768, Pause = 0xE769, ToStart = 0xE892, ToEnd = 0xE893, Prev = 0xE76B, Next = 0xE76C, Loop = 0xE8EE,
    Record = 0xE7C8, Save = 0xE74E, Undo = 0xE7A7, Redo = 0xE7A6, Settings = 0xE713, Help = 0xE897, Add = 0xE710,
    Delete = 0xE74D, Search = 0xE721, Close = 0xE711, More = 0xE712, Open = 0xE8E5, Folder = 0xE8B7, View = 0xE890,
    Down = 0xE70D, Copy = 0xE8C8, Paste = 0xE77F, Warning = 0xE7BA, Check = 0xE73E, Lock = 0xE72E, Camera = 0xE722,
    ZoomIn = 0xE8A3, ZoomOut = 0xE71F, Edit = 0xE70F,
};
bool HasIcons();                          // every icon above is in the merged font
std::string Glyph(Icon i);                // the icon as UTF-8 text

} // namespace theme
