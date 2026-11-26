// AnimStudio Editor -- EditorDoc, P6 lot 2 (ideas from Skate Style's studio): pelvis offset, the body follows the hand.
#include "editor_doc.h"
#include "ik.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kReach = 0.9f;     // the spine starts to follow when the hand target is beyond 90 % of the arm length
constexpr float kClavMax = 30.f;   // most a clavicle turns to follow the hand (deg)
constexpr float kSpineMax = 12.f;  // most each spine bone turns (deg)

float Dist(const FTr& a, const FTr& b) { return std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) + (a.pz - b.pz) * (a.pz - b.pz)); }
float AngleDeg(Quat q) { const float w = std::fabs(q.w) > 1.f ? 1.f : std::fabs(q.w); return 2.f * std::acos(w) * 57.29578f; }
// rotate x (component space) by r around the point pivot
void CarryInto(FTr& x, Quat r, const float pivot[3]) {
    float v[3] = {x.px - pivot[0], x.py - pivot[1], x.pz - pivot[2]};
    QRotVec(r, v[0], v[1], v[2]);
    x.px = pivot[0] + v[0]; x.py = pivot[1] + v[1]; x.pz = pivot[2] + v[2];
    const Quat q = QNorm(QMul(r, TrQ(x)));
    x.qx = q.x; x.qy = q.y; x.qz = q.z; x.qw = q.w;
}
} // namespace

// ------------------------------------------------------------------ pelvis offset
void EditorDoc::PelvisOffset(float t, float out[3]) const {
    if (pelPending_) { for (int k = 0; k < 3; k++) out[k] = pel_[k]; return; }
    EvalClipPelvis(clip_, KeyTime(t), LoopsInTime(), out);
}

bool EditorDoc::SetPendingPelvis(const float v[3]) {
    if (PelvisBone() < 0) return false;
    for (int k = 0; k < 3; k++) { if (!std::isfinite(v[k])) return false; }
    for (int k = 0; k < 3; k++) pel_[k] = std::clamp(v[k], -60.f, 60.f);
    pelPending_ = true;
    return true;
}

bool EditorDoc::PendingPelvis(float out[3]) const {
    if (!pelPending_) return false;
    for (int k = 0; k < 3; k++) out[k] = pel_[k];
    return true;
}

// ------------------------------------------------------------------ the spine and the clavicle follow the hand
bool EditorDoc::IkMoveHandBody(int handBone, const float target[3], bool keepHandWorld, float t) {
    int u, f, h;
    if (!IkChain(handBone, u, f, h) || IsLegBone(skel_.names[h])) return IkMoveHand(handBone, target, 0.f, keepHandWorld, t);
    const int clav = skel_.parents[u];
    if (clav < 0 || skel_.names[clav].find("Clavicle") == std::string::npos || !IsEditable(clav))
        return IkMoveHand(handBone, target, 0.f, keepHandWorld, t);
    if (ikBaseHand_ != h || (int)ikBody_.size() != BoneCount()) IkBegin(h, t);
    if ((int)ikBody_.size() != BoneCount()) return false;
    // chain that may follow, from the shoulder down: clavicle, then the spine bones above the pelvis
    std::vector<int> chain{clav};
    std::vector<float> maxDeg{kClavMax};
    for (int b = skel_.parents[clav]; b >= 0 && skel_.names[b].find("Spine") != std::string::npos && IsEditable(b); b = skel_.parents[b]) {
        chain.push_back(b);
        maxDeg.push_back(kSpineMax);
    }
    // back to the pose of the gesture start for these bones (a target back in reach gives the body back)
    for (int b : chain) {
        auto it = ikPendBase_.find(b);
        if (it != ikPendBase_.end()) pending_[b] = it->second; else pending_.erase(b);
    }
    std::vector<FTr> P = ikBody_;
    const float L = Dist(P[u], P[f]) + Dist(P[f], P[h]);
    const float S[3] = {P[u].px, P[u].py, P[u].pz};
    const float dv[3] = {S[0] - target[0], S[1] - target[1], S[2] - target[2]};
    const float d = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
    if (d > kReach * L && L > 1.f) {
        // where the shoulder should be for the hand to reach the target with the arm at 90 % of its length
        const float want[3] = {target[0] + dv[0] / d * kReach * L, target[1] + dv[1] / d * kReach * L, target[2] + dv[2] / d * kReach * L};
        const int n = BoneCount();
        std::vector<Quat> acc(chain.size(), QIdent);
        for (int iter = 0; iter < 8; iter++) {
            for (size_t c = 0; c < chain.size(); c++) {   // CCD, shoulder side first
                const int b = chain[c];
                const float piv[3] = {P[b].px, P[b].py, P[b].pz};
                const float a[3] = {P[u].px - piv[0], P[u].py - piv[1], P[u].pz - piv[2]};
                const float w[3] = {want[0] - piv[0], want[1] - piv[1], want[2] - piv[2]};
                Quat r = QSlerp(QIdent, ShortestArc(a, w), 0.5f);   // half a step: the bones share the turn
                Quat next = QNorm(QMul(r, acc[c]));
                const float ang = AngleDeg(next);
                if (ang > maxDeg[c]) { next = QSlerp(QIdent, next, maxDeg[c] / ang); r = QNorm(QMul(next, QInv(acc[c]))); }
                acc[c] = next;
                // the bone and everything it carries
                std::vector<char> in(n, 0);
                in[b] = 1;
                for (int i = b + 1; i < n; i++) if (skel_.parents[i] >= 0 && in[skel_.parents[i]]) in[i] = 1;
                for (int i = 0; i < n; i++) if (in[i]) CarryInto(P[i], r, piv);
            }
        }
        for (int c = (int)chain.size() - 1; c >= 0; c--) {   // parents first
            const int b = chain[c], p = skel_.parents[b];
            if (AngleDeg(acc[c]) < 0.05f) continue;
            pending_[b] = QNorm(QMul(QInv(p >= 0 ? TrQ(P[p]) : QIdent), TrQ(P[b])));
        }
    }
    FTr hand = P[h];   // the hand keeps the orientation it had at the start of the gesture
    hand.qx = ikBody_[h].qx; hand.qy = ikBody_[h].qy; hand.qz = ikBody_[h].qz; hand.qw = ikBody_[h].qw;
    Quat qu, qf, qh;
    if (!SolveTwoBoneIK(P[u], P[f], hand, target, 0.f, keepHandWorld, qu, qf, qh)) return false;
    SetPending(u, QMul(QInv(TrQ(P[clav])), qu));
    SetPending(f, QMul(QInv(qu), qf));
    if (keepHandWorld) SetPending(h, QMul(QInv(qf), qh));
    return true;
}
