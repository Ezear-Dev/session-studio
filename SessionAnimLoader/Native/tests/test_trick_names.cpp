// v0.27: the trick names the game shows, read at the pop and kept in clips\_trick_names.json (mod writes, editor reads).
#include "check.h"
#include "../src/trick.h"
#include <string>

namespace {
std::string Title(const char* trick, int stance, const TrickNames* read) {
    FigureVariant v; v.state = ST_Trick; v.trick = trick; v.stance = stance;
    return VariantTitle(v, read);
}
}

TEST(trick_names_file_roundtrip) {
    TrickNames a;
    CHECK(a.Set("RGS_KickFlip", "Kickflip"));
    CHECK(a.Set("SWS_KickFlip", "Switch Kickflip"));
    CHECK(!a.Set("rgs_kickflip", "Kickflip"));   // the same name again: nothing to write
    CHECK(a.Set("RGS_Treflip", "360 \"Flip\" \\ é"));
    TrickNames b;
    CHECK(b.Merge(a.ToJson()));
    CHECK(b.Size() == 3);
    const std::string* s = b.Find("rgs_KICKFLIP");
    CHECK(s && *s == "Kickflip");
    s = b.Find("RGS_Treflip");
    CHECK(s && *s == "360 \"Flip\" \\ é");
    CHECK(b.ToJson().find("\"RGS_KickFlip\"") != std::string::npos);   // the game's own spelling is kept in the file
    CHECK(!b.Find("NLS_KickFlip"));
}

TEST(trick_names_merge_keeps_what_was_just_read_and_ignores_a_bad_file) {
    TrickNames old;
    old.Set("RGS_KickFlip", "Old name");
    old.Set("FKS_Ollie", "Fakie Ollie");
    TrickNames now;
    now.Set("RGS_KickFlip", "Kickflip");        // read in game this session: newer than the file
    CHECK(now.Merge(old.ToJson()));
    CHECK(now.Size() == 2 && *now.Find("RGS_KickFlip") == "Kickflip" && *now.Find("FKS_Ollie") == "Fakie Ollie");
    CHECK(!now.Merge("{ not json"));
    CHECK(!now.Merge("{\"names\": 3}"));
    CHECK(now.Size() == 2);
}

TEST(trick_names_are_cleaned) {
    TrickNames n;
    CHECK(n.Set("RGS_Ollie", "  Oll\nie\t  "));
    CHECK(*n.Find("RGS_Ollie") == "Ollie");
    CHECK(!n.Set("RGS_KickFlip", " \r\n "));    // nothing left: ignored
    CHECK(!n.Set("RGS_KickFlip", ""));
    CHECK(!n.Set("KickFlip", "Kickflip"));       // not a trick name of the game (no stance)
    CHECK(!n.Find("RGS_KickFlip"));
    std::string longName;
    for (int i = 0; i < 60; i++) longName += "é";   // 120 bytes, cut on a character boundary
    CHECK(n.Set("RGS_HeelFlip", longName));
    const std::string& cut = *n.Find("RGS_HeelFlip");
    CHECK(cut.size() <= 80 && cut.size() % 2 == 0 && cut.size() >= 78);
}

TEST(trick_names_title_order) {
    TrickNames n;
    // the names the game gave on 2026-11-27: switch (and fakie) show the foot the other way round from the internal name
    n.Set("SWS_Late_BackFoot_KickFlip", "Switch Late Front Foot Flip");
    n.Set("SWS_Late_Front_KickFlip", "Switch Late Flip");
    n.Set("SWS_KickFlip", "Switch Kickflip");
    n.Set("RGS_Treflip", "360 Flip");
    // the name read in game, else the derived one (no name typed by hand any more)
    CHECK(Title("Late_BackFoot_KickFlip", STANCE_Switch, &n) == "Switch Late Front Foot Flip  ·  Switch  ·  regular");
    CHECK(Title("Late_Front_KickFlip", STANCE_Switch, &n) == "Switch Late Flip  ·  Switch  ·  regular");
    CHECK(Title("Late_BackFoot_KickFlip", STANCE_Switch, nullptr) == "Late Front Foot KickFlip  ·  Switch  ·  regular");
    CHECK(Title("KickFlip", STANCE_Switch, &n) == "Switch Kickflip  ·  Switch  ·  regular");
    CHECK(Title("treflip", STANCE_Normal, &n) == "360 Flip  ·  Normal  ·  regular");
    CHECK(Title("KickFlip", STANCE_Fakie, &n) == "KickFlip  ·  Fakie  ·  regular");
    CHECK(Title("KickFlip", STANCE_Switch, nullptr) == "KickFlip  ·  Switch  ·  regular");
    FigureVariant land; land.state = ST_Landing; land.trick = "Treflip";
    CHECK(VariantTitle(land, &n).rfind("Landing 360 Flip", 0) == 0);
}

TEST(trick_names_card_title_comes_from_the_normal_stance_only) {
    TrickNames n;
    n.Set("RGS_Treflip", "360 Flip");
    n.Set("SWS_KickFlip", "Switch Kickflip");
    CHECK(TrickCardTitle("Treflip", &n) == "360 Flip");
    CHECK(TrickCardTitle("KickFlip", &n) == "KickFlip");   // only the switch name is known: it would mislabel the card
    CHECK(TrickCardTitle("Treflip", nullptr) == "Treflip");
}
