// 2026-11-25: named grinds (crooked, smith, feeble...): recording key, clip format, editor variant.
#include "check.h"
#include "../src/trick.h"
#include "../src/clip.h"
#include "../../AnimStudioEditor/model/editor_doc.h"

TEST(grind_named_figure_key_round_trip) {
    FigureVariant v; v.state = ST_Grind; v.trick = "Crooked"; v.stance = STANCE_Switch; v.goofy = true;
    CHECK(FigureKey(v) == "Grind-Crooked_SWS_goofy");
    FigureVariant b;
    CHECK(ParseFigureKey("Grind-Crooked_SWS_goofy", b));
    CHECK(b == v);
    FigureVariant u; u.state = ST_Grind;   // no name: the old key
    CHECK(FigureKey(u) == "Grind_RGS_regular");
    CHECK(ParseFigureKey("Grind_RGS_regular", b));
    CHECK(b.state == ST_Grind && b.trick.empty());
    FigureVariant w; w.state = ST_Grind; w.trick = "Feeble_Grind";   // a name with "_" stays whole
    CHECK(ParseFigureKey(FigureKey(w), b) && b.trick == "Feeble_Grind");
}

TEST(grind_named_clip_format) {
    Clip c;
    c.name = "g"; c.states = ST_Grind; c.trick = "Smith"; c.trickHash = TrickHash("Smith"); c.duration = 1.f;
    Track tr; tr.bone = "AMXX_R_UpperArm"; tr.keys.push_back(Key{0.f, QFromRotatorDeg(10, 0, 0), true});
    c.tracks.push_back(tr);
    Clip back; std::string err;
    CHECK(ParseClip(ClipToJson(c), "g", back, err));
    CHECK(back.trick == "Smith");
    CHECK(back.trickHash == TrickHash("Smith"));
    Clip any = back; any.trick.clear(); any.trickHash = 0;
    CHECK(ClipSpecificity(back) > ClipSpecificity(any));   // a named grind wins over an "every grind" clip
}

TEST(grind_editor_variant_of_a_clip) {
    Clip c; c.states = ST_Grind; c.trick = "Feeble"; c.stances = 1u << STANCE_Normal;
    const FigureVariant v = VariantOfClip(c);
    CHECK(v.state == ST_Grind && v.trick == "Feeble");
    Clip d;
    SetClipVariant(d, v);
    CHECK(d.states == ST_Grind && d.trick == "Feeble" && d.trickHash == TrickHash("Feeble"));
    CHECK(VariantTitle(v).rfind("Grind Feeble", 0) == 0);
}
