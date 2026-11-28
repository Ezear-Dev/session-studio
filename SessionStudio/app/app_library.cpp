// Session Studio -- the Library drawer of Animate (B), and the clip file actions shared with Browse.
#include "app_internal.h"

using namespace app_detail;

// ------------------------------------------------------------------ the Library drawer
// UI Phase 2 (2026-11-25) made it the Library panel; redesign step 2 turned it into the Animate drawer, step 6 gave
// Browse its own page. 2026-11-27 (user: "make it look more like Browse"): the same pieces as Browse. Top: the move
// being edited with Browse's variant grid; below, a segmented switch Tricks / Clips / Captures. Tricks are rows with
// the stance x feet dots of the Browse cards; clips are rows with an on / off switch like the Browse detail.
namespace {
void LibCaps(const char* text) {
    ImGui::Spacing();
    ImGui::TextColored(theme::Text3, "%s", text);
}

void LibTitle(const char* text, float scale) {
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * scale);
    ImGui::TextUnformatted(text);
    if (theme::Strong()) ImGui::PopFont();
}
} // namespace

void App::TricksPanel() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    Place(L.lib.x, L.lib.y, L.lib.w, L.lib.h);
    static int lastFrame = -10;   // brought to the front when it opens
    if (ImGui::GetFrameCount() != lastFrame + 1) ImGui::SetNextWindowFocus();
    lastFrame = ImGui::GetFrameCount();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Bg1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.9f, fs * 0.8f));
    ImGui::Begin("##library", nullptr, (kPanel & ~ImGuiWindowFlags_NoBringToFrontOnFocus) | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ---- header: the title and a close button
    ImGui::AlignTextToFramePadding();
    LibTitle("Library", 1.2f);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    if (ui::IconButton("libclose", theme::Icon::Close, "x", false, ui::Kind::Ghost)) drawer_ = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close the Library (B)");

    if (clipsDir_.empty()) {   // empty state: nothing can be listed without the game's clips folder
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(theme::Text2, "%s", "The game's clips folder is not chosen yet.");
        ImGui::TextColored(theme::Text3, "%s", "...\\Win64\\Mods\\SessionAnimLoader\\clips");
        ImGui::PopTextWrapPos();
        if (ui::Button("Choose the folder...", ui::Kind::Primary)) dirDialog_ = true;
        ImGui::End();
        return;
    }
    const FigureVariant shown = ShownVar();
    auto pick = [&](const FigureVariant& v) {
        if (v == shown) return;
        askVar_ = v;
        Request(Ask::ChangeFigure);
    };
    std::vector<const Clip*> clipPtrs;
    const std::vector<LibCard> cards = LibraryCards(clipPtrs);

    // ---- the move being edited: its variant grid (click = that variant), recorded or not
    const LibCard* card = nullptr;
    for (auto& c : cards) if (CardHolds(c, shown)) card = &c;
    ImGui::Spacing();
    LibTitle(card ? card->title.c_str() : VariantTitleUi(shown).c_str(), 1.1f);
    if (card) {
        ImGui::TextColored(theme::Text3, "%s  ·  recorded in %d of %d variants", card->category.c_str(), card->recorded, card->variants);
        ImGui::Spacing();
        bool click = false;
        FigureVariant cv;
        VariantGrid(*card, shown, nullptr, "Click: edit this variant", &click, &cv, nullptr, nullptr);
        if (click) pick(cv);
    }
    if (FigureReady(shown)) {
        ImGui::TextColored(theme::Good, "%s", "Game movement recorded");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's movement for this variant is recorded: you can edit it.");
        ImGui::SameLine();
        if (ImGui::SmallButton(FigureRedo(shown) ? "redo in game" : "record again")) RequestRerecord(shown);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next time you do this move in game, its movement is recorded again.");
    } else {
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(theme::Warn, "Do \"%s\" once in game: the mod records it by itself.", VariantTitleUi(shown).c_str());
        ImGui::PopTextWrapPos();
    }

    // ---- Tricks / Clips / Captures
    int nClips = 0;
    for (auto& ci : clips_) if (ClipTargets(ci.clip, shown)) nClips++;
    const std::string clipsLab = "Clips " + std::to_string(clipsAll_ ? (int)clips_.size() : nClips);
    const std::string capsLab = "Captures " + std::to_string(captures_.size());
    const char* tabs[3] = {"Tricks", clipsLab.c_str(), capsLab.c_str()};
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ui::Segmented("##libtab", tabs, 3, &libTab_)) SaveSetting(settings_, "library.tab", std::to_string(libTab_));
    ImGui::Spacing();
    if (libTab_ == 0) LibraryTricks(cards, pick);
    else if (libTab_ == 1) LibraryClips();
    else LibraryCaptures();
    ImGui::End();
}

// Organised like Browse (user 2026-11-27): the same collections (Everything, then each category, with counts), the same
// "Recorded" / "Has clips" filters and the same cards. The collection and the filters are Browse's own (one choice for
// both places). Click a card = animate that move in the stance and foot being edited; a dot = that exact variant.
void App::LibraryTricks(const std::vector<LibCard>& cards, const std::function<void(const FigureVariant&)>& pick) {
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##cherche", "Search a trick...", search_, sizeof(search_));
    // the collection: Browse's sidebar as a list box (My clips and Captures have their own tabs here)
    std::vector<std::string> cats;
    for (auto& c : cards) if (std::find(cats.begin(), cats.end(), c.category) == cats.end()) cats.push_back(c.category);
    const std::string cat = (!browseCat_.empty() && browseCat_[0] == '#') ? std::string() : browseCat_;
    auto label = [&](const std::string& k) {
        int n = 0;
        for (auto& c : cards) n += k.empty() || c.category == k;
        return (k.empty() ? std::string("Everything") : k) + "  (" + std::to_string(n) + ")";
    };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##collection", label(cat).c_str())) {
        if (ImGui::Selectable(label("").c_str(), cat.empty())) { browseCat_.clear(); SaveSetting(settings_, "browse.view", ""); }
        for (auto& k : cats)
            if (ImGui::Selectable(label(k).c_str(), cat == k)) { browseCat_ = k; SaveSetting(settings_, "browse.view", k); }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Which tricks and states to show (the same choice as in Browse).");
    if (ui::Toggle("Recorded##only", browseRecOnly_)) browseRecOnly_ = !browseRecOnly_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only what you already did in game (at least one variant recorded).");
    ImGui::SameLine();
    if (ui::Toggle("Has clips##only", browseClipsOnly_)) browseClipsOnly_ = !browseClipsOnly_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only what has at least one clip of yours.");
    ImGui::Spacing();

    LibFilter f;
    f.search = search_;
    f.category = cat;
    f.recordedOnly = browseRecOnly_;
    f.withClipsOnly = browseClipsOnly_;
    ImGui::BeginChild("##liste", ImVec2(0, 0), ImGuiChildFlags_None);
    // 2026-11-25 (user): grabs set aside; grinds parked (one shared recording), so neither is listed
    const float gap = std::round(fs * 0.6f), avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)((avail + gap) / (fs * 9.f + gap)));
    const float cw = std::floor((avail - gap * (cols - 1)) / cols), ch = std::round(fs * 6.6f);
    std::string shownCat;
    int col = 0, shown = 0;
    for (size_t i = 0; i < cards.size(); i++) {
        const LibCard& c = cards[i];
        if (!CardMatches(c, f)) continue;
        shown++;
        if (cat.empty() && !search_[0] && c.category != shownCat) {   // a quiet title per category, as in the Browse sidebar
            shownCat = c.category;
            col = 0;
            LibCaps(shownCat.c_str());
        }
        if (col) ImGui::SameLine(0, gap);
        ImGui::PushID((int)i);
        FigureVariant dv;
        const int r = LibCardUi(c, ImVec2(cw, ch), ShownVar(), CardHolds(c, ShownVar()), "Click: animate it (same stance and foot) · a dot: that variant", &dv);
        if (r == 1) {
            int stance = ShownVar().stance;   // keep the stance and the foot being edited
            if (!StateAllStances(c.base.state) && stance > STANCE_Switch) stance = STANCE_Normal;
            FigureVariant v = c.cell[stance][ShownVar().goofy ? 1 : 0].v;
            pick(v);
        } else if (r == 3) pick(dv);
        ImGui::PopID();
        col = (col + 1) % cols;
        if (col == 0) ImGui::Dummy(ImVec2(0, gap * 0.25f));
    }
    if (!shown) ImGui::TextColored(theme::Text2, "%s", "Nothing matches: try another name, or turn the filters off.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextColored(theme::Text3, "%s", "Dots: one per stance (columns) and foot (rows). Green: recorded in game. Orange centre: a clip of yours is on. White ring: what you are editing. A trick missing from the list shows up as soon as you do it in game.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void App::LibraryClips() {
    const float fs = ImGui::GetFontSize();
    static const char* kScope[2] = {"This variant", "All clips"};
    int scope = clipsAll_ ? 1 : 0;
    if (ui::Segmented("##clipscope", kScope, 2, &scope)) { clipsAll_ = scope == 1; SaveSetting(settings_, "library.all", clipsAll_ ? "1" : "0"); }
    if (ui::Button("+ New clip for this variant", ui::Kind::Normal, ImVec2(-1, 0))) Request(Ask::New);
    ImGui::Spacing();
    ImGui::BeginChild("##clips", ImVec2(0, 0), ImGuiChildFlags_None);
    int shown = 0;
    for (auto& ci : clips_) {
        if (!clipsAll_ && ci.err.empty() && !ClipTargets(ci.clip, ShownVar())) continue;   // broken files: always shown
        shown++;
        const Clip& c = ci.clip;
        const bool open = ci.file == fileName_ || (hasComp_ && ci.file == comp_.fileName);
        ImGui::PushID(ci.file.c_str());
        const std::string stem = ci.file.size() > 6 ? ci.file.substr(0, ci.file.size() - 6) : ci.file;
        if (!ci.err.empty()) {   // a file the game refuses too: say so, offer the menu (delete), nothing else
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::Bad, "%s", stem.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unreadable file: the game refuses it too.\n%s", ci.err.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
            if (ui::IconButton("menu", theme::Icon::More, "...", false, ui::Kind::Ghost)) ImGui::OpenPopup("##menu");
            if (ImGui::BeginPopup("##menu")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextColored(theme::Bad, "unreadable: %s", ci.err.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopID();
            continue;
        }
        bool en = c.enabled;
        if (ui::Switch("##on", &en)) ToggleClipEnabled(ci.file, en);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(c.enabled ? "On: plays in game. Switch off to keep it without playing it." : "Off: does not play in game.");
        ImGui::SameLine();
        const float menuW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushStyleColor(ImGuiCol_Text, open ? theme::AccentHi : c.enabled ? theme::Text : theme::Text3);
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
        if (ui::IconButton("menu", theme::Icon::More, "...", false, ui::Kind::Ghost)) ImGui::OpenPopup("##menu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rename, duplicate, turn on, delete");
        if (ImGui::BeginPopup("##menu")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
        // second line: what it targets, and problems
        std::string sub = clipsAll_ ? VariantTitleUi(VariantOfClip(c)) + " · " : std::string();
        const std::string note = LandingNote(ci.file);
        if (!note.empty()) sub += note + " · ";
        sub += c.mode == Mode::Additive ? "add" : "replace";
        char len[16];
        snprintf(len, sizeof(len), " · %.2f s", c.duration);
        sub += len;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFrameHeight() * 1.8f + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (noEffect) ImGui::TextColored(theme::Warn, "%s · changes nothing!", sub.c_str());
        else ImGui::TextColored(theme::Text3, "%s", sub.c_str());
        ImGui::Dummy(ImVec2(0, fs * 0.15f));
        ImGui::PopID();
    }
    if (!clipsAll_ && ShownVar().state == ST_Trick) GameDrawRow(ShownVar(), DrawFor(ClipPtrs(), ShownVar(), gameDraw_));
    if (!shown) {
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(theme::Text2, "%s", clipsAll_ ? "No clip in the game folder." : "No clip for this variant yet. \"+ New clip\" makes one; Ctrl+S saves it into the game.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::TextColored(theme::Text3, "%s", "Delete moves the file to the backups.");
    ImGui::EndChild();
}

// v0.31: the clips of a trick that are on take turns at random; this row lets the game's own animation join them. Drawn
// like a clip row (same switch), under the clips of the figure, in the drawer and in Browse
void App::GameDrawRow(const FigureVariant& v, const FigureDraw& d) {
    const float fs = ImGui::GetFontSize();
    ImGui::PushID("##gamedraw");
    const bool chosen = HasGameDraw(gameDraw_, v);
    bool on = chosen;
    ImGui::BeginDisabled(!gameDrawErr_.empty());
    if (ui::Switch("##on", &on)) ToggleGameDraw(v, on);
    ImGui::EndDisabled();
    const char* tip = "On: the game's own animation is drawn too, as often as each clip that is on.\nWhen it comes out, no clip of yours plays on the trick, and no landing of these clips.";
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(chosen ? theme::Text : theme::Text3, "%s", "Game animation");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::PushTextWrapPos(0.f);
    if (!gameDrawErr_.empty()) ImGui::TextColored(theme::Warn, "%s", gameDrawErr_.c_str());
    else if (chosen && !d.game) ImGui::TextColored(theme::Text3, "%s", "Switch on a clip of this trick to draw between them.");
    else if (d.Choices() >= 2) ImGui::TextColored(theme::Accent, "At each pop, one of %d is drawn at random, never the same twice in a row.", d.Choices());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, fs * 0.15f));
    ImGui::PopID();
}

void App::ToggleGameDraw(const FigureVariant& v, bool on) {
    std::string err;
    if (!SetGameDrawFile(clipsDir_, v, on, err)) { Status(err); return; }
    ReadGameDrawFile(clipsDir_, gameDraw_, gameDrawErr_);
    scanAge_ = 99.f;
    Status(on ? "the game's own animation joins the draw of this trick" : "the game's own animation is out of the draw of this trick");
}

void App::LibraryCaptures() {
    const float fs = ImGui::GetFontSize();
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextColored(theme::Text2, "%s", "Ctrl+F4 in game freezes the skater's pose here. Opening a capture makes a new clip from it.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::BeginChild("##caps", ImVec2(0, 0), ImGuiChildFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = std::round(fs * 2.4f), gap = std::round(fs * 0.4f);
    for (auto& f : captures_) {   // small cards, as in Browse > Captures
        ImGui::PushID(f.c_str());
        std::string stem = f.substr(f.find('\\') + 1);
        if (stem.size() > 6) stem = stem.substr(0, stem.size() - 6);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##cap", ImVec2(w, h))) { askFile_ = f; Request(Ask::Open); }
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetTooltip("%s\nClick: open as a new clip (the capture itself does not change) · right-click: delete", stem.c_str());
        if (ImGui::BeginPopupContextItem("##ctx")) { ClipMenuItems(f, true); ImGui::EndPopup(); }
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::U32(hov ? theme::Raised : theme::Surface), fs * 0.5f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), theme::U32(theme::Line), fs * 0.5f);
        if (theme::HasIcons()) dl->AddText(ImVec2(p.x + fs * 0.7f, p.y + (h - fs) * 0.5f), theme::U32(theme::Text3), theme::Glyph(theme::Icon::Camera).c_str());
        dl->PushClipRect(p, ImVec2(p.x + w - fs * 0.5f, p.y + h), true);
        dl->AddText(ImVec2(p.x + fs * 2.2f, p.y + (h - fs) * 0.5f), theme::U32(theme::Text), stem.c_str());
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        ImGui::PopID();
    }
    if (captures_.empty()) ImGui::TextColored(theme::Text3, "%s", "No capture yet: Ctrl+F4 in game.");
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
    bool isTrick = false;
    for (auto& ci : clips_) if (ci.file == file && ci.err.empty()) isTrick = (ci.clip.states & ST_Trick) && ci.clip.trickHash;
    const std::vector<std::string> landings = LandingFilesOf(file);
    const std::string id = isTrick ? NewLinkId() : std::string();   // the copy of a trick clip is a new trick clip
    if (!DuplicateClipFile(clipsDir_ + L"\\" + Widen(file), clipsDir_ + L"\\" + Widen(dst), err, isTrick ? &id : nullptr)) { Status("cannot copy: " + err); return; }
    std::string with;
    for (const std::string& l : landings) {   // its landing is copied with it (it plays only after the copy)
        const std::string name = LandingName(dst);
        if (!DuplicateClipFile(clipsDir_ + L"\\" + Widen(l), clipsDir_ + L"\\" + Widen(name), err, &id, false)) { Status("copy made, but not its landing " + l + ": " + err); scanAge_ = 99.f; return; }
        with = " with its landing";
    }
    scanAge_ = 99.f;
    Status("copy made: " + dst + " (off)" + with);
}

void App::RenameClip(const std::string& file, const std::string& raw) {
    std::string name = raw, err;
    if (!NormalizeClipName(name, err)) { libErr_ = err; return; }
    if (name == file) { popRenameClose_ = true; return; }
    const std::vector<std::string> landings = LandingFilesOf(file);
    if (!RenameClipFile(clipsDir_ + L"\\" + Widen(file), clipsDir_ + L"\\" + Widen(name), err)) { libErr_ = err; return; }
    auto renamed = [&](const std::string& from, const std::string& to) {
        if (fileName_ == from) { fileName_ = to; doc_.clip().file = to; }
        if (hasComp_ && comp_.fileName == from) { comp_.fileName = to; comp_.doc.clip().file = to; }
        if (lastSave_.file == from) lastSave_.file = to;
    };
    renamed(file, name);
    // a landing still named after the trick clip follows the new name (the link itself does not depend on names)
    const std::string oldLanding = "Landing-" + file, newLanding = "Landing-" + name;
    for (const std::string& l : landings)
        if (l == oldLanding && RenameClipFile(clipsDir_ + L"\\" + Widen(l), clipsDir_ + L"\\" + Widen(newLanding), err)) renamed(l, newLanding);
    scanAge_ = 99.f;
    popRenameClose_ = true;
    Status("renamed: " + file + " -> " + name);
}

void App::DeleteClip(const std::string& file) {
    RefreshClips();
    const std::vector<std::string> landings = LandingFilesOf(file);   // read before the trick clip is gone
    std::wstring dest;
    std::string err;
    if (!MoveToBackups(clipsDir_ + L"\\" + Widen(file), backupRoot_, "supprime", BackupStamp(), &dest, err)) { Status("cannot delete: " + err); return; }
    std::string with;
    std::vector<std::string> gone{file};
    for (const std::string& l : landings) {
        if (!MoveToBackups(clipsDir_ + L"\\" + Widen(l), backupRoot_, "supprime", BackupStamp(), nullptr, err)) { with += " -- WARNING, its landing " + l + " is still there: " + err; continue; }
        gone.push_back(l);
        with += " (and its landing " + l + ")";
    }
    bool openTrickGone = false, openLandingGone = false;
    const std::string trickSlot = hasComp_ && inLanding_ ? comp_.fileName : fileName_;
    const std::string landSlot = !hasComp_ ? std::string() : inLanding_ ? fileName_ : comp_.fileName;
    for (const std::string& g : gone) {
        if (g == trickSlot) openTrickGone = true;
        if (g == landSlot) openLandingGone = true;
        if (g == fileName_) { fileName_.clear(); doc_.clip().file.clear(); savedText_.clear(); }   // the document stays open, as a clip no longer on disk
        if (hasComp_ && g == comp_.fileName) { comp_.fileName.clear(); comp_.doc.clip().file.clear(); comp_.savedText.clear(); }
    }
    RefreshClips();
    if (openLandingGone && !openTrickGone) BuildLandingPart();   // its landing alone was deleted: back to the game's
    scanAge_ = 99.f;
    Status("« " + file + "\" removed from the game, moved to the backups: " + Narrow(dest) + with);
}
