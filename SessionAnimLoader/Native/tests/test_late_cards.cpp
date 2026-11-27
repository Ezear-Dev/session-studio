// 2026-11-27: in switch and fakie the game shows a late flip with the OTHER foot than its internal name (normal and
// nollie do not). A card must hold, in every column, the variant the game shows with the card's foot.
#include "check.h"
#include "../src/trick.h"
#include "../../../SessionStudio/model/library.h"
#include <string>
#include <vector>

namespace {
struct Read { const char* full; const char* shown; };
// the names the game gave on 2026-11-27 (clips\_trick_names.json), only those that say a foot
const Read kRead[] = {
    {"FKS_Late_BackFoot_Heelflip", "Fakie Late Front Foot Heelflip"},
    {"FKS_Late_Front_HeelFlip", "Fakie Late Back Foot Heel"},
    {"FKS_Late_Front_KickFlip", "Fakie Late Back Foot Flip"},
    {"NLS_Late_BackFoot_Heelflip", "Nollie Late Back Foot Heelflip"},
    {"NLS_Late_BackFoot_KickFlip", "Nollie Late Back Foot flip"},
    {"RGS_Late_BackFoot_Heelflip", "Late Back Foot Heel"},
    {"RGS_Late_BackFoot_KickFlip", "Late Back Foot Flip"},
    {"RGS_Late_Front_HeelFlip", "Late Front Foot Heelflip"},
    {"RGS_Late_Front_KickFlip", "Late Front Foot Kickflip"},
    {"SWS_Late_BackFoot_Heelflip", "Switch Late Front Foot Heelflip"},
    {"SWS_Late_BackFoot_KickFlip", "Switch Late Front Foot Flip"},
};
std::vector<LibCard> Cards(const std::vector<const char*>& bases) {
    std::vector<LibEntry> entries;
    for (const char* b : bases) {
        LibEntry e; e.base.state = ST_Trick; e.base.trick = b; e.title = b;
        entries.push_back(e);
    }
    return BuildLibrary(entries, nullptr, {});
}
const std::vector<const char*> kLate = {"Late_Front_KickFlip", "Late_Front_HeelFlip", "Late_BackFoot_KickFlip",
                                        "Late_BackFoot_Heelflip", "Late_BackFoot_Ollie", "Late_Front_OllieNorth", "KickFlip"};
bool BackFoot(const std::string& shown) { return shown.find("Back Foot") != std::string::npos; }
}

TEST(late_cards_hold_the_foot_the_game_shows) {
    const std::vector<LibCard> cards = Cards(kLate);
    int checked = 0;
    for (const Read& r : kRead) {
        std::string base; int stance = 0;
        CHECK(SplitTrickName(r.full, base, stance));
        int found = 0;
        for (const LibCard& c : cards) {
            const FigureVariant& v = c.cell[stance][0].v;
            if (Lower(v.trick) != Lower(base)) continue;
            found++;
            CHECK((Lower(c.base.trick).find("late_backfoot_") == 0) == BackFoot(r.shown));
            // the flip (kick or heel) stays the same
            CHECK((Lower(c.base.trick).find("heel") != std::string::npos) == (Lower(base).find("heel") != std::string::npos));
            CHECK(CardHolds(c, v));
        }
        CHECK(found == 1);   // every variant is in one card, once
        checked++;
    }
    CHECK(checked == 11);
}

TEST(late_cards_swap_only_switch_and_fakie_late_flips) {
    for (const LibCard& c : Cards(kLate)) {
        const bool flip = Lower(c.base.trick).find("late_") == 0 && Lower(c.base.trick).find("ollie") == std::string::npos;
        for (int s = 0; s < 4; s++) {
            const bool swapped = flip && (s == STANCE_Switch || s == STANCE_Fakie);
            CHECK((Lower(c.cell[s][0].v.trick) != Lower(c.base.trick)) == swapped);
            CHECK(c.cell[s][1].v.trick == c.cell[s][0].v.trick && c.cell[s][1].v.stance == s);
        }
    }
}

TEST(late_variant_changes_stance_to_the_same_foot) {
    FigureVariant v; v.state = ST_Trick; v.trick = "Late_BackFoot_Heelflip"; v.stance = STANCE_Normal; v.goofy = true;
    const FigureVariant f = WithStance(v, STANCE_Fakie);
    CHECK(f.trick == "Late_Front_HeelFlip" && f.stance == STANCE_Fakie && f.goofy);
    CHECK(WithStance(f, STANCE_Switch).trick == "Late_Front_HeelFlip");      // switch <-> fakie: no change
    CHECK(WithStance(f, STANCE_Nollie).trick == "Late_BackFoot_Heelflip");
    CHECK(WithStance(f, STANCE_Normal) == v);
    FigureVariant land = v; land.state = ST_Landing;                           // the landing of that trick follows it
    CHECK(WithStance(land, STANCE_Switch).trick == "Late_Front_HeelFlip");
    FigureVariant kick; kick.state = ST_Trick; kick.trick = "KickFlip";
    CHECK(WithStance(kick, STANCE_Fakie).trick == "KickFlip");
    FigureVariant ride; ride.state = ST_Riding;
    CHECK(WithStance(ride, STANCE_Switch).stance == STANCE_Switch);
    // without a name read in game, the derived title says the same foot as the card
    CHECK(VariantTitle(f).rfind("Late Back Foot HeelFlip", 0) == 0);
}
