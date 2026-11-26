// Session Studio -- 3D stage: limbs, stage toolbar, camera, tool info, the view and its gizmos.
#include "app_internal.h"

using namespace app_detail;


int App::LimbBone(int k) const {
    if (k < 0 || k >= kLimbs) return -1;
    const SkeletonFile& s = doc_.skeleton();
    for (int b = 0; b < (int)s.names.size(); b++) if (s.names[b] == kLimbBoneName[k]) return b;
    return -1;
}

void App::SelectLimb(int k) {
    const int b = LimbBone(k);
    if (b < 0) return;
    if (k == 4) {
        if (doc_.PelvisBone() < 0) { Status("pelvis not found in the skeleton"); return; }
        sel_ = b;
        Status("pelvis: drag the arrows to raise, lower or shift the body; the feet stay in place");
        return;
    }
    if (!doc_.IsEditable(b)) {
        Status(k == 2 || k == 3 ? "feet can only be edited in a trick clip (otherwise the feet stay on the board)" : "locked bone");
        return;
    }
    sel_ = b;
    ikMode_ = k <= 3;   // hands and feet: drag them (IK); head and chest: rings
    Status(k <= 1 ? (bodyFollow_ ? "hand: drag it; far from the body, the chest and shoulder follow" : "hand: drag it")
         : k <= 3 ? "foot: drag it, the knee follows" : "rings: rotate the bone");
}

// redesign step 3 (2026-11-27): the stage tools float over the edges of the 3D view. They are child windows: the view
// does not get the mouse under them, so a ring or an arrow can never be grabbed through a tool (ImGuizmo only reacts
// when the stage window itself is hovered). The middle of the view stays free for the rings.
namespace {
std::map<std::string, ImVec2> g_overlaySize;   // size of each card at the last frame (to place it by its corner)

// a floating card; pivot (0..1) says which point of the card sits on the anchor
bool BeginOverlay(const char* id, const ImVec2& anchor, const ImVec2& pivot) {
    const ImVec2 sz = g_overlaySize[id];
    ImGui::SetCursorScreenPos(ImVec2(std::round(anchor.x - sz.x * pivot.x), std::round(anchor.y - sz.y * pivot.y)));
    const float fs = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Alpha(theme::Surface, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Line);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::round(fs * 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(fs * 0.3f), std::round(fs * 0.3f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::round(fs * 0.25f), std::round(fs * 0.25f)));
    return ImGui::BeginChild(id, ImVec2(0, 0),
                             ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
void EndOverlay(const char* id) {
    g_overlaySize[id] = ImGui::GetWindowSize();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}
// a thin vertical separator between groups of buttons
void VSep() {
    ImGui::SameLine();
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 2.f, p.y + h * 0.2f), ImVec2(p.x + 2.f, p.y + h * 0.8f), theme::U32(theme::Line), 1.f);
    ImGui::Dummy(ImVec2(5.f, h));
    ImGui::SameLine();
}
} // namespace

ImVec2 App::OverlaySize(const char* id) const { auto it = g_overlaySize.find(id); return it == g_overlaySize.end() ? ImVec2(0, 0) : it->second; }

void App::StageOverlays(const ImVec2& p0, const ImVec2& sz) {
    const float fs = ImGui::GetFontSize();
    const float m = std::round(fs * 0.6f);
    const int n = doc_.BoneCount();

    // ---- tools, top centre: what a drag does, and the ring space
    if (BeginOverlay("##tools", ImVec2(p0.x + sz.x * 0.5f, p0.y + m), ImVec2(0.5f, 0.f))) {
        const bool pelvisSel = sel_ >= 0 && sel_ == doc_.PelvisBone();
        int a, b2, c2, d2;
        const bool ikHere = sel_ >= 0 && sel_ < n && !pelvisSel && doc_.IkHandle(sel_, a, b2, c2, d2) != IkJoint::None;
        if (ui::Toggle("Rotate##tool", !pelvisSel && !(ikMode_ && ikHere), pelvisSel)) { ikMode_ = false; if (pelvisSel) Status("the pelvis moves: pick another part to rotate"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (I toggles)\nRings around the bone: drag a ring to rotate the bone (and its children).");
        ImGui::SameLine();
        if (ui::Toggle("Pull (IK)##tool", !pelvisSel && ikMode_ && ikHere, pelvisSel || (sel_ >= 0 && !ikHere))) {
            ikMode_ = true;
            if (sel_ >= 0 && !ikHere && !pelvisSel) Status("pull (IK): works on a hand, elbow, shoulder, foot or knee");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pull (I toggles)\nArrows on the hand, elbow, shoulder, foot or knee:\ndrag them, the rest of the arm or leg follows.");
        ImGui::SameLine();
        if (ui::Toggle("Move##tool", pelvisSel, !pelvisSel)) { if (!pelvisSel) SelectLimb(4); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the pelvis (key 5): raises, lowers or shifts the whole body; the feet stay in place.");
        VSep();
        const bool rotating = sel_ >= 0 && !pelvisSel && !(ikMode_ && ikHere);
        if (ui::Toggle("Local##space", !worldMode_, !rotating)) worldMode_ = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Local rings: they follow the bone's orientation.");
        ImGui::SameLine();
        if (ui::Toggle("World##space", worldMode_, !rotating)) worldMode_ = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("World rings: they stay aligned with the ground and the vertical.");
    }
    EndOverlay("##tools");

    // ---- view options, top right
    if (BeginOverlay("##view", ImVec2(p0.x + sz.x - m, p0.y + m), ImVec2(1.f, 0.f))) {
        const std::string vl = theme::HasIcons() ? theme::Glyph(theme::Icon::View) + "  View" : std::string("View");
        if (ui::Button((vl + "##viewmenu").c_str(), ui::Kind::Ghost)) ImGui::OpenPopup("##viewmenu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Body or skeleton, ghost of the game's animation, board, volumes.");
        if (ImGui::BeginPopup("##viewmenu")) {
            ImGui::TextDisabled("Show");
            if (ImGui::RadioButton("Body", bodyView_) && !bodyView_) { bodyView_ = true; stageErrShown_ = false; SaveSetting(settings_, "ui.view", "body"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A solid body: what is in front, which way a limb is twisted (yellow stripe = the bone's front face).\nIt is not your character: no clothes, no real skin. The game stays the real test.");
            ImGui::SameLine();
            if (ImGui::RadioButton("Skeleton", !bodyView_) && bodyView_) { bodyView_ = false; SaveSetting(settings_, "ui.view", "skeleton"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The bone lines, as before.");
            ImGui::Separator();
            if (bodyView_) {   // user 2026-11-27: the ghost is useful on the body, not in the skeleton view
                ui::Switch("##ghost", &ghost_, "Ghost");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blue: the game's original animation, to compare.");
            }
            if (ui::Switch("##board", &showBoard_, "Board")) SaveSetting(settings_, "ui.board", showBoard_ ? "1" : "0");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("An APPROXIMATE board (light arrow = nose), from the animation's board bones.\nIt is not the board you see in game (driven by physics): it does not flip in tricks,\nand the feet may not sit exactly on it. Use it as a rough guide only.");
            if (!bodyView_) {
                ui::Switch("##volumes", &showVolumes_, "Volumes");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Limb volumes: the yellow line shows the bone's front face.\nA bone twisted on itself shows this way.");
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ui::IconButton("stagehelp", theme::Icon::Help, "?", false, ui::Kind::Ghost);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("In the view:\n  click: pick a body part or a bone\n  right-click + drag: orbit the view\n  middle-click + drag: pan the view\n  wheel: zoom\n\nKeyboard:\n  1-7: body parts   I: rotate / pull (IK)   F: center\n  Space: play   left / right arrows: previous / next frame   Home / End: start / end of the clip\n  K: set key   Del: delete the selected keys   Esc: deselect the keys\n  Ctrl+S: save   Ctrl+Z / Ctrl+Y: undo / redo   Ctrl+N: new clip   Ctrl+O: open\n  Tab: Browse / Animate   B: Library drawer\n\nTimeline:\n  Ctrl + wheel (or wheel on the ruler): zoom   Shift + wheel or middle-click: scroll");
    }
    EndOverlay("##view");

    // ---- camera, bottom right
    if (BeginOverlay("##camera", ImVec2(p0.x + sz.x - m, p0.y + sz.y - m), ImVec2(1.f, 1.f))) {
        if (theme::HasIcons()) { ImGui::AlignTextToFramePadding(); ImGui::TextColored(theme::Text3, "%s", theme::Glyph(theme::Icon::Camera).c_str()); ImGui::SameLine(); }
        if (sel_ < 0) ImGui::BeginDisabled();
        if (ui::Button("Center##cam", ui::Kind::Ghost)) CenterOnSelection();
        if (sel_ < 0) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(sel_ < 0 ? "Center (F): pick a body part first." : "Center (F): the view turns around the chosen part.");
        ImGui::SameLine();
        if (ui::Button("Reset##cam", ui::Kind::Ghost)) CameraPreset(0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the starting view (the whole skater).");
        ImGui::SameLine();
        if (ui::Button("1/4 turn##cam", ui::Kind::Ghost)) CameraPreset(1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns the view a quarter turn around the skater.");
        ImGui::SameLine();
        if (ui::Button("Top##cam", ui::Kind::Ghost)) CameraPreset(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("View from above.");
    }
    EndOverlay("##camera");
}

void App::CenterOnSelection() {
    if (sel_ < 0 || sel_ >= doc_.BoneCount()) return;
    std::vector<FTr> cs;
    doc_.ComponentPose(t_, cs, false);
    const FTr d = Disp(cs[sel_]);
    cam_.target[0] = d.px; cam_.target[1] = d.py; cam_.target[2] = d.pz;
    cam_.dist = std::min(cam_.dist, 140.f);   // redesign step 3 fix: and comes closer (never further)
}

void App::CameraPreset(int k) {
    const OrbitCamera def;
    if (k == 0) {   // redesign step 3 fix (user: Reset did not go back to the right place): frame the skater as posed now
        cam_ = def;
        std::vector<FTr> cs;
        doc_.ComponentPose(t_, cs, false);
        const SkeletonFile& s = doc_.skeleton();
        float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
        int count = 0;
        for (int b = 0; b < (int)cs.size() && b < (int)s.names.size(); b++) {
            const std::string& nm = s.names[b];
            if (nm.rfind("SKXX_", 0) == 0 || nm.find("IK_") != std::string::npos || nm == "AMXX_Master" || nm == "AMXX_Root") continue;
            const FTr d = Disp(cs[b]);
            const float p[3] = {d.px, d.py, d.pz};
            for (int i = 0; i < 3; i++) { mn[i] = std::min(mn[i], p[i]); mx[i] = std::max(mx[i], p[i]); }
            count++;
        }
        if (count) {
            for (int i = 0; i < 3; i++) { mn[i] -= 12.f; mx[i] += 12.f; }   // the body around the joints
            FrameBox(cam_, mn, mx, stageAspect_);
        }
    }
    else if (k == 1) { cam_.yawDeg = std::fmod(cam_.yawDeg + 90.f, 360.f); if (cam_.pitchDeg > 70.f) cam_.pitchDeg = def.pitchDeg; }
    else { cam_.pitchDeg = 85.f; }
}

void App::ToolInfo(std::string& what, std::string& tool, ImVec4& col) const {
    const int n = doc_.BoneCount();
    if (sel_ < 0 || sel_ >= n) { what = "Nothing selected"; tool = "click a body part"; col = kMuted; return; }
    what.clear();
    for (int k = 0; k < kLimbs; k++) if (LimbBone(k) == sel_) what = kLimbLabel[k];
    if (what.empty()) what = BoneLabelUi(doc_.skeleton().names[sel_]);
    if (sel_ == doc_.PelvisBone()) { tool = "Move (the feet stay)"; col = theme::AccentHi; return; }
    int a, b, c, d;
    const IkJoint j = ikMode_ ? doc_.IkHandle(sel_, a, b, c, d) : IkJoint::None;
    if (j != IkJoint::None) {
        const bool leg = IsLegBone(doc_.skeleton().names[sel_]);
        tool = j == IkJoint::Mid ? (leg ? "Pull the knee (IK)" : "Pull the elbow (IK)") : j == IkJoint::Root ? "Pull the shoulder (IK)" : (leg ? "Pull the foot (IK)" : "Pull the hand (IK)");
        col = ImVec4(0.212f, 0.773f, 0.941f, 1.f);   // hands / feet: the limb cyan
        return;
    }
    tool = worldMode_ ? "Rotate (world rings)" : "Rotate (local rings)";
    col = theme::Warn;
}

void App::PelvisProps() {
    ImGui::SeparatorText("Pelvis");
    float v[3];
    doc_.PelvisOffset(t_, v);
    const EditorDoc::State before = doc_.SaveState();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat3("##pel", v, 0.2f, -60.f, 60.f, "%.1f cm")) doc_.SetPendingPelvis(v);
    if (ImGui::IsItemActivated()) doc_.CheckpointState(before, "pelvis (keyboard)");
    ImGui::TextDisabled("offset X / Y / Z (cm, Z = up)");
    if (ImGui::Button("Reset to zero")) { doc_.Checkpoint("pelvis to zero"); const float z[3] = {0, 0, 0}; doc_.SetPendingPelvis(z); }
    ImGui::TextWrapped("The upper body follows the pelvis. The feet stay in place: the legs bend or stretch.");
}

void App::ViewPanel() {
    const Layout L = Lay();
    Place(L.stage.x, L.stage.y, L.stage.w, L.stage.h);
    // redesign step 3 (2026-11-27): the view fills its panel (no title, no padding); the tools float over its edges
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    BeginPanel("3D view", kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    if (sz.x < 50) sz.x = 50;
    if (sz.y < 50) sz.y = 50;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fsz = ImGui::GetFontSize();
    const float round = ImGui::GetStyle().WindowRounding;
    {   // background: a vertical gradient (lighter at the top), rounded like the panel
        const ImU32 top = theme::U32(ImVec4(0.110f, 0.125f, 0.157f, 1.f)), bot = theme::U32(ImVec4(0.059f, 0.067f, 0.086f, 1.f));
        const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + 2.f * round), top, round, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(p0.x, p1.y - 2.f * round), p1, bot, round, ImDrawFlags_RoundCornersBottom);
        dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + round), ImVec2(p1.x, p1.y - round), top, top, bot, bot);
    }

    if (!skeletonOk_) {   // empty state: a card in the middle of the view
        const float cw = std::min(sz.x - fsz * 2.f, fsz * 30.f);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + (sz.x - cw) * 0.5f, p0.y + sz.y * 0.3f));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        if (theme::Strong()) ImGui::PushFont(theme::Strong(), 0.f);
        ImGui::TextUnformatted("The game has not written the skater's skeleton yet");
        if (theme::Strong()) ImGui::PopFont();
        ImGui::TextColored(theme::Text2, "Start the game once with the mod installed and put the skater on the board: it writes _skeleton.json into the clips folder.");
        if (!skeletonErr_.empty()) ImGui::TextColored(theme::Text3, "%s", skeletonErr_.c_str());
        ImGui::PopTextWrapPos();
        if (ui::Button("Retry", ui::Kind::Primary)) { std::string err; skeletonOk_ = LoadSkeletonFile(err); skeletonErr_ = err; if (skeletonOk_) { ScanFolder(); SelectFigure(var_); } }
        ImGui::SameLine();
        if (ui::Button("Choose the clips folder")) dirDialog_ = true;
        ImGui::EndGroup();
        ImGui::End();
        return;
    }
    // messages about the view, drawn as banners at the bottom left (never ImGui items: nothing may catch the mouse there)
    struct Banner { std::string text; ImVec4 col; };
    std::vector<Banner> banners;

    // No ImGui item under the rings (otherwise ImGuizmo can never activate): camera and picking read the mouse directly.
    ImGuiIO& io = ImGui::GetIO();
    const bool inRect = io.MousePos.x >= p0.x && io.MousePos.y >= p0.y && io.MousePos.x < p0.x + sz.x && io.MousePos.y < p0.y + sz.y;
    const bool hovered = inRect && ImGui::IsWindowHovered();
    const float aspect = sz.x / sz.y;
    stageAspect_ = aspect;
    if (!camFramed_) { camFramed_ = true; CameraPreset(0); }   // the first view frames the skater
    if (hovered && camDrag_ < 0) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) camDrag_ = ImGuiMouseButton_Right;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) camDrag_ = ImGuiMouseButton_Middle;
    }
    if (camDrag_ >= 0) {
        if (!ImGui::IsMouseDown(camDrag_)) camDrag_ = -1;
        else if (camDrag_ == ImGuiMouseButton_Right) {
            cam_.yawDeg -= io.MouseDelta.x * 0.4f;
            cam_.pitchDeg = std::clamp(cam_.pitchDeg + io.MouseDelta.y * 0.4f, -89.f, 89.f);
        } else {
            const float yr = cam_.yawDeg * 3.14159265f / 180.f;
            const float k = cam_.dist * 0.0025f;
            const float rx = -std::sin(yr), ry = std::cos(yr);
            cam_.target[0] -= rx * io.MouseDelta.x * k;
            cam_.target[1] -= ry * io.MouseDelta.x * k;
            cam_.target[2] += io.MouseDelta.y * k;
        }
    }
    if (hovered && io.MouseWheel != 0.f) cam_.dist = std::clamp(cam_.dist * std::pow(0.88f, io.MouseWheel), 30.f, 3000.f);

    const int n = doc_.BoneCount();
    const SkeletonFile& s = doc_.skeleton();
    std::vector<FTr> cs, gh;
    doc_.ComponentPose(t_, cs, false);
    if (originFocus_) doc_.OriginPose(originT_, gh); else doc_.ComponentPose(t_, gh, true);
    std::vector<FTr> dcs(n), dgh(n);
    for (int b = 0; b < n; b++) { dcs[b] = Disp(cs[b]); dgh[b] = Disp(gh[b]); }
    // gestures (rings, IK arrows) start from the pose WITHOUT the leg weight: what they set is a key value
    std::vector<FTr> csG = cs, dcsG = dcs;
    if (doc_.IsTrickClip()) { doc_.ComponentPose(t_, csG, false, true); for (int b = 0; b < n; b++) dcsG[b] = Disp(csG[b]); }
    // pose shown: the same whether a leg is selected or not, as in game. 2026-11-26 fix (user: a foot moved at 0.53 s
    // went back when another bone was picked): the selected leg used to be drawn without the leg weight. Legs are
    // posed only before the catch (LegFrozenAt), where both poses are the same.
    std::vector<FTr> csV = cs, dcsV = dcs;
    const bool legFrozen = sel_ >= 0 && sel_ < n && doc_.LegFrozenAt(sel_, t_);
    // 2026-11-26: start / end locked to the game's pose: nothing is posed there (task 3)
    const bool lockedHere = sel_ >= 0 && sel_ < n && doc_.LockedAt(doc_.KeyTime(t_));
    const bool poseBlocked = legFrozen || lockedHere;

    auto PU = [&](const float u[3], ImVec2& out) {   // UE component point -> screen
        const float p[3] = {u[0], -u[1], u[2]};
        float sx, sy;
        if (!Project(cam_, aspect, p, sz.x, sz.y, sx, sy)) return false;
        out = ImVec2(p0.x + sx, p0.y + sy);
        return true;
    };
    auto P = [&](const FTr& t, ImVec2& out) {   // display transform -> screen
        const float p[3] = {t.px, t.py, t.pz};
        float sx, sy;
        if (!Project(cam_, aspect, p, sz.x, sz.y, sx, sy)) return false;
        out = ImVec2(p0.x + sx, p0.y + sy);
        return true;
    };
    dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
    {   // redesign step 3 fix (user: the feet went through the floor): the floor under the lowest point of the game's
        // animation of this variant (tricks go up to ~40 cm below 0); 0 without a recording
        const std::string key = FigureKey(var_) + (doc_.HasFigure() ? "|fig" : "|none") + (mrig_.ok ? "|rig" : "");
        if (key != floorKey_) {
            floorKey_ = key;
            floorZ_ = 0.f;
            if (!mrigBuilt_) BuildBodyRig(csV);
            if (doc_.HasFigure() && mrig_.ok) {
                std::vector<std::vector<FTr>> poses(25);
                for (int i = 0; i < 25; i++) doc_.OriginPose(doc_.FigureDuration() * i / 24.f, poses[i]);
                floorZ_ = GroundHeight(mrig_, poses);
            }
        }
    }
    const float fz = floorZ_;
    for (int i = -10; i <= 10; i++) {   // ground: 5 m, the lines fade away from the skater
        const float a[3] = {i * 25.f, -250.f, fz}, b[3] = {i * 25.f, 250.f, fz}, c[3] = {-250.f, i * 25.f, fz}, d[3] = {250.f, i * 25.f, fz};
        ImVec2 pa, pb;
        const float k = 1.f - std::fabs((float)i) / 11.f;
        const ImU32 gc = theme::U32(ImVec4(0.30f, 0.33f, 0.40f, 1.f), i == 0 ? 0.0f : 0.55f * k * k);   // centre lines: drawn below, coloured
        if (PU(a, pa) && PU(b, pb)) dl->AddLine(pa, pb, gc);
        if (PU(c, pa) && PU(d, pb)) dl->AddLine(pa, pb, gc);
    }
    {   // orientation (user: "1/4 turn" looked like the skater turned): coloured centre lines and the word "front" on the
        // floor, so a turn of the camera reads as one. +X = where the skater faces (front), red; Y, green
        ImVec2 pa, pb;
        const float xa[3] = {-250.f, 0.f, fz}, xb[3] = {250.f, 0.f, fz}, ya[3] = {0.f, -250.f, fz}, yb[3] = {0.f, 250.f, fz};
        if (PU(ya, pa) && PU(yb, pb)) dl->AddLine(pa, pb, IM_COL32(95, 211, 138, 120), 1.5f);
        if (PU(xa, pa) && PU(xb, pb)) dl->AddLine(pa, pb, IM_COL32(229, 83, 75, 150), 1.5f);
        const float fr[3] = {120.f, 0.f, fz};
        if (PU(fr, pa)) dl->AddText(ImVec2(pa.x + 4.f, pa.y - fsz - 2.f), IM_COL32(229, 83, 75, 200), "front");
    }
    {   // a soft contact shadow on the ground under the skater (stacked discs, darker in the middle)
        float cx = 0.f, cy = 0.f;
        int nb = 0;
        for (const char* fb : {"AMXX_L_Foot", "AMXX_R_Foot", "AMXX_Pelvis"})
            for (int b = 0; b < n; b++) if (s.names[b] == fb) { cx += csV[b].px; cy += csV[b].py; nb++; }
        if (nb) {
            cx /= (float)nb; cy /= (float)nb;
            for (int ring = 0; ring < 4; ring++) {
                const float rad = 22.f + ring * 9.f;
                ImVec2 pts[32];
                bool ok = true;
                for (int i = 0; i < 32 && ok; i++) {
                    const float ang = i * 6.2831853f / 32.f;
                    const float g[3] = {cx + std::cos(ang) * rad, cy + std::sin(ang) * rad * 0.8f, fz};
                    ok = PU(g, pts[i]);
                }
                if (ok) dl->AddConvexPolyFilled(pts, 32, IM_COL32(0, 0, 0, 34));
            }
        }
    }
    auto boneIdx = [&](const char* name) { for (int b = 0; b < n; b++) if (s.names[b] == name) return b; return -1; };

    // board (P6, line drawing): replaced on 2026-11-26 by the solid board of the stage picture (board_mesh.h)

    auto skip = [&](int b) { return s.names[b].rfind("SKXX_", 0) == 0 || s.names[b].find("IK_UPNODE") != std::string::npos; };
    // 2026-11-25 (user): the reference bones on the floor (Master, Root) and their link to the pelvis looked like a stick
    // holding the skater by the hips; they are not body parts, so they are not drawn
    auto refBone = [&](int b) { return b >= 0 && (s.names[b] == "AMXX_Master" || s.names[b] == "AMXX_Root"); };
    // 2026-11-26 (mannequin): the solid body, drawn with depth into a picture, under the ghost, dots, handles and rings
    bool body = false;
    if ((bodyView_ || showBoard_) && stage_ && stage_->Ready()) {
        if (!mrigBuilt_) BuildBodyRig(csV);
        std::vector<const Mesh*> meshes;
        bool bodyBuilt = false;
        // 2026-11-26 (mannequin stage 2): the board from the recorded board bones (also in the skeleton view)
        if (showBoard_ && doc_.HasFigure() && brig_.ok) {
            float tmp[3];
            const bool recorded = doc_.OriginPos(brig_.deck, t_, tmp);   // old figures: rotations only (set on its wheels)
            if (BuildBoardMesh(brig_, csV, recorded, board_)) meshes.push_back(&board_);
        }
        if (bodyView_ && mrig_.ok) {
            const bool warnSide[2] = {doc_.Shoulder(0, t_).warn, doc_.Shoulder(1, t_).warn};
            auto rgba = [](int r, int g, int b) { return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u; };
            std::vector<uint32_t> cols(n);
            for (int b = 0; b < n; b++) {
                // 2026-11-26 second look: one clay colour (the timeline already shows what is animated)
                uint32_t c = rgba(200, 196, 189);                                   // body: warm clay (redesign step 3)
                if (!doc_.IsEditable(b) && b != doc_.PelvisBone()) c = rgba(128, 131, 140);   // locked part: a darker grey
                const int sd = doc_.ArmSide(b);
                if (sd >= 0 && warnSide[sd] && s.names[b].find("Finger") == std::string::npos && s.names[b].find("Clavicle") == std::string::npos) c = rgba(229, 83, 75);   // shoulder at risk (theme::Bad)
                if (b == sel_) c = rgba(255, 107, 26);                              // selected part (theme::Accent)
                cols[b] = c;
            }
            BuildMannequinMesh(mrig_, csV, cols, body_, sel_ >= 0 ? sel_ : -1);   // the stripe on the selected part only
            meshes.push_back(&body_);
            bodyBuilt = true;
        }
        if (!meshes.empty()) {
            float vp[16], eye[3];
            StageViewProj(cam_, aspect, vp);
            cam_.Eye(eye);   // display space: back to UE (Y mirrored)
            float dir[3] = {cam_.target[0] - eye[0], -(cam_.target[1] - eye[1]), cam_.target[2] - eye[2]};
            const float dl2 = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (dl2 > 1e-6f) for (float& x : dir) x /= dl2;
            if (ID3D11ShaderResourceView* srv = stage_->Render((int)std::lround(sz.x), (int)std::lround(sz.y), vp, dir, meshes)) {
                dl->AddImageRounded((ImTextureID)(intptr_t)srv, p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, round);
                body = bodyBuilt;
            }
        }
    }
    if (bodyView_ && !body && !stageErrShown_) {   // the body cannot be drawn: the skeleton view, and why
        stageErrShown_ = true;
        Status("Body view unavailable (" + (stage_ ? std::string("the body could not be built from this skeleton") : stageErr_.empty() ? std::string("no graphics device") : stageErr_) + "): skeleton shown");
    }
    if (ghost_ && bodyView_) for (int b = 0; b < n; b++) {
        const int p = s.parents[b];
        if (p < 0 || skip(b) || refBone(b) || refBone(p)) continue;
        ImVec2 a, c;
        if (P(dgh[p], a) && P(dgh[b], c)) dl->AddLine(a, c, kColGhost, 3.f);
    }
    // limb volumes (P6): a box per bone along its axes, the bone's +Y face in bright: a twist of the bone is visible
    if (showVolumes_ && !body) {
        std::vector<int> mainChild(n, -1);
        std::vector<float> len(n, 0.f);
        for (int b = 0; b < n; b++) {
            const int p = s.parents[b];
            if (p < 0 || skip(b) || refBone(b) || refBone(p)) continue;
            const float d = std::sqrt((csV[b].px - csV[p].px) * (csV[b].px - csV[p].px) + (csV[b].py - csV[p].py) * (csV[b].py - csV[p].py) + (csV[b].pz - csV[p].pz) * (csV[b].pz - csV[p].pz));
            if (d > len[p]) { len[p] = d; mainChild[p] = b; }
        }
        for (int b = 0; b < n; b++) {
            if (skip(b) || refBone(b)) continue;
            const float hw = LimbHalf(s.names[b]);
            if (hw <= 0.f) continue;
            const FTr& T = csV[b];
            float L2 = len[b];
            float dir[3] = {1.f, 0.f, 0.f};   // bone length axis, in bone space
            if (mainChild[b] >= 0 && L2 > 3.f) {
                const FTr& C = csV[mainChild[b]];
                float v[3] = {C.px - T.px, C.py - T.py, C.pz - T.pz};
                const Quat inv = QInv(TrQ(T));
                QRotVec(inv, v[0], v[1], v[2]);
                dir[0] = v[0] / L2; dir[1] = v[1] / L2; dir[2] = v[2] / L2;
            } else if (s.names[b].find("Head") != std::string::npos) L2 = 22.f;
            else continue;
            // two axes across the bone (bone space), the first one as close to local Y as possible
            float u[3] = {0, 1, 0};
            const float du = u[0] * dir[0] + u[1] * dir[1] + u[2] * dir[2];
            for (int k = 0; k < 3; k++) u[k] -= du * dir[k];
            float nu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            if (nu < 0.2f) { u[0] = 0; u[1] = 0; u[2] = 1; const float d2 = dir[2]; for (int k = 0; k < 3; k++) u[k] -= d2 * dir[k]; nu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); }
            for (float& x : u) x /= nu;
            const float w[3] = {dir[1] * u[2] - dir[2] * u[1], dir[2] * u[0] - dir[0] * u[2], dir[0] * u[1] - dir[1] * u[0]};
            const float ha = hw, hb = hw * (s.names[b].find("Spine") != std::string::npos || s.names[b].find("Pelvis") != std::string::npos ? 0.6f : 0.85f);
            ImVec2 sp[2][4];
            bool ok = true;
            const float cu[4] = {1, -1, -1, 1}, cw[4] = {1, 1, -1, -1};
            for (int e = 0; e < 2 && ok; e++) for (int k = 0; k < 4 && ok; k++) {
                const float t0 = e ? L2 : 0.f, taper = e ? 0.8f : 1.f;
                float lp[3];
                for (int d = 0; d < 3; d++) lp[d] = dir[d] * t0 + (u[d] * cu[k] * ha + w[d] * cw[k] * hb) * taper;
                float wp[3]; Xf(T, lp[0], lp[1], lp[2], wp);
                ok = PU(wp, sp[e][k]);
            }
            if (!ok) continue;
            const bool selB = b == sel_;
            const ImU32 col = selB ? IM_COL32(255, 107, 26, 160) : doc_.TrackOf(b) ? IM_COL32(120, 220, 140, 110) : IM_COL32(200, 200, 210, 70);
            for (int e = 0; e < 2; e++) for (int k = 0; k < 4; k++) dl->AddLine(sp[e][k], sp[e][(k + 1) % 4], col, 1.f);
            for (int k = 0; k < 4; k++) dl->AddLine(sp[0][k], sp[1][k], k == 0 ? IM_COL32(255, 230, 120, 200) : col, k == 0 ? 2.f : 1.f);
        }
    }
    const bool shWarn[2] = {doc_.Shoulder(0, t_).warn, doc_.Shoulder(1, t_).warn};   // 2026-11-25: arm likely to deform in game
    for (int b = 0; b < n && !body; b++) {   // bone lines: skeleton view only
        const int p = s.parents[b];
        if (p < 0 || skip(b) || refBone(b) || refBone(p)) continue;
        ImVec2 a, c;
        if (!P(dcsV[p], a) || !P(dcsV[b], c)) continue;
        ImU32 col = (b == sel_ || p == sel_) ? kColSel : doc_.IsEditable(b) ? (doc_.TrackOf(b) ? kColTrack : kColBone) : kColLocked;
        { const int sd = doc_.ArmSide(p); if (sd >= 0 && shWarn[sd] && doc_.ArmSide(b) == sd && s.names[b].find("Finger") == std::string::npos) col = IM_COL32(255, 80, 60, 255); }
        dl->AddLine(a, c, col, 2.f);
    }
    for (int b = 0; b < n; b++) {
        if (skip(b)) continue;
        ImVec2 a;
        if (!P(dcsV[b], a)) continue;
        if (b == sel_) dl->AddCircleFilled(a, body ? 4.f : 5.f, kColSel);
        else if (doc_.IsEditable(b)) {
            if (body) {   // small dots on the body, dark rim so they read on a light surface
                if (s.names[b].find("Finger") != std::string::npos) continue;
                dl->AddCircleFilled(a, 2.6f, IM_COL32(40, 42, 50, 200));
                dl->AddCircleFilled(a, 1.6f, doc_.TrackOf(b) ? kColTrack : kColBone);
            } else dl->AddCircle(a, 3.5f, doc_.TrackOf(b) ? kColTrack : kColBone);
        }
    }
    // P6 lot 2: limb handles (click = choose the limb and its tool)
    ImVec2 limbScr[kLimbs];
    bool limbOk[kLimbs] = {};
    for (int k = 0; k < kLimbs; k++) {
        const int b = LimbBone(k);
        if (b < 0 || (k == 4 ? doc_.PelvisBone() < 0 : !doc_.IsEditable(b))) continue;
        if (!P(dcsV[b], limbScr[k])) continue;
        limbOk[k] = true;
        const float r = k == 4 ? 8.f : 6.f;
        dl->AddCircle(limbScr[k], r, kLimbCol[k], 0, b == sel_ ? 3.f : 2.f);
        if (k == 4) dl->AddCircleFilled(limbScr[k], 3.f, kLimbCol[k]);
    }
    // helps (drawn, not ImGui items: nothing may catch the mouse above the rings)
    {   // UI Phase 3 / redesign step 3: what is selected and what a drag does, as a badge under the tools card
        std::string what, tool;
        ImVec4 tc;
        ToolInfo(what, tool, tc);
        const std::string txt = what + "  ·  " + tool;
        const ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
        const float px = fsz * 0.6f, py = fsz * 0.22f;
        const float ty = p0.y + std::round(fsz * 0.6f) + OverlaySize("##tools").y + fsz * 0.35f;
        const ImVec2 bp(std::round(p0.x + (sz.x - ts.x) * 0.5f - px), ty);
        dl->AddRectFilled(bp, ImVec2(bp.x + ts.x + 2 * px, bp.y + ts.y + 2 * py), theme::U32(theme::Bg0, 0.80f), ts.y);
        dl->AddRect(bp, ImVec2(bp.x + ts.x + 2 * px, bp.y + ts.y + 2 * py), theme::U32(tc, 0.55f), ts.y);
        dl->AddText(ImVec2(bp.x + px, bp.y + py), U32(tc), txt.c_str());
    }
    if (doc_.HasPending() && !autoKey_) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Change without key: K sets the key at %.2f s", t_);
        banners.push_back({buf, theme::Warn});
    }
    if (!doc_.HasFigure())
        banners.push_back({figErr_.empty() ? "Game animation not recorded yet: do \"" + VariantTitleUi(var_) + "\" once in game, it records itself."
                                           : "Game animation unusable (" + figErr_ + "): \"record again\", then do the move again in game.", theme::Bad});
    dl->PopClipRect();

    // manipulator (display space; back to UE component space before computing the local rotation)
    bool usingGizmo = false;
    int ikR, ikU, ikF, ikH;
    const IkJoint ikKind = (ikMode_ && sel_ >= 0 && sel_ < n && !poseBlocked) ? doc_.IkHandle(sel_, ikR, ikU, ikF, ikH) : IkJoint::None;
    if (poseBlocked) {   // no handle: the game takes the legs back from the catch, or a locked instant
        banners.push_back({lockedHere ? "Start and end are locked to the game's pose: pose between them (Clip tab to unlock)."
                                      : "After the catch, the game takes the legs back: pose them before the catch, or in the landing clip.", theme::Warn});
    } else if (ikKind != IkJoint::None) {
        const int joint = ikKind == IkJoint::Mid ? ikF : ikKind == IkJoint::Root ? ikU : ikH;
        const bool leg = IsLegBone(s.names[sel_]);
        const char* label = ikKind == IkJoint::Mid ? (leg ? "IK knee" : "IK elbow") : ikKind == IkJoint::Root ? "IK shoulder" : (leg ? "IK foot" : "IK hand");
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(dl);
        ImGuizmo::SetRect(p0.x, p0.y, sz.x, sz.y);
        float view[16], proj[16], m[16];
        cam_.View(view);
        cam_.Proj(aspect, proj);
        BoneMatrix(dcsG[joint], m);
        if (ImGuizmo::Manipulate(view, proj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, m)) {
            if (!dragCheckpointed_) { doc_.Checkpoint(label); dragCheckpointed_ = true; doc_.IkBegin(sel_, t_); }
            const float target[3] = {m[12], -m[13], m[14]};
            const bool body = bodyFollow_ && ikKind == IkJoint::End && !leg;   // P6 lot 2: the spine follows the hand
            if (!(body ? doc_.IkMoveHandBody(ikH, target, keepHand_, t_) : doc_.IkMoveJoint(sel_, target, keepInPlace_, keepHand_, t_))) Status("IK not possible here");
        }
        usingGizmo = ImGuizmo::IsUsing();
        if (!usingGizmo && dragCheckpointed_) doc_.IkEnd();
        ImVec2 a;
        if (P(dcsG[joint], a)) dl->AddCircle(a, 9.f, kColSel, 0, 2.f);
    } else if (sel_ >= 0 && sel_ == doc_.PelvisBone()) {   // P6 lot 2: move the pelvis, the feet stay
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(dl);
        ImGuizmo::SetRect(p0.x, p0.y, sz.x, sz.y);
        float view[16], proj[16], m[16];
        cam_.View(view);
        cam_.Proj(aspect, proj);
        BoneMatrix(dcs[sel_], m);
        if (ImGuizmo::Manipulate(view, proj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, m)) {
            if (!dragCheckpointed_) {
                doc_.Checkpoint("pelvis");
                dragCheckpointed_ = true;
                doc_.PelvisOffset(t_, pelStart_);
                pelGrab_[0] = cs[sel_].px; pelGrab_[1] = cs[sel_].py; pelGrab_[2] = cs[sel_].pz;
            }
            const float now[3] = {m[12], -m[13], m[14]};
            float v[3];
            for (int k = 0; k < 3; k++) v[k] = pelStart_[k] + now[k] - pelGrab_[k];
            doc_.SetPendingPelvis(v);
        }
        usingGizmo = ImGuizmo::IsUsing();
    } else if (sel_ >= 0 && sel_ < n && doc_.IsEditable(sel_)) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(dl);
        ImGuizmo::SetRect(p0.x, p0.y, sz.x, sz.y);
        float view[16], proj[16], m[16];
        cam_.View(view);
        cam_.Proj(aspect, proj);
        // 2026-11-25 FIX (user: a thigh spun in every direction, the foot ran away): the rings turn the pose before the
        // pelvis offset. With an offset, its leg re-solve (feet kept) rewrites thigh and calf: measured after it, every
        // frame of the gesture fought it. Rotations only differ on the legs; the rings stay where the bone is shown.
        std::vector<FTr> csR;
        doc_.ComponentPose(t_, csR, false, doc_.IsTrickClip(), false);
        FTr ring = Disp(csR[sel_]);
        ring.px = dcsG[sel_].px; ring.py = dcsG[sel_].py; ring.pz = dcsG[sel_].pz;
        BoneMatrix(ring, m);
        if (ImGuizmo::Manipulate(view, proj, ImGuizmo::ROTATE, worldMode_ ? ImGuizmo::WORLD : ImGuizmo::LOCAL, m)) {
            if (!dragCheckpointed_) { doc_.Checkpoint("rotation"); dragCheckpointed_ = true; }
            Quat qd;
            if (MatrixRotation(m, qd)) {
                const Quat q = UndispQ(qd);
                const int p = s.parents[sel_];
                const Quat parentQ = p >= 0 ? TrQ(csR[p]) : QIdent;
                doc_.SetPending(sel_, QMul(QInv(parentQ), q));
            }
        }
        usingGizmo = ImGuizmo::IsUsing();
    }
    if (!usingGizmo) dragCheckpointed_ = false;

    if (hovered && camDrag_ < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !usingGizmo) {
        int limbHit = -1;
        float best = 11.f;
        for (int k = 0; k < kLimbs; k++) {
            if (!limbOk[k]) continue;
            const float d = std::hypot(io.MousePos.x - limbScr[k].x, io.MousePos.y - limbScr[k].y);
            if (d < best) { best = d; limbHit = k; }
        }
        std::vector<bool> selectable(n);
        for (int b = 0; b < n; b++) selectable[b] = doc_.IsEditable(b) || b == doc_.PelvisBone();
        const int hit = limbHit >= 0 ? -1 : PickBone(cam_, aspect, dcsV, selectable, sz.x, sz.y, io.MousePos.x - p0.x, io.MousePos.y - p0.y);
        if (limbHit >= 0) SelectLimb(limbHit);
        else if (hit >= 0) sel_ = hit;
        else if (sel_ >= 0) {
            const int any = PickBone(cam_, aspect, dcsV, std::vector<bool>(n, true), sz.x, sz.y, io.MousePos.x - p0.x, io.MousePos.y - p0.y);
            if (any >= 0 && IsLegBone(s.names[any]) && !doc_.IsTrickClip()) Status("legs can only be edited in a trick clip (the feet stay on the board)");
        }
    }
    // banners, bottom left, stacked upwards (left of the camera card)
    {
        const float m = std::round(fsz * 0.6f), px = fsz * 0.7f, py = fsz * 0.4f;
        const float maxW = std::max(fsz * 8.f, sz.x - OverlaySize("##camera").x - m * 3.f - px * 2.f - fsz * 0.2f);
        float y = p0.y + sz.y - m;
        dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
        for (const Banner& bn : banners) {
            const ImVec2 ts = ImGui::CalcTextSize(bn.text.c_str(), nullptr, false, maxW);
            const ImVec2 q0(p0.x + m, y - ts.y - py * 2.f), q1(q0.x + fsz * 0.2f + ts.x + px * 2.f, y);
            dl->AddRectFilled(q0, q1, theme::U32(theme::Bg0, 0.88f), fsz * 0.4f);
            dl->AddRectFilled(q0, ImVec2(q0.x + fsz * 0.2f, q1.y), theme::U32(bn.col), fsz * 0.4f, ImDrawFlags_RoundCornersLeft);
            dl->AddText(nullptr, 0.f, ImVec2(q0.x + fsz * 0.2f + px, q0.y + py), theme::U32(bn.col), bn.text.c_str(), nullptr, maxW);
            y = q0.y - fsz * 0.35f;
        }
        dl->PopClipRect();
    }
    StageOverlays(p0, sz);   // redesign step 3: the floating tools, last (above the view)
    ImGui::End();
}
