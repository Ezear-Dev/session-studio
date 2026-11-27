// Session Studio -- EditorDoc: the ends of trick and landing clips (2026-11-26, PLAN-EDITOR-3-ADDITIONS tasks 2-3).
// A trick clip with no landing clip fades out over its blend out from the ground contact (player.cpp): a part not back
// at the game's pose by then changes fast at touchdown. A landing clip hands back to the game at its end, and should
// start where its trick clip ends.
#include "editor_doc.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kKeyEps = 1e-3f;
constexpr float kRad = 57.29578f;
float AngleDeg(const Quat& q) {
    const Quat n = QNorm(q);
    const float w = std::min(1.f, std::fabs(n.w));
    return 2.f * std::acos(w) * kRad;
}
// q^e on the shortest path
Quat QPowE(Quat q, float e) {
    q = QNorm(q);
    if (q.w < 0.f) q = Quat{-q.x, -q.y, -q.z, -q.w};
    const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-7f) return QIdent;
    const float a = 2.f * std::atan2(s, q.w) * e;
    const float k = std::sin(a * 0.5f) / s;
    return QNorm(Quat{q.x * k, q.y * k, q.z * k, std::cos(a * 0.5f)});
}
// the change a track gives at t, its weight included (additive)
Quat Effective(const Clip& c, const Track& tr, float t) {
    if (tr.keys.empty()) return QIdent;
    return QSlerp(QIdent, EvalClipTrack(c, tr, t, false), tr.weight);
}
const Track* TrackNamed(const Clip& c, const std::string& n) {
    for (auto& t : c.tracks) if (t.bone == n) return &t;
    return nullptr;
}
float TrickEnd(const Clip& trick) {
    float e = trick.markers.ok ? trick.markers.ground : trick.duration;
    return std::min(e > 0.f ? e : trick.duration, trick.duration);
}
float Len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
// sets (or replaces) the key of a track at t
void PutKey(std::vector<Key>& keys, float t, const Quat& q) {
    for (auto& k : keys) if (std::fabs(k.t - t) < kKeyEps) { k.q = q; return; }
    keys.push_back(Key{t, q, true});
    std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
}
void PutPel(std::vector<PosKey>& keys, float t, const float v[3]) {
    for (auto& k : keys) if (std::fabs(k.t - t) < kKeyEps) { for (int j = 0; j < 3; j++) k.v[j] = v[j]; return; }
    keys.push_back(PosKey{t, {v[0], v[1], v[2]}, true});
    std::sort(keys.begin(), keys.end(), [](const PosKey& a, const PosKey& b) { return a.t < b.t; });
}
const char* kStanceWord[STANCE_Count] = {"normal", "switch", "fakie", "nollie"};
} // namespace

bool LandingCoversAllVariants(const Clip& trick, const std::vector<const Clip*>& clips, std::string& missing) {
    missing.clear();
    std::vector<bool> feet;   // goofy?
    if (trick.feet == Feet::Any) feet = {false, true};
    else feet = {trick.feet == Feet::Goofy};
    for (int s = 0; s < STANCE_Count; s++) {
        if (!(trick.stances & (1u << s))) continue;
        for (bool goofy : feet) {
            bool covered = false;
            for (const Clip* c : clips) {
                if (!c || !c->enabled || c->foreign || !(c->states & ST_Landing) || (c->states & ST_Trick)) continue;
                if (!c->trickHash || c->trickHash != trick.trickHash) continue;   // a generic landing does not count
                if (c->link != trick.link) continue;                              // nor the landing of another trick clip
                if (!(c->stances & (1u << s))) continue;
                if (c->feet != Feet::Any && (c->feet == Feet::Goofy) != goofy) continue;
                covered = true;
                break;
            }
            if (!covered) {
                if (!missing.empty()) missing += ", ";
                missing += std::string(kStanceWord[s]) + (goofy ? " goofy" : " regular");
            }
        }
    }
    return missing.empty();
}

std::vector<EditorDoc::EndIssue> EditorDoc::EndIssues(const Clip* linkedTrick, bool hasLandingForAllVariants) const {
    std::vector<EndIssue> out;
    if (clip_.mode != Mode::Additive || !(IsTrickClip() || IsLandingClip())) return out;
    const int n = BoneCount();
    const float end = clip_.duration;
    auto endCheck = [&](EndIssue::Kind kind, bool skipLegs) {
        EndIssue e;
        e.kind = kind;
        e.t = end;
        for (int b = 0; b < n; b++) {
            if (!IsEditable(b) || (skipLegs && leg_[b])) continue;
            const Track* tr = TrackOf(b);
            if (!tr) continue;
            const float a = AngleDeg(Effective(clip_, *tr, end));
            if (a > kEndDeg) { e.bones.push_back(b); e.maxDeg = std::max(e.maxDeg, a); }
        }
        if (!clip_.pelvis.empty() && PelvisBone() >= 0) {
            float v[3];
            EvalClipPelvis(clip_, end, false, v);
            const float l = Len3(v);
            if (l > kEndCm) { e.pelvis = true; e.maxCm = l; }
        }
        if (!e.bones.empty() || e.pelvis) out.push_back(std::move(e));
    };
    if (IsTrickClip()) {
        if (!hasLandingForAllVariants) endCheck(EndIssue::TrickEndNoLanding, LegsReleased());   // legs: given back after the catch
        return out;
    }
    endCheck(EndIssue::LandingEndNotBack, false);
    if (linkedTrick && linkedTrick->mode == Mode::Additive && (linkedTrick->states & ST_Trick)) {
        // the start against the trick's end. Legs are not compared: the trick gives them back to the game before
        // the ground, so what the trick clip holds there is not seen.
        const float te = TrickEnd(*linkedTrick);
        EndIssue e;
        e.kind = EndIssue::LandingStartMismatch;
        e.t = 0.f;
        for (int b = 0; b < n; b++) {
            if (!IsEditable(b) || (leg_[b] && !linkedTrick->legsAfterCatch)) continue;
            const Track* mine = TrackOf(b);
            const Track* his = TrackNamed(*linkedTrick, skel_.names[b]);
            if (!mine && !his) continue;
            const Quat qa = mine ? Effective(clip_, *mine, 0.f) : QIdent;
            const Quat qb = his ? Effective(*linkedTrick, *his, te) : QIdent;
            const float a = AngleDeg(QMul(QInv(qa), qb));
            if (a > kEndDeg) { e.bones.push_back(b); e.maxDeg = std::max(e.maxDeg, a); }
        }
        if (PelvisBone() >= 0 && (!clip_.pelvis.empty() || !linkedTrick->pelvis.empty())) {
            float va[3] = {0, 0, 0}, vb[3] = {0, 0, 0};
            if (!clip_.pelvis.empty()) EvalClipPelvis(clip_, 0.f, false, va);
            if (!linkedTrick->pelvis.empty()) EvalClipPelvis(*linkedTrick, te, false, vb);
            const float d[3] = {va[0] - vb[0], va[1] - vb[1], va[2] - vb[2]};
            const float l = Len3(d);
            if (l > kEndCm) { e.pelvis = true; e.maxCm = l; }
        }
        if (!e.bones.empty() || e.pelvis) out.push_back(std::move(e));
    }
    return out;
}

bool EditorDoc::FixEndIssue(const EndIssue& e, const Clip* linkedTrick) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    if (clip_.mode != Mode::Additive) return false;
    const State before = SaveState();
    bool changed = false;
    const float end = clip_.duration;
    if (e.kind == EndIssue::TrickEndNoLanding || e.kind == EndIssue::LandingEndNotBack) {
        for (int b : e.bones) {
            if (!IsEditable(b)) continue;
            const int ti = TrackIndex(b);
            if (ti < 0) continue;
            PutKey(clip_.tracks[ti].keys, end, QIdent);   // the game's pose at the end
            changed = true;
        }
        if (e.pelvis && !clip_.pelvis.empty()) {
            const float z[3] = {0, 0, 0};
            PutPel(clip_.pelvis, end, z);
            changed = true;
        }
    } else if (linkedTrick) {   // start mismatch: the trick's end, for the listed parts only
        const float te = TrickEnd(*linkedTrick);
        for (int b : e.bones) {
            if (!IsEditable(b)) continue;
            const Track* his = TrackNamed(*linkedTrick, skel_.names[b]);
            const Quat target = his ? Effective(*linkedTrick, *his, te) : QIdent;
            int ti = TrackIndex(b);
            if (ti < 0) {   // a new track: from the trick's pose back to the game's at the end
                Track tr;
                tr.bone = skel_.names[b];
                tr.index = b;
                tr.keys.push_back(Key{end, QIdent, true});
                clip_.tracks.push_back(tr);
                ti = (int)clip_.tracks.size() - 1;
            }
            const float w = clip_.tracks[ti].weight;
            PutKey(clip_.tracks[ti].keys, 0.f, (w > 1e-3f && w < 0.999f) ? QPowE(target, 1.f / w) : target);
            changed = true;
        }
        if (e.pelvis && PelvisBone() >= 0) {
            float v[3] = {0, 0, 0};
            if (!linkedTrick->pelvis.empty()) EvalClipPelvis(*linkedTrick, te, false, v);
            if (clip_.pelvis.empty()) { const float z[3] = {0, 0, 0}; PutPel(clip_.pelvis, end, z); }
            PutPel(clip_.pelvis, 0.f, v);
            changed = true;
        }
    }
    if (changed) CheckpointState(before, e.kind == EndIssue::LandingStartMismatch ? "take the end of the trick" : "back to the game's pose");
    return changed;
}

// ------------------------------------------------------------------ the seam of the two-part timeline (2026-11-27)
bool EditorDoc::LinkStartToTrick(const EditorDoc& trick) {
    startLinked_ = IsLandingClip() && trick.IsTrickClip() && HasFigure() && trick.HasFigure() && BoneCount() == trick.BoneCount() &&
                   EffectiveDrive() == Drive::Time && trick.EffectiveDrive() == Drive::Time;
    if (!startLinked_) return false;
    constexpr float kSameDeg = 0.05f, kSameCm = 0.01f;   // under this, nothing is written (the file's rounding)
    const float te = TrickEnd(trick.clip_), end = clip_.duration;
    auto angle = [](const Quat& a, const Quat& b) {   // precise near zero (an arc cosine is not)
        const Quat d = QNorm(QMul(QInv(a), b));
        return 2.f * std::atan2(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), std::fabs(d.w)) * kRad;
    };
    bool changed = false;
    for (int b = 0; b < BoneCount(); b++) {
        if (!IsEditable(b)) continue;
        // what is SEEN at the end of the trick, keyed changes only (legs given back after the catch count as the game's)
        const Quat gameT = trick.OriginFor(b, te);
        Quat seen = trick.PoseNoPending(b, te, gameT);
        if (b < (int)trick.leg_.size() && trick.leg_[b]) seen = QSlerp(gameT, seen, EaseSine(trick.LegWeightAt(te)));
        const bool posed = angle(gameT, seen) > kSameDeg;
        int ti = TrackIndex(b);
        if (ti < 0 && !posed) continue;
        const Quat game0 = OriginFor(b, 0.f);
        const Quat want = posed ? seen : game0;
        const Quat gameEnd = OriginFor(b, end);
        if (angle(PoseNoPending(b, 0.f, game0), want) > kSameDeg) {
            if (ti < 0) {   // a part the link adds goes from the trick's pose back to the game's at the end
                Track tr;
                tr.bone = skel_.names[b];
                tr.index = b;
                tr.keys.push_back(Key{end, KeyForOrigin(gameEnd, -1, gameEnd), true});
                clip_.tracks.push_back(tr);
                ti = (int)clip_.tracks.size() - 1;
            }
            PutKey(clip_.tracks[ti].keys, 0.f, KeyForOrigin(game0, ti, want));
            if (clip_.tracks[ti].keys.size() == 1) PutKey(clip_.tracks[ti].keys, end, KeyForOrigin(gameEnd, ti, gameEnd));   // a lone key would hold all along
            changed = true;
        }
        if (ti >= 0 && !posed && clip_.mode == Mode::Additive) {   // nothing left but the game's pose at both ends: no part
            bool empty = true;
            for (auto& k : clip_.tracks[ti].keys)
                if ((k.t > kKeyEps && std::fabs(k.t - end) > kKeyEps) || angle(QIdent, k.q) > kSameDeg) empty = false;
            if (empty) { clip_.tracks.erase(clip_.tracks.begin() + ti); changed = true; }
        }
    }
    if (PelvisBone() >= 0) {
        float want[3] = {0, 0, 0}, have[3] = {0, 0, 0};
        if (!trick.clip_.pelvis.empty()) EvalClipPelvis(trick.clip_, te, false, want);
        if (!clip_.pelvis.empty()) EvalClipPelvis(clip_, 0.f, false, have);
        const float gap[3] = {want[0] - have[0], want[1] - have[1], want[2] - have[2]};
        const float zero[3] = {0, 0, 0};
        if (Len3(gap) > kSameCm) {
            if (clip_.pelvis.empty()) PutPel(clip_.pelvis, end, zero);
            PutPel(clip_.pelvis, 0.f, want);
            if (clip_.pelvis.size() == 1) PutPel(clip_.pelvis, end, zero);
            changed = true;
        }
        if (Len3(want) <= kSameCm && !clip_.pelvis.empty()) {
            bool empty = true;
            for (auto& k : clip_.pelvis) if ((k.t > kKeyEps && std::fabs(k.t - end) > kKeyEps) || Len3(k.v) > kSameCm) empty = false;
            if (empty) { clip_.pelvis.clear(); changed = true; }
        }
    }
    return changed;
}

// ------------------------------------------------------------------ locked start and end (task 3)
void SetClipExtraBool(Clip& c, const char* name, bool v) {
    JValue j; j.t = JValue::Bool; j.b = v;
    for (auto& kv : c.extra) if (kv.first == name) { kv.second = j; return; }
    c.extra.emplace_back(name, j);
}
bool ClipExtraBool(const Clip& c, const char* name, bool def) {
    for (auto& kv : c.extra) if (kv.first == name) return kv.second.t == JValue::Bool ? kv.second.b : def;
    return def;
}

namespace {
constexpr float kPreviewDeg = 0.05f, kPreviewCm = 0.01f;   // "posed there" for LockPreview: any visible change
}

bool EditorDoc::LockEnds() const { return ClipExtraBool(clip_, "lockEnds", false); }

void EditorDoc::SetLockEnds(bool on) {
    if (on == LockEnds() && ClipExtraBool(clip_, "lockEnds", !on) == on) return;
    const State before = SaveState();
    SetClipExtraBool(clip_, "lockEnds", on);
    if (on && LockAvailable()) {   // the user confirmed: every locked instant goes back to the game's pose
        if (StartLocked()) LockKeysAt(0.f, false);
        if (EndLockedRule()) LockKeysAt(clip_.duration, IsTrickClip() && LegsReleased());
    }
    CheckpointState(before, on ? "lock start and end" : "unlock start and end");
}

bool EditorDoc::EndLockedRule() const {
    if (!LockEnds() || !LockAvailable()) return false;
    return IsLandingClip() || !landCover_;
}
void EditorDoc::SetLegsAfterCatch(bool on) {
    if (clip_.legsAfterCatch == on) return;
    Checkpoint("legs after the catch");
    LockGuard lockGuard{*this};   // the locked end covers the legs from now on (or no longer does)
    clip_.legsAfterCatch = on;
}

bool EditorDoc::StartLocked() const { return LockEnds() && LockAvailable() && IsTrickClip(); }
bool EditorDoc::EndLocked() const {
    if (!EndLockedRule()) return false;
    if (IsLandingClip()) return true;
    // a trick whose end KEY is still posed (it lost its landing cover): left as it is until it is back (the fix button).
    // A track with no key at the end only carries its last key on: the lock gives it its end key.
    const float end = clip_.duration;
    for (auto& tr : clip_.tracks) {
        if (!EditableName(tr.bone) || (LegsReleased() && IsLegBone(tr.bone))) continue;
        for (auto& k : tr.keys)
            if (std::fabs(k.t - end) < kKeyEps && AngleDeg(QSlerp(QIdent, k.q, tr.weight)) > kEndDeg) return false;
    }
    for (auto& k : clip_.pelvis) if (std::fabs(k.t - end) < kKeyEps && Len3(k.v) > kEndCm) return false;
    return true;
}
bool EditorDoc::LockedAt(float t) const {
    if (StartLinked() && std::fabs(t) < kKeyEps) return true;   // 2026-11-27: it is the trick's end
    if (StartLocked() && std::fabs(t) < kKeyEps) return true;
    return EndLocked() && std::fabs(t - clip_.duration) < kKeyEps;
}

std::vector<EditorDoc::EndIssue> EditorDoc::LockPreview() const {
    std::vector<EndIssue> out;
    if (!LockAvailable()) return out;
    const int n = BoneCount();
    auto posed = [&](EndIssue::Kind kind, float t, bool skipLegs) {
        EndIssue e;
        e.kind = kind;
        e.t = t;
        for (int b = 0; b < n; b++) {
            if (!IsEditable(b) || (skipLegs && leg_[b])) continue;
            const Track* tr = TrackOf(b);
            if (!tr) continue;
            const float a = AngleDeg(Effective(clip_, *tr, t));
            if (a > kPreviewDeg) { e.bones.push_back(b); e.maxDeg = std::max(e.maxDeg, a); }
        }
        if (!clip_.pelvis.empty() && PelvisBone() >= 0) {
            float v[3];
            EvalClipPelvis(clip_, t, false, v);
            const float l = Len3(v);
            if (l > kPreviewCm) { e.pelvis = true; e.maxCm = l; }
        }
        if (!e.bones.empty() || e.pelvis) out.push_back(std::move(e));
    };
    if (IsTrickClip()) posed(EndIssue::StartNotGame, 0.f, false);
    if (IsLandingClip() || !landCover_)
        posed(IsTrickClip() ? EndIssue::TrickEndNoLanding : EndIssue::LandingEndNotBack, clip_.duration, IsTrickClip() && LegsReleased());
    return out;
}

void EditorDoc::LockKeysAt(float t, bool skipLegs) {
    for (auto& tr : clip_.tracks) {
        if (!EditableName(tr.bone) || (skipLegs && IsLegBone(tr.bone))) continue;   // locked tracks are never written
        if (tr.keys.empty()) continue;
        PutKey(tr.keys, t, QIdent);
    }
    if (!clip_.pelvis.empty()) { const float z[3] = {0, 0, 0}; PutPel(clip_.pelvis, t, z); }
    // a track left with only game-pose keys on locked instants changes nothing: removed (else deleting every key of
    // a part would never remove it, the locked keys coming back)
    auto onlyLocked = [&](float kt) { return LockedAt(kt); };
    clip_.tracks.erase(std::remove_if(clip_.tracks.begin(), clip_.tracks.end(), [&](const Track& tr) {
        if (!EditableName(tr.bone)) return false;
        for (auto& k : tr.keys) if (!onlyLocked(k.t) || AngleDeg(k.q) > 1e-3f) return false;
        return true;
    }), clip_.tracks.end());
    bool pelOnly = !clip_.pelvis.empty();
    for (auto& k : clip_.pelvis) if (!onlyLocked(k.t) || Len3(k.v) > 1e-4f) pelOnly = false;
    if (pelOnly) clip_.pelvis.clear();
}

void EditorDoc::ApplyEndLock() { ApplyEndLockFrom(EndLocked()); }

void EditorDoc::ApplyEndLockFrom(bool endLockedBefore) {
    if (StartLocked()) LockKeysAt(0.f, false);
    // locked before the change (a pose put on the end is undone) or now (a trick end back within the limits)
    if (EndLockedRule() && (endLockedBefore || EndLocked())) LockKeysAt(clip_.duration, IsTrickClip() && LegsReleased());
}

void EditorDoc::SetDuration(float d) {
    if (!std::isfinite(d)) return;
    d = std::max(0.1f, d);
    const float old = clip_.duration;
    if (std::fabs(d - old) < kKeyEps) return;
    const bool locked = EndLocked();
    if (locked) {   // the locked end key goes with the end (it is the game's pose: dropped, put back below)
        for (auto& tr : clip_.tracks) {
            if (!EditableName(tr.bone)) continue;
            tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const Key& k) { return std::fabs(k.t - old) < kKeyEps && AngleDeg(k.q) < 1e-3f; }), tr.keys.end());
        }
        clip_.pelvis.erase(std::remove_if(clip_.pelvis.begin(), clip_.pelvis.end(), [&](const PosKey& k) { return std::fabs(k.t - old) < kKeyEps && Len3(k.v) < 1e-4f; }), clip_.pelvis.end());
    }
    clip_.duration = d;
    ApplyEndLockFrom(locked);
}
