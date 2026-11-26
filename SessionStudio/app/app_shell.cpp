// Session Studio -- workspace shell: layout, splitters, header, welcome window, game polling, status strip.
#include "app_internal.h"

using namespace app_detail;


App::Layout App::Lay() const {
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    Layout L{};
    L.w = d.x; L.h = d.y;
    L.top = fs * 2.6f;
    L.status = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.f;
    L.gap = std::max(4.f, std::round(fs * 0.35f));
    // sides: at least a usable width each, and the Stage keeps at least 20 font sizes
    L.left = std::clamp(leftU_ * fs, fs * 12.f, std::max(fs * 12.f, L.w * 0.35f));
    L.right = std::clamp(rightU_ * fs, fs * 14.f, std::max(fs * 14.f, L.w * 0.40f));
    const float stageMin = fs * 20.f;
    const float over = L.left + L.right + 2.f * L.gap + stageMin - L.w;
    if (over > 0.f) { const float k = std::max(0.f, (L.left + L.right - over) / (L.left + L.right)); L.left *= k; L.right *= k; }
    // timeline: at least 7 font sizes, and the middle row keeps at least 12
    const float maxB = std::max(fs * 7.f, L.h - L.top - L.status - L.gap - fs * 12.f);
    const float wantB = bottomDefault_ ? std::min(bottomU_ * fs, L.h * 0.30f) : bottomU_ * fs;
    L.bottom = std::clamp(wantB, fs * 7.f, maxB);
    return L;
}

void App::SaveLayout() {
    Settings st;
    LoadSettings(settings_, st);
    st.SetFloat("layout.left", leftU_);
    st.SetFloat("layout.right", rightU_);
    st.SetFloat("layout.bottom", bottomU_);
    SaveSettings(settings_, st);
}

void App::Splitters() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    const float midH = L.h - L.top - L.status - L.bottom - L.gap;
    struct S { const char* id; float x, y, w, h; int kind; };   // kind 0 left, 1 right, 2 bottom
    const S ss[3] = {{"##split_gauche", L.left, L.top, L.gap, midH, 0},
                     {"##split_droite", L.w - L.right - L.gap, L.top, L.gap, midH, 1},
                     {"##split_bas", 0.f, L.top + midH, L.w, L.gap, 2}};
    for (const S& s : ss) {
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
            leftU_ = N.left / fs; rightU_ = N.right / fs; bottomU_ = N.bottom / fs;
            SaveLayout();
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (s.kind == 0) leftU_ = kDefLeft; else if (s.kind == 1) rightU_ = kDefRight; else bottomU_ = kDefBottom;
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

void App::HeaderBar() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    Place(0, 0, L.w, L.top);
    ImGui::Begin("##haut", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY((L.top - ImGui::GetFrameHeight()) * 0.5f);
    // logo mark (orange rounded square, drawn) and the name in the title font
    {
        const float h = ImGui::GetFrameHeight();
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
    Badge(VariantTitleUi(var_).c_str(), theme::Text);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The trick (or ground state), stance and foot you are editing.\nChange them in the left column.");
    ImGui::SameLine(0, fs * 0.8f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Clip: %s", fileName_.empty() ? "new clip" : fileName_.c_str());
    ImGui::SameLine(0, fs * 0.6f);
    // save state
    if (doc_.HasPending() && !autoKey_) Badge("Change without key", kAlert);
    else if (fileName_.empty()) Badge(Dirty() ? "Never saved · changed" : "Never saved", Dirty() ? kAlert : kMuted);
    else if (Dirty()) Badge("Not saved", kAlert);
    else Badge("Saved", kGood);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fileName_.empty()
        ? "This clip is not on disk yet. Save (Ctrl+S) creates it in the game's clips folder."
        : Dirty() ? "Some changes are not in the file yet. Save (Ctrl+S) to keep them."
                  : "The file on disk holds everything you see.");

    // file actions
    ImGui::SameLine(0, fs * 1.5f);
    if (ui::Button("New")) Request(Ask::New);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New clip for the variant shown.");
    ImGui::SameLine();
    if (ui::Button("Open...")) openDialog_ = true;
    ImGui::SameLine();
    if (ui::Button("Save", ui::Kind::Primary)) Request(Ask::Save);   // the only orange filled button
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save (Ctrl+S)\nWrites the clip to the game's clips folder (automatic name for a new clip).\nThe previous version is copied to the backups first.\nThe bottom bar tells you whether the game reloaded it.");
    ImGui::SameLine();
    if (ui::Button("Save as...")) Request(Ask::SaveAs);

    // undo / redo (never called "Annuler": that word is the Cancel of every dialog)
    ImGui::SameLine(0, fs * 1.5f);
    if (!doc_.CanUndo()) ImGui::BeginDisabled();
    if (ui::IconButton("undo", theme::Icon::Undo, "Undo")) { if (doc_.Undo()) AfterUndoRedo(); }
    if (!doc_.CanUndo()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(doc_.CanUndo() ? "Undo: %s (Ctrl+Z)" : "Nothing to undo (Ctrl+Z)", doc_.UndoLabel());
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    if (!doc_.CanRedo()) ImGui::BeginDisabled();
    if (ui::IconButton("redo", theme::Icon::Redo, "Redo")) { if (doc_.Redo()) AfterUndoRedo(); }
    if (!doc_.CanRedo()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(doc_.CanRedo() ? "Redo: %s (Ctrl+Y)" : "Nothing to redo (Ctrl+Y)", doc_.RedoLabel());

    // auto key: always visible, it changes what every gesture does (record style: red dot when on)
    ImGui::SameLine(0, fs * 1.5f);
    if (ui::RecordToggle("Auto key", autoKey_)) autoKey_ = !autoKey_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(autoKey_ ? "Auto key is ON: every gesture sets a key at the time shown.\nClick to turn it off (then you set keys yourself with K)."
                                                           : "Auto key is OFF: you set the key yourself with K (or the \"+ key\" button).\nClick to turn it on.");
    // UI Phase 5: the welcome window (setup check, the 5 steps, the guide) can come back at any time
    ImGui::SameLine(0, fs * 1.5f);
    if (ui::IconButton("help", theme::Icon::Help, "Help")) showWelcome_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Help: checks the installation, recalls the steps and opens the guide.");
    ImGui::End();
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

void App::WelcomeWindow() {
    if (!showWelcome_) return;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(d.x * 0.5f, d.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(d.x - fs * 2.f, fs * 40.f), 0.f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Welcome to Session Studio###bienvenue", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        if (!open) showWelcome_ = false;
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 38.f);
    ImGui::TextWrapped("%s", "Session Studio changes how your skater moves in Session. You adjust a pose here, you save, and the game plays it right away, without restarting. The game itself is never modified: everything goes through clip files (.sanim) that you can share.");
    ImGui::Spacing();
    ImGui::SeparatorText("Installation");
    // the setup checker: one line per condition, with what to do when it is not met
    auto Row = [&](bool ok, const char* what, const char* fix) {
        Dot(ok ? kGood : kBad);
        ImGui::TextUnformatted(what);
        if (!ok && fix) { ImGui::Indent(fs * 1.2f); ImGui::TextColored(kAlert, "%s", fix); ImGui::Unindent(fs * 1.2f); }
    };
    Row(!clipsDir_.empty() && DirExists(clipsDir_), "Game clips folder found",
        "Choose the folder ...\\Win64\\Mods\\SessionAnimLoader\\clips (button below).");
    Row(modUe4ss_, "UE4SS (the mod loader) is installed",
        "Install UE4SS in the game's Win64 folder: without it, no mod loads.");
    Row(modNative_ && modLua_, "The Session Anim Loader mod is installed (DLL + Lua)",
        "Copy Mods\\SessionAnimLoader and Mods\\SessionAnimLoaderNative into the game's Mods folder, each with its enabled.txt.");
    Row(skeletonOk_, "The game wrote the skater's skeleton",
        "Start the game once with the mod and put the skater on the board: the _skeleton.json file is written by itself.");
    Row(logFound_ && logHooked_, "The mod hooked into the game (last session)",
        "Start the game and wait about 10 s with the skater: the mod hooks in by itself. Ctrl+F3 in game turns it on or off.");
    if (ImGui::Button("Choose the clips folder")) dirDialog_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Open the clips folder")) OpenInExplorer(clipsDir_);
    ImGui::Spacing();
    ImGui::SeparatorText("How it works");
    const char* steps[] = {
        "1. Do the move once in game: its original movement is recorded by itself (it turns green in the Library).",
        "2. In the Library, pick the move, the stance and the foot.",
        "3. Pick a body part (buttons above the view, keys 1 to 7) and pull or rotate it: a key is set by itself.",
        "4. Save (Ctrl+S): the clip is written into the game, which reloads it within a second. The bottom bar confirms it.",
        "5. Do the move again in game to see the result. Only your skater changes, on your screen.",
    };
    for (const char* st : steps) ImGui::TextWrapped("%s", st);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", "Not supported yet: grinds, grabs, walking and running (in progress). Powerslide not checked yet.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool start = ui::Button("Start", ui::Kind::Primary, ImVec2(fs * 8.f, 0));
    ImGui::SameLine();
    if (ImGui::Button("Open the guide")) OpenGuide();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", "(\"Help\" button at the top to come back here)");
    ImGui::End();
    if (start || !open) {
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

void App::StatusBar() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    Place(0, L.h - L.status, L.w, L.status);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Bg1);
    ImGui::Begin("##etat", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    const float sp = fs * 1.2f;

    // 1. the game and the in-game mod
    {
        ImVec4 col = kMuted;
        std::string t;
        if (gameBin_.empty()) { col = kAlert; t = "Game: folder unknown"; }
        else if (!modNative_ || !modLua_) { col = kBad; t = "Mod not installed in the game"; }
        else if (!gameRunning_) { col = kMuted; t = "Game: closed"; }
        else if (logFound_ && logHooked_) { col = logStudioOn_ == 0 ? kAlert : kGood; t = logStudioOn_ == 0 ? "Game: running · mod off (Ctrl+F3)" : "Game: running · mod on"; }
        else if (logFound_ && logRefused_) { col = kBad; t = "Game: running · mod refused"; }
        else { col = kAlert; t = "Game: running · mod not active yet"; }
        Dot(col);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col, "%s", t.c_str());
        if (ImGui::IsItemHovered()) {
            std::string tip = "Game folder: " + (gameBin_.empty() ? std::string("unknown (the clips folder is not ...\\Mods\\SessionAnimLoader\\clips)") : Narrow(gameBin_));
            tip += std::string("\nNative mod (SessionAnimLoaderNative): ") + (modNative_ ? "installed" : "missing or disabled");
            tip += std::string("\nLua mod (SessionAnimLoader): ") + (modLua_ ? "installed" : "missing or disabled");
            tip += std::string("\nGame running: ") + (gameRunning_ ? "yes" : "no");
            if (logFound_) {
                tip += "\nLast game session: SessionAnimLoaderNative v0." + std::to_string(logVersion_) + (logHooked_ ? ", hooked to the animation" : ", not hooked");
                if (!logInstall_.empty() && !logHooked_) tip += "\n  " + logInstall_;
            } else tip += "\nGame log (SessionAnimLoader.native.log): missing";
            tip += "\n\nThe mod hooks in about 10 s after the skater appears. Ctrl+F3 in game turns it on or off.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    // 2. the last save and what the game did with it
    ImGui::SameLine(0, sp);
    {
        ImVec4 col = kMuted;
        std::string t;
        const LastSave& s = lastSave_;
        if (!s.any) t = "Not saved since start";
        else {
            t = "Saved " + s.clock + " · ";
            if (!s.backupErr.empty()) { col = kBad; t += "previous version NOT backed up · "; }
            if (s.unchanged) t += "same as the file: nothing to reload";
            else if (s.reload == (int)ReloadKind::Loaded) { col = s.backupErr.empty() ? kGood : col; t += "reloaded by the game at " + s.reloadTime; }
            else if (s.reload == (int)ReloadKind::Refused) { col = kBad; t += "REFUSED by the game (see details)"; }
            else if (s.reload == (int)ReloadKind::KeptPrevious) { col = kBad; t += "error: the game keeps the previous version"; }
            else if (s.reload == (int)ReloadKind::ReadFailed) { col = kAlert; t += "the game cannot read the file (it retries)"; }
            else if (!gameRunning_) t += "game closed: loaded at next start";
            else if (s.age < 10.f) { col = kAlert; t += "waiting for the game..."; }
            else { col = kAlert; t += "the game reported no reload"; }
        }
        Dot(col);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col, "%s", t.c_str());
        if (ImGui::IsItemHovered() && s.any) {
            std::string tip = "File: " + Narrow(clipsDir_) + "\\" + s.file;
            tip += "\nPrevious version: " + (!s.backupErr.empty() ? "not backed up (" + s.backupErr + ")" : s.backup.empty() ? (s.unchanged ? std::string("unchanged") : std::string("none (new file)")) : Narrow(s.backup));
            if (!s.reloadDetail.empty()) tip += "\nThe game wrote: " + s.reloadDetail;
            if (s.reload == 0 && !s.unchanged && gameRunning_ && s.age >= 10.f) tip += "\nIs the mod on in game? Look at the dot on the left.";
            tip += "\n\n\"Reloaded\" means the game read the file without error.\nTo see the result, do the move in game.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    // 3. skeleton and source animation
    ImGui::SameLine(0, sp);
    Dot(skeletonOk_ ? kGood : kBad);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(skeletonOk_ ? kMuted : kBad, skeletonOk_ ? "Skeleton: %d bones" : "Skeleton missing", doc_.BoneCount());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(skeletonOk_ ? "The skater's skeleton, written by the game (_skeleton.json)." : "Start the game once with the mod installed: it writes _skeleton.json.");
    ImGui::SameLine(0, sp);
    const bool ready = doc_.HasFigure();
    Dot(ready ? kGood : kAlert);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ready ? kMuted : kAlert, ready ? "Game animation: ready" : "Game animation: to do in game");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(ready ? "The game's movement for this variant is recorded: you edit on top of it."
                                                        : "Do \"%s\" once in game: the mod records the movement by itself.", VariantTitleUi(var_).c_str());
    // 4. folders
    ImGui::SameLine(0, sp);
    if (ImGui::SmallButton("Clips folder")) OpenInExplorer(clipsDir_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens in Explorer:\n%s", clipsDir_.empty() ? "(not chosen yet)" : Narrow(clipsDir_).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Backups")) {
        const std::string stem = fileName_.size() > 6 ? fileName_.substr(0, fileName_.size() - 6) : std::string();
        const std::wstring d = backupRoot_ + (stem.empty() ? L"" : L"\\" + Widen(stem));
        OpenInExplorer(DirExists(d) ? d : backupRoot_);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous versions of your clips (the last 20 per clip),\ncopied before every save, outside the game folder:\n%s", Narrow(backupRoot_).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Change...")) dirDialog_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose another game clips folder.");
    // 5. the last message
    if (!status_.empty()) {
        ImGui::SameLine(0, sp);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(statusAge_ < 12.f ? theme::Warn : kMuted, "%s", status_.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", status_.c_str());
    }
    ImGui::End();
}
