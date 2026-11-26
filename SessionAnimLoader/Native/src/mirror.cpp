#include "mirror.h"
#include <cmath>

namespace {

// the four possible reflections (diagonal signs of determinant -1), in the order of conventions 0..3
const int kRefl[4][3] = {{-1, -1, -1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, -1}};

Quat ConvQuat(int c) {
    switch (c) {
        case 1: return Quat{1.f, 0.f, 0.f, 0.f};
        case 2: return Quat{0.f, 1.f, 0.f, 0.f};
        case 3: return Quat{0.f, 0.f, 1.f, 0.f};
        default: return QIdent;
    }
}

// picks the reflection that agrees most with the per-axis votes (>0: same sign, <0: opposite sign, 0: no opinion).
// On a tie: the preferred convention (the parent's) if it is one of them, else the first (identity first).
// -1 if there is no vote.
int Pick(const double vote[3], int preferred) {
    if (vote[0] == 0 && vote[1] == 0 && vote[2] == 0) return -1;
    double score[4];
    double bestScore = -1e30;
    for (int c = 0; c < 4; c++) {
        score[c] = 0;
        for (int k = 0; k < 3; k++) score[c] += vote[k] * kRefl[c][k];
        if (score[c] > bestScore) bestScore = score[c];
    }
    if (preferred >= 0 && preferred < 4 && score[preferred] > bestScore - 1e-9) return preferred;
    for (int c = 0; c < 4; c++) if (score[c] > bestScore - 1e-9) return c;
    return 0;
}

} // namespace

int ConvFromName(const std::string& s) {
    if (s == "I") return 0;
    if (s == "X") return 1;
    if (s == "Y") return 2;
    if (s == "Z") return 3;
    return -1;
}

MirrorMap BuildMirrorMap(const std::vector<std::string>& names, const std::vector<int>& parents, const std::vector<float>& refPos) {
    MirrorMap m;
    const int n = (int)names.size();
    m.other.resize(n);
    m.center.resize(n);
    m.parent.assign(n, -1);
    m.conv.assign(n, 0);
    for (int i = 0; i < n; i++) {
        if (i < (int)parents.size()) m.parent[i] = parents[i];
        const std::string& s = names[i];
        const size_t l = s.find("_L_"), r = s.find("_R_");
        m.other[i] = i;
        m.center[i] = (l == std::string::npos && r == std::string::npos);
        if (m.center[i]) continue;
        std::string twin = s;
        if (l != std::string::npos) twin.replace(l, 3, "_R_");
        else twin.replace(r, 3, "_L_");
        for (int j = 0; j < n; j++) if (names[j] == twin) { m.other[i] = j; break; }
    }
    const bool havePos = (int)refPos.size() >= 3 * n;
    for (int i = 0; i < n; i++) {
        double vote[3] = {0, 0, 0};
        if (havePos) {
            const int oi = m.other[i];
            for (int c = i + 1; c < n; c++) {
                if (m.parent[c] != i || m.center[c]) continue;   // only left/right children give information
                const int oc = m.other[c];
                if (oc == c || m.parent[oc] != oi) continue;
                for (int k = 0; k < 3; k++) {
                    const double a = refPos[3 * c + k], b = refPos[3 * oc + k];
                    if (std::fabs(a) < 1e-2 || std::fabs(b) < 1e-2) continue;   // null component: no opinion
                    vote[k] += ((a > 0) == (b > 0)) ? 1.0 : -1.0;
                }
            }
        }
        const int p = m.parent[i];
        const bool sameKind = p >= 0 && m.center[p] == m.center[i];
        int c = Pick(vote, sameKind ? m.conv[p] : -1);
        // without a clue: like the parent if it is of the same kind (side / centre), else side I, centre Z
        if (c < 0) c = sameKind ? m.conv[p] : (m.center[i] ? 3 : 0);
        m.conv[i] = c;
    }
    // a bone and its twin share the same convention: take the left side one if they differ
    for (int i = 0; i < n; i++) {
        const int o = m.other[i];
        if (o != i && names[i].find("_L_") != std::string::npos) m.conv[o] = m.conv[i];
    }
    return m;
}

std::vector<int> EffectiveConv(const MirrorMap& map, const std::vector<std::pair<int, int>>& overrides) {
    std::vector<int> e = map.conv;
    for (auto& ov : overrides) {
        if (ov.first < 0 || ov.first >= (int)e.size() || ov.second < 0 || ov.second > 3) continue;
        e[ov.first] = ov.second;
        e[map.other[ov.first]] = ov.second;
    }
    return e;
}

Quat MirrorKey(Quat k, int convParent, int convBone) {
    return QNorm(QMul(QMul(ConvQuat(convParent), k), QInv(ConvQuat(convBone))));
}
