// The Browse page's data (redesign step 6, 2026-11-27)
#include "check.h"
#include "../../../SessionStudio/model/library.h"
#include <set>

namespace {
Clip TrickClip(const char* trick, uint8_t stances, Feet feet, bool on, int prio = 0) {
    Clip c;
    c.states = ST_Trick; c.trick = trick; c.trickHash = TrickHash(trick);
    c.stances = stances; c.feet = feet; c.enabled = on; c.priority = prio;
    return c;
}
std::vector<LibEntry> Entries() {
    std::vector<LibEntry> e(3);
    e[0].base.state = ST_Manual; e[0].title = "Manual"; e[0].category = "On the ground"; e[0].ground = true;
    e[1].base.state = ST_Trick; e[1].base.trick = "KickFlip"; e[1].title = "Kickflip"; e[1].category = "Flips";
    e[2].base.state = ST_Trick; e[2].base.trick = "HeelFlip"; e[2].title = "Heelflip"; e[2].category = "Flips";
    return e;
}
} // namespace

TEST(library_cards_count_variants_and_clips) {
    const std::set<std::string> rec = {"kickflip_rgs_regular", "kickflip_sws_goofy", "manual_rgs_regular"};
    auto recorded = [&](const FigureVariant& v) { return rec.count(Lower(FigureKey(v))) != 0; };
    const Clip a = TrickClip("KickFlip", 1u << STANCE_Normal, Feet::Regular, true);       // normal regular only
    const Clip b = TrickClip("KickFlip", 1u << STANCE_Switch, Feet::Any, false);          // switch, both feet, turned off
    const Clip c = TrickClip("", kAllStances, Feet::Any, true);                           // every flip trick
    const std::vector<const Clip*> clips = {&a, &b, &c};
    const std::vector<LibCard> cards = BuildLibrary(Entries(), recorded, clips);
    CHECK(cards.size() == 3);
    const LibCard& m = cards[0];
    CHECK(m.stances == 2 && m.variants == 4 && m.recorded == 1 && m.clips == 0 && m.ground);   // manuals: normal / switch only
    const LibCard& k = cards[1];
    CHECK(k.stances == 4 && k.variants == 8 && k.recorded == 2);
    CHECK(k.cell[STANCE_Normal][0].recorded && k.cell[STANCE_Switch][1].recorded && !k.cell[STANCE_Fakie][0].recorded);
    CHECK(k.cell[STANCE_Normal][0].clips == 2 && k.cell[STANCE_Normal][0].clipsOn == 2);   // a + every-trick c
    CHECK(k.cell[STANCE_Normal][1].clips == 1);                                             // goofy: only c
    CHECK(k.cell[STANCE_Switch][0].clips == 2 && k.cell[STANCE_Switch][0].clipsOn == 1);    // b (off) + c
    CHECK(k.clips == 3);                                                                    // each clip once
    CHECK(cards[2].clips == 1 && cards[2].cell[STANCE_Normal][0].clips == 1);               // heelflip: only c
}

TEST(library_filters) {
    const std::set<std::string> rec = {"kickflip_rgs_regular"};
    auto recorded = [&](const FigureVariant& v) { return rec.count(Lower(FigureKey(v))) != 0; };
    const Clip h = TrickClip("HeelFlip", kAllStances, Feet::Any, true);
    const std::vector<LibCard> cards = BuildLibrary(Entries(), recorded, {&h});
    LibFilter f;
    int n = 0;
    for (auto& c : cards) n += CardMatches(c, f);
    CHECK(n == 3);
    f.search = "FLIP";
    n = 0; for (auto& c : cards) n += CardMatches(c, f);
    CHECK(n == 2);                       // any case, part of the title
    f = LibFilter{}; f.category = "On the ground";
    CHECK(CardMatches(cards[0], f) && !CardMatches(cards[1], f));
    f = LibFilter{}; f.recordedOnly = true;
    CHECK(!CardMatches(cards[0], f) && CardMatches(cards[1], f) && !CardMatches(cards[2], f));
    f = LibFilter{}; f.withClipsOnly = true;
    CHECK(!CardMatches(cards[1], f) && CardMatches(cards[2], f));
}

TEST(library_playing_clip_follows_the_player_order) {
    FigureVariant v; v.state = ST_Trick; v.trick = "KickFlip";
    const Clip all = TrickClip("", kAllStances, Feet::Any, true);              // less specific
    const Clip kf = TrickClip("KickFlip", kAllStances, Feet::Any, true);       // more specific
    const Clip off = TrickClip("KickFlip", kAllStances, Feet::Any, false, 50);   // off: never plays
    const Clip prio = TrickClip("", kAllStances, Feet::Any, true, 5);          // higher priority wins over specificity
    CHECK(PlayingClip({&all, &kf}, v) == 1);
    CHECK(PlayingClip({&all, &off}, v) == 0);
    CHECK(PlayingClip({&kf, &prio}, v) == 1);
    CHECK(PlayingClip({&off}, v) == -1);
    FigureVariant other = v; other.trick = "HeelFlip";
    CHECK(PlayingClip({&kf}, other) == -1);
    CHECK(ClipPlaysOn(all, other) && !ClipPlaysOn(kf, other));
}

TEST(library_clip_to_open_is_the_one_that_plays) {   // user 2026-11-27: picking a trick opened the first clip, not the active one
    FigureVariant v; v.state = ST_Trick; v.trick = "KickFlip";
    const Clip off = TrickClip("KickFlip", kAllStances, Feet::Any, false);
    const Clip on = TrickClip("KickFlip", kAllStances, Feet::Any, true);
    const Clip other = TrickClip("HeelFlip", kAllStances, Feet::Any, true);
    CHECK(ClipToOpen({&off, &on}, v) == 1);        // the active one, not the first in the list
    CHECK(ClipToOpen({&other, &off}, v) == 1);     // nothing plays: the first one made for this trick
    CHECK(ClipToOpen({&other}, v) == -1);
    CHECK(ClipToOpen({nullptr, &on}, v) == 1);     // unreadable files are skipped
}
