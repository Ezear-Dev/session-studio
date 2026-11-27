// Session Studio -- Timeline panel: key selection, copy / paste, end warnings, transport, view bar, rows.
#include "app_internal.h"

using namespace app_detail;

bool App::KeySelected(const std::vector<int>& bones, float t) const {
    for (auto& k : keySel_) if (k.bones == bones && std::fabs(k.t - t) < 1e-3f) return true;
    return false;
}

void App::ToggleKey(const std::vector<int>& bones, float t) {
    for (size_t i = 0; i < keySel_.size(); i++)
        if (keySel_[i].bones == bones && std::fabs(keySel_[i].t - t) < 1e-3f) { keySel_.erase(keySel_.begin() + i); return; }
    keySel_.push_back(KeySel{bones, t});
}

void App::DeleteSelectedKeys() {
    if (keySel_.empty()) return;
    doc_.Checkpoint("delete keys");
    for (auto& k : keySel_) { if (k.bones.empty()) doc_.DeleteKeysAt(k.t, -1); else doc_.DeleteKeysBones(k.bones, k.t); }
    Status(std::to_string(keySel_.size()) + (keySel_.size() > 1 ? " keys deleted" : " key deleted"));
    keySel_.clear();
}

void App::SetSelectedInterp(int interp) {
    if (keySel_.empty()) return;
    doc_.Checkpoint("interpolation");
    for (auto& k : keySel_) {
        if (k.bones.empty()) doc_.SetKeyInterp(k.t, -1, interp);
        else for (int b : k.bones) doc_.SetKeyInterp(k.t, b, interp);
    }
}

void App::CopyNow() {
    if (!keySel_.empty()) {
        std::vector<EditorDoc::KeyRef> refs;
        for (auto& k : keySel_) refs.push_back(EditorDoc::KeyRef{k.bones, k.t});
        const int n = doc_.CopyKeys(refs);
        if (n) Status(std::to_string(n) + (n > 1 ? " keys copied" : " key copied") + ": Ctrl+V pastes them at the playhead");
        else Status("nothing to copy: the selected keys hold no editable part");
        return;
    }
    const bool pelSel = sel_ >= 0 && sel_ == doc_.PelvisBone();
    const bool part = doc_.IsEditable(sel_) || pelSel;
    const int n = doc_.CopyPose(t_, part ? sel_ : -1);
    const std::string what = part ? (pelSel ? std::string("the pelvis") : BoneLabelUi(doc_.skeleton().names[sel_])) : std::string("the whole body");
    if (n) Status("your pose of " + what + " copied (" + std::to_string(n) + (n > 1 ? " parts" : " part") + "): Ctrl+V pastes it at the playhead");
    else Status("nothing to copy: " + what + " has no change at this time");
}

void App::PasteNow() {
    if (LandingLockedHere()) { Status("the landing is locked (the game's): unlock it in Clip settings to pose it"); return; }   // 2026-11-27
    if (!doc_.CanPaste()) { Status("nothing to paste: copy a pose or keys first (Ctrl+C)"); return; }
    if (!doc_.ClipboardHasKeys() && doc_.LockedAt(doc_.KeyTime(t_))) {   // 2026-11-26 (task 3)
        Status("start and end are locked to the game's pose: paste the pose between them");
        return;
    }
    if (doc_.clip().mode == Mode::Additive && !doc_.HasFigure()) {
        Status("additive needs the game animation: do \"" + VariantTitleUi(var_) + "\" once in game");
        return;
    }
    if (doc_.HasPending()) {
        if (!autoKey_) { Status("set or discard the pending change first (K)"); return; }
        CommitKeyNow(true);   // auto key: the change of the last gesture becomes its key first
    }
    const bool keys = doc_.ClipboardHasKeys();
    const EditorDoc::PasteResult r = doc_.Paste(t_);
    std::string s;
    if (r.pasted == 0) s = "nothing pasted";
    else if (keys) s = std::to_string(r.pasted) + (r.pasted > 1 ? " keys pasted" : " key pasted");
    else s = "pose pasted at frame " + std::to_string((int)std::lround(t_ / kFrame));
    if (r.outOfClip) s += ", " + std::to_string(r.outOfClip) + " past the end of the clip skipped";
    if (r.refused) s += ", " + std::to_string(r.refused) + " not editable in this clip";
    Status(s);
}

bool App::HasLandingForAllVariants(const Clip& trick, std::string& missing) const {
    std::vector<const Clip*> cs;
    for (auto& ci : clips_) if (ci.err.empty()) cs.push_back(&ci.clip);   // enabled landing clips of that trick only (checked inside)
    return LandingCoversAllVariants(trick, cs, missing);
}

void App::EndWarnings(bool withFix) {
    if (!doc_.IsTrickClip() && !doc_.IsLandingClip()) return;
    std::string missing;
    bool all = false;
    const Clip* lt = nullptr;
    if (doc_.IsTrickClip()) all = HasLandingForAllVariants(doc_.clip(), missing);
    else if (!var_.trick.empty()) lt = TrickForLanding();   // 2026-11-27: the trick in memory on one timeline
    const std::vector<EditorDoc::EndIssue> issues = doc_.EndIssues(lt, all);
    const ImVec4 yellow(1.f, 0.85f, 0.3f, 1.f);
    int idx = 0;
    for (const EditorDoc::EndIssue& e : issues) {
        std::string parts;
        int shown = 0;
        for (int b : e.bones) {
            if (shown == 4) { parts += " +" + std::to_string(e.bones.size() - 4) + " more"; break; }
            parts += (shown ? ", " : "") + BoneLabelUi(doc_.skeleton().names[b]);
            shown++;
        }
        if (e.pelvis) parts += parts.empty() ? "Pelvis" : ", Pelvis";
        std::string line, tip;
        char lim[96];
        snprintf(lim, sizeof(lim), "(up to %.0f°%s)", e.maxDeg, e.pelvis ? (", pelvis " + std::to_string((int)std::lround(e.maxCm)) + " cm").c_str() : "");
        switch (e.kind) {
            case EditorDoc::EndIssue::TrickEndNoLanding:
                line = "Not back at the end: " + parts;
                tip = std::string("At the end of this trick clip, these parts still differ from the game's pose ") + lim + ".\n" +
                      (missing.empty() ? std::string("This trick has no landing clip") : "No landing clip of this trick for: " + missing) +
                      ",\nso at the ground contact the clip fades out over its blend out:\nthese parts snap back to the game's pose quickly.\n"
                      "Fix: end them at the game's pose, or make a landing clip (\"Landing of this trick\").\nLegs are not checked: the game takes them back after the catch.";
                break;
            case EditorDoc::EndIssue::LandingEndNotBack:
                line = "Not back at the end of the landing: " + parts;
                tip = std::string("At the end of this landing clip, these parts still differ from the game's pose ") + lim +
                      ".\nThe landing hands back to the game there: they snap back quickly.";
                break;
            case EditorDoc::EndIssue::LandingStartMismatch:
                line = "Start differs from the end of the trick: " + parts;
                tip = std::string("At the start of this landing, these parts differ from the end of the trick clip ") + lim +
                      ".\nThe landing takes over at the ground contact: they jump there.";
                break;
            default: break;   // StartNotGame: LockPreview only
        }
        if (doc_.LockEnds() && e.kind == EditorDoc::EndIssue::TrickEndNoLanding)
            tip += "\nThe end is not locked while it is posed: bringing it back locks it again.";
        ImGui::PushID(1000 + idx++);
        ImGui::PushStyleColor(ImGuiCol_Text, yellow);
        const bool click = ImGui::Selectable(("! " + line).c_str(), false, 0, ImVec2(withFix ? 0.f : ImGui::CalcTextSize(("! " + line).c_str()).x, 0));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n\nClick: selects the part and goes to that instant.", tip.c_str());
        if (click) {
            sel_ = !e.bones.empty() ? e.bones.front() : doc_.PelvisBone();
            JumpTo(e.t);
        }
        if (withFix) {
            const char* label = e.kind == EditorDoc::EndIssue::LandingStartMismatch ? "Take the end of the trick for these parts" : "Bring these parts back at the end";
            if (ImGui::SmallButton(label)) {
                if (doc_.HasPending()) Status("set or discard the pending change first (K)");
                else if (doc_.FixEndIssue(e, lt)) Status(e.kind == EditorDoc::EndIssue::LandingStartMismatch ? "start of the landing = end of the trick, for these parts" : "these parts end at the game's pose (Ctrl+Z undoes it)");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.kind == EditorDoc::EndIssue::LandingStartMismatch
                ? "Sets the start of the landing to the trick's end, for the listed parts only.\nThe other keys are kept."
                : "Adds a key at the end of the clip with the game's pose, for the listed parts only.\nThe other keys are kept.");
        }
        ImGui::PopID();
    }
}

// UI Phase 4: go to a time (through the pending-change check)
void App::JumpTo(float t) {
    // 2026-11-27: t is a time of the open part; on one timeline it becomes a shared time
    JumpShared(hasComp_ ? JoinTime(Parts(), inLanding_ ? 1 : 0, std::clamp(t, 0.f, doc_.clip().duration)) : t);
}

// redesign step 5 (2026-11-27): one header line. Left: keys and interpolation. Centre: transport, Auto key, the time.
// Right: end warnings, zoom, view options, help. (Was three lines: transport, view bar, selection line.)
void App::TimelineTransport() {
    Clip& c = doc_.clip();
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    static const char* kInterpName[3] = {"smooth", "linear", "constant"};
    const float lineY = ImGui::GetCursorPosY();
    const float winR = ImGui::GetWindowContentRegionMax().x;

    // ---- left: keys
    if (LandingLockedHere()) ImGui::BeginDisabled();   // 2026-11-27: a locked landing (the game's) takes no key
    if (ui::Button("+ key")) CommitKeyNow();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set a key at the playhead (K).");
    ImGui::SameLine(0, st.ItemSpacing.x * 0.5f);
    const bool pelSel = sel_ >= 0 && sel_ == doc_.PelvisBone();
    if (!keySel_.empty()) {
        if (ui::Button("- keys")) DeleteSelectedKeys();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the selected keys (Del).");
    } else {
        if (ui::Button("- key")) { doc_.Checkpoint("delete key"); doc_.DeleteKeysAt(doc_.KeyTime(t_), (doc_.IsEditable(sel_) || pelSel) ? sel_ : -1); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the key at the playhead (of the part picked, or of every part).");
    }
    {   // interpolation: of the selected keys, else of the key of the selected bone under the playhead
        int cur = -1;
        if (!keySel_.empty()) {
            const KeySel& k0 = keySel_.front();
            cur = doc_.KeyInterp(k0.t, k0.bones.empty() ? -1 : k0.bones.front());
            for (auto& k : keySel_) if (doc_.KeyInterp(k.t, k.bones.empty() ? -1 : k.bones.front()) != cur) cur = -2;   // mixed
        } else if (doc_.IsEditable(sel_) || pelSel) cur = doc_.KeyInterp(t_, sel_);
        if (cur != -1) {
            ImGui::SameLine(0, st.ItemSpacing.x);
            int m = cur;
            if (ui::Segmented("##interp", kInterpName, 3, &m)) {
                if (!keySel_.empty()) SetSelectedInterp(m);
                else { doc_.Checkpoint("interpolation"); doc_.SetKeyInterp(t_, sel_, m); }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How the pose goes from this key to the next one:\nsmooth: a soft curve through the keys\nlinear: constant speed from one key to the next\nconstant: the pose holds until the next key");
        }
    }
    if (doc_.CanPaste()) {   // 2026-11-26 copy / paste
        ImGui::SameLine(0, st.ItemSpacing.x);
        if (ui::IconButton("paste", theme::Icon::Paste, "Paste")) PasteNow();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", doc_.ClipboardHasKeys() ? "Paste (Ctrl+V): the copied keys, the first one at the playhead (same spacing and interpolation)."
                                                                                 : "Paste (Ctrl+V): keys the copied pose at the playhead. The other keys keep their pose.");
    }
    if (!keySel_.empty()) {   // what is selected, in one short line (details in the tooltip)
        ImGui::SameLine(0, st.ItemSpacing.x);
        ImGui::AlignTextToFramePadding();
        if (keySel_.size() == 1) {
            const KeySel& k = keySel_.front();
            std::string who = k.bones.empty() ? std::string("all bones") : k.bones.size() == 1 ? BoneLabelUi(doc_.skeleton().names[k.bones.front()]) : (std::to_string(k.bones.size()) + " bones");
            if (k.bones.size() == 1 && k.bones.front() == doc_.PelvisBone()) who = "Pelvis";
            ImGui::TextColored(theme::AccentHi, "1 key · %.2f s", k.t);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Selected key: %.2f s · %s\nDel: delete · Esc: deselect · drag: move", k.t, who.c_str());
        } else {
            float a = 1e9f, b = -1e9f;
            for (auto& k : keySel_) { a = std::min(a, k.t); b = std::max(b, k.t); }
            ImGui::TextColored(theme::AccentHi, "%d keys", (int)keySel_.size());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d keys selected, from %.2f to %.2f s\nDel: delete · Esc: deselect", (int)keySel_.size(), a, b);
        }
    }
    if (LandingLockedHere()) ImGui::EndDisabled();
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    // ---- centre: transport, Auto key, time (centred in the panel when there is room)
    ImGui::SameLine(std::max(leftEnd + fs * 1.5f, (winR - tlCenterW_) * 0.5f));
    ImGui::SetCursorPosY(lineY);
    ImGui::BeginGroup();
    if (doc_.EffectiveDrive() != Drive::ManualRatio) {
        const float nowF = std::round(SharedT() / kFrame);   // 2026-11-27: frames of the shared timeline
        const float totalT = hasComp_ ? Parts().Total() : c.duration;
        if (ui::IconButton("tostart", theme::Icon::ToStart, "|<", false, ui::Kind::Ghost)) JumpShared(0.f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start of the clip (Home)");
        ImGui::SameLine(0, fs * 0.15f);
        if (ui::IconButton("prevframe", theme::Icon::Prev, "<", false, ui::Kind::Ghost)) JumpShared((nowF - 1.f) * kFrame);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous frame (left arrow)");
        ImGui::SameLine(0, fs * 0.15f);
        {   // play / pause: the main action of the transport, always orange
            const std::string pl = theme::HasIcons() ? theme::Glyph(playing_ ? theme::Icon::Pause : theme::Icon::Play) + (playing_ ? "  Pause##c" : "  Play##c")
                                                     : std::string(playing_ ? "Pause##c" : "Play##c");
            if (ui::Button(pl.c_str(), ui::Kind::Primary, ImVec2(fs * 5.f, 0))) { if (playing_) playing_ = false; else Request(Ask::Play); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(playing_ ? "Pause (Space)" : "Plays the clip (Space)");
        ImGui::SameLine(0, fs * 0.15f);
        if (ui::IconButton("nextframe", theme::Icon::Next, ">", false, ui::Kind::Ghost)) JumpShared((nowF + 1.f) * kFrame);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next frame (right arrow)");
        ImGui::SameLine(0, fs * 0.15f);
        if (ui::IconButton("toend", theme::Icon::ToEnd, ">|", false, ui::Kind::Ghost)) JumpShared(totalT);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("End of the clip (End)");
        // 2026-11-25 (user request): loop and speed of the editor playback, to study the motion
        ImGui::SameLine(0, fs * 0.4f);
        const bool gameLoops = doc_.LoopsInTime();
        const bool loopOn = gameLoops || loopPreview_;
        if (gameLoops) ImGui::BeginDisabled();
        if (ui::IconButton("loop", theme::Icon::Loop, "Loop", loopOn)) { loopPreview_ = !loopPreview_; SaveSetting(settings_, "timeline.loop", loopPreview_ ? "1" : "0"); }
        if (gameLoops) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", gameLoops ? "This clip also loops in game: playback always loops."
                                              : loopOn ? "Loop is ON (L): the clip replays endlessly, to study it.\nEditor only: in game, the clip plays as before."
                     : "Loop is OFF (L): the clip plays once.\nEditor only: in game, the clip plays as before.");
        ImGui::SameLine(0, fs * 0.15f);
        static const float kSpeeds[3] = {1.f, 0.5f, 0.25f};
        static const char* kSpeedName[3] = {"x1", "x0.5", "x0.25"};
        int si = 0;
        for (int k = 0; k < 3; k++) if (std::fabs(playSpeed_ - kSpeeds[k]) < 1e-3f) si = k;
        ImGui::SetNextItemWidth(fs * 4.2f);
        if (ImGui::BeginCombo("##vitesse", kSpeedName[si])) {
            for (int k = 0; k < 3; k++) if (ImGui::Selectable(kSpeedName[k], k == si)) playSpeed_ = kSpeeds[k];
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed in the editor (x0.5: slow motion). Does not change the clip.");
    } else {
        float r = doc_.SimulatedRatio();
        ImGui::SetNextItemWidth(fs * 12.f);
        if (ImGui::SliderFloat("##ratio", &r, std::min(c.r0, c.r1), std::max(c.r0, c.r1), "ManualRatio %.2f")) {
            if (!doc_.HasPending()) doc_.SetSimulatedRatio(r);
            else Status("set or discard the pending change first (K)");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("This clip follows the game's ManualRatio (balance of the manual):\nslide to see the pose for a given ratio.");
    }
    // redesign step 2: Auto key lives with the transport (record style: red dot when on)
    ImGui::SameLine(0, fs * 0.6f);
    if (ui::RecordToggle("Auto key", autoKey_)) autoKey_ = !autoKey_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(autoKey_ ? "Auto key is ON: every gesture sets a key at the time shown.\nClick to turn it off (then you set keys yourself with K)."
                                                           : "Auto key is OFF: you set the key yourself with K (or the \"+ key\" button).\nClick to turn it on.");
    // the time, large, then the frame (30 images per second, like the game's recordings)
    ImGui::SameLine(0, fs * 0.9f);
    {
        const float big = ImGui::GetStyle().FontSizeBase * 1.35f;
        const float y0 = ImGui::GetCursorPosY();
        ImGui::PushFont(theme::Strong(), big);
        const float hBig = ImGui::GetFontSize();
        ImGui::SetCursorPosY(y0 + (ImGui::GetFrameHeight() - hBig) * 0.5f - 1.f);
        const float st = SharedT(), tot = hasComp_ ? Parts().Total() : c.duration;
        ImGui::Text("%.2f", st);
        ImGui::PopFont();
        ImGui::SameLine(0, fs * 0.3f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Text2, "s  ·  frame %d / %d", (int)std::lround(st / kFrame), (int)std::lround(tot / kFrame));
        if (ImGui::IsItemHovered()) {
            if (hasComp_) ImGui::SetTooltip("The playhead, in seconds and in frames (30 per second).\nThe trick lasts %.2f s, then its landing %.2f s (%s).", Parts().first, Parts().second, landLocked_ ? "locked: the game's" : "yours");
            else ImGui::SetTooltip("The playhead, in seconds and in frames (30 per second). The clip lasts %.2f s.", c.duration);
        }
        if (hasComp_) {   // which part is open: the trick or its landing
            ImGui::SameLine(0, fs * 0.5f);
            const char* part = inLanding_ ? (landLocked_ ? "landing (locked)" : "landing") : "trick";
            ui::Chip(part, inLanding_ ? ImVec4(0.745f, 0.588f, 1.f, 1.f) : theme::Text2);
            // the same room for every label: the transport is centred and must not move at the violet line
            const float pad = ImGui::CalcTextSize("landing (locked)").x - ImGui::CalcTextSize(part).x;
            if (pad > 0.5f) { ImGui::SameLine(0.f, 0.f); ImGui::Dummy(ImVec2(pad, 1.f)); }
        }
    }
    if (doc_.IsHold()) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("held pose"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("A single pose, applied all the time."); }
    else if (doc_.EffectiveDrive() == Drive::Cycle) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("one stride"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("One full stride, synced to the game's steps."); }
    ImGui::EndGroup();
    tlCenterW_ = ImGui::GetItemRectSize().x;
    const float centreEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    // ---- right: end warnings, zoom, view options, help (aligned on the right edge)
    ImGui::SameLine(std::max(centreEnd + fs * 1.5f, winR - tlRightW_));
    ImGui::SetCursorPosY(lineY);
    ImGui::BeginGroup();
    {   // 2026-11-26 end warnings: one chip; click = the list, each line jumps to the part and the instant, with its fix
        std::string missing;
        const Clip* lt = nullptr;
        bool all = false;
        if (doc_.IsTrickClip()) all = HasLandingForAllVariants(doc_.clip(), missing);
        else if (doc_.IsLandingClip() && !var_.trick.empty()) lt = TrickForLanding();
        const size_t nIssues = (doc_.IsTrickClip() || doc_.IsLandingClip()) ? doc_.EndIssues(lt, all).size() : 0;
        if (nIssues) {
            const std::string lab = (theme::HasIcons() ? theme::Glyph(theme::Icon::Warning) + " " : std::string("! ")) + std::to_string(nIssues) + "##endwarn";
            ImGui::PushStyleColor(ImGuiCol_Button, theme::Alpha(theme::Warn, 0.14f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::Alpha(theme::Warn, 0.26f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::Alpha(theme::Warn, 0.34f));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::Warn);
            if (ImGui::Button(lab.c_str())) ImGui::OpenPopup("##endwarnings");
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d part(s) of the body are not back at the end of this clip.\nClick: the list, with a fix for each.", (int)nIssues);
            ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 18.f, 0.f), ImVec2(fs * 34.f, fs * 30.f));
            if (ImGui::BeginPopup("##endwarnings")) { EndWarnings(true); ImGui::EndPopup(); }
            ImGui::SameLine(0, fs * 0.8f);
        } else if (hasComp_) {   // 2026-11-27: one part has warnings, the other not: keep the room
            const float w = ImGui::CalcTextSize("9").x + fs * 1.4f + ImGui::GetStyle().FramePadding.x * 2.f;
            ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
            ImGui::SameLine(0, fs * 0.8f);
        }
    }
    const float dur = hasComp_ ? Parts().Total() : c.duration;   // the view covers the trick and its landing
    const float head = SharedT();
    const float mid = tview_.Start(dur) + tview_.Span(dur) * 0.5f;
    const float anchor = tview_.Visible(head, dur) ? head : mid;   // zoom around the playhead when it is visible
    if (ui::IconButton("zoomout", theme::Icon::ZoomOut, "-", false, ui::Kind::Ghost)) tview_.ZoomAt(1.f / 1.5f, anchor, dur);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("See more time (Ctrl + wheel on the timeline)");
    ImGui::SameLine(0, fs * 0.15f);
    if (ui::IconButton("zoomin", theme::Icon::ZoomIn, "+", false, ui::Kind::Ghost)) tview_.ZoomAt(1.5f, anchor, dur);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("See closer (Ctrl + wheel on the timeline)");
    ImGui::SameLine(0, fs * 0.15f);
    const bool fitted = tview_.Fitted(dur);
    if (fitted) ImGui::BeginDisabled();
    if (ui::Button("Fit", ui::Kind::Ghost)) tview_.Fit();
    if (fitted) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(fitted ? "The whole clip is shown." : "Show the whole clip across the width (now x%.1f, %.2f to %.2f s).", tview_.Zoom(dur), tview_.Start(dur), tview_.Start(dur) + tview_.Span(dur));
    ImGui::SameLine(0, fs * 0.15f);
    if (ui::IconButton("tlview", theme::Icon::Down, "v", false, ui::Kind::Ghost)) ImGui::OpenPopup("##tlview");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rows and size of the timeline");
    if (ImGui::BeginPopup("##tlview")) {
        if (ui::Switch("##compact", &compactRows_, "Compact rows")) SaveSetting(settings_, "timeline.compact", compactRows_ ? "1" : "0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lower rows: more body parts visible at once.");
        // 2026-11-25: was "all bones" next to the keys, unclear; a view option, saved
        if (ui::Switch("##allrows", &allRows_, "Empty rows too")) SaveSetting(settings_, "timeline.allbones", allRows_ ? "1" : "0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also shows the body parts that have no key yet.\nOff: only the ones your clip animates (easier to read).");
        ImGui::Separator();
        const Layout L = Lay();
        if (ImGui::MenuItem(bottomBeforeBig_ > 0.f ? "Shrink the timeline" : "Enlarge the timeline")) {
            if (bottomBeforeBig_ > 0.f) { bottomU_ = bottomBeforeBig_; bottomBeforeBig_ = 0.f; }
            else { bottomBeforeBig_ = L.bottom / fs; bottomU_ = L.h * 0.55f / fs; bottomDefault_ = false; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gives the timeline more room, then puts it back as before.\nYou can also drag the edge between the 3D view and the timeline.");
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, fs * 0.15f);
    ui::IconButton("tlhelp", theme::Icon::Help, "?", false, ui::Kind::Ghost);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keys: click to pick · Shift+click to add · Shift+drag on an empty part: box · drag a key to move it\n"
                          "right-click a key: delete, copy, paste, interpolation · Del: delete · Esc: deselect\n"
                          "Ruler: click or drag to change the time · wheel on the ruler or Ctrl+wheel: zoom\n"
                          "Shift+wheel or middle-click drag: scroll in time");
    ImGui::EndGroup();
    tlRightW_ = ImGui::GetItemRectSize().x;
}

// the game's original animation, in blue (the ghost): play it alone, scrub it, copy a pose from it
void App::TimelineGameRow() {
    const float fs = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.616f, 0.722f, 1.f, 1.f), "Game");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's original movement, to compare with (the blue ghost in the 3D view).");
    ImGui::SameLine(fs * 4.f);
    if (doc_.HasFigure()) {
        if (ui::IconButton("gameplay", originPlaying_ ? theme::Icon::Pause : theme::Icon::Play, originPlaying_ ? "||" : ">", false, ui::Kind::Ghost)) originPlaying_ = !originPlaying_;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays the game's movement alone, looping.");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::Alpha(theme::Ghost, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::Alpha(theme::Ghost, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme::Alpha(theme::Ghost, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::Ghost);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.616f, 0.722f, 1.f, 1.f));
        ImGui::SetNextItemWidth(fs * 26.f);
        ImGui::SliderFloat("##origine", &originT_, 0.f, doc_.FigureDuration(), "%.2f s");
        ImGui::PopStyleColor(5);
        originFocus_ = originPlaying_ || ImGui::IsItemActive() || ImGui::IsItemHovered();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the game's pose at that moment (the blue ghost).");
        ImGui::SameLine();
        // 2026-11-25: was "Take this pose (all / bone + children)", unclear to the user
        const bool part = doc_.IsEditable(sel_);
        if (LandingLockedHere()) ImGui::BeginDisabled();   // 2026-11-27: nothing is posed on a locked landing
        const bool copyPose = ui::Button(part ? "Copy the game's pose to this part" : "Copy the game's pose to the whole body");
        if (LandingLockedHere()) ImGui::EndDisabled();
        if (copyPose) {
            doc_.Checkpoint("game pose");
            doc_.TakeOriginPose(originT_, part ? sel_ : -1);
            Status("game pose taken");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the game's pose shown by this slider (%.2f s) to the playhead (%.2f s).\nUse it to put a part back as in the game, or to reuse a game pose from another moment.\nA part picked: that part and what it carries (the arm with the hand). Nothing picked: the whole body.\nWith \"Auto key\", the key is set right away; Ctrl+Z undoes it.", originT_, t_);
    } else {
        originFocus_ = false;
        ImGui::TextColored(theme::Bad, "game animation not recorded yet: do \"%s\" once in game", VariantTitleUi(var_).c_str());
    }
}

// interpolation of the key of a row at k (the first bone of the row that has one there)
static int KeyShape(const EditorDoc& d, float k, const std::vector<int>& bones) {
    if (bones.empty()) return d.KeyInterp(k, -1);
    for (int b : bones) {
        const int ip = d.KeyInterp(k, b);
        if (ip >= 0) return ip;
    }
    return -1;
}

void App::TimelinePanel() {
    const Layout L = Lay();
    Place(L.time.x, L.time.y, L.time.w, L.time.h);
    BeginPanel("Timeline", kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);   // redesign step 5: no title row
    // selected keys that no longer exist (undo, another clip, deleted) leave the selection
    keySel_.erase(std::remove_if(keySel_.begin(), keySel_.end(), [&](const KeySel& k) {
        const std::vector<float> ts = k.bones.empty() ? doc_.KeyTimes(-1) : doc_.KeyTimesBones(k.bones);
        for (float x : ts) if (std::fabs(x - k.t) < 1e-3f) return false;
        return true;
    }), keySel_.end());
    Clip& c = doc_.clip();
    const float fs = ImGui::GetFontSize();
    static const char* kInterpName[3] = {"smooth", "linear", "constant"};

    TimelineTransport();   // redesign step 5: one header line (the end warnings are its chip)
    TimelineGameRow();

    // rows: "All", then one row per limb group, unfolded into one row per bone
    struct DRow { std::string label; std::vector<int> bones; int primary; int depth; std::string gid; bool fold; };
    if (sel_ != prevSel_) {   // a bone picked anywhere: unfold its group and bring it into view
        prevSel_ = sel_;
        const std::string g = doc_.GroupOfBone(sel_);
        if (!g.empty()) { expanded_.insert(g); scrollToSel_ = true; }
    }
    std::vector<DRow> rows;
    rows.push_back(DRow{"All", {}, -1, 0, "", false});
    // 2026-11-27: one timeline, two files: the same rows on both sides of the violet line
    const std::vector<EditorDoc::KeyGroup> groups = hasComp_ ? EditorDoc::MergeKeyGroups(doc_.KeyGroups(allRows_), comp_.doc.KeyGroups(allRows_))
                                                             : doc_.KeyGroups(allRows_);
    for (auto& G : groups) {
        const bool open = expanded_.count(G.id) != 0;
        const int prim = !G.children.empty() ? G.children.front().primary : G.bones.front();
        rows.push_back(DRow{G.label, G.bones, prim, 0, G.id, !G.children.empty()});
        if (open) for (auto& r : G.children) rows.push_back(DRow{r.label, r.bones, r.primary, 1, G.id, false});
    }
    const float labelW = fs * 9.f, rowH = compactRows_ ? fs * 1.05f : fs * 1.35f;
    ImGuiIO& io = ImGui::GetIO();
    // UI Phase 4: the same width for the ruler and the rows (room kept for the rows' scrollbar)
    const float W = std::max(50.f, ImGui::GetContentRegionAvail().x - labelW - ImGui::GetStyle().ScrollbarSize);
    const float dur = c.duration;   // the open part (its own times)
    // 2026-11-27 (one timeline, two files): the view covers the trick and its landing. X / T work in the open part's
    // time, XS / TS in the shared time (the trick's times are shared times: it starts at 0)
    const TwoPartTime tp = Parts();
    const float off = hasComp_ ? JoinTime(tp, inLanding_ ? 1 : 0, 0.f) : 0.f;   // where the open part starts
    const float total = hasComp_ ? tp.Total() : dur;
    const float trickDur = hasComp_ ? tp.first : dur;
    const EditorDoc& td = TrickDoc();
    const bool roOpen = LandingLockedHere();   // a locked landing: its keys are not shown, nothing is edited there
    const TrickMarkers hang = td.IsTrickClip() ? td.Markers() : TrickMarkers{};   // 2026-11-25 landing sync
    {   // another clip or variant: show it whole; the view always stays inside the timeline
        const std::string key = (inLanding_ ? comp_.fileName + "|" + FigureKey(comp_.var) : fileName_ + "|" + FigureKey(var_)) + (hasComp_ ? "|two" : "");
        if (key != tviewKey_) { tviewKey_ = key; tview_.Fit(); }
        tview_.Clamp(total);
        const float ts = t_ + off;
        if (std::fabs(ts - tviewLastT_) > 1e-5f) { tview_.Follow(ts, total, playing_ ? 0.f : 0.1f); tviewLastT_ = ts; }
    }
    float trackX = ImGui::GetCursorScreenPos().x + labelW, top = 0.f, bottom = 0.f;
    auto XS = [&](float g) { return tview_.ToX(g, trackX + 8.f, W - 16.f, total); };
    auto X = [&](float t) { return XS(t + off); };
    auto TS = [&](float x) { return std::clamp(tview_.ToT(x, trackX + 8.f, W - 16.f, total), 0.f, total); };
    auto T = [&](float x) { return std::clamp(TS(x) - off, 0.f, dur); };
    const float xDiv = hasComp_ ? tp.first + tp.gap * 0.5f : -1.f;   // the ground contact: the trick ends, the landing begins
    auto InView = [&](float x) { return x >= trackX + 2.f && x <= trackX + W - 2.f; };
    const float rulerH = fs * 1.4f;
    const ImVec2 rulerP(trackX, ImGui::GetCursorScreenPos().y);
    {   // ruler: time labels, playhead handle; click or drag = move the time
        ImDrawList* rd = ImGui::GetWindowDrawList();
        rd->AddText(ImVec2(rulerP.x - labelW + fs * 0.3f, rulerP.y + (rulerH - fs) * 0.5f), U32(kMuted), "Time");
        ImGui::SetCursorScreenPos(rulerP);
        ImGui::InvisibleButton("##regle", ImVec2(W, rulerH), ImGuiButtonFlags_MouseButtonLeft);
        const bool rulerHov = ImGui::IsItemHovered();
        if (doc_.EffectiveDrive() != Drive::ManualRatio) {
            const float tn = std::min(std::round(TS(io.MousePos.x) / kFrame) * kFrame, total);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) JumpShared(tn);
            else if (ImGui::IsItemActive() && !doc_.HasPending() && std::fabs(tn - (t_ + off)) > 1e-4f) JumpShared(tn);
        }
        rd->AddRectFilled(rulerP, ImVec2(rulerP.x + W, rulerP.y + rulerH), theme::U32(theme::Bg0));
        rd->PushClipRect(rulerP, ImVec2(rulerP.x + W, rulerP.y + rulerH), true);
        const float pps = (W - 16.f) / tview_.Span(total);
        const float minor = RulerStep(pps, fs * 0.45f), major = RulerStep(pps, fs * 4.5f);
        const float a0 = tview_.Start(total), a1 = a0 + tview_.Span(total);
        for (int i = (int)std::floor(a0 / minor); i * minor <= a1 + 1e-4f; i++) {
            const float t = i * minor;
            if (t < -1e-4f || t > total + 1e-4f) continue;
            const float q = t / major;
            const bool isMajor = std::fabs(q - std::round(q)) < 1e-3f;
            const float x = XS(t);
            rd->AddLine(ImVec2(x, rulerP.y + (isMajor ? rulerH * 0.45f : rulerH * 0.75f)), ImVec2(x, rulerP.y + rulerH), theme::U32(theme::Text3));
            if (isMajor) {
                char lb[32];
                if (major < 0.099f) snprintf(lb, sizeof(lb), "%d", (int)std::lround(t / kFrame));   // very close: frame numbers
                else snprintf(lb, sizeof(lb), major < 1.f ? "%.2f s" : "%.0f s", t);
                rd->AddText(ImVec2(x + 3.f, rulerP.y + 1.f), theme::U32(theme::Text2), lb);
            }
        }
        if (total > 0.f) {   // end of the clip (of the landing, on one timeline)
            const float xe = XS(total);
            rd->AddLine(ImVec2(xe, rulerP.y), ImVec2(xe, rulerP.y + rulerH), IM_COL32(200, 200, 210, 160), 1.f);
        }
        if (xDiv > 0.f) {   // the ground contact
            const float xd = XS(xDiv);
            rd->AddLine(ImVec2(xd, rulerP.y), ImVec2(xd, rulerP.y + rulerH), IM_COL32(190, 150, 255, 230), 2.f);
        }
        const float xp = X(t_);   // playhead handle
        if (InView(xp)) {
            const float hw = fs * 0.4f;
            const ImVec2 tri[3] = {ImVec2(xp - hw, rulerP.y + rulerH - hw * 1.6f), ImVec2(xp + hw, rulerP.y + rulerH - hw * 1.6f), ImVec2(xp, rulerP.y + rulerH)};
            rd->AddTriangleFilled(tri[0], tri[1], tri[2], theme::U32(theme::Accent));
        }
        rd->PopClipRect();
        if (rulerHov) ImGui::SetTooltip(major < 0.099f ? "Click or drag: change the time · wheel: zoom\nThe numbers are frames (30 per second)." : "Click or drag: change the time · wheel: zoom");
    }
    // redesign step 5 (2026-11-27): the story of the clip under the ruler. Trick: takeoff, flip, catch (hang time:
    // it stretches with the jump in game), landing, ground; riding with a dismount: loop, dismount
    if ((td.IsTrickClip() || doc_.HasDismount()) && dur > 0.f) {
        const float ribH = std::round(fs * 1.15f);
        const ImVec2 rp(trackX, ImGui::GetCursorScreenPos().y);
        ImDrawList* rd = ImGui::GetWindowDrawList();
        rd->AddText(ImVec2(rp.x - labelW + fs * 0.3f, rp.y + (ribH - fs) * 0.5f), U32(kMuted), td.IsTrickClip() ? "Trick" : "Parts");
        ImGui::SetCursorScreenPos(rp);
        ImGui::InvisibleButton("##ribbon", ImVec2(W, ribH));
        const bool ribHov = ImGui::IsItemHovered();
        struct Seg { float a, b; ImVec4 col; const char* label; const char* tip; };
        std::vector<Seg> segs;
        float flipAt = -1.f;   // the flip, as a tick inside the air part
        if (td.IsTrickClip() && hang.ok) {
            // user check 2026-11-27: built from the same markers as the yellow band of the rows (TrickMarkers): a recording
            // without "catch" or "ground" events still shows its air part (hang time from the takeoff, see trick_sync.h)
            const float air = std::clamp(hang.air, 0.f, trickDur), ca = std::clamp(hang.catchT, air, trickDur);
            const float la = std::clamp(hang.land, ca, trickDur), gr = std::clamp(hang.ground, la, trickDur);
            if (air > 1e-3f) segs.push_back(Seg{0.f, air, theme::Alpha(theme::Text3, 0.30f), "pop", "Before the takeoff: the pop."});
            if (ca > air + 1e-3f) segs.push_back(Seg{air, ca, theme::Alpha(theme::Ghost, 0.32f), "flip", "From the takeoff to the catch: the board flips under the feet."});
            if (la > ca + 1e-3f) segs.push_back(Seg{ca, la, theme::Alpha(theme::Warn, 0.24f), "in the air",
                "In the air (the yellow band of the rows): in game this part stretches with the jump height\n(higher jump: longer), so your keys here follow the real jump.\nThe legs go back to the game 0.12 s after the catch."});
            if (gr > la + 1e-3f) segs.push_back(Seg{la, gr, theme::Alpha(theme::Ghost, 0.22f), "landing", "Coming down to the ground."});
            if (trickDur > gr + 1e-3f) segs.push_back(Seg{gr, trickDur, theme::Alpha(theme::Good, 0.22f), "ground", "On the ground: from here the landing clip of this trick takes over (if there is one)."});
            const float fl = td.PhaseStart(2);
            if (fl >= air && fl < la && !(ca > air + 1e-3f)) flipAt = fl;
        }
        if (doc_.HasDismount()) {
            const float ll = LoopLength(c);
            segs.push_back(Seg{0.f, ll, theme::Alpha(theme::Ghost, 0.18f), "loop", "The riding loop."});
            segs.push_back(Seg{ll, dur, ImVec4(0.745f, 0.588f, 1.f, 0.24f), "dismount", "Played once when the feet leave the board."});
        }
        if (hasComp_)   // 2026-11-27: the landing of this trick, after the ground contact
            segs.push_back(Seg{tp.SecondStart(), total, landLocked_ ? ImVec4(0.45f, 0.47f, 0.52f, 0.30f) : ImVec4(0.745f, 0.588f, 1.f, 0.30f),
                               landLocked_ ? "landing · locked (the game's)" : "landing · yours",
                               landLocked_ ? "The landing of this trick, after the ground contact. LOCKED: the game plays its own landing,\nnothing can be posed here. Unlock it in Clip settings to make it yours."
                                           : "The landing of this trick, after the ground contact: your landing clip.\nIn game it starts at the real ground contact. Save writes it with the trick."});
        rd->PushClipRect(rp, ImVec2(rp.x + W, rp.y + ribH), true);
        const char* tip = nullptr;
        for (const Seg& sg : segs) {
            const float xa = std::max(XS(sg.a), rp.x), xb = std::min(XS(sg.b), rp.x + W);
            if (xb <= xa + 1.f) continue;
            rd->AddRectFilled(ImVec2(xa + 1.f, rp.y + 2.f), ImVec2(xb - 1.f, rp.y + ribH - 2.f), theme::U32(sg.col), fs * 0.25f);
            const ImVec4 tc(std::min(1.f, sg.col.x + 0.35f), std::min(1.f, sg.col.y + 0.35f), std::min(1.f, sg.col.z + 0.35f), 1.f);
            const ImVec2 ts = ImGui::CalcTextSize(sg.label);
            if (ts.x + 8.f < xb - xa) rd->AddText(ImVec2(xa + 5.f, rp.y + (ribH - ts.y) * 0.5f), theme::U32(tc), sg.label);
            if (ribHov && io.MousePos.x >= xa && io.MousePos.x < xb) tip = sg.tip;
        }
        if (flipAt >= 0.f && InView(XS(flipAt))) {   // the flip inside the air part (recordings without a catch event)
            const float xf = XS(flipAt);
            rd->AddLine(ImVec2(xf, rp.y + 3.f), ImVec2(xf, rp.y + ribH - 3.f), IM_COL32(157, 184, 255, 220), 1.5f);
            rd->AddText(ImVec2(xf + 4.f, rp.y + (ribH - fs) * 0.5f), IM_COL32(157, 184, 255, 255), "flip");
            if (ribHov && std::fabs(io.MousePos.x - xf) < fs * 1.5f) tip = "The flip starts here (the board turns under the feet).";
        }
        const float xp = X(t_);
        if (InView(xp)) rd->AddLine(ImVec2(xp, rp.y), ImVec2(xp, rp.y + ribH), theme::U32(theme::Accent), 2.f);
        rd->PopClipRect();
        if (tip) ImGui::SetTooltip("%s", tip);
    }
    // zoom / pan with the mouse over the ruler and the rows
    const ImVec2 winMax(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
    const bool overTracks = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MousePos.x >= trackX && io.MousePos.x <= trackX + W &&
                            io.MousePos.y >= rulerP.y && io.MousePos.y <= winMax.y;
    const bool overRuler = overTracks && io.MousePos.y <= rulerP.y + rulerH;
    const float wheelPan = io.MouseWheelH != 0.f ? io.MouseWheelH : (io.KeyShift ? io.MouseWheel : 0.f);
    if (overTracks && io.MouseWheel != 0.f && (io.KeyCtrl || (overRuler && !io.KeyShift)))
        tview_.ZoomAt(std::pow(1.25f, io.MouseWheel), TS(io.MousePos.x), total);
    else if (overTracks && wheelPan != 0.f) tview_.Pan(-wheelPan * tview_.Span(total) * 0.15f, total);
    if (overTracks && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) tview_.Pan(-io.MouseDelta.x / std::max(1.f, W - 16.f) * tview_.Span(total), total);
    const bool wheelOwned = overTracks && (io.KeyCtrl || io.KeyShift || overRuler);
    ImGui::BeginChild("##feuille", ImVec2(0, 0), ImGuiChildFlags_None, wheelOwned ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto Snap = [&](float x) { return std::min(std::round(T(x) / kFrame) * kFrame, c.duration); };
    struct Hit { std::vector<int> bones; float t, x, y; };
    std::vector<Hit> hits;   // every key drawn this frame (box selection)
    bool clickedKey = false;
    for (size_t ri = 0; ri < rows.size(); ri++) {
        const DRow& r = rows[ri];
        ImGui::PushID((int)ri);
        {   // redesign step 5: a colour bar per body part, the colours of the stage (head violet, chest yellow, arms and
            // hands cyan, legs green, pelvis orange); the bone rows of an open group a lighter bar
            const ImVec2 bp = ImGui::GetCursorScreenPos();
            ImU32 bc = 0;
            const std::string& g = r.gid;
            if (g == "head") bc = IM_COL32(180, 140, 255, 255);
            else if (g == "chest") bc = IM_COL32(240, 210, 74, 255);
            else if (g == "armL" || g == "armR" || g == "handL" || g == "handR") bc = IM_COL32(54, 197, 240, 255);
            else if (g == "legL" || g == "legR") bc = IM_COL32(95, 211, 138, 255);
            else if (g == "pelvis") bc = IM_COL32(255, 148, 66, 255);
            if (bc) {
                if (r.depth) bc = (bc & 0x00FFFFFFu) | 0x70000000u;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bp.x, bp.y + 1.f), ImVec2(bp.x + 3.f, bp.y + rowH - 1.f), bc, 1.5f);
            }
        }
        const bool inRow = !r.bones.empty() && std::find(r.bones.begin(), r.bones.end(), sel_) != r.bones.end();
        const bool exact = inRow && (r.depth == 1 || !r.fold);   // the bone's own row (or a group without children)
        const float indent = r.depth ? fs * 2.0f : 0.f;
        if (r.fold) {
            const bool open = expanded_.count(r.gid) != 0;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
            if (ImGui::ArrowButton("##plier", open ? ImGuiDir_Down : ImGuiDir_Right)) { if (open) expanded_.erase(r.gid); else expanded_.insert(r.gid); }
            ImGui::PopStyleVar();
            ImGui::SameLine(0.f, fs * 0.2f);
        } else ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (r.depth ? indent : fs * 1.2f));
        if (inRow) ImGui::PushStyleColor(ImGuiCol_Text, exact ? theme::AccentHi : theme::Text);
        const float lw = labelW - ImGui::GetCursorPosX() - fs * 0.5f;
        if (ImGui::Selectable(r.label.c_str(), inRow, 0, ImVec2(std::max(fs, lw), rowH)) && r.primary >= 0) {
            if (doc_.IsEditable(r.primary) || r.primary == doc_.PelvisBone()) { sel_ = r.primary; prevSel_ = sel_; }
            else Status(r.label + " is locked (kept as it is)");
            if (r.fold && !expanded_.count(r.gid)) expanded_.insert(r.gid);
        }
        if (inRow) ImGui::PopStyleColor();
        if (scrollToSel_ && exact) { ImGui::SetScrollHereY(0.5f); scrollToSel_ = false; }
        ImGui::SameLine(labelW);
        const ImVec2 q0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##track", ImVec2(W, rowH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        trackX = q0.x;
        if (ri == 0) top = q0.y;
        bottom = q0.y + rowH;
        const ImU32 bg = ri == 0 ? theme::U32(theme::Surface) : exact ? IM_COL32(58, 37, 24, 255) : inRow ? IM_COL32(40, 31, 26, 255)
                       : r.depth ? theme::U32(theme::Bg1) : (ri % 2 ? IM_COL32(25, 28, 35, 255) : IM_COL32(21, 24, 30, 255));   // orange tints = the selected part
        dl->AddRectFilled(q0, ImVec2(q0.x + W, q0.y + rowH), bg);
        if (hang.ok && hang.land > hang.catchT + 1e-3f) {   // 2026-11-25: the hang time, stretched in game with the jump height
            const float xa = std::clamp(XS(hang.catchT), q0.x, q0.x + W), xb = std::clamp(XS(hang.land), q0.x, q0.x + W);
            if (xb > xa) dl->AddRectFilled(ImVec2(xa, q0.y), ImVec2(xb, q0.y + rowH), IM_COL32(200, 170, 60, 38));
        }
        if (doc_.HasDismount()) {   // 2026-11-25: the dismount part of a riding clip, tinted
            const float xd = std::clamp(X(LoopLength(c)), q0.x, q0.x + W);
            dl->AddRectFilled(ImVec2(xd, q0.y), ImVec2(q0.x + W, q0.y + rowH), IM_COL32(120, 90, 160, 50));
        }
        {   // grid: the ruler's labelled steps
            const float major = RulerStep((W - 16.f) / tview_.Span(total), fs * 4.5f);
            const float a0 = tview_.Start(total), a1 = a0 + tview_.Span(total);
            for (int i = (int)std::ceil(a0 / major - 1e-4f); i * major <= std::min(a1, total) + 1e-4f; i++)
                dl->AddLine(ImVec2(XS(i * major), q0.y), ImVec2(XS(i * major), q0.y + rowH), theme::U32(theme::Line, 0.7f));
        }
        if (total > 0.f && XS(total) < q0.x + W) dl->AddRectFilled(ImVec2(XS(total) + 8.f, q0.y), ImVec2(q0.x + W, q0.y + rowH), theme::U32(theme::Bg0));
        if (hasComp_) {   // 2026-11-27: the landing part; locked = the game's (greyed), else yours (a violet tint)
            const float xa = std::clamp(XS(tp.SecondStart()), q0.x, q0.x + W), xb = std::clamp(XS(total), q0.x, q0.x + W);
            if (xb > xa) dl->AddRectFilled(ImVec2(xa, q0.y), ImVec2(xb, q0.y + rowH), landLocked_ ? IM_COL32(11, 13, 17, 150) : IM_COL32(120, 90, 160, 34));
            const float xd = XS(xDiv);
            if (xd >= q0.x && xd <= q0.x + W) dl->AddLine(ImVec2(xd, q0.y), ImVec2(xd, q0.y + rowH), IM_COL32(190, 150, 255, 170), 1.5f);
            if (ri == 0 && landLocked_ && xb - xa > fs * 6.f) dl->AddText(ImVec2(xa + fs * 0.5f, q0.y + (rowH - fs) * 0.5f), theme::U32(theme::Text3), "locked: the game's landing");
            // the other part's keys, read-only (click: go there); a locked landing has none to show. 2026-11-27: drawn
            // as when that part is open (colour, shape, padlocks), so nothing flashes when the playhead changes part
            const bool otherIsLockedLanding = !inLanding_ && landLocked_;
            if (!otherIsLockedLanding) {
                const EditorDoc& od = comp_.doc;
                const float ooff = JoinTime(tp, inLanding_ ? 0 : 1, 0.f), odur = od.clip().duration;
                const std::vector<float> ok2 = r.bones.empty() ? od.KeyTimes(-1) : od.KeyTimesBones(r.bones);
                const float my2 = q0.y + rowH * 0.5f, rr2 = std::min(fs * 0.35f, rowH * 0.32f);
                auto padlock = [&](float x) {
                    const ImU32 lc = IM_COL32(150, 175, 205, 225);
                    dl->AddRectFilled(ImVec2(x - rr2 * 0.75f, my2 - rr2 * 0.05f), ImVec2(x + rr2 * 0.75f, my2 + rr2 * 0.85f), lc, rr2 * 0.15f);
                    dl->PathArcTo(ImVec2(x, my2 - rr2 * 0.05f), rr2 * 0.48f, 3.14159f, 6.28318f, 10);
                    dl->PathStroke(lc, 0, std::max(1.5f, rr2 * 0.25f));
                };
                auto otherLocked = [&](float k) { return od.LockedAt(k) && !(od.IsTrickClip() && std::fabs(k - odur) < 1e-3f && !r.bones.empty() && std::all_of(r.bones.begin(), r.bones.end(), [&](int bn) { return bn >= 0 && bn < od.BoneCount() && IsLegBone(od.skeleton().names[bn]); })); };
                for (float k : ok2) {
                    const float x = XS(k + ooff);
                    if (!InView(x)) continue;
                    if (otherLocked(k)) { padlock(x); continue; }
                    const ImVec2 pts2[4] = {ImVec2(x, my2 - rr2), ImVec2(x + rr2, my2), ImVec2(x, my2 + rr2), ImVec2(x - rr2, my2)};
                    const int ip = KeyShape(od, k, r.bones);
                    const ImU32 col = ri == 0 ? theme::U32(theme::Text2, 0.88f) : IM_COL32(232, 193, 80, 225);
                    if (ip == EditorDoc::IP_Step) dl->AddRectFilled(ImVec2(x - rr2 * 0.8f, my2 - rr2 * 0.8f), ImVec2(x + rr2 * 0.8f, my2 + rr2 * 0.8f), col);
                    else dl->AddConvexPolyFilled(pts2, 4, col);
                    if (ip == EditorDoc::IP_Linear) dl->AddLine(ImVec2(x - rr2 * 0.5f, my2), ImVec2(x + rr2 * 0.5f, my2), IM_COL32(40, 40, 40, 255), 2.f);
                }
                if (ri == 0) for (float lt : {0.f, odur})
                    if (od.LockedAt(lt) && InView(XS(lt + ooff)) && std::none_of(ok2.begin(), ok2.end(), [&](float k) { return std::fabs(k - lt) < 1e-3f; })) padlock(XS(lt + ooff));
            }
        }
        const std::vector<float> keys = roOpen ? std::vector<float>() : r.bones.empty() ? doc_.KeyTimes(-1) : doc_.KeyTimesBones(r.bones);
        const bool draggingThis = dragKey_ && dragBones_ == r.bones;
        // 2026-11-26 (task 3): a locked start / end key is a padlock: not selectable, not movable, not deletable
        auto drawLock = [&](float x, float my2, float rr) {
            const ImU32 lc = IM_COL32(150, 175, 205, 255);
            dl->AddRectFilled(ImVec2(x - rr * 0.75f, my2 - rr * 0.05f), ImVec2(x + rr * 0.75f, my2 + rr * 0.85f), lc, rr * 0.15f);
            dl->PathArcTo(ImVec2(x, my2 - rr * 0.05f), rr * 0.48f, 3.14159f, 6.28318f, 10);
            dl->PathStroke(lc, 0, std::max(1.5f, rr * 0.25f));
        };
        auto lockedKey = [&](float k) { return doc_.LockedAt(k) && !(doc_.IsTrickClip() && std::fabs(k - dur) < 1e-3f && !r.bones.empty() && std::all_of(r.bones.begin(), r.bones.end(), [&](int b) { return b >= 0 && b < doc_.BoneCount() && IsLegBone(doc_.skeleton().names[b]); })); };
        const float my = q0.y + rowH * 0.5f, rad = std::min(fs * 0.35f, rowH * 0.32f);
        for (float k : keys) {
            const float kt = (draggingThis && std::fabs(k - dragFromT_) < 1e-3f) ? dragKeyT_ : k;
            const float x = X(kt);
            if (!InView(x)) continue;   // outside the zoomed part
            if (lockedKey(k)) { drawLock(x, q0.y + rowH * 0.5f, std::min(fs * 0.35f, rowH * 0.32f)); continue; }
            const bool picked = KeySelected(r.bones, k);
            const float rr = picked ? rad * 1.35f : rad;
            const ImVec2 pts[4] = {ImVec2(x, my - rr), ImVec2(x + rr, my), ImVec2(x, my + rr), ImVec2(x - rr, my)};
            const int ip = KeyShape(doc_, k, r.bones);
            ImU32 col = ri == 0 ? theme::U32(theme::Text2) : IM_COL32(232, 193, 80, 255);
            if (std::fabs(kt - t_) < 1e-3f) col = kColSel;
            if (ip == EditorDoc::IP_Step) dl->AddRectFilled(ImVec2(x - rr * 0.8f, my - rr * 0.8f), ImVec2(x + rr * 0.8f, my + rr * 0.8f), col);   // square: constant
            else dl->AddConvexPolyFilled(pts, 4, col);
            if (ip == EditorDoc::IP_Linear) dl->AddLine(ImVec2(x - rr * 0.5f, my), ImVec2(x + rr * 0.5f, my), IM_COL32(40, 40, 40, 255), 2.f);   // dash: linear
            if (picked) dl->AddPolyline(pts, 4, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 2.f);
            hits.push_back(Hit{r.bones, k, x, my});
        }
        if (ri == 0 && !roOpen) for (float lt : {0.f, dur})   // the padlocks also show on an empty clip
            if (doc_.LockedAt(lt) && InView(X(lt)) && std::none_of(keys.begin(), keys.end(), [&](float k) { return std::fabs(k - lt) < 1e-3f; })) drawLock(X(lt), my, rad);
        float hit = -1.f;
        for (float k : keys) if (!lockedKey(k) && InView(X(k)) && std::fabs(X(k) - io.MousePos.x) < fs * 0.5f) hit = k;
        bool lockHover = false;
        for (float lt : {0.f, dur}) if (!roOpen && doc_.LockedAt(lt) && InView(X(lt)) && std::fabs(X(lt) - io.MousePos.x) < fs * 0.5f) lockHover = true;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (r.primary >= 0 && (doc_.IsEditable(r.primary) || r.primary == doc_.PelvisBone()) && !inRow) { sel_ = r.primary; prevSel_ = sel_; }
            if (io.KeyShift) {
                if (hit >= 0.f) { ToggleKey(r.bones, hit); clickedKey = true; }
                else { boxing_ = true; boxX0_ = io.MousePos.x; boxY0_ = io.MousePos.y; }
            } else if (hit >= 0.f) {
                clickedKey = true;
                if (!KeySelected(r.bones, hit)) { keySel_.clear(); keySel_.push_back(KeySel{r.bones, hit}); }
                if (doc_.HasPending()) JumpShared(hit + off);
                else if (!doc_.IsHold()) { dragKey_ = true; dragBones_ = r.bones; dragFromT_ = dragKeyT_ = hit; dragClickX_ = io.MousePos.x; }
            } else {
                keySel_.clear();
                if (doc_.EffectiveDrive() != Drive::ManualRatio) JumpShared(std::min(std::round(TS(io.MousePos.x) / kFrame) * kFrame, total));   // 2026-11-27: may go to the other part
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hit >= 0.f) {
            if (!KeySelected(r.bones, hit)) { keySel_.clear(); keySel_.push_back(KeySel{r.bones, hit}); }
            openKeyMenu_ = true;
        }
        if (ImGui::IsItemHovered() && hit < 0.f && lockHover)
            ImGui::SetTooltip("%s", "Locked: the game's pose, so the clip joins the game smoothly.\nPose between the start and the end (Clip settings: Lock start and end).");
        if (ImGui::IsItemHovered() && hit >= 0.f) {
            const int ip = doc_.KeyInterp(hit, r.bones.empty() ? -1 : r.bones.front());
            ImGui::SetTooltip("%.2f s · %s · %s\nclick: select, drag: move\nShift+click: add to the selection, right-click: menu", hit, r.label.c_str(), ip >= 0 ? kInterpName[ip] : "?");
        }
        ImGui::PopID();
    }
    (void)clickedKey;
    if (td.IsTrickClip() && bottom > top) {
        static const char* kPhaseLabel[6] = {"", "takeoff", "flip", "catch", "landing", "ground"};
        float lastEnd = -1e9f;
        for (int ph = 1; ph <= 5; ph++) {
            const float pt = td.PhaseStart(ph);
            if (pt < 0.f || pt > trickDur + 1e-3f) continue;
            const float x = XS(std::min(pt, trickDur));
            if (!InView(x)) continue;
            dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), ph == 5 ? theme::U32(theme::Good, 0.55f) : theme::U32(theme::Ghost, 0.40f), 1.f);   // names: the ribbon (step 5)
            (void)lastEnd; (void)kPhaseLabel;
        }
    }
    if (doc_.HasDismount() && bottom > top) {
        const float xd = X(LoopLength(c));
        if (InView(xd)) dl->AddLine(ImVec2(xd, top), ImVec2(xd, bottom), IM_COL32(190, 150, 255, 220), 2.f);
    }
    if (bottom > top && InView(X(t_))) dl->AddLine(ImVec2(X(t_), top), ImVec2(X(t_), bottom), theme::U32(theme::Accent), 2.f);
    // box selection
    if (boxing_) {
        const float x0 = std::min(boxX0_, io.MousePos.x), x1 = std::max(boxX0_, io.MousePos.x);
        const float y0 = std::min(boxY0_, io.MousePos.y), y1 = std::max(boxY0_, io.MousePos.y);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), theme::U32(theme::Accent, 0.12f));
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), theme::U32(theme::Accent, 0.8f));
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            boxing_ = false;
            int added = 0;
            for (auto& h : hits)
                if (h.x >= x0 - 2.f && h.x <= x1 + 2.f && h.y >= y0 - 2.f && h.y <= y1 + 2.f && !KeySelected(h.bones, h.t)) { keySel_.push_back(KeySel{h.bones, h.t}); added++; }
            if (added) Status(std::to_string(keySel_.size()) + " key(s) selected");
        }
    }
    if (dragKey_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f)) dragKeyT_ = std::clamp(dragFromT_ + (Snap(io.MousePos.x) - Snap(dragClickX_)), 0.f, c.duration);
    if (dragKey_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragKey_ = false;
        if (std::fabs(dragKeyT_ - dragFromT_) > 1e-3f) {
            const EditorDoc::State st = doc_.SaveState();
            const bool ok = dragBones_.empty() ? doc_.MoveKeys(dragFromT_, dragKeyT_) : doc_.MoveKeysBones(dragBones_, dragFromT_, dragKeyT_);
            if (ok) {
                doc_.CheckpointState(st, "move key");
                if (doc_.EffectiveDrive() != Drive::ManualRatio) t_ = dragKeyT_;
                keySel_.clear(); keySel_.push_back(KeySel{dragBones_, dragKeyT_});
            } else Status(doc_.LockedAt(dragKeyT_) ? "cannot move: the start and end are locked to the game's pose" : "cannot move: a key already exists at that time on this row");
        } else if (doc_.EffectiveDrive() != Drive::ManualRatio) JumpShared(dragFromT_ + off);
        dragBones_.clear();
    }
    if (openKeyMenu_) { ImGui::OpenPopup("##menu_cles"); openKeyMenu_ = false; }
    if (ImGui::BeginPopup("##menu_cles")) {
        ImGui::TextDisabled("%d key(s)", (int)keySel_.size());
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) DeleteSelectedKeys();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) CopyNow();
        if (ImGui::MenuItem("Paste at the playhead", "Ctrl+V", false, doc_.CanPaste())) PasteNow();
        ImGui::Separator();
        for (int m = 0; m < 3; m++) if (ImGui::MenuItem(kInterpName[m])) SetSelectedInterp(m);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::End();
}
