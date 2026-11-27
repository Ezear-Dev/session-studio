// Session Studio -- workspace shell: layout, splitters, top bar, game pill, welcome window, game polling, messages.
#include "app_internal.h"

using namespace app_detail;


App::Layout App::Lay() const {
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    Layout L{};
    L.w = d.x; L.h = d.y;
    L.top = std::round(fs * 2.75f);
    L.gap = std::max(4.f, std::round(fs * 0.35f));
    // the content area: an outer margin of one gap around the panels (the window background shows between them)
    const float x0 = L.gap, x1 = std::max(x0 + 1.f, L.w - L.gap), y0 = L.top, y1 = std::max(y0 + 1.f, L.h - L.gap);
    const float cw = x1 - x0, ch = y1 - y0;
    if (ws_ == 0) {   // Browse: the Library on the whole content area
        L.lib = {x0, y0, cw, ch};
        L.left = L.right = L.bottom = 0.f;
        return L;
    }
    // Animate. Timeline: 38 % of the height until the user drags its edge; at least 7 font sizes, the stage keeps 12
    const float maxB = std::max(fs * 7.f, ch - L.gap - fs * 12.f);
    const float wantB = bottomDefault_ ? ch * 0.38f : bottomU_ * fs;
    L.bottom = std::clamp(wantB, fs * 7.f, maxB);
    const float midH = std::max(1.f, ch - L.bottom - L.gap);
    // redesign step 4: the stage takes the whole width; the inspector card (L.right wide) lives inside it, at its right
    L.right = std::clamp(rightU_ * fs, fs * 14.f, std::max(fs * 14.f, cw * 0.4f));
    L.stage = {x0, y0, cw, midH};
    L.time = {x0, y1 - L.bottom, cw, L.bottom};
    // the Library drawer, over the left part of the stage
    L.left = std::clamp(leftU_ * fs, fs * 14.f, std::max(fs * 14.f, L.stage.w * 0.6f));
    L.lib = {x0, y0, std::min(L.left, L.stage.w), midH};
    return L;
}

void App::SaveLayout() {
    Settings st;
    LoadSettings(settings_, st);
    st.SetFloat("layout.left", leftU_);
    st.SetFloat("layout.right", rightU_);
    if (bottomDefault_) st.Set("layout.bottom", "");   // the default: 38 % of the height, whatever the window size
    else st.SetFloat("layout.bottom", bottomU_);
    SaveSettings(settings_, st);
}

void App::Splitters() {
    if (ws_ != 1) return;   // Browse has no splitter
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    struct S { const char* id; float x, y, w, h; int kind; };   // kind 0 drawer edge, 1 inspector edge, 2 timeline edge
    const S ss[2] = {{"##split_gauche", L.lib.x + L.lib.w, L.lib.y, L.gap, L.lib.h, 0},
                     {"##split_bas", L.time.x, L.time.y - L.gap, L.time.w, L.gap, 2}};
    for (const S& s : ss) {
        if (s.kind == 0 && !drawer_) continue;
        Place(s.x, s.y, s.w, s.h);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        ImGui::Begin(s.id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::InvisibleButton("##poignee", ImVec2(s.w, s.h));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (hov || act) ImGui::SetMouseCursor(s.kind == 2 ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (hov && !act) ImGui::SetTooltip("Drag to resize · double-click: default size");
        if (act) {
            const ImVec2 md = ImGui::GetIO().MouseDelta;
            if (s.kind == 0) leftU_ = std::max(1.f, L.left + md.x) / fs;
            else if (s.kind == 1) rightU_ = std::max(1.f, L.right - md.x) / fs;
            else { bottomU_ = std::max(1.f, L.bottom - md.y) / fs; bottomDefault_ = false; }
        }
        if (ImGui::IsItemDeactivated()) {
            // store what is really shown (the clamped size), not a value beyond the limits
            const Layout N = Lay();
            if (s.kind == 0) leftU_ = N.left / fs; else if (s.kind == 1) rightU_ = N.right / fs; else bottomU_ = N.bottom / fs;
            SaveLayout();
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (s.kind == 0) leftU_ = kDefLeft; else if (s.kind == 1) rightU_ = kDefRight; else { bottomU_ = kDefBottom; bottomDefault_ = true; }
            SaveLayout();
        }
        const ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
        const ImU32 col = act ? U32(kAccent) : hov ? U32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f)) : IM_COL32(0, 0, 0, 0);   // idle: the gap between panels shows the window background
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (s.kind == 2) dl->AddLine(ImVec2(p.x, (p.y + q.y) * 0.5f), ImVec2(q.x, (p.y + q.y) * 0.5f), col, act || hov ? 3.f : 1.f);
        else dl->AddLine(ImVec2((p.x + q.x) * 0.5f, p.y), ImVec2((p.x + q.x) * 0.5f, q.y), col, act || hov ? 3.f : 1.f);
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}

// redesign step 2 (2026-11-27): one light top bar. Left: logo, workspace, Library drawer. Centre: what is edited
// (variant, clip). Right: save state, game pill, Save, undo / redo, ⋯ menu. New / Open / Save as / Help: ⋯ menu.
// Auto key moved to the timeline transport; the status strip became the game pill and its popup.
void App::HeaderBar() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    Place(0, 0, L.w, L.top);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("##haut", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    const float y = std::round((L.top - h) * 0.5f);
    ImGui::SetCursorPosY(y);

    // ---- left: logo mark (orange rounded square, drawn), the name in the title font, the workspaces
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float m = std::round(h * 0.1f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x + m, p.y + m), ImVec2(p.x + h - m, p.y + h - m), theme::U32(theme::Accent), h * 0.28f);
        const ImVec2 ss = ImGui::CalcTextSize("S");
        dl->AddText(theme::Strong(), 0.f, ImVec2(p.x + (h - ss.x) * 0.5f, p.y + (h - ss.y) * 0.5f), theme::U32(theme::OnAccent), "S");
        ImGui::Dummy(ImVec2(h, h));
        ImGui::SameLine(0, fs * 0.5f);
    }
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Session Studio");
    if (theme::Strong()) ImGui::PopFont();
    ImGui::SameLine(0, fs * 1.2f);
    {
        static const char* kWs[2] = {"Browse", "Animate"};
        int w = ws_;
        if (ui::Segmented("##workspace", kWs, 2, &w)) SetWorkspace(w);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse: find and pick what to animate (the Library on the whole window).\nAnimate: the 3D view and the timeline.\nTab switches between them.");
    }
    if (ws_ == 1) {
        ImGui::SameLine(0, st.ItemSpacing.x);
        if (ui::Toggle("Library##drawer", drawer_)) drawer_ = !drawer_;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(drawer_ ? "Close the Library (B)" : "Open the Library over the 3D view (B):\ntricks, stances and feet, your clips, captures.");
    }
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    // ---- right group, laid out from the right edge: widths first
    std::string saveTxt;
    ImVec4 saveCol;
    if (doc_.HasPending() && !autoKey_) { saveTxt = "Change without key"; saveCol = theme::Warn; }
    else if (fileName_.empty()) { saveTxt = Dirty() ? "Never saved · changed" : "Never saved"; saveCol = Dirty() ? theme::Warn : theme::Text2; }
    else if (Dirty()) { saveTxt = "Not saved"; saveCol = theme::Warn; }
    else { saveTxt = "Saved"; saveCol = theme::Good; }
    std::string gameTxt;
    ImVec4 gameCol;
    GameStatus(gameTxt, gameCol);
    const bool icons = theme::HasIcons();
    auto IconW = [&](const char* fallback) { return icons ? h : ImGui::CalcTextSize(fallback).x + st.FramePadding.x * 2.f; };
    const float dotW = fs * 0.44f + fs * 0.33f;
    const float wSave = dotW + ImGui::CalcTextSize(saveTxt.c_str()).x;
    const float wPill = ImGui::CalcTextSize(gameTxt.c_str()).x + fs * 1.2f + fs * 0.9f;
    const float wBtn = ImGui::CalcTextSize("Save").x + st.FramePadding.x * 2.f;
    const float gapS = st.ItemSpacing.x, gapL = fs * 1.1f;
    const float right = wSave + gapL + wPill + gapL + wBtn + gapS + IconW("Undo") + gapS * 0.5f + IconW("Redo") + gapS + IconW("...");
    const float rx = ImGui::GetWindowContentRegionMax().x - right;

    // ---- centre: the variant (click: the Library) and the clip
    {
        const std::string var = VariantTitleUi(var_) + (icons ? "  " + theme::Glyph(theme::Icon::Down) : std::string("  v"));
        const std::string clip = fileName_.empty() ? std::string("new clip") : fileName_;
        const float wv = ImGui::CalcTextSize(var.c_str()).x + st.FramePadding.x * 2.f, wc = ImGui::CalcTextSize(clip.c_str()).x;
        const float total = wv + gapS + wc;
        float cx = (L.w - total) * 0.5f;                       // centred in the window when it fits
        cx = std::clamp(cx, leftEnd + gapL, std::max(leftEnd + gapL, rx - gapL - total));
        ImGui::SameLine(cx);
        if (ui::Button((var + "##breadcrumb").c_str(), ui::Kind::Ghost)) {
            if (ws_ == 1) drawer_ = !drawer_;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(ws_ == 1 ? "The trick (or ground state), stance and foot you are editing.\nClick: open the Library to change them."
                                                               : "The trick (or ground state), stance and foot you are editing.");
        ImGui::SameLine(0, gapS * 0.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Text2, "%s", clip.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fileName_.empty() ? "This clip is not a file yet: Save (Ctrl+S) gives it an automatic name." : "The clip file open, in the game's clips folder.");
    }

    // ---- right
    ImGui::SameLine(std::max(rx, ImGui::GetCursorPosX() + gapL));
    ImGui::SetCursorPosY(y);
    ui::StatusDot(saveCol);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(saveCol, "%s", saveTxt.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", doc_.HasPending() && !autoKey_ ? "A bone was changed without setting a key: K sets it at the time shown."
        : fileName_.empty() ? "This clip is not on disk yet. Save (Ctrl+S) creates it in the game's clips folder."
        : Dirty() ? "Some changes are not in the file yet. Save (Ctrl+S) to keep them."
                  : "The file on disk holds everything you see.");
    ImGui::SameLine(0, gapL);
    {   // the game pill: click for the details (what the status strip used to show)
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##gamepill", ImVec2(wPill, h));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) ImGui::OpenPopup("##game");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + wPill, p.y + h), theme::U32(gameCol, hov ? 0.22f : 0.13f), h * 0.5f);
        dl->AddRect(p, ImVec2(p.x + wPill, p.y + h), theme::U32(gameCol, 0.45f), h * 0.5f);
        dl->AddCircleFilled(ImVec2(p.x + fs * 0.7f, p.y + h * 0.5f), fs * 0.2f, theme::U32(gameCol));
        dl->AddText(ImVec2(p.x + fs * 1.2f, p.y + (h - fs) * 0.5f), theme::U32(gameCol), gameTxt.c_str());
        if (hov) ImGui::SetTooltip("The game and the mod, and what the game did with your last save.\nClick for the details, the folders and the last messages.");
        ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 22.f, 0.f), ImVec2(fs * 34.f, L.h * 0.8f));
        if (ImGui::BeginPopup("##game")) { GamePopup(); ImGui::EndPopup(); }
    }
    ImGui::SameLine(0, gapL);
    if (ui::Button("Save", ui::Kind::Primary)) Request(Ask::Save);   // the only orange filled button
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save (Ctrl+S)\nWrites the clip to the game's clips folder (automatic name for a new clip).\nThe previous version is copied to the backups first.\nThe game pill (left of this button) tells you whether the game reloaded it.");
    // undo / redo (never called "Annuler": that word is the Cancel of every dialog)
    ImGui::SameLine(0, gapS);
    if (!doc_.CanUndo()) ImGui::BeginDisabled();
    if (ui::IconButton("undo", theme::Icon::Undo, "Undo", false, ui::Kind::Ghost)) { if (doc_.Undo()) AfterUndoRedo(); }
    if (!doc_.CanUndo()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(doc_.CanUndo() ? "Undo: %s (Ctrl+Z)" : "Nothing to undo (Ctrl+Z)", doc_.UndoLabel());
    ImGui::SameLine(0, gapS * 0.5f);
    if (!doc_.CanRedo()) ImGui::BeginDisabled();
    if (ui::IconButton("redo", theme::Icon::Redo, "Redo", false, ui::Kind::Ghost)) { if (doc_.Redo()) AfterUndoRedo(); }
    if (!doc_.CanRedo()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(doc_.CanRedo() ? "Redo: %s (Ctrl+Y)" : "Nothing to redo (Ctrl+Y)", doc_.RedoLabel());
    ImGui::SameLine(0, gapS);
    if (ui::IconButton("more", theme::Icon::More, "...", false, ui::Kind::Ghost)) ImGui::OpenPopup("##more");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New, open, save as, Studio mode, help, folders");
    if (ImGui::BeginPopup("##more")) { MoreMenuItems(); ImGui::EndPopup(); }
    ImGui::End();
}

void App::SetWorkspace(int w) {
    if (w == ws_) return;
    ws_ = w;
    if (ws_ == 0) playing_ = false;   // nothing plays behind the Library
    SaveSetting(settings_, "ui.workspace", ws_ == 0 ? "browse" : "animate");
}

void App::MoreMenuItems() {
    if (ImGui::MenuItem("New clip", "Ctrl+N")) Request(Ask::New);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new clip for the variant shown.");
    if (ImGui::MenuItem("Open...", "Ctrl+O")) openDialog_ = true;
    if (ImGui::MenuItem("Save as...")) Request(Ask::SaveAs);
    ImGui::Separator();
    bool st = studio_;
    if (ImGui::MenuItem("Studio mode", nullptr, &st)) { studio_ = st; SaveSetting(settings_, "ui.studio", studio_ ? "1" : "0"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Studio: precise values, track weights, bone list, Advanced tab.\nOff (Creator): the essentials. The clip does not change.");
    ImGui::Separator();
    if (ImGui::MenuItem("Preferences...", "Ctrl+,")) popPrefs_ = true;   // redesign step 7
    if (ImGui::MenuItem("Keyboard shortcuts", "F1")) popKeys_ = true;
    if (ImGui::MenuItem("Welcome and setup check")) showWelcome_ = true;
    if (ImGui::MenuItem("Open the guide")) OpenGuide();
    ImGui::Separator();
    if (ImGui::MenuItem("Open the clips folder")) OpenInExplorer(clipsDir_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens in Explorer:\n%s", clipsDir_.empty() ? "(not chosen yet)" : Narrow(clipsDir_).c_str());
    if (ImGui::MenuItem("Open the backups")) {
        const std::string stem = fileName_.size() > 6 ? fileName_.substr(0, fileName_.size() - 6) : std::string();
        const std::wstring d = backupRoot_ + (stem.empty() ? L"" : L"\\" + Widen(stem));
        OpenInExplorer(DirExists(d) ? d : backupRoot_);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous versions of your clips (the last 20 per clip),\ncopied before every save, outside the game folder:\n%s", Narrow(backupRoot_).c_str());
    if (ImGui::MenuItem("Change the clips folder...")) dirDialog_ = true;
}

// ------------------------------------------------------------------ UI Phase 5 (2026-11-25): onboarding
void App::OpenGuide() {
    // 2026-11-25: the web page (.html) opens in the browser; a .md can be tied to any program (on the user's PC: an AI
    // tool), so the .md only goes to Notepad, never to its file association
    auto Has = [](const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    const std::wstring stem = exeDir_ + L"docs\\Getting-Started";
    const std::wstring html = stem + L".html", md = stem + L".md";
    if (Has(html) && (INT_PTR)ShellExecuteW(nullptr, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) return;
    if (!Has(md)) { Status("guide not found next to the editor (docs folder)"); return; }
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + md + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void App::WelcomeWindow() {   // redesign step 7: a card like the dialogs (a plain window: the dialogs it opens come on top)
    if (!showWelcome_) return;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    const float w = std::min(d.x - fs * 2.f, fs * 44.f);
    const float pad = fs * 1.6f;
    ImGui::SetNextWindowPos(ImVec2(d.x * 0.5f, d.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Line);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Raised);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.212f, 0.239f, 0.294f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.270f, 0.302f, 0.365f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Bg1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, fs * 1.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fs * 0.75f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    const bool visible = ImGui::Begin("Welcome to Session Studio###bienvenue", nullptr,
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);
    bool close = false;
    if (visible) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.5f, fs * 0.45f));
        // ---- header: the logo mark, the title, a close button
        {
            const float lh = std::round(fs * 2.8f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + lh, p.y + lh), theme::U32(theme::Accent), lh * 0.28f);
            if (theme::Strong()) {
                const float ls = ImGui::GetStyle().FontSizeBase * 1.6f;
                ImGui::PushFont(theme::Strong(), ls);
                const ImVec2 ss = ImGui::CalcTextSize("S");
                dl->AddText(ImVec2(p.x + (lh - ss.x) * 0.5f, p.y + (lh - ss.y) * 0.5f), theme::U32(theme::OnAccent), "S");
                ImGui::PopFont();
            }
            ImGui::Dummy(ImVec2(lh, lh));
            ImGui::SameLine(0, fs * 0.9f);
            ImGui::BeginGroup();
            if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.45f);
            ImGui::TextUnformatted("Welcome to Session Studio");
            if (theme::Strong()) ImGui::PopFont();
            ImGui::TextColored(theme::Text2, "%s", "Animate your skater here, then see it in Session.");
            ImGui::EndGroup();
            ImGui::SameLine(w - pad - ImGui::GetFrameHeight());
            if (ui::IconButton("welcomeclose", theme::Icon::Close, "x", false, ui::Kind::Ghost)) close = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close (the ... menu at the top right brings it back)");
        }
        ImGui::PushTextWrapPos(w - pad);
        ImGui::Dummy(ImVec2(0.f, fs * 0.2f));
        ImGui::TextWrapped("%s", "You adjust a pose here, you save, and the game plays it right away, without restarting. The game itself is never modified: everything goes through clip files (.sanim) that you can share.");

        // ---- the setup checker: one line per condition, with what to do when it is not met
        struct Check { bool ok; const char* what; const char* fix; };
        const Check checks[] = {
            {!clipsDir_.empty() && DirExists(clipsDir_), "Game clips folder found",
             "Choose the folder ...\\Win64\\Mods\\SessionAnimLoader\\clips (button below)."},
            {modUe4ss_, "UE4SS (the mod loader) is installed", "Install UE4SS in the game's Win64 folder: without it, no mod loads."},
            {modNative_ && modLua_, "The Session Anim Loader mod is installed (DLL + Lua)",
             "Copy Mods\\SessionAnimLoader and Mods\\SessionAnimLoaderNative into the game's Mods folder, each with its enabled.txt."},
            {skeletonOk_, "The game wrote the skater's skeleton",
             "Start the game once with the mod and put the skater on the board: the _skeleton.json file is written by itself."},
            {logFound_ && logHooked_, "The mod hooked into the game (last session)",
             "Start the game and wait about 10 s with the skater: the mod hooks in by itself. Ctrl+F3 in game turns it on or off."},
        };
        int missing = 0;
        for (const Check& c : checks) missing += !c.ok;
        ImGui::Dummy(ImVec2(0.f, fs * 0.3f));
        ImGui::TextColored(theme::Text3, "%s", "YOUR SETUP");
        ImGui::SameLine();
        if (missing) { char t[32]; snprintf(t, sizeof(t), "%d to fix", missing); ui::Chip(t, theme::Warn); }
        else ui::Chip("all good", theme::Good);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.9f, fs * 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, fs * 0.5f);
        ImGui::BeginChild("##setup", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(2);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
        for (const Check& c : checks) {
            Dot(c.ok ? kGood : kBad);
            ImGui::TextUnformatted(c.what);
            if (!c.ok) { ImGui::Indent(fs * 1.2f); ImGui::TextColored(kAlert, "%s", c.fix); ImGui::Unindent(fs * 1.2f); }
        }
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.f, fs * 0.1f));
        if (ui::Button(clipsDir_.empty() ? "Choose the clips folder" : "Change the clips folder", clipsDir_.empty() ? ui::Kind::Primary : ui::Kind::Normal)) dirDialog_ = true;
        if (!clipsDir_.empty()) {
            ImGui::SameLine();
            if (ui::Button("Open the clips folder")) OpenInExplorer(clipsDir_);
        }
        ImGui::EndChild();

        // ---- how it works: five numbered steps
        ImGui::Dummy(ImVec2(0.f, fs * 0.3f));
        ImGui::TextColored(theme::Text3, "%s", "HOW IT WORKS");
        const char* steps[] = {
            "Do the move once in game: its original movement is recorded by itself (its dots turn green in Browse).",
            "In Browse, pick the move, then the stance and the foot, and click \"Animate this variant\".",
            "Pick a body part (click it in the view, or keys 1 to 7) and pull or rotate it: a key is set by itself.",
            "Save (Ctrl+S): the clip is written into the game, which reloads it within a second. The game pill at the top confirms it.",
            "Do the move again in game to see the result. Only your skater changes, on your screen.",
        };
        for (int i = 0; i < 5; i++) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float r = std::round(fs * 0.62f);
            dl->AddCircleFilled(ImVec2(p.x + r, p.y + r), r, theme::U32(theme::Accent, 0.18f));
            char n[4];
            snprintf(n, sizeof(n), "%d", i + 1);
            const ImVec2 ns = ImGui::CalcTextSize(n);
            dl->AddText(ImVec2(p.x + r - ns.x * 0.5f, p.y + r - ns.y * 0.5f), theme::U32(theme::AccentHi), n);
            ImGui::Dummy(ImVec2(r * 2.f, r * 2.f));
            ImGui::SameLine(0, fs * 0.6f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + r - fs * 0.5f);
            ImGui::TextWrapped("%s", steps[i]);
        }
        ImGui::Dummy(ImVec2(0.f, fs * 0.1f));
        ImGui::TextColored(theme::Text3, "%s", "Not supported yet: grinds, grabs, walking and running (in progress). Powerslide not checked yet.");
        ImGui::PopTextWrapPos();

        // ---- footer: a hint on the left, the buttons on the right
        ImGui::Dummy(ImVec2(0.f, fs * 0.4f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Text3, "%s", "F1: every shortcut");
        const ImGuiStyle& s = ImGui::GetStyle();
        const float bw = ImGui::CalcTextSize("Open the guide").x + ImGui::CalcTextSize("Start").x + s.FramePadding.x * 4.f + s.ItemSpacing.x + fs * 4.f;
        ImGui::SameLine(w - pad - bw);
        if (ui::Button("Open the guide")) OpenGuide();
        ImGui::SameLine();
        if (ui::Button("Start", ui::Kind::Primary, ImVec2(ImGui::CalcTextSize("Start").x + s.FramePadding.x * 2.f + fs * 4.f, 0.f))) close = true;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))) close = true;
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    if (close) {
        showWelcome_ = false;
        SaveSetting(settings_, "ui.welcomed", "1");
    }
}

void App::OpenInExplorer(const std::wstring& dir) {
    if (dir.empty() || !DirExists(dir)) { Status("folder not found"); return; }
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::PollGame(float dt) {
    gameAge_ += dt;
    if (lastSave_.any) lastSave_.age += dt;
    const bool waiting = lastSave_.any && !lastSave_.unchanged && lastSave_.reload == 0 && lastSave_.age < 10.f;
    if (gameAge_ < (waiting ? 0.5f : 3.f)) return;
    gameAge_ = 0.f;
    gameBin_ = GameBinDirFromClips(clipsDir_);
    gameRunning_ = IsGameRunning();
    const ModInstall mi = CheckModInstall(gameBin_);
    modNative_ = mi.native; modLua_ = mi.lua; modUe4ss_ = mi.ue4ss;
    if (gameBin_.empty()) { logFound_ = false; return; }
    const std::wstring log = gameBin_ + L"\\SessionAnimLoader.native.log";
    const uint64_t st = FileStamp(log);
    if (st != logStamp_) {   // the session state: parse the whole log only when it changed
        logStamp_ = st;
        std::string text;
        unsigned long long n = 0;
        if (st && ReadFrom(log, 0, text, &n)) {
            const GameLogInfo g = ParseGameLog(text);
            logFound_ = g.found; logVersion_ = g.version; logHooked_ = g.hooked; logRefused_ = g.refused; logStudioOn_ = g.studioOn;
            logInstall_ = g.lastInstallLine;
        } else logFound_ = false;
    }
    if (lastSave_.any && !lastSave_.unchanged && lastSave_.reload == 0) {   // did the game reload the file we wrote?
        std::string text;
        unsigned long long n = 0;
        if (ReadFrom(log, lastSave_.logFrom, text, &n)) {
            const ReloadEvent ev = FindReload(text, lastSave_.file);
            if (ev.kind != ReloadKind::None) { lastSave_.reload = (int)ev.kind; lastSave_.reloadTime = ev.time; lastSave_.reloadDetail = ev.detail; }
        }
    }
}

// redesign step 2 (2026-11-27): the status strip became the game pill (short state) and its popup (details).
void App::GameStatus(std::string& t, ImVec4& col) const {
    // 1. the game and the in-game mod; sev: 0 fine, 1 warning, 2 error (the worst one gives the colour)
    int sev = 0;
    bool good = false;
    if (gameBin_.empty()) { sev = 1; t = "Game folder unknown"; }
    else if (!modNative_ || !modLua_) { sev = 2; t = "Mod not installed"; }
    else if (!gameRunning_) t = "Game closed";
    else if (logFound_ && logHooked_) { if (logStudioOn_ == 0) sev = 1; else good = true; t = logStudioOn_ == 0 ? "In game · mod off" : "In game · mod on"; }
    else if (logFound_ && logRefused_) { sev = 2; t = "In game · mod refused"; }
    else { sev = 1; t = "In game · mod not active yet"; }
    // 2. what the game did with the last save
    const LastSave& s = lastSave_;
    if (s.any && !s.unchanged) {
        if (!s.backupErr.empty()) { t += " · backup failed"; sev = 2; }
        if (s.reload == (int)ReloadKind::Loaded) { t += " · reloaded " + s.reloadTime; good = true; }
        else if (s.reload == (int)ReloadKind::Refused) { t += " · save REFUSED"; sev = 2; }
        else if (s.reload == (int)ReloadKind::KeptPrevious) { t += " · previous version kept"; sev = 2; }
        else if (s.reload == (int)ReloadKind::ReadFailed) { t += " · cannot read the file"; sev = std::max(sev, 1); }
        else if (gameRunning_ && s.age < 10.f) t += " · waiting for the game...";
        else if (gameRunning_) { t += " · no reload reported"; sev = std::max(sev, 1); }
    }
    col = sev == 2 ? theme::Bad : sev == 1 ? theme::Warn : good ? theme::Good : theme::Text2;
}

void App::GamePopup() {
    const float fs = ImGui::GetFontSize();
    auto Row = [&](const char* label, const std::string& value, const ImVec4& col) {
        ImGui::TextColored(theme::Text2, "%s", label);
        ImGui::SameLine(fs * 9.f);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(col, "%s", value.c_str());
        ImGui::PopTextWrapPos();
    };
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
    ImGui::TextUnformatted("Game and mod");
    if (theme::Strong()) ImGui::PopFont();
    {
        std::string t;
        ImVec4 c;
        GameStatus(t, c);
        Row("Now", t, c);
    }
    Row("Game folder", gameBin_.empty() ? std::string("unknown (the clips folder is not ...\\Mods\\SessionAnimLoader\\clips)") : Narrow(gameBin_), gameBin_.empty() ? theme::Warn : theme::Text);
    Row("Native mod", modNative_ ? "installed" : "missing or disabled", modNative_ ? theme::Text : theme::Bad);
    Row("Lua mod", modLua_ ? "installed" : "missing or disabled", modLua_ ? theme::Text : theme::Bad);
    Row("Game running", gameRunning_ ? "yes" : "no", theme::Text);
    if (logFound_) {
        Row("Last session", "SessionAnimLoaderNative v0." + std::to_string(logVersion_) + (logHooked_ ? ", hooked to the animation" : ", not hooked"), logHooked_ ? theme::Text : theme::Warn);
        if (!logInstall_.empty() && !logHooked_) Row("", logInstall_, theme::Text2);
    } else Row("Game log", "SessionAnimLoader.native.log missing", theme::Warn);
    ImGui::TextDisabled("%s", "The mod hooks in about 10 s after the skater appears. Ctrl+F3 in game turns it on or off.");

    ImGui::Spacing();
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
    ImGui::TextUnformatted("Last save");
    if (theme::Strong()) ImGui::PopFont();
    const LastSave& s = lastSave_;
    if (!s.any) ImGui::TextColored(theme::Text2, "%s", "Not saved since start.");
    else {
        ImVec4 col = theme::Text;
        std::string t;
        if (s.unchanged) t = "same as the file: nothing to reload";
        else if (s.reload == (int)ReloadKind::Loaded) { col = theme::Good; t = "reloaded by the game at " + s.reloadTime; }
        else if (s.reload == (int)ReloadKind::Refused) { col = theme::Bad; t = "REFUSED by the game"; }
        else if (s.reload == (int)ReloadKind::KeptPrevious) { col = theme::Bad; t = "error: the game keeps the previous version"; }
        else if (s.reload == (int)ReloadKind::ReadFailed) { col = theme::Warn; t = "the game cannot read the file (it retries)"; }
        else if (!gameRunning_) t = "game closed: loaded at next start";
        else if (s.age < 10.f) { col = theme::Warn; t = "waiting for the game..."; }
        else { col = theme::Warn; t = "the game reported no reload: is the mod on in game?"; }
        Row("Saved at", s.clock, theme::Text);
        Row("File", Narrow(clipsDir_) + "\\" + s.file, theme::Text);
        Row("Game", t, col);
        if (!s.reloadDetail.empty()) Row("The game wrote", s.reloadDetail, theme::Text2);
        Row("Previous version", !s.backupErr.empty() ? "NOT backed up (" + s.backupErr + ")" : s.backup.empty() ? (s.unchanged ? std::string("unchanged") : std::string("none (new file)")) : Narrow(s.backup),
            !s.backupErr.empty() ? theme::Bad : theme::Text2);
        ImGui::TextDisabled("%s", "\"Reloaded\" means the game read the file without error. To see the result, do the move in game.");
    }

    ImGui::Spacing();
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
    ImGui::TextUnformatted("Editor");
    if (theme::Strong()) ImGui::PopFont();
    Row("Skeleton", skeletonOk_ ? std::to_string(doc_.BoneCount()) + " bones (_skeleton.json)" : std::string("missing: start the game once with the mod"), skeletonOk_ ? theme::Text : theme::Bad);
    Row("This variant", doc_.HasFigure() ? "game animation recorded" : "do \"" + VariantTitleUi(var_) + "\" once in game", doc_.HasFigure() ? theme::Good : theme::Warn);
    if (ui::Button("Clips folder")) OpenInExplorer(clipsDir_);
    ImGui::SameLine();
    if (ui::Button("Backups")) {
        const std::string stem = fileName_.size() > 6 ? fileName_.substr(0, fileName_.size() - 6) : std::string();
        const std::wstring d = backupRoot_ + (stem.empty() ? L"" : L"\\" + Widen(stem));
        OpenInExplorer(DirExists(d) ? d : backupRoot_);
    }
    ImGui::SameLine();
    if (ui::Button("Change folder...")) { dirDialog_ = true; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ui::Button("Setup check")) { showWelcome_ = true; ImGui::CloseCurrentPopup(); }

    ImGui::Spacing();
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
    ImGui::TextUnformatted("Last messages");
    if (theme::Strong()) ImGui::PopFont();
    if (msgs_.empty()) ImGui::TextColored(theme::Text2, "%s", "None yet.");
    for (size_t i = msgs_.size(); i-- > 0;) {
        ImGui::TextColored(theme::Text3, "%s", msgs_[i].clock.c_str());
        ImGui::SameLine(fs * 4.5f);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(msgs_[i].text.c_str());
        ImGui::PopTextWrapPos();
    }
}

// the last message for a few seconds, at the bottom-right of the stage (Animate) or of the window (Browse);
// drawn above everything, never catches the mouse
void App::Toast() {
    if (status_.empty() || statusAge_ > 6.f) return;
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    const float a = std::clamp((6.f - statusAge_) / 0.8f, 0.f, 1.f) * std::clamp(statusAge_ / 0.12f, 0.f, 1.f);   // fade in and out
    const float maxW = fs * 26.f;
    const ImVec2 ts = ImGui::CalcTextSize(status_.c_str(), nullptr, false, maxW);
    const float px = fs * 0.8f, py = fs * 0.5f;
    // redesign step 3: bottom centre (the stage has its camera card bottom right and its banners bottom left)
    const float cx = ws_ == 1 ? L.stage.x + L.stage.w * 0.5f : L.w * 0.5f;
    const float by = ws_ == 1 ? L.stage.y + L.stage.h - OverlaySize("##camera").y - fs * 1.4f : L.h - L.gap - fs * 0.8f;   // above the cards
    const float tw = ts.x + px * 2.f + fs * 0.2f;
    const ImVec2 p0(std::round(cx - tw * 0.5f), by - ts.y - py * 2.f), p1(p0.x + tw, by);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(p0, p1, theme::U32(theme::Raised, 0.97f * a), fs * 0.4f);
    dl->AddRect(p0, p1, theme::U32(theme::Line, a), fs * 0.4f);
    dl->AddRectFilled(p0, ImVec2(p0.x + fs * 0.2f, p1.y), theme::U32(theme::AccentHi, a), fs * 0.4f, ImDrawFlags_RoundCornersLeft);
    dl->AddText(nullptr, 0.f, ImVec2(p0.x + fs * 0.2f + px, p0.y + py), theme::U32(theme::Text, a), status_.c_str(), nullptr, maxW);
}
