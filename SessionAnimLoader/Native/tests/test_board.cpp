// The visible board (2026-11-27): conversion into the mesh's space, its track in the move recordings.
#include "check.h"
#include "../src/board.h"
#include "../src/figure.h"
#include <cmath>
#include <limits>
#include <vector>

namespace {
// first line of the probe of 2026-11-27 (regular stance, rolling): world values read in game
const double kRiding[kBoardValues] = {2357.358, 2078.392, 138.297, 0.000013, -0.000007, 0.316290, 0.948663, 1, 1, 1,
                                      2357.358, 2078.392, 145.297, 0.000194, 0.000058, 0.316247, 0.948677};
float AngleDeg(Quat a, Quat b) { return 2.f * std::acos(std::fmin(1.f, std::fabs(QDot(a, b)))) * 57.29578f; }
void Copy(double* d) { for (int i = 0; i < kBoardValues; i++) d[i] = kRiding[i]; }

GameState Trick(uint32_t pop, bool board, float x) {
    GameState g{false, false, false, false, 0.f, true, pop, 1};
    g.figId = 1; g.figWanted = true; g.boardOk = board;
    g.board.p[0] = x; g.board.p[2] = 7.f;
    return g;
}
bool Record(int holeAt, FigureRec& out) {   // a one second trick, then the ground; holeAt: frame without a board
    FigureRecorder r; r.Reset(4, {});
    const std::vector<Quat> q(4, QIdent);
    float pos[4][3] = {};
    GameState idle{false, false, false, false, 0.f};
    r.Tick(idle, q.data(), pos, 4, 1.f / 30.f);
    for (int i = 0; i < 30; i++) r.Tick(Trick(1, i != holeAt, (float)i), q.data(), pos, 4, 1.f / 30.f);
    GameState ground = Trick(1, true, 99.f);
    ground.kickflip = false;
    for (int i = 0; i < 80; i++) r.Tick(ground, q.data(), pos, 4, 1.f / 30.f);
    int id = 0;
    return r.TakeFinished(out, id);
}
} // namespace

TEST(board_in_mesh_space_matches_the_probe_while_riding) {
    BoardPose b; const char* why = "";
    CHECK(BoardInMesh(kRiding, b, why));
    CHECK_NEAR(b.p[0], 0.0, 0.01);
    CHECK_NEAR(b.p[1], 0.0, 0.01);
    CHECK_NEAR(b.p[2], 7.0, 0.01);
    CHECK(AngleDeg(b.q, QIdent) < 0.1f);
}

TEST(board_in_mesh_space_with_a_turned_skater) {
    double in[kBoardValues]; Copy(in);
    const Quat mq = QFromRotatorDeg(0, 90, 0);   // the skater looks along world +Y
    const Quat dq = QMul(mq, QFromRotatorDeg(0, 0, 30));
    in[0] = 100; in[1] = 200; in[2] = 50;
    in[3] = mq.x; in[4] = mq.y; in[5] = mq.z; in[6] = mq.w;
    in[10] = 100; in[11] = 230; in[12] = 57;       // 30 cm ahead along world Y = 30 cm along the mesh's X
    in[13] = dq.x; in[14] = dq.y; in[15] = dq.z; in[16] = dq.w;
    BoardPose b; const char* why = "";
    CHECK(BoardInMesh(in, b, why));
    CHECK_NEAR(b.p[0], 30.0, 0.01);
    CHECK_NEAR(b.p[1], 0.0, 0.01);
    CHECK_NEAR(b.p[2], 7.0, 0.01);
    CHECK(AngleDeg(b.q, QFromRotatorDeg(0, 0, 30)) < 0.1f);
}

TEST(board_refused_when_unreadable_scaled_or_far) {
    BoardPose b; const char* why = "";
    double in[kBoardValues];
    Copy(in); in[12] = std::numeric_limits<double>::quiet_NaN();
    CHECK(!BoardInMesh(in, b, why));
    Copy(in); in[13] = in[14] = in[15] = in[16] = 0;
    CHECK(!BoardInMesh(in, b, why));
    Copy(in); in[8] = -1;   // a mirrored mesh
    CHECK(!BoardInMesh(in, b, why) && std::string(why) == "the skater mesh is scaled");
    Copy(in); in[10] += 250;
    CHECK(!BoardInMesh(in, b, why) && std::string(why) == "the board is away from the skater");
}

TEST(recorder_keeps_the_board_with_every_frame) {
    FigureRec out;
    CHECK(Record(-1, out));
    CHECK(!out.board.empty() && out.board.size() == out.times.size());
    if (out.board.size() > 10) CHECK(out.board[10].p[0] > out.board[0].p[0] && std::fabs(out.board[10].p[2] - 7.f) < 1e-4f);
}

TEST(recorder_leaves_the_board_out_when_one_frame_misses_it) {
    FigureRec out;
    CHECK(Record(12, out));
    CHECK(out.board.empty() && out.times.size() > 20);
    CHECK(Record(0, out) && out.board.empty());     // the very first frame counts too
    CHECK(Record(-1, out) && !out.board.empty());   // and the next recording starts clean
}

TEST(figure_board_track_round_trip_and_optional) {
    FigureRec a; a.figure = "KickFlip_RGS_regular"; a.bones = 1;
    a.times = {0.f, 1.f / 30.f}; a.ratios = {0, 0}; a.frames = {QIdent, QIdent};
    FigureRec b; std::string err;
    const std::string without = FigureToJson(a);
    CHECK(without.find("\"board\"") == std::string::npos);
    CHECK(ParseFigure(without, b, err) && b.board.empty());
    a.board.resize(2);
    a.board[0].p[0] = 1.25f; a.board[0].p[2] = 7.f;
    a.board[1].p[1] = -3.5f; a.board[1].q = QFromRotatorDeg(10, 20, 170);
    CHECK(ParseFigure(FigureToJson(a), b, err) && b.board.size() == 2);
    if (b.board.size() == 2) {
        CHECK_NEAR(b.board[0].p[0], 1.25, 0.006);
        CHECK_NEAR(b.board[1].p[1], -3.5, 0.006);
        CHECK(AngleDeg(a.board[1].q, b.board[1].q) < 0.1f);
    }
    a.board.resize(1);   // not one entry per frame: ignored, the recording stays usable
    CHECK(ParseFigure(FigureToJson(a), b, err) && b.board.empty() && b.times.size() == 2);
}

// ------------------------------------------------------------------ the editor
#include "../src/skeleton.h"
#include "../../../SessionStudio/model/board_mesh.h"
#include "../../../SessionStudio/model/editor_doc.h"

namespace {
// a loop recording of the reference body whose board makes one full turn about its length in 12 frames
FigureRec FlipFigure(int bones, bool turnedRound) {
    FigureRec f;
    f.figure = "Riding_RGS_regular"; f.bones = bones;
    const Quat half{0.f, 0.f, 1.f, 0.f};
    for (int i = 0; i <= 12; i++) {
        f.times.push_back((float)i / 30.f);
        f.ratios.push_back(0.f);
        for (int b = 0; b < bones; b++) f.frames.push_back(QIdent);
        BoardPose p;
        p.p[2] = 7.f; p.p[0] = (float)i;
        p.q = QFromRotatorDeg(0.f, 0.f, 30.f * (float)i);
        if (turnedRound) p.q = QMul(half, p.q);
        f.board.push_back(p);
    }
    return f;
}
bool LoadDoc(EditorDoc& d, const FigureRec& f) {
    SkeletonFile s; s.names = RefNames(); s.parents = RefParents(); s.refPos = RefPositions(); s.conv = RefMirror().conv;
    std::string err;
    if (!d.LoadSkeleton(SkeletonToJson(s), err)) return false;
    return d.LoadFigure(FigureToJson(f), err);
}
float AngleBetween(Quat a, Quat b) { return 2.f * std::acos(std::fmin(1.f, std::fabs(QDot(a, b)))) * 57.29578f; }
} // namespace

TEST(editor_board_interpolates_a_flip_without_spinning_back) {
    EditorDoc d;
    CHECK(LoadDoc(d, FlipFigure((int)RefNames().size(), false)));
    CHECK(d.HasRealBoard());
    BoardPose prev, cur;
    CHECK(d.OriginBoard(0.f, prev));
    float turned = 0.f, biggest = 0.f;
    for (int i = 1; i <= 120; i++) {   // ten reads between two frames
        CHECK(d.OriginBoard(0.4f * (float)i / 120.f - 1e-6f, cur));
        const float a = AngleBetween(prev.q, cur.q);
        turned += a;
        biggest = std::fmax(biggest, a);
        prev = cur;
    }
    CHECK(biggest < 4.f);                  // 3 degrees per read
    CHECK(std::fabs(turned - 360.f) < 3.f);
    CHECK(d.OriginBoard(2.5f / 30.f, cur));
    CHECK_NEAR(cur.p[0], 2.5, 0.01);
    CHECK(AngleBetween(cur.q, QFromRotatorDeg(0.f, 0.f, 75.f)) < 0.5f);
}

TEST(editor_board_follows_the_time_of_the_pose) {
    EditorDoc d;
    CHECK(LoadDoc(d, FlipFigure((int)RefNames().size(), false)));
    BoardPose a, b;
    CHECK(d.OriginBoard(0.1f, a) && d.OriginBoard(0.5f, b));   // a loop wraps, as the pose does
    CHECK_NEAR(a.p[0], b.p[0], 0.01);
    CHECK(d.BoardAt(0.2f, a) && d.OriginBoard(d.FigTime(0.2f), b));
    CHECK_NEAR(a.p[0], b.p[0], 1e-4);
}

TEST(editor_board_turned_round_by_an_earlier_trick_keeps_its_nose_forward) {
    EditorDoc straight, turned;
    CHECK(LoadDoc(straight, FlipFigure((int)RefNames().size(), false)));
    CHECK(LoadDoc(turned, FlipFigure((int)RefNames().size(), true)));
    BoardPose a, b;
    CHECK(straight.OriginBoard(0.05f, a) && turned.OriginBoard(0.05f, b));
    float ax = 1.f, ay = 0.f, az = 0.f, bx = 1.f, by = 0.f, bz = 0.f;
    QRotVec(a.q, ax, ay, az);
    QRotVec(b.q, bx, by, bz);
    CHECK(ax * bx + ay * by + az * bz > 0.99f);
}

TEST(editor_without_a_recorded_board) {
    EditorDoc d;
    FigureRec f = FlipFigure((int)RefNames().size(), false);
    f.board.clear();
    CHECK(LoadDoc(d, f));
    BoardPose p;
    CHECK(!d.HasRealBoard() && !d.OriginBoard(0.f, p) && !d.BoardAt(0.f, p));
}

TEST(real_board_mesh_rides_on_the_ground) {
    BoardPose b; const char* why = "";
    CHECK(BoardInMesh(kRiding, b, why));
    Mesh m;
    CHECK(BuildRealBoardMesh(b, 59, m) && !m.verts.empty());
    float wheelLow = 1e9f, gripLow = 1e9f, gripUp = 0.f, noseX = 0.f;
    int grip = 0, nose = 0;
    for (const auto& v : m.verts) {
        if (v.rgba == kBoardWheel) wheelLow = std::fmin(wheelLow, v.pos[2]);
        if (v.rgba == kBoardGrip) { gripLow = std::fmin(gripLow, v.pos[2]); gripUp += v.nrm[2]; grip++; }
        if (v.rgba == kBoardNose) { noseX += v.pos[0]; nose++; }
    }
    CHECK(std::fabs(wheelLow) < 0.5f);                 // wheels on the floor
    CHECK(gripLow > 8.5f && gripLow < 10.f);           // the toe bones are at 11 cm while riding
    CHECK(grip > 0 && gripUp / grip > 0.9f);
    CHECK(nose > 0 && noseX / nose > 25.f);
    b.q = QFromRotatorDeg(0.f, 0.f, 180.f); b.p[2] = 40.f;   // upside down in the air: it stays where it was recorded
    CHECK(BuildRealBoardMesh(b, 59, m));
    float up = 0.f; int k = 0;
    for (const auto& v : m.verts) if (v.rgba == kBoardGrip) { up += v.nrm[2]; k++; }
    CHECK(k > 0 && up / k < -0.9f);
}

TEST(editor_board_nose_stays_continuous_into_the_landing_of_a_shove_it) {
    FigureRec trick = FlipFigure((int)RefNames().size(), false);
    trick.figure = "BS_PopShuvIt_RGS_regular";
    trick.board.clear();
    for (size_t i = 0; i < trick.times.size(); i++) {   // a half spin in the first half, then on the ground
        BoardPose p; p.p[2] = 7.f;
        p.q = QFromRotatorDeg(0.f, i < 6 ? 30.f * (float)i : 180.f, 0.f);
        trick.board.push_back(p);
        trick.phases.push_back(i < 6 ? 1 : FigureRecorder::kGroundPhase);
    }
    EditorDoc whole;
    CHECK(LoadDoc(whole, trick));
    BoardPose atGround;
    CHECK(whole.OriginBoard(6.f / 30.f, atGround));
    // the landing part, cut the way the app does it
    FigureRec faced = trick, tail;
    whole.FaceBoardForward(faced);
    CHECK(GroundTailFigure(faced, tail));
    CHECK(tail.board.size() == tail.times.size() && !tail.board.empty());
    tail.figure = "Landing_BS_PopShuvIt_RGS_regular";
    EditorDoc landing;
    CHECK(LoadDoc(landing, tail));
    BoardPose first;
    CHECK(landing.OriginBoard(0.f, first));
    CHECK(AngleBetween(first.q, atGround.q) < 0.5f);
}
