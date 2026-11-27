// Session Studio -- one timeline, two files (2026-11-27, user request): a trick clip and the landing clip of that trick
// are edited on one timeline. The part under the playhead is the open document (doc_, var_, fileName_, savedText_,
// figErr_); the other part waits in comp_. Moving the playhead across the ground contact exchanges them. Each part keeps
// its own file and its own undo history; Save writes both. The landing is LOCKED by default: the game plays its own
// landing and nothing can be posed there; unlocked, the landing clip of this trick is yours (Clip settings).
// The mod and the clip format are unchanged: the game still starts the landing clip at the real ground contact.
#include "app_internal.h"

using namespace app_detail;

TwoPartTime App::Parts() const {
    TwoPartTime tp;
    tp.first = TrickDoc().clip().duration;
    tp.second = hasComp_ ? (inLanding_ ? doc_ : comp_.doc).clip().duration : 0.f;
    return tp;
}

float App::SharedT() const { return hasComp_ ? JoinTime(Parts(), inLanding_ ? 1 : 0, t_) : t_; }

bool App::LandingLockedHere() const { return hasComp_ && inLanding_ && landLocked_; }

const Clip* App::TrickForLanding() const {
    if (hasComp_ && inLanding_) return &comp_.doc.clip();   // the trick being edited, not only its saved file
    return TrickClipFor(var_);
}

void App::SwapParts() {
    std::swap(doc_, comp_.doc);
    std::swap(var_, comp_.var);
    std::swap(fileName_, comp_.fileName);
    std::swap(savedText_, comp_.savedText);
    std::swap(figErr_, comp_.figErr);
    inLanding_ = !inLanding_;
    keySel_.clear();
    dragKey_ = false;
    dragBones_.clear();
    boxing_ = false;
}

void App::SetShared(float g) {
    if (!hasComp_) { t_ = std::clamp(g, 0.f, doc_.clip().duration); return; }
    int part = 0;
    float local = 0.f;
    SplitTime(Parts(), g, part, local);
    if ((part == 1) != inLanding_) { SwapParts(); doc_.TakeClipboard(comp_.doc); }   // copy on one side, paste on the other
    t_ = local;
}

void App::JumpShared(float g) {
    if (doc_.EffectiveDrive() == Drive::ManualRatio) return;
    if (!hasComp_) { askGlobal_ = -1.f; askTime_ = std::clamp(g, 0.f, doc_.clip().duration); }
    else askGlobal_ = std::clamp(g, 0.f, Parts().Total());
    Request(Ask::ChangeTime);   // through the pending-change check
}

void App::LeaveLanding() {
    if (inLanding_) { SwapParts(); t_ = doc_.clip().duration; }
}

bool App::PartNeedsSave(bool landingPart, const EditorDoc& d, const std::string& file, const std::string& saved) const {
    if (landingPart && file.empty()) return !landLocked_;   // a new landing of yours is written; a locked one never
    return ClipToJson(d.clip()) != saved;
}

void App::BuildLandingPart() {
    LeaveLanding();   // callers are on the trick part already; this keeps the slots right if not
    hasComp_ = false;
    inLanding_ = false;
    landLocked_ = true;
    comp_ = Companion{};
    if (!doc_.IsTrickClip() || !doc_.clip().trickHash || var_.trick.empty() || clipsDir_.empty() || !skeletonOk_) return;
    FigureVariant lv = var_;
    lv.state = ST_Landing;
    // the landing clip of THIS trick clip, if there is one (a trick clip not saved yet has none)
    int self = -1;
    for (int i = 0; i < (int)clips_.size(); i++) if (!fileName_.empty() && clips_[i].file == fileName_) self = i;
    const int found = LandingForTrick(ClipPtrs(), self, lv);
    const std::string foundFile = found >= 0 ? clips_[found].file : std::string();
    const Clip trick = doc_.clip();
    // build the landing part in the landing slot (status messages of a missing landing recording stay quiet)
    const std::string keepStatus = status_;
    const float keepAge = statusAge_;
    const size_t keepMsgs = msgs_.size();
    comp_.doc = doc_;   // the skeleton; NewClip / OpenClip below clear the clip and the undo history
    comp_.var = lv;
    SwapParts();
    bool opened = false;
    if (!foundFile.empty()) {
        std::string text, err;
        if (ReadFileText(clipsDir_ + L"\\" + Widen(foundFile), text) && doc_.OpenClip(text, foundFile, err)) {
            const Clip keep = doc_.clip();
            SelectFigure(lv);
            doc_.clip().states = keep.states; doc_.clip().trick = keep.trick; doc_.clip().trickHash = keep.trickHash;
            fileName_ = foundFile;
            savedText_ = ClipToJson(doc_.clip());
            landLocked_ = !doc_.clip().enabled;   // an enabled landing clip of yours: unlocked
            opened = true;
        }
    }
    if (!opened) {   // no landing clip yet: the game's landing, ready to be yours when unlocked
        doc_.NewClip(lv);
        SelectFigure(lv);
        DefaultMode();
        if (doc_.HasFigure()) doc_.SetDuration(TrickLandingLength(trick.duration, doc_.FigureDuration()));   // 2 s with the trick
        doc_.SeedKeys();
        doc_.SeedLandingFromTrick(trick);   // it starts where the trick ends
        doc_.clip().link = trick.link;
        fileName_.clear();
        savedText_ = ClipToJson(doc_.clip());
        landLocked_ = true;
    }
    const bool usable = doc_.HasFigure();   // without the game's landing recording there is nothing to show
    SwapParts();
    status_ = keepStatus;
    statusAge_ = keepAge;
    if (msgs_.size() > keepMsgs) msgs_.resize(keepMsgs);
    hasComp_ = usable;
    if (!hasComp_) { comp_ = Companion{}; landLocked_ = true; }
}

// 2026-11-27: after a new recording, the part that is not under the playhead is read again too. The game's own landing
// (locked, never written: nothing of yours in it) is made again from the new recording; a landing clip of yours, saved
// or unlocked, keeps its keys and only gets the new game movement under them. A trick that had no landing gets one.
void App::ReloadOtherPart() {
    if (!hasComp_) { BuildLandingPart(); return; }   // does nothing unless a trick clip is open
    const std::string keepStatus = status_;
    const float keepAge = statusAge_;
    const size_t keepMsgs = msgs_.size();
    SwapParts();
    ReloadShownFigure();
    SwapParts();
    status_ = keepStatus;   // the message about the part shown stays
    statusAge_ = keepAge;
    if (msgs_.size() > keepMsgs) msgs_.resize(keepMsgs);
    const bool gamesOwn = landLocked_ && (inLanding_ ? fileName_ : comp_.fileName).empty();
    if (gamesOwn) {
        const float at = SharedT();
        BuildLandingPart();   // leaves the landing part first, so it starts from the trick
        SetShared(hasComp_ ? std::min(at, Parts().Total()) : at);
    }
}

std::vector<const Clip*> App::ClipPtrs() const {
    std::vector<const Clip*> v;
    for (auto& ci : clips_) v.push_back(ci.err.empty() ? &ci.clip : nullptr);
    return v;
}

std::vector<std::string> App::LandingFilesOf(const std::string& trickFile) const {
    std::vector<std::string> out;
    for (int i = 0; i < (int)clips_.size(); i++)
        if (clips_[i].file == trickFile)
            for (int l : LandingsOfTrick(ClipPtrs(), i)) out.push_back(clips_[l].file);
    return out;
}

std::string App::LandingNote(const std::string& file) const {
    for (int i = 0; i < (int)clips_.size(); i++) {
        const ClipInfo& ci = clips_[i];
        if (ci.file != file || !ci.err.empty() || !(ci.clip.states & ST_Landing) || (ci.clip.states & ST_Trick) || !ci.clip.trickHash) continue;
        const int t = TrickOfLanding(ClipPtrs(), i);
        if (t >= 0) return "landing of " + clips_[t].file.substr(0, clips_[t].file.size() - 6);
        return ci.clip.link.empty() ? "landing without a trick clip" : "its trick clip is gone: never plays";
    }
    return "";
}

std::string App::LandingName(const std::string& trickFile) const {
    if (trickFile.size() <= 6) return AutoName();
    const std::string base = "Landing-" + trickFile.substr(0, trickFile.size() - 6);
    for (int i = 1; i < 1000; i++) {
        const std::string n = base + (i == 1 ? std::string() : "_" + std::to_string(i)) + ".sanim";
        if (!FileExists(clipsDir_ + L"\\" + Widen(n))) return n;
    }
    return AutoName();
}

void App::LinkLandingFiles() {
    if (!hasComp_) return;
    EditorDoc& trick = inLanding_ ? comp_.doc : doc_;
    EditorDoc& land = inLanding_ ? doc_ : comp_.doc;
    if (trick.clip().link.empty()) {
        std::string err;
        if (trick.SaveText(err).empty()) return;   // the trick cannot be saved: change nothing
        const std::string id = NewLinkId();
        const std::string& trickFile = inLanding_ ? comp_.fileName : fileName_;
        const std::string& landFile = inLanding_ ? fileName_ : comp_.fileName;
        // an older trick clip may own more than the landing shown (another foot, another stance): they follow it, else
        // the game would stop playing them
        for (const std::string& f : LandingFilesOf(trickFile))
            if (f != landFile && !SetClipLinkFile(clipsDir_ + L"\\" + Widen(f), id, backupRoot_, err)) Status(f + ": " + err);
        trick.clip().link = id;
    }
    land.clip().link = trick.clip().link;
}

void App::LinkLandingSeam() {
    if (!hasComp_) { doc_.UnlinkStart(); return; }
    EditorDoc& land = inLanding_ ? doc_ : comp_.doc;
    if (landLocked_) land.UnlinkStart();                       // the game's landing: nothing of yours to write
    else land.LinkStartToTrick(inLanding_ ? comp_.doc : doc_);
}

void App::SetLandingLocked(bool lock) {
    if (!hasComp_ || lock == landLocked_) return;
    if (doc_.HasPending()) { Status("set or discard the pending change first (K)"); return; }
    EditorDoc& land = inLanding_ ? doc_ : comp_.doc;
    landLocked_ = lock;
    land.clip().enabled = !lock;   // a landing clip of yours that exists stays on disk, turned off while locked
    Status(lock ? "landing locked: the game plays its own landing (your landing clip, if any, is kept but turned off)"
                : "landing unlocked: pose it after the ground contact; Save writes it with the trick");
}
