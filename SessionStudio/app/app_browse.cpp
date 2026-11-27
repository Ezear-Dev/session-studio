// Session Studio -- the Browse page (redesign step 6, 2026-11-27): collections on the left, one card per trick or ground
// state in the middle (with its stance x feet dots), the chosen card on the right (big variant grid, its clips,
// "Animate this variant"). "My clips" is a sortable table, "Captures" a grid. The data comes from model/library.
#include "app_internal.h"

using namespace app_detail;

namespace {
const char* kClipsCat = "#clips";
const char* kCapturesCat = "#captures";
const ImVec4 kViolet(0.745f, 0.588f, 1.f, 1.f);

void SmallCaps(const char* text) {   // a quiet section title of the sidebar
    ImGui::Spacing();
    ImGui::TextColored(theme::Text3, "%s", text);
}
} // namespace

// the variant being edited (the trick's, when the playhead is in its landing)
static FigureVariant CurrentVariant(bool inLanding, const FigureVariant& var, const FigureVariant& compVar) { return inLanding ? compVar : var; }

void App::AnimateVariant(const FigureVariant& v, bool newClip) {
    const FigureVariant cur = CurrentVariant(inLanding_, var_, comp_.var);
    if (v == cur) {
        if (newClip) Request(Ask::New);
        if (ask_ == Ask::None) SetWorkspace(1);   // no confirmation pending: go and animate
        return;
    }
    askVar_ = v;
    newAfterPick_ = newClip;
    Request(Ask::ChangeFigure);
    if (ask_ == Ask::None) SetWorkspace(1);
}

void App::BrowseView() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    Place(L.lib.x, L.lib.y, L.lib.w, L.lib.h);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##browse", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (clipsDir_.empty()) {   // empty state: nothing can be listed without the game's clips folder
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(fs, (avail.x - fs * 26.f) * 0.5f), avail.y * 0.3f));
        ImGui::BeginGroup();
        if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted("Choose the game's clips folder");
        if (theme::Strong()) ImGui::PopFont();
        ImGui::TextColored(theme::Text2, "...\\Win64\\Mods\\SessionAnimLoader\\clips");
        if (ui::Button("Choose the folder...", ui::Kind::Primary)) dirDialog_ = true;
        ImGui::SameLine();
        if (ui::Button("Setup check")) showWelcome_ = true;
        ImGui::EndGroup();
        ImGui::End();
        return;
    }

    // ---- the data of this frame
    std::vector<LibEntry> entries;
    for (uint32_t s : {ST_OnFoot, ST_Walking, ST_Running, ST_Riding, ST_Pushing, ST_Crouch, ST_Landing, ST_Manual, ST_NoseManual, ST_Powerslide}) {
        LibEntry e;
        e.base.state = s;
        e.title = StateTitle(s);
        e.category = "On the ground";
        e.ground = true;
        if (s == ST_Powerslide) e.note = "not checked in game yet";
        entries.push_back(e);
    }
    for (auto& t : tricks_) {
        LibEntry e;
        e.base.state = ST_Trick;
        e.base.trick = t.base;
        e.title = TrickDisplay(t.base);
        e.category = t.category;
        entries.push_back(e);
    }
    std::vector<const Clip*> clipPtrs;
    for (auto& ci : clips_) if (ci.err.empty()) clipPtrs.push_back(&ci.clip);
    const std::vector<LibCard> cards = BuildLibrary(entries, [&](const FigureVariant& v) { return FigureReady(v); }, clipPtrs);
    std::vector<std::string> cats;   // in the order of the list
    for (auto& c : cards) if (std::find(cats.begin(), cats.end(), c.category) == cats.end()) cats.push_back(c.category);
    const FigureVariant cur = CurrentVariant(inLanding_, var_, comp_.var);

    const float sideW = std::round(fs * 13.f), detailW = std::round(fs * 21.f);
    const bool clipsView = browseCat_ == kClipsCat, capsView = browseCat_ == kCapturesCat;
    const bool showDetail = !clipsView && !capsView;

    // ---- sidebar: search, collections, filters
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Bg1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.8f, fs * 0.8f));
    ImGui::BeginChild("##side", ImVec2(sideW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    {
        ImGui::SetNextItemWidth(-1);
        if (focusSearch_) { ImGui::SetKeyboardFocusHere(); focusSearch_ = false; }
        ImGui::InputTextWithHint("##search", "Search  (Ctrl+K)", browseSearch_, sizeof(browseSearch_));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Part of a name: tricks in the trick views, file names in My clips and Captures.");
        auto Item = [&](const char* label, const std::string& key, int count) {
            const bool on = browseCat_ == key;
            ImGui::PushID(key.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##item", on, 0, ImVec2(0, ImGui::GetFrameHeight()))) { browseCat_ = key; SaveSetting(settings_, "browse.view", key); }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float w = ImGui::GetItemRectSize().x, h = ImGui::GetItemRectSize().y;
            if (on) dl->AddRectFilled(p, ImVec2(p.x + 3.f, p.y + h), theme::U32(theme::Accent));
            dl->AddText(ImVec2(p.x + fs * 0.6f, p.y + (h - fs) * 0.5f), theme::U32(on ? theme::AccentHi : theme::Text), label);
            char n[16];
            snprintf(n, sizeof(n), "%d", count);
            const float nw = ImGui::CalcTextSize(n).x;
            dl->AddText(ImVec2(p.x + w - nw - fs * 0.3f, p.y + (h - fs) * 0.5f), theme::U32(on ? theme::AccentHi : theme::Text3), n);
            ImGui::PopID();
        };
        SmallCaps("TRICKS AND STATES");
        Item("Everything", "", (int)cards.size());
        for (auto& cat : cats) {
            int n = 0;
            for (auto& c : cards) n += c.category == cat;
            Item(cat.c_str(), cat, n);
        }
        SmallCaps("YOUR WORK");
        Item("My clips", kClipsCat, (int)clips_.size());
        Item("Captures", kCapturesCat, (int)captures_.size());
        if (showDetail) {
            SmallCaps("SHOW ONLY");
            if (ui::Toggle("Recorded##only", browseRecOnly_)) { browseRecOnly_ = !browseRecOnly_; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only what you already did in game (at least one variant recorded).");
            ImGui::SameLine();
            if (ui::Toggle("Has clips##only", browseClipsOnly_)) { browseClipsOnly_ = !browseClipsOnly_; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only what has at least one clip of yours.");
        }
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(theme::Text3, "%s", "A trick missing from the list shows up as soon as you do it in game.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);

    // ---- centre
    const float centreW = std::max(fs * 12.f, ImGui::GetContentRegionAvail().x - (showDetail ? detailW : 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.f, fs * 0.9f));
    ImGui::BeginChild("##centre", ImVec2(centreW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    const std::string q = Lower(browseSearch_);
    if (clipsView) BrowseClipsTable(q);
    else if (capsView) BrowseCaptures(q);
    else {
        LibFilter f;
        f.search = browseSearch_;
        f.category = browseCat_;
        f.recordedOnly = browseRecOnly_;
        f.withClipsOnly = browseClipsOnly_;
        std::vector<int> shown;
        for (int i = 0; i < (int)cards.size(); i++) if (CardMatches(cards[i], f)) shown.push_back(i);
        if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextUnformatted(browseCat_.empty() ? "Everything" : browseCat_.c_str());
        if (theme::Strong()) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Text3, "%d", (int)shown.size());
        ImGui::Spacing();
        const float cw = std::round(fs * 11.f), ch = std::round(fs * 7.4f), gap = std::round(fs * 0.8f);
        const int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cw + gap)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int col = 0;
        for (int idx : shown) {
            const LibCard& c = cards[idx];
            if (col) ImGui::SameLine(0, gap);
            ImGui::PushID(idx);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const std::string key = FigureKey(c.base);
            const bool selected = browseCard_ == key;
            const bool isCur = c.base.state == cur.state && (!StateNamed(c.base.state) || Lower(c.base.trick) == Lower(cur.trick));
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton("##card", ImVec2(cw, ch))) { browseCard_ = key; detailVar_ = isCur ? cur : c.base; }
            const bool hov = ImGui::IsItemHovered();
            if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) AnimateVariant(isCur ? cur : c.base, false);
            if (hov) ImGui::SetTooltip("%s\n%d of %d variants recorded in game, %d clip(s) of yours%s\nClick: details · double-click: animate · a dot: that variant",
                                       c.title.c_str(), c.recorded, c.variants, c.clips, c.note.empty() ? "" : ("\n" + c.note).c_str());
            // the card
            dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), theme::U32(hov ? theme::Raised : theme::Surface), fs * 0.6f);
            dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), selected ? theme::U32(theme::Accent) : theme::U32(theme::Line), fs * 0.6f, 0, selected ? 2.f : 1.f);
            {   // a quiet figure (pose previews: later)
                const ImVec2 a(p.x + fs * 0.6f, p.y + fs * 0.6f), b(p.x + cw - fs * 0.6f, p.y + ch * 0.52f);
                dl->AddRectFilled(a, b, theme::U32(theme::Bg1), fs * 0.4f);
                const float cx = (a.x + b.x) * 0.5f, h = b.y - a.y, top = a.y + h * 0.18f;
                const ImU32 fc = theme::U32(c.recorded ? theme::Text3 : theme::Line);
                dl->AddCircleFilled(ImVec2(cx, top + h * 0.08f), h * 0.09f, fc);
                const float lw = std::max(2.f, h * 0.06f);
                dl->AddLine(ImVec2(cx, top + h * 0.17f), ImVec2(cx, top + h * 0.48f), fc, lw);
                dl->AddLine(ImVec2(cx, top + h * 0.24f), ImVec2(cx - h * 0.2f, top + h * 0.40f), fc, lw);
                dl->AddLine(ImVec2(cx, top + h * 0.24f), ImVec2(cx + h * 0.18f, top + h * 0.34f), fc, lw);
                dl->AddLine(ImVec2(cx, top + h * 0.48f), ImVec2(cx - h * 0.1f, top + h * 0.72f), fc, lw);
                dl->AddLine(ImVec2(cx, top + h * 0.48f), ImVec2(cx + h * 0.12f, top + h * 0.72f), fc, lw);
                dl->AddLine(ImVec2(cx - h * 0.28f, top + h * 0.76f), ImVec2(cx + h * 0.28f, top + h * 0.76f), theme::U32(theme::Line), lw);
                if (!c.recorded) {
                    const char* t = "do it once in game";
                    const ImVec2 ts = ImGui::CalcTextSize(t);
                    dl->AddText(ImVec2(cx - ts.x * 0.5f, b.y - ts.y - fs * 0.2f), theme::U32(theme::Text3), t);
                }
            }
            const float ty = p.y + ch * 0.56f;
            dl->AddText(theme::Strong(), 0.f, ImVec2(p.x + fs * 0.7f, ty), theme::U32(c.recorded ? theme::Text : theme::Text2), c.title.c_str());
            if (c.clips) {
                char cl[24];
                snprintf(cl, sizeof(cl), "%d clip%s", c.clips, c.clips > 1 ? "s" : "");
                dl->AddText(ImVec2(p.x + fs * 0.7f, ty + fs * 1.3f), theme::U32(theme::AccentHi), cl);
            } else if (!c.note.empty()) dl->AddText(ImVec2(p.x + fs * 0.7f, ty + fs * 1.3f), theme::U32(theme::Warn), c.note.c_str());
            else dl->AddText(ImVec2(p.x + fs * 0.7f, ty + fs * 1.3f), theme::U32(theme::Text3), c.ground ? "on the ground" : c.category.c_str());
            // the variant dots: columns = stances, rows = regular / goofy; click = that variant
            const float dr = std::round(fs * 0.26f), ds = std::round(fs * 0.72f);
            const float gx = p.x + cw - fs * 0.6f - ds * (float)c.stances, gy = p.y + ch - fs * 0.5f - ds * 2.f;
            for (int s = 0; s < c.stances; s++)
                for (int g = 0; g < 2; g++) {
                    const LibVariant& lv = c.cell[s][g];
                    const ImVec2 dc(gx + ds * (s + 0.5f), gy + ds * (g + 0.5f));
                    ImGui::SetCursorScreenPos(ImVec2(dc.x - ds * 0.5f, dc.y - ds * 0.5f));
                    ImGui::PushID(s * 2 + g);
                    const bool click = ImGui::InvisibleButton("##dot", ImVec2(ds, ds));
                    const bool dh = ImGui::IsItemHovered();
                    ImGui::PopID();
                    if (lv.recorded) dl->AddCircleFilled(dc, dr, theme::U32(theme::Good));
                    else dl->AddCircle(dc, dr, theme::U32(theme::Good, 0.7f), 0, 1.2f);
                    if (lv.clipsOn) dl->AddCircleFilled(dc, dr * 0.5f, theme::U32(theme::Accent));
                    if (lv.v == cur) dl->AddCircle(dc, dr + 2.5f, theme::U32(theme::Text), 0, 1.5f);
                    else if (dh) dl->AddCircle(dc, dr + 2.f, theme::U32(theme::Text2), 0, 1.f);
                    if (dh) ImGui::SetTooltip("%s\n%s%s\nClick: open this variant", VariantTitleUi(lv.v).c_str(),
                                              lv.recorded ? "recorded in game" : "not recorded yet: do it once in game",
                                              lv.clips ? (", " + std::to_string(lv.clips) + " clip(s) (" + std::to_string(lv.clipsOn) + " on)").c_str() : "");
                    if (click) { browseCard_ = key; detailVar_ = lv.v; askVar_ = lv.v; if (lv.v != cur) Request(Ask::ChangeFigure); }
                }
            ImGui::SetCursorScreenPos(ImVec2(p.x + cw, p.y));
            ImGui::Dummy(ImVec2(0, ch));
            ImGui::PopID();
            col = (col + 1) % cols;
            if (col == 0) ImGui::Dummy(ImVec2(0, gap * 0.25f));
        }
        if (shown.empty()) ImGui::TextColored(theme::Text2, "Nothing matches: try another name, or turn the filters off.");
        ImGui::Spacing();
        ImGui::TextColored(theme::Text3, "%s", "Dots: one per stance (columns: normal, switch, fakie, nollie) and foot (rows: regular, goofy). Green: recorded in game. Orange centre: a clip of yours is on. White ring: what you are editing.");
    }
    ImGui::EndChild();

    // ---- detail of the chosen card
    if (showDetail) {
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Bg1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.f, fs * 0.9f));
        ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        const LibCard* card = nullptr;
        for (auto& c : cards) if (FigureKey(c.base) == browseCard_) card = &c;
        if (!card) {   // nothing chosen yet: the card of what is being edited
            for (auto& c : cards) if (c.base.state == cur.state && (!StateNamed(c.base.state) || Lower(c.base.trick) == Lower(cur.trick))) card = &c;
            if (card) { browseCard_ = FigureKey(card->base); detailVar_ = cur; }
        }
        if (card) BrowseDetail(*card, cur, clipPtrs);
        else ImGui::TextColored(theme::Text2, "%s", "Pick a card to see its variants.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void App::BrowseDetail(const LibCard& card, const FigureVariant& cur, const std::vector<const Clip*>& clipPtrs) {
    const float fs = ImGui::GetFontSize();
    // the chosen variant must belong to this card
    bool inCard = false;
    for (int s = 0; s < card.stances; s++) for (int g = 0; g < 2; g++) inCard |= card.cell[s][g].v == detailVar_;
    if (!inCard) detailVar_ = card.base;
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextUnformatted(card.title.c_str());
    if (theme::Strong()) ImGui::PopFont();
    ImGui::TextColored(theme::Text3, "%s  ·  recorded in %d of %d variants", card.category.c_str(), card.recorded, card.variants);
    if (!card.note.empty()) ImGui::TextColored(theme::Warn, "%s", card.note.c_str());
    ImGui::Spacing();

    // the big grid: columns = stances, rows = feet
    const float lw = std::round(fs * 4.2f), cw = std::round(fs * 3.3f), chh = std::round(fs * 2.f), g2 = std::round(fs * 0.3f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    for (int s = 0; s < card.stances; s++) {
        const char* sl = StanceLabel(s);
        const ImVec2 ts = ImGui::CalcTextSize(sl);
        dl->AddText(ImVec2(o.x + lw + s * (cw + g2) + (cw - ts.x) * 0.5f, o.y), theme::U32(theme::Text3), sl);
    }
    for (int g = 0; g < 2; g++) {
        const float ry = o.y + fs * 1.3f + g * (chh + g2);
        dl->AddText(ImVec2(o.x, ry + (chh - fs) * 0.5f), theme::U32(theme::Text3), g ? "Goofy" : "Regular");
        for (int s = 0; s < card.stances; s++) {
            const LibVariant& lv = card.cell[s][g];
            const ImVec2 a(o.x + lw + s * (cw + g2), ry), b(a.x + cw, ry + chh);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(s * 2 + g);
            const bool click = ImGui::InvisibleButton("##cell", ImVec2(cw, chh));
            const bool hov = ImGui::IsItemHovered();
            const bool dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
            dl->AddRectFilled(a, b, lv.recorded ? theme::U32(theme::Good, hov ? 0.30f : 0.20f) : theme::U32(hov ? theme::Raised : theme::Surface), fs * 0.35f);
            if (lv.clipsOn) dl->AddCircleFilled(ImVec2(b.x - fs * 0.45f, a.y + fs * 0.45f), fs * 0.18f, theme::U32(theme::Accent));
            if (lv.v == cur) dl->AddRect(a, b, theme::U32(theme::Accent), fs * 0.35f, 0, 2.f);
            if (lv.v == detailVar_) dl->AddRect(ImVec2(a.x + 2.f, a.y + 2.f), ImVec2(b.x - 2.f, b.y - 2.f), theme::U32(theme::Text), fs * 0.3f, 0, 1.5f);
            if (hov) ImGui::SetTooltip("%s\n%s\nClick: choose · double-click: animate it", VariantTitleUi(lv.v).c_str(), lv.recorded ? "recorded in game" : "not recorded yet");
            if (click) detailVar_ = lv.v;
            if (dbl) AnimateVariant(lv.v, false);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + fs * 1.3f + 2.f * (chh + g2)));
    ImGui::Dummy(ImVec2(lw + card.stances * (cw + g2), 1.f));
    {   // legend
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(p.x, p.y + 3.f), ImVec2(p.x + fs * 0.8f, p.y + fs * 0.8f), theme::U32(theme::Good, 0.35f), 3.f);
        dl->AddText(ImVec2(p.x + fs, p.y), theme::U32(theme::Text3), "recorded");
        dl->AddCircleFilled(ImVec2(p.x + fs * 6.2f, p.y + fs * 0.5f), fs * 0.18f, theme::U32(theme::Accent));
        dl->AddText(ImVec2(p.x + fs * 6.6f, p.y), theme::U32(theme::Text3), "clip on");
        dl->AddRect(ImVec2(p.x + fs * 10.5f, p.y + 2.f), ImVec2(p.x + fs * 11.4f, p.y + fs * 0.85f), theme::U32(theme::Accent), 3.f, 0, 2.f);
        dl->AddText(ImVec2(p.x + fs * 11.7f, p.y), theme::U32(theme::Text3), "editing");
        ImGui::Dummy(ImVec2(0, fs));
    }
    ImGui::Separator();

    // the chosen variant
    const FigureVariant v = detailVar_;
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
    ImGui::TextUnformatted(VariantTitleUi(v).c_str());
    if (theme::Strong()) ImGui::PopFont();
    if (FigureReady(v)) {
        ImGui::TextColored(theme::Good, "%s", "Game movement recorded");
        ImGui::SameLine();
        if (ImGui::SmallButton(FigureRedo(v) ? "redo in game" : "record again")) RequestRerecord(v);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next time you do this move in game, its movement is recorded again.");
    } else ImGui::TextColored(theme::Warn, "Do it once in game: the mod records it by itself.");
    {
        const int pl = PlayingClip(clipPtrs, v);
        std::string who = "the game's own animation";
        if (pl >= 0) for (auto& ci : clips_) if (&ci.clip == clipPtrs[pl]) who = ci.file;
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(theme::Text2, "Plays in game: %s", who.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Among your clips that are on for this variant, the one the game plays\n(higher priority first, then the more specific). Saved files only.");
    }
    ImGui::Spacing();
    // its clips
    int listed = 0;
    for (auto& ci : clips_) {
        if (!ci.err.empty() || !ClipPlaysOn(ci.clip, v)) continue;
        listed++;
        ImGui::PushID(ci.file.c_str());
        bool en = ci.clip.enabled;
        if (ui::Switch("##on", &en)) ToggleClipEnabled(ci.file, en);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(ci.clip.enabled ? "On: plays in game. Switch off to keep it without playing it." : "Off: does not play in game.");
        ImGui::SameLine();
        const std::string stem = ci.file.size() > 6 ? ci.file.substr(0, ci.file.size() - 6) : ci.file;
        const bool open = ci.file == fileName_ || (inLanding_ && ci.file == comp_.fileName);
        ImGui::PushStyleColor(ImGuiCol_Text, ci.clip.enabled ? theme::Text : theme::Text3);
        if (ImGui::Selectable((stem + (open ? "  (open)" : "")).c_str(), open, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(ImGui::GetContentRegionAvail().x - fs * 3.5f, 0))) {
            if (!open) { askFile_ = ci.file; Request(Ask::Open); }
            if (ask_ == Ask::None) SetWorkspace(1);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s · %.2f s\nClick: open it and animate", ci.file.c_str(), ci.clip.mode == Mode::Additive ? "add" : "replace", ci.clip.duration);
        if (ImGui::BeginPopupContextItem("##ctx")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
        ImGui::SameLine();
        ImGui::TextColored(theme::Text3, "%s", ci.clip.mode == Mode::Additive ? "add" : "repl.");
        ImGui::SameLine();
        if (ui::IconButton("menu", theme::Icon::More, "...", false, ui::Kind::Ghost)) ImGui::OpenPopup("##menu");
        if (ImGui::BeginPopup("##menu")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
        ImGui::PopID();
    }
    if (!listed) ImGui::TextColored(theme::Text3, "%s", "No clip of yours for this variant yet.");
    ImGui::Spacing();
    ImGui::Spacing();
    if (ui::Button("Animate this variant", ui::Kind::Primary, ImVec2(-1, ImGui::GetFrameHeight() * 1.3f))) AnimateVariant(v, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the Animate workspace on %s (Tab): its first clip, or a new one.", VariantTitleUi(v).c_str());
    if (ui::Button("New clip for this variant", ui::Kind::Normal, ImVec2(-1, 0))) AnimateVariant(v, true);
}

void App::BrowseClipsTable(const std::string& q) {
    const float fs = ImGui::GetFontSize();
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextUnformatted("My clips");
    if (theme::Strong()) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::Text3, "%d", (int)clips_.size());
    static const char* kF[5] = {"All", "On", "Off", "Problems", "Editing variant"};
    ui::Segmented("##clipfilter", kF, 5, &clipsFilter_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Problems: unreadable files, and clips that change nothing in game.\nEditing variant: the clips that play on the variant you are editing.");
    ImGui::Spacing();
    const FigureVariant cur = inLanding_ ? comp_.var : var_;
    struct Row { const ClipInfo* ci; std::string forWhat; bool problem; };
    std::vector<Row> rows;
    for (auto& ci : clips_) {
        if (!q.empty() && Lower(ci.file).find(q) == std::string::npos) continue;
        const bool problem = !ci.err.empty() || ClipHasNoEffect(ci.clip);
        if (clipsFilter_ == 1 && (!ci.err.empty() || !ci.clip.enabled)) continue;
        if (clipsFilter_ == 2 && (!ci.err.empty() || ci.clip.enabled)) continue;
        if (clipsFilter_ == 3 && !problem) continue;
        if (clipsFilter_ == 4 && (!ci.err.empty() || !ClipPlaysOn(ci.clip, cur))) continue;
        rows.push_back(Row{&ci, ci.err.empty() ? VariantTitleUi(VariantOfClip(ci.clip)) : std::string("-"), problem});
    }
    const ImGuiTableFlags tf = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("##clips", 6, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, fs * 2.6f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 3.f);
        ImGui::TableSetupColumn("For", 0, 2.5f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, fs * 4.f);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, fs * 4.f);
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_NoSort, 2.5f);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = ss->Specs[0];
                const bool asc = s.SortDirection == ImGuiSortDirection_Ascending;
                std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                    int d = 0;
                    switch (s.ColumnIndex) {
                        case 0: d = (int)a.ci->clip.enabled - (int)b.ci->clip.enabled; break;
                        case 2: d = a.forWhat.compare(b.forWhat); break;
                        case 3: d = (int)a.ci->clip.mode - (int)b.ci->clip.mode; break;
                        case 4: d = a.ci->clip.duration < b.ci->clip.duration ? -1 : a.ci->clip.duration > b.ci->clip.duration ? 1 : 0; break;
                        default: d = Lower(a.ci->file).compare(Lower(b.ci->file)); break;
                    }
                    return asc ? d < 0 : d > 0;
                });
            }
        }
        for (const Row& r : rows) {
            const ClipInfo& ci = *r.ci;
            ImGui::PushID(ci.file.c_str());
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::TableSetColumnIndex(0);
            if (ci.err.empty()) {
                bool en = ci.clip.enabled;
                if (ui::Switch("##on", &en)) ToggleClipEnabled(ci.file, en);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(ci.clip.enabled ? "On: plays in game." : "Off: does not play in game.");
            }
            ImGui::TableSetColumnIndex(1);
            const bool open = ci.file == fileName_ || (inLanding_ && ci.file == comp_.fileName);
            const std::string stem = ci.file.size() > 6 ? ci.file.substr(0, ci.file.size() - 6) : ci.file;
            ImGui::PushStyleColor(ImGuiCol_Text, !ci.err.empty() ? theme::Bad : ci.clip.enabled ? theme::Text : theme::Text3);
            ImGui::AlignTextToFramePadding();
            const bool click = ImGui::Selectable((stem + (open ? "  (open)" : "")).c_str(), open, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            ImGui::PopStyleColor();
            if (click && ci.err.empty()) {
                if (!open) { askFile_ = ci.file; Request(Ask::Open); }
                if (ask_ == Ask::None) SetWorkspace(1);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", ci.file.c_str(), ci.err.empty() ? "Click: open it and animate · right-click: rename, duplicate, delete" : ("Unreadable: " + ci.err + "\nRight-click: delete (to the backups)").c_str());
            if (ImGui::BeginPopupContextItem("##ctx")) { ClipMenuItems(ci.file, false); ImGui::EndPopup(); }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(theme::Text2, "%s", r.forWhat.c_str());
            ImGui::TableSetColumnIndex(3);
            if (ci.err.empty()) ImGui::TextColored(theme::Text2, "%s", ci.clip.mode == Mode::Additive ? "add" : "replace");
            ImGui::TableSetColumnIndex(4);
            if (ci.err.empty()) ImGui::TextColored(theme::Text2, "%.2f s", ci.clip.duration);
            ImGui::TableSetColumnIndex(5);
            if (!ci.err.empty()) ImGui::TextColored(theme::Bad, "unreadable: %s", ci.err.c_str());
            else if (ClipHasNoEffect(ci.clip)) ImGui::TextColored(theme::Warn, "%s", "changes nothing in game");
            else if (ci.clip.feet == Feet::Regular && !ci.clip.mirror) ImGui::TextColored(theme::Text3, "%s", "regular only");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rows.empty()) ImGui::TextColored(theme::Text2, "%s", clips_.empty() ? "Your clips appear here when you save them (Ctrl+S in Animate)." : "No clip matches.");
    ImGui::TextColored(theme::Text3, "%s", "Delete moves the file to the backups (recoverable).");
}

void App::BrowseCaptures(const std::string& q) {
    const float fs = ImGui::GetFontSize();
    if (theme::Strong()) ImGui::PushFont(theme::Strong(), ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextUnformatted("Captures");
    if (theme::Strong()) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::Text3, "%d", (int)captures_.size());
    ImGui::TextColored(theme::Text2, "%s", "Ctrl+F4 in game freezes the skater's pose here. Opening a capture makes a new clip from it.");
    ImGui::Spacing();
    const float cw = std::round(fs * 11.f), ch = std::round(fs * 3.2f), gap = std::round(fs * 0.8f);
    const int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cw + gap)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int col = 0, shown = 0;
    for (auto& f : captures_) {
        std::string stem = f.substr(f.find('\\') + 1);
        if (stem.size() > 6) stem = stem.substr(0, stem.size() - 6);
        if (!q.empty() && Lower(stem).find(q) == std::string::npos) continue;
        shown++;
        if (col) ImGui::SameLine(0, gap);
        ImGui::PushID(f.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##cap", ImVec2(cw, ch))) { askFile_ = f; Request(Ask::Open); if (ask_ == Ask::None) SetWorkspace(1); }
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetTooltip("%s\nClick: open as a new clip and animate (the capture itself does not change) · right-click: delete", stem.c_str());
        if (ImGui::BeginPopupContextItem("##ctx")) { ClipMenuItems(f, true); ImGui::EndPopup(); }
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), theme::U32(hov ? theme::Raised : theme::Surface), fs * 0.6f);
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), theme::U32(theme::Line), fs * 0.6f);
        if (theme::HasIcons()) dl->AddText(ImVec2(p.x + fs * 0.7f, p.y + (ch - fs) * 0.5f), theme::U32(theme::Text3), theme::Glyph(theme::Icon::Camera).c_str());
        dl->AddText(ImVec2(p.x + fs * 2.2f, p.y + (ch - fs) * 0.5f), theme::U32(theme::Text), stem.c_str());
        ImGui::PopID();
        col = (col + 1) % cols;
    }
    if (!shown) ImGui::TextColored(theme::Text2, "%s", captures_.empty() ? "No capture yet: Ctrl+F4 in game." : "No capture matches.");
}
