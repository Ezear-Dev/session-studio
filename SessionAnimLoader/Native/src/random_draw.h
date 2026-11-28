// v0.31: when several trick clips are on for a figure, one is drawn at random at each pop. "_random.json" (clips
// folder, written by the editor) lists the figures whose draw also holds the game's own animation. Shared DLL / editor.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "clip.h"
#include "trick.h"

constexpr const char* kGameDrawFile = "_random.json";
constexpr int kMaxDraw = 64;   // clips of one figure in a draw (the first ones, in file order)

struct DrawSlot { uint32_t trickHash = 0; int stance = 0; bool goofy = false; };   // one figure, ready for the hook
// "KickFlip_RGS_regular" -> slot; false if it is not the key of a trick figure
bool DrawSlotOf(const std::string& figureKey, DrawSlot& out);
// {"gameAnimation": ["KickFlip_RGS_regular", ...]}; false (err set, keys untouched) if unreadable
bool ParseGameDraw(const std::string& text, std::vector<std::string>& keys, std::string& err);
std::string GameDrawToJson(const std::vector<std::string>& keys);

// The one rule of who is in a figure's draw, for the player and the editor. get(i): const Clip* (may be nullptr),
// i in [0, n). Writes into out the clips made for this trick that are on and play on this stance and these feet, with
// the highest priority among them (whatever else they cover). 0 if none, or if a clip for every trick has a higher
// priority (it then plays, as before). Nothing is allocated.
template <class Get>
int TrickPool(int n, Get get, uint32_t trickHash, int stance, bool goofy, const Clip** out, int max) {
    const uint8_t bit = (uint8_t)(1u << (stance >= 0 && stance < STANCE_Count ? stance : 0));
    int count = 0, top = 0, every = 0;
    bool haveEvery = false;
    for (int i = 0; i < n; i++) {
        const Clip* c = get(i);
        if (!c || !c->enabled || c->foreign || !(c->states & ST_Trick) || !(c->stances & bit)) continue;
        if (c->feet != Feet::Any && (c->feet == Feet::Goofy) != goofy) continue;
        if (!c->trickHash) {
            if (!haveEvery || c->priority > every) every = c->priority;
            haveEvery = true;
            continue;
        }
        if (c->trickHash != trickHash) continue;
        if (count == 0 || c->priority > top) { top = c->priority; count = 0; }
        if (c->priority == top && count < max) out[count++] = c;
    }
    return haveEvery && count && every > top ? 0 : count;
}
