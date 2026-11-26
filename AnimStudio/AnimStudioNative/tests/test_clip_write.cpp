// Unknown fields kept, writing a clip (ClipToJson)
#include "check.h"
#include "../src/clip.h"
#include "../src/json.h"

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

const char* kSpecClip = R"({
  "format": "sanim", "version": 1,
  "name": "manual_bras_ouverts",
  "enabled": true,
  "trigger": { "states": ["Manual"], "priority": 3 },
  "play":    { "drive": "time", "duration": 1.6, "loop": true },
  "blend":   { "in": 0.25, "out": 0.30 },
  "mode": "additive",
  "mirrorForGoofy": true,
  "weights": { "AMXX_L_UpperArm": 0.5 },
  "tracks": {
    "AMXX_L_UpperArm": [ { "t": 0.0, "rot": [0, 0, 20], "interp": "smooth" },
                         { "t": 0.8, "rot": [0, 0, 35], "interp": "linear" } ],
    "AMXX_R_UpperArm": [ { "t": 0.0, "q": [0, 0, 0.17, 0.985] } ]
  }
})";

const char* kRatioClip = R"({
  "format": "sanim", "version": 1, "name": "ratio", "enabled": false,
  "trigger": { "states": ["Manual", "NoseManual"], "priority": 10 },
  "play": { "drive": "ManualRatio", "duration": 1.0, "loop": false, "ratioRange": [0.2, 0.9] },
  "blend": { "in": 0.1, "out": 0.4 }, "mode": "replace", "mirrorForGoofy": false,
  "tracks": { "AMXX_R_UpperArm": [ { "t": 0.0, "rot": [-15, -30, 10], "interp": "linear" }, { "t": 1.0, "rot": [15, 30, -10] } ] },
  "source": "x", "note": { "a": 1 }
})";

void SameClip(const Clip& a, const Clip& b) {
    CHECK(a.name == b.name && a.enabled == b.enabled && a.states == b.states && a.priority == b.priority);
    CHECK(a.drive == b.drive && a.loop == b.loop && a.mode == b.mode && a.mirror == b.mirror);
    CHECK_NEAR(a.duration, b.duration, 1e-6); CHECK_NEAR(a.r0, b.r0, 1e-6); CHECK_NEAR(a.r1, b.r1, 1e-6);
    CHECK_NEAR(a.blendIn, b.blendIn, 1e-6); CHECK_NEAR(a.blendOut, b.blendOut, 1e-6);
    CHECK(a.tracks.size() == b.tracks.size());
    for (size_t i = 0; i < a.tracks.size() && i < b.tracks.size(); i++) {
        const Track& x = a.tracks[i];
        const Track* y = nullptr;
        for (auto& t : b.tracks) if (t.bone == x.bone) y = &t;
        CHECK(y != nullptr);
        if (!y) continue;
        CHECK_NEAR(x.weight, y->weight, 1e-6);
        CHECK(x.keys.size() == y->keys.size());
        for (size_t k = 0; k < x.keys.size() && k < y->keys.size(); k++) {
            CHECK_NEAR(x.keys[k].t, y->keys[k].t, 1e-6);
            CHECK(x.keys[k].smooth == y->keys[k].smooth);
            CHECK(AngDeg(x.keys[k].q, y->keys[k].q) < 0.01f);
        }
    }
}
} // namespace

TEST(clip_extra_fields_survive_roundtrip) {
    Clip c, d; std::string err;
    CHECK(ParseClip(kRatioClip, "r.sanim", c, err));
    CHECK(c.extra.size() == 2);
    if (c.extra.size() == 2) { CHECK(c.extra[0].first == "source" && c.extra[0].second.s == "x"); CHECK(c.extra[1].first == "note"); }
    CHECK(ParseClip(ClipToJson(c), "r.sanim", d, err));
    CHECK(d.extra.size() == 2);
    if (d.extra.size() == 2) {
        CHECK(d.extra[0].first == "source" && d.extra[0].second.s == "x");
        CHECK(d.extra[1].first == "note" && d.extra[1].second.get("a") && d.extra[1].second.get("a")->n == 1.0);
    }
}

TEST(clip_to_json_roundtrip_equal) {
    for (const char* src : {kSpecClip, kRatioClip}) {
        Clip c, d; std::string err;
        CHECK(ParseClip(src, "a.sanim", c, err));
        const std::string out = ClipToJson(c);
        CHECK(ParseClip(out, "a.sanim", d, err));
        SameClip(c, d);
    }
    Clip t; std::string err;
    CHECK(ParseClip(kSpecClip, "a.sanim", t, err));
    CHECK(ClipToJson(t).find("ratioRange") == std::string::npos);   // only for ManualRatio
}

TEST(clip_to_json_is_accepted_by_bind) {
    Clip c, d; std::string err;
    CHECK(ParseClip(kSpecClip, "a.sanim", c, err));
    CHECK(ParseClip(ClipToJson(c), "a.sanim", d, err));
    CHECK(BindClip(d, RefNames(), err));
}
