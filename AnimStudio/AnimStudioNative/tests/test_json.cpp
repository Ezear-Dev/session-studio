#include "check.h"
#include "../src/json.h"

static const char* kSpecExample = R"({
  "format": "sanim", "version": 1,
  "name": "manual_bras_ouverts",
  "enabled": true,
  "trigger": { "states": ["Manual"], "priority": 0 },
  "play":    { "drive": "time", "duration": 1.6, "loop": true, "ratioRange": [0.0, 1.0] },
  "blend":   { "in": 0.25, "out": 0.30 },
  "mode": "additive",
  "mirrorForGoofy": true,
  "weights": { "AMXX_Spine3": 0.5 },
  "tracks": {
    "AMXX_L_UpperArm": [ { "t": 0.0, "rot": [0, 0, 20], "interp": "smooth" },
                         { "t": 0.8, "rot": [0, 0, 35], "interp": "smooth" } ],
    "AMXX_R_UpperArm": [ { "t": 0.0, "q": [0, 0, 0.17, 0.985] } ]
  },
  "note": "\u00e9t\u00e9 \"ok\" \\ \ud83d\ude00"
})";

TEST(json_parses_spec_example) {
    JValue v; std::string err;
    CHECK(JParse(kSpecExample, v, err));
    CHECK(err.empty());
    CHECK(v.t == JValue::Obj);
    CHECK(v.get("format") && v.get("format")->s == "sanim");
    CHECK(v.get("play") && v.get("play")->get("duration") && std::fabs(v.get("play")->get("duration")->n - 1.6) < 1e-12);
    const JValue* tr = v.get("tracks");
    CHECK(tr && tr->get("AMXX_L_UpperArm") && tr->get("AMXX_L_UpperArm")->a.size() == 2);
    CHECK(v.get("note") && v.get("note")->s == "\xC3\xA9t\xC3\xA9 \"ok\" \\ \xF0\x9F\x98\x80");
    CHECK(v.get("enabled") && v.get("enabled")->t == JValue::Bool && v.get("enabled")->b);
}

TEST(json_roundtrip_write_parse) {
    JValue v, w; std::string err;
    CHECK(JParse(kSpecExample, v, err));
    const std::string out = JWrite(v);
    CHECK(JParse(out, w, err));
    CHECK(JWrite(w) == out);
    CHECK(w.get("tracks")->get("AMXX_R_UpperArm")->a[0].get("q")->a[3].n > 0.98);
}

TEST(json_accepts_utf8_bom) {
    JValue v; std::string err;
    CHECK(JParse(std::string("\xEF\xBB\xBF") + kSpecExample, v, err));
}

TEST(json_rejects_bad_text) {
    JValue v; std::string err;
    CHECK(!JParse("", v, err)); CHECK(!err.empty());
    err.clear(); CHECK(!JParse("{\"a\":", v, err)); CHECK(!err.empty());
    err.clear(); CHECK(!JParse("[1,]", v, err)); CHECK(!err.empty());
    err.clear(); CHECK(!JParse("{\"a\":1} x", v, err)); CHECK(!err.empty());
    err.clear(); CHECK(!JParse(std::string(100, '['), v, err)); CHECK(!err.empty());   // profondeur > 64
    CHECK(err.find("line") != std::string::npos);
}

TEST(json_get_missing_is_null) {
    JValue v; std::string err;
    CHECK(JParse("{\"a\":1}", v, err));
    CHECK(v.get("absent") == nullptr);
    CHECK(v.get("a")->get("x") == nullptr);
}

TEST(json_writes_nan_as_null) {
    JValue v; v.t = JValue::Num; v.n = std::nan("");
    CHECK(JWrite(v) == "null");
}

#include <clocale>
TEST(json_ignores_process_locale) {
    const char* fr = std::setlocale(LC_NUMERIC, "French_France.1252");
    CHECK(fr != nullptr);   // the "comma" decimal locale is really active for this test
    JValue v; std::string err;
    CHECK(JParse("[1.6, 2.5e-1]", v, err));
    CHECK(v.a.size() == 2 && std::fabs(v.a[0].n - 1.6) < 1e-12 && std::fabs(v.a[1].n - 0.25) < 1e-12);
    JValue n; n.t = JValue::Num; n.n = 0.5;
    CHECK(JWrite(n) == "0.5");
    std::setlocale(LC_NUMERIC, "C");
}
