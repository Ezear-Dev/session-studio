// Session Studio -- the visual theme (redesign step 1, 2026-11-26).
#include "ui_theme.h"
#include <windows.h>

namespace theme {
namespace {

ImFont* g_body = nullptr;
ImFont* g_strong = nullptr;
std::string g_iconFile;
int g_hasIcons = -1;   // -1 not checked yet

const Icon kAll[] = {Icon::Play, Icon::Pause, Icon::ToStart, Icon::ToEnd, Icon::Prev, Icon::Next, Icon::Loop, Icon::Record,
                     Icon::Save, Icon::Undo, Icon::Redo, Icon::Settings, Icon::Help, Icon::Add, Icon::Delete, Icon::Search,
                     Icon::Close, Icon::More, Icon::Open, Icon::Folder, Icon::View, Icon::Down, Icon::Copy, Icon::Paste,
                     Icon::Warning, Icon::Check, Icon::Lock, Icon::Camera, Icon::ZoomIn, Icon::ZoomOut, Icon::Edit};

bool Exists(const char* p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

// merges the Windows icon font into the font just added (icons only: the Private Use Area)
void MergeIcons(float sizePx) {
    if (g_iconFile.empty()) return;
    static const ImWchar kRange[] = {0xE700, 0xF8FF, 0};
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.GlyphMinAdvanceX = sizePx;            // icons take the same width: rows of icons line up
    cfg.GlyphOffset = ImVec2(0.f, sizePx * 0.12f);   // the icon font sits high next to Segoe UI
    ImGui::GetIO().Fonts->AddFontFromFileTTF(g_iconFile.c_str(), sizePx * 0.95f, &cfg, kRange);
}

} // namespace

void LoadFonts(float sizePx) {
    ImGuiIO& io = ImGui::GetIO();
    for (const char* f : {"C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf"})
        if (Exists(f)) { g_iconFile = f; break; }
    if (Exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        g_body = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", sizePx);
        MergeIcons(sizePx);
    } else {
        g_body = io.Fonts->AddFontDefault();
        g_iconFile.clear();                    // no icons on ImGui's own font: text labels
    }
    if (g_body && Exists("C:\\Windows\\Fonts\\seguisb.ttf") && !g_iconFile.empty()) {
        g_strong = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", sizePx);
        MergeIcons(sizePx);
    }
    io.FontDefault = g_body;
}

ImFont* Strong() { return g_strong; }

bool HasIcons() {
    if (g_hasIcons < 0) {
        bool all = g_body && !g_iconFile.empty();
        for (Icon i : kAll) if (all && !g_body->IsGlyphInFont((ImWchar)i)) all = false;
        g_hasIcons = all ? 1 : 0;
    }
    return g_hasIcons == 1;
}

FontReport CheckFonts() {
    FontReport r;
    r.body = g_body != nullptr && Exists("C:\\Windows\\Fonts\\segoeui.ttf");
    r.strong = g_strong != nullptr;
    r.iconFile = g_iconFile;
    r.iconsTotal = (int)(sizeof(kAll) / sizeof(kAll[0]));
    if (g_body && !g_iconFile.empty()) for (Icon i : kAll) if (g_body->IsGlyphInFont((ImWchar)i)) r.iconsFound++;
    return r;
}

std::string Glyph(Icon i) {
    const unsigned c = (unsigned)i;   // all icons are in U+E000-U+FFFF: three UTF-8 bytes
    std::string s;
    s += (char)(0xE0 | (c >> 12));
    s += (char)(0x80 | ((c >> 6) & 0x3F));
    s += (char)(0x80 | (c & 0x3F));
    return s;
}

void ApplyStyle(float dpiScale) {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);   // every colour has a value; ours override them below
    s.WindowPadding = ImVec2(10, 10);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 4);
    s.IndentSpacing = 16;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.TabBarOverlineSize = 2;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(12, 4);
    s.WindowRounding = 8;
    s.ChildRounding = 6;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 6;
    s.TabRounding = 6;
    s.WindowTitleAlign = ImVec2(0.f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.DisabledAlpha = 0.45f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = Text3;
    c[ImGuiCol_WindowBg] = Bg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = Surface;
    c[ImGuiCol_Border] = Line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = Surface;
    c[ImGuiCol_FrameBgHovered] = Raised;
    c[ImGuiCol_FrameBgActive] = Line;
    c[ImGuiCol_TitleBg] = Bg1;
    c[ImGuiCol_TitleBgActive] = Bg1;
    c[ImGuiCol_TitleBgCollapsed] = Bg1;
    c[ImGuiCol_MenuBarBg] = Bg1;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = Line;
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.227f, 0.251f, 0.298f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.290f, 0.318f, 0.376f, 1.f);
    c[ImGuiCol_CheckMark] = Accent;
    c[ImGuiCol_SliderGrab] = Accent;
    c[ImGuiCol_SliderGrabActive] = AccentHi;
    c[ImGuiCol_Button] = Surface;
    c[ImGuiCol_ButtonHovered] = Raised;
    c[ImGuiCol_ButtonActive] = Line;
    c[ImGuiCol_Header] = Alpha(Accent, 0.16f);
    c[ImGuiCol_HeaderHovered] = Raised;
    c[ImGuiCol_HeaderActive] = Alpha(Accent, 0.26f);
    c[ImGuiCol_Separator] = Line;
    c[ImGuiCol_SeparatorHovered] = Alpha(Accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = Accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = Alpha(Accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = Accent;
    c[ImGuiCol_InputTextCursor] = Accent;
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = Raised;
    c[ImGuiCol_TabSelected] = Surface;
    c[ImGuiCol_TabSelectedOverline] = Accent;
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = Surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = Alpha(Accent, 0.5f);
    c[ImGuiCol_TableHeaderBg] = Surface;
    c[ImGuiCol_TableBorderStrong] = Line;
    c[ImGuiCol_TableBorderLight] = Alpha(Line, 0.6f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextLink] = AccentHi;
    c[ImGuiCol_TextSelectedBg] = Alpha(Accent, 0.35f);
    c[ImGuiCol_TreeLines] = Line;
    c[ImGuiCol_DragDropTarget] = Accent;
    c[ImGuiCol_NavCursor] = Accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.55f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.f, 0.f, 0.f, 0.45f);

    s.ScaleAllSizes(dpiScale);
    s.FontScaleDpi = dpiScale;
}

} // namespace theme
