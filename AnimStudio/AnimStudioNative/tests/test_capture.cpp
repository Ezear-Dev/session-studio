#include "check.h"
#include "../src/capture.h"
#include "../src/player.h"
#include <memory>

namespace {

float AngDeg(Quat a, Quat b) {
    const Quat d = QMul(QInv(QNorm(a)), QNorm(b));
    double s = std::sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    if (s > 1.0) s = 1.0;
    double ang = 2.0 * std::asin(s) * 57.29577951308232;
    if (d.w < 0) ang = 360.0 - ang;
    if (ang > 180.0) ang = 360.0 - ang;
    return (float)ang;
}

struct Pose { int n; std::vector<int> parent; std::vector<FTr> cs; };

Pose MakePose() {   // game pose: reference positions, identity rotations
    Pose p;
    const auto& s = RefSkeleton();
    p.n = (int)s.size();
    p.parent.resize(p.n);
    std::vector<Quat> lq(p.n, QIdent);
    std::vector<float> lp(p.n * 3);
    for (int i = 0; i < p.n; i++) { p.parent[i] = s[i].parent; lp[i * 3] = s[i].x; lp[i * 3 + 1] = s[i].y; lp[i * 3 + 2] = s[i].z; }
    p.cs.assign(p.n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    std::vector<char> dirty(p.n, 1);
    ToComponent(p.cs.data(), p.parent.data(), p.n, lq.data(), (const float (*)[3])lp.data(), (const bool*)dirty.data());
    return p;
}

std::vector<Quat> Captured(int n) {   // an arbitrary captured pose, different from the game's one
    std::vector<Quat> q(n);
    for (int i = 0; i < n; i++) q[i] = QFromRotatorDeg((float)(i % 9) * 4.f, (float)(i % 4) * -6.f, (float)(i % 5) * 3.f);
    return q;
}

// replays the capture in nose manual, at full strength, and returns the local rotations obtained
std::vector<Quat> Replay(const std::string& json, bool goofy) {
    Clip c; std::string err;
    CHECK(ParseClip(json, "cap.sanim", c, err));
    CHECK(BindClip(c, RefNames(), err));
    c.enabled = true;
    c.states = ST_NoseManual;
    ClipSet set; set.generation = 1; set.clips.push_back(c);
    Pose p = MakePose();
    auto pl = std::make_unique<Player>();
    pl->SetRig(p.parent.data(), p.n, RefMirror());
    std::vector<FTr> cs;
    for (int f = 0; f < 40; f++) { cs = p.cs; pl->Apply(cs.data(), p.n, &set, GameState{false, true, goofy, false, 0.f}, 1.f / 60.f); }
    std::vector<Quat> lq(p.n);
    std::vector<float> lp(p.n * 3);
    ToLocal(cs.data(), p.parent.data(), p.n, lq.data(), (float (*)[3])lp.data());
    return lq;
}

} // namespace

TEST(capture_roundtrip_replace_reproduces_pose) {
    const auto names = RefNames();
    const auto cap = Captured((int)names.size());
    const std::string json = CaptureToJson(cap.data(), names, false, RefMirror(), RefMirror().conv, "cap");
    Clip c; std::string err;
    CHECK(ParseClip(json, "cap.sanim", c, err));
    CHECK(!c.enabled && c.mode == Mode::Replace && c.states == ST_Manual && c.loop && c.mirror);
    CHECK(json.find("\"capturedStance\": \"regular\"") != std::string::npos);
    const auto got = Replay(json, false);
    for (int i = 0; i < 58; i++) CHECK(AngDeg(got[i], cap[i]) < 0.02f);
}

TEST(capture_in_goofy_is_stored_regular_and_replays_back) {
    const auto names = RefNames();
    const auto cap = Captured((int)names.size());
    const std::string json = CaptureToJson(cap.data(), names, true, RefMirror(), RefMirror().conv, "cap");
    CHECK(json.find("\"capturedStance\": \"goofy\"") != std::string::npos);
    // replayed in goofy, it goes through the same mirror again and lands on the captured pose
    const std::string jsonDefault = CaptureToJson(cap.data(), names, true, RefMirror(), RefMirror().conv, "cap");
    const auto got = Replay(jsonDefault, true);
    for (int i = 0; i < 58; i++) CHECK(AngDeg(got[i], cap[i]) < 0.02f);
}

TEST(capture_has_no_board_bones) {
    const auto names = RefNames();
    const auto cap = Captured((int)names.size());
    const std::string json = CaptureToJson(cap.data(), names, false, RefMirror(), RefMirror().conv, "cap");
    CHECK(json.find("SKXX_") == std::string::npos);
    CHECK(json.find("AMXX_Pelvis") != std::string::npos);
}
