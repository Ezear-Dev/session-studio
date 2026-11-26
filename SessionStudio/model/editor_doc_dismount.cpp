// Session Studio -- EditorDoc: dismount part of a riding clip (2026-11-25, user: "stepping off the board jumps").
// The riding clip's timeline goes on after its loop with the game's own dismount (figure Dismount_*), so the user can
// animate the step off in the same clip. The player plays that part once when the feet leave the board.
#include "editor_doc.h"
#include <algorithm>
#include <cmath>

namespace { constexpr float kEps = 1e-3f; }

bool EditorDoc::LoadDismountFigure(const std::string& json, std::string& err) {
    FigureRec r;
    if (!ParseFigure(json, r, err)) return false;
    if (HasSkeleton() && r.bones != BoneCount()) { err = "dismount recorded with another skeleton: record it again in game"; return false; }
    dis_ = std::move(r);
    return true;
}

Quat EditorDoc::OriginDismount(int bone, float u) const {
    const int n = (int)dis_.times.size();
    if (!HasDismountFigure() || bone < 0 || bone >= dis_.bones) {   // not recorded: the loop's start pose
        return HasFigure() ? Origin(bone, FigTime(0.f)) : QIdent;
    }
    const float end = dis_.times.back();
    if (!std::isfinite(u) || u < 0.f) u = 0.f;
    if (u > end) u = end;
    int i = 0;
    while (i + 1 < n && dis_.times[i + 1] <= u) i++;
    if (i + 1 >= n) return QNorm(dis_.frames[(size_t)i * dis_.bones + bone]);
    const float span = dis_.times[i + 1] - dis_.times[i];
    const float a = span > 1e-6f ? (u - dis_.times[i]) / span : 0.f;
    return QSlerp(QNorm(dis_.frames[(size_t)i * dis_.bones + bone]), QNorm(dis_.frames[(size_t)(i + 1) * dis_.bones + bone]), a);
}

bool EditorDoc::DismountPos(int bone, float u, float out[3]) const {
    if (!HasDismountFigure() || dis_.posBones.empty()) return false;
    int k = -1;
    for (size_t i = 0; i < dis_.posBones.size(); i++) if (dis_.posBones[i] == bone) { k = (int)i; break; }
    const int n = (int)dis_.times.size();
    const size_t nb = dis_.posBones.size();
    if (k < 0 || n == 0 || dis_.positions.size() != (size_t)n * nb * 3) return false;
    u = std::clamp(std::isfinite(u) ? u : 0.f, 0.f, dis_.times.back());
    int i = 0;
    while (i + 1 < n && dis_.times[i + 1] <= u) i++;
    const int j = i + 1 < n ? i + 1 : i;
    const float span = dis_.times[j] - dis_.times[i];
    const float a = span > 1e-6f ? (u - dis_.times[i]) / span : 0.f;
    for (int d = 0; d < 3; d++) {
        const float p0 = dis_.positions[((size_t)i * nb + k) * 3 + d], p1 = dis_.positions[((size_t)j * nb + k) * 3 + d];
        out[d] = p0 + (p1 - p0) * a;
    }
    return true;
}

bool EditorDoc::SetDismountPart(bool on, float len) {
    if (!CanDismount()) return false;
    const float loopLen = LoopLength(clip_);
    if (!on) {
        if (!HasDismount()) return true;
        for (auto& tr : clip_.tracks)
            tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const Key& k) { return k.t >= loopLen - kEps; }), tr.keys.end());
        clip_.tracks.erase(std::remove_if(clip_.tracks.begin(), clip_.tracks.end(), [](const Track& t) { return t.keys.empty(); }), clip_.tracks.end());
        clip_.pelvis.erase(std::remove_if(clip_.pelvis.begin(), clip_.pelvis.end(), [&](const PosKey& k) { return k.t >= loopLen - kEps; }), clip_.pelvis.end());
        clip_.duration = loopLen;
        clip_.dismount = 0.f;
        return true;
    }
    if (!HasDismountFigure()) return false;
    const float l = std::clamp(len, 0.2f, std::max(0.2f, DismountFigureDuration()));
    if (HasDismount()) {   // new length: keys past the new end go, the end keys move with it
        const float oldEnd = clip_.duration, newEnd = loopLen + l;
        for (auto& tr : clip_.tracks) {
            for (auto& k : tr.keys) if (std::fabs(k.t - oldEnd) < kEps) k.t = newEnd;
            tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const Key& k) { return k.t > newEnd + kEps; }), tr.keys.end());
        }
        for (auto& k : clip_.pelvis) if (std::fabs(k.t - oldEnd) < kEps) k.t = newEnd;
        clip_.pelvis.erase(std::remove_if(clip_.pelvis.begin(), clip_.pelvis.end(), [&](const PosKey& k) { return k.t > newEnd + kEps; }), clip_.pelvis.end());
        clip_.duration = newEnd;
        clip_.dismount = l;
        return true;
    }
    // start keys: the loop's pose at 0 (the same instant as its end); then the part exists
    struct Seed { size_t track; Quat q; bool smooth; };
    std::vector<Seed> starts;
    const bool lp = LoopsInTime();
    for (size_t i = 0; i < clip_.tracks.size(); i++)
        if (!clip_.tracks[i].keys.empty()) starts.push_back(Seed{i, EvalClipTrack(clip_, clip_.tracks[i], 0.f, lp), clip_.tracks[i].keys.front().smooth});
    float pel0[3] = {0, 0, 0};
    const bool hasPel = !clip_.pelvis.empty();
    if (hasPel) EvalClipPelvis(clip_, 0.f, lp, pel0);
    clip_.duration = loopLen + l;
    clip_.dismount = l;
    const float end = clip_.duration;
    for (const Seed& s : starts) {
        Track& tr = clip_.tracks[s.track];
        int bone = tr.index;
        if (bone < 0) for (int b = 0; b < BoneCount(); b++) if (skel_.names[b] == tr.bone) bone = b;
        tr.keys.push_back(Key{loopLen, s.q, true});
        // end: the game's own pose (additive: no change; replace: the recorded dismount pose)
        const Quat o = bone >= 0 ? OriginFor(bone, end) : QIdent;
        tr.keys.push_back(Key{end, clip_.mode == Mode::Additive ? QIdent : o, true});
        std::sort(tr.keys.begin(), tr.keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
    }
    if (hasPel) {
        clip_.pelvis.push_back(PosKey{loopLen, {pel0[0], pel0[1], pel0[2]}, true});
        clip_.pelvis.push_back(PosKey{end, {0, 0, 0}, true});
        std::sort(clip_.pelvis.begin(), clip_.pelvis.end(), [](const PosKey& a, const PosKey& b) { return a.t < b.t; });
    }
    return true;
}
