// Original animations of figures (format + recorder). P6: recorder driven by GameState.figId / figWanted.
#include "check.h"
#include "../src/figure.h"
#include <vector>

namespace {
constexpr int kBones = 70;
constexpr float kDt = 1.f / 60.f;

std::vector<Quat> Pose(float phase) {   // a pose that changes with time
    std::vector<Quat> q(kBones);
    for (int i = 0; i < kBones; i++) q[i] = QFromRotatorDeg(phase * 10.f, (float)i, 0.f);
    return q;
}
// manual -> figure 1, nose manual -> figure 2 ; wanted: which figures must be recorded
GameState GS(bool manual, bool nose, bool want1 = true, bool want2 = true) {
    GameState g{manual, nose, false, false, 0.4f};
    g.figId = manual ? 1 : nose ? 2 : 0;
    g.figWanted = (manual && want1) || (nose && want2);
    return g;
}

float Run(FigureRecorder& r, GameState gs, float seconds, float t0 = 0.f) {
    const int n = (int)(seconds / kDt + 0.5f);
    float t = t0;
    float pos[kBones][3] = {};
    for (int i = 0; i < n; i++) {
        gs.manualRatio = 0.3f + 0.001f * (float)i;
        const auto p = Pose(t);
        pos[2][2] = 90.f + t;
        r.Tick(gs, p.data(), pos, kBones, kDt);
        t += kDt;
    }
    return t;
}
} // namespace

TEST(figure_json_roundtrip) {
    FigureRec a;
    a.figure = "NoseManual_RGS_regular"; a.bones = 3;
    a.times = {0.f, 1.f / 30.f}; a.ratios = {0.25f, 0.5f};
    a.frames = {QIdent, QFromRotatorDeg(10, 0, 0), QFromRotatorDeg(0, 20, 0), QFromRotatorDeg(0, 0, 30), QIdent, QIdent};
    a.posBones = {0, 2};
    a.positions = {1, 2, 3, 4, 5, 6, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f};
    FigureRec b; std::string err;
    CHECK(ParseFigure(FigureToJson(a), b, err));
    CHECK(b.figure == "NoseManual_RGS_regular" && b.bones == 3 && b.version == 1);
    CHECK(b.times.size() == 2 && b.ratios.size() == 2 && b.frames.size() == 6);
    if (b.frames.size() == 6) for (int i = 0; i < 6; i++) CHECK(std::fabs(QDot(a.frames[i], b.frames[i])) > 0.99999f);
    CHECK_NEAR(b.ratios[1], 0.5, 1e-6);
    CHECK(b.posBones == a.posBones && b.positions.size() == 12);
    if (b.positions.size() == 12) CHECK_NEAR(b.positions[11], 6.5, 1e-4);
}

TEST(figure_parse_rejects_bad) {
    FigureRec a; a.figure = "Manual"; a.bones = 2; a.times = {0.f, 0.1f}; a.ratios = {0, 0}; a.frames = {QIdent, QIdent, QIdent, QIdent};
    FigureRec b; std::string err;
    FigureRec u = a; u.figure = "";
    CHECK(!ParseFigure(FigureToJson(u), b, err)); CHECK(!err.empty());
    FigureRec one = a; one.times = {0.f}; one.ratios = {0.f}; one.frames = {QIdent, QIdent};
    err.clear(); CHECK(!ParseFigure(FigureToJson(one), b, err)); CHECK(!err.empty());
    FigureRec bad = a; bad.frames.pop_back();
    err.clear(); CHECK(!ParseFigure(FigureToJson(bad), b, err)); CHECK(!err.empty());
    err.clear(); CHECK(!ParseFigure("{", b, err)); CHECK(!err.empty());
    // inconsistent positions are dropped, the figure stays usable
    FigureRec p = a; p.posBones = {1}; p.positions = {1, 2};
    err.clear(); CHECK(ParseFigure(FigureToJson(p), b, err) && b.posBones.empty());
}

TEST(position_bones) {
    CHECK(IsPositionBone("AMXX_Pelvis") && IsPositionBone("SKXX_SkateSkel_Flipper"));
    CHECK(!IsPositionBone("AMXX_L_Calf") && !IsPositionBone("AMXX_Root"));
}

TEST(recorder_records_three_seconds_at_30fps) {
    FigureRecorder r; r.Reset(kBones, {2, 59});
    Run(r, GS(false, false), 0.2f);
    Run(r, GS(true, false), 4.f);
    FigureRec out; int id = 0;
    CHECK(r.TakeFinished(out, id));
    CHECK(id == 1 && out.bones == kBones && out.phases.empty());
    CHECK(out.times.size() == 91 && out.frames.size() == 91u * kBones && out.ratios.size() == 91);
    CHECK(out.posBones.size() == 2 && out.positions.size() == 91u * 2 * 3);
    if (out.positions.size() > 2) CHECK(out.positions[2] > 89.f);   // pelvis z
    for (size_t i = 1; i < out.times.size(); i++) CHECK(out.times[i] > out.times[i - 1]);
    if (!out.times.empty()) CHECK_NEAR(out.times.back(), 3.0, 1e-3);
    CHECK(!r.TakeFinished(out, id));   // only once
}

TEST(recorder_short_figure_discarded_and_retried) {
    FigureRecorder r; r.Reset(kBones);
    Run(r, GS(true, false), 0.3f);
    Run(r, GS(false, false), 0.2f);
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    Run(r, GS(true, false), 2.5f);            // under 3 s: dropped too
    Run(r, GS(false, false), 0.1f);
    CHECK(!r.TakeFinished(out, id));
    Run(r, GS(true, false), 3.5f);
    Run(r, GS(false, false), 0.1f);
    CHECK(r.TakeFinished(out, id));
    CHECK(out.times.size() == 91);
}

TEST(recorder_records_goofy_as_is) {   // P6: every variant is recorded, goofy included (its own figure id)
    FigureRecorder r; r.Reset(kBones);
    GameState g = GS(true, false);
    g.goofy = true; g.figId = 7;
    Run(r, g, 3.5f);
    Run(r, GS(false, false), 0.1f);
    FigureRec out; int id = 0;
    CHECK(r.TakeFinished(out, id) && id == 7);
}

TEST(recorder_needs_rising_edge) {
    FigureRecorder r; r.Reset(kBones);
    Run(r, GS(true, false, false), 0.5f);
    Run(r, GS(true, false), 1.f);   // manual already on when it became wanted
    Run(r, GS(false, false), 0.1f);
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    Run(r, GS(true, false), 3.5f);
    Run(r, GS(false, false), 0.1f);
    CHECK(r.TakeFinished(out, id));
}

TEST(recorder_switching_figure_stops) {
    FigureRecorder r; r.Reset(kBones);
    Run(r, GS(true, false), 1.f);
    Run(r, GS(false, true), 3.5f);    // manual -> nose manual: the manual (too short) is dropped, the nose manual starts
    Run(r, GS(false, false), 0.1f);
    FigureRec out; int id = 0;
    CHECK(r.TakeFinished(out, id) && id == 2);
}

TEST(recorder_reset_discards_partial) {
    FigureRecorder r; r.Reset(kBones);
    Run(r, GS(true, false), 0.8f);
    r.Reset(kBones);
    Run(r, GS(true, false), 0.3f);
    Run(r, GS(false, false), 0.1f);
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    Run(r, GS(true, false), 0.6f);
    const auto p = Pose(0);
    for (int i = 0; i < 60; i++) r.Tick(GS(true, false), p.data(), nullptr, kBones - 1, kDt);
    Run(r, GS(false, false), 0.1f);
    CHECK(!r.TakeFinished(out, id));
}

TEST(recorder_only_wanted_figures) {
    FigureRecorder r; r.Reset(kBones);
    Run(r, GS(false, true, true, false), 3.5f);
    Run(r, GS(false, false), 0.1f);
    FigureRec out; int id = 0;
    CHECK(!r.TakeFinished(out, id));
    Run(r, GS(false, true), 3.5f);
    Run(r, GS(false, false), 0.1f);
    CHECK(r.TakeFinished(out, id) && id == 2);
}
