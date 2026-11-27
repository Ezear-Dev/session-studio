// Session Studio -- EditorDoc: copy / paste of a pose or of keys inside the open clip (2026-11-26).
// The clipboard holds key values (my change), never the game's pose: "Copy the game's pose" keeps that job.
#include "editor_doc.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kKeyEps = 1e-3f;
constexpr float kFrame = 1.f / 30.f;   // the timeline's frame (30 per second, like the game's recordings)
float SnapFrame(float t) { return std::round(t / kFrame) * kFrame; }
}

void EditorDoc::TakeClipboard(EditorDoc& from) {
    if (&from == this || !from.CanPaste()) return;
    clipKind_ = from.clipKind_;
    cbPose_ = from.cbPose_;
    cbKeys_ = from.cbKeys_;
    cbHasPel_ = from.cbHasPel_;
    for (int k = 0; k < 3; k++) cbPel_[k] = from.cbPel_[k];
    from.ClearClipboard();
}

int EditorDoc::CopyPose(float t, int root) {
    const int n = BoneCount();
    if (n == 0 || (root >= n)) return 0;
    if (!std::isfinite(t)) t = 0.f;
    const float tPose = std::clamp(t, 0.f, clip_.duration);
    const float kt = KeyTime(tPose);
    const bool lp = LoopsInTime();
    const int pel = PelvisBone();
    const bool pelOnly = root >= 0 && root == pel;   // the pelvis part in the interface is its offset
    std::vector<bool> in(n, root < 0);
    if (root >= 0) {
        in[root] = true;
        for (int i = root + 1; i < n; i++) if (skel_.parents[i] >= 0 && in[skel_.parents[i]]) in[i] = true;
    }
    std::map<int, Quat> pose;
    if (!pelOnly)
        for (int b = 0; b < n; b++) {
            if (!in[b] || !IsEditable(b)) continue;
            const int ti = TrackIndex(b);
            auto p = pending_.find(b);
            if (p != pending_.end()) pose[b] = KeyFor(b, ti, p->second, tPose);   // a change not keyed yet counts too
            else if (ti >= 0 && !clip_.tracks[ti].keys.empty()) pose[b] = EvalClipTrack(clip_, clip_.tracks[ti], kt, lp);
        }
    bool hasPel = false;
    float pv[3] = {0, 0, 0};
    if ((root < 0 || pelOnly) && pel >= 0) {
        if (pelPending_) { for (int k = 0; k < 3; k++) pv[k] = pel_[k]; hasPel = true; }
        else if (!clip_.pelvis.empty()) { EvalClipPelvis(clip_, kt, lp, pv); hasPel = true; }
    }
    if (pose.empty() && !hasPel) return 0;   // nothing changed there: the clipboard keeps what it had
    ClearClipboard();
    clipKind_ = CB_Pose;
    cbPose_ = std::move(pose);
    cbHasPel_ = hasPel;
    for (int k = 0; k < 3; k++) cbPel_[k] = pv[k];
    return (int)cbPose_.size() + (hasPel ? 1 : 0);
}

int EditorDoc::CopyKeys(const std::vector<KeyRef>& keys) {
    if (keys.empty()) return 0;
    float tmin = keys.front().t;
    for (auto& k : keys) tmin = std::min(tmin, k.t);
    const int pel = PelvisBone();
    std::vector<CbKey> out;
    for (auto& ref : keys) {
        CbKey e;
        e.dt = ref.t - tmin;
        std::vector<int> bones = ref.bones;
        const bool all = bones.empty();
        if (all) for (int b = 0; b < BoneCount(); b++) bones.push_back(b);
        for (int b : bones) {
            if (b == pel || !IsEditable(b)) continue;   // locked tracks are never copied
            const Track* tr = TrackOf(b);
            if (!tr) continue;
            for (auto& k : tr->keys) if (std::fabs(k.t - ref.t) < kKeyEps) { e.bones.push_back({b, k}); break; }
        }
        if (pel >= 0 && (all || std::find(ref.bones.begin(), ref.bones.end(), pel) != ref.bones.end()))
            for (auto& k : clip_.pelvis) if (std::fabs(k.t - ref.t) < kKeyEps) { e.hasPel = true; e.pel = k; break; }
        if (!e.bones.empty() || e.hasPel) out.push_back(std::move(e));
    }
    if (out.empty()) return 0;
    ClearClipboard();
    clipKind_ = CB_Keys;
    cbKeys_ = std::move(out);
    return (int)cbKeys_.size();
}

EditorDoc::PasteResult EditorDoc::Paste(float t) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    PasteResult r;
    if (clipKind_ == CB_None) return r;
    if (!std::isfinite(t)) t = 0.f;
    if (HasPending()) {   // the caller keys or discards it first
        r.refused = clipKind_ == CB_Pose ? (int)cbPose_.size() + (cbHasPel_ ? 1 : 0) : (int)cbKeys_.size();
        return r;
    }
    if (clipKind_ == CB_Pose) {
        // the pose becomes pending changes, keyed like a normal key (CommitKey protects the other keyed instants and
        // keys the start of a part that had no key yet)
        const float tPose = std::clamp(t, 0.f, clip_.duration);
        std::map<int, Quat> pend;
        for (auto& kv : cbPose_) {
            const int b = kv.first;
            if (!IsEditable(b)) { r.refused++; continue; }
            const int ti = TrackIndex(b);
            const float w = ti >= 0 ? clip_.tracks[ti].weight : 1.f;
            const Quat o = OriginFor(b, tPose);
            pend[b] = clip_.mode == Mode::Replace ? QSlerp(o, kv.second, w) : QNorm(QMul(o, QSlerp(QIdent, kv.second, w)));
            r.pasted++;
        }
        const bool pel = cbHasPel_ && PelvisBone() >= 0;
        if (cbHasPel_ && !pel) r.refused++;
        if (pel) r.pasted++;
        if (r.pasted == 0) return r;
        Checkpoint("paste pose");
        pending_ = std::move(pend);
        if (pel) { pelPending_ = true; for (int k = 0; k < 3; k++) pel_[k] = cbPel_[k]; }
        CommitKey(tPose);
        return r;
    }
    // keys
    const State before = SaveState();
    const float t0 = SnapFrame(std::clamp(t, 0.f, clip_.duration));
    std::vector<int> newTracks;
    const bool pelWasEmpty = clip_.pelvis.empty();
    bool pelWritten = false;
    for (auto& e : cbKeys_) {
        float tt = SnapFrame(t0 + e.dt);
        if (tt > clip_.duration + kKeyEps) { r.outOfClip++; continue; }
        tt = KeyTime(std::min(tt, clip_.duration));
        bool any = false;
        for (auto& bk : e.bones) {
            const int b = bk.first;
            if (!IsEditable(b)) continue;
            int ti = TrackIndex(b);
            if (ti < 0) {
                Track tr;
                tr.bone = skel_.names[b];
                tr.index = b;
                clip_.tracks.push_back(tr);
                ti = (int)clip_.tracks.size() - 1;
                newTracks.push_back(b);
            }
            auto& keys = clip_.tracks[ti].keys;
            keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Key& k) { return std::fabs(k.t - tt) < kKeyEps; }), keys.end());
            Key k = bk.second;
            k.t = tt;
            keys.push_back(k);
            std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b2) { return a.t < b2.t; });
            any = true;
        }
        if (e.hasPel && PelvisBone() >= 0) {
            auto& pk = clip_.pelvis;
            pk.erase(std::remove_if(pk.begin(), pk.end(), [&](const PosKey& k) { return std::fabs(k.t - tt) < kKeyEps; }), pk.end());
            PosKey k = e.pel;
            k.t = tt;
            pk.push_back(k);
            std::sort(pk.begin(), pk.end(), [](const PosKey& a, const PosKey& b2) { return a.t < b2.t; });
            pelWritten = true;
            any = true;
        }
        if (any) r.pasted++; else r.refused++;
    }
    // a part keyed for the first time: its start keeps the game's pose (else the first key would apply from the start)
    if (autoStartKey) {
        for (int b : newTracks) {
            const int ti = TrackIndex(b);
            auto& keys = clip_.tracks[ti].keys;
            if (keys.empty() || keys.front().t < kKeyEps) continue;
            keys.insert(keys.begin(), Key{0.f, KeyFor(b, ti, OriginFor(b, 0.f), 0.f), true});
        }
        if (pelWasEmpty && pelWritten && clip_.pelvis.front().t > kKeyEps)
            clip_.pelvis.insert(clip_.pelvis.begin(), PosKey{0.f, {0, 0, 0}, true});
    }
    if (r.pasted > 0) CheckpointState(before, "paste keys");
    return r;
}
