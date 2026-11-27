// Session Studio -- EditorDoc: the game's visible board (2026-11-27), recorded with the move (FigureRec::board). The
// time rules are the ones of the pose it is shown with: OriginBoard as OriginPose, BoardAt as ComponentPose.
#include "editor_doc.h"
#include <algorithm>
#include <cmath>

namespace {
// the board at figure time u (already inside the recording)
BoardPose SampleBoard(const FigureRec& r, float u) {
    const int n = (int)r.board.size();
    int i = 0;
    while (i + 1 < n && r.times[(size_t)i + 1] <= u) i++;
    const int j = i + 1 < n ? i + 1 : i;
    const float span = r.times[j] - r.times[i];
    const float a = span > 1e-6f ? std::clamp((u - r.times[i]) / span, 0.f, 1.f) : 0.f;
    const BoardPose &p0 = r.board[i], &p1 = r.board[j];
    BoardPose out;
    for (int k = 0; k < 3; k++) out.p[k] = p0.p[k] + (p1.p[k] - p0.p[k]) * a;
    out.q = QSlerp(p0.q, p1.q, a);   // shortest path: a flip moves well under 180 degrees between two frames
    return out;
}
bool Usable(const FigureRec& r) { return !r.board.empty() && r.board.size() == r.times.size(); }
void XAxis(Quat q, float v[3]) {
    v[0] = 1.f; v[1] = 0.f; v[2] = 0.f;
    QRotVec(q, v[0], v[1], v[2]);
}
Quat HalfTurn(Quat q) { return QNorm(QMul(q, Quat{0.f, 0.f, 1.f, 0.f})); }   // 180 degrees about the deck's own up axis
} // namespace

void EditorDoc::FaceBoardForward(FigureRec& rec) const {
    if (rec.boardFaced || !Usable(rec) || rec.bones != BoneCount() || rec.frames.size() < (size_t)rec.bones) return;
    int deck = -1;
    for (int i = 0; i < BoneCount(); i++) if (skel_.names[i] == "SKXX_SkateSkel_Flipper") { deck = i; break; }
    if (deck < 0) return;
    Quat boneQ = QIdent;   // the deck bone in component space on the first frame: its chain up to the root
    for (int i = deck; i >= 0; i = skel_.parents[i]) boneQ = QMul(rec.frames[(size_t)i], boneQ);
    float bone[3], board[3];
    XAxis(QNorm(boneQ), bone);
    XAxis(rec.board.front().q, board);
    if (bone[0] * board[0] + bone[1] * board[1] + bone[2] * board[2] < 0.f)
        for (BoardPose& p : rec.board) p.q = HalfTurn(p.q);
    rec.boardFaced = true;
}

bool EditorDoc::OriginBoard(float tOrigin, BoardPose& out) const {
    if (!HasFigure() || !Usable(fig_)) return false;
    const float period = fig_.times.back();
    float u = std::isfinite(tOrigin) ? tOrigin : 0.f;
    if (IsTrickClip() || IsLandingClip() || !(period > 1e-6f)) u = std::clamp(u, 0.f, period > 0.f ? period : 0.f);
    else if (u < 0.f || u > period) { u = std::fmod(u, period); if (u < 0.f) u += period; }
    out = SampleBoard(fig_, u);
    return true;
}

bool EditorDoc::BoardAt(float t, BoardPose& out) const {
    if (!InDismount(t)) return OriginBoard(FigTime(t), out);
    if (!HasDismountFigure() || !Usable(dis_)) return false;
    out = SampleBoard(dis_, std::clamp(std::isfinite(t) ? t - LoopLength(clip_) : 0.f, 0.f, dis_.times.back()));
    // the dismount is its own recording: its nose mark carries on from the loop's (both start with the feet on the board)
    BoardPose loop;
    if (OriginBoard(0.f, loop)) {
        float a[3], b[3];
        XAxis(loop.q, a);
        XAxis(dis_.board.front().q, b);
        if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] < 0.f) out.q = HalfTurn(out.q);
    }
    return true;
}
