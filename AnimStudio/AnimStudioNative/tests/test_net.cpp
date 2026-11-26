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
    c.netId = (uint32_t)(ContentId(file) >> 32);
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
    f.l[0] = NetLayer{0x1111u, 0xdeadbeefu, true, false, true, 0.42f, 1.f, 0.8f, -2.f, 0.5f};
    f.l[1] = NetLayer{0x2222u, 1u, false, true, false, 3.f, 0.f, 1.f, 0.f, 1.f};
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

TEST(net_content_id_ignores_bom_and_line_endings) {
    const std::string lf = "{\n  \"name\": \"k\"\n}\n";
    const std::string crlf = "{\r\n  \"name\": \"k\"\r\n}\r\n";
    const std::string bom = "\xEF\xBB\xBF" + lf;
    CHECK(ContentId(lf) == ContentId(crlf));
    CHECK(ContentId(lf) == ContentId(bom));
    CHECK(ContentId(lf) != ContentId("{\n  \"name\": \"K\"\n}\n"));   // any other change: another clip
    CHECK(ContentId("") != ContentId(" "));
}

TEST(net_old_wire_is_not_read) {
    NetFrame f; f.n = 1; f.l[0] = NetLayer{1, 2, false, false, false, 0.1f, 1.f, 1.f, 0.f, 1.f};
    std::string s = NetEncode(f);
    CHECK(s.substr(0, 2) == "S2");
    NetFrame g;
    s[1] = '1';   // a v0.19 / v0.20 frame (clips named by file): refused
    CHECK(!NetDecode(s, g));
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

// 2026-11-26 (v0.20): receiver clock. The sender plays at 60 frames/s and sends every 4th frame; packets arrive
// with an irregular delay. Returns the receiver's clip time on every frame (frame j uses the last packet arrived).
namespace {
struct NetSim { std::vector<float> t; float maxJump = 0.f; int snaps = 0; };
NetSim RunNetSim(const ClipSet& set, const char* file, bool trick, float (*truth)(int), int frames, bool smooth = true) {
    NetSim out;
    auto b = NP();
    const int lat[] = {2, 5, 3, 6, 2, 4, 3, 5};
    struct Pkt { int sent, arrives; NetFrame f; };
    std::vector<Pkt> pk;
    for (int s = 0, k = 0; s < frames; s += 4, k++) {
        NetFrame f; f.seq = (uint32_t)k + 1; f.n = 1;
        const float sp = (truth(s + 1) - truth(s)) / kNDt;
        f.l[0] = NetLayer{set.clips[0].netId, set.clips[0].netHash, false, false, trick, truth(s), sp, 1.f, 0.f, 1.f};
        pk.push_back(Pkt{s, s + lat[k % 8], f});
    }
    int cur = -1;
    for (int j = 0; j < frames; j++) {
        while (cur + 1 < (int)pk.size() && pk[cur + 1].arrives <= j) cur++;
        if (cur < 0) continue;
        std::vector<FTr> cb = NR().ref;
        Player::NetClockStats clk;
        const float age = (float)(j - pk[cur].arrives) * kNDt;
        b->ApplyRemote(cb.data(), NR().n, &set, pk[cur].f, age, smooth ? kNDt : 0.f, nullptr, &clk);
        if (clk.newFrame && clk.jump > out.maxJump) out.maxJump = clk.jump;
        out.snaps += clk.snaps;
        out.t.push_back(b->RemoteTime(0));
    }
    return out;
}
float LoopTruth(int k) { return std::fmod((float)k * kNDt, 1.f); }
// trick: plays to 0.4 s, held there 0.3 s (in the air), then 3x catch-up, ends at 1.5 s
float TrickTruth(int k) {
    const float s = (float)k * kNDt;
    if (s < 0.4f) return s;
    if (s < 0.7f) return 0.4f;
    const float t = 0.4f + 3.f * (s - 0.7f);
    return t < 1.5f ? t : 1.5f;
}
}

TEST(net_remote_clock_absorbs_packet_jitter) {
    ClipSet set; set.generation = 1;
    set.clips.push_back(NClip("r.sanim", ST_Riding, kNLUpper));
    const NetSim smooth = RunNetSim(set, "r.sanim", false, LoopTruth, 300);
    const NetSim old = RunNetSim(set, "r.sanim", false, LoopTruth, 300, false);   // dt 0: the old rule (reset per frame)
    auto maxStep = [](const NetSim& s) {
        float m = 0.f;
        for (size_t i = 1; i < s.t.size(); i++) {
            float d = s.t[i] - s.t[i - 1];
            if (d < -0.5f) d += 1.f;   // loop wrap
            const float e = std::fabs(d - kNDt);
            if (i > 30 && e > m) m = e;   // after the first second
        }
        return m;
    };
    CHECK(smooth.maxJump > 2.5f * kNDt);           // the old rule would have jumped by 3+ frames
    CHECK(maxStep(old) > 2.5f * kNDt);
    CHECK(maxStep(smooth) < 0.5f * kNDt);          // now: every frame moves by about one frame
    CHECK(smooth.snaps == 0);
}

TEST(net_remote_clock_follows_the_landing_sync_without_a_jump) {
    ClipSet set; set.generation = 1;
    Clip c = NClip("k.sanim", ST_Trick, kNLUpper);
    c.loop = false; c.duration = 1.5f;
    set.clips.push_back(c);
    const int frames = 120;   // 2 s
    const NetSim s = RunNetSim(set, "k.sanim", true, TrickTruth, frames);
    const NetSim old = RunNetSim(set, "k.sanim", true, TrickTruth, frames, false);
    auto maxStep = [](const NetSim& r) { float m = 0.f; for (size_t i = 1; i < r.t.size(); i++) m = std::fmax(m, std::fabs(r.t[i] - r.t[i - 1])); return m; };
    std::printf("landing sync: biggest step %.3f s (old rule %.3f)\n", maxStep(s), maxStep(old));
    CHECK(maxStep(old) > 0.1f);                    // hold / 3x catch-up extrapolated with the old speed, then a snap
    CHECK(maxStep(s) < 0.085f);                    // 3x speed is 0.05 per frame
    CHECK(maxStep(s) < 0.75f * maxStep(old));
    CHECK(s.snaps == 0);
    CHECK_NEAR(s.t.back(), 1.5f, 0.01);            // ends where the sender ends
}

// v0.22, the v0.21 test: stutters on the peer on low jumps (fast trick clip). The sender's clip speed changes in
// bursts: real speed to the takeoff, 2.5x hang, 0.6x creep, finish 4x in 0.1 s at the ground contact, held at the end.
namespace {
float LowJumpTruth(int k) {
    const float s = (float)k * kNDt;
    if (s < 0.2f) return s;
    if (s < 0.35f) return 0.2f + 2.5f * (s - 0.2f);
    if (s < 0.5f) return 0.575f + 0.6f * (s - 0.35f);
    if (s < 0.6f) return 0.665f + 4.f * (s - 0.5f);
    return 1.065f;
}
float MinStep(const NetSim& r) { float m = 1e9f; for (size_t i = 1; i < r.t.size(); i++) m = std::fmin(m, r.t[i] - r.t[i - 1]); return m; }
}

TEST(net_remote_clock_never_goes_back_on_a_low_jump) {
    ClipSet set; set.generation = 1;
    Clip c = NClip("k.sanim", ST_Trick, kNLUpper);
    c.loop = false; c.duration = 1.1f;
    set.clips.push_back(c);
    const int frames = 60;
    const NetSim s = RunNetSim(set, "k.sanim", true, LowJumpTruth, frames);
    const NetSim old = RunNetSim(set, "k.sanim", true, LowJumpTruth, frames, false);
    const float back = -Player::kNetBackRate * kNDt - 1e-5f;   // the slow drift back toward a sender that stopped
    CHECK(MinStep(old) < -0.02f);   // the old rule jumped back in time (the stutter)
    CHECK(MinStep(s) >= back);       // now: at most a slow drift
    auto worstGap = [&](const NetSim& r) {   // distance to the sender (who is only known 2..6 frames late)
        float w = 0.f;
        const int first = frames - (int)r.t.size();
        for (size_t i = 0; i < r.t.size(); i++) w = std::fmax(w, std::fabs(r.t[i] - LowJumpTruth(first + (int)i)));
        return w;
    };
    std::printf("low jump: worst gap to the sender %.3f s (old rule %.3f), min step %.4f s (old rule %.4f)\n",
                worstGap(s), worstGap(old), MinStep(s), MinStep(old));
    CHECK(worstGap(s) <= worstGap(old) + 0.02f);   // smoother without being further behind
    CHECK_NEAR(s.t.back(), 1.065f, 0.01);           // ends where the sender ends
    CHECK(s.snaps == 0);
    // the hold-then-3x trick of v0.20 does not jump back either
    Clip h = NClip("h.sanim", ST_Trick, kNLUpper);
    h.loop = false; h.duration = 1.5f;
    ClipSet hs; hs.generation = 1; hs.clips.push_back(h);
    CHECK(MinStep(RunNetSim(hs, "h.sanim", true, TrickTruth, 120)) >= back);
}

TEST(net_remote_clock_snaps_on_a_big_gap) {
    ClipSet set; set.generation = 1;
    set.clips.push_back(NClip("m.sanim", ST_Manual, kNLUpper));
    auto b = NP();
    NetFrame f; f.seq = 1; f.n = 1;
    f.l[0] = NetLayer{set.clips[0].netId, set.clips[0].netHash, false, false, false, 0.1f, 1.f, 1.f, 0.f, 1.f};
    std::vector<FTr> cb = NR().ref;
    b->ApplyRemote(cb.data(), NR().n, &set, f, 0.f, kNDt);
    CHECK_NEAR(b->RemoteTime(0), 0.1f, 1e-5);      // a new layer takes the sent time at once
    f.seq = 2; f.l[0].t = 0.6f;                    // the sender jumped (clip restarted elsewhere)
    Player::NetClockStats clk;
    cb = NR().ref;
    b->ApplyRemote(cb.data(), NR().n, &set, f, 0.f, kNDt, nullptr, &clk);
    CHECK_NEAR(b->RemoteTime(0), 0.6f, 1e-5);
    CHECK(clk.snaps == 1 && clk.newFrame);
    f.seq = 3; f.l[0].t = 0.6f + 2 * kNDt + 0.05f; // small gap: pulled softly, not jumped
    cb = NR().ref;
    b->ApplyRemote(cb.data(), NR().n, &set, f, 0.f, kNDt, nullptr, &clk);
    const float t = b->RemoteTime(0);
    CHECK(t > 0.6f + kNDt && t < 0.6f + 2 * kNDt + 0.05f);
    CHECK(clk.snaps == 0);
}

// 2026-11-26 v0.21, the user's case: my friend has his kickflip A and my B (in others\), I have my B and his A
// (in others\). Each plays his own on himself, and sees the other's on the other.
TEST(net_each_player_keeps_his_clip_and_sees_the_other_one) {
    Clip a = NClip("KickFlip_RGS_regular.sanim", ST_Manual, kNLUpper);
    Clip b = NClip("KickFlip_RGS_regular.sanim", ST_Manual, kNLUpper + 1);   // same name, other content
    b.netId ^= 0x5a5a5a5au;
    ClipSet friendSet; friendSet.generation = 1;   // friend: A is his, B is in others\ (and would win on priority)
    friendSet.clips.push_back(a);
    friendSet.clips.push_back(b); friendSet.clips.back().foreign = true; friendSet.clips.back().priority = 5;
    friendSet.clips.back().file = "others\\KickFlip_RGS_regular.sanim";
    ClipSet mySet; mySet.generation = 1;           // me: B is mine, A is in others\ (first in file order)
    mySet.clips.push_back(a); mySet.clips.back().foreign = true; mySet.clips.back().file = "others\\a.sanim";
    mySet.clips.push_back(b);
    auto fr = NP(), me = NP(), frSeenHere = NP(), meSeenThere = NP();
    std::vector<FTr> cf, cm;
    NetFrame ff, fm;
    for (int k = 0; k < 40; k++) {
        cf = NR().ref; fr->Apply(cf.data(), NR().n, &friendSet, NGS(true), kNDt); fr->ExportNet(ff, kNDt);
        cm = NR().ref; me->Apply(cm.data(), NR().n, &mySet, NGS(true), kNDt); me->ExportNet(fm, kNDt);
    }
    CHECK(std::string(fr->CurrentClipName()) == a.name && std::string(me->CurrentClipName()) == b.name);
    CHECK(ff.n == 1 && ff.l[0].id == a.netId && fm.n == 1 && fm.l[0].id == b.netId);
    std::vector<FTr> onFriend = NR().ref, onMe = NR().ref;
    int miss = -1;
    CHECK(frSeenHere->ApplyRemote(onFriend.data(), NR().n, &mySet, ff, 0.f, kNDt, &miss) > 0 && miss == 0);
    CHECK(meSeenThere->ApplyRemote(onMe.data(), NR().n, &friendSet, fm, 0.f, kNDt, &miss) > 0 && miss == 0);
    CHECK(MaxDiff(onFriend, cf) < 1e-4f);   // I see his A on him
    CHECK(MaxDiff(onMe, cm) < 1e-4f);       // he sees my B on me
    CHECK(MaxDiff(cf, cm) > 0.01f);         // and they are really two different clips
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
