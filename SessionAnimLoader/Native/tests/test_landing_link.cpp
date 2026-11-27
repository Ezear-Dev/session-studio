// 2026-11-27 (user: "3 kickflip clips, 3 landing clips: landing 1 = kickflip 1..."): a trick clip and its landing clip
// share a "link". In game a landing with a link plays only after the trick clip with the same link; in the editor the
// landing follows its trick clip. Clips without a link behave as before.
#include "check.h"
#include "../src/clip.h"
#include "../src/player.h"
#include "../src/trick.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../../../SessionStudio/model/library.h"
#include <memory>

namespace {
Clip Make(const char* file, uint32_t state, const char* trick, const char* link, bool on = true) {
    Clip c;
    c.file = file; c.name = file; c.states = state; c.trick = trick; c.trickHash = trick[0] ? TrickHash(trick) : 0u;
    c.link = link; c.enabled = on; c.loop = false; c.duration = state == ST_Trick ? 0.9f : 1.0f;
    return c;
}
std::string Json(const char* extra) {
    return std::string("{\"format\":\"sanim\",\"version\":1,\"trigger\":{\"states\":[\"Trick\"],\"trick\":\"KickFlip\"},\"play\":{\"duration\":1.0},") + extra +
           "\"tracks\":{\"AMXX_R_UpperArm\":[{\"t\":0,\"q\":[0,0,0,1]}]}}";
}
// what the player would play as a landing after a trick (the length tells the clips apart)
struct Game {
    std::unique_ptr<Player> p = std::make_unique<Player>();
    ClipSet set;
    uint32_t pop = 0;
    Game() {
        const int parent[2] = {-1, 0};
        MirrorMap m;
        m.other = {0, 1}; m.center = {true, true}; m.parent = {-1, 0}; m.conv = {0, 0};
        p->SetRig(parent, 2, m);
    }
    void Pop(const char* trick) {
        FTr cs[2] = {};
        cs[0].qw = cs[1].qw = 1.f;
        for (int i = 0; i < 2; i++) cs[i].sx = cs[i].sy = cs[i].sz = 1.f;
        GameState g{}; g.kickflip = true; g.popSerial = ++pop; g.trickHash = TrickHash(trick);
        p->Apply(cs, 2, &set, g, 1.f / 60.f);
    }
    float LandingAfter(const char* trick) {
        GameState g{}; g.popSerial = pop; g.landTrickHash = TrickHash(trick); g.landStance = STANCE_Normal; g.groundStance = STANCE_Normal;
        return p->LandingClipLength(&set, g);
    }
};
Clip Len(Clip c, float d) { c.duration = d; return c; }
} // namespace

TEST(link_is_written_only_when_there_is_one) {
    Clip c; std::string err;
    CHECK(ParseClip(Json(""), "a.sanim", c, err) && c.link.empty());
    CHECK(ClipToJson(c).find("\"link\"") == std::string::npos);   // existing clips are written as before
    CHECK(ParseClip(Json("\"link\":\"abc123\","), "b.sanim", c, err) && c.link == "abc123" && c.extra.empty());
    Clip again;
    CHECK(ParseClip(ClipToJson(c), "b.sanim", again, err) && again.link == "abc123");
    CHECK(!ParseClip(Json("\"link\":7,"), "c.sanim", again, err));
}

TEST(link_new_ids_differ) {
    const std::string a = NewLinkId(), b = NewLinkId();
    CHECK(a.size() == 16 && b.size() == 16 && a != b);
}

TEST(link_game_plays_the_landing_of_the_trick_clip_that_played) {
    Game g;
    g.set.clips.push_back(Make("kf1", ST_Trick, "KickFlip", "one", false));
    g.set.clips.push_back(Make("kf2", ST_Trick, "KickFlip", "two", true));
    g.set.clips.push_back(Len(Make("land1", ST_Landing, "KickFlip", "one"), 1.1f));
    g.set.clips.push_back(Len(Make("land2", ST_Landing, "KickFlip", "two"), 1.2f));
    g.set.clips.push_back(Len(Make("old", ST_Landing, "KickFlip", ""), 1.3f));
    g.Pop("KickFlip");
    CHECK_NEAR(g.LandingAfter("KickFlip"), 1.2, 1e-6);   // not land1 (first in file order), not the old one
    g.set.clips[0].enabled = true; g.set.clips[1].enabled = false;
    g.Pop("KickFlip");
    CHECK_NEAR(g.LandingAfter("KickFlip"), 1.1, 1e-6);
    // its landing turned off or deleted (locked in the editor): the game's own landing, never another trick clip's
    g.set.clips[2].enabled = false;
    g.Pop("KickFlip");
    CHECK_NEAR(g.LandingAfter("KickFlip"), 0.0, 1e-6);
}

TEST(link_old_clips_without_a_link_behave_as_before) {
    Game g;
    g.set.clips.push_back(Make("kf", ST_Trick, "KickFlip", ""));
    g.set.clips.push_back(Len(Make("land_linked", ST_Landing, "KickFlip", "x"), 1.2f));
    g.set.clips.push_back(Len(Make("land_old", ST_Landing, "KickFlip", ""), 1.3f));
    g.Pop("KickFlip");
    CHECK_NEAR(g.LandingAfter("KickFlip"), 1.3, 1e-6);
    // no trick clip on at all: the old landing still plays, a linked one waits for its trick clip
    g.set.clips[0].enabled = false;
    g.Pop("KickFlip");
    CHECK_NEAR(g.LandingAfter("KickFlip"), 1.3, 1e-6);
}

TEST(link_of_an_every_trick_clip_changes_nothing) {
    Game g;
    g.set.clips.push_back(Make("every", ST_Trick, "", "stray"));
    g.set.clips.push_back(Len(Make("ollie_old", ST_Landing, "Ollie", ""), 1.3f));
    g.Pop("Ollie");
    CHECK_NEAR(g.LandingAfter("Ollie"), 1.3, 1e-6);
}

TEST(link_does_not_survive_the_next_pop) {
    Game g;
    g.set.clips.push_back(Make("kf", ST_Trick, "KickFlip", "one"));
    g.set.clips.push_back(Len(Make("ollie_old", ST_Landing, "Ollie", ""), 1.3f));
    g.Pop("KickFlip");
    g.Pop("Ollie");   // no Ollie trick clip
    CHECK_NEAR(g.LandingAfter("Ollie"), 1.3, 1e-6);
}

TEST(link_library_each_trick_clip_has_its_own_landing) {
    const Clip kf1 = Make("kf1", ST_Trick, "KickFlip", "one", false), kf2 = Make("kf2", ST_Trick, "KickFlip", "two"),
               kf3 = Make("kf3", ST_Trick, "KickFlip", "three", false);
    const Clip l1 = Make("l1", ST_Landing, "KickFlip", "one"), l2 = Make("l2", ST_Landing, "KickFlip", "two");
    const std::vector<const Clip*> cs = {&kf1, &kf2, &kf3, &l1, &l2, nullptr};
    FigureVariant lv; lv.state = ST_Landing; lv.trick = "KickFlip";
    CHECK(LandingForTrick(cs, 0, lv) == 3 && LandingForTrick(cs, 1, lv) == 4 && LandingForTrick(cs, 2, lv) == -1);
    CHECK(LandingForTrick(cs, -1, lv) == -1);   // a trick clip not saved yet never takes an existing landing
    CHECK(LandingsOfTrick(cs, 0) == std::vector<int>{3});
    CHECK(LandingsOfTrick(cs, 2).empty());
    CHECK(TrickOfLanding(cs, 3) == 0 && TrickOfLanding(cs, 4) == 1);
    CHECK(PlayingClip(cs, lv) == 4);   // the landing of the trick clip that is on
}

TEST(link_library_old_shared_landing_goes_to_the_trick_clip_that_is_on) {
    // the user's folder on 2026-11-27: Treflip (off), Treflip_2 (on), one landing, no link
    const Clip a = Make("treflip", ST_Trick, "Treflip", "", false), b = Make("treflip_2", ST_Trick, "Treflip", "");
    const Clip l = Make("landing", ST_Landing, "Treflip", "");
    const std::vector<const Clip*> cs = {&a, &b, &l};
    FigureVariant lv; lv.state = ST_Landing; lv.trick = "Treflip";
    CHECK(LandingForTrick(cs, 1, lv) == 2 && LandingForTrick(cs, 0, lv) == -1);
    CHECK(LandingsOfTrick(cs, 1) == std::vector<int>{2});
    CHECK(LandingsOfTrick(cs, 0).empty());   // deleting the turned-off one keeps the landing
    CHECK(TrickOfLanding(cs, 2) == 1);
}

TEST(link_library_orphans_and_shared_links_are_never_taken) {
    const Clip l = Make("lone", ST_Landing, "KickFlip", ""), linked = Make("lone2", ST_Landing, "KickFlip", "gone");
    const Clip any = Make("generic", ST_Landing, "", "");
    const Clip a = Make("a", ST_Trick, "HeelFlip", "same"), b = Make("b", ST_Trick, "HeelFlip", "same");
    const Clip hl = Make("hl", ST_Landing, "HeelFlip", "same");
    const std::vector<const Clip*> cs = {&l, &linked, &any, &a, &b, &hl};
    CHECK(TrickOfLanding(cs, 0) == -1 && TrickOfLanding(cs, 1) == -1 && TrickOfLanding(cs, 2) == -1);
    CHECK(LandingsOfTrick(cs, 3).empty());   // two trick files with one link (copied by hand): keep the landing
}

TEST(link_library_old_landing_used_on_two_trick_clips_stays) {
    // one old landing for both feet, one trick clip per foot: deleting one of them keeps it
    Clip r = Make("kf_r", ST_Trick, "KickFlip", ""), g = Make("kf_g", ST_Trick, "KickFlip", "");
    r.feet = Feet::Regular; g.feet = Feet::Goofy;
    const Clip l = Make("land", ST_Landing, "KickFlip", "");
    const std::vector<const Clip*> cs = {&r, &g, &l};
    CHECK(LandingsOfTrick(cs, 0).empty() && LandingsOfTrick(cs, 1).empty());
}

TEST(link_end_of_a_trick_is_free_only_with_its_own_landing) {
    Clip trick = Make("kf1", ST_Trick, "KickFlip", "one");
    trick.stances = 1u << STANCE_Normal; trick.feet = Feet::Regular;
    const Clip other = Make("l2", ST_Landing, "KickFlip", "two"), mine = Make("l1", ST_Landing, "KickFlip", "one");
    std::string missing;
    CHECK(!LandingCoversAllVariants(trick, {&other}, missing));
    CHECK(LandingCoversAllVariants(trick, {&other, &mine}, missing));
}
