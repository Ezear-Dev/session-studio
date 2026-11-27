#include "library.h"
#include <algorithm>

bool ClipPlaysOn(const Clip& c, const FigureVariant& v) {
    if (!(c.states & v.state)) return false;
    if (StateNamed(v.state) && c.trickHash && c.trickHash != TrickHash(v.trick)) return false;
    if (v.state == ST_Landing && !v.trick.empty() && c.trickHash != TrickHash(v.trick)) return false;   // a trick landing: its own clips only
    if (!(c.stances & (1u << v.stance))) return false;
    return c.feet == Feet::Any || (c.feet == Feet::Goofy) == v.goofy;
}

int PlayingClip(const std::vector<const Clip*>& clips, const FigureVariant& v) {
    int best = -1;
    for (int i = 0; i < (int)clips.size(); i++) {
        const Clip* c = clips[i];
        if (!c || !c->enabled || !ClipPlaysOn(*c, v)) continue;
        if (best < 0) { best = i; continue; }
        const Clip* b = clips[best];
        if (c->priority > b->priority || (c->priority == b->priority && ClipSpecificity(*c) > ClipSpecificity(*b))) best = i;   // ties: the first one
    }
    return best;
}

std::vector<LibCard> BuildLibrary(const std::vector<LibEntry>& entries, const std::function<bool(const FigureVariant&)>& recorded,
                                  const std::vector<const Clip*>& clips) {
    std::vector<LibCard> out;
    out.reserve(entries.size());
    for (const LibEntry& e : entries) {
        LibCard c;
        c.base = e.base; c.title = e.title; c.category = e.category; c.note = e.note; c.ground = e.ground;
        c.stances = StateAllStances(e.base.state) ? 4 : 2;
        for (int s = 0; s < c.stances; s++)
            for (int g = 0; g < 2; g++) {
                LibVariant& lv = c.cell[s][g];
                lv.v = e.base;
                lv.v.stance = s;
                lv.v.goofy = g == 1;
                lv.recorded = recorded && recorded(lv.v);
                for (const Clip* cl : clips) {
                    if (!cl || !ClipPlaysOn(*cl, lv.v)) continue;
                    lv.clips++;
                    if (cl->enabled) lv.clipsOn++;
                }
                c.variants++;
                if (lv.recorded) c.recorded++;
            }
        // clips of this card: once each, even when they play on several variants
        for (const Clip* cl : clips) {
            if (!cl) continue;
            bool any = false;
            for (int s = 0; s < c.stances && !any; s++) for (int g = 0; g < 2 && !any; g++) any = ClipPlaysOn(*cl, c.cell[s][g].v);
            if (any) c.clips++;
        }
        out.push_back(std::move(c));
    }
    return out;
}

bool CardMatches(const LibCard& c, const LibFilter& f) {
    if (!f.category.empty() && c.category != f.category) return false;
    if (f.recordedOnly && c.recorded == 0) return false;
    if (f.withClipsOnly && c.clips == 0) return false;
    if (!f.search.empty() && Lower(c.title).find(Lower(f.search)) == std::string::npos) return false;
    return true;
}
