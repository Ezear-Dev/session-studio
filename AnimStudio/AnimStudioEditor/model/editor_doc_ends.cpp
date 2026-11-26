// AnimStudio Editor -- EditorDoc: the ends of trick and landing clips (2026-11-26, PLAN-EDITOR-3-ADDITIONS tasks 2-3).
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
        if (!hasLandingForAllVariants) endCheck(EndIssue::TrickEndNoLanding, true);   // legs: given back after the catch
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
            if (!IsEditable(b) || leg_[b]) continue;
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
