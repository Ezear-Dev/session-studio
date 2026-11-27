#include "board.h"
#include <cmath>

namespace {
bool UnitQuat(const double* v, Quat& q) {
    const double n = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
    if (!(n > 0.9) || !(n < 1.1)) return false;   // also refuses the all-zero values of a failed read
    q = QNorm(Quat{(float)v[0], (float)v[1], (float)v[2], (float)v[3]});
    return true;
}
} // namespace

bool BoardInMesh(const double in[kBoardValues], BoardPose& out, const char*& why) {
    why = "";
    for (int i = 0; i < kBoardValues; i++)
        if (!std::isfinite(in[i])) { why = "a value is not a number"; return false; }
    const double *meshP = in, *meshQ = in + 3, *meshS = in + 7, *deckP = in + 10, *deckQ = in + 13;
    Quat mq, dq;
    if (!UnitQuat(meshQ, mq) || !UnitQuat(deckQ, dq)) { why = "unreadable rotation"; return false; }
    for (int k = 0; k < 3; k++)
        if (std::fabs(meshS[k] - 1.0) > 0.01) { why = "the skater mesh is scaled"; return false; }
    // the difference is taken in double: world positions are large, the deck is close to the mesh
    const double d[3] = {deckP[0] - meshP[0], deckP[1] - meshP[1], deckP[2] - meshP[2]};
    if (std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) > kBoardMaxDist) { why = "the board is away from the skater"; return false; }
    const Quat inv = QInv(mq);
    float x = (float)d[0], y = (float)d[1], z = (float)d[2];
    QRotVec(inv, x, y, z);
    out.p[0] = x; out.p[1] = y; out.p[2] = z;
    out.q = QNorm(QMul(inv, dq));
    return true;
}
