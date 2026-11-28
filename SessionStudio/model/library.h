// Session Studio -- the Browse page's data (redesign step 6, 2026-11-27): one card per trick or ground state, with the
// state of each of its variants (stance x feet): recorded in game, clips of the user, the clip that plays. No interface.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../../SessionAnimLoader/Native/src/clip.h"
#include "../../SessionAnimLoader/Native/src/random_draw.h"
#include "../../SessionAnimLoader/Native/src/trick.h"

// clip c plays on variant v: its state, its trick (or grind) name, the stance and the feet match
bool ClipPlaysOn(const Clip& c, const FigureVariant& v);
// among clips, the one the game plays on v (enabled ones; higher priority first, then the more specific); -1: none
int PlayingClip(const std::vector<const Clip*>& clips, const FigureVariant& v);
// v0.31: what takes turns on v in game: indexes of the clips (one: it plays alone; none: the game's own animation) and
// whether the game's own animation is in the draw too (gameKeys: _random.json, see random_draw.h)
struct FigureDraw {
    std::vector<int> clips;
    bool game = false;
    int Choices() const { return (int)clips.size() + (game ? 1 : 0); }
};
FigureDraw DrawFor(const std::vector<const Clip*>& clips, const FigureVariant& v, const std::vector<std::string>& gameKeys);
// the user put the game's own animation in the draw of the figure v
bool HasGameDraw(const std::vector<std::string>& gameKeys, const FigureVariant& v);
// the clip to open when the user picks v: the one that plays, else the first one made for v (even off); -1: none
int ClipToOpen(const std::vector<const Clip*>& clips, const FigureVariant& v);

// 2026-11-27: each trick clip has its own landing clip (same "link", see clip.h). Landing clips without a link (older
// files, one for every clip of the trick) go to ONE trick clip without a link: the one that is on, else the first.
std::string NewLinkId();   // 16 random hex characters
// the landing clip to show with the saved trick clip clips[trick] on the landing variant lv (-1: none; trick -1: none)
int LandingForTrick(const std::vector<const Clip*>& clips, int trick, const FigureVariant& lv);
// the landing clips that go with clips[trick] alone (deleted, copied and renamed with it)
std::vector<int> LandingsOfTrick(const std::vector<const Clip*>& clips, int trick);
// the trick clip the landing clip clips[landing] goes with (-1: none)
int TrickOfLanding(const std::vector<const Clip*>& clips, int landing);

struct LibEntry {                 // what to list (the app fills it: ground states, then the trick list)
    FigureVariant base;           // stance normal, regular
    std::string title, category, note;
    bool ground = false;          // a ground state (on foot, riding...) rather than a trick
};
struct LibVariant { FigureVariant v; bool recorded = false; int clips = 0, clipsOn = 0; };
struct LibCard {
    FigureVariant base;
    std::string title, category, note;
    bool ground = false;
    int stances = 4;              // 4, or 2 when only normal and switch exist (manuals...)
    LibVariant cell[4][2];        // [stance][goofy]; only the first `stances` rows exist
    int recorded = 0, variants = 0, clips = 0;   // over the variants that exist
};
// v is one of the card's variants (a card is a move as the game shows it, see WithStance)
bool CardHolds(const LibCard& card, const FigureVariant& v);
std::vector<LibCard> BuildLibrary(const std::vector<LibEntry>& entries, const std::function<bool(const FigureVariant&)>& recorded,
                                  const std::vector<const Clip*>& clips);

struct LibFilter {
    std::string search;           // part of the title, any case ("" = all)
    std::string category;         // "" = every category
    bool recordedOnly = false;    // at least one variant recorded in game
    bool withClipsOnly = false;   // at least one clip of the user
};
bool CardMatches(const LibCard& c, const LibFilter& f);
