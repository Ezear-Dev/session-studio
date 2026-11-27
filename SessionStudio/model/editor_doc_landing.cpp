// Session Studio -- landing sync (2026-11-25, LANDING-SYNC-design.md): markers of a trick clip, landing clips of
// one trick edited over that trick's recorded ground tail, split of the old lot 5 landing part.
#include "editor_doc.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kEps = 1e-3f;
bool IsIdent(const Quat& q) { return std::fabs(std::fabs(q.w) - 1.f) < 1e-5f; }

// keys of one track cut at `at`: before = [0, at] (a key added at `at` if the track goes on after it), after = [at, end]
// shifted to start at 0 (a key added at 0 if the first one comes later)
void CutTrack(const Track& tr, float at, float dur, Track& before, Track& after) {
    before = tr; before.keys.clear();
    after = tr; after.keys.clear();
    for (const Key& k : tr.keys) {
        if (k.t <= at + kEps) before.keys.push_back(k);
        if (k.t >= at - kEps) { Key s = k; s.t = std::max(0.f, k.t - at); after.keys.push_back(s); }
    }
    const bool goesOn = !tr.keys.empty() && tr.keys.back().t > at + kEps;
    if (goesOn && !before.keys.empty() && before.keys.back().t < at - kEps) {
        Key k = before.keys.back(); k.t = at; k.q = EvalTrack(tr, at, dur, false); before.keys.push_back(k);
    }
    if (!after.keys.empty() && after.keys.front().t > kEps) {
        Key k = after.keys.front(); k.t = 0.f; k.q = EvalTrack(tr, at, dur, false); after.keys.insert(after.keys.begin(), k);
    }
}
void CutPelvis(const std::vector<PosKey>& p, float at, float dur, std::vector<PosKey>& before, std::vector<PosKey>& after) {
    before.clear(); after.clear();
    for (const PosKey& k : p) {
        if (k.t <= at + kEps) before.push_back(k);
        if (k.t >= at - kEps) { PosKey s = k; s.t = std::max(0.f, k.t - at); after.push_back(s); }
    }
    const bool goesOn = !p.empty() && p.back().t > at + kEps;
    if (goesOn && !before.empty() && before.back().t < at - kEps) { PosKey k = before.back(); k.t = at; EvalPelvis(p, at, dur, false, k.v); before.push_back(k); }
    if (!after.empty() && after.front().t > kEps) { PosKey k = after.front(); k.t = 0.f; EvalPelvis(p, at, dur, false, k.v); after.insert(after.begin(), k); }
}
} // namespace

bool GroundTailFigure(const FigureRec& trick, FigureRec& out) {
    const size_t n = std::min(trick.phases.size(), trick.times.size());
    size_t g = n;
    for (size_t i = 0; i < n; i++) if (trick.phases[i] == FigureRecorder::kGroundPhase) { g = i; break; }
    if (g >= n || trick.times.size() - g < 2 || trick.bones <= 0) return false;
    FigureRec r;
    r.figure = trick.figure;
    r.version = trick.version;
    r.fps = trick.fps;
    r.bones = trick.bones;
    const float t0 = trick.times[g];
    for (size_t i = g; i < trick.times.size(); i++) r.times.push_back(trick.times[i] - t0);
    if (trick.ratios.size() == trick.times.size()) r.ratios.assign(trick.ratios.begin() + g, trick.ratios.end());
    r.frames.assign(trick.frames.begin() + (size_t)g * trick.bones, trick.frames.end());
    r.posBones = trick.posBones;
    const size_t nb = trick.posBones.size();
    if (nb && trick.positions.size() == trick.times.size() * nb * 3) r.positions.assign(trick.positions.begin() + g * nb * 3, trick.positions.end());
    if (trick.board.size() == trick.times.size()) r.board.assign(trick.board.begin() + g, trick.board.end());   // 2026-11-27
    r.boardFaced = trick.boardFaced;
    out = std::move(r);
    return true;
}

bool SplitLandingPart(const Clip& trick, Clip& air, Clip& land) {
    if (!(trick.states & ST_Trick) || trick.landing <= 0.f) return false;
    const float g = trick.groundAt > 0.f ? trick.groundAt : std::max(0.f, trick.duration - trick.landing);
    air = trick;
    air.landing = air.groundAt = 0.f;
    air.tracks.clear();
    land = Clip{};
    land.name = trick.name + "_reception";
    land.enabled = trick.enabled;
    land.states = ST_Landing;
    land.trick = trick.trick;
    land.trickHash = TrickHash(trick.trick);
    land.stances = trick.stances;
    land.feet = trick.feet;
    land.mirror = trick.mirror;
    land.priority = trick.priority;
    land.mode = trick.mode;
    land.loop = false;
    land.blendIn = 0.12f; land.blendOut = 0.30f;
    land.duration = std::max(0.1f, trick.duration - g);
    for (const Track& tr : trick.tracks) {
        Track b, a;
        CutTrack(tr, g, trick.duration, b, a);
        if (!b.keys.empty()) air.tracks.push_back(std::move(b));
        if (!a.keys.empty()) land.tracks.push_back(std::move(a));
    }
    CutPelvis(trick.pelvis, g, trick.duration, air.pelvis, land.pelvis);
    air.duration = std::max(0.1f, g);
    if (air.markers.ok && air.markers.ground > air.duration) air.markers.ground = air.duration;
    if (air.markers.ok) {
        air.markers.land = std::min(air.markers.land, air.markers.ground);
        air.markers.catchT = std::min(air.markers.catchT, air.markers.land);
        air.markers.air = std::min(air.markers.air, air.markers.catchT);
    }
    return true;
}

bool ConvertForLandingSync(const Clip& in, const TrickMarkers& m0, Clip& air, Clip& land, bool& hasLand) {
    hasLand = false;
    if (!(in.states & ST_Trick)) return false;
    Clip work = in;
    TrickMarkers m = m0;
    if (work.landing <= 0.f && m.ok && work.duration > m.ground + 1.f / 60.f) {   // keys past the ground: they become the landing
        bool after = false;
        for (auto& tr : work.tracks) for (auto& k : tr.keys) if (k.t > m.ground + kEps) after = true;
        for (auto& k : work.pelvis) if (k.t > m.ground + kEps) after = true;
        if (after) { work.groundAt = m.ground; work.landing = work.duration - m.ground; }
    }
    if (work.landing > 0.f) {
        if (!SplitLandingPart(work, air, land)) return false;
        hasLand = !land.tracks.empty() || !land.pelvis.empty();
    } else air = work;
    if (m.ok) {
        if (m.ground > air.duration) m.ground = air.duration;
        m.land = std::min(m.land, m.ground); m.catchT = std::min(m.catchT, m.land); m.air = std::min(m.air, m.catchT);
        air.markers = m;
    }
    const bool sameMarkers = in.markers.ok == air.markers.ok && (!air.markers.ok ||
        (std::fabs(in.markers.air - air.markers.air) < kEps && std::fabs(in.markers.catchT - air.markers.catchT) < kEps &&
         std::fabs(in.markers.land - air.markers.land) < kEps && std::fabs(in.markers.ground - air.markers.ground) < kEps));
    return !(sameMarkers && in.landing <= 0.f && !hasLand && std::fabs(in.duration - air.duration) < kEps);
}

TrickMarkers EditorDoc::Markers() const {
    if (IsTrickClip() && HasFigure()) {
        const TrickMarkers m = MarkersFromPhases(fig_.phases, fig_.times);
        if (m.ok) return m;
    }
    return clip_.markers;
}

int EditorDoc::SeedLandingFromTrick(const Clip& trick, bool keepKeys) {
    LockGuard lockGuard{*this};   // 2026-11-26: locked start and end, after the change
    if (!IsLandingClip() || !(trick.states & ST_Trick)) return 0;
    if (keepKeys && (!clip_.tracks.empty() || !clip_.pelvis.empty())) return 0;
    float end = trick.markers.ok ? trick.markers.ground : trick.duration;
    if (end > trick.duration) end = trick.duration;
    const float dur = clip_.duration;
    clip_.tracks.clear();
    clip_.pelvis.clear();
    clip_.mode = trick.mode;
    int count = 0;
    for (const Track& tr : trick.tracks) {
        if (!EditableName(tr.bone)) continue;
        const Quat q0 = EvalClipTrack(trick, tr, end, false);
        if (trick.mode == Mode::Additive && IsIdent(q0)) continue;   // nothing to carry over
        int idx = -1;
        for (int b = 0; b < BoneCount(); b++) if (skel_.names[b] == tr.bone) { idx = b; break; }
        Track nt;
        nt.bone = tr.bone;
        nt.index = idx;
        nt.weight = tr.weight;
        nt.keys.push_back(Key{0.f, q0, true});
        if (trick.mode == Mode::Additive) nt.keys.push_back(Key{dur, QIdent, true});   // back to the game's pose
        else if (HasFigure() && idx >= 0) nt.keys.push_back(Key{dur, Origin(idx, dur), true});
        count += (int)nt.keys.size();
        clip_.tracks.push_back(std::move(nt));
    }
    if (!trick.pelvis.empty()) {
        PosKey k0{};
        EvalPelvis(trick.pelvis, end, trick.duration, false, k0.v);
        k0.t = 0.f; k0.smooth = true;
        if (std::fabs(k0.v[0]) + std::fabs(k0.v[1]) + std::fabs(k0.v[2]) > 1e-3f) {
            PosKey k1{dur, {0.f, 0.f, 0.f}, true};
            clip_.pelvis.push_back(k0);
            clip_.pelvis.push_back(k1);
            count += 2;
        }
    }
    return count;
}
