// Session Studio -- the Browse page's data (redesign step 6, 2026-11-27): one card per trick or ground state, with the
// state of each of its variants (stance x feet): recorded in game, clips of the user, the clip that plays. No interface.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../../SessionAnimLoader/Native/src/clip.h"
#include "../../SessionAnimLoader/Native/src/trick.h"

// clip c plays on variant v: its state, its trick (or grind) name, the stance and the feet match
bool ClipPlaysOn(const Clip& c, const FigureVariant& v);
// among clips, the one the game plays on v (enabled ones; higher priority first, then the more specific); -1: none
int PlayingClip(const std::vector<const Clip*>& clips, const FigureVariant& v);
// the clip to open when the user picks v: the one that plays, else the first one made for v (even off); -1: none
int ClipToOpen(const std::vector<const Clip*>& clips, const FigureVariant& v);

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
std::vector<LibCard> BuildLibrary(const std::vector<LibEntry>& entries, const std::function<bool(const FigureVariant&)>& recorded,
                                  const std::vector<const Clip*>& clips);

struct LibFilter {
    std::string search;           // part of the title, any case ("" = all)
    std::string category;         // "" = every category
    bool recordedOnly = false;    // at least one variant recorded in game
    bool withClipsOnly = false;   // at least one clip of the user
};
bool CardMatches(const LibCard& c, const LibFilter& f);
