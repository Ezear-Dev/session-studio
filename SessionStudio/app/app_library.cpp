// Session Studio -- Library panel: variant, tricks, clips, captures, clip file actions.
#include "app_internal.h"

using namespace app_detail;

// ------------------------------------------------------------------ UI Phase 2 (2026-11-25): Library
// Top: the variant (stance x feet) and whether the game's animation is recorded, always visible.
// Tabs: Tricks (what to animate), Clips (files, with enable / rename / duplicate / delete), Captures (Ctrl+F4 poses).
void App::TricksPanel() {
    const Layout L = Lay();
    Place(0, L.top, L.left, L.h - L.top - L.status - L.bottom - L.gap);
    ImGui::Begin("Library", nullptr, kPanel);
    if (clipsDir_.empty()) {   // empty state: nothing can be listed without the game's clips folder
        ImGui::TextWrapped("The game's clips folder is not chosen yet.");
        ImGui::TextDisabled("...\\Win64\\Mods\\SessionAnimLoader\\clips");
        if (ImGui::Button("Choose the folder...")) dirDialog_ = true;
        ImGui::End();
        return;
    }
    auto pick = [&](const FigureVariant& v) {
        if (v == var_) return;
        askVar_ = v;
        Request(Ask::ChangeFigure);
    };

    // ---- variant
    ImGui::SeparatorText("Variant");
    const int nSt = StateAllStances(var_.state) ? STANCE_Count : 2;   // manuals, grinds...: normal / switch only
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Stance");
    ImGui::SameLine(ImGui::GetFontSize() * 3.6f);
    const float indentX = ImGui::GetFontSize() * 3.6f;
    for (int s = 0; s < nSt; s++) {
        FigureVariant v = var_; v.stance = s;
        const bool on = var_.stance == s;
        if (s) {   // wrap to a new line when the panel is too narrow for the next button
            const ImGuiStyle& st = ImGui::GetStyle();
            const float w = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(StanceLabel(s)).x;
            const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= right) ImGui::SameLine();
            else ImGui::SetCursorPosX(indentX);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, FigureReady(v) ? kOk : (on ? ImVec4(1, 1, 1, 1) : kDim));
        if (ImGui::RadioButton(StanceLabel(s), on)) pick(v);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            static const char* kTip[4] = {"Normal: the skater's natural stance.", "Switch: the opposite stance (the other foot forward).",
                                          "Fakie: rolling backwards, natural stance.", "Nollie: pop with the front foot, on the nose (the front of the board)."};
            ImGui::SetTooltip("%s", kTip[s & 3]);
        }
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Foot");
    ImGui::SameLine(ImGui::GetFontSize() * 3.6f);
    for (int g = 0; g < 2; g++) {
        FigureVariant v = var_; v.goofy = g == 1;
        if (g) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, FigureReady(v) ? kOk : (var_.goofy == (g == 1) ? ImVec4(1, 1, 1, 1) : kDim));
        if (ImGui::RadioButton(g ? "Goofy" : "Regular", var_.goofy == (g == 1))) pick(v);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(g ? "Goofy: right foot forward." : "Regular: left foot forward.");
    }
    if (FigureReady(var_)) {
        Badge("Game animation ready", kGood);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's movement for this variant is recorded: you can edit it.\nGreen in the lists: variants already recorded.");
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y * 0.5f);
        if (ImGui::SmallButton(FigureRedo(var_) ? "redo in game" : "record again")) RequestRerecord(var_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next time you do this move in game, its movement is recorded again.");
    } else {
        Badge("Do it once in game", kAlert);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Do \"%s\" once in game (mod installed):\nthe mod records the game's movement by itself, then you can edit it here.", VariantTitleUi(var_).c_str());
    }

    // ---- tabs
    int nClips = 0;
    for (auto& ci : clips_) if (ClipTargets(ci.clip, var_)) nClips++;
    const std::string clipsTab = "Clips (" + std::to_string(clipsAll_ ? (int)clips_.size() : nClips) + ")###clips";
    const std::string capTab = "Captures (" + std::to_string(captures_.size()) + ")###captures";
    ImGui::Spacing();
    if (ImGui::BeginTabBar("##biblio")) {
        // the last tab used comes back at the next launch (settings "library.tab")
        auto flags = [&](int k) { return libTabRestore_ && libTab_ == k ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        int now = libTab_;
        if (ImGui::BeginTabItem("Tricks", nullptr, flags(0))) { now = 0; LibraryTricks(pick); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(clipsTab.c_str(), nullptr, flags(1))) { now = 1; LibraryClips(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(capTab.c_str(), nullptr, flags(2))) { now = 2; LibraryCaptures(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        // SetSelected takes effect a frame later: keep asking until the saved tab is really shown
        if (libTabRestore_) { if (now == libTab_ || ++libTabFrames_ > 10) libTabRestore_ = false; }
        else if (now != libTab_) { libTab_ = now; SaveSetting(settings_, "library.tab", std::to_string(now)); }
    }
    ImGui::End();
}

void App::LibraryTricks(const std::function<void(const FigureVariant&)>& pick) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##cherche", "search a trick...", search_, sizeof(search_));
    const std::string q = Lower(search_);
    ImGui::BeginChild("##liste", ImVec2(0, 0), ImGuiChildFlags_None);
    auto has = [&](const FigureVariant& base) {   // variants recorded
        int rec = 0;
        for (int s = 0; s < STANCE_Count; s++) for (int g = 0; g < 2; g++) { FigureVariant v = base; v.stance = s; v.goofy = g == 1; if (FigureReady(v)) rec++; }
        return rec;
    };
    auto clipCount = [&](const FigureVariant& base) {
        int n = 0;
        for (auto& ci : clips_) {
            if (!(ci.clip.states & base.state)) continue;
            if (StateNamed(base.state) && ci.clip.trickHash && ci.clip.trickHash != TrickHash(base.trick)) continue;
            n++;
        }
        return n;
    };
    auto line = [&](const FigureVariant& base, const std::string& title, const char* note) {
        if (!q.empty() && Lower(title).find(q) == std::string::npos) return;
        const bool cur = base.state == var_.state && (!StateNamed(base.state) || Lower(base.trick) == Lower(var_.trick));
        const int nc = clipCount(base), nr = has(base);
        std::string lab = title;
        if (note) lab += std::string("  (") + note + ")";
        if (nc) lab += "  [" + std::to_string(nc) + " clip" + (nc > 1 ? "s" : "") + "]";
        ImGui::PushID(title.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, nr ? kOk : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (ImGui::Selectable(lab.c_str(), cur)) {
            FigureVariant v = base;
            v.stance = var_.stance; v.goofy = var_.goofy;
            if (!StateAllStances(v.state) && v.stance > STANCE_Switch) v.stance = STANCE_Normal;
            pick(v);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%d variant(s) recorded in game, %d clip(s)%s", title.c_str(), nr, nc,
                              note ? "\nIn-game detection not checked yet: the clip may never play." : "");
        ImGui::PopID();
    };
    if (q.empty()) ImGui::TextDisabled("On the ground");
    // 2026-11-25 (user): grabs set aside; the powerslide is still to be tried in game
    for (uint32_t s : {ST_OnFoot, ST_Walking, ST_Running, ST_Riding, ST_Pushing, ST_Crouch, ST_Landing, ST_Manual, ST_NoseManual, ST_Powerslide}) {
        FigureVariant v; v.state = s;
        line(v, StateTitle(s), s == ST_Powerslide ? "not checked" : nullptr);
    }
    // 2026-11-25: grinds, one line per grind the game named (crooked, smith, feeble...): recorded in game or in a clip.
    // PARKED by the user the same night: _replicatedGrindName stays empty offline, every grind is one "Grind" (kept hidden)
    constexpr bool kShowGrinds = false;
    if (kShowGrinds) {
        std::map<std::string, std::string> grinds;   // lower -> name as the game writes it
        for (auto& key : figHave_) { FigureVariant v; if (ParseFigureKey(key, v) && v.state == ST_Grind && !v.trick.empty()) grinds.emplace(Lower(v.trick), v.trick); }
        for (auto& ci : clips_) if ((ci.clip.states & ST_Grind) && !ci.clip.trick.empty()) grinds.emplace(Lower(ci.clip.trick), ci.clip.trick);
        if (q.empty()) { ImGui::Spacing(); ImGui::TextDisabled("Grinds"); }
        for (auto& g : grinds) { FigureVariant v; v.state = ST_Grind; v.trick = g.second; line(v, TrickDisplay(g.second), nullptr); }
        if (grinds.empty() && q.empty()) ImGui::TextDisabled("Do a grind in game: it shows up here,\none per type (crooked, smith, feeble...).");
    }
    std::string cat;
    for (auto& t : tricks_) {
        if (t.category != cat) { cat = t.category; if (q.empty()) { ImGui::Spacing(); ImGui::TextDisabled("%s", cat); } }
        FigureVariant v; v.state = ST_Trick; v.trick = t.base;
        line(v, TrickDisplay(t.base), nullptr);
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Green: already done in game (movement recorded). A trick missing from the list shows up as soon as you do it in game.");
    ImGui::EndChild();
}

void App::LibraryClips() {
    if (ImGui::Button("+ New clip for this variant")) Request(Ask::New);
    if (ImGui::RadioButton("This variant", !clipsAll_) && clipsAll_) { clipsAll_ = false; SaveSetting(settings_, "library.all", "0"); }
    ImGui::SameLine();
    if (ImGui::RadioButton("All clips", clipsAll_) && !clipsAll_) { clipsAll_ = true; SaveSetting(settings_, "library.all", "1"); }
    ImGui::BeginChild("##clips", ImVec2(0, 0), ImGuiChildFlags_None);
    int shown = 0;
    const float fs = ImGui::GetFontSize();
    for (auto& ci : clips_) {
        if (!clipsAll_ && ci.err.empty() && !ClipTargets(ci.clip, var_)) continue;   // broken files: always shown
        shown++;
        const Clip& c = ci.clip;
        const bool open = ci.file == fileName_;
        ImGui::PushID(ci.file.c_str());
        if (!ci.err.empty()) {   // a file the game refuses too: say so, offer the menu (delete), nothing else
            ImGui::TextColored(kBad, "%s", ci.file.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unreadable file: the game refuses it too.\n%s", ci.err.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("...")) ImGui::OpenPopup("##menu");
            if (ImGui::BeginPopup("##menu")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
            ImGui::TextColored(kBad, "  unreadable: %s", ci.err.c_str());
            ImGui::PopID();
            continue;
        }
        bool en = c.enabled;
        if (ImGui::Checkbox("##on", &en)) ToggleClipEnabled(ci.file, en);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(c.enabled ? "On: plays in game. Untick to turn it off without deleting it." : "Off: does not play in game. Tick to turn it on.");
        ImGui::SameLine();
        const std::string stem = ci.file.size() > 6 ? ci.file.substr(0, ci.file.size() - 6) : ci.file;
        const float menuW = fs * 1.8f;
        ImGui::PushStyleColor(ImGuiCol_Text, c.enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kDim);
        if (ImGui::Selectable((stem + (open ? "  (open)" : "")).c_str(), open, 0, ImVec2(std::max(fs * 4.f, ImGui::GetContentRegionAvail().x - menuW), 0))) {
            if (!open) { askFile_ = ci.file; Request(Ask::Open); }
        }
        ImGui::PopStyleColor();
        const bool noEffect = ClipHasNoEffect(c);
        if (ImGui::IsItemHovered()) {
            std::string tip = ci.file + "\nFor: " + VariantTitleUi(VariantOfClip(c));
            tip += std::string("\nMode: ") + (c.mode == Mode::Additive ? "add (adds to the game's movement)" : "replace (forces the pose)");
            tip += "\nLength: " + std::to_string(c.duration).substr(0, 4) + " s";
            if (c.feet == Feet::Regular && !c.mirror) tip += "\nRegular only: does not play for a goofy skater (Copy to another variant to make a goofy version).";
            if (noEffect) tip += "\n\nWARNING: this clip changes nothing in game (no rotation, no offset).";
            tip += "\n\nClick: open · \"...\" or right-click: rename, duplicate, delete";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (ImGui::BeginPopupContextItem("##ctx")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("...")) ImGui::OpenPopup("##menu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rename, duplicate, turn on, delete");
        if (ImGui::BeginPopup("##menu")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
        // second line: what it targets, and problems
        std::string sub = clipsAll_ ? VariantTitleUi(VariantOfClip(c)) : std::string(c.mode == Mode::Additive ? "add" : "replace");
        if (!c.enabled) sub += " · off";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
        if (noEffect) { ImGui::TextColored(kWarn, "%s · changes nothing!", sub.c_str()); }
        else ImGui::TextDisabled("%s", sub.c_str());
        ImGui::PopID();
    }
    if (!shown) {
        ImGui::Spacing();
        ImGui::TextWrapped(clipsAll_ ? "No clip in the game folder." : "No clip for this variant. \"+ New clip\" makes one; Ctrl+S saves it into the game.");
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Delete moves the file to the backups (recoverable).");
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void App::LibraryCaptures() {
    ImGui::TextWrapped("Ctrl+F4 in game freezes the skater's pose here. Opening a capture makes a new clip from it.");
    ImGui::BeginChild("##caps", ImVec2(0, 0), ImGuiChildFlags_None);
    const float fs = ImGui::GetFontSize();
    for (auto& f : captures_) {
        ImGui::PushID(f.c_str());
        std::string stem = f.substr(f.find('\\') + 1);
        if (stem.size() > 6) stem = stem.substr(0, stem.size() - 6);
        if (ImGui::Selectable(stem.c_str(), false, 0, ImVec2(std::max(fs * 4.f, ImGui::GetContentRegionAvail().x - fs * 1.8f), 0))) { askFile_ = f; Request(Ask::Open); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click: open as a new clip (the capture itself does not change).");
        if (ImGui::BeginPopupContextItem("##ctx")) { ClipMenuItems(f, true); ImGui::EndPopup(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("...")) ImGui::OpenPopup("##menu");
        if (ImGui::BeginPopup("##menu")) { ClipMenuItems(f, true); ImGui::EndPopup(); }
        ImGui::PopID();
    }
    if (captures_.empty()) ImGui::TextDisabled("No capture yet.");
    ImGui::EndChild();
}

void App::ClipMenuItems(const std::string& file, bool capture) {
    ImGui::TextDisabled("%s", file.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem(capture ? "Open as a new clip" : "Open", nullptr, false, file != fileName_)) { askFile_ = file; Request(Ask::Open); }
    if (!capture) {
        const ClipInfo* ci = nullptr;
        for (auto& c : clips_) if (c.file == file) ci = &c;
        if (ci && ci->err.empty() && ImGui::MenuItem(ci->clip.enabled ? "Turn off in game" : "Turn on in game")) ToggleClipEnabled(file, !ci->clip.enabled);
        if (ImGui::MenuItem("Rename...")) { libFile_ = file; strncpy_s(renameBuf_, file.substr(0, file.size() - 6).c_str(), _TRUNCATE); popRename_ = true; }
        if (ImGui::MenuItem("Duplicate")) DuplicateClip(file);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A turned-off copy, to try a variation without touching the original.");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete...")) { libFile_ = file; popDelete_ = true; }
}

void App::ToggleClipEnabled(const std::string& file, bool on) {
    if (file == fileName_ && (Dirty() || doc_.HasPending())) {
        Status("save this clip first (Ctrl+S), or use the \"On in game\" box on the right");
        return;
    }
    std::string err;
    if (!SetClipEnabledFile(clipsDir_ + L"\\" + Widen(file), on, backupRoot_, err)) { Status(file + ": " + err); return; }
    if (file == fileName_) { doc_.clip().enabled = on; savedText_ = ClipToJson(doc_.clip()); }
    scanAge_ = 99.f;
    Status(file + (on ? " on: it plays in game" : " off: it no longer plays in game"));
}

void App::DuplicateClip(const std::string& file) {
    const std::string stem = file.substr(0, file.size() - 6);
    const std::string dst = UniqueClipName(clipsDir_, stem, "_copie");
    std::string err;
    if (!DuplicateClipFile(clipsDir_ + L"\\" + Widen(file), clipsDir_ + L"\\" + Widen(dst), err)) { Status("cannot copy: " + err); return; }
    scanAge_ = 99.f;
    Status("copy made: " + dst + " (off)");
}

void App::RenameClip(const std::string& file, const std::string& raw) {
    std::string name = raw, err;
    if (!NormalizeClipName(name, err)) { libErr_ = err; return; }
    if (name == file) { popRenameClose_ = true; return; }
    if (!RenameClipFile(clipsDir_ + L"\\" + Widen(file), clipsDir_ + L"\\" + Widen(name), err)) { libErr_ = err; return; }
    if (file == fileName_) { fileName_ = name; doc_.clip().file = name; }
    if (lastSave_.file == file) lastSave_.file = name;
    scanAge_ = 99.f;
    popRenameClose_ = true;
    Status("renamed: " + file + " -> " + name);
}

void App::DeleteClip(const std::string& file) {
    const std::wstring path = clipsDir_ + L"\\" + Widen(file);
    std::wstring dest;
    std::string err;
    if (!MoveToBackups(path, backupRoot_, "supprime", BackupStamp(), &dest, err)) { Status("cannot delete: " + err); return; }
    if (file == fileName_) {   // the document stays open, as a clip that is no longer on disk
        fileName_.clear();
        doc_.clip().file.clear();
        savedText_.clear();
    }
    scanAge_ = 99.f;
    Status("« " + file + "\" removed from the game, moved to the backups: " + Narrow(dest));
}
