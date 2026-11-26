// Session Studio -- the application: UI state and panels (Dear ImGui + ImGuizmo).
// P6: trick browser (every flip trick + ground states), one variant per stance and feet, board preview, limb volumes,
// auto key, one-click save.
#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include "editor_doc.h"
#include "view_math.h"
#include "imgui.h"
#include "time_view.h"
#include "mannequin.h"
#include "board_mesh.h"
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
class StageRenderer;

class App {
public:
    explicit App(std::wstring exeDir);
    ~App();
    void Frame(float dt);                 // one UI frame (between ImGui::NewFrame and ImGui::Render)
    void RequestClose() { wantClose_ = true; }
    bool ShouldQuit() const { return quit_; }
    // --selftest: loads the folder, the skeleton and the figures; writes a report; 0 if the skeleton is loaded
    int SelfTest(std::string& report);
    const std::wstring& SettingsPath() const { return settings_; }   // UI Phase 1: main.cpp keeps the window placement there
    // 2026-11-26 (mannequin): the graphics device for the solid body of the 3D view; released before the device
    void SetGraphics(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void ReleaseGraphics();

private:
    // ---------------------------------------------------------------- UI Phase 1 (2026-11-25): workspace shell
    struct Layout { float top, status, gap, left, right, bottom, w, h; };
    Layout Lay() const;                   // panel sizes (pixels) from the saved proportions, clamped to the window
    void Splitters();                     // drag handles between the panels (double-click: default size)
    void SaveLayout();
    void HeaderBar();                     // replaces TopBar: context, save state, file actions, undo/redo, auto key
    void StatusBar();                     // game / save / reload / skeleton / source animation / folders / message
    void PollGame(float dt);              // game process, mod files, log (install state, reload of the last save)
    void OpenInExplorer(const std::wstring& dir);
    // ---------------------------------------------------------------- UI Phase 2 (2026-11-25): Library
    void LibraryTricks(const std::function<void(const FigureVariant&)>& pick);
    void LibraryClips();
    void LibraryCaptures();
    void ClipMenuItems(const std::string& file, bool capture);   // Open / Enable / Rename / Duplicate / Delete
    void ToggleClipEnabled(const std::string& file, bool on);
    void DuplicateClip(const std::string& file);
    void RenameClip(const std::string& file, const std::string& newName);
    void DeleteClip(const std::string& file);
    std::vector<std::string> captures_;   // "captures\x.sanim", refreshed with the clips
    bool clipsAll_ = false;               // Clips tab: every clip, not only the variant's (saved)
    int libTab_ = 0;                      // 0 Tricks, 1 Clips, 2 Captures (saved)
    bool libTabRestore_ = true;           // select the saved tab during the first frames only
    int libTabFrames_ = 0;
    std::string libFile_, libErr_;        // target of Rename / Delete, error shown in the dialog
    char renameBuf_[128] = {};
    bool popRename_ = false, popDelete_ = false, popRenameClose_ = false;
    float leftU_ = 17.f, rightU_ = 20.f, bottomU_ = 15.f;   // in font-size units (DPI independent)
    bool bottomDefault_ = true;           // timeline height never chosen by the user: at most 30 % of the window
    std::wstring backupRoot_;             // <Documents>\SessionStudio\backups unless "backupDir" is set
    std::wstring gameBin_;                // ...\Win64 (from the clips folder), "" if unknown
    bool gameRunning_ = false;
    bool modNative_ = false, modLua_ = false;
    int logVersion_ = -1, logStudioOn_ = -1;
    bool logFound_ = false, logHooked_ = false, logRefused_ = false;
    std::string logInstall_;
    uint64_t logStamp_ = 0;
    float gameAge_ = 99.f;
    struct LastSave {
        bool any = false;
        std::string file, clock;          // file name, local time "17:52:10"
        bool unchanged = false;           // same bytes as the file already there: the game has nothing to reload
        bool gameWasRunning = false;
        std::wstring backup;              // copy of the previous version ("" if there was none)
        std::string backupErr;
        unsigned long long logFrom = 0;   // log size just before the write
        float age = 0.f;
        int reload = 0;                   // ReloadKind of the answer, 0 = none yet
        std::string reloadTime, reloadDetail;
    } lastSave_;
    // an action asked by the user, which may wait for a confirmation (pending change, unsaved clip)
    enum class Ask { None, ChangeTime, Play, ChangeFigure, Open, New, Close, Save, SaveAs, CopyTo };
    struct ClipInfo { std::string file; Clip clip; std::string err; };   // clips of the folder (parsed, refreshed every 2 s); err: unreadable file
    struct TrickEntry { std::string base; std::string category; uint8_t stances = 0; };   // browser line

    // ---------------------------------------------------------------- UI Phase 3 (2026-11-25): Stage toolbar + Inspector
    void StageToolbar();                  // replaces LimbBar: body part, tool, ring space, display, camera, shortcuts
    void InspectorSelection();            // Inspector tabs: what is selected and its tool options
    void InspectorClip();                 //   the clip (name, mode, parts, duration, who it plays for)
    void InspectorAdvanced();             //   Studio only: stances, feet, priority, blends, loop, drive
    void CenterOnSelection();             // F
    void CameraPreset(int k);             // 0 start view, 1 quarter turn, 2 from above
    // the selection and the tool that a drag in the Stage will use (badge in the Stage, Selection tab)
    void ToolInfo(std::string& what, std::string& tool, ImVec4& col) const;
    bool studio_ = false;                 // Studio: Advanced tab, precise values, bone list (saved "ui.studio")
    int inspForce_ = -1;                  // tab to bring to front this frame
    int inspSelPrev_ = -2;                // a new selection brings the Selection tab to front
    // ---------------------------------------------------------------- UI Phase 4 (2026-11-25): Timeline
    void TimelineTransport();             // jump / frame / play, time and frame readout, keys, interpolation
    void TimelineViewBar();               // zoom - / + / fit, row density, enlarge, selection line
    void JumpTo(float t);                 // sets the time (through the pending-change check)
    TimeView tview_;                      // visible part of the clip (model/time_view.h)
    std::string tviewKey_;                // file + variant: another clip shows whole
    float tviewLastT_ = -1.f;             // the view follows the time when it changes
    bool compactRows_ = false;            // saved "timeline.compact"
    bool loopPreview_ = true;             // saved "timeline.loop": replay a one-shot clip in the editor (file unchanged)
    float playSpeed_ = 1.f;               // playback speed in the editor (1, 0.5, 0.25)
    float bottomBeforeBig_ = 0.f;         // timeline height (font units) before "Agrandir"; 0 = not enlarged

    // ---------------------------------------------------------------- UI Phase 5 (2026-11-25): onboarding
    void WelcomeWindow();                 // first launch (and "Aide"): what the tool does, setup checker, the 5 steps
    void OpenGuide();                     // docs\Getting-Started.md next to the exe
    bool showWelcome_ = false;            // shown until "Commencer" once (saved "ui.welcomed")
    bool modUe4ss_ = false;

    // files
    void ScanFolder();
    bool LoadSkeletonFile(std::string& err);
    void SelectFigure(const FigureVariant& v);
    std::wstring FigurePath(const FigureVariant& v) const;   // _figures\<key>.json (or a v0.7 name if only that exists)
    bool FigureReady(const FigureVariant& v) const;
    bool FigureRedo(const FigureVariant& v) const;
    void RequestRerecord(const FigureVariant& v);
    // 2026-11-25 landing sync: the landing of one trick is edited over that trick's recording (its ground tail)
    static FigureVariant FigureSource(const FigureVariant& v);   // Landing + trick -> the trick; else v
    const Clip* TrickClipFor(const FigureVariant& landing) const; // first enabled saved clip of the trick of that landing
    void GoToLanding();                                           // from a trick clip: its landing clip (opened or new)
    void OpenFile(const std::string& name);
    bool SaveAs(const std::string& name, bool overwriteOk);
    void SaveCurrent();                   // Save: under its name, or under an automatic name for a new clip
    std::string AutoName() const;         // "<variant key>.sanim", "_2"... if taken
    std::vector<std::string> ListClips(bool captures) const;
    void RefreshClips();
    void BuildTrickList();
    bool ClipTargets(const Clip& c, const FigureVariant& v) const;
    bool Dirty() const;
    bool NeedDir();
    // confirmations (every popup opens in Dialogs(), at the base of the ID stack)
    void Request(Ask a);
    void Proceed();
    void AfterUndoRedo();
    // panels
    void TricksPanel();
    void ViewPanel();
    void PropsPanel();
    void TimelinePanel();
    void Dialogs();
    void Shortcuts();
    void Status(const std::string& s);
    bool CommitKeyNow(bool quiet = false);
    void AutoKey();

    std::wstring exeDir_, settings_, clipsDir_;
    EditorDoc doc_;
    bool skeletonOk_ = false;
    std::string skeletonErr_;
    FigureVariant var_;                    // the variant shown (its original animation)
    std::set<std::string> figHave_, figRedo_;   // lower-case keys of _figures\*.json / *.refaire
    std::string figErr_;
    uint64_t figStamp_ = 0;
    std::vector<ClipInfo> clips_;
    std::vector<TrickEntry> tricks_;
    std::string fileName_;                // .sanim opened or saved ("" = new)
    std::string savedText_;               // text at the last save / open (for "modified")
    std::vector<std::string> conflicts_;
    float scanAge_ = 99.f;

    int sel_ = -1;
    OrbitCamera cam_;
    bool ghost_ = true, worldMode_ = false, showBoard_ = false   /* 2026-11-26 (mannequin stage 2): solid board, ui.board, off by default */, showVolumes_ = true;
    // 2026-11-26 (mannequin): "View: Body / Skeleton" (ui.view); the body rig is calibrated once per skeleton
    bool bodyView_ = true;
    std::unique_ptr<StageRenderer> stage_;
    std::string stageErr_;
    bool stageErrShown_ = false;
    MannequinRig mrig_;
    bool mrigBuilt_ = false;
    Mesh body_;
    BoardRig brig_;   // 2026-11-26 (mannequin stage 2): the board bones, found with the body rig
    Mesh board_;
    void BuildBodyRig(const std::vector<FTr>& current);
    bool autoKey_ = true;                 // P6: a key is set as soon as a gesture ends
    bool autoKeyFailed_ = false;          // the last automatic key could not be set (no retry until a new change)
    bool dragCheckpointed_ = false;
    int camDrag_ = -1;
    float t_ = 0.f;
    bool playing_ = false;
    float originT_ = 0.f;
    bool originPlaying_ = false, originFocus_ = false;
    bool ikMode_ = false, keepHand_ = false /* user 2026-11-25: off by default */, keepInPlace_ = true;
    // P6 lot 2 (ideas from Skate Style): limb bar / handles, the body follows the hand, pelvis offset
    static constexpr int kLimbs = 7;   // hand L, hand R, foot L, foot R, pelvis, head, chest
    int LimbBone(int k) const;
    void SelectLimb(int k);
    void PelvisProps();
    bool bodyFollow_ = true;
    bool shoulderAuto_ = true;            // 2026-11-25: natural shoulder at every key on a raised arm
    float pelStart_[3] = {}, pelGrab_[3] = {};
    float elbowDeg_ = 0.f;
    bool elbowActive_ = false;
    float ikSliderTarget_[3] = {};
    float dragClickX_ = 0.f;
    bool allRows_ = false;
    std::vector<int> dragBones_;
    bool dragKey_ = false;
    float dragFromT_ = 0.f, dragKeyT_ = 0.f;
    char search_[64] = {};
    // lot 5: timeline rows grouped by limb, key selection
    struct KeySel { std::vector<int> bones; float t; };   // bones of the row (empty = "All"), key time
    std::vector<KeySel> keySel_;
    std::set<std::string> expanded_;      // group ids shown unfolded
    int prevSel_ = -2;                    // to unfold / scroll to the group of a bone picked in the 3D view
    bool scrollToSel_ = false;
    bool boxing_ = false;                 // Shift + drag on an empty part of the timeline: box selection
    float boxX0_ = 0.f, boxY0_ = 0.f;
    bool openKeyMenu_ = false;
    bool KeySelected(const std::vector<int>& bones, float t) const;
    void ToggleKey(const std::vector<int>& bones, float t);
    void DeleteSelectedKeys();
    void SetSelectedInterp(int interp);
    // 2026-11-26 copy / paste inside the open clip: selected keys if any, else my pose of the selected part
    void CopyNow();
    void PasteNow();
    // 2026-11-26 ends of trick and landing clips: a short yellow line per issue (click: the part and the instant);
    // withFix: a fix button under each (Inspector)
    bool HasLandingForAllVariants(const Clip& trick, std::string& missing) const;
    void EndWarnings(bool withFix);

    Ask ask_ = Ask::None;
    float askTime_ = 0.f;
    FigureVariant askVar_;
    std::string askFile_;
    Mode askMode_ = Mode::Additive;
    void DefaultMode();
    bool popPending_ = false, popUnsaved_ = false, popMode_ = false, popOverwrite_ = false, popCopy_ = false;
    bool popLock_ = false;   // 2026-11-26 (task 3): confirm turning the lock on over posed start / end
    bool openDialog_ = false, saveDialog_ = false, dirDialog_ = false, wantClose_ = false, quit_ = false;
    bool afterSave_ = false;
    std::string overwriteName_;
    char nameBuf_[128] = {};
    char dirBuf_[512] = {};
    std::string status_;
    float statusAge_ = 0.f;
};
