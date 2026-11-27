// Session Studio -- Inspector card (redesign step 4): the selection, or the clip settings (+ advanced in Studio).
#include "app_internal.h"

using namespace app_detail;

// redesign step 4 (2026-11-27): a card at the right of the 3D view, a child window of the stage (nothing under it can be
// grabbed). It shows what is selected, or the clip settings when they are open (timeline: "Clip settings").
void App::InspectorCard(const ImVec2& pos, float w, float maxH) {
    const float fs = ImGui::GetFontSize();
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Alpha(theme::Surface, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Line);
    // on the card's surface colour, the panel buttons and fields would not show: lighter buttons, darker fields
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Raised);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.212f, 0.239f, 0.294f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.270f, 0.302f, 0.365f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::Bg1);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.098f, 0.110f, 0.141f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.125f, 0.141f, 0.176f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::round(fs * 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(fs * 0.7f), std::round(fs * 0.6f)));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.f), ImVec2(w, std::max(fs * 6.f, maxH)));
    if (ImGui::BeginChild("##card", ImVec2(w, 0.f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY)) {
        ImGui::PopStyleVar();   // the content keeps the normal spacing
        // header: what the card shows, and its close button on the right
        std::string title, sub;
        ImVec4 subCol = theme::Text2;
        if (clipPanel_) { title = "Clip settings"; sub = fileName_.empty() ? std::string("new clip (not saved yet)") : fileName_; }
        else if (sel_ >= 0 && sel_ < doc_.BoneCount()) {
            std::string what, tool;
            ToolInfo(what, tool, subCol);
            title = what;
            sub = tool;
        } else { title = "Nothing selected"; sub = "pick a body part"; }
        if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title.c_str());
        if (theme::Strong()) ImGui::PopFont();
        const float bw = theme::HasIcons() ? ImGui::GetFrameHeight() : ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw));
        if (ui::IconButton("cardclose", theme::Icon::Close, "x", false, ui::Kind::Ghost)) {
            if (clipPanel_) clipPanel_ = false;
            else { cardOpen_ = false; SaveSetting(settings_, "ui.card", "0"); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(clipPanel_ ? "Close the clip settings (back to the selection)." : "Hide the card: the 3D view takes the whole width.\nThe \"Inspector\" button (top right of the view) brings it back.");
        ImGui::TextColored(subCol, "%s", sub.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.f);   // long lines wrap inside the card
        if (clipPanel_) {
            InspectorClip();
            if (studio_) {
                ImGui::Spacing();
                ImGui::SeparatorText("Advanced");
                InspectorAdvanced();
            }
        } else InspectorSelection();
        ImGui::PopTextWrapPos();
    } else ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(8);
}

void App::InspectorSelection() {
    const int n = doc_.BoneCount();
    Clip& c = doc_.clip();
    const bool lockedHere = sel_ >= 0 && doc_.LockedAt(doc_.KeyTime(t_));   // 2026-11-26: locked start / end (task 3)
    if (lockedHere) ImGui::TextColored(theme::Warn, "%s", "Start and end are locked to the game's pose:\npose between them (Clip settings to unlock).");
    if (sel_ >= 0 && sel_ == doc_.PelvisBone()) {
        ImGui::BeginDisabled(lockedHere);
        PelvisProps();
        ImGui::EndDisabled();
    } else if (sel_ >= 0 && sel_ < n) {
        const std::string& name = doc_.skeleton().names[sel_];
        ImGui::SeparatorText(("Bone: " + Short(name)).c_str());
        const bool legFrozen = doc_.LegFrozenAt(sel_, t_);   // 2026-11-26: the game takes the legs back from the catch
        if (legFrozen && !lockedHere) ImGui::TextColored(theme::Warn, "%s", "After the catch, the game takes the legs back:\npose them before the catch, or in the landing clip.");
        ImGui::BeginDisabled(legFrozen || lockedHere);
        float r[3];
        QToRotatorDeg(doc_.EditedRaw(sel_, t_), r[0], r[1], r[2]);
        if (studio_) {   // precise values (Studio); ring space and tool: Stage toolbar
            const EditorDoc::State before = doc_.SaveState();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##rot", r, 0.5f, -360.f, 360.f, "%.1f°")) doc_.SetPending(sel_, QFromRotatorDeg(r[0], r[1], r[2]));
            if (ImGui::IsItemActivated()) doc_.CheckpointState(before, "rotation (keyboard)");
            ImGui::TextDisabled("pitch / yaw / roll (local rotation)");
        }
        int ir, iu, ifo, ih;
        const IkJoint handle = doc_.IkHandle(sel_, ir, iu, ifo, ih);
        const bool legSel = IsLegBone(name);
        if (ikMode_ && handle == IkJoint::End && !legSel) {
            ImGui::Checkbox("chest follows the hand", &bodyFollow_);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("When you pull a hand far from the body, the shoulder and chest turn to help it.");
        }
        if (handle == IkJoint::Mid || handle == IkJoint::Root) {
            ImGui::Checkbox(legSel ? "foot stays in place" : "hand stays in place", &keepInPlace_);
        }
        if (doc_.IkChain(sel_, iu, ifo, ih)) {
            const bool sliderOff = handle == IkJoint::Mid && !keepInPlace_;
            if (sliderOff) ImGui::BeginDisabled();
            if (!elbowActive_) elbowDeg_ = 0.f;
            const EditorDoc::State st = doc_.SaveState();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
            const bool changed = ImGui::SliderFloat(legSel ? "knee" : "elbow", &elbowDeg_, -180.f, 180.f, "%+.0f°");
            if (ImGui::IsItemActivated()) {
                doc_.CheckpointState(st, "elbow");
                doc_.IkBegin(ih, t_);
                std::vector<FTr> cs;
                doc_.ComponentPose(t_, cs, false, true);
                ikSliderTarget_[0] = cs[ih].px; ikSliderTarget_[1] = cs[ih].py; ikSliderTarget_[2] = cs[ih].pz;
            }
            if (changed) doc_.IkMoveHand(ih, ikSliderTarget_, elbowDeg_, keepHand_, t_);
            elbowActive_ = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivated()) { doc_.IkEnd(); elbowDeg_ = 0.f; }
            if (sliderOff) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(legSel ? "Turns the knee around the hip -> foot axis." : "Turns the elbow around the shoulder -> hand axis.");
            ImGui::Checkbox(legSel ? "foot keeps its orientation" : "hand keeps its orientation", &keepHand_);
        }
        if (doc_.ArmSide(sel_) >= 0) {   // 2026-11-25: shoulder check (the game mesh crushes an over-raised / rolled arm)
            const int side = doc_.ArmSide(sel_);
            const EditorDoc::ShoulderInfo sh = doc_.Shoulder(side, t_);
            if (sh.ok) {
                const ImVec4 col = sh.warn ? theme::Bad : theme::Good;
                ImGui::TextColored(col, "%sshoulder: arm raised %.0f°, twist %+.0f° (forearm %+.0f°), clavicle %+.0f°",
                                   sh.warn ? "! " : "", sh.elevation, sh.twistUpper, sh.twistFore, sh.clavRaise);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's body has no twist bones: an arm raised very high by the shoulder alone,\nor twisted on itself, crushes the shoulder's skin in game (the editor only shows bones).\nRed: risk of deformation.");
                if (ImGui::Button("Fix the shoulder")) {
                    const EditorDoc::State st = doc_.SaveState();
                    if (doc_.FixShoulder(side, t_, true, true)) { doc_.CheckpointState(st, "fix the shoulder"); CommitKeyNow(true); Status("shoulder fixed: the clavicle rises, the arm twists less, the hand stays in place"); }
                    else Status("cannot fix at this time");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The hand stays where it is. The clavicle takes part of the raise,\nthe elbow turns to where the arm twists least, and half of the forearm's\ntwist goes to the wrist. A key is set at this time.");
                ImGui::SameLine();
                ImGui::Checkbox("natural shoulder (auto)", &shoulderAuto_);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("On every key on an arm raised above 60°, the clavicle rises too,\nlike a real shoulder (the hand stays in place).");
            }
        }
        for (auto& tr : c.tracks) {   // track weight (Studio)
            if (!studio_ || tr.bone != name) continue;
            const EditorDoc::State st = doc_.SaveState();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
            float w = tr.weight;
            if (ImGui::SliderFloat("weight", &w, 0.f, 1.f, "%.2f")) tr.weight = w;
            if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "weight");
        }
        const MirrorMap& m = doc_.mirror();
        const int other = sel_ < (int)m.other.size() ? m.other[sel_] : sel_;
        if (other != sel_ && other >= 0 && other < n) {
            const std::string b = "Copy to " + Short(doc_.skeleton().names[other]);
            if (ImGui::Button(b.c_str())) { doc_.Checkpoint("mirror"); doc_.MirrorCopy(sel_, t_); Status("pose copied to the other side"); }
            ImGui::SameLine();
        }
        if (ImGui::Button("Game pose")) { doc_.Checkpoint("game pose"); doc_.SetPending(sel_, doc_.OriginForClip(sel_, t_)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Puts this bone back as in the game's animation, at this time.");
        ImGui::EndDisabled();
    } else {   // nothing selected: a guide with every body part
        ImGui::TextWrapped("Pick a body part: click it in the view, or here.");
        for (int k = 0; k < kLimbs; k++) {
            const int b = LimbBone(k);
            const bool off = b < 0 || (k == 4 ? doc_.PelvisBone() < 0 : !doc_.IsEditable(b));
            char lb[64];
            snprintf(lb, sizeof(lb), "%s   (%d)##guide%d", kLimbLong[k], k + 1, k);
            if (off) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
            if (ImGui::Button(lb, ImVec2(-1, 0))) SelectLimb(k);
            if (off) ImGui::PopStyleVar();
            if (off && ImGui::IsItemHovered()) ImGui::SetTooltip(k == 2 || k == 3 ? "Feet can only be edited in a trick clip." : "Not available for this clip.");
        }
        ImGui::TextDisabled("Legs are edited in trick clips.");
    }
    if (doc_.HasPending() && !autoKey_) {
        if (ImGui::Button("Set key (K)")) CommitKeyNow();
        ImGui::SameLine();
        if (ImGui::Button("Discard")) { doc_.Checkpoint("discard"); doc_.DiscardPending(); }
    }
    {   // 2026-11-26: copy / paste my pose inside this clip
        const bool part = doc_.IsEditable(sel_) || (sel_ >= 0 && sel_ == doc_.PelvisBone());
        ImGui::SeparatorText("Copy / paste");
        if (ImGui::Button(part ? "Copy my pose of this part" : "Copy my pose of the whole body")) {
            const std::vector<KeySel> keep = keySel_;   // the button copies the pose, even with keys selected
            keySel_.clear();
            CopyNow();
            keySel_ = keep;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies YOUR change at the playhead (%.2f s), not the game's pose%s.\nPaste it at another time of this clip (Ctrl+V).\nTo put a part back as in the game, use \"Copy the game's pose\" above the timeline.\nCtrl+C copies the selected keys instead, if any.",
                              t_, part ? " (this part and what it carries)" : " (pelvis offset included)");
        ImGui::SameLine();
        if (!doc_.CanPaste()) ImGui::BeginDisabled();
        if (ImGui::Button("Paste (Ctrl+V)##insp")) PasteNow();
        if (!doc_.CanPaste()) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !doc_.CanPaste() ? "Copy a pose or keys first. The clipboard empties when you open another clip."
                                    : doc_.ClipboardHasKeys() ? "Pastes the copied keys, the first one at the playhead (same spacing and interpolation)."
                                                              : "Keys the copied pose at the playhead. The other keys keep their pose.");
    }

    if (studio_ && ImGui::CollapsingHeader("Bone list")) {
        const SkeletonFile& s = doc_.skeleton();
        for (int i = 0; i < n; i++) {
            if (s.names[i].rfind("SKXX_", 0) == 0 || !doc_.IsEditable(i)) continue;
            std::string label = BoneLabelUi(s.names[i]) + (doc_.TrackOf(i) ? "  *" : "");
            if (ImGui::Selectable(label.c_str(), i == sel_)) sel_ = i;
        }
    }
}

void App::InspectorClip() {
    Clip& c = doc_.clip();
    char nm[128];
    strncpy_s(nm, c.name.c_str(), _TRUNCATE);
    {
        const EditorDoc::State st = doc_.SaveState();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", nm, sizeof(nm))) c.name = nm;
        if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "name");
    }
    ImGui::TextUnformatted("Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Replace", c.mode == Mode::Replace) && c.mode != Mode::Replace) { askMode_ = Mode::Replace; if (c.tracks.empty()) doc_.SetMode(Mode::Replace); else popMode_ = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animated bones take exactly the clip's pose.");
    ImGui::SameLine();
    if (ImGui::RadioButton("Add", c.mode == Mode::Additive) && c.mode != Mode::Additive) { askMode_ = Mode::Additive; if (c.tracks.empty()) doc_.SetMode(Mode::Additive); else popMode_ = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The clip adds to the game's movement (keeps its small variations).");
    bool b = c.enabled;
    if (ui::Switch("##ongame", &b, "On in game")) { doc_.Checkpoint("on"); doc_.clip().enabled = b; }
    {   // who it plays for: goofy players see a regular clip only when it is mirrored (audit F)
        bool both = c.feet == Feet::Any;
        if (ImGui::Checkbox("Regular and goofy (auto mirror)", &both)) {
            doc_.Checkpoint("feet");
            c.feet = both ? Feet::Any : (var_.goofy ? Feet::Goofy : Feet::Regular);
            c.mirror = both;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ticked: the clip also plays for the other foot, mirrored left / right.\nUnticked: only for %s. Think of it before sharing a clip.", var_.goofy ? "goofy" : "regular");
    }
    if (doc_.CanHold()) {
        bool h = doc_.IsHold();
        if (ImGui::Checkbox("Held pose", &h)) {
            if (doc_.HasPending()) Status("set or discard the pending change first (K)");
            else doc_.SetHold(h, t_);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A single pose, applied all the time over the game's animation\n(walking, pushing, riding...): nothing to loop. The time bar is only\nused to see the game animation. Unticked: keys over time, looping.");
    }
    if (doc_.IsTrickClip()) {   // 2026-11-25 landing sync: the trick clip follows the jump, the landing is its own clip
        ImGui::TextWrapped("%s", "Synced to the jump: in game, the hang time (yellow area) stretches with the jump height, and the end of the clip falls on the ground contact.");
        if (doc_.HasOldLandingPart()) ImGui::TextColored(kWarn, "%s", "This clip holds an old built-in landing: it no longer follows the ground. Redo it in “Landing of this trick”.");
        const bool can = doc_.GroundAt() > 0.f;
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Landing of this trick")) GoToLanding();
        if (!can) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", "Opens the landing clip of this trick (or makes one): it starts at the real\nground contact and takes over the pose of the end of the trick.");
        if (!can) {   // on its own line: it was cut at the panel edge
            ImGui::TextDisabled("%s", "(landing not recorded)");
            ImGui::SameLine();
            if (ImGui::SmallButton(FigureRedo(var_) ? "redo in game" : "record again##rec")) RequestRerecord(var_);
        }
    }
    if (doc_.IsLandingClip() && !var_.trick.empty()) {   // the landing clip of one trick
        ImGui::TextWrapped("%s", "Landing of the trick: starts at the ground contact, in real time.");
        if (ImGui::Button("Back to the trick")) { FigureVariant tv = var_; tv.state = ST_Trick; askVar_ = tv; Request(Ask::ChangeFigure); }
        ImGui::SameLine();
        if (ImGui::Button("Take the end of the trick")) {
            if (doc_.HasPending()) Status("set or discard the pending change first (K)");
            else if (const Clip* tc = TrickClipFor(var_)) {
                doc_.Checkpoint("take the end of the trick");
                const int n = doc_.SeedLandingFromTrick(*tc, false);
                Status(n ? "start of the landing = end of the trick" : "the trick clip changes nothing at its end: nothing to take");
            } else Status("no saved clip for this trick in this variant");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Replaces the keys: at the start, the pose at the end of the trick clip;\nat the end, the game's pose (the landing hands back to the game smoothly).");
    }
    if (doc_.IsTrickClip() || doc_.IsLandingClip()) {   // 2026-11-26 (task 3): locked start and end
        const bool avail = doc_.LockAvailable();
        bool lk = doc_.LockEnds();
        ImGui::BeginDisabled(!avail);
        if (ImGui::Checkbox("Lock start and end", &lk)) {
            if (doc_.HasPending()) Status("set or discard the pending change first (K)");
            else if (!lk) { doc_.SetLockEnds(false); Status("start and end unlocked: they can be posed"); }
            else if (doc_.LockPreview().empty()) { doc_.SetLockEnds(true); Status("start and end locked to the game's pose"); }
            else popLock_ = true;   // some parts are posed there: ask first
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!avail) ImGui::SetTooltip("%s", "Only in Add mode. A Replace clip is compared with the recording,\nnot with the game's live pose, so there is nothing to lock it to.");
            else ImGui::SetTooltip("%s",
                "Keeps the start and the end at the game's pose, so the clip joins the game smoothly.\n"
                "Trick clip: the start is locked; the end too, unless this trick has a landing clip\n"
                "for all its variants (then the landing takes over at the ground).\n"
                "Landing clip: the end is locked; the start is free (it continues the trick).\n"
                "With the end locked, the change eases back from your last key to the end,\n"
                "including the hang time that stretches on higher jumps.");
        }
        if (avail && lk) {
            const bool s = doc_.StartLocked(), e = doc_.EndLocked();
            const char* what = s && e ? "Locked: start and end (padlocks on the timeline)."
                             : s && doc_.HasLandingCover() ? "Locked: start. The end is free: the landing clip takes over."
                             : s ? "Locked: start. The end is posed: bring it back to lock it."
                             : e ? "Locked: end. The start is free: it continues the trick."
                             : "";
            if (*what) ImGui::TextDisabled("%s", what);
        }
    }
    EndWarnings(true);   // 2026-11-26: with their fix buttons
    if (doc_.CanDismount()) {   // 2026-11-25: the dismount part inside the riding clip
        bool dis = doc_.HasDismount();
        const bool can = doc_.HasDismountFigure();
        if (!can && !dis) ImGui::BeginDisabled();
        if (ImGui::Checkbox("Include the dismount", &dis)) {
            if (doc_.HasPending()) Status("set or discard the pending change first (K)");
            else {
                doc_.Checkpoint("dismount");
                if (doc_.SetDismountPart(dis, doc_.DismountFigureDuration())) Status(dis ? "dismount added: animate the step off the board after the loop" : "dismount removed");
                if (t_ > doc_.clip().duration) t_ = doc_.clip().duration;
            }
        }
        if (!can && !dis) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The riding clip goes on with a dismount part, played once when the feet\nleave the board, then blended into the on-foot, walking or running pose.\nBy default, the riding pose fades out smoothly during the dismount.");
        if (!can && !dis) { ImGui::SameLine(); ImGui::TextDisabled("(dismount not recorded: step off the board once in game)"); }
        if (dis) {
            const EditorDoc::State st = doc_.SaveState();
            float len = doc_.clip().dismount;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
            if (ImGui::DragFloat("dismount (s)", &len, 0.01f, 0.2f, std::max(0.2f, doc_.DismountFigureDuration()), "%.2f")) doc_.SetDismountPart(true, len);
            if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "dismount");
            if (t_ > doc_.clip().duration) t_ = doc_.clip().duration;
        }
    }
    if (!doc_.HasDismount()) {
        float lastKey = 0.f;
        const bool endLocked = doc_.EndLocked();   // 2026-11-26 (task 3): the locked end key moves with the end
        for (float k : doc_.KeyTimes(-1)) if (!(endLocked && std::fabs(k - c.duration) < 1e-3f)) lastKey = std::max(lastKey, k);
        const EditorDoc::State st = doc_.SaveState();
        float dur = c.duration;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
        if (ImGui::DragFloat("length (s)", &dur, 0.01f, std::max(0.1f, lastKey + (endLocked ? 1.f / 30.f : 0.f)), 30.f, "%.2f"))
            doc_.SetDuration(std::clamp(dur, std::max(0.1f, lastKey + (endLocked ? 1.f / 30.f : 0.f)), 30.f));
        if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "length");
        if (t_ > c.duration) t_ = c.duration;
        // 2026-11-26: the game keeps the landing state up to 2 s (DLL v0.23; 0.8 s before)
        if (doc_.IsLandingClip()) {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A landing clip plays for its whole length, up to 2 s.\nIt stops early as soon as you push, crouch, pop or start another move.");
            if (c.duration > 2.f + 1e-3f) ImGui::TextColored(kWarn, "Only the first 2 s of a landing clip play in game.");
        }
    }
    if (ImGui::Button("Copy to another variant...")) { askVar_ = var_; popCopy_ = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Makes a copy of the clip for another stance or the other foot\n(automatic left/right mirror when needed).");

    if (doc_.HasLockedTracks()) ImGui::TextColored(kWarn, "This clip has locked bones (kept as they are).");
    if (!conflicts_.empty()) {
        ImGui::SeparatorText("Several clips on this variant");
        for (auto& l : conflicts_) ImGui::TextWrapped("%s", l.c_str());
    }
    if (!studio_) ImGui::TextDisabled("Stances, priority, blends, loop, drive:\nStudio mode (... menu at the top right).");
}

void App::InspectorAdvanced() {
    Clip& c = doc_.clip();
    const bool trick = doc_.IsTrickClip();
    // which variants the clip plays on
    ImGui::TextUnformatted("Plays in:");
    for (int s = 0; s < STANCE_Count; s++) {
        if (!StateAllStances(VariantOfClip(c).state) && s > STANCE_Switch) break;
        bool on = (c.stances & (1u << s)) != 0;
        if (s) ImGui::SameLine();
        if (ImGui::Checkbox(StanceLabel(s), &on)) {
            const uint8_t nv = on ? (uint8_t)(c.stances | (1u << s)) : (uint8_t)(c.stances & ~(1u << s));
            if (nv) { doc_.Checkpoint("stances"); c.stances = nv; }
        }
    }
    int feet = (int)c.feet;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
    if (ImGui::Combo("feet", &feet, "both (mirrored)\0regular\0goofy\0")) {
        doc_.Checkpoint("feet");
        c.feet = (Feet)feet;
        c.mirror = c.feet == Feet::Any;
    }
    if (trick) {
        bool all = c.trick.empty();
        if (ImGui::Checkbox("all flip tricks", &all)) {
            doc_.Checkpoint("trick");
            c.trick = all ? std::string() : var_.trick;
            c.trickHash = TrickHash(c.trick);
        }
    }
    if ((c.states & ST_Grind) && !var_.trick.empty()) {   // 2026-11-25: one grind, or every grind
        bool all = c.trick.empty();
        if (ImGui::Checkbox("all grinds", &all)) {
            doc_.Checkpoint("grind");
            c.trick = all ? std::string() : var_.trick;
            c.trickHash = TrickHash(c.trick);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ticked: the clip plays on every grind.\nUnticked: only on this one.");
    }
    {
        const EditorDoc::State st = doc_.SaveState();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
        int prio = c.priority;
        if (ImGui::InputInt("priority", &prio)) c.priority = std::clamp(prio, -100, 100);
        if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "priority");
    }
    {
        const EditorDoc::State st = doc_.SaveState();
        float bl[2] = {c.blendIn, c.blendOut};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
        if (ImGui::DragFloat2("blends (s)", bl, 0.01f, 0.f, 5.f, "%.2f")) { c.blendIn = std::max(0.f, bl[0]); c.blendOut = std::max(0.f, bl[1]); }
        if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "blends");
    }
    bool b = c.loop;
    const bool noLoop = trick || doc_.IsHold();
    if (noLoop) { b = false; ImGui::BeginDisabled(); }
    if (ImGui::Checkbox("Loop", &b)) { doc_.Checkpoint("loop"); doc_.clip().loop = b; }
    if (noLoop) ImGui::EndDisabled();
    int drive = doc_.EffectiveDrive() == Drive::ManualRatio ? 1 : doc_.EffectiveDrive() == Drive::Cycle ? 2 : 0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
    if (trick) ImGui::BeginDisabled();
    if (ImGui::Combo("drive", &drive, "time\0ManualRatio\0game steps\0")) {
        if (doc_.HasPending()) Status("set or discard the pending change first");
        else if (drive == 2 && !doc_.HasCycle()) Status("game steps: no step found in the game animation (record the move while walking)");
        else {
            doc_.Checkpoint("drive");
            c.drive = drive == 1 ? Drive::ManualRatio : drive == 2 ? Drive::Cycle : Drive::Time;
            if (drive == 2) c.duration = doc_.CycleEnd() - doc_.CycleStart();
            playing_ = false;
        }
    }
    if (trick) ImGui::EndDisabled();
    if (doc_.EffectiveDrive() == Drive::ManualRatio) {
        const EditorDoc::State st = doc_.SaveState();
        float rr[2] = {c.r0, c.r1};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
        if (ImGui::DragFloat2("ManualRatio range", rr, 0.01f, -2.f, 3.f, "%.2f") && std::fabs(rr[0] - rr[1]) > 0.01f) { c.r0 = rr[0]; c.r1 = rr[1]; }
        if (ImGui::IsItemActivated()) doc_.CheckpointState(st, "range");
    }
}
