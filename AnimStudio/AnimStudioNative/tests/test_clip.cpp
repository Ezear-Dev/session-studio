#include "check.h"
#include "../src/clip.h"

static float AngDeg(Quat a, Quat b) {
    float d = std::fabs(QDot(QNorm(a), QNorm(b)));
    if (d > 1.f) d = 1.f;
    return 2.f * std::acos(d) * 57.2957795f;
}

static const char* kSpec = R"({
  "format": "sanim", "version": 1,
  "name": "manual_bras_ouverts",
  "enabled": true,
  "trigger": { "states": ["Manual"], "priority": 0 },
  "play":    { "drive": "time", "duration": 1.6, "loop": true, "ratioRange": [0.0, 1.0] },
  "blend":   { "in": 0.25, "out": 0.30 },
  "mode": "additive",
  "mirrorForGoofy": true,
  "weights": { "AMXX_L_UpperArm": 0.5 },
  "tracks": {
    "AMXX_L_UpperArm": [ { "t": 0.0, "rot": [0, 0, 20], "interp": "smooth" },
                         { "t": 0.8, "rot": [0, 0, 35], "interp": "smooth" } ],
    "AMXX_R_UpperArm": [ { "t": 0.0, "q": [0, 0, 0.17, 0.985] } ]
  }
})";

// minimal valid clip, with one fragment replaced: used by the refusal cases
static std::string Mk(const char* trigger, const char* play, const char* blend, const char* mode, const char* tracks, const char* extra = "") {
    std::string s = "{\"format\":\"sanim\",\"version\":1,\"name\":\"x\",";
    s += "\"trigger\":"; s += trigger; s += ",\"play\":"; s += play; s += ",\"blend\":"; s += blend;
    s += ",\"mode\":"; s += mode; s += ",\"tracks\":"; s += tracks; s += extra; s += "}";
    return s;
}
static const char* T_OK = "{\"states\":[\"Manual\"]}";
static const char* P_OK = "{\"drive\":\"time\",\"duration\":1}";
static const char* B_OK = "{\"in\":0.2,\"out\":0.2}";
static const char* M_OK = "\"additive\"";
static const char* K_OK = "{\"AMXX_L_UpperArm\":[{\"t\":0,\"rot\":[0,0,10]}]}";

static bool Refused(const std::string& text, const char* fieldInErr) {
    Clip c; std::string err;
    const bool ok = ParseClip(text, "f.sanim", c, err);
    if (ok) return false;
    if (fieldInErr && err.find(fieldInErr) == std::string::npos) { std::printf("    err without '%s': %s\n", fieldInErr, err.c_str()); return false; }
    return true;
}

TEST(clip_parses_spec_example_with_defaults) {
    Clip c; std::string err;
    CHECK(ParseClip(kSpec, "manual.sanim", c, err));
    CHECK(c.name == "manual_bras_ouverts" && c.file == "manual.sanim");
    CHECK(c.enabled && c.states == ST_Manual && c.priority == 0);
    CHECK(c.drive == Drive::Time && c.loop);
    CHECK_NEAR(c.duration, 1.6, 1e-6); CHECK_NEAR(c.r0, 0, 1e-9); CHECK_NEAR(c.r1, 1, 1e-9);
    CHECK_NEAR(c.blendIn, 0.25, 1e-6); CHECK_NEAR(c.blendOut, 0.30, 1e-6);
    CHECK(c.mode == Mode::Additive && c.mirror);
    CHECK(c.tracks.size() == 2);
    const Track* l = nullptr; const Track* r = nullptr;
    for (auto& t : c.tracks) { if (t.bone == "AMXX_L_UpperArm") l = &t; if (t.bone == "AMXX_R_UpperArm") r = &t; }
    CHECK(l && r);
    if (l && r) {
        CHECK(l->keys.size() == 2 && l->keys[0].smooth);
        CHECK(AngDeg(l->keys[0].q, QFromRotatorDeg(0, 0, 20)) < 0.01f);
        CHECK_NEAR(l->weight, 0.5, 1e-6); CHECK_NEAR(r->weight, 1.0, 1e-6);
        CHECK_NEAR(std::sqrt(QDot(r->keys[0].q, r->keys[0].q)), 1.0, 1e-5);   // q normalised
        CHECK(r->keys[0].smooth);   // default interp: smooth
    }
}

TEST(clip_defaults_when_optional_fields_absent) {
    Clip c; std::string err;
    CHECK(ParseClip("{\"format\":\"sanim\",\"version\":1,\"name\":\"x\",\"trigger\":{\"states\":[\"NoseManual\",\"Manual\"]},\"play\":{\"duration\":1},\"mode\":\"replace\",\"tracks\":{\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,1],\"interp\":\"linear\"}]},\"capturedStance\":\"goofy\",\"note\":1}", "a", c, err));
    CHECK(err.empty());
    CHECK(c.enabled && c.priority == 0 && c.loop && c.drive == Drive::Time);
    CHECK(c.states == (ST_Manual | ST_NoseManual));
    CHECK_NEAR(c.blendIn, 0.25, 1e-6); CHECK_NEAR(c.blendOut, 0.30, 1e-6);
    CHECK(c.mirror && c.mode == Mode::Replace);
    CHECK(!c.tracks[0].keys[0].smooth);
}

TEST(clip_refusals_name_the_field) {
    CHECK(Refused("{\"format\":\"other\",\"version\":1}", "format"));
    CHECK(Refused("{\"format\":\"sanim\",\"version\":2}", "version"));
    CHECK(Refused(Mk("{\"states\":[]}", P_OK, B_OK, M_OK, K_OK), "states"));
    CHECK(Refused(Mk("{\"states\":[\"Foo\"]}", P_OK, B_OK, M_OK, K_OK), "states"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, "\"blend\"", K_OK), "mode"));
    CHECK(Refused(Mk(T_OK, "{\"drive\":\"vitesse\",\"duration\":1}", B_OK, M_OK, K_OK), "drive"));
    CHECK(Refused(Mk(T_OK, "{\"duration\":0}", B_OK, M_OK, K_OK), "duration"));
    CHECK(Refused(Mk(T_OK, "{\"duration\":-1}", B_OK, M_OK, K_OK), "duration"));
    CHECK(Refused(Mk(T_OK, "{\"duration\":\"x\"}", B_OK, M_OK, K_OK), "duration"));
    CHECK(Refused(Mk(T_OK, P_OK, "{\"in\":-0.1}", M_OK, K_OK), "blend"));
    CHECK(Refused(Mk(T_OK, "{\"duration\":1,\"ratioRange\":[0,0]}", B_OK, M_OK, K_OK), "ratioRange"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[]}"), "AMXX_Head"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0.5,\"rot\":[0,0,0]},{\"t\":0.5,\"rot\":[0,0,1]}]}"), "t"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":1.5,\"rot\":[0,0,0]}]}"), "t"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0,\"rot\":[0,0,0],\"q\":[0,0,0,1]}]}"), "rot"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0}]}"), "rot"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0,\"rot\":[0,0,1e30]}]}"), "rot"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,0,0]}]}"), "q"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0,\"rot\":[0,0]}]}"), "rot"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, K_OK, ",\"weights\":{\"AMXX_L_UpperArm\":1.5}"), "weights"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{}"), "tracks"));
    CHECK(Refused("{\"format\":\"sanim\",", nullptr));   // broken JSON
}

TEST(clip_rejects_nan_never_reaches_game) {
    // NaN cannot be written in JSON; a huge number (1e400 -> inf) must be refused
    CHECK(Refused(Mk(T_OK, "{\"duration\":1e400}", B_OK, M_OK, K_OK), "duration"));
    CHECK(Refused(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Head\":[{\"t\":0,\"q\":[0,0,1e400,1]}]}"), "q"));
}

TEST(clip_bind_with_reference_skeleton) {
    Clip c; std::string err;
    CHECK(ParseClip(kSpec, "m", c, err));
    CHECK(BindClip(c, RefNames(), err));
    for (auto& t : c.tracks) if (t.bone == "AMXX_L_UpperArm") CHECK(t.index == 15);
    Clip d;
    CHECK(ParseClip(Mk(T_OK, P_OK, B_OK, M_OK, "{\"AMXX_Foo\":[{\"t\":0,\"rot\":[0,0,0]}]}"), "d", d, err));
    err.clear();
    CHECK(!BindClip(d, RefNames(), err)); CHECK(err.find("AMXX_Foo") != std::string::npos);
    Clip e;
    CHECK(ParseClip(Mk(T_OK, P_OK, B_OK, M_OK, "{\"SKXX_SkateSkel_Root\":[{\"t\":0,\"rot\":[0,0,0]}]}"), "e", e, err));
    err.clear();
    CHECK(!BindClip(e, RefNames(), err)); CHECK(err.find("SKXX_") != std::string::npos);
}

// ------------------------------------------------------------------ evaluation
static Track Tr2(float t0, float a0, float t1, float a1, bool smooth) {
    Track tr; tr.bone = "b";
    tr.keys.push_back(Key{t0, QFromRotatorDeg(0, a0, 0), smooth});
    tr.keys.push_back(Key{t1, QFromRotatorDeg(0, a1, 0), smooth});
    return tr;
}

TEST(eval_single_key_is_constant) {
    Track tr; tr.keys.push_back(Key{0.3f, QFromRotatorDeg(0, 25, 0), true});
    for (float t : {0.f, 0.3f, 0.9f, 5.f}) CHECK(AngDeg(EvalTrack(tr, t, 1.f, true), QFromRotatorDeg(0, 25, 0)) < 0.01f);
}

TEST(eval_linear_midpoint) {
    Track tr = Tr2(0, 0, 1, 40, false);
    CHECK_NEAR(AngDeg(EvalTrack(tr, 0.5f, 2.f, false), QIdent), 20, 0.05);
    CHECK_NEAR(AngDeg(EvalTrack(tr, 3.f, 2.f, false), QIdent), 40, 0.05);   // after the last key, without loop
}

TEST(eval_smooth_has_zero_slope_at_keys) {
    Track tr = Tr2(0, 0, 1, 40, true);
    const float a0 = AngDeg(EvalTrack(tr, 0.f, 2.f, false), QIdent), a1 = AngDeg(EvalTrack(tr, 0.01f, 2.f, false), QIdent);
    CHECK(a1 - a0 < 0.02f);                       // linear would give 0.4 degrees
    CHECK_NEAR(AngDeg(EvalTrack(tr, 0.5f, 2.f, false), QIdent), 20, 0.05);
}

TEST(eval_loop_wraps_without_jump) {
    Track tr = Tr2(0, 0, 0.8f, 30, true);
    CHECK(AngDeg(EvalTrack(tr, 1.6f, 1.6f, true), tr.keys[0].q) < 0.01f);
    CHECK(AngDeg(EvalTrack(tr, 1.599f, 1.6f, true), tr.keys[0].q) < 0.5f);
    CHECK(AngDeg(EvalTrack(tr, 1.2f, 1.6f, true), QFromRotatorDeg(0, 15, 0)) < 0.05f);   // halfway back
}

TEST(cliptime_time_and_ratio) {
    Clip c; c.duration = 2.f; c.loop = true; c.drive = Drive::Time; c.r0 = 0; c.r1 = 1;
    CHECK_NEAR(ClipTime(c, 2.5f, 0), 0.5, 1e-5);
    c.loop = false;
    CHECK_NEAR(ClipTime(c, 2.5f, 0), 2.0, 1e-5);
    c.drive = Drive::ManualRatio;
    CHECK_NEAR(ClipTime(c, 0, 0.25f), 0.5, 1e-5);
    CHECK_NEAR(ClipTime(c, 0, 1.5f), 2.0, 1e-5);    // brought back into the range
    CHECK_NEAR(ClipTime(c, 0, -3.f), 0.0, 1e-5);
    c.r0 = 1; c.r1 = 0;                              // reversed range: allowed
    CHECK_NEAR(ClipTime(c, 0, 0.25f), 1.5, 1e-5);
}
