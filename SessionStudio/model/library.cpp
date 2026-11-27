#include "library.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include <set>

bool ClipPlaysOn(const Clip& c, const FigureVariant& v) {
    if (!(c.states & v.state)) return false;
    if (StateNamed(v.state) && c.trickHash && c.trickHash != TrickHash(v.trick)) return false;
    if (v.state == ST_Landing && !v.trick.empty() && c.trickHash != TrickHash(v.trick)) return false;   // a trick landing: its own clips only
    if (!(c.stances & (1u << v.stance))) return false;
    return c.feet == Feet::Any || (c.feet == Feet::Goofy) == v.goofy;
}

int PlayingClip(const std::vector<const Clip*>& clips, const FigureVariant& v) {
    // as in the player: after a trick, only the landing of the trick clip that plays (a generic landing: always)
    const bool trickLanding = v.state == ST_Landing && !v.trick.empty();
    std::string link;
    if (trickLanding) {
        FigureVariant tv = v;
        tv.state = ST_Trick;
        const int t = PlayingClip(clips, tv);
        if (t >= 0) link = clips[t]->link;
    }
    int best = -1;
    for (int i = 0; i < (int)clips.size(); i++) {
        const Clip* c = clips[i];
        if (!c || !c->enabled || !ClipPlaysOn(*c, v)) continue;
        if (trickLanding && c->trickHash && c->link != link) continue;
        if (best < 0) { best = i; continue; }
        const Clip* b = clips[best];
        if (c->priority > b->priority || (c->priority == b->priority && ClipSpecificity(*c) > ClipSpecificity(*b))) best = i;   // ties: the first one
    }
    return best;
}

namespace {
bool NamedTrick(const Clip* c) { return c && (c->states & ST_Trick) && c->trickHash; }
bool NamedLanding(const Clip* c) { return c && (c->states & ST_Landing) && !(c->states & ST_Trick) && c->trickHash; }
// the trick clip an older landing (no link) goes with on the trick variant tv
int OldOwner(const std::vector<const Clip*>& clips, const FigureVariant& tv) {
    std::vector<const Clip*> old(clips.size(), nullptr);
    for (size_t i = 0; i < clips.size(); i++)
        if (NamedTrick(clips[i]) && clips[i]->link.empty()) old[i] = clips[i];
    return ClipToOpen(old, tv);
}
// every trick clip an older landing goes with, over the variants the landing plays on
std::set<int> OldOwners(const std::vector<const Clip*>& clips, const Clip& landing) {
    std::set<int> out;
    for (int s = 0; s < STANCE_Count; s++)
        for (int g = 0; g < 2; g++) {
            FigureVariant v;
            v.state = ST_Landing; v.trick = landing.trick; v.stance = s; v.goofy = g == 1;
            if (!ClipPlaysOn(landing, v)) continue;
            v.state = ST_Trick;
            const int o = OldOwner(clips, v);
            if (o >= 0) out.insert(o);
        }
    return out;
}
} // namespace

std::string NewLinkId() {
    std::random_device rd;
    char b[24];
    std::snprintf(b, sizeof(b), "%08x%08x", (unsigned)rd(), (unsigned)rd());
    return b;
}

int LandingForTrick(const std::vector<const Clip*>& clips, int trick, const FigureVariant& lv) {
    if (trick < 0 || trick >= (int)clips.size() || !NamedTrick(clips[trick])) return -1;
    const std::string& link = clips[trick]->link;
    if (link.empty()) {
        FigureVariant tv = lv;
        tv.state = ST_Trick;
        if (OldOwner(clips, tv) != trick) return -1;
    }
    int found = -1;
    for (int i = 0; i < (int)clips.size(); i++) {
        const Clip* c = clips[i];
        if (!NamedLanding(c) || c->link != link || !ClipPlaysOn(*c, lv)) continue;
        if (found < 0 || (c->enabled && !clips[found]->enabled)) found = i;   // one that is on first
    }
    return found;
}

std::vector<int> LandingsOfTrick(const std::vector<const Clip*>& clips, int trick) {
    std::vector<int> out;
    if (trick < 0 || trick >= (int)clips.size() || !NamedTrick(clips[trick])) return out;
    const Clip& t = *clips[trick];
    if (!t.link.empty())
        for (int i = 0; i < (int)clips.size(); i++)
            if (i != trick && NamedTrick(clips[i]) && clips[i]->link == t.link) return out;   // shared: it stays
    for (int i = 0; i < (int)clips.size(); i++) {
        const Clip* c = clips[i];
        if (!NamedLanding(c) || c->link != t.link) continue;
        if (!t.link.empty() || OldOwners(clips, *c) == std::set<int>{trick}) out.push_back(i);
    }
    return out;
}

int TrickOfLanding(const std::vector<const Clip*>& clips, int landing) {
    if (landing < 0 || landing >= (int)clips.size() || !NamedLanding(clips[landing])) return -1;
    const Clip& l = *clips[landing];
    if (l.link.empty()) {
        const std::set<int> o = OldOwners(clips, l);
        return o.empty() ? -1 : *o.begin();
    }
    for (int i = 0; i < (int)clips.size(); i++)
        if (NamedTrick(clips[i]) && clips[i]->link == l.link) return i;
    return -1;
}

int ClipToOpen(const std::vector<const Clip*>& clips, const FigureVariant& v) {
    const int pl = PlayingClip(clips, v);
    if (pl >= 0) return pl;
    for (int i = 0; i < (int)clips.size(); i++)
        if (clips[i] && ClipPlaysOn(*clips[i], v)) return i;
    return -1;
}

bool CardHolds(const LibCard& card, const FigureVariant& v) {
    return card.base.state == v.state && (!StateNamed(v.state) || Lower(WithStance(card.base, v.stance).trick) == Lower(v.trick));
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
                lv.v = WithStance(e.base, s);
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
