// Session Studio -- the UI (P4, reworked in P6).
// Left: trick browser (ground states + every flip trick), variant (stance x feet), clips of the variant.
// Centre: 3D view (skeleton with limb volumes, board, ghost of the original animation). Right: bone + clip settings.
// Bottom: timeline. Texts shown to the user are in French.
#include "app.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include "imgui.h"
#include "ImGuizmo.h"
#include "paths.h"
#include "ik.h"
#include "workspace.h"
#include "stage_renderer.h"
#include <ctime>
#include <shellapi.h>
#include <shobjidl.h>

namespace {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
bool ReadFileText(const std::wstring& path, std::string& out) {
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
    fclose(f);
    return true;
}
// .tmp then rename; the game rereads the clips every second: retry if the file is open at that moment
bool WriteFileAtomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfsopen(tmp.c_str(), L"wb", _SH_DENYNO);
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    for (int i = 0; i < 20; i++) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        Sleep(10);
    }
    DeleteFileW(tmp.c_str());
    return false;
}
bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
uint64_t FileStamp(const std::wstring& p) {   // modification date + size (0 if missing)
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    const uint64_t t = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    return t ^ (((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow) * 0x9E3779B97F4A7C15ull;
}
std::string Short(const std::string& n) { return n.rfind("AMXX_", 0) == 0 ? n.substr(5) : n; }
// UI Phase 4b: labels built outside the UI, in the chosen language
std::string VariantTitleUi(const FigureVariant& v) { return VariantTitle(v); }
std::string BoneLabelUi(const std::string& n) { return BoneLabel(n); }

// UE component space is left-handed (X forward, Y right, Z up). The 3D view is right-handed: Y is mirrored for display
// (position (x,-y,z), rotation (-qx, qy, -qz, qw)), otherwise the skater would appear mirrored.
FTr Disp(const FTr& t) { FTr d = t; d.py = -t.py; d.qx = -t.qx; d.qz = -t.qz; return d; }
Quat UndispQ(Quat q) { return Quat{-q.x, q.y, -q.z, q.w}; }

const ImU32 kColBone = IM_COL32(230, 230, 235, 255);
const ImU32 kColLocked = IM_COL32(95, 95, 105, 255);
const ImU32 kColSel = IM_COL32(255, 160, 40, 255);
const ImU32 kColGhost = IM_COL32(120, 170, 255, 90);
const ImU32 kColTrack = IM_COL32(120, 220, 140, 255);
const ImVec4 kOk(0.45f, 0.85f, 0.5f, 1.f), kWarn(1.f, 0.6f, 0.35f, 1.f), kDim(0.6f, 0.6f, 0.65f, 1.f);
constexpr float kFrame = 1.f / 30.f;

const char* StateTitle(uint32_t s) {
    switch (s) {
        case ST_Manual: return "Manual";
        case ST_NoseManual: return "Nose manual";
        case ST_Grind: return "Grind";
        case ST_Powerslide: return "Powerslide";
        case ST_Grab: return "Grab";
        case ST_OnFoot: return "On foot (standing)";
        case ST_Walking: return "Walking";
        case ST_Running: return "Running";
        case ST_Pushing: return "Pushing";
        case ST_Riding: return "Riding (standing)";
        case ST_Crouch: return "Crouch";
        case ST_Landing: return "Landing";
        default: return "Trick";
    }
}
// local direction (bone axes) -> UE component space
void Xf(const FTr& t, float x, float y, float z, float out[3]) {
    float v[3] = {x, y, z};
    QRotVec(TrQ(t), v[0], v[1], v[2]);
    out[0] = t.px + v[0]; out[1] = t.py + v[1]; out[2] = t.pz + v[2];
}
// half width of the limb volume drawn for a bone (cm); 0 = none
float LimbHalf(const std::string& n) {
    struct W { const char* part; float w; };
    static const W k[] = {{"Thigh", 7.f}, {"Calf", 5.5f}, {"Foot", 4.5f}, {"UpperArm", 5.f}, {"Forearm", 4.f}, {"_Hand", 3.f},
                          {"Spine1", 12.f}, {"Spine2", 12.f}, {"Spine3", 13.f}, {"Pelvis", 13.f}, {"Neck", 4.5f}, {"Head", 9.f}, {"Clavicle", 3.5f}};
    if (n.find("Finger") != std::string::npos || n.find("Xtra") != std::string::npos || n.find("IK_") != std::string::npos) return 0.f;
    for (auto& w : k) if (n.find(w.part) != std::string::npos) return w.w;
    return 0.f;
}

} // namespace

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
        bodyView_ = st.Get("ui.view") != "skeleton";   // 2026-11-26 (mannequin): Body by default
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
    const std::string name = fileName_.empty() ? AutoName() : fileName_;
    if (doc_.clip().name.empty() || doc_.clip().name == "nouveau_clip") doc_.clip().name = name.substr(0, name.size() - 6);
    if (SaveAs(name, true) && afterSave_) { afterSave_ = false; Proceed(); }
}

// P6: a new clip adds to the game's animation (Ajouter); without the recorded animation it can only replace
void App::DefaultMode() {
    if (!doc_.clip().tracks.empty()) return;
    doc_.SetMode(doc_.HasFigure() ? Mode::Additive : Mode::Replace);
}

bool App::Dirty() const { return ClipToJson(doc_.clip()) != savedText_; }

void App::Status(const std::string& s) { status_ = s; statusAge_ = 0.f; }

bool App::CommitKeyNow(bool quiet) {
    if (!doc_.HasPending()) { if (!quiet) Status("nothing to key: rotate a bone first"); return false; }
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
        case Ask::ChangeTime: t_ = askTime_; playing_ = false; break;
        case Ask::Play: if (t_ >= doc_.clip().duration - 1e-4f) t_ = 0.f; playing_ = true; break;   // at the end: from the start
        case Ask::ChangeFigure: {
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
            break;
        }
        case Ask::Open: OpenFile(askFile_); break;
        case Ask::New:
            doc_.NewClip(var_);
            SelectFigure(var_);
            DefaultMode();
            doc_.SeedKeys();
            fileName_.clear();
            savedText_ = ClipToJson(doc_.clip());
            sel_ = -1; t_ = 0.f; playing_ = false;
            scanAge_ = 99.f;
            break;
        case Ask::CopyTo: {
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
    if (playing_ && doc_.EffectiveDrive() != Drive::ManualRatio) {
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

    HeaderBar();
    TricksPanel();
    ViewPanel();
    PropsPanel();
    TimelinePanel();
    StatusBar();
    Splitters();
    WelcomeWindow();   // UI Phase 5
    Dialogs();
    Shortcuts();
    AutoKey();
}

// ------------------------------------------------------------------ layout
namespace {
const ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
void Place(float x, float y, float w, float h) { ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h)); }
constexpr float kDefLeft = 17.f, kDefRight = 20.f, kDefBottom = 15.f;   // font-size units
const ImVec4 kAccent(0.22f, 0.52f, 0.95f, 1.f), kGood(0.40f, 0.80f, 0.48f, 1.f), kAlert(1.f, 0.62f, 0.28f, 1.f), kBad(1.f, 0.42f, 0.38f, 1.f), kMuted(0.62f, 0.63f, 0.68f, 1.f);
ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
// a coloured pill with a label, laid out like a text item (hover works with IsItemHovered)
void Badge(const char* text, const ImVec4& col) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float px = ImGui::GetFontSize() * 0.5f, py = ImGui::GetStyle().FramePadding.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + px * 2.f, ts.y + py * 2.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), U32(ImVec4(col.x, col.y, col.z, 0.22f)), sz.y * 0.5f);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), U32(ImVec4(col.x, col.y, col.z, 0.85f)), sz.y * 0.5f);
    dl->AddText(ImVec2(p.x + px, p.y + py), U32(col), text);
    ImGui::Dummy(sz);
}
// a dot then a text (status strip item)
void Dot(const ImVec4& col) {
    const float r = ImGui::GetFontSize() * 0.22f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, U32(col));
    ImGui::Dummy(ImVec2(r * 2.f, h));
    ImGui::SameLine(0.f, r * 1.5f);
}
} // namespace

App::Layout App::Lay() const {
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    Layout L{};
    L.w = d.x; L.h = d.y;
    L.top = fs * 2.6f;
    L.status = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.f;
    L.gap = std::max(4.f, std::round(fs * 0.35f));
    // sides: at least a usable width each, and the Stage keeps at least 20 font sizes
    L.left = std::clamp(leftU_ * fs, fs * 12.f, std::max(fs * 12.f, L.w * 0.35f));
    L.right = std::clamp(rightU_ * fs, fs * 14.f, std::max(fs * 14.f, L.w * 0.40f));
    const float stageMin = fs * 20.f;
    const float over = L.left + L.right + 2.f * L.gap + stageMin - L.w;
    if (over > 0.f) { const float k = std::max(0.f, (L.left + L.right - over) / (L.left + L.right)); L.left *= k; L.right *= k; }
    // timeline: at least 7 font sizes, and the middle row keeps at least 12
    const float maxB = std::max(fs * 7.f, L.h - L.top - L.status - L.gap - fs * 12.f);
    const float wantB = bottomDefault_ ? std::min(bottomU_ * fs, L.h * 0.30f) : bottomU_ * fs;
    L.bottom = std::clamp(wantB, fs * 7.f, maxB);
    return L;
}

void App::SaveLayout() {
    Settings st;
    LoadSettings(settings_, st);
    st.SetFloat("layout.left", leftU_);
    st.SetFloat("layout.right", rightU_);
    st.SetFloat("layout.bottom", bottomU_);
    SaveSettings(settings_, st);
}

void App::Splitters() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    const float midH = L.h - L.top - L.status - L.bottom - L.gap;
    struct S { const char* id; float x, y, w, h; int kind; };   // kind 0 left, 1 right, 2 bottom
    const S ss[3] = {{"##split_gauche", L.left, L.top, L.gap, midH, 0},
                     {"##split_droite", L.w - L.right - L.gap, L.top, L.gap, midH, 1},
                     {"##split_bas", 0.f, L.top + midH, L.w, L.gap, 2}};
    for (const S& s : ss) {
        Place(s.x, s.y, s.w, s.h);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        ImGui::Begin(s.id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::InvisibleButton("##poignee", ImVec2(s.w, s.h));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (hov || act) ImGui::SetMouseCursor(s.kind == 2 ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (hov && !act) ImGui::SetTooltip("Drag to resize · double-click: default size");
        if (act) {
            const ImVec2 md = ImGui::GetIO().MouseDelta;
            if (s.kind == 0) leftU_ = std::max(1.f, L.left + md.x) / fs;
            else if (s.kind == 1) rightU_ = std::max(1.f, L.right - md.x) / fs;
            else { bottomU_ = std::max(1.f, L.bottom - md.y) / fs; bottomDefault_ = false; }
        }
        if (ImGui::IsItemDeactivated()) {
            // store what is really shown (the clamped size), not a value beyond the limits
            const Layout N = Lay();
            leftU_ = N.left / fs; rightU_ = N.right / fs; bottomU_ = N.bottom / fs;
            SaveLayout();
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (s.kind == 0) leftU_ = kDefLeft; else if (s.kind == 1) rightU_ = kDefRight; else bottomU_ = kDefBottom;
            SaveLayout();
        }
        const ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
        const ImU32 col = act ? U32(kAccent) : hov ? U32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f)) : IM_COL32(60, 63, 74, 255);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (s.kind == 2) dl->AddLine(ImVec2(p.x, (p.y + q.y) * 0.5f), ImVec2(q.x, (p.y + q.y) * 0.5f), col, act || hov ? 3.f : 1.f);
        else dl->AddLine(ImVec2((p.x + q.x) * 0.5f, p.y), ImVec2((p.x + q.x) * 0.5f, q.y), col, act || hov ? 3.f : 1.f);
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}

void App::HeaderBar() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    Place(0, 0, L.w, L.top);
    ImGui::Begin("##haut", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY((L.top - ImGui::GetFrameHeight()) * 0.5f);
    // what is being edited
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "Session Studio");
    ImGui::SameLine(0, fs);
    Badge(VariantTitleUi(var_).c_str(), ImVec4(0.55f, 0.78f, 1.f, 1.f));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The trick (or ground state), stance and foot you are editing.\nChange them in the left column.");
    ImGui::SameLine(0, fs * 0.8f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Clip: %s", fileName_.empty() ? "new clip" : fileName_.c_str());
    ImGui::SameLine(0, fs * 0.6f);
    // save state
    if (doc_.HasPending() && !autoKey_) Badge("Change without key", kAlert);
    else if (fileName_.empty()) Badge(Dirty() ? "Never saved · changed" : "Never saved", Dirty() ? kAlert : kMuted);
    else if (Dirty()) Badge("Not saved", kAlert);
    else Badge("Saved", kGood);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fileName_.empty()
        ? "This clip is not on disk yet. Save (Ctrl+S) creates it in the game's clips folder."
        : Dirty() ? "Some changes are not in the file yet. Save (Ctrl+S) to keep them."
                  : "The file on disk holds everything you see.");

    // file actions
    ImGui::SameLine(0, fs * 1.5f);
    if (ImGui::Button("New")) Request(Ask::New);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New clip for the variant shown.");
    ImGui::SameLine();
    if (ImGui::Button("Open...")) openDialog_ = true;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.60f, 1.f, 1.f));
    if (ImGui::Button("Save (Ctrl+S)")) Request(Ask::Save);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes the clip to the game's clips folder (automatic name for a new clip).\nThe previous version is copied to the backups first.\nThe bottom bar tells you whether the game reloaded it.");
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) Request(Ask::SaveAs);

    // undo / redo (never called "Annuler": that word is the Cancel of every dialog)
    ImGui::SameLine(0, fs * 1.5f);
    if (!doc_.CanUndo()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo")) { if (doc_.Undo()) AfterUndoRedo(); }
    if (!doc_.CanUndo()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(doc_.CanUndo() ? "Undo: %s (Ctrl+Z)" : "Nothing to undo (Ctrl+Z)", doc_.UndoLabel());
    ImGui::SameLine();
    if (!doc_.CanRedo()) ImGui::BeginDisabled();
    if (ImGui::Button("Redo")) { if (doc_.Redo()) AfterUndoRedo(); }
    if (!doc_.CanRedo()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(doc_.CanRedo() ? "Redo: %s (Ctrl+Y)" : "Nothing to redo (Ctrl+Y)", doc_.RedoLabel());

    // auto key: always visible, it changes what every gesture does
    ImGui::SameLine(0, fs * 1.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, autoKey_ ? ImVec4(0.18f, 0.42f, 0.25f, 1.f) : ImVec4(0.30f, 0.30f, 0.34f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, autoKey_ ? ImVec4(0.24f, 0.52f, 0.32f, 1.f) : ImVec4(0.38f, 0.38f, 0.43f, 1.f));
    if (ImGui::Button(autoKey_ ? "Auto key: ON" : "Auto key: OFF")) autoKey_ = !autoKey_;
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON: every gesture sets a key at the time shown.\nOFF: you set the key yourself with K (or the \"+ key\" button).");
    // UI Phase 5: the welcome window (setup check, the 5 steps, the guide) can come back at any time
    ImGui::SameLine(0, fs * 1.5f);
    if (ImGui::Button("Help")) showWelcome_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Checks the installation, recalls the steps and opens the guide.");
    ImGui::End();
}

// ------------------------------------------------------------------ UI Phase 5 (2026-11-25): onboarding
void App::OpenGuide() {
    // 2026-11-25: the web page (.html) opens in the browser; a .md can be tied to any program (on the user's PC: an AI
    // tool), so the .md only goes to Notepad, never to its file association
    auto Has = [](const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    const std::wstring stem = exeDir_ + L"docs\\Getting-Started";
    const std::wstring html = stem + L".html", md = stem + L".md";
    if (Has(html) && (INT_PTR)ShellExecuteW(nullptr, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) return;
    if (!Has(md)) { Status("guide not found next to the editor (docs folder)"); return; }
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + md + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void App::WelcomeWindow() {
    if (!showWelcome_) return;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(d.x * 0.5f, d.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(d.x - fs * 2.f, fs * 40.f), 0.f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Welcome to Session Studio###bienvenue", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        if (!open) showWelcome_ = false;
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 38.f);
    ImGui::TextWrapped("%s", "Session Studio changes how your skater moves in Session. You adjust a pose here, you save, and the game plays it right away, without restarting. The game itself is never modified: everything goes through clip files (.sanim) that you can share.");
    ImGui::Spacing();
    ImGui::SeparatorText("Installation");
    // the setup checker: one line per condition, with what to do when it is not met
    auto Row = [&](bool ok, const char* what, const char* fix) {
        Dot(ok ? kGood : kBad);
        ImGui::TextUnformatted(what);
        if (!ok && fix) { ImGui::Indent(fs * 1.2f); ImGui::TextColored(kAlert, "%s", fix); ImGui::Unindent(fs * 1.2f); }
    };
    Row(!clipsDir_.empty() && DirExists(clipsDir_), "Game clips folder found",
        "Choose the folder ...\\Win64\\Mods\\SessionAnimLoader\\clips (button below).");
    Row(modUe4ss_, "UE4SS (the mod loader) is installed",
        "Install UE4SS in the game's Win64 folder: without it, no mod loads.");
    Row(modNative_ && modLua_, "The Session Anim Loader mod is installed (DLL + Lua)",
        "Copy Mods\\SessionAnimLoader and Mods\\SessionAnimLoaderNative into the game's Mods folder, each with its enabled.txt.");
    Row(skeletonOk_, "The game wrote the skater's skeleton",
        "Start the game once with the mod and put the skater on the board: the _skeleton.json file is written by itself.");
    Row(logFound_ && logHooked_, "The mod hooked into the game (last session)",
        "Start the game and wait about 10 s with the skater: the mod hooks in by itself. Ctrl+F3 in game turns it on or off.");
    if (ImGui::Button("Choose the clips folder")) dirDialog_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Open the clips folder")) OpenInExplorer(clipsDir_);
    ImGui::Spacing();
    ImGui::SeparatorText("How it works");
    const char* steps[] = {
        "1. Do the move once in game: its original movement is recorded by itself (it turns green in the Library).",
        "2. In the Library, pick the move, the stance and the foot.",
        "3. Pick a body part (buttons above the view, keys 1 to 7) and pull or rotate it: a key is set by itself.",
        "4. Save (Ctrl+S): the clip is written into the game, which reloads it within a second. The bottom bar confirms it.",
        "5. Do the move again in game to see the result. Only your skater changes, on your screen.",
    };
    for (const char* st : steps) ImGui::TextWrapped("%s", st);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", "Not supported yet: grinds, grabs, walking and running (in progress). Powerslide not checked yet.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    const bool start = ImGui::Button("Start", ImVec2(fs * 8.f, 0));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Open the guide")) OpenGuide();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", "(\"Help\" button at the top to come back here)");
    ImGui::End();
    if (start || !open) {
        showWelcome_ = false;
        SaveSetting(settings_, "ui.welcomed", "1");
    }
}

void App::OpenInExplorer(const std::wstring& dir) {
    if (dir.empty() || !DirExists(dir)) { Status("folder not found"); return; }
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::PollGame(float dt) {
    gameAge_ += dt;
    if (lastSave_.any) lastSave_.age += dt;
    const bool waiting = lastSave_.any && !lastSave_.unchanged && lastSave_.reload == 0 && lastSave_.age < 10.f;
    if (gameAge_ < (waiting ? 0.5f : 3.f)) return;
    gameAge_ = 0.f;
    gameBin_ = GameBinDirFromClips(clipsDir_);
    gameRunning_ = IsGameRunning();
    const ModInstall mi = CheckModInstall(gameBin_);
    modNative_ = mi.native; modLua_ = mi.lua; modUe4ss_ = mi.ue4ss;
    if (gameBin_.empty()) { logFound_ = false; return; }
    const std::wstring log = gameBin_ + L"\\SessionAnimLoader.native.log";
    const uint64_t st = FileStamp(log);
    if (st != logStamp_) {   // the session state: parse the whole log only when it changed
        logStamp_ = st;
        std::string text;
        unsigned long long n = 0;
        if (st && ReadFrom(log, 0, text, &n)) {
            const GameLogInfo g = ParseGameLog(text);
            logFound_ = g.found; logVersion_ = g.version; logHooked_ = g.hooked; logRefused_ = g.refused; logStudioOn_ = g.studioOn;
            logInstall_ = g.lastInstallLine;
        } else logFound_ = false;
    }
    if (lastSave_.any && !lastSave_.unchanged && lastSave_.reload == 0) {   // did the game reload the file we wrote?
        std::string text;
        unsigned long long n = 0;
        if (ReadFrom(log, lastSave_.logFrom, text, &n)) {
            const ReloadEvent ev = FindReload(text, lastSave_.file);
            if (ev.kind != ReloadKind::None) { lastSave_.reload = (int)ev.kind; lastSave_.reloadTime = ev.time; lastSave_.reloadDetail = ev.detail; }
        }
    }
}

void App::StatusBar() {
    const Layout L = Lay();
    const float fs = ImGui::GetFontSize();
    Place(0, L.h - L.status, L.w, L.status);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.08f, 0.10f, 1.f));
    ImGui::Begin("##etat", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    const float sp = fs * 1.2f;

    // 1. the game and the in-game mod
    {
        ImVec4 col = kMuted;
        std::string t;
        if (gameBin_.empty()) { col = kAlert; t = "Game: folder unknown"; }
        else if (!modNative_ || !modLua_) { col = kBad; t = "Mod not installed in the game"; }
        else if (!gameRunning_) { col = kMuted; t = "Game: closed"; }
        else if (logFound_ && logHooked_) { col = logStudioOn_ == 0 ? kAlert : kGood; t = logStudioOn_ == 0 ? "Game: running · mod off (Ctrl+F3)" : "Game: running · mod on"; }
        else if (logFound_ && logRefused_) { col = kBad; t = "Game: running · mod refused"; }
        else { col = kAlert; t = "Game: running · mod not active yet"; }
        Dot(col);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col, "%s", t.c_str());
        if (ImGui::IsItemHovered()) {
            std::string tip = "Game folder: " + (gameBin_.empty() ? std::string("unknown (the clips folder is not ...\\Mods\\SessionAnimLoader\\clips)") : Narrow(gameBin_));
            tip += std::string("\nNative mod (SessionAnimLoaderNative): ") + (modNative_ ? "installed" : "missing or disabled");
            tip += std::string("\nLua mod (SessionAnimLoader): ") + (modLua_ ? "installed" : "missing or disabled");
            tip += std::string("\nGame running: ") + (gameRunning_ ? "yes" : "no");
            if (logFound_) {
                tip += "\nLast game session: SessionAnimLoaderNative v0." + std::to_string(logVersion_) + (logHooked_ ? ", hooked to the animation" : ", not hooked");
                if (!logInstall_.empty() && !logHooked_) tip += "\n  " + logInstall_;
            } else tip += "\nGame log (SessionAnimLoader.native.log): missing";
            tip += "\n\nThe mod hooks in about 10 s after the skater appears. Ctrl+F3 in game turns it on or off.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    // 2. the last save and what the game did with it
    ImGui::SameLine(0, sp);
    {
        ImVec4 col = kMuted;
        std::string t;
        const LastSave& s = lastSave_;
        if (!s.any) t = "Not saved since start";
        else {
            t = "Saved " + s.clock + " · ";
            if (!s.backupErr.empty()) { col = kBad; t += "previous version NOT backed up · "; }
            if (s.unchanged) t += "same as the file: nothing to reload";
            else if (s.reload == (int)ReloadKind::Loaded) { col = s.backupErr.empty() ? kGood : col; t += "reloaded by the game at " + s.reloadTime; }
            else if (s.reload == (int)ReloadKind::Refused) { col = kBad; t += "REFUSED by the game (see details)"; }
            else if (s.reload == (int)ReloadKind::KeptPrevious) { col = kBad; t += "error: the game keeps the previous version"; }
            else if (s.reload == (int)ReloadKind::ReadFailed) { col = kAlert; t += "the game cannot read the file (it retries)"; }
            else if (!gameRunning_) t += "game closed: loaded at next start";
            else if (s.age < 10.f) { col = kAlert; t += "waiting for the game..."; }
            else { col = kAlert; t += "the game reported no reload"; }
        }
        Dot(col);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col, "%s", t.c_str());
        if (ImGui::IsItemHovered() && s.any) {
            std::string tip = "File: " + Narrow(clipsDir_) + "\\" + s.file;
            tip += "\nPrevious version: " + (!s.backupErr.empty() ? "not backed up (" + s.backupErr + ")" : s.backup.empty() ? (s.unchanged ? std::string("unchanged") : std::string("none (new file)")) : Narrow(s.backup));
            if (!s.reloadDetail.empty()) tip += "\nThe game wrote: " + s.reloadDetail;
            if (s.reload == 0 && !s.unchanged && gameRunning_ && s.age >= 10.f) tip += "\nIs the mod on in game? Look at the dot on the left.";
            tip += "\n\n\"Reloaded\" means the game read the file without error.\nTo see the result, do the move in game.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    // 3. skeleton and source animation
    ImGui::SameLine(0, sp);
    Dot(skeletonOk_ ? kGood : kBad);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(skeletonOk_ ? kMuted : kBad, skeletonOk_ ? "Skeleton: %d bones" : "Skeleton missing", doc_.BoneCount());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(skeletonOk_ ? "The skater's skeleton, written by the game (_skeleton.json)." : "Start the game once with the mod installed: it writes _skeleton.json.");
    ImGui::SameLine(0, sp);
    const bool ready = doc_.HasFigure();
    Dot(ready ? kGood : kAlert);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ready ? kMuted : kAlert, ready ? "Game animation: ready" : "Game animation: to do in game");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(ready ? "The game's movement for this variant is recorded: you edit on top of it."
                                                        : "Do \"%s\" once in game: the mod records the movement by itself.", VariantTitleUi(var_).c_str());
    // 4. folders
    ImGui::SameLine(0, sp);
    if (ImGui::SmallButton("Clips folder")) OpenInExplorer(clipsDir_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens in Explorer:\n%s", clipsDir_.empty() ? "(not chosen yet)" : Narrow(clipsDir_).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Backups")) {
        const std::string stem = fileName_.size() > 6 ? fileName_.substr(0, fileName_.size() - 6) : std::string();
        const std::wstring d = backupRoot_ + (stem.empty() ? L"" : L"\\" + Widen(stem));
        OpenInExplorer(DirExists(d) ? d : backupRoot_);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous versions of your clips (the last 20 per clip),\ncopied before every save, outside the game folder:\n%s", Narrow(backupRoot_).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Change...")) dirDialog_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose another game clips folder.");
    // 5. the last message
    if (!status_.empty()) {
        ImGui::SameLine(0, sp);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(statusAge_ < 12.f ? ImVec4(1, 0.85f, 0.4f, 1) : kMuted, "%s", status_.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", status_.c_str());
    }
    ImGui::End();
}

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

// ------------------------------------------------------------------ P6 lot 2: limbs (ideas from Skate Style's limb wheel)
namespace {
const char* kLimbBoneName[7] = {"AMXX_L_Hand", "AMXX_R_Hand", "AMXX_L_Foot", "AMXX_R_Foot", "AMXX_Pelvis", "AMXX_Head", "AMXX_Spine2"};
const char* kLimbLabel[7] = {"L Hand", "R Hand", "L Foot", "R Foot", "Pelvis", "Head", "Chest"};
const char* kLimbLong[7] = {"Left hand", "Right hand", "Left foot", "Right foot", "Pelvis", "Head", "Chest"};
const ImU32 kLimbCol[7] = {IM_COL32(90, 200, 255, 200), IM_COL32(90, 200, 255, 200), IM_COL32(120, 230, 120, 200), IM_COL32(120, 230, 120, 200),
                                    IM_COL32(255, 150, 60, 220), IM_COL32(200, 140, 255, 200), IM_COL32(240, 220, 90, 200)};
} // namespace

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
        if (on) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.60f, 1.f, 1.f)); }
        if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
        const bool r = ImGui::Button(label);
        if (dim) ImGui::PopStyleVar();
        if (on) ImGui::PopStyleColor(2);
        return r;
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
    if (sel_ == doc_.PelvisBone()) { tool = "Move (the feet stay)"; col = ImVec4(1.f, 0.65f, 0.3f, 1.f); return; }
    int a, b, c, d;
    const IkJoint j = ikMode_ ? doc_.IkHandle(sel_, a, b, c, d) : IkJoint::None;
    if (j != IkJoint::None) {
        const bool leg = IsLegBone(doc_.skeleton().names[sel_]);
        tool = j == IkJoint::Mid ? (leg ? "Pull the knee (IK)" : "Pull the elbow (IK)") : j == IkJoint::Root ? "Pull the shoulder (IK)" : (leg ? "Pull the foot (IK)" : "Pull the hand (IK)");
        col = ImVec4(0.47f, 0.86f, 1.f, 1.f);
        return;
    }
    tool = worldMode_ ? "Rotate (world rings)" : "Rotate (local rings)";
    col = ImVec4(1.f, 0.78f, 0.4f, 1.f);
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
    Place(L.left + L.gap, L.top, L.w - L.left - L.right - 2.f * L.gap, L.h - L.top - L.status - L.bottom - L.gap);
    ImGui::Begin("3D view", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (skeletonOk_) StageToolbar();   // UI Phase 3; above the canvas: never over the rings
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    if (sz.x < 50) sz.x = 50;
    if (sz.y < 50) sz.y = 50;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(28, 30, 36, 255));

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
        if (PU(a, pa) && PU(b, pb)) dl->AddLine(pa, pb, IM_COL32(60, 62, 70, 255));
        if (PU(c, pa) && PU(d, pb)) dl->AddLine(pa, pb, IM_COL32(60, 62, 70, 255));
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
            const ImU32 col = selB ? IM_COL32(255, 170, 60, 150) : doc_.TrackOf(b) ? IM_COL32(120, 220, 140, 110) : IM_COL32(200, 200, 210, 70);
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
        dl->AddRectFilled(bp, ImVec2(bp.x + ts.x + 2 * px, bp.y + ts.y + 2 * py), IM_COL32(20, 21, 26, 210), ts.y * 0.5f);
        dl->AddRect(bp, ImVec2(bp.x + ts.x + 2 * px, bp.y + ts.y + 2 * py), U32(ImVec4(tc.x, tc.y, tc.z, 0.8f)), ts.y * 0.5f);
        dl->AddText(ImVec2(bp.x + px, bp.y + py), U32(tc), txt.c_str());
    }
    if (doc_.HasPending() && !autoKey_) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Pending change: K to set the key at %.2f s", t_);
        dl->AddText(ImVec2(p0.x + 8, p0.y + 12 + lh), IM_COL32(255, 165, 50, 255), buf);
    }
    if (!doc_.HasFigure()) {
        const std::string msg = figErr_.empty()
            ? "Game animation not recorded yet: do \"" + VariantTitleUi(var_) + "\" once in game, it records itself."
            : "Game animation unusable (" + figErr_ + "): \"record again\", then do the move again in game.";
        dl->AddText(ImVec2(p0.x + 8, p0.y + sz.y - lh - 4), IM_COL32(255, 128, 128, 255), msg.c_str());
    }
    dl->PopClipRect();

    // manipulator (display space; back to UE component space before computing the local rotation)
    bool usingGizmo = false;
    int ikR, ikU, ikF, ikH;
    const IkJoint ikKind = (ikMode_ && sel_ >= 0 && sel_ < n && !poseBlocked) ? doc_.IkHandle(sel_, ikR, ikU, ikF, ikH) : IkJoint::None;
    if (poseBlocked) {   // no handle: the game takes the legs back from the catch, or a locked instant
        const char* msg = lockedHere ? "Start and end are locked to the game's pose: pose between them (Clip tab to unlock)."
                                     : "After the catch, the game takes the legs back: pose them before the catch, or in the landing clip.";
        dl->AddText(ImVec2(p0.x + 8, p0.y + sz.y - 2 * ImGui::GetTextLineHeightWithSpacing() - 4), IM_COL32(255, 217, 77, 255), msg);
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

void App::PropsPanel() {
    const Layout L = Lay();
    Place(L.w - L.right, L.top, L.right, L.h - L.top - L.status - L.bottom - L.gap);
    ImGui::Begin("Inspector", nullptr, kPanel);
    // Creator / Studio: what is shown, never what the clip contains (UI Phase 3)
    {
        auto Seg = [&](const char* label, bool on) {
            if (on) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.60f, 1.f, 1.f)); }
            const bool r = ImGui::Button(label);
            if (on) ImGui::PopStyleColor(2);
            return r;
        };
        if (Seg("Creator", !studio_) && studio_) { studio_ = false; SaveSetting(settings_, "ui.studio", "0"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The essentials: pick, pose, pull, save.");
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        if (Seg("Studio", studio_) && !studio_) { studio_ = true; SaveSetting(settings_, "ui.studio", "1"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything: precise values, track weights, bone list,\nAdvanced tab (stances, feet, priority, blends, loop, drive).\nThe clip does not change when you switch modes.");
    }
    if (sel_ != inspSelPrev_) { if (sel_ >= 0) inspForce_ = 0; inspSelPrev_ = sel_; }   // a new selection: its tab
    if (ImGui::BeginTabBar("##inspecteur")) {
        auto Flags = [&](int k) { return inspForce_ == k ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        if (ImGui::BeginTabItem("Selection", nullptr, Flags(0))) { InspectorSelection(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Clip", nullptr, Flags(1))) { InspectorClip(); ImGui::EndTabItem(); }
        if (studio_ && ImGui::BeginTabItem("Advanced", nullptr, Flags(2))) { InspectorAdvanced(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    inspForce_ = -1;
    ImGui::End();
}

void App::InspectorSelection() {
    const int n = doc_.BoneCount();
    Clip& c = doc_.clip();
    if (sel_ >= 0 && sel_ < n) {   // the tool a drag in the Stage uses, in words
        std::string what, tool;
        ImVec4 col;
        ToolInfo(what, tool, col);
        ImGui::TextColored(col, "Tool: %s", tool.c_str());
    }
    const bool lockedHere = sel_ >= 0 && doc_.LockedAt(doc_.KeyTime(t_));   // 2026-11-26: locked start / end (task 3)
    if (lockedHere) ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), "%s", "Start and end are locked to the game's pose:\npose between them (Clip tab to unlock).");
    if (sel_ >= 0 && sel_ == doc_.PelvisBone()) {
        ImGui::BeginDisabled(lockedHere);
        PelvisProps();
        ImGui::EndDisabled();
    } else if (sel_ >= 0 && sel_ < n) {
        const std::string& name = doc_.skeleton().names[sel_];
        ImGui::SeparatorText(("Bone: " + Short(name)).c_str());
        const bool legFrozen = doc_.LegFrozenAt(sel_, t_);   // 2026-11-26: the game takes the legs back from the catch
        if (legFrozen && !lockedHere) ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), "%s", "After the catch, the game takes the legs back:\npose them before the catch, or in the landing clip.");
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
                const ImVec4 col = sh.warn ? ImVec4(1.f, 0.45f, 0.35f, 1.f) : ImVec4(0.6f, 0.8f, 0.6f, 1.f);
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
    if (ImGui::Checkbox("On in game", &b)) { doc_.Checkpoint("on"); doc_.clip().enabled = b; }
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
    if (!studio_) ImGui::TextDisabled("Stances, priority, blends, loop, drive:\nStudio mode (at the top), Advanced tab.");
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
    else if (!var_.trick.empty()) lt = TrickClipFor(var_);
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

// UI Phase 4: the transport row (time, frames, keys, interpolation)
void App::JumpTo(float t) {
    if (doc_.EffectiveDrive() == Drive::ManualRatio) return;
    askTime_ = std::clamp(t, 0.f, doc_.clip().duration);
    Request(Ask::ChangeTime);
}

void App::TimelineTransport() {
    Clip& c = doc_.clip();
    const float fs = ImGui::GetFontSize();
    static const char* kInterpName[3] = {"smooth", "linear", "constant"};
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Clip    ");
    ImGui::SameLine();
    if (doc_.EffectiveDrive() != Drive::ManualRatio) {
        const float nowF = std::round(t_ / kFrame);
        if (ImGui::Button("|<")) JumpTo(0.f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start of the clip (Home)");
        ImGui::SameLine(0, fs * 0.2f);
        if (ImGui::Button("<##img")) JumpTo((nowF - 1.f) * kFrame);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous frame (left arrow)");
        ImGui::SameLine(0, fs * 0.2f);
        if (playing_) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent); }
        if (ImGui::Button(playing_ ? "Pause##c" : "Play##c", ImVec2(fs * 4.5f, 0))) { if (playing_) playing_ = false; else Request(Ask::Play); }
        if (playing_) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays the clip (Space)");
        ImGui::SameLine(0, fs * 0.2f);
        if (ImGui::Button(">##img")) JumpTo((nowF + 1.f) * kFrame);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next frame (right arrow)");
        ImGui::SameLine(0, fs * 0.2f);
        if (ImGui::Button(">|")) JumpTo(c.duration);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("End of the clip (End)");
        // 2026-11-25 (user request): loop and speed of the editor playback, to study the motion
        ImGui::SameLine(0, fs * 0.5f);
        const bool gameLoops = doc_.LoopsInTime();
        const bool loopOn = gameLoops || loopPreview_;
        if (loopOn) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        if (gameLoops) ImGui::BeginDisabled();
        if (ImGui::Button("Loop (L)")) { loopPreview_ = !loopPreview_; SaveSetting(settings_, "timeline.loop", loopPreview_ ? "1" : "0"); }
        if (gameLoops) ImGui::EndDisabled();
        if (loopOn) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", gameLoops ? "This clip also loops in game: playback always loops."
                                              : "Replays the clip endlessly, to study it.\nEditor only: in game, the clip plays as before.");
        ImGui::SameLine(0, fs * 0.2f);
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
        if (ImGui::SliderFloat("simulated ManualRatio", &r, std::min(c.r0, c.r1), std::max(c.r0, c.r1), "%.2f")) {
            if (!doc_.HasPending()) doc_.SetSimulatedRatio(r);
            else Status("set or discard the pending change first (K)");
        }
    }
    ImGui::SameLine(0, fs);
    // time and frame readout (30 images per second, like the game's recordings)
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.2f s", t_);
    ImGui::SameLine(0, fs * 0.4f);
    ImGui::TextColored(kMuted, "frame %d / %d   length %.2f s", (int)std::lround(t_ / kFrame), (int)std::lround(c.duration / kFrame), c.duration);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The clip is set frame by frame: 30 frames per second.");
    if (doc_.IsHold()) { ImGui::SameLine(); ImGui::TextDisabled("(held pose: a single pose, all the time)"); }
    else if (doc_.EffectiveDrive() == Drive::Cycle) { ImGui::SameLine(); ImGui::TextDisabled("(one full stride, synced to the game's steps)"); }
    ImGui::SameLine(0, fs);
    if (ImGui::Button("+ key (K)")) CommitKeyNow();
    ImGui::SameLine();
    const bool pelSel = sel_ >= 0 && sel_ == doc_.PelvisBone();
    if (!keySel_.empty()) {
        if (ImGui::Button("- selected keys (Del)")) DeleteSelectedKeys();
    } else if (ImGui::Button("- key")) { doc_.Checkpoint("delete key"); doc_.DeleteKeysAt(doc_.KeyTime(t_), (doc_.IsEditable(sel_) || pelSel) ? sel_ : -1); }
    ImGui::SameLine();
    if (doc_.CanPaste()) {   // 2026-11-26 copy / paste
        if (ImGui::Button("Paste (Ctrl+V)")) PasteNow();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", doc_.ClipboardHasKeys() ? "Pastes the copied keys, the first one at the playhead (same spacing and interpolation)."
                                                                             : "Keys the copied pose at the playhead. The other keys keep their pose.");
        ImGui::SameLine();
    }
    // interpolation: of the selected keys, else of the key of the selected bone under the playhead
    {
        int cur = -1;
        if (!keySel_.empty()) {
            const KeySel& k0 = keySel_.front();
            cur = doc_.KeyInterp(k0.t, k0.bones.empty() ? -1 : k0.bones.front());
            for (auto& k : keySel_) if (doc_.KeyInterp(k.t, k.bones.empty() ? -1 : k.bones.front()) != cur) cur = -2;   // mixed
        } else if (doc_.IsEditable(sel_) || pelSel) cur = doc_.KeyInterp(t_, sel_);
        if (cur != -1) {
            for (int m = 0; m < 3; m++) {
                if (m) ImGui::SameLine();
                if (ImGui::RadioButton(kInterpName[m], cur == m) && cur != m) {
                    if (!keySel_.empty()) SetSelectedInterp(m);
                    else { doc_.Checkpoint("interpolation"); doc_.SetKeyInterp(t_, sel_, m); }
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("smooth: a soft curve through the keys\nlinear: constant speed from one key to the next\nconstant: the pose holds until the next key");
            ImGui::SameLine();
        }
    }
    if (doc_.IsTrickClip()) {
        ImGui::TextDisabled("%s", "legs: given back to the game 0.12 s after the catch");
    }
}

// UI Phase 4: view controls (zoom, fit, row density, enlarge) and the selection line
void App::TimelineViewBar() {
    const Clip& c = doc_.clip();
    const float fs = ImGui::GetFontSize();
    static const char* kInterpName[3] = {"smooth", "linear", "constant"};
    const float dur = c.duration;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "Zoom");
    ImGui::SameLine();
    const float mid = tview_.Start(dur) + tview_.Span(dur) * 0.5f;
    const float anchor = tview_.Visible(t_, dur) ? t_ : mid;   // zoom around the playhead when it is visible
    if (ImGui::Button("-##zoom")) tview_.ZoomAt(1.f / 1.5f, anchor, dur);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("See more time (Ctrl + wheel on the timeline)");
    ImGui::SameLine(0, fs * 0.2f);
    if (ImGui::Button("+##zoom")) tview_.ZoomAt(1.5f, anchor, dur);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("See closer (Ctrl + wheel on the timeline)");
    ImGui::SameLine(0, fs * 0.2f);
    const bool fitted = tview_.Fitted(dur);
    if (fitted) ImGui::BeginDisabled();
    if (ImGui::Button("Fit")) tview_.Fit();
    if (fitted) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Shows the whole clip across the width.");
    ImGui::SameLine(0, fs * 0.4f);
    ImGui::TextColored(kMuted, fitted ? "whole clip" : "x%.1f  (%.2f to %.2f s)", tview_.Zoom(dur), tview_.Start(dur), tview_.Start(dur) + tview_.Span(dur));
    ImGui::SameLine(0, fs);
    if (ImGui::Checkbox("compact rows", &compactRows_)) SaveSetting(settings_, "timeline.compact", compactRows_ ? "1" : "0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lower rows: more body parts visible at once.");
    ImGui::SameLine();
    // 2026-11-25: was "all bones" next to the keys, unclear; a view option, saved
    if (ImGui::Checkbox("empty rows too", &allRows_)) SaveSetting(settings_, "timeline.allbones", allRows_ ? "1" : "0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also shows the body parts that have no key yet.\nUnticked: only the ones your clip animates (easier to read).");
    ImGui::SameLine();
    {
        const Layout L = Lay();
        if (ImGui::Button(bottomBeforeBig_ > 0.f ? "Shrink the timeline" : "Enlarge the timeline")) {
            if (bottomBeforeBig_ > 0.f) { bottomU_ = bottomBeforeBig_; bottomBeforeBig_ = 0.f; }
            else { bottomBeforeBig_ = L.bottom / fs; bottomU_ = L.h * 0.55f / fs; bottomDefault_ = false; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gives the timeline more room, then puts it back as before.\nYou can also drag the edge between the 3D view and the timeline.");
    }
    ImGui::SameLine(0, fs);
    // selection line
    {
        if (keySel_.empty()) ImGui::TextDisabled("Click: pick a key · Shift+click: add · Shift+drag: box · right-click: menu · Ctrl+wheel: zoom · Shift+wheel or middle-click: scroll");
        else if (keySel_.size() == 1) {
            const KeySel& k = keySel_.front();
            std::string who = k.bones.empty() ? std::string("all bones") : k.bones.size() == 1 ? BoneLabelUi(doc_.skeleton().names[k.bones.front()]) : (std::to_string(k.bones.size()) + " bones");
            if (k.bones.size() == 1 && k.bones.front() == doc_.PelvisBone()) who = "Pelvis";
            const int ip = doc_.KeyInterp(k.t, k.bones.empty() ? -1 : k.bones.front());
            ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Selected key: %.2f s · %s · %s", k.t, who.c_str(), ip >= 0 ? kInterpName[ip] : "?");
        } else {
            float a = 1e9f, b = -1e9f;
            for (auto& k : keySel_) { a = std::min(a, k.t); b = std::max(b, k.t); }
            ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%d keys selected (%.2f to %.2f s) · Del to delete · Esc to deselect", (int)keySel_.size(), a, b);
        }
    }

}

void App::TimelinePanel() {
    const Layout L = Lay();
    Place(0, L.h - L.status - L.bottom, L.w, L.bottom);
    ImGui::Begin("Timeline", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // selected keys that no longer exist (undo, another clip, deleted) leave the selection
    keySel_.erase(std::remove_if(keySel_.begin(), keySel_.end(), [&](const KeySel& k) {
        const std::vector<float> ts = k.bones.empty() ? doc_.KeyTimes(-1) : doc_.KeyTimesBones(k.bones);
        for (float x : ts) if (std::fabs(x - k.t) < 1e-3f) return false;
        return true;
    }), keySel_.end());
    Clip& c = doc_.clip();
    const float fs = ImGui::GetFontSize();
    static const char* kInterpName[3] = {"smooth", "linear", "constant"};

    EndWarnings(false);   // 2026-11-26: parts not back at the end of a trick / landing clip
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Game    ");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's original movement, to compare with (the blue ghost).");
    ImGui::SameLine();
    if (doc_.HasFigure()) {
        if (ImGui::Button(originPlaying_ ? "||##o" : ">##o", ImVec2(fs * 2, 0))) originPlaying_ = !originPlaying_;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays the game's movement alone, looping.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 26.f);
        ImGui::SliderFloat("##origine", &originT_, 0.f, doc_.FigureDuration(), "%.2f s");
        originFocus_ = originPlaying_ || ImGui::IsItemActive() || ImGui::IsItemHovered();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the game's pose at that moment.");
        ImGui::SameLine();
        // 2026-11-25: was "Take this pose (all / bone + children)", unclear to the user
        const bool part = doc_.IsEditable(sel_);
        if (ImGui::Button(part ? "Copy the game's pose to this part" : "Copy the game's pose to the whole body")) {
            doc_.Checkpoint("game pose");
            doc_.TakeOriginPose(originT_, part ? sel_ : -1);
            Status("game pose taken");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the game's pose shown by this slider (%.2f s) to the playhead (%.2f s).\nUse it to put a part back as in the game, or to reuse a game pose from another moment.\nA part picked: that part and what it carries (the arm with the hand). Nothing picked: the whole body.\nWith \"Auto key\", the key is set right away; Ctrl+Z undoes it.", originT_, t_);
    } else {
        originFocus_ = false;
        ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "game animation not recorded yet: do \"%s\" once in game", VariantTitleUi(var_).c_str());
    }

    TimelineTransport();   // UI Phase 4
    TimelineViewBar();

    // rows: "All", then one row per limb group, unfolded into one row per bone
    struct DRow { std::string label; std::vector<int> bones; int primary; int depth; std::string gid; bool fold; };
    if (sel_ != prevSel_) {   // a bone picked anywhere: unfold its group and bring it into view
        prevSel_ = sel_;
        const std::string g = doc_.GroupOfBone(sel_);
        if (!g.empty()) { expanded_.insert(g); scrollToSel_ = true; }
    }
    std::vector<DRow> rows;
    rows.push_back(DRow{"All", {}, -1, 0, "", false});
    for (auto& G : doc_.KeyGroups(allRows_)) {
        const bool open = expanded_.count(G.id) != 0;
        const int prim = !G.children.empty() ? G.children.front().primary : G.bones.front();
        rows.push_back(DRow{G.label, G.bones, prim, 0, G.id, !G.children.empty()});
        if (open) for (auto& r : G.children) rows.push_back(DRow{r.label, r.bones, r.primary, 1, G.id, false});
    }
    const float labelW = fs * 9.f, rowH = compactRows_ ? fs * 1.05f : fs * 1.35f;
    ImGuiIO& io = ImGui::GetIO();
    // UI Phase 4: the same width for the ruler and the rows (room kept for the rows' scrollbar)
    const float W = std::max(50.f, ImGui::GetContentRegionAvail().x - labelW - ImGui::GetStyle().ScrollbarSize);
    const float dur = c.duration;
    const TrickMarkers hang = doc_.IsTrickClip() ? doc_.Markers() : TrickMarkers{};   // 2026-11-25 landing sync
    {   // another clip or variant: show it whole; the view always stays inside the clip
        const std::string key = fileName_ + "|" + FigureKey(var_);
        if (key != tviewKey_) { tviewKey_ = key; tview_.Fit(); }
        tview_.Clamp(dur);
        if (std::fabs(t_ - tviewLastT_) > 1e-5f) { tview_.Follow(t_, dur, playing_ ? 0.f : 0.1f); tviewLastT_ = t_; }
    }
    float trackX = ImGui::GetCursorScreenPos().x + labelW, top = 0.f, bottom = 0.f;
    auto X = [&](float t) { return tview_.ToX(t, trackX + 8.f, W - 16.f, dur); };
    auto T = [&](float x) { return std::clamp(tview_.ToT(x, trackX + 8.f, W - 16.f, dur), 0.f, dur); };
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
            const float tn = std::min(std::round(T(io.MousePos.x) / kFrame) * kFrame, dur);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) JumpTo(tn);
            else if (ImGui::IsItemActive() && !doc_.HasPending() && std::fabs(tn - t_) > 1e-4f) JumpTo(tn);
        }
        rd->AddRectFilled(rulerP, ImVec2(rulerP.x + W, rulerP.y + rulerH), IM_COL32(38, 40, 48, 255));
        rd->PushClipRect(rulerP, ImVec2(rulerP.x + W, rulerP.y + rulerH), true);
        const float pps = (W - 16.f) / tview_.Span(dur);
        const float minor = RulerStep(pps, fs * 0.45f), major = RulerStep(pps, fs * 4.5f);
        const float a0 = tview_.Start(dur), a1 = a0 + tview_.Span(dur);
        for (int i = (int)std::floor(a0 / minor); i * minor <= a1 + 1e-4f; i++) {
            const float t = i * minor;
            if (t < -1e-4f || t > dur + 1e-4f) continue;
            const float q = t / major;
            const bool isMajor = std::fabs(q - std::round(q)) < 1e-3f;
            const float x = X(t);
            rd->AddLine(ImVec2(x, rulerP.y + (isMajor ? rulerH * 0.45f : rulerH * 0.75f)), ImVec2(x, rulerP.y + rulerH), IM_COL32(110, 112, 124, 255));
            if (isMajor) {
                char lb[32];
                if (major < 0.099f) snprintf(lb, sizeof(lb), "%d", (int)std::lround(t / kFrame));   // very close: frame numbers
                else snprintf(lb, sizeof(lb), major < 1.f ? "%.2f s" : "%.0f s", t);
                rd->AddText(ImVec2(x + 3.f, rulerP.y + 1.f), IM_COL32(170, 172, 184, 255), lb);
            }
        }
        if (dur > 0.f) {   // end of the clip
            const float xe = X(dur);
            rd->AddLine(ImVec2(xe, rulerP.y), ImVec2(xe, rulerP.y + rulerH), IM_COL32(200, 200, 210, 160), 1.f);
        }
        const float xp = X(t_);   // playhead handle
        if (InView(xp)) {
            const float hw = fs * 0.4f;
            const ImVec2 tri[3] = {ImVec2(xp - hw, rulerP.y + rulerH - hw * 1.6f), ImVec2(xp + hw, rulerP.y + rulerH - hw * 1.6f), ImVec2(xp, rulerP.y + rulerH)};
            rd->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32(255, 90, 90, 255));
        }
        rd->PopClipRect();
        if (rulerHov) ImGui::SetTooltip(major < 0.099f ? "Click or drag: change the time · wheel: zoom\nThe numbers are frames (30 per second)." : "Click or drag: change the time · wheel: zoom");
    }
    // zoom / pan with the mouse over the ruler and the rows
    const ImVec2 winMax(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
    const bool overTracks = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MousePos.x >= trackX && io.MousePos.x <= trackX + W &&
                            io.MousePos.y >= rulerP.y && io.MousePos.y <= winMax.y;
    const bool overRuler = overTracks && io.MousePos.y <= rulerP.y + rulerH;
    const float wheelPan = io.MouseWheelH != 0.f ? io.MouseWheelH : (io.KeyShift ? io.MouseWheel : 0.f);
    if (overTracks && io.MouseWheel != 0.f && (io.KeyCtrl || (overRuler && !io.KeyShift)))
        tview_.ZoomAt(std::pow(1.25f, io.MouseWheel), T(io.MousePos.x), dur);
    else if (overTracks && wheelPan != 0.f) tview_.Pan(-wheelPan * tview_.Span(dur) * 0.15f, dur);
    if (overTracks && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) tview_.Pan(-io.MouseDelta.x / std::max(1.f, W - 16.f) * tview_.Span(dur), dur);
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
        if (inRow) ImGui::PushStyleColor(ImGuiCol_Text, exact ? ImVec4(1.f, 0.75f, 0.3f, 1.f) : ImVec4(1.f, 0.9f, 0.6f, 1.f));
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
        const ImU32 bg = ri == 0 ? IM_COL32(45, 47, 56, 255) : exact ? IM_COL32(72, 58, 30, 255) : inRow ? IM_COL32(55, 48, 35, 255)
                       : r.depth ? IM_COL32(28, 30, 36, 255) : (ri % 2 ? IM_COL32(35, 37, 44, 255) : IM_COL32(31, 33, 39, 255));
        dl->AddRectFilled(q0, ImVec2(q0.x + W, q0.y + rowH), bg);
        if (hang.ok && hang.land > hang.catchT + 1e-3f) {   // 2026-11-25: the hang time, stretched in game with the jump height
            const float xa = std::clamp(X(hang.catchT), q0.x, q0.x + W), xb = std::clamp(X(hang.land), q0.x, q0.x + W);
            if (xb > xa) dl->AddRectFilled(ImVec2(xa, q0.y), ImVec2(xb, q0.y + rowH), IM_COL32(200, 170, 60, 38));
        }
        if (doc_.HasDismount()) {   // 2026-11-25: the dismount part of a riding clip, tinted
            const float xd = std::clamp(X(LoopLength(c)), q0.x, q0.x + W);
            dl->AddRectFilled(ImVec2(xd, q0.y), ImVec2(q0.x + W, q0.y + rowH), IM_COL32(120, 90, 160, 50));
        }
        {   // grid: the ruler's labelled steps
            const float major = RulerStep((W - 16.f) / tview_.Span(dur), fs * 4.5f);
            const float a0 = tview_.Start(dur), a1 = a0 + tview_.Span(dur);
            for (int i = (int)std::ceil(a0 / major - 1e-4f); i * major <= std::min(a1, dur) + 1e-4f; i++)
                dl->AddLine(ImVec2(X(i * major), q0.y), ImVec2(X(i * major), q0.y + rowH), IM_COL32(52, 54, 64, 255));
        }
        if (dur > 0.f && X(dur) < q0.x + W) dl->AddRectFilled(ImVec2(X(dur) + 8.f, q0.y), ImVec2(q0.x + W, q0.y + rowH), IM_COL32(20, 21, 26, 255));
        const std::vector<float> keys = r.bones.empty() ? doc_.KeyTimes(-1) : doc_.KeyTimesBones(r.bones);
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
            const int ip = doc_.KeyInterp(k, r.bones.empty() ? -1 : r.bones.front());
            ImU32 col = ri == 0 ? IM_COL32(200, 200, 210, 255) : IM_COL32(230, 200, 80, 255);
            if (std::fabs(kt - t_) < 1e-3f) col = kColSel;
            if (ip == EditorDoc::IP_Step) dl->AddRectFilled(ImVec2(x - rr * 0.8f, my - rr * 0.8f), ImVec2(x + rr * 0.8f, my + rr * 0.8f), col);   // square: constant
            else dl->AddConvexPolyFilled(pts, 4, col);
            if (ip == EditorDoc::IP_Linear) dl->AddLine(ImVec2(x - rr * 0.5f, my), ImVec2(x + rr * 0.5f, my), IM_COL32(40, 40, 40, 255), 2.f);   // dash: linear
            if (picked) dl->AddPolyline(pts, 4, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 2.f);
            hits.push_back(Hit{r.bones, k, x, my});
        }
        if (ri == 0) for (float lt : {0.f, dur})   // the padlocks also show on an empty clip
            if (doc_.LockedAt(lt) && InView(X(lt)) && std::none_of(keys.begin(), keys.end(), [&](float k) { return std::fabs(k - lt) < 1e-3f; })) drawLock(X(lt), my, rad);
        float hit = -1.f;
        for (float k : keys) if (!lockedKey(k) && InView(X(k)) && std::fabs(X(k) - io.MousePos.x) < fs * 0.5f) hit = k;
        bool lockHover = false;
        for (float lt : {0.f, dur}) if (doc_.LockedAt(lt) && InView(X(lt)) && std::fabs(X(lt) - io.MousePos.x) < fs * 0.5f) lockHover = true;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (r.primary >= 0 && (doc_.IsEditable(r.primary) || r.primary == doc_.PelvisBone()) && !inRow) { sel_ = r.primary; prevSel_ = sel_; }
            if (io.KeyShift) {
                if (hit >= 0.f) { ToggleKey(r.bones, hit); clickedKey = true; }
                else { boxing_ = true; boxX0_ = io.MousePos.x; boxY0_ = io.MousePos.y; }
            } else if (hit >= 0.f) {
                clickedKey = true;
                if (!KeySelected(r.bones, hit)) { keySel_.clear(); keySel_.push_back(KeySel{r.bones, hit}); }
                if (doc_.HasPending()) { askTime_ = hit; Request(Ask::ChangeTime); }
                else if (!doc_.IsHold()) { dragKey_ = true; dragBones_ = r.bones; dragFromT_ = dragKeyT_ = hit; dragClickX_ = io.MousePos.x; }
            } else {
                keySel_.clear();
                if (doc_.EffectiveDrive() != Drive::ManualRatio) { askTime_ = Snap(io.MousePos.x); Request(Ask::ChangeTime); }
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hit >= 0.f) {
            if (!KeySelected(r.bones, hit)) { keySel_.clear(); keySel_.push_back(KeySel{r.bones, hit}); }
            openKeyMenu_ = true;
        }
        if (ImGui::IsItemHovered() && hit < 0.f && lockHover)
            ImGui::SetTooltip("%s", "Locked: the game's pose, so the clip joins the game smoothly.\nPose between the start and the end (Clip tab: Lock start and end).");
        if (ImGui::IsItemHovered() && hit >= 0.f) {
            const int ip = doc_.KeyInterp(hit, r.bones.empty() ? -1 : r.bones.front());
            ImGui::SetTooltip("%.2f s · %s · %s\nclick: select, drag: move\nShift+click: add to the selection, right-click: menu", hit, r.label.c_str(), ip >= 0 ? kInterpName[ip] : "?");
        }
        ImGui::PopID();
    }
    (void)clickedKey;
    if (doc_.IsTrickClip() && bottom > top) {
        static const char* kPhaseLabel[6] = {"", "takeoff", "flip", "catch", "landing", "ground"};
        float lastEnd = -1e9f;
        for (int ph = 1; ph <= 5; ph++) {
            const float pt = doc_.PhaseStart(ph);
            if (pt < 0.f || pt > c.duration + 1e-3f) continue;
            const float x = X(std::min(pt, c.duration));
            if (!InView(x)) continue;
            dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), ph == 5 ? IM_COL32(120, 230, 140, 200) : IM_COL32(120, 200, 255, 160), ph == 5 ? 2.f : 1.f);
            if (x >= lastEnd) {
                dl->AddText(ImVec2(x + 3.f, top + 1.f), ph == 5 ? IM_COL32(120, 230, 140, 255) : IM_COL32(120, 200, 255, 255), kPhaseLabel[ph]);
                lastEnd = x + 3.f + ImGui::CalcTextSize(kPhaseLabel[ph]).x + 4.f;
            }
        }
    }
    if (doc_.HasDismount() && bottom > top) {
        const float xd = X(LoopLength(c));
        if (InView(xd)) dl->AddLine(ImVec2(xd, top), ImVec2(xd, bottom), IM_COL32(190, 150, 255, 220), 2.f);
        if (InView(xd)) dl->AddText(ImVec2(xd + 3.f, top + 1.f), IM_COL32(190, 150, 255, 255), "dismount");
    }
    if (bottom > top && InView(X(t_))) dl->AddLine(ImVec2(X(t_), top), ImVec2(X(t_), bottom), IM_COL32(255, 90, 90, 255), 2.f);
    // box selection
    if (boxing_) {
        const float x0 = std::min(boxX0_, io.MousePos.x), x1 = std::max(boxX0_, io.MousePos.x);
        const float y0 = std::min(boxY0_, io.MousePos.y), y1 = std::max(boxY0_, io.MousePos.y);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 200, 80, 40));
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 200, 80, 200));
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
        } else if (doc_.EffectiveDrive() != Drive::ManualRatio) { askTime_ = dragFromT_; Request(Ask::ChangeTime); }
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
