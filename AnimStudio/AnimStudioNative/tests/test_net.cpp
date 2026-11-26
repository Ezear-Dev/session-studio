// 2026-11-26 multiplayer: wire format (net.h) and "what I play = what the others see" (Player::ExportNet / ApplyRemote).
#include "check.h"
#include "../src/player.h"
#include "../src/net.h"
#include <cmath>
#include <cstring>
#include <memory>

namespace {

struct NRig { int n = 0; std::vector<int> parent; std::vector<FTr> ref; };
const NRig& NR() {
    static NRig r = [] {
        NRig g;
        const auto& s = RefSkeleton();
        g.n = (int)s.size();
        g.parent.resize(g.n);
        std::vector<Quat> lq(g.n);
        std::vector<float> lp(g.n * 3);
        for (int i = 0; i < g.n; i++) {
            g.parent[i] = s[i].parent;
            lq[i] = QFromRotatorDeg((float)(i % 7) * 3.f, (float)(i % 5) * 5.f, (float)(i % 3) * 2.f);
            lp[i * 3] = s[i].x; lp[i * 3 + 1] = s[i].y; lp[i * 3 + 2] = s[i].z;
        }
        g.ref.assign(g.n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
        std::vector<char> dirty(g.n, 1);
        ToComponent(g.ref.data(), g.parent.data(), g.n, lq.data(), (const float (*)[3])lp.data(), (const bool*)dirty.data());
        return g;
    }();
    return r;
}
std::unique_ptr<Player> NP() {
    auto p = std::make_unique<Player>();
    p->SetRig(NR().parent.data(), NR().n, RefMirror());
    return p;
}
// a looping clip on one bone that turns over its 1 s (so the clip time matters)
Clip NClip(const char* file, uint32_t states, int bone, Mode mode = Mode::Additive) {
    Clip c;
    c.file = file; c.name = file; c.states = states; c.mode = mode;
    c.blendIn = c.blendOut = 0.2f; c.duration = 1.f; c.loop = true;
    Track t; t.bone = RefNames()[bone]; t.index = bone;
    t.keys.push_back(Key{0.f, QFromRotatorDeg(0, 0, 0), true});
    t.keys.push_back(Key{0.5f, QFromRotatorDeg(0, 0, 60), true});
    t.keys.push_back(Key{1.f, QFromRotatorDeg(0, 0, 0), true});
    c.tracks.push_back(t);
    c.netId = NetId(file);
    c.netHash = 0x1234u + (uint32_t)bone;
    return c;
}
GameState NGS(bool manual, bool goofy = false) { return GameState{manual, false, goofy, false, 0.5f}; }
float MaxDiff(const std::vector<FTr>& a, const std::vector<FTr>& b) {
    float m = 0.f;
    for (size_t i = 0; i < a.size(); i++) {
        const float d[7] = {a[i].qx - b[i].qx, a[i].qy - b[i].qy, a[i].qz - b[i].qz, a[i].qw - b[i].qw,
                            (a[i].px - b[i].px) * 0.01f, (a[i].py - b[i].py) * 0.01f, (a[i].pz - b[i].pz) * 0.01f};
        for (float v : d) if (std::fabs(v) > m) m = std::fabs(v);
    }
    return m;
}
constexpr float kNDt = 1.f / 60.f;
constexpr int kNLUpper = 15;

} // namespace

TEST(net_encode_decode_roundtrip) {
    NetFrame f;
    f.seq = 77; f.n = 2;
    f.l[0] = NetLayer{NetId("A.sanim"), 0xdeadbeefu, true, false, true, 0.42f, 1.f, 0.8f, -2.f, 0.5f};
    f.l[1] = NetLayer{NetId("b.sanim"), 1u, false, true, false, 3.f, 0.f, 1.f, 0.f, 1.f};
    const std::string s = NetEncode(f);
    CHECK(s.size() == 2 + 2 * (5 + 2 * 29));
    CHECK(s.size() < 1000);   // OpenMP: 1000 bytes per message
    NetFrame g;
    CHECK(NetDecode(s, g));
    CHECK(g.seq == 77 && g.n == 2);
    CHECK(g.l[0].id == f.l[0].id && g.l[0].hash == 0xdeadbeefu && g.l[0].mirrored && !g.l[0].dis && g.l[0].trick);
    CHECK(g.l[1].dis && !g.l[1].mirrored);
    CHECK_NEAR(g.l[0].t, 0.42f, 1e-6); CHECK_NEAR(g.l[0].w, 0.8f, 1e-6); CHECK_NEAR(g.l[0].wSpeed, -2.f, 1e-6);
    // a full frame stays small
    NetFrame big; big.n = kNetMaxLayers;
    CHECK(NetEncode(big).size() < 300);
}

TEST(net_decode_refuses_garbage) {
    NetFrame f; f.n = 1; f.l[0] = NetLayer{1, 2, false, false, false, 0.1f, 1.f, 1.f, 0.f, 1.f};
    const std::string ok = NetEncode(f);
    NetFrame g;
    CHECK(!NetDecode("", g));
    CHECK(!NetDecode("HELLO", g));
    CHECK(!NetDecode("X" + ok.substr(1), g));
    CHECK(!NetDecode(ok.substr(0, ok.size() - 1), g));    // odd length
    CHECK(!NetDecode(ok.substr(0, ok.size() - 2), g));    // one byte short
    CHECK(!NetDecode(ok + "00", g));                      // trailing byte
    std::string bad = ok; bad[3] = 'z';
    CHECK(!NetDecode(bad, g));                            // not hex
    NetFrame many; many.n = 1; std::string m = NetEncode(many);
    m[10] = '0'; m[11] = '5';                             // layer count 5
    CHECK(!NetDecode(m, g));
    NetFrame nan = f; nan.l[0].t = std::nanf("");
    CHECK(!NetDecode(NetEncode(nan), g));
    NetFrame heavy = f; heavy.l[0].w = 3.f;
    CHECK(!NetDecode(NetEncode(heavy), g));
    CHECK(NetDecode(ok, g));
}

TEST(net_id_ignores_case) {
    CHECK(NetId("KickFlip_RGS_regular.sanim") == NetId("kickflip_rgs_regular.SANIM"));
    CHECK(NetId("a.sanim") != NetId("b.sanim"));
}

TEST(net_remote_pose_matches_the_sender) {
    for (int goofy = 0; goofy < 2; goofy++) {
        auto a = NP(), b = NP();
        ClipSet set; set.generation = 1;
        set.clips.push_back(NClip("m.sanim", ST_Manual, kNLUpper));
        std::vector<FTr> ca, cb;
        NetFrame f;
        for (int k = 0; k < 40; k++) {   // mid-fade and steady frames
            ca = NR().ref;
            a->Apply(ca.data(), NR().n, &set, NGS(true, goofy == 1), kNDt);
            a->ExportNet(f, kNDt);
            if (k == 5 || k == 39) {
                cb = NR().ref;
                CHECK(b->ApplyRemote(cb.data(), NR().n, &set, f, 0.f, kNDt) > 0);
                CHECK(MaxDiff(ca, cb) < 1e-4f);
            }
        }
        CHECK(f.n == 1 && f.l[0].mirrored == (goofy == 1));
        CHECK(MaxDiff(ca, NR().ref) > 0.05f);   // the clip really moved the arm
    }
}

TEST(net_remote_extrapolates_between_frames) {
    auto a = NP(), b = NP();
    ClipSet set; set.generation = 1;
    set.clips.push_back(NClip("m.sanim", ST_Manual, kNLUpper));
    std::vector<FTr> ca;
    NetFrame f;
    for (int k = 0; k < 30; k++) { ca = NR().ref; a->Apply(ca.data(), NR().n, &set, NGS(true), kNDt); a->ExportNet(f, kNDt); }
    const NetFrame sent = f;   // the receiver only has this one, 4 frames old
    for (int k = 0; k < 4; k++) { ca = NR().ref; a->Apply(ca.data(), NR().n, &set, NGS(true), kNDt); a->ExportNet(f, kNDt); }
    std::vector<FTr> cb = NR().ref;
    b->ApplyRemote(cb.data(), NR().n, &set, sent, 4 * kNDt, kNDt);
    CHECK(MaxDiff(ca, cb) < 2e-3f);
    std::vector<FTr> stale = NR().ref;   // no extrapolation: visibly behind
    auto c = NP();
    c->ApplyRemote(stale.data(), NR().n, &set, sent, 0.f, kNDt);
    CHECK(MaxDiff(ca, stale) > MaxDiff(ca, cb));
}

TEST(net_remote_missing_clip_or_old_stream_leaves_the_game_pose) {
    auto a = NP(), b = NP();
    ClipSet set; set.generation = 1;
    set.clips.push_back(NClip("m.sanim", ST_Manual, kNLUpper));
    std::vector<FTr> ca;
    NetFrame f;
    for (int k = 0; k < 30; k++) { ca = NR().ref; a->Apply(ca.data(), NR().n, &set, NGS(true), kNDt); a->ExportNet(f, kNDt); }
    // the receiver has the same file name with other content
    ClipSet other; other.generation = 1;
    other.clips.push_back(NClip("m.sanim", ST_Manual, kNLUpper));
    other.clips[0].netHash ^= 1u;
    std::vector<FTr> cb = NR().ref;
    int miss = 0;
    CHECK(b->ApplyRemote(cb.data(), NR().n, &other, f, 0.f, kNDt, &miss) == 0);
    CHECK(miss == 1);
    CHECK(std::memcmp(cb.data(), NR().ref.data(), sizeof(FTr) * NR().n) == 0);
    // the stream stopped: faded out after kNetStale + kNetStaleFade
    cb = NR().ref;
    CHECK(b->ApplyRemote(cb.data(), NR().n, &set, f, Player::kNetStale + Player::kNetStaleFade + 0.01f, kNDt) == 0);
    CHECK(std::memcmp(cb.data(), NR().ref.data(), sizeof(FTr) * NR().n) == 0);
    std::vector<FTr> half = NR().ref;   // half way through the fade: in between
    b->ApplyRemote(half.data(), NR().n, &set, f, Player::kNetStale + 0.5f * Player::kNetStaleFade, kNDt);
    CHECK(MaxDiff(half, NR().ref) > 1e-3f);
    // a skeleton of another size is never touched
    std::vector<FTr> small(NR().ref.begin(), NR().ref.begin() + 40);
    CHECK(b->ApplyRemote(small.data(), 40, &set, f, 0.f, kNDt) == 0);
}

TEST(net_master_off_sends_zero_weight) {
    auto a = NP();
    ClipSet set; set.generation = 1;
    set.clips.push_back(NClip("m.sanim", ST_Manual, kNLUpper));
    std::vector<FTr> ca;
    NetFrame f;
    a->SetMaster(false);
    for (int k = 0; k < 40; k++) { ca = NR().ref; a->Apply(ca.data(), NR().n, &set, NGS(true), kNDt); a->ExportNet(f, kNDt); }
    for (int i = 0; i < f.n; i++) CHECK(f.l[i].w < 1e-4f);
}
