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

// UI Phase 3: the Stage toolbar (above the canvas: never over the rings). Groups wrap to a new line when the Stage is narrow.
void App::StageToolbar() {
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    bool first = true;
    auto Next = [&](float w, float gap) {   // same line when the item fits, else a new line
        if (first) { first = false; return; }
        if (w <= 0.f) { ImGui::SameLine(0.f, gap); return; }
        ImGui::SameLine(0.f, gap);
        if (ImGui::GetCursorScreenPos().x + w > right) ImGui::NewLine();
    };
    auto BtnW = [&](const char* label) { return ImGui::CalcTextSize(label, nullptr, true).x + st.FramePadding.x * 2.f; };
    auto ChkW = [&](const char* label) { return ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x; };
    // a button drawn "on" (accent) or dimmed; returns the click
    auto Tool = [&](const char* label, bool on, bool dim, float gap) {
        Next(BtnW(label), gap);
        return ui::Toggle(label, on, dim);
    };
    const float groupGap = fs * 1.2f;
    // a group (quiet title + its controls) moves to a new line as a whole when it does not fit on this one
    auto Group = [&](const char* title, std::initializer_list<const char*> buttons, float extra = 0.f) {
        float w = extra + (title ? ImGui::CalcTextSize(title).x + st.ItemSpacing.x : 0.f);
        for (const char* b : buttons) w += BtnW(b) + st.ItemSpacing.x * 0.5f;
        Next(w, first ? 0.f : groupGap);
        if (!title) return;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "%s", title);
    };

    // body part (keys 1-7)
    Group("Part", {kLimbLabel[0], kLimbLabel[1], kLimbLabel[2], kLimbLabel[3], kLimbLabel[4], kLimbLabel[5], kLimbLabel[6]});
    for (int k = 0; k < kLimbs; k++) {
        const int b = LimbBone(k);
        const bool off = b < 0 || (k == 4 ? doc_.PelvisBone() < 0 : !doc_.IsEditable(b));
        char id[48];
        snprintf(id, sizeof(id), "%s##limb%d", kLimbLabel[k], k);
        if (Tool(id, b >= 0 && sel_ == b, off, k ? st.ItemSpacing.x * 0.5f : st.ItemSpacing.x)) SelectLimb(k);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(off && (k == 2 || k == 3) ? "Key %d\nFeet can only be edited in a trick clip (otherwise they stay on the board)." : "Key %d", k + 1);
    }

    // tool: what a drag does
    const int n = doc_.BoneCount();
    const bool pelvisSel = sel_ >= 0 && sel_ == doc_.PelvisBone();
    int a, b2, c2, d2;
    const bool ikHere = sel_ >= 0 && sel_ < n && !pelvisSel && doc_.IkHandle(sel_, a, b2, c2, d2) != IkJoint::None;
    Group("Tool", {"Rotate", "Pull (IK)", "Move"});
    if (Tool("Rotate##tool", !pelvisSel && !(ikMode_ && ikHere), pelvisSel, st.ItemSpacing.x)) { ikMode_ = false; if (pelvisSel) Status("the pelvis moves: pick another part to rotate"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rings around the bone: drag a ring to rotate the bone (and its children).");
    if (Tool("Pull (IK)##tool", !pelvisSel && ikMode_ && ikHere, pelvisSel || (sel_ >= 0 && !ikHere), st.ItemSpacing.x * 0.5f)) {
        ikMode_ = true;
        if (sel_ >= 0 && !ikHere && !pelvisSel) Status("pull (IK): works on a hand, elbow, shoulder, foot or knee");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Key I. Arrows on the hand, elbow, shoulder, foot or knee:\ndrag them, the rest of the arm or leg follows.");
    if (Tool("Move##tool", pelvisSel, !pelvisSel, st.ItemSpacing.x * 0.5f)) { if (!pelvisSel) SelectLimb(4); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pelvis (key 5): raises, lowers or shifts the whole body; the feet stay in place.");

    // ring space (rotate tool only)
    const bool rotating = sel_ >= 0 && !pelvisSel && !(ikMode_ && ikHere);
    Group("Rings", {"Local", "World"});
    if (Tool("Local##space", !worldMode_, !rotating, st.ItemSpacing.x)) worldMode_ = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The rings follow the bone's orientation.");
    if (Tool("World##space", worldMode_, !rotating, st.ItemSpacing.x * 0.5f)) worldMode_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The rings stay aligned with the ground and the vertical.");

    // display: 2026-11-26 (mannequin) Body / Skeleton
    Group("Show", {"Body", "Skeleton"});
    if (Tool("Body##view", bodyView_, false, st.ItemSpacing.x) && !bodyView_) { bodyView_ = true; stageErrShown_ = false; SaveSetting(settings_, "ui.view", "body"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A solid body: what is in front, which way a limb is twisted (yellow stripe = the bone's front face).\nIt is not your character: no clothes, no real skin. The game stays the real test.");
    if (Tool("Skeleton##view", !bodyView_, false, st.ItemSpacing.x * 0.5f) && bodyView_) { bodyView_ = false; SaveSetting(settings_, "ui.view", "skeleton"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The bone lines, as before.");
    Next(ChkW("Ghost") + st.ItemSpacing.x + (bodyView_ ? 0.f : ChkW("Volumes")), st.ItemSpacing.x);
    ImGui::Checkbox("Ghost", &ghost_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blue: the game's original animation, to compare.");
    Next(ChkW("Board"), st.ItemSpacing.x);   // 2026-11-26 (mannequin stage 2)
    if (ImGui::Checkbox("Board", &showBoard_)) SaveSetting(settings_, "ui.board", showBoard_ ? "1" : "0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("An APPROXIMATE board (light arrow = nose), from the animation's board bones.\nIt is not the board you see in game (driven by physics): it does not flip in tricks,\nand the feet may not sit exactly on it. Use it as a rough guide only.");
    if (!bodyView_) {
        Next(ChkW("Volumes"), st.ItemSpacing.x);
        ImGui::Checkbox("Volumes", &showVolumes_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Limb volumes: the yellow line shows the bone's front face.\nA bone twisted on itself shows this way.");
    }

    // camera
    Group("View", {"Center (F)", "Reset", "1/4 turn", "Top"});
    if (Tool("Center (F)", false, sel_ < 0, st.ItemSpacing.x)) CenterOnSelection();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(sel_ < 0 ? "Pick a body part first." : "Centers the view on the chosen part.");
    if (Tool("Reset##cam", false, false, st.ItemSpacing.x * 0.5f)) CameraPreset(0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the starting view (the whole skater).");
    if (Tool("1/4 turn##cam", false, false, st.ItemSpacing.x * 0.5f)) CameraPreset(1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns the view a quarter turn around the skater.");
    if (Tool("Top##cam", false, false, st.ItemSpacing.x * 0.5f)) CameraPreset(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("View from above.");
    Tool("?##aide", false, false, groupGap);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("In the view:\n  click: pick a body part or a bone\n  right-click + drag: orbit the view\n  middle-click + drag: pan the view\n  wheel: zoom\n\nKeyboard:\n  1-7: body parts   I: rotate / pull (IK)   F: center\n  Space: play   left / right arrows: previous / next frame   Home / End: start / end of the clip\n  K: set key   Del: delete the selected keys   Esc: deselect the keys\n  Ctrl+S: save   Ctrl+Z / Ctrl+Y: undo / redo\n\nTimeline:\n  Ctrl + wheel (or wheel on the ruler): zoom   Shift + wheel or middle-click: scroll");
}

void App::CenterOnSelection() {
    if (sel_ < 0 || sel_ >= doc_.BoneCount()) return;
    std::vector<FTr> cs;
    doc_.ComponentPose(t_, cs, false);
    const FTr d = Disp(cs[sel_]);
    cam_.target[0] = d.px; cam_.target[1] = d.py; cam_.target[2] = d.pz;
}

void App::CameraPreset(int k) {
    const OrbitCamera def;
    if (k == 0) cam_ = def;
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
    BeginPanel("3D view", kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (skeletonOk_) StageToolbar();   // UI Phase 3; above the canvas: never over the rings
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    if (sz.x < 50) sz.x = 50;
    if (sz.y < 50) sz.y = 50;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), theme::U32(theme::Stage));

    if (!skeletonOk_) {
        ImGui::TextWrapped("Skeleton not found.\n%s\n\nStart the game once with the mod installed: it writes _skeleton.json into the clips folder.", skeletonErr_.c_str());
        if (ImGui::Button("Choose the clips folder")) dirDialog_ = true;
        ImGui::SameLine();
        if (ImGui::Button("Retry")) { std::string err; skeletonOk_ = LoadSkeletonFile(err); skeletonErr_ = err; if (skeletonOk_) { ScanFolder(); SelectFigure(var_); } }
        ImGui::End();
        return;
    }

    // No ImGui item under the rings (otherwise ImGuizmo can never activate): camera and picking read the mouse directly.
    ImGuiIO& io = ImGui::GetIO();
    const bool inRect = io.MousePos.x >= p0.x && io.MousePos.y >= p0.y && io.MousePos.x < p0.x + sz.x && io.MousePos.y < p0.y + sz.y;
    const bool hovered = inRect && ImGui::IsWindowHovered();
    const float aspect = sz.x / sz.y;
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
    for (int i = -6; i <= 6; i++) {   // ground
        const float a[3] = {i * 25.f, -150.f, 0}, b[3] = {i * 25.f, 150.f, 0}, c[3] = {-150.f, i * 25.f, 0}, d[3] = {150.f, i * 25.f, 0};
        ImVec2 pa, pb;
        const ImU32 gc = i == 0 ? theme::U32(theme::Line, 1.f) : theme::U32(theme::Line, 0.75f);   // the centre lines a bit brighter
        if (PU(a, pa) && PU(b, pb)) dl->AddLine(pa, pb, gc);
        if (PU(c, pa) && PU(d, pb)) dl->AddLine(pa, pb, gc);
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
                uint32_t c = rgba(190, 188, 184);                                   // body
                if (!doc_.IsEditable(b) && b != doc_.PelvisBone()) c = rgba(150, 150, 156);   // locked part: a darker grey
                const int sd = doc_.ArmSide(b);
                if (sd >= 0 && warnSide[sd] && s.names[b].find("Finger") == std::string::npos && s.names[b].find("Clavicle") == std::string::npos) c = rgba(232, 92, 72);   // shoulder at risk
                if (b == sel_) c = rgba(255, 160, 40);                              // selected part
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
                dl->AddImage((ImTextureID)(intptr_t)srv, p0, ImVec2(p0.x + sz.x, p0.y + sz.y));
                body = bodyBuilt;
            }
        }
    }
    if (bodyView_ && !body && !stageErrShown_) {   // the body cannot be drawn: the skeleton view, and why
        stageErrShown_ = true;
        Status("Body view unavailable (" + (stage_ ? std::string("the body could not be built from this skeleton") : stageErr_.empty() ? std::string("no graphics device") : stageErr_) + "): skeleton shown");
    }
    if (ghost_) for (int b = 0; b < n; b++) {
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
    const float lh = ImGui::GetTextLineHeightWithSpacing();
    // UI Phase 3: what is selected and what a drag does, as a badge (replaces the long hint line and the IK notice)
    {
        std::string what, tool;
        ImVec4 tc;
        ToolInfo(what, tool, tc);
        const std::string txt = what + "  ·  " + tool;
        const ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
        const float px = ImGui::GetFontSize() * 0.5f, py = 3.f;
        const ImVec2 bp(p0.x + 8, p0.y + 6);
        dl->AddRectFilled(bp, ImVec2(bp.x + ts.x + 2 * px, bp.y + ts.y + 2 * py), theme::U32(theme::Bg0, 0.85f), ts.y * 0.5f);
        dl->AddRect(bp, ImVec2(bp.x + ts.x + 2 * px, bp.y + ts.y + 2 * py), U32(ImVec4(tc.x, tc.y, tc.z, 0.8f)), ts.y * 0.5f);
        dl->AddText(ImVec2(bp.x + px, bp.y + py), U32(tc), txt.c_str());
    }
    if (doc_.HasPending() && !autoKey_) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Pending change: K to set the key at %.2f s", t_);
        dl->AddText(ImVec2(p0.x + 8, p0.y + 12 + lh), theme::U32(theme::Warn), buf);
    }
    if (!doc_.HasFigure()) {
        const std::string msg = figErr_.empty()
            ? "Game animation not recorded yet: do \"" + VariantTitleUi(var_) + "\" once in game, it records itself."
            : "Game animation unusable (" + figErr_ + "): \"record again\", then do the move again in game.";
        dl->AddText(ImVec2(p0.x + 8, p0.y + sz.y - lh - 4), theme::U32(theme::Bad), msg.c_str());
    }
    dl->PopClipRect();

    // manipulator (display space; back to UE component space before computing the local rotation)
    bool usingGizmo = false;
    int ikR, ikU, ikF, ikH;
    const IkJoint ikKind = (ikMode_ && sel_ >= 0 && sel_ < n && !poseBlocked) ? doc_.IkHandle(sel_, ikR, ikU, ikF, ikH) : IkJoint::None;
    if (poseBlocked) {   // no handle: the game takes the legs back from the catch, or a locked instant
        const char* msg = lockedHere ? "Start and end are locked to the game's pose: pose between them (Clip tab to unlock)."
                                     : "After the catch, the game takes the legs back: pose them before the catch, or in the landing clip.";
        dl->AddText(ImVec2(p0.x + 8, p0.y + sz.y - 2 * ImGui::GetTextLineHeightWithSpacing() - 4), theme::U32(theme::Warn), msg);
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
    ImGui::End();
}
