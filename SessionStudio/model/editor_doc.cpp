#include "editor_doc.h"
#include "../../SessionAnimLoader/Native/src/cycle.h"
#include "ik.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kKeyEps = 1e-3f;
constexpr size_t kMaxUndo = 200;
bool Fail(std::string& err, const std::string& m) { err = m; return false; }
// q^e (same axis, angle multiplied by e); q taken on the shortest path
Quat QPow(Quat q, float e) {
    q = QNorm(q);
    if (q.w < 0.f) q = Quat{-q.x, -q.y, -q.z, -q.w};
    const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-7f) return QIdent;
    const float a = 2.f * std::atan2(s, q.w) * e;
    const float k = std::sin(a * 0.5f) / s;
    return QNorm(Quat{q.x * k, q.y * k, q.z * k, std::cos(a * 0.5f)});
}
} // namespace

bool IsEditableBone(const std::string& n) {
    if (n.rfind("SKXX_", 0) == 0) return false;
    static const char* kParts[] = {"Spine1", "Spine2", "Spine3", "_Neck", "_Head", "Clavicle", "UpperArm", "Forearm", "_Hand", "Finger"};
    for (const char* p : kParts) if (n.find(p) != std::string::npos) return true;
    return false;
}

// ------------------------------------------------------------------ chargement
bool EditorDoc::LoadSkeleton(const std::string& json, std::string& err) {
    SkeletonFile s;
    if (!ParseSkeleton(json, s, err)) return false;
    skel_ = std::move(s);
    map_ = BuildMirrorMap(skel_.names, skel_.parents, skel_.refPos);
    map_.conv = skel_.conv;   // effective conventions written by the game (overrides included)
    editable_.assign(skel_.names.size(), false);
    leg_.assign(skel_.names.size(), false);
    for (size_t i = 0; i < skel_.names.size(); i++) { editable_[i] = IsEditableBone(skel_.names[i]); leg_[i] = IsLegBone(skel_.names[i]); }
    if (fig_.bones > 0 && fig_.bones != (int)skel_.names.size()) fig_ = FigureRec{};
    rig_ = BuildPelvisRig(skel_.names, skel_.parents.data(), (int)skel_.names.size());
    pending_.clear();
    pelPending_ = false;
    return true;
}

FigureVariant LegacyVariant(int figure) {
    FigureVariant v;
    if (figure == FIG_NoseManual) v.state = ST_NoseManual;
    else if (figure == FIG_Kickflip) { v.state = ST_Trick; v.trick = "KickFlip"; }
    else v.state = ST_Manual;
    return v;
}

FigureVariant VariantOfClip(const Clip& c) {
    FigureVariant v;
    for (uint32_t b : kStateOrder) if (c.states & b) { v.state = b; break; }
    if (c.states & ST_Trick) v.state = ST_Trick;
    if (v.state == ST_Trick) v.trick = c.trick.empty() ? std::string("KickFlip") : c.trick;
    if (v.state == ST_Grind) v.trick = c.trick;   // 2026-11-25: one grind ("" = every grind)
    for (int s = 0; s < STANCE_Count; s++) if (c.stances & (1u << s)) { v.stance = s; break; }
    v.goofy = c.feet == Feet::Goofy;
    return v;
}

void SetClipVariant(Clip& c, const FigureVariant& v) {
    c.states = v.state;
    c.trick = StateNamed(v.state) ? v.trick : std::string();
    c.trickHash = TrickHash(c.trick);
    c.stances = (uint8_t)(1u << v.stance);
    c.feet = v.goofy ? Feet::Goofy : Feet::Regular;
    c.mirror = false;   // made for one body: never mirrored
}

int EditorDoc::FigureIndex() const {
    for (int f = 0; f < FIG_Count; f++) {
        const FigureVariant l = LegacyVariant(f);
        if (l.state == variant_.state && Lower(l.trick) == Lower(variant_.trick)) return f;
    }
    return -1;
}

bool EditorDoc::OriginPos(int bone, float t, float out[3]) const {
    if (!HasFigure() || fig_.posBones.empty()) return false;
    int k = -1;
    for (size_t i = 0; i < fig_.posBones.size(); i++) if (fig_.posBones[i] == bone) { k = (int)i; break; }
    if (k < 0) return false;
    const int n = (int)fig_.times.size();
    const size_t nb = fig_.posBones.size();
    if (fig_.positions.size() != (size_t)n * nb * 3 || n == 0) return false;
    const float period = fig_.times.back();
    float u = std::isfinite(t) ? t : 0.f;
    if (IsTrickClip() || IsLandingClip() || !(period > 1e-6f)) u = std::clamp(u, 0.f, period > 0.f ? period : 0.f);
    else if (u < 0.f || u > period) { u = std::fmod(u, period); if (u < 0.f) u += period; }
    int i = 0;
    while (i + 1 < n && fig_.times[i + 1] <= u) i++;
    const int j = i + 1 < n ? i + 1 : i;
    const float span = fig_.times[j] - fig_.times[i];
    const float a = span > 1e-6f ? (u - fig_.times[i]) / span : 0.f;
    for (int d = 0; d < 3; d++) {
        const float p0 = fig_.positions[((size_t)i * nb + k) * 3 + d], p1 = fig_.positions[((size_t)j * nb + k) * 3 + d];
        out[d] = p0 + (p1 - p0) * a;
    }
    return true;
}

bool EditorDoc::LoadFigure(const std::string& json, std::string& err) {
    FigureRec r;
    if (!ParseFigure(json, r, err)) return false;
    FigureVariant v;
    if (r.figure == "Manual") v = LegacyVariant(FIG_Manual);
    else if (r.figure == "NoseManual") v = LegacyVariant(FIG_NoseManual);
    else if (r.figure == "Kickflip") v = LegacyVariant(FIG_Kickflip);
    else if (!ParseFigureKey(r.figure, v)) return Fail(err, "recording: unknown variant name");
    return LoadFigure(json, v, err);
}

bool EditorDoc::LoadFigure(const std::string& json, const FigureVariant& v, std::string& err) {
    FigureRec r;
    if (!ParseFigure(json, r, err)) return false;
    if (HasSkeleton() && r.bones != BoneCount())
        return Fail(err, "recording made with another skeleton (" + std::to_string(r.bones) + " bones instead of " + std::to_string(BoneCount()) + "): record it again in game");
    FaceBoardForward(r);
    fig_ = std::move(r);
    if (!(variant_ == v)) ClearClipboard();   // another variant (the same one reloaded keeps the clipboard)
    variant_ = v;
    ComputeCycle();
    SetClipForFigure();
    return true;
}

void EditorDoc::ClearFigure(const FigureVariant& v) {
    fig_ = FigureRec{};
    if (!(variant_ == v)) ClearClipboard();
    variant_ = v;
    hasCycle_ = false;
    SetClipForFigure();
}

void EditorDoc::SetClipForFigure() {
    if (clip_.tracks.empty() && clip_.pelvis.empty()) ApplyFigureDefaults(clip_, variant_);   // empty clip: move settings (name and file kept); a pelvis-only clip has keys
    else if (VariantOfClip(clip_).state != variant_.state || Lower(VariantOfClip(clip_).trick) != Lower(variant_.trick)) {
        // another figure: the trigger follows it (stance and feet kept as they are)
        clip_.states = variant_.state;
        clip_.trick = StateNamed(variant_.state) ? variant_.trick : std::string();
        clip_.trickHash = TrickHash(clip_.trick);
        if (!StateLoops(variant_.state)) { clip_.loop = false; clip_.hold = false; }   // a trick clip never loops
    }
}

// ------------------------------------------------------------------ clip
void EditorDoc::NewClip(const FigureVariant& v) {
    Clip c;
    c.name = "nouveau_clip";
    c.enabled = true;
    ApplyFigureDefaults(c, v);   // compares v with the variant of the loaded figure: before variant_ changes
    variant_ = v;
    clip_ = std::move(c);
    pending_.clear();
    pelPending_ = false;
    undo_.clear(); redo_.clear();
    ClearClipboard();
}

void EditorDoc::ApplyFigureDefaults(Clip& c, const FigureVariant& v) const {
    SetClipVariant(c, v);
    c.priority = 0;
    c.drive = Drive::Time;
    c.duration = 3.0f;   // like the recorded original animation (3 s)
    c.loop = true;
    c.blendIn = 0.25f; c.blendOut = 0.30f;
    c.mode = Mode::Additive;   // P6: edits are added on top of the game's animation by default
    // base poses: a held pose by default (a 3 s loop never stays in phase with the game's own walk / push cycle)
    c.hold = false;   // user 2026-11-25: a held pose has no life; animated loops by default, hold is an option
    // 2026-11-27: late flips raise the catch almost at once, so their clips keep the legs after it by default
    c.legsAfterCatch = v.state == ST_Trick && IsLateFlip(v.trick);
    if (!StateLoops(v.state)) {   // trick clip: a single pass, from the pop to the landing
        c.loop = false;
        c.hold = false;
        // lot 5: the air part only (a new figure also holds 2 s on the ground, for the landing clip; 0.8 s before 2026-11-26)
        const float air = GroundAt() > 0.f ? GroundAt() : FigureDuration();
        c.duration = (HasFigure() && variant_ == v && air > 0.f) ? air : 1.0f;
        c.blendIn = 0.05f; c.blendOut = 0.15f;
        if (v.state == ST_Landing) { c.blendIn = 0.12f; c.blendOut = 0.30f; }   // comes in at the contact, over the trick clip
        // 2026-11-26: new trick and landing clips have their start / end locked to the game's pose (can be turned off)
        if (v.state == ST_Trick || v.state == ST_Landing) SetClipExtraBool(c, "lockEnds", true);
    }
    // 2026-11-25: walking follows the game's steps: one clip = one stride of the recording (cycle drive)
    if ((v.state == ST_Walking || v.state == ST_Running) && HasFigure() && variant_ == v && hasCycle_) {   // running too
        c.drive = Drive::Cycle;
        c.duration = cycEnd_ - cycStart_;
    }
}

bool EditorDoc::OpenClip(const std::string& json, const std::string& file, std::string& err) {
    Clip c;
    if (!ParseClip(json, file, c, err)) return false;
    if (HasSkeleton() && !BindClip(c, skel_.names, err)) return false;
    clip_ = std::move(c);
    variant_ = VariantOfClip(clip_);
    if (clip_.states & ST_Trick) { clip_.loop = false; clip_.drive = Drive::Time; }
    if (!CanHold()) clip_.hold = false;
    // a held clip only has keys at 0; keys elsewhere = an animation in time: the file's keys win, hold goes off
    if (clip_.hold) for (float k : KeyTimes(-1)) if (k > kKeyEps && !(clip_.dismount > 0.f && k >= LoopLength(clip_) - kKeyEps)) { clip_.hold = false; break; }   // dismount keys do not count
    MergeLoopEnd();
    pending_.clear();
    pelPending_ = false;
    undo_.clear(); redo_.clear();
    ClearClipboard();
    return true;
}

std::string EditorDoc::SaveText(std::string& err) const {
    if (clip_.tracks.empty() && clip_.pelvis.empty()) { err = "the clip has no key: set at least one key (K) before saving"; return ""; }
    Clip saved = clip_;
    if (IsTrickClip()) {
        saved.loop = false; saved.drive = Drive::Time;   // a trick never loops, driven by time
        // 2026-11-25 landing sync: the game's events of the recording go with the clip (the player warps onto them);
        // the old lot 5 landing part is gone (its keys stay, played after the ground while the landing clip comes in)
        if (saved.landing > 0.f) { saved.landing = 0.f; saved.groundAt = 0.f; }
        TrickMarkers m = Markers();
        if (m.ok && m.ground > saved.duration) {   // a clip cut before the ground: the markers stop at its end
            m.ground = saved.duration;
            m.land = std::min(m.land, m.ground); m.catchT = std::min(m.catchT, m.land); m.air = std::min(m.air, m.catchT);
        }
        saved.markers = m;
    }
    if (!CanHold()) saved.hold = false;
    const std::string text = ClipToJson(saved);
    Clip check;
    if (!ParseClip(text, clip_.file, check, err)) { err = "the player would refuse this clip: " + err; return ""; }
    if (HasSkeleton() && !BindClip(check, skel_.names, err)) { err = "the player would refuse this clip: " + err; return ""; }
    err.clear();
    return text;
}

bool EditorDoc::HasLockedTracks() const {
    for (auto& t : clip_.tracks) if (!EditableName(t.bone)) return true;
    return false;
}

int EditorDoc::TrackIndex(int bone) const {
    if (bone < 0 || bone >= BoneCount()) return -1;
    for (size_t i = 0; i < clip_.tracks.size(); i++) if (clip_.tracks[i].bone == skel_.names[bone]) return (int)i;
    return -1;
}

const Track* EditorDoc::TrackOf(int bone) const {
    const int i = TrackIndex(bone);
    return i < 0 ? nullptr : &clip_.tracks[i];
}

std::vector<float> EditorDoc::KeyTimes(int bone) const {
    std::vector<float> v;
    const int only = bone >= 0 ? TrackIndex(bone) : -1;   // computed once
    if (bone >= 0 && only < 0 && bone != PelvisBone()) return v;
    for (size_t i = 0; i < clip_.tracks.size(); i++) {
        if (bone >= 0 && (int)i != only) continue;

        for (auto& k : clip_.tracks[i].keys) {
            bool dup = false;
            for (float x : v) if (std::fabs(x - k.t) < kKeyEps) dup = true;
            if (!dup) v.push_back(k.t);
        }
    }
    if (bone < 0 || bone == PelvisBone())   // P6 lot 2: the pelvis keys
        for (auto& k : clip_.pelvis) {
            bool dup = false;
            for (float x : v) if (std::fabs(x - k.t) < kKeyEps) dup = true;
            if (!dup) v.push_back(k.t);
        }
    std::sort(v.begin(), v.end());
    return v;
}

void EditorDoc::SetKeySmooth(float t, int bone, bool smooth) { SetKeyInterp(t, bone, smooth ? IP_Smooth : IP_Linear); }

// ------------------------------------------------------------------ pose
Quat EditorDoc::Origin(int bone, float t) const {
    const int n = (int)fig_.times.size();
    if (!HasFigure() || bone < 0 || bone >= fig_.bones || n == 0) return QIdent;
    const float period = fig_.times.back();
    if (n == 1 || !(period > 1e-6f)) return QNorm(fig_.frames[bone]);
    if (!std::isfinite(t)) t = 0.f;
    // [0, period] reads directly (the last frame stays reachable); beyond, the original animation loops,
    // except for a trick clip (single pass): clamped to [0, period]
    float u = t;
    if (IsTrickClip() || (clip_.states & ST_Landing)) u = u < 0.f ? 0.f : (u > period ? period : u);   // one shot: no loop
    else if (u < 0.f || u > period) {
        u = std::fmod(t, period);
        if (u < 0.f) u += period;
    }
    int i = 0;
    while (i + 1 < n && fig_.times[i + 1] <= u) i++;
    if (i + 1 >= n) return QNorm(fig_.frames[(size_t)i * fig_.bones + bone]);
    const float span = fig_.times[i + 1] - fig_.times[i];
    const float a = span > 1e-6f ? (u - fig_.times[i]) / span : 0.f;
    return QSlerp(QNorm(fig_.frames[(size_t)i * fig_.bones + bone]), QNorm(fig_.frames[(size_t)(i + 1) * fig_.bones + bone]), a);
}

Quat EditorDoc::OriginAtRatio(int bone, float ratio) const {
    const int n = (int)fig_.ratios.size();
    if (!HasFigure() || bone < 0 || bone >= fig_.bones || n == 0) return QIdent;
    int best = 0;
    float bestD = 1e30f;
    for (int i = 0; i < n; i++) {
        const float d = std::fabs(fig_.ratios[i] - ratio);
        if (d < bestD) { bestD = d; best = i; }
    }
    return QNorm(fig_.frames[(size_t)best * fig_.bones + bone]);
}

Quat EditorDoc::OriginFor(int bone, float t) const {
    if (InDismount(t)) return OriginDismount(bone, t - LoopLength(clip_));   // 2026-11-25: the game's dismount
    return EffectiveDrive() == Drive::ManualRatio ? OriginAtRatio(bone, simRatio_) : Origin(bone, FigTime(t));
}

Quat EditorDoc::EditedRaw(int bone, float t) const {
    auto it = pending_.find(bone);
    if (it != pending_.end()) return it->second;
    return PoseNoPending(bone, t, OriginFor(bone, t));
}

Quat EditorDoc::PoseNoPending(int bone, float t, const Quat& o) const {
    const Track* tr = TrackOf(bone);
    if (!tr || tr->keys.empty()) return o;
    // a trick clip never loops, as in the player
    const Quat k = EvalClipTrack(clip_, *tr, KeyTime(t), LoopsInTime());   // 2026-11-25: knows the dismount part
    // same formula as Player::Apply, bone weight included
    return clip_.mode == Mode::Replace ? QSlerp(o, k, tr->weight) : QNorm(QMul(o, QSlerp(QIdent, k, tr->weight)));
}

Quat EditorDoc::KeyFor(int bone, int track, Quat pose, float t) const {
    return KeyForOrigin(OriginFor(bone, t), track, pose);
}

Quat EditorDoc::KeyForOrigin(const Quat& o, int track, Quat pose) const {
    const float w = track >= 0 ? clip_.tracks[track].weight : 1.f;
    // the player does slerp(origin, key, w) (replace) or origin x key^w (additive): inverted to get the wanted pose back
    const Quat delta = QNorm(QMul(QInv(o), pose));
    if (clip_.mode == Mode::Replace) return (w > 1e-3f && w < 0.999f) ? QNorm(QMul(o, QPow(delta, 1.f / w))) : pose;
    return (w > 1e-3f && w < 0.999f) ? QPow(delta, 1.f / w) : delta;
}

Quat EditorDoc::Edited(int bone, float t) const {
    const Quat r = EditedRaw(bone, t);
    // legs of a trick clip: same weight rule as the player; a pending change is shown as it is
    if (IsTrickClip() && bone >= 0 && bone < (int)leg_.size() && leg_[bone] && !pending_.count(bone))
        return QSlerp(OriginFor(bone, t), r, EaseSine(LegWeightAt(t)));
    return r;
}

float EditorDoc::PhaseStart(int phase) const {
    if (phase < 0 || phase > FigureRecorder::kGroundPhase) return -1.f;
    const size_t n = std::min(fig_.phases.size(), fig_.times.size());
    for (size_t i = 0; i < n; i++)   // 0..4 bounded like the recorder; 5 = on the ground (lot 5)
        if ((fig_.phases[i] == FigureRecorder::kGroundPhase ? fig_.phases[i] : std::clamp(fig_.phases[i], 0, 4)) == phase) return fig_.times[i];
    return -1.f;
}

float EditorDoc::LegWeightAt(float t) const {
    if (clip_.legsAfterCatch) return 1.f;   // as in the player
    float tc = PhaseStart(3);
    if (tc < 0.f) tc = PhaseStart(4);
    if (tc < 0.f || !std::isfinite(t)) return 1.f;
    const float air = std::clamp(1.f - (t - tc) / 0.12f, 0.f, 1.f);   // 0.12 s: the player's kLegFade
    return air;
}

void EditorDoc::OriginPose(float tOrigin, std::vector<FTr>& out) const {
    const int n = BoneCount();
    out.assign(n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    if (n == 0) return;
    std::vector<Quat> lq(n);
    std::vector<float> lp((size_t)n * 3, 0.f);
    for (int b = 0; b < n; b++) {
        lq[b] = Origin(b, tOrigin);
        if (skel_.refPos.size() >= (size_t)n * 3) for (int k = 0; k < 3; k++) lp[(size_t)b * 3 + k] = skel_.refPos[(size_t)b * 3 + k];
        float rp[3];
        if (OriginPos(b, tOrigin, rp)) for (int k = 0; k < 3; k++) lp[(size_t)b * 3 + k] = rp[k];
    }
    std::vector<char> dirty(n, 1);
    ToComponent(out.data(), skel_.parents.data(), n, lq.data(), (const float (*)[3])lp.data(), (const bool*)dirty.data());
}

void EditorDoc::ComponentPose(float t, std::vector<FTr>& out, bool ghost, bool raw, bool pelvis) const {
    const int n = BoneCount();
    out.assign(n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    if (n == 0) return;
    std::vector<Quat> lq(n);
    std::vector<float> lp((size_t)n * 3, 0.f);
    for (int b = 0; b < n; b++) {
        lq[b] = ghost ? OriginFor(b, t) : raw ? EditedRaw(b, t) : Edited(b, t);
        if (skel_.refPos.size() >= (size_t)n * 3) for (int k = 0; k < 3; k++) lp[(size_t)b * 3 + k] = skel_.refPos[(size_t)b * 3 + k];
        float rp[3];
        if (InDismount(t) ? DismountPos(b, t - LoopLength(clip_), rp) : OriginPos(b, FigTime(t), rp)) for (int k = 0; k < 3; k++) lp[(size_t)b * 3 + k] = rp[k];
    }
    std::vector<char> dirty(n, 1);
    ToComponent(out.data(), skel_.parents.data(), n, lq.data(), (const float (*)[3])lp.data(), (const bool*)dirty.data());
    if (!ghost && pelvis) {   // P6 lot 2: pelvis offset, feet kept (same code as the player)
        float off[3];
        PelvisOffset(t, off);
        ApplyPelvisOffset(out.data(), n, rig_, off);
    }
}

// ------------------------------------------------------------------ editing
bool EditorDoc::SetPending(int bone, Quat local) {
    if (!IsEditable(bone)) return false;
    pending_[bone] = QNorm(local);
    return true;
}

bool EditorDoc::Pending(int bone, Quat* q) const {
    auto it = pending_.find(bone);
    if (it == pending_.end()) return false;
    if (q) *q = it->second;
    return true;
}

void EditorDoc::CommitKey(float t) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    if (!std::isfinite(t)) t = 0.f;
    const float tPose = std::clamp(t, 0.f, clip_.duration);   // the instant shown (its original animation)
    t = KeyTime(tPose);                                        // where the key goes (0 for a held pose)
    // hold keys: a key belongs to one bone only; without them, the new key would change that bone's pose
    // at the other times already keyed. Values computed first, on the state before the commit, without pendings.
    struct Hold { int bone; float t; Quat q; bool smooth; };
    std::vector<Hold> holds;
    std::vector<float> times = KeyTimes(-1);
    // 2026-11-25: no key at the start yet: the start keeps the game's pose (a hold key at 0 = no change there).
    // Without it the first key put in the middle applied from the very start of the clip too.
    if (autoStartKey && t > kKeyEps && (times.empty() || times.front() > kKeyEps)) times.insert(times.begin(), 0.f);
    for (auto& kv : pending_) {
        const int b = kv.first;
        if (!IsEditable(b)) continue;
        const int ex = TrackIndex(b);
        for (float tau : times) {
            if (std::fabs(tau - t) < kKeyEps) continue;
            bool has = false;
            bool smooth = true;
            if (ex >= 0)
                for (auto& k : clip_.tracks[ex].keys) {
                    if (std::fabs(k.t - tau) < kKeyEps) has = true;
                    else if (k.t < tau) smooth = k.smooth;   // sorted keys: the last one before tau
                }
            if (has) continue;
            // in ManualRatio, the pose at tau is the origin at the ratio matching that time (as SetMode)
            const Quat o = EffectiveDrive() == Drive::ManualRatio
                ? OriginAtRatio(b, clip_.r0 + (clip_.duration > 0.f ? tau / clip_.duration : 0.f) * (clip_.r1 - clip_.r0))
                : OriginFor(b, tau);
            holds.push_back(Hold{b, tau, KeyForOrigin(o, ex, PoseNoPending(b, tau, o)), smooth});
        }
    }
    for (auto& h : holds) {
        int ti = TrackIndex(h.bone);
        if (ti < 0) {
            Track tr;
            tr.bone = skel_.names[h.bone];
            tr.index = h.bone;
            clip_.tracks.push_back(tr);
            ti = (int)clip_.tracks.size() - 1;
        }
        auto& keys = clip_.tracks[ti].keys;
        keys.push_back(Key{h.t, h.q, h.smooth});
        std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b2) { return a.t < b2.t; });
    }
    for (auto& kv : pending_) {
        const int b = kv.first;
        if (!IsEditable(b)) continue;   // pending leg of a clip that is no longer a trick: never a locked track
        const int existing = TrackIndex(b);
        const Quat key = KeyFor(b, existing, kv.second, tPose);
        int ti = existing;
        if (ti < 0) {
            Track tr;
            tr.bone = skel_.names[b];
            tr.index = b;
            clip_.tracks.push_back(tr);
            ti = (int)clip_.tracks.size() - 1;
        }
        auto& keys = clip_.tracks[ti].keys;
        bool done = false;
        for (auto& k : keys) if (std::fabs(k.t - t) < kKeyEps) { k.q = key; done = true; }
        if (!done) {
            keys.push_back(Key{t, key, true});
            std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b2) { return a.t < b2.t; });
        }
    }
    if (pelPending_) {   // P6 lot 2: pelvis key, with hold keys at the other key times (same rule as the bones)
        const bool lp = LoopsInTime();
        std::vector<PosKey> add;
        for (float tau : times) {
            if (std::fabs(tau - t) < kKeyEps) continue;
            bool has = false;
            bool smooth = true;
            for (auto& k : clip_.pelvis) { if (std::fabs(k.t - tau) < kKeyEps) has = true; else if (k.t < tau) smooth = k.smooth; }
            if (has) continue;
            PosKey h{tau, {0, 0, 0}, smooth};
            EvalClipPelvis(clip_, tau, lp, h.v);
            add.push_back(h);
        }
        bool done = false;
        for (auto& k : clip_.pelvis) if (std::fabs(k.t - t) < kKeyEps) { for (int j = 0; j < 3; j++) k.v[j] = pel_[j]; done = true; }
        if (!done) add.push_back(PosKey{t, {pel_[0], pel_[1], pel_[2]}, true});
        for (auto& k : add) clip_.pelvis.push_back(k);
        std::sort(clip_.pelvis.begin(), clip_.pelvis.end(), [](const PosKey& a, const PosKey& b2) { return a.t < b2.t; });
        pelPending_ = false;
    }
    pending_.clear();
}

void EditorDoc::DeleteKeysAt(float t, int bone) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    for (size_t i = 0; i < clip_.tracks.size(); i++) {
        if (bone >= 0 && (int)i != TrackIndex(bone)) continue;
        if (bone < 0 && !EditableName(clip_.tracks[i].bone)) continue;   // "all" = all editable bones: never the locked tracks
        auto& keys = clip_.tracks[i].keys;
        keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Key& k) { return std::fabs(k.t - t) < kKeyEps; }), keys.end());
    }
    clip_.tracks.erase(std::remove_if(clip_.tracks.begin(), clip_.tracks.end(), [](const Track& tr) { return tr.keys.empty(); }), clip_.tracks.end());
    if (bone < 0 || bone == PelvisBone())
        clip_.pelvis.erase(std::remove_if(clip_.pelvis.begin(), clip_.pelvis.end(), [&](const PosKey& k) { return std::fabs(k.t - t) < kKeyEps; }), clip_.pelvis.end());
}

bool EditorDoc::MoveKeys(float from, float to) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    if (!std::isfinite(to) || to < 0.f || to > clip_.duration) return false;
    if (std::fabs(from - to) < kKeyEps) return true;
    if (LockedAt(from) || LockedAt(to)) return false;   // a locked key does not move, nothing moves onto it
    for (auto& tr : clip_.tracks) {
        bool hasFrom = false, hasTo = false;
        for (auto& k : tr.keys) { if (std::fabs(k.t - from) < kKeyEps) hasFrom = true; if (std::fabs(k.t - to) < kKeyEps) hasTo = true; }
        if (hasFrom && hasTo) return false;
    }
    {
        bool hasFrom = false, hasTo = false;
        for (auto& k : clip_.pelvis) { if (std::fabs(k.t - from) < kKeyEps) hasFrom = true; if (std::fabs(k.t - to) < kKeyEps) hasTo = true; }
        if (hasFrom && hasTo) return false;
    }
    for (auto& tr : clip_.tracks) {
        for (auto& k : tr.keys) if (std::fabs(k.t - from) < kKeyEps) k.t = to;
        std::sort(tr.keys.begin(), tr.keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
    }
    for (auto& k : clip_.pelvis) if (std::fabs(k.t - from) < kKeyEps) k.t = to;
    std::sort(clip_.pelvis.begin(), clip_.pelvis.end(), [](const PosKey& a, const PosKey& b) { return a.t < b.t; });
    return true;
}

void EditorDoc::TakeOriginPose(float tOrigin, int root) {
    const int n = BoneCount();
    std::vector<bool> in(n, root < 0);
    if (root >= 0 && root < n) {
        in[root] = true;
        for (int i = root + 1; i < n; i++) if (skel_.parents[i] >= 0 && in[skel_.parents[i]]) in[i] = true;
    }
    for (int b = 0; b < n; b++) if (in[b] && IsEditable(b)) pending_[b] = Origin(b, tOrigin);
}

void EditorDoc::MirrorCopy(int bone, float t) {
    const int n = BoneCount();
    if (bone < 0 || bone >= n) return;
    std::vector<bool> in(n, false);
    in[bone] = true;
    for (int i = bone + 1; i < n; i++) if (skel_.parents[i] >= 0 && in[skel_.parents[i]]) in[i] = true;
    std::map<int, Quat> out;
    for (int b = 0; b < n; b++) {
        if (!in[b] || !IsEditable(b)) continue;
        const int o = (b < (int)map_.other.size()) ? map_.other[b] : b;
        if (o == b || !IsEditable(o)) continue;   // centre bone: no twin
        const int p = skel_.parents[b];
        const int cb = map_.conv[b];
        // without the trick leg weight: else the key would be attenuated twice
        out[o] = MirrorKey(EditedRaw(b, t), p >= 0 ? map_.conv[p] : cb, cb);
    }
    for (auto& kv : out) pending_[kv.first] = kv.second;
}

void EditorDoc::SetMode(Mode m) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    if (clip_.mode == m) return;
    ClearClipboard();   // key values mean something else in the other mode
    for (auto& tr : clip_.tracks) {
        int b = -1;
        for (int i = 0; i < BoneCount(); i++) if (skel_.names[i] == tr.bone) { b = i; break; }
        for (auto& k : tr.keys) {
            // origin at the time of EACH key (for ManualRatio: the ratio matching that time)
            const Quat o = EffectiveDrive() == Drive::ManualRatio
                ? OriginAtRatio(b, clip_.r0 + (clip_.duration > 0.f ? k.t / clip_.duration : 0.f) * (clip_.r1 - clip_.r0))
                : Origin(b, FigTime(k.t));
            k.q = (m == Mode::Replace) ? QNorm(QMul(o, k.q)) : QNorm(QMul(QInv(o), k.q));
        }
    }
    clip_.mode = m;
}

// ------------------------------------------------------------------ undo / redo
EditorDoc::Snap EditorDoc::MakeSnap(const char* label) const {
    Snap s{clip_, pending_, label ? label : "", pelPending_};
    for (int k = 0; k < 3; k++) s.pel[k] = pel_[k];
    return s;
}

void EditorDoc::Restore(Snap& s) {
    clip_ = std::move(s.clip);
    pending_ = std::move(s.pending);
    pelPending_ = s.pelPending;
    for (int k = 0; k < 3; k++) pel_[k] = s.pel[k];
}

void EditorDoc::Checkpoint(const char* label) {
    undo_.push_back(MakeSnap(label));
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

bool EditorDoc::Undo() {
    if (undo_.empty()) return false;
    Snap s = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(MakeSnap(s.label.c_str()));
    Restore(s);
    return true;
}

bool EditorDoc::Redo() {
    if (redo_.empty()) return false;
    Snap s = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(MakeSnap(s.label.c_str()));
    Restore(s);
    return true;
}

const char* EditorDoc::UndoLabel() const { return undo_.empty() ? "" : undo_.back().label.c_str(); }
const char* EditorDoc::RedoLabel() const { return redo_.empty() ? "" : redo_.back().label.c_str(); }

void EditorDoc::CheckpointState(const State& before, const char* label) {
    Snap s{before.clip, before.pending, label ? label : "", before.pelPending};
    for (int k = 0; k < 3; k++) s.pel[k] = before.pel[k];
    undo_.push_back(std::move(s));
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

// ------------------------------------------------------------------ held pose, loop seam
float EditorDoc::KeyTime(float t) const {
    if (HasDismount() && t >= LoopLength(clip_) - kKeyEps) return t;   // 2026-11-25: the dismount part is not looped nor held
    if (IsHold()) return 0.f;
    if (LoopsInTime() && clip_.duration > 0.f && t >= clip_.duration - kKeyEps) return 0.f;
    return t;
}

void EditorDoc::MergeLoopEnd() {
    if (!LoopsInTime() || HasDismount()) return;   // with a dismount part, a key at the loop end starts the dismount
    const float d = clip_.duration;
    for (auto& tr : clip_.tracks) {
        bool has0 = false;
        for (auto& k : tr.keys) if (k.t < kKeyEps) has0 = true;
        std::vector<Key> keep;
        for (auto& k : tr.keys) {
            if (k.t >= d - kKeyEps) { if (!has0) { Key z = k; z.t = 0.f; keep.insert(keep.begin(), z); has0 = true; } }
            else keep.push_back(k);
        }
        tr.keys = std::move(keep);
    }
    bool has0 = false;
    for (auto& k : clip_.pelvis) if (k.t < kKeyEps) has0 = true;
    std::vector<PosKey> keep;
    for (auto& k : clip_.pelvis) {
        if (k.t >= d - kKeyEps) { if (!has0) { PosKey z = k; z.t = 0.f; keep.insert(keep.begin(), z); has0 = true; } }
        else keep.push_back(k);
    }
    clip_.pelvis = std::move(keep);
}

void EditorDoc::SetHold(bool on, float t) {
    if (on && !CanHold()) return;
    if (on == clip_.hold) return;
    Checkpoint(on ? "held pose" : "animated pose");
    if (on) {
        if (!std::isfinite(t)) t = 0.f;
        const float kt = KeyTime(std::clamp(t, 0.f, clip_.duration));
        const bool lp = (clip_.loop && EffectiveDrive() == Drive::Time) || EffectiveDrive() == Drive::Cycle;
        for (auto& tr : clip_.tracks) {
            if (tr.keys.empty()) continue;
            const Quat q = EvalClipTrack(clip_, tr, kt, lp);
            std::vector<Key> keep{Key{0.f, q, true}};   // 2026-11-25: the dismount part keeps its keys
            if (HasDismount()) for (auto& k : tr.keys) if (k.t >= LoopLength(clip_) - kKeyEps) keep.push_back(k);
            tr.keys = std::move(keep);
        }
        if (!clip_.pelvis.empty()) {
            PosKey p{0.f, {0, 0, 0}, true};
            EvalClipPelvis(clip_, kt, lp, p.v);
            std::vector<PosKey> keep{p};
            if (HasDismount()) for (auto& k : clip_.pelvis) if (k.t >= LoopLength(clip_) - kKeyEps) keep.push_back(k);
            clip_.pelvis = std::move(keep);
        }
    }
    clip_.hold = on;
    if (!on) MergeLoopEnd();
}

// ------------------------------------------------------------------ step cycle (2026-11-25)
bool EditorDoc::LoopsInTime() const {
    if (IsHold() || IsTrickClip()) return false;
    const Drive d = EffectiveDrive();
    return d == Drive::Cycle || (clip_.loop && d == Drive::Time);
}

float EditorDoc::FigTime(float t) const {
    if (!hasCycle_ || EffectiveDrive() != Drive::Cycle || !(clip_.duration > 0.f) || !std::isfinite(t)) return t;
    float u = t / clip_.duration;
    u -= std::floor(u);
    return cycStart_ + u * (cycEnd_ - cycStart_);
}

void EditorDoc::ComputeCycle() {
    hasCycle_ = false;
    if (!HasFigure() || !HasSkeleton() || !rig_.Ok() || fig_.times.size() < 3) return;
    std::vector<FootSample> s;
    std::vector<FTr> cs;
    for (float t : fig_.times) {
        OriginPose(t, cs);   // the recording itself (raw figure time)
        const FTr &L = cs[rig_.foot[0]], &R = cs[rig_.foot[1]];
        s.push_back(FootSample{t, L.px, L.py, R.px, R.py});
    }
    float a = 0.f, b = 0.f;
    if (FirstCycle(s, a, b)) { cycStart_ = a; cycEnd_ = b; hasCycle_ = true; }
}
