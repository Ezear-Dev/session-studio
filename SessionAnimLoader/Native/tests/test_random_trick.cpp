// v0.31 (user: "3 kickflips: pick one of them, or the game's own, at random"): the trick clips that are on for a figure
// are drawn at random, once per pop; "_random.json" adds the game's own animation to a figure's draw.
#include "check.h"
#include "../src/clip.h"
#include "../src/clipset.h"
#include "../src/player.h"
#include "../src/random_draw.h"
#include "../src/trick.h"
#include "../../../SessionStudio/model/library.h"
#include "../../../SessionStudio/model/workspace.h"
#include <windows.h>
#include <map>
#include <memory>
#include <string>

namespace {
Clip Make(const char* file, const char* trick, const char* link = "", int priority = 0, bool on = true) {
    Clip c;
    c.file = file; c.name = file; c.states = ST_Trick; c.trick = trick; c.trickHash = trick[0] ? TrickHash(trick) : 0u;
    c.link = link; c.enabled = on; c.loop = false; c.duration = 0.9f; c.priority = priority;
    return c;
}
Clip Landing(const char* file, const char* trick, const char* link, float len) {
    Clip c = Make(file, trick, link);
    c.states = ST_Landing; c.duration = len;
    return c;
}
struct Game {
    std::unique_ptr<Player> p = std::make_unique<Player>();
    ClipSet set;
    uint32_t pop = 0;
    FTr cs[2] = {};
    Game() {
        const int parent[2] = {-1, 0};
        MirrorMap m;
        m.other = {0, 1}; m.center = {true, true}; m.parent = {-1, 0}; m.conv = {0, 0};
        p->SetRig(parent, 2, m);
        p->SetSeed(12345);
    }
    GameState Air(const char* trick, int stance = STANCE_Normal, bool goofy = false) const {
        GameState g{}; g.kickflip = true; g.popSerial = pop; g.trickPhase = 1; g.trickHash = TrickHash(trick);
        g.stance = stance; g.goofy = goofy;
        return g;
    }
    std::string Frame(const GameState& g) {
        for (int i = 0; i < 2; i++) { cs[i] = FTr{}; cs[i].qw = 1.f; cs[i].sx = cs[i].sy = cs[i].sz = 1.f; }
        p->Apply(cs, 2, &set, g, 1.f / 60.f);
        return p->CurrentClipName();
    }
    // a new pop: the clip that plays on it ("" = the game's own animation)
    std::string Pop(const char* trick, int stance = STANCE_Normal, bool goofy = false) {
        ++pop;
        return Frame(Air(trick, stance, goofy));
    }
    float LandingAfter(const char* trick) const {
        GameState g{}; g.popSerial = pop; g.landTrickHash = TrickHash(trick); g.landStance = STANCE_Normal; g.groundStance = STANCE_Normal;
        return p->LandingClipLength(&set, g);
    }
    // how often each result came out, and how many times one came out twice in a row
    std::map<std::string, int> Count(int pops, int* repeats = nullptr) {
        std::map<std::string, int> n;
        std::string last = "?";
        if (repeats) *repeats = 0;
        for (int i = 0; i < pops; i++) {
            const std::string r = Pop("KickFlip");
            n[r]++;
            if (repeats && r == last) ++*repeats;
            last = r;
        }
        return n;
    }
};
void Three(Game& g) {
    g.set.clips.push_back(Make("kf1", "KickFlip", "one"));
    g.set.clips.push_back(Make("kf2", "KickFlip", "two"));
    g.set.clips.push_back(Make("kf3", "KickFlip", "three"));
}
void AddGame(Game& g, const char* key = "KickFlip_RGS_regular") {
    DrawSlot s;
    if (DrawSlotOf(key, s)) g.set.gameDraw.push_back(s);
}
} // namespace

TEST(random_one_clip_on_always_plays) {
    Game g;
    g.set.clips.push_back(Make("kf1", "KickFlip", "one"));
    g.set.clips.push_back(Make("kf_off", "KickFlip", "two", 0, false));
    std::map<std::string, int> n = g.Count(20);
    CHECK(n.size() == 1 && n["kf1"] == 20);
    CHECK(g.p->LastDraw().serial == 0);   // nothing to draw
}

TEST(random_clips_that_are_on_share_the_pops) {
    Game g;
    Three(g);
    int repeats = -1;
    std::map<std::string, int> n = g.Count(600, &repeats);
    CHECK(n.size() == 3);
    CHECK(n["kf1"] > 160 && n["kf2"] > 160 && n["kf3"] > 160);
    CHECK(repeats == 0);   // never the same one twice in a row
    CHECK(g.p->LastDraw().serial == 600 && g.p->LastDraw().choices == 3);
}

TEST(random_order_is_not_fixed) {
    Game g;
    Three(g);
    std::string a, b;
    for (int i = 0; i < 30; i++) a += g.Pop("KickFlip");
    Game h;
    Three(h);
    h.p->SetSeed(999);
    for (int i = 0; i < 30; i++) b += h.Pop("KickFlip");
    CHECK(a != b);
}

TEST(random_pick_holds_for_the_whole_trick) {
    Game g;
    Three(g);
    for (int pop = 0; pop < 20; pop++) {
        const std::string first = g.Pop("KickFlip");
        bool held = true;
        for (int f = 0; f < 20; f++) {
            if (f == 10) g.set.generation++;   // the folder was read again in mid-trick
            held = held && g.Frame(g.Air("KickFlip")) == first;
        }
        CHECK(held);
    }
}

TEST(random_picked_clip_switched_off_in_mid_trick_gives_way) {
    Game g;
    Three(g);
    const std::string first = g.Pop("KickFlip");
    for (auto& c : g.set.clips) if (c.name == first) c.enabled = false;
    g.set.generation++;
    const std::string now = g.Frame(g.Air("KickFlip"));
    CHECK(!now.empty() && now != first);
}

TEST(random_landing_follows_the_pick) {
    Game g;
    Three(g);
    g.set.clips.push_back(Landing("land1", "KickFlip", "one", 1.1f));
    g.set.clips.push_back(Landing("land2", "KickFlip", "two", 1.2f));
    g.set.clips.push_back(Landing("land3", "KickFlip", "three", 1.3f));
    bool ok = true;
    for (int i = 0; i < 60; i++) {
        const std::string r = g.Pop("KickFlip");
        const float want = r == "kf1" ? 1.1f : r == "kf2" ? 1.2f : 1.3f;
        ok = ok && std::fabs(g.LandingAfter("KickFlip") - want) < 1e-5f;
    }
    CHECK(ok);
}

TEST(random_game_animation_takes_its_turn_with_no_clip_and_no_clip_landing) {
    Game g;
    Three(g);
    g.set.clips.push_back(Landing("land1", "KickFlip", "one", 1.1f));
    g.set.clips.push_back(Landing("old", "KickFlip", "", 1.9f));   // an older landing without a link
    g.set.clips.push_back(Make("every", "", ""));                   // a clip for every trick
    AddGame(g);
    int games = 0, repeats = 0;
    bool cleanGame = true;
    std::string last = "?";
    for (int i = 0; i < 800; i++) {
        const std::string r = g.Pop("KickFlip");
        if (r == last) repeats++;
        last = r;
        if (!r.empty()) continue;
        games++;
        cleanGame = cleanGame && g.LandingAfter("KickFlip") == 0.f && g.p->LastDraw().game;
        for (int f = 0; f < 5; f++) cleanGame = cleanGame && g.Frame(g.Air("KickFlip")).empty();
    }
    CHECK(games > 150 && games < 250);   // one in four
    CHECK(repeats == 0);
    CHECK(cleanGame);
}

TEST(random_game_animation_keeps_a_landing_of_every_landing) {
    Game g;
    g.set.clips.push_back(Make("kf1", "KickFlip", "one"));
    g.set.clips.push_back(Landing("generic", "", "", 1.5f));
    AddGame(g);
    for (int i = 0; i < 4; i++) {
        const std::string r = g.Pop("KickFlip");
        if (r.empty()) CHECK_NEAR(g.LandingAfter("KickFlip"), 1.5, 1e-5);
    }
}

TEST(random_game_animation_with_one_clip_takes_turns) {
    Game g;
    g.set.clips.push_back(Make("kf1", "KickFlip", "one"));
    AddGame(g);
    int repeats = -1;
    std::map<std::string, int> n = g.Count(40, &repeats);
    CHECK(n["kf1"] == 20 && n[""] == 20 && repeats == 0);
}

TEST(random_game_animation_is_per_figure) {
    Game g;
    g.set.clips.push_back(Make("kf1", "KickFlip", "one"));
    g.set.clips.push_back(Make("hf1", "HeelFlip", "h"));
    AddGame(g, "KickFlip_SWS_regular");
    std::map<std::string, int> n = g.Count(20);   // normal stance: not concerned
    CHECK(n["kf1"] == 20);
    bool same = true;
    for (int i = 0; i < 10; i++) same = same && g.Pop("HeelFlip", STANCE_Switch) == "hf1" && g.Pop("KickFlip", STANCE_Normal, true) == "kf1";
    CHECK(same);
    int games = 0;
    for (int i = 0; i < 20; i++) games += g.Pop("KickFlip", STANCE_Switch).empty();
    CHECK(games == 10);
}

TEST(random_game_animation_without_a_clip_changes_nothing) {
    Game g;
    g.set.clips.push_back(Make("every", "", ""));
    AddGame(g);
    std::map<std::string, int> n = g.Count(10);
    CHECK(n["every"] == 10 && g.p->LastDraw().serial == 0);
}

TEST(random_higher_priority_plays_alone) {
    Game g;
    Three(g);
    g.set.clips.push_back(Make("boss", "KickFlip", "b", 5));
    std::map<std::string, int> n = g.Count(20);
    CHECK(n["boss"] == 20);
    Game h;
    Three(h);
    h.set.clips.push_back(Make("every_high", "", "", 5));
    std::map<std::string, int> m = h.Count(20);
    CHECK(m["every_high"] == 20);
}

TEST(random_clips_set_for_more_stances_or_both_feet_are_drawn_too) {
    Game g;
    g.set.clips.push_back(Make("narrow", "KickFlip", "a"));
    g.set.clips[0].stances = 1u << STANCE_Normal;
    g.set.clips[0].feet = Feet::Regular;
    g.set.clips.push_back(Make("wide", "KickFlip", "b"));
    std::map<std::string, int> n = g.Count(20);
    CHECK(n["narrow"] == 10 && n["wide"] == 10);
}

TEST(random_each_figure_has_its_own_turns) {
    Game g;
    g.set.clips.push_back(Make("kf1", "KickFlip", "a"));
    g.set.clips.push_back(Make("kf2", "KickFlip", "b"));
    g.set.clips.push_back(Make("hf1", "HeelFlip", "c"));
    g.set.clips.push_back(Make("hf2", "HeelFlip", "d"));
    std::string k = "?", h = "?";
    bool ok = true;
    for (int i = 0; i < 20; i++) {
        const std::string nk = g.Pop("KickFlip"), nh = g.Pop("HeelFlip");
        ok = ok && nk != k && nh != h;
        k = nk; h = nh;
    }
    CHECK(ok);
}

TEST(random_settings_text_round_trip) {
    std::vector<std::string> keys;
    std::string err;
    CHECK(ParseGameDraw("{\"gameAnimation\":[\"KickFlip_RGS_regular\",\"Late_Front_KickFlip_SWS_goofy\"]}", keys, err));
    CHECK(keys.size() == 2 && keys[1] == "Late_Front_KickFlip_SWS_goofy");
    std::vector<std::string> again;
    CHECK(ParseGameDraw(GameDrawToJson(keys), again, err) && again == keys);
    CHECK(ParseGameDraw("\xEF\xBB\xBF{}", again, err) && again.empty());
    CHECK(!ParseGameDraw("[1]", again, err) && !err.empty());
    CHECK(!ParseGameDraw("{\"gameAnimation\":\"KickFlip_RGS_regular\"}", again, err));
    CHECK(!ParseGameDraw("{\"gameAnimation\":[7]}", again, err));
    DrawSlot s;
    CHECK(DrawSlotOf("Late_Front_KickFlip_SWS_goofy", s) && s.trickHash == TrickHash("Late_Front_KickFlip") && s.stance == STANCE_Switch && s.goofy);
    CHECK(!DrawSlotOf("nonsense", s) && !DrawSlotOf("Manual_RGS_regular", s));
}

TEST(random_library_reads_the_settings_file) {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    const std::wstring d = std::wstring(base) + L"asn_tests_random_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(d.c_str(), nullptr);
    const std::wstring f = d + L"\\_random.json";
    auto put = [&](const char* text) {
        FILE* o = _wfopen(f.c_str(), L"wb");
        if (!o) return;
        fputs(text, o);
        fclose(o);
    };
    DeleteFileW(f.c_str());
    ClipLibrary lib(d);
    lib.SetRig(RefNames());
    lib.Scan();
    CHECK(lib.Current()->gameDraw.empty());
    put("{\"gameAnimation\":[\"KickFlip_RGS_regular\"]}");
    lib.Scan();
    CHECK(lib.Current()->gameDraw.size() == 1);
    const uint64_t gen = lib.Current()->generation;
    lib.Scan();
    CHECK(lib.Current()->generation == gen);   // unchanged: nothing published again
    put("{ broken");
    bool said = false;
    for (auto& l : lib.Scan()) said = said || l.find("_random.json") != std::string::npos;
    CHECK(said && lib.Current()->gameDraw.size() == 1);   // kept
    DeleteFileW(f.c_str());
    lib.Scan();
    CHECK(lib.Current()->gameDraw.empty());
    RemoveDirectoryW(d.c_str());
}

// ---- the editor's side
namespace {
FigureVariant Fig(uint32_t state, const char* trick, int stance = STANCE_Normal) {
    FigureVariant v; v.state = state; v.trick = trick; v.stance = stance;
    return v;
}
}

TEST(random_editor_lists_the_same_draw_as_the_game) {
    const Clip a = Make("a", "KickFlip", "one"), b = Make("b", "KickFlip", "two"), off = Make("off", "KickFlip", "x", 0, false),
               h = Make("h", "HeelFlip", "h");
    const Clip la = Landing("la", "KickFlip", "one", 1.f), lb = Landing("lb", "KickFlip", "two", 1.f), lo = Landing("lo", "KickFlip", "x", 1.f);
    const std::vector<const Clip*> cs = {&a, &b, &off, &h, &la, &lb, &lo, nullptr};
    const std::vector<std::string> keys = {"Kickflip_RGS_regular", "HeelFlip_SWS_regular"};   // any case
    FigureDraw d = DrawFor(cs, Fig(ST_Trick, "KickFlip"), keys);
    CHECK((d.clips == std::vector<int>{0, 1}) && d.game && d.Choices() == 3);
    d = DrawFor(cs, Fig(ST_Trick, "HeelFlip"), keys);
    CHECK(d.clips == std::vector<int>{3} && !d.game && d.Choices() == 1);
    d = DrawFor(cs, Fig(ST_Landing, "KickFlip"), keys);   // the landings of the clips in the draw
    CHECK((d.clips == std::vector<int>{4, 5}));
    CHECK(DrawFor(cs, Fig(ST_Trick, "Ollie"), {"Ollie_RGS_regular"}).Choices() == 0);   // no clip: nothing to draw
    CHECK(HasGameDraw(keys, Fig(ST_Trick, "KickFlip")) && !HasGameDraw(keys, Fig(ST_Trick, "KickFlip", STANCE_Switch)));
}

TEST(random_editor_draw_of_a_clip_for_every_trick_and_of_a_state) {
    Clip every = Make("every", "", ""), manual = Make("m1", "", ""), manual2 = Make("m2", "", "");
    manual.states = manual2.states = ST_Manual;
    const std::vector<const Clip*> cs = {&every, &manual, &manual2};
    FigureDraw d = DrawFor(cs, Fig(ST_Trick, "KickFlip"), {"KickFlip_RGS_regular"});
    CHECK(d.clips == std::vector<int>{0} && !d.game);
    d = DrawFor(cs, Fig(ST_Manual, ""), {});   // states are never drawn: one plays, as before
    CHECK(d.clips == std::vector<int>{1});
}

TEST(random_editor_writes_the_settings_file) {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    const std::wstring d = std::wstring(base) + L"asn_tests_randomw_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(d.c_str(), nullptr);
    const std::wstring f = d + L"\\_random.json";
    DeleteFileW(f.c_str());
    std::vector<std::string> keys{"stale"};
    std::string err;
    CHECK(ReadGameDrawFile(d, keys, err) && keys.empty());   // no file: nothing chosen
    CHECK(SetGameDrawFile(d, Fig(ST_Trick, "KickFlip"), true, err));
    CHECK(SetGameDrawFile(d, Fig(ST_Trick, "HeelFlip", STANCE_Switch), true, err));
    CHECK(SetGameDrawFile(d, Fig(ST_Trick, "Kickflip"), true, err));   // already there under another case
    CHECK(ReadGameDrawFile(d, keys, err) && keys.size() == 2);
    CHECK(SetGameDrawFile(d, Fig(ST_Trick, "KICKFLIP"), false, err));
    CHECK(ReadGameDrawFile(d, keys, err) && keys == std::vector<std::string>{"HeelFlip_SWS_regular"});
    FILE* o = _wfopen(f.c_str(), L"wb");
    if (o) { fputs("{ broken", o); fclose(o); }
    CHECK(!SetGameDrawFile(d, Fig(ST_Trick, "KickFlip"), true, err) && !err.empty());   // never overwritten blindly
    CHECK(!ReadGameDrawFile(d, keys, err));
    DeleteFileW(f.c_str());
    RemoveDirectoryW(d.c_str());
}
