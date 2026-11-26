// Session Studio -- confirmation and file dialogs, keyboard shortcuts.
#include "app_internal.h"

using namespace app_detail;

void App::Dialogs() {
    if (popPending_) { ImGui::OpenPopup("Pending change"); popPending_ = false; }
    if (popUnsaved_) { ImGui::OpenPopup("Unsaved clip"); popUnsaved_ = false; }
    if (popMode_) { ImGui::OpenPopup("Change mode"); popMode_ = false; }
    if (popOverwrite_) { ImGui::OpenPopup("Replace the file"); popOverwrite_ = false; }
    if (popCopy_) { ImGui::OpenPopup("Copy to another variant"); popCopy_ = false; }
    if (openDialog_) { ImGui::OpenPopup("Open a clip"); openDialog_ = false; }
    if (saveDialog_) { ImGui::OpenPopup("Save the clip"); saveDialog_ = false; }
    if (dirDialog_) { ImGui::OpenPopup("Clips folder"); dirDialog_ = false; strncpy_s(dirBuf_, Narrow(clipsDir_).c_str(), _TRUNCATE); }
    // UI Phase 2: Library file actions
    if (popRename_) { ImGui::OpenPopup("Rename the clip"); popRename_ = false; libErr_.clear(); }
    if (popDelete_) { ImGui::OpenPopup("Delete the clip"); popDelete_ = false; }
    if (popLock_) { ImGui::OpenPopup("Lock start and end"); popLock_ = false; }
    if (ImGui::BeginPopupModal("Lock start and end", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {   // 2026-11-26 (task 3)
        ImGui::TextWrapped("%s", "These parts are posed where the lock puts the game's pose back:");
        for (const EditorDoc::EndIssue& e : doc_.LockPreview()) {
            std::string parts;
            for (size_t i = 0; i < e.bones.size(); i++) parts += (i ? ", " : "") + BoneLabelUi(doc_.skeleton().names[e.bones[i]]);
            if (e.pelvis) parts += parts.empty() ? "Pelvis" : ", Pelvis";
            ImGui::BulletText("%s: %s", e.kind == EditorDoc::EndIssue::StartNotGame ? "at the start" : "at the end", parts.c_str());
        }
        ImGui::TextDisabled("%s", "The keys between them are kept. Ctrl+Z undoes it.");
        if (ImGui::Button("Reset them and lock")) { doc_.SetLockEnds(true); Status("start and end locked to the game's pose (Ctrl+Z undoes it)"); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Rename the clip", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("New name for %s:", libFile_.c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##name", renameBuf_, sizeof(renameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("Only the file name: what the clip does does not change.");
        if (!libErr_.empty()) ImGui::TextColored(kBad, "%s", libErr_.c_str());
        if (ImGui::Button("Rename") || enter) RenameClip(libFile_, renameBuf_);
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || popRenameClose_) { popRenameClose_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete the clip", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s?", libFile_.c_str());
        ImGui::TextWrapped("It will be removed from the game and moved to the backups\n(\"Backups\" button at the bottom): you can get it back.");
        if (libFile_ == fileName_) ImGui::TextColored(kAlert, "It is the open clip: it stays shown, as a clip not saved yet.");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.20f, 1.f));
        if (ImGui::Button("Delete")) { ImGui::CloseCurrentPopup(); DeleteClip(libFile_); }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Pending change", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("A bone was changed without setting a key (at %.2f s).", t_);
        if (ImGui::Button("Set the key")) {
            ImGui::CloseCurrentPopup();
            CommitKeyNow();
            if (!doc_.HasPending()) Request(ask_); else ask_ = Ask::None;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) { ImGui::CloseCurrentPopup(); doc_.Checkpoint("discard"); doc_.DiscardPending(); Request(ask_); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ask_ = Ask::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Unsaved clip", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(ask_ == Ask::Close ? "The clip has unsaved changes. Quit anyway?"
                                                  : "The clip has unsaved changes.");
        if (ImGui::Button("Save")) { ImGui::CloseCurrentPopup(); afterSave_ = true; SaveCurrent(); }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) { ImGui::CloseCurrentPopup(); Proceed(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); ask_ = Ask::None; }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Change mode", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The existing keys will be converted using the game's animation.\nThe pose shown does not change. Ctrl+Z undoes it.");
        if (ImGui::Button("Convert")) {
            if (askMode_ == Mode::Additive && !doc_.HasFigure()) Status("Add mode needs the game's animation: do the move once in game");
            else { doc_.Checkpoint("mode"); doc_.SetMode(askMode_); }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Replace the file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s already exists. Replace it?", overwriteName_.c_str());
        if (ImGui::Button("Replace")) {
            ImGui::CloseCurrentPopup();
            if (SaveAs(overwriteName_, true) && afterSave_) { afterSave_ = false; Proceed(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); afterSave_ = false; ask_ = Ask::None; }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Copy to another variant", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Copy of this clip for:");
        const int nSt = StateAllStances(var_.state) ? STANCE_Count : 2;
        for (int s = 0; s < nSt; s++) { if (s) ImGui::SameLine(); if (ImGui::RadioButton(StanceLabel(s), askVar_.stance == s)) askVar_.stance = s; }
        if (ImGui::RadioButton("Regular", !askVar_.goofy)) askVar_.goofy = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Goofy", askVar_.goofy)) askVar_.goofy = true;
        ImGui::TextDisabled("Left/right is swapped automatically when needed.");
        if (ImGui::Button("Copy") && askVar_ != var_) { ImGui::CloseCurrentPopup(); Request(Ask::CopyTo); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.f, ImGui::GetFontSize() * 22.f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open a clip", nullptr)) {
        for (int cap = 0; cap < 2; cap++) {
            ImGui::SeparatorText(cap ? "Captures (Ctrl+F4 in game)" : "All clips");
            for (auto& f : ListClips(cap == 1)) {
                if (ImGui::Selectable(f.c_str())) { askFile_ = f; ImGui::CloseCurrentPopup(); Request(Ask::Open); }
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Save the clip", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File name (in the game's clips folder):");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##file", nameBuf_, sizeof(nameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled(".sanim is added if missing");
        if (ImGui::Button("Save") || enter) {
            if (SaveAs(nameBuf_, false)) { ImGui::CloseCurrentPopup(); if (afterSave_) { afterSave_ = false; Proceed(); } }
            else if (popOverwrite_) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); afterSave_ = false; ask_ = Ask::None; }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Clips folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static std::string dirErr;   // shown inside the dialog (the status strip is hidden behind it)
        if (ImGui::IsWindowAppearing()) dirErr.clear();
        ImGui::TextUnformatted("The mod's clips folder, inside the game folder:");
        ImGui::TextDisabled("...\\steamapps\\common\\Session\\SessionGame\\Binaries\\Win64\\Mods\\SessionAnimLoader\\clips");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 36.f);
        ImGui::InputText("##folder", dirBuf_, sizeof(dirBuf_));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {   // Windows folder picker (UI Phase 1)
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
            ImGui::TextColored(kAlert, "This folder does not end with Mods\\SessionAnimLoader\\clips: the game will not read clips there.");
        if (!dirErr.empty()) ImGui::TextColored(kBad, "%s", dirErr.c_str());
        if (ImGui::Button("OK")) {
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
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::Shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const bool ctrl = io.KeyCtrl;
    if (dragKey_ || ImGuizmo::IsUsing() || ImGui::IsAnyItemActive()) return;   // no undo in the middle of a drag
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        const bool ok = io.KeyShift ? doc_.Redo() : doc_.Undo();
        if (ok) AfterUndoRedo(); else Status(io.KeyShift ? "nothing to redo" : "nothing to undo");
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) { if (doc_.Redo()) AfterUndoRedo(); else Status("nothing to redo"); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Request(Ask::Save);
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
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && doc_.EffectiveDrive() != Drive::ManualRatio) { askTime_ = std::max(0.f, std::round(t_ / kFrame - 1.f) * kFrame); Request(Ask::ChangeTime); }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && doc_.EffectiveDrive() != Drive::ManualRatio) { askTime_ = std::min(doc_.clip().duration, std::round(t_ / kFrame + 1.f) * kFrame); Request(Ask::ChangeTime); }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) CenterOnSelection();
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Home, false)) JumpTo(0.f);   // UI Phase 4
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_End, false)) JumpTo(doc_.clip().duration);
}
