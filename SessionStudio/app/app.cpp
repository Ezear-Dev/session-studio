// Session Studio -- the application core: files, clips, confirmations, frame, 3D mannequin rig.
// Panels live in app_shell / app_library / app_stage / app_inspector / app_timeline / app_dialogs (.cpp).
#include "app_internal.h"

using namespace app_detail;


App::App(std::wstring exeDir) : exeDir_(std::move(exeDir)) {
    settings_ = exeDir_ + L"SessionStudio.ini";
    {   // UI Phase 1: saved panel sizes and backup folder
        Settings st;
        LoadSettings(settings_, st);
        leftU_ = st.GetFloat("layout.left", leftU_);
        rightU_ = st.GetFloat("layout.right", rightU_);
        bottomU_ = st.GetFloat("layout.bottom", bottomU_);
        bottomDefault_ = st.Get("layout.bottom").empty();   // never dragged: small windows get a smaller timeline
        libTab_ = std::clamp((int)st.GetFloat("library.tab", 0.f), 0, 2);   // UI Phase 2
        clipsAll_ = st.Get("library.all") == "1";
        studio_ = st.Get("ui.studio") == "1";   // UI Phase 3: Creator (default) / Studio
        compactRows_ = st.Get("timeline.compact") == "1";   // UI Phase 4
        loopPreview_ = st.Get("timeline.loop") != "0";      // 2026-11-25: on unless turned off
        allRows_ = st.Get("timeline.allbones") == "1";
        showWelcome_ = st.Get("ui.welcomed") != "1";   // UI Phase 5: first launch
        ws_ = st.Get("ui.workspace") == "browse" ? 0 : 1;   // redesign step 2: Animate unless Browse was left open
        bodyView_ = st.Get("ui.view") != "skeleton";   // 2026-11-26 (mannequin): Body by default
        cardOpen_ = st.Get("ui.card") != "0";         // redesign step 4: the inspector card, shown unless closed
        showFloor_ = st.Get("ui.floor") == "1";       // redesign step 3: the floor, OFF by default (tricks dip below it)
        showBoard_ = st.Get("ui.board") == "1";        // 2026-11-26 (mannequin stage 2): the board, OFF by default (approximate, see board_mesh.h)
        const std::string bd = st.Get("backupDir");
        backupRoot_ = bd.empty() ? DefaultBackupRoot() : Widen(bd);
    }
    clipsDir_ = FindClipsDir(settings_);
    gameBin_ = GameBinDirFromClips(clipsDir_);
    if (clipsDir_.empty()) dirDialog_ = true;
    std::string err;
    skeletonOk_ = LoadSkeletonFile(err);
    skeletonErr_ = err;
    var_.state = ST_Trick; var_.trick = "KickFlip";
    ScanFolder();
    doc_.NewClip(var_);
    SelectFigure(var_);
    DefaultMode();
    doc_.SeedKeys();
    savedText_ = ClipToJson(doc_.clip());
    BuildLandingPart();   // 2026-11-27: one timeline, two files
}

int App::SelfTest(std::string& report) {
    ScanFolder();
    report = "clips folder: " + (clipsDir_.empty() ? std::string("NOT FOUND") : Narrow(clipsDir_)) + "\n";
    report += "skeleton: " + (skeletonOk_ ? std::to_string(doc_.BoneCount()) + " bones" : "absent (" + skeletonErr_ + ")") + "\n";
    report += "recorded moves: " + std::to_string(figHave_.size()) + "\n";
    for (auto& k : figHave_) report += "  " + k + "\n";
    report += "tricks in the list: " + std::to_string(tricks_.size()) + "\n";
    report += "clips: " + std::to_string(clips_.size()) + ", captures: " + std::to_string(ListClips(true).size()) + "\n";
    // UI Phase 1
    PollGame(99.f);
    report += "game folder (Win64): " + (gameBin_.empty() ? std::string("unknown") : Narrow(gameBin_)) + "\n";
    report += std::string("installed mod: DLL ") + (modNative_ ? "yes" : "no") + ", Lua " + (modLua_ ? "yes" : "no") + "\n";
    report += std::string("game running: ") + (gameRunning_ ? "yes" : "no") + "\n";
    report += "game log: " + (logFound_ ? "v0." + std::to_string(logVersion_) + (logHooked_ ? ", hooked" : ", not hooked") : std::string("absent")) + "\n";
    report += "backups: " + (backupRoot_.empty() ? std::string("unknown") : Narrow(backupRoot_)) + "\n";
    return skeletonOk_ ? 0 : 2;
}

// ------------------------------------------------------------------ files
bool App::NeedDir() {
    if (!clipsDir_.empty()) return false;
    dirDialog_ = true;
    Status("choose the game's clips folder first");
    return true;
}

namespace {
const wchar_t* LegacyFigureFile(const FigureVariant& v) {   // v0.7 names, used until the game renames them
    if (v.stance != STANCE_Normal || v.goofy) return nullptr;
    if (v.state == ST_Manual) return L"Manual";
    if (v.state == ST_NoseManual) return L"NoseManual";
    if (v.state == ST_Trick && Lower(v.trick) == "kickflip") return L"Kickflip";
    return nullptr;
}
}

FigureVariant App::FigureSource(const FigureVariant& v) {
    if (v.state != ST_Landing || v.trick.empty()) return v;
    FigureVariant t = v;
    t.state = ST_Trick;
    return t;
}

const Clip* App::TrickClipFor(const FigureVariant& landing) const {
    const FigureVariant tv = FigureSource(landing);
    if (tv.state != ST_Trick) return nullptr;
    for (auto& ci : clips_) if (ci.err.empty() && ci.clip.enabled && ClipTargets(ci.clip, tv)) return &ci.clip;
    return nullptr;
}

void App::GoToLanding() {
    if (!doc_.IsTrickClip()) return;
    FigureVariant lv = var_;
    lv.state = ST_Landing;
    askVar_ = lv;
    Request(Ask::ChangeFigure);   // asks to save the trick clip first if it changed
}

std::wstring App::FigurePath(const FigureVariant& v0) const {
    const FigureVariant v = FigureSource(v0);
    const std::wstring dir = clipsDir_ + L"\\_figures\\";
    const std::wstring p = dir + Widen(FigureKey(v)) + L".json";
    if (!FileExists(p)) if (const wchar_t* old = LegacyFigureFile(v)) if (FileExists(dir + old + L".json")) return dir + old + L".json";
    return p;
}
bool App::FigureReady(const FigureVariant& v0) const {
    const FigureVariant v = FigureSource(v0);
    if (figHave_.count(Lower(FigureKey(v)))) return true;
    const wchar_t* old = LegacyFigureFile(v);
    return old && figHave_.count(Lower(Narrow(old)));
}
bool App::FigureRedo(const FigureVariant& v) const { return figRedo_.count(Lower(FigureKey(FigureSource(v)))) != 0; }

void App::ScanFolder() {
    figHave_.clear();
    figRedo_.clear();
    if (!clipsDir_.empty()) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((clipsDir_ + L"\\_figures\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::string f = Narrow(fd.cFileName), lf = Lower(f);
                if (lf.size() > 5 && lf.substr(lf.size() - 5) == ".json") figHave_.insert(lf.substr(0, lf.size() - 5));
                else if (lf.size() > 8 && lf.substr(lf.size() - 8) == ".refaire") figRedo_.insert(lf.substr(0, lf.size() - 8));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    RefreshClips();
    BuildTrickList();
    // the figure shown has been (re)recorded by the game: reload it (the keys stay)
    if (!clipsDir_.empty() && skeletonOk_) {
        const uint64_t st = FileStamp(FigurePath(var_));
        if (st != figStamp_) {
            const Clip keep = doc_.clip();
            SelectFigure(var_);
            doc_.clip().states = keep.states; doc_.clip().trick = keep.trick; doc_.clip().trickHash = keep.trickHash;
            if (doc_.HasFigure()) Status("game animation of \"" + VariantTitleUi(var_) + "\" loaded");
            if (fileName_.empty() && doc_.clip().tracks.empty() && doc_.SeedKeys() > 0) savedText_ = ClipToJson(doc_.clip());
        }
    }
}

void App::BuildTrickList() {
    std::map<std::string, TrickEntry> byLower;
    std::vector<std::string> order;
    auto add = [&](const std::string& base, int stance, const char* cat) {
        const std::string k = Lower(base);
        auto it = byLower.find(k);
        if (it == byLower.end()) { byLower[k] = TrickEntry{base, cat, 0}; order.push_back(k); it = byLower.find(k); }
        if (stance >= 0) it->second.stances |= (uint8_t)(1u << stance);
    };
    for (auto& t : TrickCatalog()) {
        std::string b; int s;
        if (SplitTrickName(t.full, b, s)) add(b, s, t.category);
    }
    for (auto& key : figHave_) {   // tricks done in the game that the catalogue does not know
        FigureVariant v;
        if (ParseFigureKey(key, v) && v.state == ST_Trick) add(v.trick, v.stance, "Seen in game");
    }
    for (auto& ci : clips_) if ((ci.clip.states & ST_Trick) && !ci.clip.trick.empty()) add(ci.clip.trick, -1, "Seen in game");
    tricks_.clear();
    for (auto& k : order) tricks_.push_back(byLower[k]);
}

bool App::LoadSkeletonFile(std::string& err) {
    if (clipsDir_.empty()) { err = "clips folder unknown"; return false; }
    std::string text;
    if (!ReadFileText(clipsDir_ + L"\\_skeleton.json", text)) { err = "_skeleton.json not found: start the game once with the mod installed"; return false; }
    mrigBuilt_ = false;   // 2026-11-26 (mannequin): calibrate the body again on the new skeleton
    return doc_.LoadSkeleton(text, err);
}

void App::SelectFigure(const FigureVariant& v) {
    std::string text, err;
    const std::wstring path = FigurePath(v);
    figStamp_ = clipsDir_.empty() ? 0 : FileStamp(path);
    // 2026-11-25 landing sync: the landing of one trick shows the trick's recording after the ground contact
    if (clipsDir_.empty() || !ReadFileText(path, text)) text.clear();
    if (v.state == ST_Landing && !v.trick.empty() && !text.empty()) {
        FigureRec tr, tail;
        if (!ParseFigure(text, tr, err)) text.clear();
        else if (!GroundTailFigure(tr, tail)) { text.clear(); err = "landing not recorded: record the trick again (“record again” button)"; }
        else { tail.figure = FigureKey(v); text = FigureToJson(tail); }
    }
    if (!clipsDir_.empty() && !text.empty() && doc_.LoadFigure(text, v, err)) figErr_.clear();
    else {
        doc_.ClearFigure(v);
        figErr_ = FigureReady(v) ? (err.empty() ? std::string("unreadable file") : err) : std::string();
        if (!figErr_.empty()) Status("recording " + VariantTitleUi(v) + ": " + figErr_);
    }
    // 2026-11-25: riding clips can go on with the game's dismount (same stance and feet)
    doc_.ClearDismountFigure();
    if (v.state == ST_Riding && !clipsDir_.empty()) {
        FigureVariant dv = v; dv.state = ST_Dismount; dv.trick.clear();
        std::string dt, de;
        if (ReadFileText(FigurePath(dv), dt)) doc_.LoadDismountFigure(dt, de);
    }
    var_ = v;
    originT_ = 0.f;
}

void App::RequestRerecord(const FigureVariant& v0) {
    if (NeedDir()) return;
    const FigureVariant v = FigureSource(v0);
    const std::wstring dir = clipsDir_ + L"\\_figures";
    CreateDirectoryW(clipsDir_.c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    if (WriteFileAtomic(dir + L"\\" + Widen(FigureKey(v)) + L".refaire", "refaire\n")) {
        figRedo_.insert(Lower(FigureKey(v)));
        Status("« " + VariantTitleUi(v) + "\" will be recorded again the next time you do it in game");
    } else Status("could not write the re-record request");
}

std::vector<std::string> App::ListClips(bool captures) const {
    std::vector<std::string> v;
    if (clipsDir_.empty()) return v;
    const std::wstring dir = captures ? clipsDir_ + L"\\captures" : clipsDir_;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.sanim").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        v.push_back((captures ? "captures\\" : "") + Narrow(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end());
    return v;
}

void App::RefreshClips() {
    clips_.clear();
    captures_ = ListClips(true);   // UI Phase 2: Captures tab (not listed every frame)
    for (auto& f : ListClips(false)) {
        std::string text, err;
        ClipInfo ci;
        ci.file = f;
        // UI Phase 2: a broken file stays in the list (with its error) instead of silently disappearing;
        // its empty Clip (states 0) targets nothing, so it plays no part in conflicts or counts
        if (!ReadFileText(clipsDir_ + L"\\" + Widen(f), text)) ci.err = "cannot read";
        else if (!ParseClip(text, f, ci.clip, err)) { ci.clip = Clip{}; ci.clip.states = 0; ci.err = err; }
        clips_.push_back(std::move(ci));
    }
    // clips that would play on the variant shown, in the player's order
    conflicts_.clear();
    struct E { std::string file; const Clip* c; bool me; };
    std::vector<E> es;
    const Clip& me = doc_.clip();
    if (me.enabled && ClipTargets(me, var_)) es.push_back({fileName_.empty() ? std::string("(this clip, not saved yet)") : fileName_, &me, true});
    for (auto& ci : clips_) if (ci.file != fileName_ && ci.clip.enabled && ClipTargets(ci.clip, var_)) es.push_back({ci.file, &ci.clip, false});
    if (es.size() < 2) return;
    std::stable_sort(es.begin(), es.end(), [](const E& a, const E& b) {
        if (a.c->priority != b.c->priority) return a.c->priority > b.c->priority;
        return ClipSpecificity(*a.c) > ClipSpecificity(*b.c);
    });
    for (size_t i = 0; i < es.size(); i++)
        conflicts_.push_back(es[i].file + (i == 0 ? "  <- plays" : "") + (es[i].me ? "  [this clip]" : ""));
}

bool App::ClipTargets(const Clip& c, const FigureVariant& v) const {
    if (!(c.states & v.state)) return false;
    if (StateNamed(v.state) && c.trickHash && c.trickHash != TrickHash(v.trick)) return false;
    if (v.state == ST_Landing && !v.trick.empty() && c.trickHash != TrickHash(v.trick)) return false;   // a trick landing: its own clips only
    if (!(c.stances & (1u << v.stance))) return false;
    return c.feet == Feet::Any || (c.feet == Feet::Goofy) == v.goofy;
}

void App::OpenFile(const std::string& name) {
    std::string text, err;
    if (!ReadFileText(clipsDir_ + L"\\" + Widen(name), text)) { Status("cannot read " + name); return; }
    if (!doc_.OpenClip(text, name, err)) { Status(name + ": " + err); return; }
    const Clip keep = doc_.clip();
    // show the variant the user was on if the clip covers it, else the clip's own variant
    const FigureVariant v = ClipTargets(keep, var_) ? var_ : doc_.Variant();
    SelectFigure(v);
    doc_.clip().states = keep.states; doc_.clip().trick = keep.trick; doc_.clip().trickHash = keep.trickHash;   // an opened clip keeps its trigger
    const bool capture = name.rfind("captures\\", 0) == 0;
    fileName_ = capture ? "" : name;
    savedText_ = capture ? "" : ClipToJson(doc_.clip());
    sel_ = -1;
    t_ = 0.f;
    playing_ = false;
    scanAge_ = 99.f;
    Status("opened: " + name + (doc_.HasLockedTracks() ? " (has locked bones: kept as they are)" : "") +
           (doc_.HasOldLandingPart() ? " (old built-in landing: redo it in “Landing of this trick”)" : ""));
    BuildLandingPart();   // 2026-11-27: a trick shows its landing on the same timeline
}

std::string App::AutoName() const {
    const std::string base = FigureKey(var_);
    for (int i = 1; i < 1000; i++) {
        const std::string n = base + (i == 1 ? std::string() : "_" + std::to_string(i)) + ".sanim";
        if (!FileExists(clipsDir_ + L"\\" + Widen(n))) return n;
    }
    return base + "_x.sanim";
}

bool App::SaveAs(const std::string& raw, bool overwriteOk) {
    if (NeedDir()) return false;
    std::string name = raw, nerr;
    if (!NormalizeClipName(name, nerr)) { Status(nerr); return false; }   // UI Phase 2: shared with Rename
    if (!overwriteOk && name != fileName_ && FileExists(clipsDir_ + L"\\" + Widen(name))) {
        overwriteName_ = name;
        popOverwrite_ = true;
        return false;
    }
    if (doc_.HasPending()) CommitKeyNow(true);
    std::string err;
    const std::string text = doc_.SaveText(err);
    if (text.empty()) { Status("not saved: " + err); return false; }
    const std::wstring path = clipsDir_ + L"\\" + Widen(name);
    // UI Phase 1: keep the previous version outside the game folder before replacing it
    LastSave ls;
    std::string old;
    ls.unchanged = ReadFileText(path, old) && old == text;
    if (!ls.unchanged && !BackupClip(path, backupRoot_, BackupStamp(), 20, &ls.backup, ls.backupErr) && ls.backupErr.empty()) ls.backupErr = "?";
    const std::wstring log = gameBin_.empty() ? std::wstring() : gameBin_ + L"\\SessionAnimLoader.native.log";
    ls.logFrom = log.empty() ? 0 : FileSizeOf(log);
    ls.gameWasRunning = IsGameRunning();
    if (!WriteFileAtomic(path, text)) { Status("cannot write to the clips folder (try again)"); return false; }
    ls.any = true;
    ls.file = name;
    { const std::time_t now = std::time(nullptr); std::tm tm{}; localtime_s(&tm, &now); char b[16]; std::strftime(b, sizeof(b), "%H:%M:%S", &tm); ls.clock = b; }
    lastSave_ = ls;
    fileName_ = name;
    doc_.clip().file = name;
    savedText_ = ClipToJson(doc_.clip());
    scanAge_ = 99.f;
    gameAge_ = 99.f;
    if (!ls.backupErr.empty()) Status("saved: " + name + " -- WARNING, the previous version could not be backed up (" + ls.backupErr + ")");
    else if (ClipHasNoEffect(doc_.clip())) Status("saved: " + name + " -- WARNING, this clip changes nothing in game (no rotation, no offset): move a body part first");
    else Status("saved: " + name + " -- do \"" + VariantTitleUi(var_) + "\" in game to see it");
    return true;
}

void App::SaveCurrent() {
    if (NeedDir()) return;
    // 2026-11-27 (one timeline, two files): the part under the playhead, then the other part if it needs it
    auto saveOpen = [&]() {
        if (hasComp_ && !PartNeedsSave(inLanding_, doc_, fileName_, savedText_) && inLanding_ && fileName_.empty()) return true;   // a locked new landing: nothing to write
        const std::string name = fileName_.empty() ? AutoName() : fileName_;
        if (doc_.clip().name.empty() || doc_.clip().name == "nouveau_clip") doc_.clip().name = name.substr(0, name.size() - 6);
        return SaveAs(name, true);
    };
    bool ok = saveOpen();
    if (ok && hasComp_ && PartNeedsSave(!inLanding_, comp_.doc, comp_.fileName, comp_.savedText)) {
        SwapParts();
        ok = saveOpen();
        SwapParts();
    }
    if (ok && afterSave_) { afterSave_ = false; Proceed(); }
}

// P6: a new clip adds to the game's animation (Ajouter); without the recorded animation it can only replace
void App::DefaultMode() {
    if (!doc_.clip().tracks.empty()) return;
    doc_.SetMode(doc_.HasFigure() ? Mode::Additive : Mode::Replace);
}

bool App::Dirty() const {
    if (!hasComp_) return ClipToJson(doc_.clip()) != savedText_;
    return PartNeedsSave(inLanding_, doc_, fileName_, savedText_) || PartNeedsSave(!inLanding_, comp_.doc, comp_.fileName, comp_.savedText);
}

void App::Status(const std::string& s) {
    status_ = s;
    statusAge_ = 0.f;
    // redesign step 2: kept for the game pill popup (the status strip is gone)
    char clock[16] = "";
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    if (localtime_s(&lt, &now) == 0) std::strftime(clock, sizeof(clock), "%H:%M:%S", &lt);
    msgs_.push_back(Msg{clock, s});
    if (msgs_.size() > 12) msgs_.erase(msgs_.begin());
}

bool App::CommitKeyNow(bool quiet) {
    if (!doc_.HasPending()) { if (!quiet) Status("nothing to key: rotate a bone first"); return false; }
    if (LandingLockedHere()) { Status("the landing is locked (the game's): unlock it in Clip settings to pose it"); return false; }
    if (doc_.LockedAt(doc_.KeyTime(t_))) {   // 2026-11-26 (task 3): the locked start / end stays the game's pose
        Status("start and end are locked to the game's pose: pose between them, or discard the change");
        return false;
    }
    if (doc_.clip().mode == Mode::Additive && !doc_.HasFigure()) {
        Status("additive needs the game animation: do \"" + VariantTitleUi(var_) + "\" once in game");
        return false;
    }
    // 2026-11-25 FIX (user: Ctrl+Z / Undo did nothing): the automatic key no longer adds its own undo step. That
    // step held the change not yet keyed: undoing it gave the change back, and the auto key set the same key again at
    // once. The gesture already made its step (before the change): one undo = the whole gesture, key included.
    if (!quiet) doc_.Checkpoint("key");
    // 2026-11-25: natural shoulder: an arm raised above 60 deg lifts its clavicle too (hand kept in place)
    if (shoulderAuto_) for (int side = 0; side < 2; side++) {
        bool armPending = false;
        for (int b = 0; b < doc_.BoneCount() && !armPending; b++) if (doc_.ArmSide(b) == side && doc_.Pending(b, nullptr)) armPending = true;
        if (armPending && doc_.Shoulder(side, t_).elevation > EditorDoc::kShoulderStart) doc_.FixShoulder(side, t_, true, false);
    }
    doc_.CommitKey(t_);
    return true;
}

// P6: auto key: once a gesture is over (rings, IK arrows, sliders), the change becomes a key at the current time
void App::AutoKey() {
    if (!autoKey_ || !doc_.HasPending()) { autoKeyFailed_ = false; return; }
    if (autoKeyFailed_ || ImGuizmo::IsUsing() || ImGui::IsAnyItemActive() || dragKey_) return;
    if (!CommitKeyNow(true)) autoKeyFailed_ = true;
}

// ------------------------------------------------------------------ confirmations
void App::Request(Ask a) {
    ask_ = a;
    if (doc_.HasPending()) { popPending_ = true; return; }
    if ((a == Ask::New || a == Ask::Open || a == Ask::Close || a == Ask::ChangeFigure) && Dirty()) { popUnsaved_ = true; return; }
    Proceed();
}

void App::Proceed() {
    const Ask a = ask_;
    ask_ = Ask::None;
    switch (a) {
        case Ask::ChangeTime:
            if (askGlobal_ >= 0.f) { SetShared(askGlobal_); askGlobal_ = -1.f; }   // 2026-11-27: may move to the other part
            else t_ = askTime_;
            playing_ = false;
            break;
        case Ask::Play:   // at the end: from the start (of the trick, on one timeline)
            if (hasComp_) { if (SharedT() >= Parts().Total() - 1e-4f) SetShared(0.f); }
            else if (t_ >= doc_.clip().duration - 1e-4f) t_ = 0.f;
            playing_ = true;
            break;
        case Ask::ChangeFigure: {
            LeaveLanding();   // 2026-11-27: from the trick part
            // another variant: open its first clip, or start a new clip for it
            var_ = askVar_;
            const ClipInfo* first = nullptr;
            for (auto& ci : clips_) if (ClipTargets(ci.clip, askVar_)) { first = &ci; break; }
            if (first) { const std::string f = first->file; OpenFile(f); break; }
            doc_.NewClip(askVar_);
            SelectFigure(askVar_);
            DefaultMode();
            doc_.SeedKeys();
            if (const Clip* tc = TrickClipFor(askVar_)) doc_.SeedLandingFromTrick(*tc);   // a new trick landing starts where the trick ends
            fileName_.clear();
            savedText_ = ClipToJson(doc_.clip());
            sel_ = -1; t_ = 0.f; playing_ = false;
            scanAge_ = 99.f;
            BuildLandingPart();
            break;
        }
        case Ask::Open: LeaveLanding(); OpenFile(askFile_); break;
        case Ask::New:
            LeaveLanding();
            doc_.NewClip(var_);
            SelectFigure(var_);
            DefaultMode();
            doc_.SeedKeys();
            fileName_.clear();
            savedText_ = ClipToJson(doc_.clip());
            sel_ = -1; t_ = 0.f; playing_ = false;
            scanAge_ = 99.f;
            BuildLandingPart();
            break;
        case Ask::CopyTo: {
            LeaveLanding();
            doc_.Checkpoint("copy to");
            doc_.RetargetVariant(askVar_);
            const Clip keep = doc_.clip();
            SelectFigure(askVar_);
            doc_.clip() = keep;
            fileName_.clear();
            savedText_.clear();
            doc_.clip().name = "nouveau_clip";
            scanAge_ = 99.f;
            Status("clip copied to \"" + VariantTitleUi(askVar_) + "\" (not saved yet: Ctrl+S)");
            BuildLandingPart();
            break;
        }
        case Ask::Close: quit_ = true; break;
        case Ask::Save: SaveCurrent(); break;
        case Ask::SaveAs: saveDialog_ = true; strncpy_s(nameBuf_, (fileName_.empty() ? AutoName() : fileName_).c_str(), _TRUNCATE); break;
        default: break;
    }
}

void App::AfterUndoRedo() {
    doc_.IkEnd();
    // the restored trigger may target another figure: show its original animation
    const Clip keep = doc_.clip();
    if (!ClipTargets(keep, var_)) { SelectFigure(VariantOfClip(keep)); doc_.clip() = keep; }
    if (sel_ >= doc_.BoneCount()) sel_ = -1;
}

// ------------------------------------------------------------------ frame
void App::Frame(float dt) {
    ImGuizmo::BeginFrame();
    statusAge_ += dt;
    scanAge_ += dt;
    if (scanAge_ > 2.f) { scanAge_ = 0.f; ScanFolder(); }

    const Clip& c = doc_.clip();
    if (doc_.HasPending()) playing_ = false;   // time never moves under a pending change
    if (playing_ && hasComp_) {   // 2026-11-27: the trick, then its landing; the loop goes back to the trick's start
        int part = inLanding_ ? 1 : 0;
        float local = t_;
        const bool run = AdvanceTime(Parts(), part, local, dt * playSpeed_, loopPreview_);
        if ((part == 1) != inLanding_) SwapParts();
        t_ = local;
        if (!run) playing_ = false;
    } else if (playing_ && doc_.EffectiveDrive() != Drive::ManualRatio) {
        t_ += dt * playSpeed_;
        // 2026-11-25: "Loop" replays a one-shot clip in the editor only (the clip file is unchanged)
        if (t_ > c.duration) { if ((doc_.LoopsInTime() || loopPreview_) && c.duration > 0.f) t_ = std::fmod(t_, c.duration); else { t_ = c.duration; playing_ = false; } }
    }
    if (originPlaying_ && doc_.FigureDuration() > 0.f) originT_ = std::fmod(originT_ + dt * playSpeed_, doc_.FigureDuration());
    if (doc_.EffectiveDrive() == Drive::ManualRatio) t_ = ClipTime(c, 0.f, doc_.SimulatedRatio());

    if (wantClose_) { wantClose_ = false; Request(Ask::Close); }
    PollGame(dt);
    {   // 2026-11-26 (task 3): a trick's end is free when the trick has a landing clip for all its variants
        std::string missing;
        doc_.SetHasLandingCover(doc_.IsTrickClip() && HasLandingForAllVariants(doc_.clip(), missing));
    }

    // redesign step 2: two workspaces (Browse: the Library; Animate: stage, inspector, timeline, Library drawer)
    HeaderBar();
    if (ws_ == 0) TricksPanel();
    else {
        ViewPanel();
        TimelinePanel();
        if (drawer_) TricksPanel();   // drawn after the stage: on top of it
    }
    Splitters();
    Toast();
    WelcomeWindow();   // UI Phase 5
    Dialogs();
    Shortcuts();
    AutoKey();
}

// ------------------------------------------------------------------ 3D mannequin (2026-11-26, PLAN-MANNEQUIN stage 1)
App::~App() = default;

void App::SetGraphics(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    stage_ = std::make_unique<StageRenderer>();
    stageErr_.clear();
    if (!stage_->Init(dev, ctx, stageErr_)) { stage_.reset(); if (stageErr_.empty()) stageErr_ = "unknown error"; }
}

void App::ReleaseGraphics() { stage_.reset(); }

// which way the torso and the face look: calibrated once on a standing figure (the face looks the way of the chest
// there), else a riding one, else the current pose
void App::BuildBodyRig(const std::vector<FTr>& current) {
    mrigBuilt_ = true;
    std::vector<FTr> cal;
    std::string skel, fig, err;
    if (!clipsDir_.empty() && ReadFileText(clipsDir_ + L"\\_skeleton.json", skel)) {
        for (const char* key : {"OnFoot_RGS_regular", "Riding_RGS_regular"}) {
            FigureVariant v;
            if (!ParseFigureKey(key, v) || !ReadFileText(clipsDir_ + L"\\_figures\\" + Widen(key) + L".json", fig)) continue;
            EditorDoc tmp;
            if (!tmp.LoadSkeleton(skel, err) || !tmp.LoadFigure(fig, v, err)) continue;
            tmp.OriginPose(0.f, cal);
            break;
        }
    }
    if (cal.size() != (size_t)doc_.BoneCount()) cal = current;
    mrig_ = BuildMannequinRig(doc_.skeleton(), cal);
    brig_ = BuildBoardRig(doc_.skeleton());
}
