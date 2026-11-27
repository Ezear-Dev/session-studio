// Session Studio -- confirmation and file dialogs, Preferences, the shortcuts list, keyboard shortcuts.
// Redesign step 7 (2026-11-27): every dialog uses BeginDialog / DialogButtons (one look: title in the title font,
// quiet text, buttons on the right, Enter = the main button, Esc = Cancel). What each button does is unchanged.
#include "app_internal.h"
#include "imgui_internal.h"   // SetKeyOwner (Tab)

using namespace app_detail;

namespace app_detail {
static const int kDialogColors = 8;

bool BeginDialog(const char* id, const char* title, float widthEm, float heightEm) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float w = std::min(fs * widthEm, d.x - fs * 2.f);
    ImGui::SetNextWindowPos(ImVec2(d.x * 0.5f, d.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (heightEm > 0.f) ImGui::SetNextWindowSize(ImVec2(w, std::min(fs * heightEm, d.y - fs * 2.f)), ImGuiCond_Appearing);
    else ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.f), ImVec2(std::max(w, d.x * 0.8f), d.y * 0.9f));
    // the card colours of the Inspector: lighter buttons, darker fields (buttons must not look like plain text)
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Line);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Raised);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.212f, 0.239f, 0.294f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.270f, 0.302f, 0.365f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::Bg1);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.098f, 0.110f, 0.141f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.4f, fs * 1.2f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fs * 0.75f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                                   (heightEm > 0.f ? 0 : ImGuiWindowFlags_AlwaysAutoResize);
    const bool open = ImGui::BeginPopupModal(id, nullptr, flags);
    ImGui::PopStyleVar(3);
    if (!open) { ImGui::PopStyleColor(kDialogColors); return false; }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.5f, fs * 0.45f));
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::TextUnformatted(title);
    if (theme::Strong()) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, fs * 0.15f));
    ImGui::PushTextWrapPos(w - fs * 1.4f);   // text wraps inside the dialog's width (auto-resize would grow otherwise)
    return true;
}

void EndDialog() {
    ImGui::PopTextWrapPos();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
    ImGui::PopStyleColor(kDialogColors);
}

int DialogButtons(std::initializer_list<const char*> labels, int primary, int danger) {
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::Dummy(ImVec2(0.f, fs * 0.35f));
    float w = 0.f;
    int n = 0;
    for (const char* l : labels) { w += ImGui::CalcTextSize(l, nullptr, true).x + s.FramePadding.x * 2.f + (n ? s.ItemSpacing.x : 0.f); n++; }
    const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    int hit = -1, i = 0;
    for (const char* l : labels) {
        if (i) ImGui::SameLine();
        const ui::Kind k = i == danger ? ui::Kind::Danger : i == primary ? ui::Kind::Primary : ui::Kind::Normal;
        if (ui::Button(l, k)) hit = i;
        i++;
    }
    if (hit < 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enter && primary >= 0 && primary != danger) hit = primary;
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) hit = n - 1;
    }
    return hit;
}
} // namespace app_detail

namespace {
void QuietWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::Text2);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
void Section(const char* text) {   // a small section title inside a dialog
    ImGui::Dummy(ImVec2(0.f, ImGui::GetFontSize() * 0.3f));
    ImGui::TextColored(theme::Text3, "%s", text);
}
} // namespace

void App::Dialogs() {
    if (popPending_) { ImGui::OpenPopup("Pending change"); popPending_ = false; }
    if (popUnsaved_) { ImGui::OpenPopup("Unsaved clip"); popUnsaved_ = false; }
    if (popMode_) { ImGui::OpenPopup("Change mode"); popMode_ = false; }
    if (popOverwrite_) { ImGui::OpenPopup("Replace the file"); popOverwrite_ = false; }
    if (popCopy_) { ImGui::OpenPopup("Copy to another variant"); popCopy_ = false; }
    if (openDialog_) { ImGui::OpenPopup("Open a clip"); openDialog_ = false; openFilter_[0] = 0; }
    if (saveDialog_) { ImGui::OpenPopup("Save the clip"); saveDialog_ = false; }
    if (dirDialog_) { ImGui::OpenPopup("Clips folder"); dirDialog_ = false; strncpy_s(dirBuf_, Narrow(clipsDir_).c_str(), _TRUNCATE); }
    if (popPrefs_) { ImGui::OpenPopup("Preferences"); popPrefs_ = false; }
    if (popKeys_) { ImGui::OpenPopup("Keyboard shortcuts"); popKeys_ = false; }
    // UI Phase 2: Library file actions
    if (popRename_) { ImGui::OpenPopup("Rename the clip"); popRename_ = false; libErr_.clear(); }
    if (popDelete_) { ImGui::OpenPopup("Delete the clip"); popDelete_ = false; }
    if (popLock_) { ImGui::OpenPopup("Lock start and end"); popLock_ = false; }
    const float fs = ImGui::GetFontSize();

    if (BeginDialog("Lock start and end", "Lock start and end")) {   // 2026-11-26 (task 3)
        ImGui::TextWrapped("%s", "These parts are posed where the lock puts the game's pose back:");
        for (const EditorDoc::EndIssue& e : doc_.LockPreview()) {
            std::string parts;
            for (size_t i = 0; i < e.bones.size(); i++) parts += (i ? ", " : "") + BoneLabelUi(doc_.skeleton().names[e.bones[i]]);
            if (e.pelvis) parts += parts.empty() ? "Pelvis" : ", Pelvis";
            ImGui::BulletText("%s: %s", e.kind == EditorDoc::EndIssue::StartNotGame ? "at the start" : "at the end", parts.c_str());
        }
        QuietWrapped("The keys between them are kept. Ctrl+Z undoes it.");
        const int b = DialogButtons({"Reset them and lock", "Cancel"}, 0);
        if (b == 0) { doc_.SetLockEnds(true); Status("start and end locked to the game's pose (Ctrl+Z undoes it)"); ImGui::CloseCurrentPopup(); }
        if (b == 1) ImGui::CloseCurrentPopup();
        EndDialog();
    }
    if (BeginDialog("Rename the clip", "Rename the clip")) {
        ImGui::Text("New name for %s:", libFile_.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##name", renameBuf_, sizeof(renameBuf_));
        QuietWrapped("Only the file name: what the clip does does not change.");
        if (!libErr_.empty()) ImGui::TextColored(kBad, "%s", libErr_.c_str());
        const int b = DialogButtons({"Rename", "Cancel"}, 0);
        if (b == 0) RenameClip(libFile_, renameBuf_);
        if (b == 1 || popRenameClose_) { popRenameClose_ = false; ImGui::CloseCurrentPopup(); }
        EndDialog();
    }
    if (BeginDialog("Delete the clip", "Delete the clip?")) {
        ImGui::TextWrapped("%s", libFile_.c_str());
        QuietWrapped("It is removed from the game and moved to the backups (... menu at the top right, \"Open the backups\"): you can get it back.");
        if (libFile_ == fileName_) ImGui::TextColored(kAlert, "%s", "It is the open clip: it stays shown, as a clip not saved yet.");
        const int b = DialogButtons({"Delete", "Cancel"}, -1, 0);
        if (b == 0) { ImGui::CloseCurrentPopup(); DeleteClip(libFile_); }
        if (b == 1) ImGui::CloseCurrentPopup();
        EndDialog();
    }

    if (BeginDialog("Pending change", "A change has no key")) {
        ImGui::TextWrapped("A body part was changed without setting a key (at %.2f s). Without a key, the change is lost.", t_);
        const int b = DialogButtons({"Set the key", "Discard", "Cancel"}, 0);
        if (b == 0) {
            ImGui::CloseCurrentPopup();
            CommitKeyNow();
            if (!doc_.HasPending()) Request(ask_); else { ask_ = Ask::None; newAfterPick_ = false; }
        }
        if (b == 1) { ImGui::CloseCurrentPopup(); doc_.Checkpoint("discard"); doc_.DiscardPending(); Request(ask_); }
        if (b == 2) { ask_ = Ask::None; newAfterPick_ = false; ImGui::CloseCurrentPopup(); }
        EndDialog();
    }
    if (BeginDialog("Unsaved clip", ask_ == Ask::Close ? "Quit without saving?" : "Save the clip first?")) {
        ImGui::TextWrapped("%s", fileName_.empty() ? "This clip was never saved." : (fileName_ + " has changes that are not saved.").c_str());
        QuietWrapped("Save writes it into the game (a backup of the previous version is kept).");
        const int b = DialogButtons({"Save", "Don't save", "Cancel"}, 0);
        if (b == 0) { ImGui::CloseCurrentPopup(); afterSave_ = true; SaveCurrent(); }
        if (b == 1) { ImGui::CloseCurrentPopup(); Proceed(); }
        if (b == 2) { ImGui::CloseCurrentPopup(); ask_ = Ask::None; newAfterPick_ = false; }
        EndDialog();
    }
    if (BeginDialog("Change mode", "Change the mode")) {
        ImGui::TextWrapped("%s", "The existing keys will be converted using the game's animation. The pose shown does not change.");
        QuietWrapped("Ctrl+Z undoes it.");
        const int b = DialogButtons({"Convert", "Cancel"}, 0);
        if (b == 0) {
            if (askMode_ == Mode::Additive && !doc_.HasFigure()) Status("Add mode needs the game's animation: do the move once in game");
            else { doc_.Checkpoint("mode"); doc_.SetMode(askMode_); }
            ImGui::CloseCurrentPopup();
        }
        if (b == 1) ImGui::CloseCurrentPopup();
        EndDialog();
    }
    if (BeginDialog("Replace the file", "Replace the file?")) {
        ImGui::TextWrapped("%s already exists.", overwriteName_.c_str());
        QuietWrapped("The file in place is copied to the backups first.");
        const int b = DialogButtons({"Replace", "Cancel"}, 0);
        if (b == 0) {
            ImGui::CloseCurrentPopup();
            if (SaveAs(overwriteName_, true) && afterSave_) { afterSave_ = false; Proceed(); }
        }
        if (b == 1) { ImGui::CloseCurrentPopup(); afterSave_ = false; ask_ = Ask::None; newAfterPick_ = false; }
        EndDialog();
    }
    if (BeginDialog("Copy to another variant", "Copy to another variant")) {
        QuietWrapped("A copy of this clip, for:");
        const int nSt = StateAllStances(var_.state) ? STANCE_Count : 2;
        const char* stances[STANCE_Count];
        for (int s = 0; s < nSt; s++) stances[s] = StanceLabel(s);
        int st = std::clamp(askVar_.stance, 0, nSt - 1);
        if (ui::Segmented("##copystance", stances, nSt, &st)) askVar_.stance = st;
        static const char* kFeet[2] = {"Regular", "Goofy"};
        int ft = askVar_.goofy ? 1 : 0;
        if (ui::Segmented("##copyfeet", kFeet, 2, &ft)) askVar_.goofy = ft == 1;
        QuietWrapped("Left and right are swapped automatically when needed.");
        if (askVar_ == var_) ImGui::TextColored(kAlert, "%s", "Pick another variant than this one.");
        const int b = DialogButtons({"Copy", "Cancel"}, 0);
        if (b == 0 && askVar_ != var_) { ImGui::CloseCurrentPopup(); Request(Ask::CopyTo); }
        if (b == 1) ImGui::CloseCurrentPopup();
        EndDialog();
    }
    if (BeginDialog("Open a clip", "Open a clip", 32.f, 28.f)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##openfilter", "Part of the file name", openFilter_, sizeof(openFilter_));
        const std::string q = Lower(openFilter_);
        ImGui::BeginChild("##openlist", ImVec2(0.f, -(ImGui::GetFrameHeightWithSpacing() + fs * 0.6f)));
        for (int cap = 0; cap < 2; cap++) {
            const std::vector<std::string> files = ListClips(cap == 1);
            int shown = 0;
            for (auto& f : files) shown += q.empty() || Lower(f).find(q) != std::string::npos;
            Section(cap ? "CAPTURES (Ctrl+F4 in game)" : "MY CLIPS");
            if (!shown) { ImGui::TextColored(theme::Text3, "%s", files.empty() ? "none yet" : "nothing matches"); continue; }
            for (auto& f : files) {
                if (!q.empty() && Lower(f).find(q) == std::string::npos) continue;
                if (ImGui::Selectable(f.c_str())) { askFile_ = f; ImGui::CloseCurrentPopup(); Request(Ask::Open); }
            }
        }
        ImGui::EndChild();
        ImGui::TextColored(theme::Text3, "%s", "Browse (Tab) lists the clips by trick.");
        if (DialogButtons({"Close"}, -1) == 0) ImGui::CloseCurrentPopup();
        EndDialog();
    }
    if (BeginDialog("Save the clip", "Save the clip")) {
        QuietWrapped("File name, in the game's clips folder:");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##file", nameBuf_, sizeof(nameBuf_));
        ImGui::TextColored(theme::Text3, "%s", ".sanim is added if missing");
        const int b = DialogButtons({"Save", "Cancel"}, 0);
        if (b == 0) {
            if (SaveAs(nameBuf_, false)) { ImGui::CloseCurrentPopup(); if (afterSave_) { afterSave_ = false; Proceed(); } }
            else if (popOverwrite_) ImGui::CloseCurrentPopup();
        }
        if (b == 1) { ImGui::CloseCurrentPopup(); afterSave_ = false; ask_ = Ask::None; newAfterPick_ = false; }
        EndDialog();
    }
    if (BeginDialog("Clips folder", "The game's clips folder", 36.f)) {
        static std::string dirErr;   // shown inside the dialog (the status strip is hidden behind it)
        if (ImGui::IsWindowAppearing()) dirErr.clear();
        QuietWrapped("The mod's clips folder, inside the game folder:");
        ImGui::TextColored(theme::Text3, "%s", "...\\steamapps\\common\\Session\\SessionGame\\Binaries\\Win64\\Mods\\SessionAnimLoader\\clips");
        const float bw = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##folder", dirBuf_, sizeof(dirBuf_));
        ImGui::SameLine();
        if (ui::Button("Browse...")) {   // Windows folder picker (UI Phase 1)
            IFileOpenDialog* fd = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd)))) {
                DWORD o = 0;
                fd->GetOptions(&o);
                fd->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                fd->SetTitle(L"The game's Mods\\SessionAnimLoader\\clips folder");
                if (SUCCEEDED(fd->Show(GetActiveWindow()))) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(fd->GetResult(&it))) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { strncpy_s(dirBuf_, Narrow(p).c_str(), _TRUNCATE); CoTaskMemFree(p); }
                        it->Release();
                    }
                }
                fd->Release();
            } else dirErr = "the Windows folder picker could not open: paste the path";
        }
        const std::wstring cand = Widen(dirBuf_);
        if (!cand.empty() && DirExists(cand) && GameBinDirFromClips(cand).empty())
            ImGui::TextColored(kAlert, "%s", "This folder does not end with Mods\\SessionAnimLoader\\clips: the game will not read clips there.");
        if (!dirErr.empty()) ImGui::TextColored(kBad, "%s", dirErr.c_str());
        const int b = DialogButtons({"Use this folder", "Cancel"}, 0);
        if (b == 0) {
            const std::wstring d = Widen(dirBuf_);
            if (DirExists(d)) {
                clipsDir_ = d;
                gameBin_ = GameBinDirFromClips(d);
                gameAge_ = 99.f; logStamp_ = 0;
                SaveClipsDir(settings_, d);
                std::string err;
                skeletonOk_ = LoadSkeletonFile(err);
                skeletonErr_ = err;
                ScanFolder();
                SelectFigure(var_);
                ImGui::CloseCurrentPopup();
            } else dirErr = "this folder does not exist";
        }
        if (b == 1) ImGui::CloseCurrentPopup();
        EndDialog();
    }
    PreferencesDialog();
    ShortcutsDialog();
}

// redesign step 7: what the user chooses once for the whole editor (each choice is saved at once)
void App::PreferencesDialog() {
    if (!BeginDialog("Preferences", "Preferences", 30.f)) return;
    Section("MODE");
    static const char* kModes[2] = {"Creator", "Studio"};
    int m = studio_ ? 1 : 0;
    if (ui::Segmented("##prefmode", kModes, 2, &m)) { studio_ = m == 1; SaveSetting(settings_, "ui.studio", studio_ ? "1" : "0"); }
    QuietWrapped(studio_ ? "Studio: precise values, track weights, the bone list and the Advanced settings of the clip."
                         : "Creator: the essentials. Studio adds precise values and advanced settings; the clip does not change.");
    Section("TEXT SIZE");
    static const char* kSizes[4] = {"Small", "Normal", "Large", "Larger"};
    int ts = textSize_;
    if (ui::Segmented("##preftext", kSizes, 4, &ts)) {
        textSize_ = ts;
        ImGui::GetStyle().FontScaleMain = kTextScales[textSize_];
        char v[8];
        snprintf(v, sizeof(v), "%d", textSize_);
        SaveSetting(settings_, "ui.textsize", v);
    }
    QuietWrapped("The whole editor: text, buttons and panels follow.");
    Section("CLIPS FOLDER");
    ImGui::TextWrapped("%s", clipsDir_.empty() ? "(not chosen yet)" : Narrow(clipsDir_).c_str());
    if (ui::Button("Open##prefclips")) OpenInExplorer(clipsDir_);
    ImGui::SameLine();
    if (ui::Button("Change...##prefclips")) { ImGui::CloseCurrentPopup(); dirDialog_ = true; }   // one dialog at a time
    Section("BACKUPS");
    ImGui::TextWrapped("%s", Narrow(backupRoot_).c_str());
    QuietWrapped("The last 20 versions of each clip, copied before every save, outside the game folder.");
    if (ui::Button("Open##prefbackups")) OpenInExplorer(backupRoot_);
    Section("AT START");
    Settings cur;
    LoadSettings(settings_, cur);
    bool welcome = cur.Get("ui.welcomed") != "1";
    if (ui::Switch("##prefwelcome", &welcome, "Show the welcome and setup check")) SaveSetting(settings_, "ui.welcomed", welcome ? "0" : "1");
    if (DialogButtons({"Close"}, 0) == 0) ImGui::CloseCurrentPopup();
    EndDialog();
}

// redesign step 7: every key and mouse action in one place (the "?" buttons keep their short tooltips)
void App::ShortcutsDialog() {
    if (!BeginDialog("Keyboard shortcuts", "Keyboard and mouse", 44.f, 36.f)) return;
    const float fs = ImGui::GetFontSize();
    struct Row { const char* keys; const char* what; };
    auto Group = [&](const char* title, std::initializer_list<Row> rows) {
        Section(title);
        if (ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, fs * 11.f);
            ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
            for (const Row& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(theme::AccentHi, "%s", r.keys);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.what);
            }
            ImGui::EndTable();
        }
    };
    ImGui::BeginChild("##keyslist", ImVec2(0.f, -(ImGui::GetFrameHeightWithSpacing() + fs * 0.6f)));
    if (ImGui::BeginTable("##keyscols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        Group("EVERYWHERE", {{"Tab", "Browse / Animate"}, {"Ctrl+K", "Search (Browse)"}, {"Ctrl+N", "New clip"},
                             {"Ctrl+O", "Open a clip"}, {"Ctrl+S", "Save"}, {"Ctrl+Z", "Undo"},
                             {"Ctrl+Y, Ctrl+Shift+Z", "Redo"}, {"Ctrl+,", "Preferences"}, {"F1", "This list"}});
        Group("ANIMATE", {{"B", "Library drawer"}, {"1 to 7", "Body parts"}, {"I", "Rotate / Pull (IK)"},
                          {"F", "Center the view on the part"}, {"K", "Set a key"},
                          {"Ctrl+C / Ctrl+V", "Copy / paste (the selected keys, else the pose)"},
                          {"Del", "Delete the selected keys"}, {"Esc", "Deselect the keys"}});
        Group("PLAYBACK", {{"Space", "Play / stop"}, {"L", "Loop on / off"}, {"Left / Right", "Previous / next frame"},
                           {"Home / End", "Start / end of the clip"}});
        ImGui::TableNextColumn();
        Group("3D VIEW (MOUSE)", {{"Click", "Pick a body part or a bone"}, {"Drag a ring or arrow", "Rotate, pull or move it"},
                                  {"Right-click + drag", "Orbit the view"}, {"Middle-click + drag", "Pan the view"},
                                  {"Wheel", "Zoom"}});
        Group("TIMELINE (MOUSE)", {{"Click a key", "Pick it"}, {"Shift+click", "Add to the selection"},
                                   {"Shift+drag (empty part)", "Box selection"}, {"Drag a key", "Move it in time"},
                                   {"Right-click a key", "Delete, copy, paste, interpolation"},
                                   {"Click / drag the ruler", "Change the time"},
                                   {"Ctrl+wheel, wheel on ruler", "Zoom"}, {"Shift+wheel, middle-drag", "Scroll in time"}});
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (DialogButtons({"Close"}, 0) == 0) ImGui::CloseCurrentPopup();
    EndDialog();
}


void App::Shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    // redesign step 2: Tab switches the workspace, so ImGui's keyboard navigation must not also use it to move the
    // focus between buttons (Space would then press a focused button instead of playing). Text fields keep Tab.
    ImGui::SetKeyOwner(ImGuiKey_Tab, ImGui::GetID("##workspace_tab"));
    const bool ctrl = io.KeyCtrl;
    if (dragKey_ || ImGuizmo::IsUsing() || ImGui::IsAnyItemActive()) return;   // no undo in the middle of a drag
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        const bool ok = io.KeyShift ? doc_.Redo() : doc_.Undo();
        if (ok) AfterUndoRedo(); else Status(io.KeyShift ? "nothing to redo" : "nothing to undo");
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) { if (doc_.Redo()) AfterUndoRedo(); else Status("nothing to redo"); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Request(Ask::Save);
    // redesign step 2 (2026-11-27): new shortcuts
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) Request(Ask::New);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openDialog_ = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) { SetWorkspace(0); focusSearch_ = true; }   // redesign step 6: search
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) popPrefs_ = true;   // redesign step 7
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F1, false)) popKeys_ = true;
    if (!ctrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) SetWorkspace(ws_ == 0 ? 1 : 0);
    if (!ctrl && ws_ == 1 && ImGui::IsKeyPressed(ImGuiKey_B, false)) drawer_ = !drawer_;
    if (ws_ != 1) return;   // Browse: the editing keys below act on the 3D view and the timeline, not shown there
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) CommitKeyNow();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopyNow();    // 2026-11-26: keys if selected, else my pose
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteNow();
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !keySel_.empty()) DeleteSelectedKeys();   // lot 5
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !keySel_.empty()) keySel_.clear();
    for (int k = 0; k < kLimbs; k++)   // P6 lot 2: limbs
        if (!ctrl && (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k), false) || ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad1 + k), false))) SelectLimb(k);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) { ikMode_ = !ikMode_; Status(ikMode_ ? "IK mode: pull a hand, elbow, shoulder, foot or knee" : "bone by bone rotation mode"); }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false) && doc_.EffectiveDrive() != Drive::ManualRatio) { if (playing_) playing_ = false; else Request(Ask::Play); }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_L, false) && !doc_.LoopsInTime()) {   // 2026-11-25: editor loop
        loopPreview_ = !loopPreview_;
        SaveSetting(settings_, "timeline.loop", loopPreview_ ? "1" : "0");
        Status(loopPreview_ ? "playback loops" : "playback once");
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && doc_.EffectiveDrive() != Drive::ManualRatio) JumpShared(std::max(0.f, std::round(SharedT() / kFrame - 1.f) * kFrame));   // 2026-11-27: on the shared timeline
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && doc_.EffectiveDrive() != Drive::ManualRatio) JumpShared(std::round(SharedT() / kFrame + 1.f) * kFrame);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) CenterOnSelection();
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Home, false)) JumpShared(0.f);   // UI Phase 4
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_End, false)) JumpShared(hasComp_ ? Parts().Total() : doc_.clip().duration);
}
