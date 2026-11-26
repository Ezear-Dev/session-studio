// AnimStudio Editor -- EditorDoc: hand IK and per-bone timeline rows.
#include "editor_doc.h"
#include "ik.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kKeyEps = 1e-3f;
bool IsHandGroup(const std::string& n) { return n.find("_Hand") != std::string::npos || n.find("Finger") != std::string::npos; }
int Side(const std::string& n) { return n.find("_L_") != std::string::npos ? 0 : n.find("_R_") != std::string::npos ? 1 : -1; }
std::string Short(const std::string& n) { return n.rfind("AMXX_", 0) == 0 ? n.substr(5) : n; }
} // namespace

// ------------------------------------------------------------------ IK
bool EditorDoc::IkChain(int bone, int& upper, int& fore, int& hand) const {
    if (bone < 0 || bone >= BoneCount()) return false;
    const std::string& n = skel_.names[bone];
    const bool arm = n.find("UpperArm") != std::string::npos || n.find("Forearm") != std::string::npos || IsHandGroup(n);
    const bool leg = IsLegBone(n);   // P5b : Thigh, Calf, Foot, Toe
    if (!arm && !leg) return false;
    const size_t p = n.find(Side(n) == 0 ? "_L_" : "_R_");
    if (Side(n) < 0 || p == std::string::npos) return false;
    const std::string prefix = n.substr(0, p + 3);   // "AMXX_L_"
    const char* parts[3] = {"UpperArm", "Forearm", "Hand"};
    if (leg) { parts[0] = "Thigh"; parts[1] = "Calf"; parts[2] = "Foot"; }
    upper = fore = hand = -1;
    for (int i = 0; i < BoneCount(); i++) {
        if (skel_.names[i] == prefix + parts[0]) upper = i;
        else if (skel_.names[i] == prefix + parts[1]) fore = i;
        else if (skel_.names[i] == prefix + parts[2]) hand = i;
    }
    if (leg && !IsEditable(hand)) return false;   // legs: only in a trick clip
    return upper >= 0 && fore >= 0 && hand >= 0 && skel_.parents[fore] == upper && skel_.parents[hand] == fore;
}

IkJoint EditorDoc::IkHandle(int bone, int& root, int& upper, int& fore, int& end) const {
    root = upper = fore = end = -1;
    if (bone < 0 || bone >= BoneCount()) return IkJoint::None;
    const std::string& n = skel_.names[bone];
    int chainBone = bone;
    const bool clav = n.find("Clavicle") != std::string::npos;
    if (clav) {   // clavicle: the arm chain on its side (its UpperArm child)
        chainBone = -1;
        for (int i = 0; i < BoneCount(); i++)
            if (skel_.parents[i] == bone && skel_.names[i].find("UpperArm") != std::string::npos) chainBone = i;
        if (chainBone < 0) return IkJoint::None;
    }
    if (!IkChain(chainBone, upper, fore, end) || !IsEditable(upper) || !IsEditable(fore) || !IsEditable(end)) {
        upper = fore = end = -1;
        return IkJoint::None;
    }
    const std::string& cn = skel_.names[chainBone];
    if (clav || cn.find("UpperArm") != std::string::npos) {
        root = skel_.parents[upper];
        if (root < 0 || skel_.names[root].find("Clavicle") == std::string::npos || !IsEditable(root)) { root = upper = fore = end = -1; return IkJoint::None; }
        return IkJoint::Root;
    }
    if (cn.find("Thigh") != std::string::npos) { upper = fore = end = -1; return IkJoint::None; }   // hip = pelvis: later
    if (chainBone == fore) return IkJoint::Mid;
    return IkJoint::End;
}

void EditorDoc::IkBegin(int bone, float t) {
    int r, u, f, h;
    ikBaseHand_ = -1;
    ikKind_ = IkJoint::None;
    IkJoint kind = IkHandle(bone, r, u, f, h);
    if (kind == IkJoint::None) {   // compatibility: a chain bone without its own handle (e.g. thigh) -> the hand or the foot
        if (!IkChain(bone, u, f, h)) return;
        kind = IkJoint::End;
        r = -1;
    }
    std::vector<FTr> cs;
    // base without the leg weight: the key must not be attenuated twice; for a leg: without the pelvis offset,
    // which rewrites thigh and calf (2026-11-25 fix: the gesture was fighting it)
    ComponentPose(t, cs, false, true, !IsLegBone(skel_.names[u]));
    ikBody_ = cs;            // P6 lot 2: whole pose, for the spine that follows the hand
    ikPendBase_ = pending_;
    const FTr id{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0};
    const int pu = skel_.parents[u];
    ikBase_[0] = pu >= 0 ? cs[pu] : id;
    ikBase_[1] = cs[u];
    ikBase_[2] = cs[f];
    ikBase_[3] = cs[h];
    if (r >= 0) {
        const int pr = skel_.parents[r];
        ikBaseRoot_[0] = pr >= 0 ? cs[pr] : id;
        ikBaseRoot_[1] = cs[r];
    }
    ikBaseHand_ = h;
    ikKind_ = kind;
}

namespace {
// rotation r (component space) around the pivot applied to a transform: position and rotation carried rigidly
FTr Carry(const FTr& x, Quat r, const FTr& pivot) {
    FTr o = x;
    float v[3] = {x.px - pivot.px, x.py - pivot.py, x.pz - pivot.pz};
    QRotVec(r, v[0], v[1], v[2]);
    o.px = pivot.px + v[0]; o.py = pivot.py + v[1]; o.pz = pivot.pz + v[2];
    const Quat q = QNorm(QMul(r, TrQ(x)));
    o.qx = q.x; o.qy = q.y; o.qz = q.z; o.qw = q.w;
    return o;
}
} // namespace

bool EditorDoc::IkMoveJoint(int bone, const float target[3], bool keepEnd, bool keepOrient, float t) {
    int r, u, f, h;
    const IkJoint kind = IkHandle(bone, r, u, f, h);
    if (kind == IkJoint::None) return false;
    if (kind == IkJoint::End) return IkMoveHand(h, target, 0.f, keepOrient, t);
    if (ikBaseHand_ != h || ikKind_ != kind) IkBegin(bone, t);   // no gesture running: the current pose is the base
    const FTr& bp = ikBase_[0];
    const FTr& bu = ikBase_[1];
    const FTr& bf = ikBase_[2];
    const FTr& bh = ikBase_[3];
    const float handPos[3] = {bh.px, bh.py, bh.pz};
    Quat qu, qf, qh;
    if (kind == IkJoint::Mid) {
        if (keepEnd) {   // the elbow turns around the shoulder -> hand axis; the hand does not move
            const float delta = SwivelToward(bu, bf, bh, target);
            if (!std::isfinite(delta)) return false;
            if (!SolveTwoBoneIK(bu, bf, bh, handPos, delta, keepOrient, qu, qf, qh)) return false;
            SetPending(u, QMul(QInv(TrQ(bp)), qu));
            SetPending(f, QMul(QInv(qu), qf));
            if (keepOrient) SetPending(h, QMul(QInv(qf), qh));
            return true;
        }
        // free elbow: the arm points at the target, the forearm keeps its local rotation, the hand follows
        if (!AimBone(bu, bf, target, qu)) return false;
        SetPending(u, QMul(QInv(TrQ(bp)), qu));
        if (keepOrient) {
            const Quat rot = QMul(qu, QInv(TrQ(bu)));
            qf = QNorm(QMul(rot, TrQ(bf)));
            SetPending(h, QMul(QInv(qf), TrQ(bh)));
        }
        return true;
    }
    // shoulder: the clavicle points the shoulder at the target; the arm is carried rigidly
    const FTr& bcp = ikBaseRoot_[0];
    const FTr& bc = ikBaseRoot_[1];
    Quat qc;
    if (!AimBone(bc, bu, target, qc)) return false;
    const Quat rot = QMul(qc, QInv(TrQ(bc)));
    const FTr cu = Carry(bu, rot, bc), cf = Carry(bf, rot, bc), ch = Carry(bh, rot, bc);
    SetPending(r, QMul(QInv(TrQ(bcp)), qc));
    if (keepEnd) {   // the arm is solved again to bring the hand back to its start position (zero elbow delta)
        if (!SolveTwoBoneIK(cu, cf, ch, handPos, 0.f, false, qu, qf, qh)) return false;
        SetPending(u, QMul(QInv(qc), qu));
        SetPending(f, QMul(QInv(qu), qf));
        if (keepOrient) SetPending(h, QMul(QInv(qf), TrQ(bh)));
        return true;
    }
    if (keepOrient) SetPending(h, QMul(QInv(TrQ(cf)), TrQ(bh)));
    return true;
}

bool EditorDoc::IkMoveHand(int handBone, const float target[3], float elbowDeltaDeg, bool keepHandWorld, float t) {
    int u, f, h;
    if (!IkChain(handBone, u, f, h) || !IsEditable(u) || !IsEditable(f) || !IsEditable(h)) return false;
    if (ikBaseHand_ != h) IkBegin(h, t);   // no gesture running: the current pose is the base
    Quat qu, qf, qh;
    // always from the gesture base: the result does not depend on the mouse path
    if (!SolveTwoBoneIK(ikBase_[1], ikBase_[2], ikBase_[3], target, elbowDeltaDeg, keepHandWorld, qu, qf, qh)) return false;
    SetPending(u, QMul(QInv(TrQ(ikBase_[0])), qu));
    SetPending(f, QMul(QInv(qu), qf));
    if (keepHandWorld) SetPending(h, QMul(QInv(qf), qh));   // otherwise the local rotation of the hand is unchanged: it follows the forearm
    return true;
}


// ------------------------------------------------------------------ timeline rows
std::vector<EditorDoc::KeyRow> EditorDoc::KeyRows(bool allEditable) const {
    std::vector<KeyRow> rows;
    int groupRow[2] = {-1, -1};
    const int n = BoneCount();
    for (int b = 0; b < n; b++) {
        const std::string& name = skel_.names[b];
        const bool tracked = TrackOf(b) != nullptr;
        if (!tracked && !(allEditable && IsEditable(b))) continue;
        const int side = Side(name);
        if (side >= 0 && IsHandGroup(name)) {
            if (groupRow[side] < 0) {
                KeyRow r;
                r.label = side == 0 ? "L Hand" : "R Hand";
                r.primary = -1;
                for (int i = 0; i < n; i++) {
                    if (Side(skel_.names[i]) != side || !IsHandGroup(skel_.names[i])) continue;
                    r.bones.push_back(i);
                    if (skel_.names[i].size() >= 5 && skel_.names[i].compare(skel_.names[i].size() - 5, 5, "_Hand") == 0) r.primary = i;
                }
                if (r.primary < 0) r.primary = b;
                groupRow[side] = (int)rows.size();
                rows.push_back(std::move(r));
            }
            continue;
        }
        rows.push_back(KeyRow{Short(name), {b}, b});
    }
    const int pel = PelvisBone();   // P6 lot 2: the pelvis offset keys
    if (pel >= 0 && (!clip_.pelvis.empty() || allEditable)) rows.push_back(KeyRow{"Pelvis", {pel}, pel});
    return rows;
}

std::vector<float> EditorDoc::KeyTimesBones(const std::vector<int>& bones) const {
    std::vector<float> v;
    for (int b : bones)
        for (float t : KeyTimes(b)) {
            bool dup = false;
            for (float x : v) if (std::fabs(x - t) < kKeyEps) dup = true;
            if (!dup) v.push_back(t);
        }
    std::sort(v.begin(), v.end());
    return v;
}

bool EditorDoc::MoveKeysBones(const std::vector<int>& bones, float from, float to) {
    if (!std::isfinite(to) || to < 0.f || to > clip_.duration) return false;
    if (std::fabs(from - to) < kKeyEps) return true;
    const bool pel = PelvisBone() >= 0 && std::find(bones.begin(), bones.end(), PelvisBone()) != bones.end();
    if (pel) {   // P6 lot 2
        bool hasFrom = false, hasTo = false;
        for (auto& k : clip_.pelvis) { if (std::fabs(k.t - from) < kKeyEps) hasFrom = true; if (std::fabs(k.t - to) < kKeyEps) hasTo = true; }
        if (hasFrom && hasTo) return false;
        if (bones.size() == 1) {
            if (!hasFrom) return false;
            for (auto& k : clip_.pelvis) if (std::fabs(k.t - from) < kKeyEps) k.t = to;
            std::sort(clip_.pelvis.begin(), clip_.pelvis.end(), [](const PosKey& a, const PosKey& b) { return a.t < b.t; });
            return true;
        }
    }
    std::vector<int> tracks;
    for (int b : bones) { if (!IsEditable(b)) continue; const int ti = TrackIndex(b); if (ti >= 0) tracks.push_back(ti); }   // never the locked tracks
    if (tracks.empty()) return false;
    for (int ti : tracks) {
        bool hasFrom = false, hasTo = false;
        for (auto& k : clip_.tracks[ti].keys) { if (std::fabs(k.t - from) < kKeyEps) hasFrom = true; if (std::fabs(k.t - to) < kKeyEps) hasTo = true; }
        if (hasFrom && hasTo) return false;
    }
    for (int ti : tracks) {
        auto& keys = clip_.tracks[ti].keys;
        for (auto& k : keys) if (std::fabs(k.t - from) < kKeyEps) k.t = to;
        std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
    }
    if (pel) {
        for (auto& k : clip_.pelvis) if (std::fabs(k.t - from) < kKeyEps) k.t = to;
        std::sort(clip_.pelvis.begin(), clip_.pelvis.end(), [](const PosKey& a, const PosKey& b) { return a.t < b.t; });
    }
    return true;
}

void EditorDoc::DeleteKeysBones(const std::vector<int>& bones, float t) {
    for (int b : bones) if (IsEditable(b) || (b >= 0 && b == PelvisBone())) DeleteKeysAt(t, b);   // never the locked tracks
}


// ------------------------------------------------------------------ P6: copy a clip to another variant
void EditorDoc::RetargetVariant(const FigureVariant& v) {
    ClearClipboard();   // another variant: copy / paste stays inside one clip
    Clip c = clip_;
    const bool before = AuthorMirrored(c);
    SetClipVariant(c, v);
    const bool after = AuthorMirrored(c);
    if (before != after && HasSkeleton()) {
        std::vector<Track> out;
        for (const Track& tr : c.tracks) {
            int b = -1;
            for (int i = 0; i < BoneCount(); i++) if (skel_.names[i] == tr.bone) { b = i; break; }
            if (b < 0 || b >= (int)map_.other.size() || b >= (int)map_.conv.size()) { out.push_back(tr); continue; }
            const int o = map_.other[b], p = skel_.parents[b];
            const int cb = map_.conv[b];
            const int cp = (c.mode == Mode::Replace && p >= 0 && p < (int)map_.conv.size()) ? map_.conv[p] : cb;
            Track t2 = tr;
            t2.bone = skel_.names[o];
            t2.index = o;
            for (Key& k : t2.keys) k.q = MirrorKey(k.q, cp, cb);
            out.push_back(std::move(t2));
        }
        c.tracks = std::move(out);
        // P6 lot 2: the pelvis offset changes side along the hip axis
        std::vector<FTr> cs;
        ComponentPose(0.f, cs, true);
        float hip[3];
        if (HipAxis(cs.data(), rig_, hip)) for (auto& k : c.pelvis) MirrorOffset(hip, k.v);
    }
    if (!StateLoops(v.state)) { c.loop = false; c.hold = false; c.drive = Drive::Time; }
    clip_ = std::move(c);
    variant_ = v;
    pending_.clear();
}

int EditorDoc::SeedKeys() {
    if (!HasFigure() || !clip_.tracks.empty() || !IsTrickClip() || !HasSkeleton()) return 0;
    std::vector<float> times{0.f};
    for (int ph : {2, 4}) {   // flip, landing
        const float pt = PhaseStart(ph);
        if (pt <= 0.f || pt > clip_.duration) continue;
        bool near = false;
        for (float t : times) if (std::fabs(t - pt) < 1.5f / 30.f) near = true;
        if (!near) times.push_back(pt);
    }
    int count = 0;
    for (int b = 0; b < BoneCount(); b++) {
        if (!IsEditable(b)) continue;
        Track tr;
        tr.bone = skel_.names[b];
        tr.index = b;
        for (float t : times) {
            const Quat o = OriginFor(b, t);
            tr.keys.push_back(Key{t, KeyForOrigin(o, -1, o), true});
            count++;
        }
        clip_.tracks.push_back(std::move(tr));
    }
    return count;
}
