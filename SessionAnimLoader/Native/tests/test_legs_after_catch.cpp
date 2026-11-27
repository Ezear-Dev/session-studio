// 2026-11-27 (user request): a trick clip may keep its legs after the catch ("play.legsAfterCatch"). Late flips raise
// the catch / IsLanding almost at once, so without it nearly nothing of their legs can be posed.
#include "check.h"
#include "../src/clip.h"
#include "../src/player.h"
#include "../src/skeleton.h"
#include "../src/trick.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include <memory>
#include <string>

namespace {
Clip Parse(const std::string& playExtra) {
    const std::string txt = R"({"format":"sanim","version":1,"name":"c","trigger":{"states":["Trick"],"priority":0},
        "play":{"duration":1.0,"loop":false)" + playExtra + R"(},"mode":"replace","tracks":{"AMXX_L_Thigh":[{"t":0,"q":[0,0,0,1]}]}})";
    Clip c; std::string err;
    CHECK(ParseClip(txt, "c.sanim", c, err));
    return c;
}
// the weight of the legs of a trick clip after the catch (0 = the game's legs, 1 = the clip's)
float LegsAfterCatch(bool keep) {
    auto p = std::make_unique<Player>();
    const auto& s = RefSkeleton();
    const int n = (int)s.size();
    std::vector<int> parent(n);
    for (int i = 0; i < n; i++) parent[i] = s[i].parent;
    p->SetRig(parent.data(), n, RefMirror());
    std::vector<bool> leg(n);
    const auto names = RefNames();
    for (int i = 0; i < n; i++) leg[i] = IsLegBone(names[i]);
    p->SetLegBones(leg);
    ClipSet set; set.generation = 1;
    Clip c = Parse(keep ? ",\"legsAfterCatch\":true" : "");
    c.blendIn = 0.05f;
    CHECK(BindClip(c, names, *std::make_unique<std::string>()));
    set.clips.push_back(c);
    std::vector<FTr> cs(n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    NetFrame net;
    for (int f = 0; f < 40; f++) {
        GameState g{false, false, false, false, 0.5f};
        g.kickflip = true; g.popSerial = 1; g.trickPhase = f < 12 ? 1 : 3;   // catch at 0.2 s
        p->Apply(cs.data(), n, &set, g, 1.f / 60.f);
        p->ExportNet(net, 1.f / 60.f);
    }
    CHECK(net.n == 1);
    return net.n == 1 ? net.l[0].legW : -1.f;   // what the other players are told: no change of the wire format
}
}

TEST(legs_after_catch_field_is_optional_and_off) {
    const Clip off = Parse("");
    CHECK(!off.legsAfterCatch);
    CHECK(ClipToJson(off).find("legsAfterCatch") == std::string::npos);   // existing clips are written as before
    const Clip on = Parse(",\"legsAfterCatch\":true");
    CHECK(on.legsAfterCatch);
    Clip again; std::string err;
    CHECK(ParseClip(ClipToJson(on), "c.sanim", again, err) && again.legsAfterCatch);
    CHECK(!ParseClip(R"({"format":"sanim","version":1,"name":"c","trigger":{"states":["Trick"]},
        "play":{"duration":1.0,"legsAfterCatch":3},"tracks":{"AMXX_L_Thigh":[{"t":0,"q":[0,0,0,1]}]}})", "c.sanim", again, err));
}

TEST(legs_after_catch_in_the_player) {
    CHECK(LegsAfterCatch(false) < 0.01f);   // as before: the game takes the legs back
    CHECK(LegsAfterCatch(true) > 0.99f);
}

TEST(legs_after_catch_is_on_by_default_for_new_late_flip_clips_only) {
    CHECK(IsLateFlip("Late_Front_KickFlip") && IsLateFlip("late_backfoot_heelflip"));
    CHECK(!IsLateFlip("KickFlip") && !IsLateFlip("Late_BackFoot_Ollie") && !IsLateFlip("Late_Front_OllieNorth") && !IsLateFlip(""));
    EditorDoc d;
    FigureVariant v; v.state = ST_Trick; v.trick = "Late_Front_KickFlip"; v.stance = STANCE_Fakie;
    d.NewClip(v);
    CHECK(d.clip().legsAfterCatch);
    v.trick = "KickFlip";
    d.NewClip(v);
    CHECK(!d.clip().legsAfterCatch);
    v.trick = "Late_BackFoot_Heelflip"; v.state = ST_Landing;   // a landing clip has free legs anyway
    d.NewClip(v);
    CHECK(!d.clip().legsAfterCatch);
}
