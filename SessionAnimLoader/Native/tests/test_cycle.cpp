// 2026-11-25: step cycle sync (cycle.h): the live tracker (player), the offline cut (editor), the cycle drive.
#include "check.h"
#include "../src/cycle.h"
#include "../src/clip.h"
#include "../src/skeleton.h"
#include "../src/figure.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr float kPi = 3.14159265f;
// a walk: the left foot swings along Y, the right one opposite, plus some lateral noise on X and an offset (the
// push foot of a skater sits behind: the mean is not zero)
void WalkFeet(float t, float T, float& lx, float& ly, float& rx, float& ry) {
    const float s = 25.f * std::sin(2.f * kPi * t / T);
    lx = 10.f + 1.5f * std::sin(7.f * t); ly = 5.f + s;
    rx = -10.f; ry = -5.f - s;
}
float PhaseDiff(float a, float b) { float d = a - b; d -= std::floor(d + 0.5f); return std::fabs(d); }
int CyIdx(const char* n) { auto v = RefNames(); for (int i = 0; i < (int)v.size(); i++) if (v[i] == n) return i; return -1; }
} // namespace

TEST(cycle_tracker_locks_on_the_stride) {
    CycleTracker c;
    const float dt = 1.f / 60.f, T = 1.1f;
    float t = 0.f;
    for (int i = 0; i < 60 * 8; i++) {
        t += dt;
        float lx, ly, rx, ry;
        WalkFeet(t, T, lx, ly, rx, ry);
        c.Step(lx, ly, rx, ry, dt);
    }
    CHECK(c.Locked());
    CHECK_NEAR(c.Period(), T, 0.03);
    // marker = left foot passing the right one going forward = sin crossing zero upward = t multiple of T
    float ph = t / T; ph -= std::floor(ph);
    CHECK(PhaseDiff(c.Phase(), ph) < 0.03f);
}

TEST(cycle_tracker_follows_a_new_pace_without_jumps) {
    CycleTracker c;
    const float dt = 1.f / 60.f;
    float t = 0.f, u = 0.f;   // u: stride phase of the synthetic walk (pace changes)
    float prev = 0.f, worst = 0.f;
    for (int i = 0; i < 60 * 14; i++) {
        const float T = i < 60 * 6 ? 1.2f : 0.8f;   // starts running
        t += dt;
        u += dt / T;
        const float s = 25.f * std::sin(2.f * kPi * u);
        const float ph = c.Step(0.f, s, 0.f, -s, dt);
        if (i > 60 * 4) {   // after the first lock (the second marker sets the phase once)
            float d = ph - prev; d -= std::floor(d + 0.5f);
            if (std::fabs(d) > worst) worst = std::fabs(d);
        }
        prev = ph;
    }
    CHECK_NEAR(c.Period(), 0.8, 0.03);
    float want = u - std::floor(u);
    CHECK(PhaseDiff(c.Phase(), want) < 0.04f);
    CHECK(worst < 0.06f);   // per frame: dt / T is 0.021 at 0.8 s, a correction adds a little, never a jump
}

TEST(cycle_tracker_standing_has_no_marker) {
    CycleTracker c;
    for (int i = 0; i < 600; i++) c.Step(10.f + 0.3f * std::sin(i * 0.1f), 5.f, -10.f, -5.f, 1.f / 60.f);
    CHECK(c.Markers() == 0);
}

TEST(cycle_offline_cut_matches) {
    std::vector<FootSample> s;
    const float T = 0.95f;
    for (int i = 0; i <= 90; i++) {
        const float t = i / 30.f + 0.13f;
        float lx, ly, rx, ry;
        WalkFeet(t, T, lx, ly, rx, ry);
        s.push_back(FootSample{i / 30.f, lx, ly, rx, ry});
    }
    float a = 0.f, b = 0.f;
    CHECK(FirstCycle(s, a, b));
    CHECK_NEAR(b - a, T, 0.02);
    CHECK_NEAR(a, T - 0.13f, 0.02);   // first upward crossing of the recording
    std::vector<FootSample> still;
    for (int i = 0; i <= 90; i++) still.push_back(FootSample{i / 30.f, 10.f, 5.f, -10.f, -5.f});
    CHECK(!FirstCycle(still, a, b));
}

TEST(cycle_drive_format_and_clock) {
    Clip c;
    c.name = "w"; c.states = ST_Walking; c.duration = 0.9f; c.drive = Drive::Cycle;
    Track tr; tr.bone = "AMXX_R_UpperArm"; tr.keys.push_back(Key{0.f, QIdent, true});
    c.tracks.push_back(tr);
    const std::string js = ClipToJson(c);
    CHECK(js.find("\"cycle\"") != std::string::npos);
    Clip back; std::string err;
    CHECK(ParseClip(js, "w", back, err));
    CHECK(back.drive == Drive::Cycle);
    CHECK_NEAR(ClipTime(back, 5.f, 0.f, 0.25f), 0.225, 1e-5);
    CHECK_NEAR(ClipTime(back, 5.f, 0.f, 1.25f), 0.225, 1e-5);
}

TEST(cycle_editor_cuts_one_stride_from_the_figure) {
    SkeletonFile sk; sk.names = RefNames(); sk.parents = RefParents(); sk.refPos = RefPositions(); sk.conv = RefMirror().conv;
    EditorDoc d; std::string err;
    CHECK(d.LoadSkeleton(SkeletonToJson(sk), err));
    FigureRec r; r.figure = "Walking_RGS_regular"; r.bones = (int)RefNames().size();
    const int lt = CyIdx("AMXX_L_Thigh"), rt = CyIdx("AMXX_R_Thigh");
    const float T = 1.0f;
    for (int i = 0; i <= 90; i++) {
        const float t = i / 30.f;
        r.times.push_back(t);
        r.ratios.push_back(0.f);
        const float a = 25.f * std::sin(2.f * kPi * t / T);
        for (int b = 0; b < r.bones; b++)
            // same angle on both thighs: the reference skeleton's right leg frame is mirrored, so the feet go opposite ways
            r.frames.push_back(b == lt ? QFromRotatorDeg(0, a, 0) : b == rt ? QFromRotatorDeg(0, a, 0) : QIdent);
    }
    FigureVariant v; v.state = ST_Walking;
    CHECK(d.LoadFigure(FigureToJson(r), v, err));
    CHECK(d.HasCycle());
    CHECK_NEAR(d.CycleEnd() - d.CycleStart(), T, 0.03);
    d.NewClip(v);
    CHECK(d.clip().drive == Drive::Cycle);   // walking follows the game's steps by default
    CHECK_NEAR(d.clip().duration, d.CycleEnd() - d.CycleStart(), 1e-5);
    // clip time 0 shows the figure at the start of the stride, half the clip shows half a stride later
    CHECK_NEAR(d.FigTime(0.f), d.CycleStart(), 1e-5);
    CHECK_NEAR(d.FigTime(d.clip().duration * 0.5f), d.CycleStart() + 0.5f * (d.CycleEnd() - d.CycleStart()), 1e-4);
    CHECK(d.LoopsInTime());
}

// the user's real recordings, when they are there (prints what the cut finds; no check fails without them)
TEST(cycle_on_the_real_figures) {
    const char* dir = "C:/Program Files (x86)/Steam/steamapps/common/Session/SessionGame/Binaries/Win64/Mods/SessionAnimLoader/clips/";
    auto readAll = [](const std::string& p, std::string& out) {
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) return false;
        char buf[65536];
        size_t k;
        out.clear();
        while ((k = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, k);
        fclose(f);
        return true;
    };
    std::string skel, fig, err;
    if (!readAll(std::string(dir) + "_skeleton.json", skel)) return;
    for (const char* name : {"Walking_RGS_regular", "Pushing_RGS_regular", "OnFoot_RGS_regular"}) {
        if (!readAll(std::string(dir) + "_figures\\" + name + ".json", fig)) continue;
        EditorDoc d;
        if (!d.LoadSkeleton(skel, err)) { printf("real: skeleton %s\n", err.c_str()); return; }
        FigureVariant v;
        CHECK(ParseFigureKey(name, v));
        if (!d.LoadFigure(fig, v, err)) { printf("real: %s %s\n", name, err.c_str()); continue; }
        printf("real: %s -> %s, stride %.3f s (from %.3f)\n", name, d.HasCycle() ? "stride found" : "no stride", d.CycleEnd() - d.CycleStart(), d.CycleStart());
        if (std::string(name) == "Walking_RGS_regular") {
            CHECK(d.HasCycle());
            // every marker of the recording (should be evenly spaced)
            const SkeletonFile& s = d.skeleton();
            int lf = -1, rf = -1;
            for (int i = 0; i < (int)s.names.size(); i++) { if (s.names[i] == "AMXX_L_Foot") lf = i; if (s.names[i] == "AMXX_R_Foot") rf = i; }
            std::vector<FootSample> fs;
            std::vector<FTr> cs;
            for (int i = 0; i <= 90; i++) {
                const float t = i / 30.f;
                d.OriginPose(t, cs);
                fs.push_back(FootSample{t, cs[lf].px, cs[lf].py, cs[rf].px, cs[rf].py});
            }
            printf("real: markers");
            for (float m : CycleMarkers(fs)) printf(" %.3f", m);
            printf("\n");
        }
    }
}
