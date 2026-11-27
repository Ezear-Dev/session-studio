#include "player.h"
#include <cmath>
#include <cstring>

namespace {
constexpr float kMasterFade = 0.25f;
constexpr float kFrozenFade = 0.30f;
constexpr float kLegFade = 0.12f;   // leg weight ramp down after the catch
constexpr float kRestartBlend = 0.15f;   // minimum fade in when a new pop restarts a running trick

float Rate(float seconds) { return seconds > 1e-4f ? 1.f / seconds : 1e9f; }
float Toward(float v, float target, float step) {
    if (v < target) return (v + step > target) ? target : v + step;
    if (v > target) return (v - step < target) ? target : v - step;
    return v;
}
} // namespace

void Player::SetRig(const int* parent, int n, MirrorMap map) {
    if (n < 0) n = 0;
    if (n > kMaxBones) n = kMaxBones;
    n_ = n;
    for (int i = 0; i < n; i++) parent_[i] = parent[i];
    map_ = std::move(map);
    conv_ = map_.conv;
    nLayers_ = 0;      // bone indices change: start from scratch
    haveGen_ = false;
    std::memset(leg_, 0, sizeof(leg_));
    for (int& r : legRoot_) r = -1;
    for (int& r : chainMid_) r = -1;
    for (int& r : chainEnd_) r = -1;
    antiT_ = 0.f;
    haveRef_ = false;
    rig_ = PelvisRig{};
    postReady_ = false;
    legW_ = 1.f;
    havePop_ = false;
    trickDone_ = false;
    pend_ = nullptr;
    pendT_ = gapT_ = 0.f;
    prevKick_ = false;
    lastRide_[0] = 0;
    prevDis_ = disOn_ = false;
}

void Player::SetLegBones(const std::vector<bool>& isLeg) {
    for (int i = 0; i < n_; i++) {
        leg_[i] = i < (int)isLeg.size() && isLeg[i];
        const int p = parent_[i];
        legRoot_[i] = !leg_[i] ? -1 : (p >= 0 && leg_[p]) ? legRoot_[p] : i;   // parent before child
        chainMid_[i] = chainEnd_[i] = -1;
    }
    for (int i = 0; i < n_; i++) {   // thigh -> calf -> foot (first leg child at each step)
        const int p = parent_[i];
        if (!leg_[i] || p < 0 || !leg_[p]) continue;
        const int r = legRoot_[i];
        if (p == r && chainMid_[r] < 0) chainMid_[r] = i;
        else if (p == chainMid_[r] && chainEnd_[r] < 0) chainEnd_[r] = i;
    }
}

void Player::Freeze(Layer& L) {   // the layer keeps its last evaluated keys and fades out
    L.clip = nullptr;
    L.target = 0.f;
    L.rate = Rate(kFrozenFade);
}

int Player::FindLiveLayer(const Clip* c, bool mirrored, bool dis) const {
    for (int i = 0; i < nLayers_; i++) if (layers_[i].clip == c && layers_[i].mirrored == mirrored && layers_[i].dis == dis) return i;
    return -1;
}

void Player::Rebind(const ClipSet* set) {
    conv_ = set ? EffectiveConv(map_, set->convOverride) : map_.conv;
    for (int i = 0; i < nLayers_; i++) {
        Layer& L = layers_[i];
        if (!L.clip) continue;   // already frozen
        const Clip* found = nullptr;
        if (set) for (auto& c : set->clips) if (c.file == L.file) { found = &c; break; }
        const bool changed = !found || found->stamp != L.stamp || (L.mirrored && set->mirrorStamp != L.mirrorStamp);
        if (changed) Freeze(L);   // content changed: fade from the old version to the new one (started by Apply)
        else L.clip = found;
    }
}

void Player::StartLayer(const Clip* c, bool mirrored, uint64_t mirrorStamp, bool keepPhase, bool dis) {
    if (dis) keepPhase = false;   // the dismount part starts at its beginning
    float clock = 0.f;   // if a frozen version of the same file exists, keep its phase (except at the pop: clock 0)
    float tPlay = 0.f;
    TrickClock tc{};
    bool grounded = false;
    if (keepPhase) for (int i = 0; i < nLayers_; i++) if (!layers_[i].clip && std::strcmp(layers_[i].file, c->file.c_str()) == 0) {
        clock = layers_[i].clock;
        tPlay = layers_[i].tPlay;
        tc = layers_[i].tc;
        grounded = layers_[i].grounded;
    }
    int slot = nLayers_;
    if (nLayers_ >= kMaxLayers) {   // full: replace the weakest layer
        slot = 0;
        for (int i = 1; i < nLayers_; i++) if (layers_[i].w < layers_[slot].w) slot = i;
        for (int i = slot; i + 1 < nLayers_; i++) layers_[i] = layers_[i + 1];
        slot = nLayers_ - 1;
    } else nLayers_++;
    Layer& L = layers_[slot];
    strncpy_s(L.file, c->file.c_str(), _TRUNCATE);
    strncpy_s(L.name, c->name.c_str(), _TRUNCATE);
    L.clip = c;
    L.stamp = c->stamp;
    L.mirrorStamp = mirrorStamp;
    L.mirrored = mirrored;
    L.clock = clock; L.w = 0.f; L.target = 1.f; L.rate = Rate(c->blendIn);
    L.trick = (c->states & ST_Trick) != 0;
    L.dis = dis;
    L.tPlay = L.trick ? (keepPhase ? tPlay : 0.f) : 0.f;
    L.tc = L.trick && keepPhase ? tc : TrickClock{};
    L.grounded = L.trick && keepPhase && grounded;
    L.hold = L.trick && !keepPhase;   // clock 0 at the pop; a resumed phase is not held
    L.legW = c->legsAfterCatch ? 1.f : legW_;
    L.nKeys = 0;
    L.hasPel = false;
    L.pel[0] = L.pel[1] = L.pel[2] = 0.f;
    L.mode = c->mode;
    L.netId = c->netId; L.netHash = c->netHash;
    L.tEval = 0.f; L.netT = L.netW = L.netSpeed = L.netWSpeed = 0.f; L.netHave = false;
    L.tFixed = L.wFixed = L.legFixed = -1.f;
}

void Player::EvalLayer(Layer& L, const GameState& gs) {
    const Clip* c = L.clip;
    if (!c) return;   // frozen
    // trick clip: clock from the pop, never loops
    // Cycle: a clip written for the other body side has its left foot on our right: half a stride later
    const float ph = cyc_.Phase() + (L.mirrored ? 0.5f : 0.f);
    float t = L.trick ? (L.tPlay < c->duration ? L.tPlay : c->duration) : ClipTime(*c, L.clock, gs.manualRatio, ph);
    bool loop = !L.trick && !c->hold && ((c->loop && c->drive == Drive::Time) || c->drive == Drive::Cycle);
    if (L.dis) {   // 2026-11-25: dismount part, once
        const float le = LoopLength(*c);
        t = le + (L.clock < c->dismount ? L.clock : c->dismount);
        loop = false;
    }   // looping only concerns the time drive
    if (L.tFixed >= 0.f) t = L.tFixed;   // 2026-11-26: remote layer, time given by the peer
    L.tEval = t;
    L.mode = c->mode;
    if (L.trick) L.legW = c->legsAfterCatch ? 1.f : legW_;   // v0.28: a clip may keep its legs after the catch
    L.nKeys = 0;
    for (auto& tr : c->tracks) {
        if (tr.index < 0 || tr.index >= n_ || L.nKeys >= kMaxBones) continue;
        Quat k = EvalClipTrack(*c, tr, t, loop);
        int b = tr.index;
        if (L.mirrored && b < (int)map_.other.size() && b < (int)conv_.size()) {
            const int p = parent_[b];
            const int cb = conv_[b];
            const int cp = (c->mode == Mode::Replace && p >= 0) ? conv_[p] : cb;
            k = MirrorKey(k, cp, cb);
            b = map_.other[b];
        }
        L.bone[L.nKeys] = b;
        L.boneW[L.nKeys] = tr.weight;
        L.key[L.nKeys] = k;
        L.nKeys++;
    }
    L.hasPel = !c->pelvis.empty();   // P6 lot 2
    if (L.hasPel) EvalClipPelvis(*c, t, loop, L.pel);
}

int Player::Apply(FTr* cs, int n, const ClipSet* set, const GameState& gs, float dt) {
    postReady_ = false;   // P6: PostApply only ever replays this frame's legs
    if (!cs || n != n_ || n_ <= 0) return 0;
    if (!std::isfinite(dt) || dt < 0.f || dt > 0.1f) dt = 0.f;   // pause, loading, alt-tab: no jump

    ToLocal(cs, parent_, n_, game_, localP_);
    // 2026-11-25: the game's step cycle, from its own feet (before our layer)
    if (rig_.Ok() && rig_.foot[0] < n && rig_.foot[1] < n)
        cyc_.Step(cs[rig_.foot[0]].px, cs[rig_.foot[0]].py, cs[rig_.foot[1]].px, cs[rig_.foot[1]].py, dt);

    if (set && (!haveGen_ || set->generation != gen_)) { Rebind(set); gen_ = set->generation; haveGen_ = true; }
    if (!set && haveGen_) { Rebind(nullptr); haveGen_ = false; }

    // pop: the first serial seen only counts if a trick is on
    bool newPop = false, restart = false;
    if (!havePop_) { havePop_ = true; newPop = gs.kickflip; }
    else newPop = gs.popSerial != pop_;
    pop_ = gs.popSerial;
    if (newPop) {
        trickLink_ = 0;
        legW_ = 1.f;
        trickDone_ = false;
        for (int i = 0; i < nLayers_; i++) {   // live trick layers leave frozen: the new one starts from 0
            Layer& L = layers_[i];
            if (!L.clip || !L.trick) continue;
            if (L.target > 0.f) restart = true;   // a trick was running
            const float out = L.clip->blendOut;
            Freeze(L);
            L.rate = Rate(out);
        }
    }
    // the trick ends (back on the ground): its live layers know it (the clip runs to its ground marker, the landing
    // clip of the trick cross-fades in)
    const bool groundNow = prevKick_ && !gs.kickflip && !newPop;
    prevKick_ = gs.kickflip;
    if (groundNow) for (int i = 0; i < nLayers_; i++) {
        Layer& L = layers_[i];
        if (L.clip && L.trick) L.grounded = true;
    }
    if (gs.trickPhase >= 3) legW_ = Toward(legW_, 0.f, dt / kLegFade);   // catch or later
    // lot 5: a trick clip that ends before the ground holds its last pose until the ground (no fade to the game pose
    // in mid-air, then a fade into the ground clip: the "double fade"); trickDone_ is kept for the logs only
    for (int i = 0; i < nLayers_; i++) {
        const Layer& L = layers_[i];
        if (L.clip && L.trick && L.target > 0.f && L.tPlay >= L.clip->duration) trickDone_ = true;
    }

    // winning clip; during a trick, a clip of that trick comes before the others (e.g. pop from a manual)
    const Clip* win = Choose(set, gs, gs.kickflip);
    // (a clip for "every trick" has no landing of its own: it never changes which landing plays)
    if (gs.kickflip && win && (win->states & ST_Trick) && win->trickHash) trickLink_ = LinkHash(win->link);
    // 2026-11-25: dismount part of the riding clip, played once from the moment the feet leave the board
    bool wantDis = false;
    {
        if (gs.riding && win && (win->states & ST_Riding)) strncpy_s(lastRide_, win->file.c_str(), _TRUNCATE);
        if (gs.dismount && !prevDis_ && !gs.kickflip) disOn_ = true;
        if (!gs.dismount || gs.kickflip) disOn_ = false;
        prevDis_ = gs.dismount;
        if (disOn_) {
            const Clip* rc = nullptr;
            if (set && lastRide_[0]) for (auto& c : set->clips) if (c.file == lastRide_) { rc = &c; break; }
            if (rc && rc->enabled && rc->dismount > 0.f) {
                const bool mir = rc->mirror && (AuthorMirrored(*rc) != (gs.goofy != gs.sw));
                const int k = FindLiveLayer(rc, mir, true);
                if (k >= 0 && layers_[k].clock >= rc->dismount) disOn_ = false;   // played to its end
                else { win = rc; wantDis = true; }
            } else disOn_ = false;
        }
    }
    // mirror: when the body the clip was written for (regular / goofy) is not the skater's body right now
    bool wantMirror = win && win->mirror && (AuthorMirrored(*win) != (gs.goofy != gs.sw));
    int cur = -1;
    for (int i = 0; i < nLayers_; i++) if (layers_[i].target > 0.f) cur = i;
    // lot 5: between two base-pose clips (no trick on either side), a new winner must hold kDebounce s, and a gap
    // shorter than kGap keeps the current clip (a push, a flicker of the state). Never for the dismount part.
    {
        const Layer* C = cur >= 0 ? &layers_[cur] : nullptr;
        const bool curBase = C && C->clip && !C->trick;
        const bool winBase = !win || !(win->states & ST_Trick);
        const bool differs = !C || C->clip != win || C->dis != wantDis || (win && C->mirrored != wantMirror);
        if (curBase && winBase && differs && !newPop && !wantDis) {
            bool keep = false;
            if (!win) { gapT_ += dt; keep = gapT_ < kGap; pend_ = nullptr; pendT_ = 0.f; }
            else {
                gapT_ = 0.f;
                if (pend_ != win) { pend_ = win; pendT_ = 0.f; }
                pendT_ += dt;
                keep = pendT_ < kDebounce;
            }
            if (keep) { win = C->clip; wantMirror = C->mirrored; wantDis = C->dis; }
        } else { pend_ = nullptr; pendT_ = gapT_ = 0.f; }
    }
    const bool same = (cur >= 0 && win && layers_[cur].clip == win && layers_[cur].mirrored == wantMirror && layers_[cur].dis == wantDis);
    if (!same) {
        // lot 5: one clip replaced by another: both fade over the same time (weights sum to 1: no dip toward the game
        // pose); to nothing, the clip's own blend out.
        // 2026-11-25: between base poses (no trick on either side) a fade lasts at least gap / kFadeSpeed
        const bool outBase = cur >= 0 && !layers_[cur].trick;
        const bool inBase = !win || !(win->states & ST_Trick);
        float minFade = 0.f;
        if ((cur < 0 || outBase) && inBase && (cur >= 0 || win)) {
            gapLayer_.clip = win;
            if (win) {
                gapLayer_.mirrored = wantMirror; gapLayer_.dis = wantDis; gapLayer_.trick = false; gapLayer_.mode = win->mode;
                const int k = FindLiveLayer(win, wantMirror, wantDis);
                gapLayer_.clock = k >= 0 ? layers_[k].clock : 0.f;
                gapLayer_.tPlay = 0.f; gapLayer_.legW = 1.f;
                EvalLayer(gapLayer_, gs);
            }
            const float gap = PoseGap(cur >= 0 ? &layers_[cur] : nullptr, win ? &gapLayer_ : nullptr);
            minFade = gap / kFadeSpeed;
            if (minFade > kFadeMax) minFade = kFadeMax;
        }
        float cross = win ? (win->blendIn > kMinCross ? win->blendIn : kMinCross) : 0.f;
        if (cross < minFade) cross = minFade;
        if (cur >= 0) {
            Layer& L = layers_[cur];
            if (L.clip && L.clip == win && L.dis == wantDis) Freeze(L);   // same clip, other stance: the old version leaves frozen
            else {
                float out = win ? cross : (L.clip ? L.clip->blendOut : kFrozenFade);
                if (!win && out < minFade) out = minFade;
                L.target = 0.f; L.rate = Rate(out);
            }
        }
        if (win) {
            float in = cur >= 0 ? cross : win->blendIn;
            if (in < minFade) in = minFade;
            const int k = FindLiveLayer(win, wantMirror, wantDis);
            if (k >= 0) { layers_[k].target = 1.f; layers_[k].rate = Rate(in); }
            else {
                StartLayer(win, wantMirror, set ? set->mirrorStamp : 0, !newPop, wantDis);
                layers_[nLayers_ - 1].rate = Rate(restart && in < kRestartBlend ? kRestartBlend : in);
            }
        }
    }

    // fades and clocks
    master_ = Toward(master_, masterTarget_, dt * Rate(kMasterFade));
    for (int i = 0; i < nLayers_;) {
        Layer& L = layers_[i];
        L.w = Toward(L.w, L.target, dt * L.rate);
        const float ldt = L.hold ? 0.f : dt;
        if (L.hold) L.hold = false;
        else L.clock += dt;
        if (L.trick && L.clip)   // 2026-11-25: clip time warped onto the live jump (trick_sync.h)
            L.tPlay = L.tc.Step(L.clip->markers, L.clip->duration, gs.kickflip ? gs.trickPhase : 0, L.grounded || !gs.kickflip,
                                gs.kickflip ? gs.predGround : -1.f, ldt, gs.landLead);
        if (L.w <= 0.f && L.target <= 0.f) {
            for (int j = i; j + 1 < nLayers_; j++) layers_[j] = layers_[j + 1];
            nLayers_--;
            continue;
        }
        i++;
    }
    return BlendLayers(cs, set, gs, dt, newPop, false);
}

// 2026-11-26: the blend of the layers into the pose (was the end of Apply), shared with ApplyRemote
int Player::BlendLayers(FTr* cs, const ClipSet* set, const GameState& gs, float dt, bool newPop, bool remote) {
    const float mw = remote ? 1.f : EaseSine(master_);
    std::memcpy(local_, game_, sizeof(Quat) * n_);
    std::memset(legWrote_, 0, sizeof(float) * n_);
    // P6: after our legs are given back to the game, the anti-stretch keeps running a little (ArmPost)
    auto armPost = [&](int ret) {
        bool wrote = false;
        for (int i = 0; i < n_; i++) if (legWrote_[i] > 0.f) { wrote = true; break; }
        if (wrote) antiT_ = kAntiHold;
        else antiT_ = antiT_ > dt ? antiT_ - dt : 0.f;
        if (newPop && !wrote) antiT_ = 0.f;
        postReady_ = (!set || set->legPostPass) && (wrote || antiT_ > 0.f) && (remote || master_ > 0.f);
        return ret;
    };
    if (nLayers_ == 0 || mw <= 0.f) return armPost(0);

    // blending, from the oldest layer to the newest
    std::memset(dirty_, 0, sizeof(bool) * n_);
    bool any = false;
    // P6 lot 2: pelvis offset of the layers (component space); a mirrored clip flips it along the hip axis
    float pelOff[3] = {0.f, 0.f, 0.f};
    bool anyPel = false;
    float hip[3] = {0.f, 1.f, 0.f};
    const bool haveHip = rig_.Ok() && HipAxis(cs, rig_, hip);
    for (int i = 0; i < nLayers_; i++) {
        Layer& L = layers_[i];
        EvalLayer(L, gs);
        const float lw = L.wFixed >= 0.f ? L.wFixed : EaseSine(L.w) * mw;
        if (lw <= 0.f) continue;
        if (L.hasPel && rig_.Ok()) {
            float v[3] = {L.pel[0], L.pel[1], L.pel[2]};
            if (L.mirrored && haveHip) MirrorOffset(hip, v);
            for (int k = 0; k < 3; k++) pelOff[k] += v[k] * lw;
            anyPel = true;
        }
        const float legMul = !L.trick ? 1.f : (L.legFixed >= 0.f ? L.legFixed : EaseSine(L.legW));
        for (int k = 0; k < L.nKeys; k++) {
            const int b = L.bone[k];   // index after mirroring: the mask follows the bone actually written
            const float w = lw * L.boneW[k] * (leg_[b] ? legMul : 1.f);
            if (w <= 0.f) continue;
            if (L.mode == Mode::Additive) local_[b] = QNorm(QMul(local_[b], QSlerp(QIdent, L.key[k], w)));
            else local_[b] = QSlerp(local_[b], L.key[k], w);
            dirty_[b] = true;
            any = true;
            if (leg_[b] && legRoot_[b] >= 0) {   // leg chain weight: 1 - product of (1 - w)
                float& lw2 = legWrote_[legRoot_[b]];
                lw2 = 1.f - (1.f - lw2) * (1.f - (w > 1.f ? 1.f : w));
            }
        }
    }
    if (!any && !anyPel) return armPost(0);
    armPost(0);
    // P6: the game stretches the shin in the air (foot moved along the bone, up to +6 cm). Kept under our own leg pose,
    // that stretch shows as a thin, pulled ankle. A leg our clips write gets its own bone lengths (as in the editor).
    std::memcpy(outP_, localP_, sizeof(float) * 3 * n_);
    for (int b = 0; b < n_; b++) {
        if (!haveRef_ || !leg_[b] || legRoot_[b] < 0 || legRoot_[b] == b) continue;   // the thigh keeps its place at the hip
        const float w = legWrote_[legRoot_[b]];
        if (w <= 0.f) continue;
        for (int k = 0; k < 3; k++) outP_[b][k] += (refP_[b][k] - outP_[b][k]) * w;
        dirty_[b] = true;
    }
    int count = 0;
    for (int i = 0; i < n_; i++) {
        if (!dirty_[i] && parent_[i] >= 0 && dirty_[parent_[i]]) dirty_[i] = true;
        if (dirty_[i]) count++;
    }
    ToComponent(cs, parent_, n_, local_, outP_, dirty_);
    if (anyPel && ApplyPelvisOffset(cs, n_, rig_, pelOff)) {
        // our locals follow (PostApply replays the legs from them)
        const int bones[7] = {rig_.pelvis, rig_.thigh[0], rig_.calf[0], rig_.foot[0], rig_.thigh[1], rig_.calf[1], rig_.foot[1]};
        for (int b : bones) {
            const int p = parent_[b];
            if (p < 0) continue;
            const Quat pq = TrQ(cs[p]);
            local_[b] = QNorm(QMul(QInv(pq), TrQ(cs[b])));
            float v[3] = {cs[b].px - cs[p].px, cs[b].py - cs[p].py, cs[b].pz - cs[p].pz};
            QRotVec(QInv(pq), v[0], v[1], v[2]);
            const float sc[3] = {cs[p].sx, cs[p].sy, cs[p].sz};
            for (int k = 0; k < 3; k++) outP_[b][k] = sc[k] != 0.f ? v[k] / sc[k] : v[k];
        }
        count += 7;
    }
    return count;
}

// ------------------------------------------------------------------ 2026-11-26 multiplayer
namespace {
// does this layer loop (same rule as EvalLayer); the dismount part and trick clips never do
bool LayerLoops(const Clip& c, bool trick, bool dis) {
    return !trick && !dis && !c.hold && ((c.loop && c.drive == Drive::Time) || c.drive == Drive::Cycle);
}
float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
}

void Player::ExportNet(NetFrame& out, float dt) {
    out.n = 0;
    const float mw = EaseSine(master_);
    for (int i = 0; i < nLayers_ && out.n < kNetMaxLayers; i++) {
        Layer& L = layers_[i];
        const float w = Clamp01(EaseSine(L.w) * mw);
        const float t = L.tEval;
        if (L.netHave && dt > 1e-4f) {
            float dtt = t - L.netT;
            if (L.clip && LayerLoops(*L.clip, L.trick, L.dis)) {   // wrapped around the loop
                const float len = LoopLength(*L.clip);
                if (len > 1e-3f && dtt < -0.5f * len) dtt += len;
            }
            float sp = dtt / dt, wsp = (w - L.netW) / dt;
            if (sp < -10.f) sp = -10.f;
            if (sp > 10.f) sp = 10.f;
            if (wsp < -100.f) wsp = -100.f;
            if (wsp > 100.f) wsp = 100.f;
            L.netSpeed += (sp - L.netSpeed) * 0.5f;    // light smoothing: frame times jitter
            L.netWSpeed += (wsp - L.netWSpeed) * 0.5f;
        }
        L.netT = t; L.netW = w; L.netHave = true;
        NetLayer& o = out.l[out.n++];
        o.id = L.netId; o.hash = L.netHash;
        o.mirrored = L.mirrored; o.dis = L.dis; o.trick = L.trick;
        o.t = t < 0.f ? 0.f : t;
        o.speed = L.clip ? L.netSpeed : 0.f;          // a frozen layer holds its last keys
        o.w = w; o.wSpeed = L.netWSpeed;
        o.legW = L.trick ? Clamp01(EaseSine(L.legW)) : 1.f;
    }
}

int Player::ApplyRemote(FTr* cs, int n, const ClipSet* set, const NetFrame& f, float age, float dt, int* missing,
                        NetClockStats* clk) {
    postReady_ = false;
    if (missing) *missing = 0;
    if (clk) *clk = NetClockStats{};
    if (!cs || n != n_ || n_ <= 0) return 0;
    if (!std::isfinite(dt) || dt < 0.f || dt > 0.1f) dt = 0.f;
    if (!std::isfinite(age) || age < 0.f) age = 0.f;
    float fade = 1.f;   // the stream stopped (player gone, packets lost): back to the game's pose
    if (age > kNetStale) fade = 1.f - (age - kNetStale) / kNetStaleFade;
    const bool newFrame = !rSeqHave_ || f.seq != rSeq_;
    rSeq_ = f.seq; rSeqHave_ = true;
    if (clk) clk->newFrame = newFrame;
    // receiver clocks: last call's clocks, each matched at most once to a layer of the same clip and flags
    RemoteClock prev[kNetMaxLayers];
    bool used[kNetMaxLayers] = {};
    const int nPrev = nRclk_;
    for (int i = 0; i < nPrev; i++) prev[i] = rclk_[i];
    nRclk_ = 0;
    // dt == 0 (first call, hitch): no clock to advance, take the sent values
    const float kt = dt > 0.f ? 1.f - std::exp(-dt / kNetTimeTau) : 1.f;
    const float kw = dt > 0.f ? 1.f - std::exp(-dt / kNetWeightTau) : 1.f;
    const float ktBehind = dt > 0.f ? 1.f - std::exp(-dt / kNetTimeTauBehind) : 1.f;
    nLayers_ = 0;
    if (fade > 0.f && set) for (int i = 0; i < f.n && i < kNetMaxLayers; i++) {
        const NetLayer& r = f.l[i];
        const Clip* c = nullptr;
        for (auto& k : set->clips) if (k.netId == r.id && k.netHash == r.hash) { c = &k; break; }
        if (!c) { if (missing) (*missing)++; continue; }
        Layer& L = layers_[nLayers_++];
        strncpy_s(L.file, c->file.c_str(), _TRUNCATE);
        strncpy_s(L.name, c->name.c_str(), _TRUNCATE);
        L.clip = c; L.stamp = c->stamp; L.mirrorStamp = set->mirrorStamp;
        L.mirrored = r.mirrored; L.dis = r.dis; L.trick = r.trick; L.hold = false;
        L.clock = 0.f; L.tPlay = 0.f; L.tc = TrickClock{}; L.grounded = false;
        L.w = L.target = 1.f; L.rate = 0.f; L.legW = 1.f;
        L.nKeys = 0; L.hasPel = false; L.pel[0] = L.pel[1] = L.pel[2] = 0.f;
        L.mode = c->mode;
        L.netId = c->netId; L.netHash = c->netHash; L.netHave = false;
        const bool loops = LayerLoops(*c, r.trick, r.dis);
        const float len = loops ? LoopLength(*c) : c->duration;
        auto fit = [&](float t) {
            if (loops) { if (len > 1e-3f) { t = std::fmod(t, len); if (t < 0.f) t += len; } }
            else { if (t < 0.f) t = 0.f; if (t > c->duration) t = c->duration; }
            return t;
        };
        auto gap = [&](float target, float t) {   // target - t, the short way round a loop
            float d = target - t;
            if (loops && len > 1e-3f) { d = std::fmod(d, len); if (d > 0.5f * len) d -= len; if (d < -0.5f * len) d += len; }
            return d;
        };
        const float tTarget = fit(r.t + r.speed * age);
        const float wTarget = Clamp01(r.w + r.wSpeed * age);
        int best = -1; float bestGap = 0.f;
        for (int k = 0; k < nPrev; k++) {
            const RemoteClock& q = prev[k];
            if (used[k] || q.id != r.id || q.hash != r.hash || q.mirrored != r.mirrored || q.dis != r.dis || q.trick != r.trick) continue;
            const float g = std::fabs(gap(tTarget, fit(q.t + r.speed * dt)));
            if (best < 0 || g < bestGap) { best = k; bestGap = g; }
        }
        float t = tTarget, w = wTarget;
        if (best >= 0) {
            used[best] = true;
            const float tAdv = fit(prev[best].t + r.speed * dt);
            const float wAdv = Clamp01(prev[best].w + r.wSpeed * dt);
            const float dT = gap(tTarget, tAdv), dW = wTarget - wAdv;
            if (clk && newFrame && std::fabs(dT) > clk->jump) clk->jump = std::fabs(dT);
            if (std::fabs(dT) > kNetTimeSnap || dt <= 0.f) { if (dt > 0.f && clk) clk->snaps++; }
            else {
                // v0.22 (after the v0.21 test: stutters on the peer, fast trick clips on low jumps): the landing sync
                // changes the sender's speed in bursts (fast hang, creep, finish in 0.1 s) that we only learn 1 frame
                // in 4. Behind by more than kNetBehind: catch up faster. Ahead (we kept the old, faster speed): slow
                // down, never jump back in time (that was the stutter) and never stop while the sender moves; a
                // sender that stopped (held pose, end of the clip) is joined by drifting back slowly.
                const float k = dT > kNetBehind ? ktBehind : kt;
                const float tPrev = prev[best].t;
                float step = gap(fit(tAdv + dT * k), tPrev);
                const float minStep = r.speed > 1e-3f ? kNetMinRate * r.speed * dt : -kNetBackRate * dt;
                if (step < minStep) step = minStep;
                const float maxStep = std::fmax(r.speed, 0.f) * dt + kNetMaxPull * dt;   // catch up without a lurch
                if (step > maxStep) step = maxStep;
                t = fit(tPrev + step);
            }
            if (std::fabs(dW) <= kNetWeightSnap && dt > 0.f) w = Clamp01(wAdv + dW * kw);
        }
        if (nRclk_ < kNetMaxLayers) rclk_[nRclk_++] = RemoteClock{r.id, r.hash, r.mirrored, r.dis, r.trick, t, w};
        L.tFixed = t;
        L.wFixed = w * Clamp01(fade);
        L.legFixed = Clamp01(r.legW);
    }
    ToLocal(cs, parent_, n_, game_, localP_);
    if (nLayers_ == 0) { postReady_ = false; return 0; }
    GameState gs{};
    gs.manualRatio = 0.5f;
    return BlendLayers(cs, set, gs, dt, false, true);
}

void Player::SnapshotGameLocals(Quat* out, int n) const {
    const int m = n < n_ ? n : n_;
    for (int i = 0; i < m; i++) out[i] = game_[i];
}

const char* Player::CurrentClipName() const {
    for (int i = nLayers_ - 1; i >= 0; i--) if (layers_[i].target > 0.f) return layers_[i].name;
    return "";
}

float Player::LandingClipLength(const ClipSet* set, const GameState& gs) const {
    GameState g{false, false, gs.goofy, gs.sw, 0.f};   // the landing alone, as Choose sees it during the landing
    g.landing = true;
    g.groundStance = gs.groundStance;
    g.landTrickHash = gs.landTrickHash;
    g.landStance = gs.landStance;
    const Clip* c = Choose(set, g, false);
    return c && (c->states & ST_Landing) ? c->duration : 0.f;   // the riding fallback is not a landing clip
}

uint64_t Player::LinkHash(const std::string& link) {   // FNV-1a 64; 0 = no link
    if (link.empty()) return 0;
    uint64_t h = 14695981039346656037ull;
    for (unsigned char ch : link) { h ^= ch; h *= 1099511628211ull; }
    return h ? h : 1;
}

// P6: clip selection. During a trick, clips of that trick (or of "any trick") win over state clips.
// Ties: priority, then specificity (named trick, feet, fewer stances), then file order.
const Clip* Player::Choose(const ClipSet* set, const GameState& gs, bool trickOn) const {
    if (!set) return nullptr;
    // old caller (P5b): kickflip was the only recognised trick
    const uint32_t th = gs.trickHash ? gs.trickHash : TrickHash("KickFlip");
    const uint8_t trickStance = (uint8_t)(1u << (gs.stance >= 0 && gs.stance < STANCE_Count ? gs.stance : 0));
    const int gst = gs.groundStance >= 0 && gs.groundStance < STANCE_Count ? gs.groundStance : (gs.sw ? STANCE_Switch : STANCE_Normal);
    const uint8_t stateStance = (uint8_t)(1u << gst);
    const uint32_t bits = (gs.manual ? ST_Manual : 0u) | (gs.noseManual ? ST_NoseManual : 0u) | (gs.grind ? ST_Grind : 0u) |
                          (gs.powerslide ? ST_Powerslide : 0u) | (gs.grab ? ST_Grab : 0u) | (gs.onFoot ? ST_OnFoot : 0u) |
                          (gs.riding ? ST_Riding : 0u) | (gs.crouch ? ST_Crouch : 0u) | (gs.landing ? ST_Landing : 0u) |
                          (gs.walking ? ST_Walking : 0u) | (gs.pushing ? ST_Pushing : 0u) | (gs.running ? ST_Running : 0u);
    auto feetOk = [&](const Clip& c) { return c.feet == Feet::Any || (c.feet == Feet::Goofy) == gs.goofy; };
    auto better = [](const Clip& c, const Clip* w) {
        if (!w) return true;
        if (c.priority != w->priority) return c.priority > w->priority;
        return ClipSpecificity(c) > ClipSpecificity(*w);
    };
    const Clip* win = nullptr;
    if (trickOn) for (auto& c : set->clips) {
        if (!c.enabled || c.foreign || !(c.states & ST_Trick) || !feetOk(c) || !(c.stances & trickStance)) continue;
        if (c.trickHash && c.trickHash != th) continue;
        if (better(c, win)) win = &c;
    }
    if (win) return win;
    const uint8_t landStance = (uint8_t)(1u << (gs.landStance >= 0 && gs.landStance < STANCE_Count ? gs.landStance : 0));
    auto pick = [&](uint32_t want, uint8_t stances) {
        const Clip* w = nullptr;
        for (auto& c : set->clips) {
            // 2026-11-25: the landing clip of one trick: only after that trick, in the trick's stance
            const bool namedLand = (c.states & ST_Landing) && (want & ST_Landing) && c.trickHash;
            if (namedLand && c.trickHash != gs.landTrickHash) continue;
            if (namedLand && LinkHash(c.link) != trickLink_) continue;   // v0.29: the landing of the trick clip that just played
            if (!c.enabled || c.foreign || !(c.states & want) || !feetOk(c) || !(c.stances & (namedLand ? landStance : stances))) continue;
            // 2026-11-25: a clip of one grind plays only on that grind (a clip with no grind name: every grind)
            if ((c.states & ST_Grind) && (want & ST_Grind) && c.trickHash && c.trickHash != gs.grindHash) continue;
            if (better(c, w)) w = &c;
        }
        return w;
    };
    if (bits) win = pick(bits, stateStance);
    // lot 5: fallback for the base poses on the board: a push, a crouch or a landing with no clip of their own keep the
    // riding clip (normal / switch stance: riding has no fakie / nollie). Nothing off the board: Riding only on the board.
    if (!win && (bits & (ST_Pushing | ST_Crouch | ST_Landing)))
        win = pick(ST_Riding, (uint8_t)(1u << (gs.sw ? STANCE_Switch : STANCE_Normal)));
    return win;
}

namespace {
float Len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
// shortest rotation taking direction a to direction b
Quat Arc(const float a[3], const float b[3]) {
    const float la = Len3(a), lb = Len3(b);
    if (la < 1e-6f || lb < 1e-6f) return QIdent;
    const float ax = a[0] / la, ay = a[1] / la, az = a[2] / la, bx = b[0] / lb, by = b[1] / lb, bz = b[2] / lb;
    const float d = ax * bx + ay * by + az * bz;
    if (d < -0.99999f) {   // opposite: half turn around any perpendicular axis
        float px = -ay, py = ax, pz = 0.f;
        if (std::fabs(az) > 0.9f) { px = 0.f; py = -az; pz = ay; }
        const float lp = std::sqrt(px * px + py * py + pz * pz);
        return Quat{px / lp, py / lp, pz / lp, 0.f};
    }
    return QNorm(Quat{ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx, 1.f + d});
}
// component transform of a child from its parent's component transform and its local rotation / position
void Compose(const FTr& parent, Quat lq, const float lp[3], Quat& q, float p[3]) {
    const Quat pq = TrQ(parent);
    q = QNorm(QMul(pq, lq));
    float x = lp[0] * parent.sx, y = lp[1] * parent.sy, z = lp[2] * parent.sz;
    QRotVec(pq, x, y, z);
    p[0] = parent.px + x; p[1] = parent.py + y; p[2] = parent.pz + z;
}
// local position of a point (component space) in a parent frame
void ToLocalPos(Quat pq, const float pp[3], const float ps[3], const float w[3], float out[3]) {
    float v[3] = {w[0] - pp[0], w[1] - pp[1], w[2] - pp[2]};
    QRotVec(QInv(pq), v[0], v[1], v[2]);
    for (int k = 0; k < 3; k++) out[k] = ps[k] != 0.f ? v[k] / ps[k] : v[k];
}
} // namespace

int Player::PostApply(FTr* cs, int n) {
    if (!postReady_ || !cs || n != n_ || n_ <= 0) return 0;
    postReady_ = false;
    ToLocal(cs, parent_, n_, postQ_, postP_);
    std::memset(dirty_, 0, sizeof(bool) * n_);
    // 1) the leg chains our clips wrote this frame get back the pose we gave them (what the editor shows), lengths included
    for (int b = 0; b < n_; b++) {
        if (!leg_[b] || legRoot_[b] < 0) continue;
        const float w = legWrote_[legRoot_[b]];
        if (w <= 0.f) continue;
        postQ_[b] = QSlerp(postQ_[b], local_[b], w);
        for (int k = 0; k < 3; k++) postP_[b][k] += (outP_[b][k] - postP_[b][k]) * w;
        dirty_[b] = true;
    }
    // 2) anti-stretch: thigh and shin keep their own length, the knee follows the foot (two-bone IK on the final foot)
    const float aS = antiT_ >= kAntiFade ? 1.f : antiT_ / kAntiFade;
    if (aS > 0.f) for (int r = 0; r < n_; r++) {
        const int c = chainMid_[r], f = chainEnd_[r], p = parent_[r];
        if (legRoot_[r] != r || c < 0 || f < 0 || p < 0) continue;
        const float our1 = Len3(haveRef_ ? refP_[c] : localP_[c]), our2 = Len3(haveRef_ ? refP_[f] : localP_[f]);
        const float cur1 = Len3(postP_[c]), cur2 = Len3(postP_[f]);
        if (our1 < 1.f || our2 < 1.f) continue;
        const float L1 = cur1 + (our1 - cur1) * aS, L2 = cur2 + (our2 - cur2) * aS;
        if (std::fabs(L1 - cur1) < 0.02f && std::fabs(L2 - cur2) < 0.02f) continue;   // nothing stretched
        // current chain in component space (parent of the thigh is not a leg bone: cs[p] is final)
        Quat qT, qC, qF;
        float H[3], K[3], F[3];
        Compose(cs[p], postQ_[r], postP_[r], qT, H);
        FTr tT{qT.x, qT.y, qT.z, qT.w, H[0], H[1], H[2], 0, cs[r].sx, cs[r].sy, cs[r].sz, 0};
        Compose(tT, postQ_[c], postP_[c], qC, K);
        FTr tC{qC.x, qC.y, qC.z, qC.w, K[0], K[1], K[2], 0, cs[c].sx, cs[c].sy, cs[c].sz, 0};
        Compose(tC, postQ_[f], postP_[f], qF, F);
        float v[3] = {F[0] - H[0], F[1] - H[1], F[2] - H[2]};
        const float d = Len3(v);
        if (d < 1e-3f) continue;
        const float u[3] = {v[0] / d, v[1] / d, v[2] / d};
        const float lo = std::fabs(L1 - L2) + 0.01f, hi = L1 + L2 - 0.01f;
        const float dc = d < lo ? lo : d > hi ? hi : d;
        const float a = (L1 * L1 - L2 * L2 + dc * dc) / (2.f * dc);
        const float hgt = std::sqrt(L1 * L1 - a * a > 0.f ? L1 * L1 - a * a : 0.f);
        float kp[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]};   // the knee keeps its side
        const float ku = kp[0] * u[0] + kp[1] * u[1] + kp[2] * u[2];
        for (int k = 0; k < 3; k++) kp[k] -= ku * u[k];
        float lk = Len3(kp);
        if (lk < 1e-4f) {   // straight leg: bend toward the calf's current front (local Y of the thigh)
            kp[0] = 0.f; kp[1] = 1.f; kp[2] = 0.f;
            QRotVec(qT, kp[0], kp[1], kp[2]);
            const float k2 = kp[0] * u[0] + kp[1] * u[1] + kp[2] * u[2];
            for (int k = 0; k < 3; k++) kp[k] -= k2 * u[k];
            lk = Len3(kp);
            if (lk < 1e-4f) continue;
        }
        float Kn[3], Fn[3];
        for (int k = 0; k < 3; k++) { Kn[k] = H[k] + u[k] * a + kp[k] / lk * hgt; Fn[k] = H[k] + u[k] * dc; }
        const float hk0[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]}, hk1[3] = {Kn[0] - H[0], Kn[1] - H[1], Kn[2] - H[2]};
        const Quat qa = Arc(hk0, hk1);
        const Quat qT2 = QNorm(QMul(qa, qT));
        float kf0[3] = {F[0] - K[0], F[1] - K[1], F[2] - K[2]};
        QRotVec(qa, kf0[0], kf0[1], kf0[2]);
        const float kf1[3] = {Fn[0] - Kn[0], Fn[1] - Kn[1], Fn[2] - Kn[2]};
        const Quat qC2 = QNorm(QMul(Arc(kf0, kf1), QMul(qa, qC)));
        // new locals: thigh rotation, calf rotation + position, foot rotation (its orientation is kept) + position
        postQ_[r] = QNorm(QMul(QInv(TrQ(cs[p])), qT2));
        postQ_[c] = QNorm(QMul(QInv(qT2), qC2));
        postQ_[f] = QNorm(QMul(QInv(qC2), qF));
        const float sT[3] = {cs[r].sx, cs[r].sy, cs[r].sz}, sC[3] = {cs[c].sx, cs[c].sy, cs[c].sz};
        ToLocalPos(qT2, H, sT, Kn, postP_[c]);
        ToLocalPos(qC2, Kn, sC, Fn, postP_[f]);
        dirty_[r] = dirty_[c] = dirty_[f] = true;
    }
    int count = 0;
    for (int i = 0; i < n_; i++) {
        if (!dirty_[i] && parent_[i] >= 0 && dirty_[parent_[i]]) dirty_[i] = true;
        if (dirty_[i]) count++;
    }
    if (count) ToComponent(cs, parent_, n_, postQ_, postP_, dirty_);
    return count;
}

void Player::SetRefPositions(const std::vector<float>& xyz) {
    haveRef_ = xyz.size() == (size_t)n_ * 3 && n_ > 0;
    if (haveRef_) for (int i = 0; i < n_; i++) for (int k = 0; k < 3; k++) refP_[i][k] = xyz[(size_t)i * 3 + k];
}

void Player::SnapshotGamePositions(float (*out)[3], int n) const {
    const int m = n < n_ ? n : n_;
    for (int i = 0; i < m; i++) for (int k = 0; k < 3; k++) out[i][k] = localP_[i][k];
}

float Player::PoseGap(const Layer* a, const Layer* b) {
    auto fill = [&](const Layer* L, Quat* out) {
        std::memcpy(out, game_, sizeof(Quat) * n_);
        if (!L) return;
        for (int k = 0; k < L->nKeys; k++) {
            const int bone = L->bone[k];
            if (bone < 0 || bone >= n_) continue;
            const float w = L->boneW[k];
            out[bone] = L->mode == Mode::Additive ? QNorm(QMul(game_[bone], QSlerp(QIdent, L->key[k], w))) : QSlerp(game_[bone], L->key[k], w);
        }
    };
    fill(a, gapA_);
    fill(b, gapB_);
    float worst = 0.f;
    for (int i = 0; i < n_; i++) {
        float d = std::fabs(QDot(gapA_[i], gapB_[i]));
        if (d > 1.f) d = 1.f;
        const float ang = 2.f * std::acos(d) * 57.29578f;
        if (ang > worst) worst = ang;
    }
    return worst;
}
