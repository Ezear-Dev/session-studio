// AnimStudioNative v0.5 -- P3 of the AnimStudio project (Session: Skate Sim, UE4SS 3.0.1 C++): the clip player.
//
// Plays .sanim clips (Mods\AnimStudio\clips\) on the FINISHED pose of the player's skater: triggered by manual /
// nose manual, additive or replacing, with fades, goofy mirroring, hot reload and pose capture.
// The logic lives in src/ (tested outside the game by asn_tests); this file keeps the P2 installation and links it
// to the Lua half (Mods/AnimStudio). See DEVELOPMENT.md.
//
// WHERE: the vtable slot of USkaterSkeletalMeshComponent that holds FinalizeBoneTransform (vtable + 0x938),
// read on the live mesh. When this slot is called, the EDITABLE pose buffer (TArray<FTransform> at
// +0x4b0 + i*0x10, i = int at +0x4f0) holds the finished pose of the frame, not yet flipped nor published.
// Method and offsets taken from ExpandedTricksNative (Portman2, GPL-3.0) and from SessionTweaks / OpenMP (matsix,
// GPL-3.0). The header third_party/ue4ss_abi/ue4ss_abi.h comes from SessionOpenMP. This mod is therefore GPL-3.0.
//
// ORDER WITH THE OTHER MODS (unchanged since P2). ExpandedTricksNative takes this slot ONCE per launch, when the
// skater appears, and REFUSES (poses switched off) if it finds anything other than the engine function. So:
//   * ExpandedTricksNative enabled (enabled.txt present): we wait until it has taken the slot and chain behind it
//     (our layer, then its own, then the engine, where SessionTweaks does its pre-hook). If it has not taken it
//     within 60 s, we do not install.
//   * ExpandedTricksNative absent: we install right away on the engine function.
// The slot is only restored if it still holds our function.
//
// SAFETY: the hook only reads the mesh whose address the Lua gave, and checks that the buffer has the announced
// bone count. No UObject is read here. The hook never blocks: it takes the player lock with TryAcquire and
// skips the frame if the Lua is changing the skeleton. The computation runs under __try.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <share.h>
#include "ue4ss_abi/ue4ss_abi.h"
#include "src/math.h"
#include "src/player.h"
#include "src/clipset.h"
#include "src/capture.h"
#include "src/skeleton.h"
#include "src/figure.h"
#include "src/trick.h"
#include "src/net.h"
#include "src/landing_hold.h"
#include <map>
#include <set>

using Lua = RC::LuaMadeSimple::Lua;

namespace {

constexpr int kVersion = 23;
constexpr int SKM_CST = 0x4b0;    // USkinnedMeshComponent::ComponentSpaceTransformsArray[2] (0x10 apart)
constexpr int SKM_EDIT = 0x4f0;   // USkinnedMeshComponent::CurrentEditableComponentTransforms
constexpr int VT_FBT = 0x938;     // FinalizeBoneTransform slot in the skater mesh vtable
constexpr int MAX_BONES = Player::kMaxBones;

struct TArr { FTr* data; int32_t num; int32_t max; };

// ------------------------------------------------------------------ paths (next to the exe)
std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return L"";
    slash[1] = 0;
    return path;
}
std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ------------------------------------------------------------------ log (AnimStudio.native.log next to the exe)
FILE* g_log = nullptr;
void Log(const char* fmt, ...) {
    if (!g_log) {
        const std::wstring p = ExeDir() + L"AnimStudio.native.log";
        if (p.size() <= 21) return;
        g_log = _wfsopen(p.c_str(), L"w", _SH_DENYNO);   // rewritten on every launch, like UE4SS.log
        if (!g_log) return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

// ------------------------------------------------------------------ state
typedef void (*FbtFn)(void* self);
FbtFn  g_prev = nullptr;          // what was in the slot (ExpandedTricks or the engine)
void** g_slot = nullptr;
int    g_state = 0;               // 0 waiting, 1 installed, -1 refused (see g_why)
char   g_why[200] = "no mesh yet";
void* volatile g_mesh = nullptr;
ULONGLONG g_firstMeshAt = 0;      // GetTickCount64 of the first mesh received (waiting for ExpandedTricks)

SRWLOCK g_lock = SRWLOCK_INIT;    // protects g_player (skeleton); the hook only does TryAcquire
Player* g_player = nullptr;
ClipLibrary* g_lib = nullptr;
std::vector<std::string> g_names; // bone names (game thread)
MirrorMap g_map;

GameState g_gs[2] = {};           // double buffer written by the Lua, read by the hook
volatile LONG g_gsIdx = 0;

volatile LONG g_capReq = 0, g_capReady = 0;
Quat g_capBuf[MAX_BONES];

// P4: original animations of the moves (recorded in the hook, written on the game thread) and skeleton for the editor
FigureRecorder g_rec;
Quat g_recLocals[MAX_BONES];
float g_recPos[MAX_BONES][3];

// P6: figure variants (game thread). id = index + 1 in g_figKeys.
std::vector<std::string> g_figKeys;
std::map<std::string, int> g_figIds;          // lower-case key -> id
std::set<std::string> g_figHave, g_figRedo;   // lower-case keys of _figures\*.json / *.refaire (refreshed every scan)
bool g_figScanned = false;
std::string g_trickBase;                      // last trick given by ASN_SetTrick
std::string g_grindName;                      // 2026-11-25: grind in progress given by ASN_SetGrind ("" = unknown)
uint32_t g_grindHash = 0;
int g_trickStance = 0;
uint32_t g_trickHash = 0;
int FigId(const std::string& key) {
    const std::string k = Lower(key);
    auto it = g_figIds.find(k);
    if (it != g_figIds.end()) return it->second;
    g_figKeys.push_back(key);
    const int id = (int)g_figKeys.size();
    g_figIds[k] = id;
    return id;
}
std::vector<int> g_parents;          // game thread
std::vector<float> g_refPos;         // game thread
std::string g_lastSkeleton;          // last text written to _skeleton.json
int  g_capN = 0;
bool g_capMirrored = false;

// current clip name and layer count, published by the hook (double buffer): the Lua never reads the Player
char g_clipName[2][128] = {};
volatile LONG g_clipIdx = 0;
volatile LONG g_layers = 0;
void PublishClipName(const char* s, int layers) {
    const LONG cur = g_clipIdx & 1;
    g_layers = layers;
    if (strcmp(g_clipName[cur], s) == 0) return;
    const LONG next = cur ^ 1;
    strncpy_s(g_clipName[next], s, _TRUNCATE);
    InterlockedExchange(&g_clipIdx, next);
}
const char* ClipNow() { return g_clipName[g_clipIdx & 1]; }

LARGE_INTEGER g_freq{}, g_last{};
volatile LONG g_calls = 0, g_mine = 0, g_applied = 0, g_badNum = 0, g_badEdit = 0, g_faults = 0, g_busy = 0;
int g_lastNum = -1;
double g_usSum = 0.0, g_usMax = 0.0;
long g_usCount = 0;

// ------------------------------------------------------------------ the pose layer (in the hook)
// ------------------------------------------------------------------ P6 diagnostics: legs during a trick
// Every frame from the pop to 1.2 s after the trick, the hook measures both legs at four stages:
//   G = game pose before our layer, A = after our layer, E = after ExpandedTricks / SessionTweaks / the engine,
//   P = after our leg post-pass, R = what was really published (read buffer, measured at the start of the next frame).
// thigh / shin: length minus the reference length (cm, + = stretched); knee: rotation of the calf off its hinge (deg);
// ankle: twist of the foot around the shin (deg). One line every ~0.05 s, and the worst value of the trick.
int g_leg[2][3] = {{-1, -1, -1}, {-1, -1, -1}};   // [left/right][thigh, calf, foot]
float g_legRef[2][2] = {};
struct LegM { float thigh, shin, knee, ankle; };
LegM MeasureLeg(const FTr* cs, int side) {
    LegM m{0, 0, 0, 0};
    const int t = g_leg[side][0], c = g_leg[side][1], f = g_leg[side][2];
    if (t < 0 || c < 0 || f < 0) return m;
    auto dist = [](const FTr& a, const FTr& b) { return std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) + (a.pz - b.pz) * (a.pz - b.pz)); };
    m.thigh = dist(cs[t], cs[c]) - g_legRef[side][0];
    m.shin = dist(cs[c], cs[f]) - g_legRef[side][1];
    const Quat kq = QNorm(QMul(QInv(TrQ(cs[t])), TrQ(cs[c])));   // calf in thigh space: the knee bends around Z
    m.knee = 2.f * std::asin(std::fmin(1.f, std::sqrt(kq.x * kq.x + kq.y * kq.y))) * 57.2958f;
    const Quat aq = QNorm(QMul(QInv(TrQ(cs[c])), TrQ(cs[f])));   // foot in calf space: twist around the shin (X)
    float tw = 2.f * std::atan2(aq.x, aq.w) * 57.2958f;
    while (tw > 180.f) tw -= 360.f;
    while (tw < -180.f) tw += 360.f;
    m.ankle = tw;
    return m;
}
struct Diag {
    bool on = false;
    uint32_t pop = 0;
    LARGE_INTEGER popAt{};
    double until = 0.0, lastLine = -1.0;
    LegM st[4][2] = {};   // G, A, E, P this frame
    bool haveP = false;
    float worstShin[5] = {}, worstThigh[5] = {}, worstT[5] = {};
    char wrote[2][8] = {};
} g_diag;
double Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart / (double)g_freq.QuadPart; }
double SincePop() { return Now() - (double)g_diag.popAt.QuadPart / (double)g_freq.QuadPart; }
void DiagWorst(int stage, const LegM* m) {
    for (int s = 0; s < 2; s++) {
        if (std::fabs(m[s].shin) > std::fabs(g_diag.worstShin[stage])) { g_diag.worstShin[stage] = m[s].shin; g_diag.worstT[stage] = (float)SincePop(); }
        if (std::fabs(m[s].thigh) > std::fabs(g_diag.worstThigh[stage])) g_diag.worstThigh[stage] = m[s].thigh;
    }
}
void DiagReport() {
    static const char* kSt = "GAEPR";
    Log("diag legs, pop %u, worst over the trick (cm, + = stretched):", (unsigned)g_diag.pop);
    for (int s = 0; s < 5; s++) Log("  %c: shin %+6.2f at %.2f s, thigh %+6.2f", kSt[s], g_diag.worstShin[s], g_diag.worstT[s], g_diag.worstThigh[s]);
}
// game thread, in the hook (before our layer): start / stop the window, measure G and the published pose R
void DiagPre(uint8_t* mesh, const GameState& gs) {
    if (gs.kickflip && gs.popSerial != g_diag.pop) {
        if (g_diag.on) DiagReport();
        g_diag = Diag{};
        g_diag.on = true;
        g_diag.pop = gs.popSerial;
        QueryPerformanceCounter(&g_diag.popAt);
        Log("diag legs, pop %u: t  phase | L: thigh shin knee ankle per stage G A E P | R: same", (unsigned)gs.popSerial);
    }
    if (!g_diag.on) return;
    if (gs.kickflip) g_diag.until = SincePop() + 1.2;
    else if (SincePop() > g_diag.until) { DiagReport(); g_diag.on = false; return; }
    const int edit = *(int32_t*)(mesh + SKM_EDIT);
    const int n = g_player->RigBones();
    if (edit < 0 || edit > 1) return;
    TArr* rd = (TArr*)(mesh + SKM_CST + (edit ^ 1) * 0x10);   // last published pose (double buffer)
    if (rd->data && rd->num == n && g_diag.haveP) {
        LegM r[2] = {MeasureLeg(rd->data, 0), MeasureLeg(rd->data, 1)};
        DiagWorst(4, r);
    }
    TArr* ed = (TArr*)(mesh + SKM_CST + edit * 0x10);
    if (ed->data && ed->num == n) { g_diag.st[0][0] = MeasureLeg(ed->data, 0); g_diag.st[0][1] = MeasureLeg(ed->data, 1); DiagWorst(0, g_diag.st[0]); }
}
void DiagStage(int stage, const FTr* cs) {   // 1 A (after our layer), 2 E (after the others), 3 P (after the post-pass)
    if (!g_diag.on || !cs) return;
    g_diag.st[stage][0] = MeasureLeg(cs, 0);
    g_diag.st[stage][1] = MeasureLeg(cs, 1);
    DiagWorst(stage, g_diag.st[stage]);
    if (stage != 3) return;
    g_diag.haveP = true;
    const double t = SincePop();
    if (t - g_diag.lastLine < 0.045) return;
    g_diag.lastLine = t;
    const GameState gs = g_gs[g_gsIdx & 1];
    char buf[400];
    int k = snprintf(buf, sizeof(buf), "  %.2f ph%d", t, gs.kickflip ? gs.trickPhase : 9);
    for (int s = 0; s < 2; s++) {
        k += snprintf(buf + k, sizeof(buf) - k, " | %c", s ? 'R' : 'L');
        for (int st = 0; st < 4 && k < (int)sizeof(buf) - 40; st++)
            k += snprintf(buf + k, sizeof(buf) - k, " %+.1f/%+.1f/%.0f/%+.0f", g_diag.st[st][s].thigh, g_diag.st[st][s].shin, g_diag.st[st][s].knee, g_diag.st[st][s].ankle);
    }
    Log("%s", buf);
}
void DiagSetRig(const std::vector<std::string>& names, const std::vector<float>& refPos) {
    const char* kNames[2][3] = {{"AMXX_L_Thigh", "AMXX_L_Calf", "AMXX_L_Foot"}, {"AMXX_R_Thigh", "AMXX_R_Calf", "AMXX_R_Foot"}};
    for (int s = 0; s < 2; s++) for (int j = 0; j < 3; j++) {
        g_leg[s][j] = -1;
        for (int i = 0; i < (int)names.size(); i++) if (names[i] == kNames[s][j]) g_leg[s][j] = i;
    }
    for (int s = 0; s < 2; s++) for (int j = 0; j < 2; j++) {
        const int b = g_leg[s][j + 1];
        g_legRef[s][j] = (b >= 0 && refPos.size() >= (size_t)(b + 1) * 3)
            ? std::sqrt(refPos[b * 3] * refPos[b * 3] + refPos[b * 3 + 1] * refPos[b * 3 + 1] + refPos[b * 3 + 2] * refPos[b * 3 + 2]) : 0.f;
    }
}

// 2026-11-26 multiplayer: our layers for the other players (written by the hook after Apply, read by ASN_NetPacket
// on the game thread)
SRWLOCK g_netLock = SRWLOCK_INIT;
NetFrame g_netOut, g_netTmp;
double g_netAt = 0.0;                  // Now() of the last export
uint32_t g_netSeq = 0;

void ApplyPose(uint8_t* mesh) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = 0.0f;
    if (g_last.QuadPart && g_freq.QuadPart) dt = (float)((double)(now.QuadPart - g_last.QuadPart) / (double)g_freq.QuadPart);
    g_last = now;

    const int edit = *(int32_t*)(mesh + SKM_EDIT);
    if (edit < 0 || edit > 1) { InterlockedIncrement(&g_badEdit); return; }
    TArr* arr = (TArr*)(mesh + SKM_CST + edit * 0x10);
    g_lastNum = arr->num;
    const int n = g_player->RigBones();
    if (!arr->data || n <= 0 || arr->num != n) { InterlockedIncrement(&g_badNum); return; }

    const GameState gs = g_gs[g_gsIdx & 1];
    DiagPre(mesh, gs);
    const int changed = g_player->Apply(arr->data, n, g_lib->Current(), gs, dt);
    DiagStage(1, arr->data);
    if (changed > 0) InterlockedIncrement(&g_applied);
    PublishClipName(g_player->CurrentClipName(), g_player->LayerCount());
    g_player->ExportNet(g_netTmp, dt);   // 2026-11-26 multiplayer (hook thread only: g_netTmp is ours)
    if (TryAcquireSRWLockExclusive(&g_netLock)) {
        g_netOut = g_netTmp;
        g_netOut.seq = ++g_netSeq;
        g_netAt = Now();
        ReleaseSRWLockExclusive(&g_netLock);
    }

    // original animation of the moves: the game's pose, before our layer (copy only)
    g_player->SnapshotGameLocals(g_recLocals, n);
    g_player->SnapshotGamePositions(g_recPos, n);
    g_rec.Tick(gs, g_recLocals, g_recPos, n, dt);

    if (g_capReq && !g_capReady) {   // capture: the game's pose, before our layer
        g_player->SnapshotGameLocals(g_capBuf, n);
        g_capN = n;
        g_capMirrored = (gs.goofy != gs.sw);
        InterlockedExchange(&g_capReq, 0);
        InterlockedExchange(&g_capReady, 1);
    }

    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    const double us = (double)(end.QuadPart - now.QuadPart) * 1e6 / (double)g_freq.QuadPart;
    g_usSum += us;
    g_usCount++;
    if (us > g_usMax) g_usMax = us;
}

void SafeApply(void* self) {
    __try { ApplyPose((uint8_t*)self); }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (InterlockedIncrement(&g_faults) <= 3) Log("fault in the pose layer (code %08x) -- frame ignored", GetExceptionCode()); }
}

// P6: second pass on the legs, AFTER ExpandedTricks, SessionTweaks and the engine (see Player::PostApply).
// The buffer written by the first pass is the one the engine has just published (same TArray, read before g_prev).
volatile LONG g_post = 0;
void PostPose(TArr* arr, FTr* data, int n) {
    if (arr->data != data || arr->num != n || n != g_player->RigBones()) return;
    DiagStage(2, data);
    if (g_player->PostApply(data, n) > 0) InterlockedIncrement(&g_post);
    DiagStage(3, data);
}
void SafePost(TArr* arr, FTr* data, int n) {
    __try { PostPose(arr, data, n); }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (InterlockedIncrement(&g_faults) <= 3) Log("fault in the leg post pass (code %08x) -- frame ignored", GetExceptionCode()); }
}
bool PostTarget(uint8_t* mesh, TArr*& arr, FTr*& data, int& n) {
    __try {
        const int edit = *(int32_t*)(mesh + SKM_EDIT);
        if (edit < 0 || edit > 1) return false;
        arr = (TArr*)(mesh + SKM_CST + edit * 0x10);
        data = arr->data;
        n = arr->num;
        return data != nullptr && n > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ------------------------------------------------------------------ 2026-11-26 multiplayer (v0.19)
// Other players' skaters (SessionOpenMP proxies) are game-class skaters: their mesh calls this same slot (proved by
// the v0.18 test, 2026-11-26). Each player's AnimStudio sends its layers (net.h) through the OpenMP mod channel; the
// Lua gives us each peer's mesh (ASN_SetPeer) and the frames it receives (ASN_NetReceive). Here, on a peer's mesh, a
// Player of its own plays those layers (Player::ApplyRemote) with OUR copy of the same clip files.
// Locks: g_peerLock (exclusive: game thread, ASN_SetPeer / ASN_SetRig; shared: the hook, never waiting) guards the
// slots and their Player; each slot's frame has its own lock (written by ASN_NetReceive, copied by the hook).
constexpr int kPeers = 4;
struct PeerSlot {
    void* volatile mesh = nullptr;
    Player* pl = nullptr;              // created on first use, never freed (a hook call may still use it)
    bool rigOk = false;
    SRWLOCK frameLock = SRWLOCK_INIT;
    NetFrame frame;                    // last frame received (frameLock)
    double frameAt = 0.0;              // Now() when it arrived (frameLock)
    bool haveFrame = false;
    NetFrame cache;                    // hook-side copy (used when the frame lock is busy)
    double cacheAt = 0.0;
    bool haveCache = false;
    double last = 0.0;                 // Now() of the last hook call (dt)
    volatile LONG calls = 0, applied = 0, faults = 0, badNum = 0, frames = 0, posts = 0;
    volatile LONG missing = 0;         // layers of the last applied frame whose clip is not here
    volatile LONG ageMs = 0;
    // receiver clock (Player::NetClockStats): jumps the old rule would have made at each new frame, in clip ms;
    // the sums and the max are reset by each stats line
    volatile LONG jumpN = 0, jumpSumMs = 0, jumpMaxMs = 0, snaps = 0;
};
PeerSlot g_peers[kPeers];
SRWLOCK g_peerLock = SRWLOCK_INIT;
volatile LONG g_otherCalls = 0;        // slot calls on a mesh that is neither ours nor a known peer

void PeerApplyImpl(PeerSlot& p, uint8_t* mesh, TArr*& arr, FTr*& data, int& n, bool& post) {
    const int edit = *(int32_t*)(mesh + SKM_EDIT);
    if (edit < 0 || edit > 1) return;
    arr = (TArr*)(mesh + SKM_CST + edit * 0x10);
    data = arr->data;
    n = arr->num;
    if (!data || n <= 0 || n != p.pl->RigBones()) { InterlockedIncrement(&p.badNum); return; }
    if (TryAcquireSRWLockShared(&p.frameLock)) {
        if (p.haveFrame) { p.cache = p.frame; p.cacheAt = p.frameAt; p.haveCache = true; }
        ReleaseSRWLockShared(&p.frameLock);
    }
    if (!p.haveCache) return;
    const double now = Now();
    const float dt = p.last > 0.0 ? (float)(now - p.last) : 0.f;
    p.last = now;
    const float age = (float)(now - p.cacheAt);
    p.ageMs = (LONG)(age * 1000.f);
    int miss = 0;
    Player::NetClockStats clk;
    if (p.pl->ApplyRemote(data, n, g_lib->Current(), p.cache, age, dt, &miss, &clk) > 0) InterlockedIncrement(&p.applied);
    p.missing = miss;
    if (clk.newFrame && age < Player::kNetStale) {
        const LONG ms = (LONG)(clk.jump * 1000.f + 0.5f);
        InterlockedIncrement(&p.jumpN);
        InterlockedExchangeAdd(&p.jumpSumMs, ms);
        if (ms > p.jumpMaxMs) p.jumpMaxMs = ms;
    }
    if (clk.snaps) InterlockedExchangeAdd(&p.snaps, clk.snaps);
    post = true;
}
void PeerApply(PeerSlot& p, uint8_t* mesh, TArr*& arr, FTr*& data, int& n, bool& post) {
    __try { PeerApplyImpl(p, mesh, arr, data, n, post); }
    __except (EXCEPTION_EXECUTE_HANDLER) { post = false; if (InterlockedIncrement(&p.faults) <= 3) Log("net: fault on a peer pose (code %08x), frame skipped", GetExceptionCode()); }
}
void PeerPostImpl(PeerSlot& p, TArr* arr, FTr* data, int n) {
    if (arr->data != data || arr->num != n || n != p.pl->RigBones()) return;
    if (p.pl->PostApply(data, n) > 0) InterlockedIncrement(&p.posts);
}
void PeerPost(PeerSlot& p, TArr* arr, FTr* data, int n) {
    __try { PeerPostImpl(p, arr, data, n); }
    __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedIncrement(&p.faults); }
}

void hkFbt(void* self) {
    InterlockedIncrement(&g_calls);
    TArr* arr = nullptr;
    FTr* data = nullptr;
    int n = 0;
    bool post = false;
    PeerSlot* peer = nullptr;          // a peer's mesh: g_peerLock is held (shared) until after g_prev
    if (self && self != g_mesh && g_lib) {
        if (TryAcquireSRWLockShared(&g_peerLock)) {
            for (PeerSlot& p : g_peers) if (p.mesh && p.mesh == self) { peer = &p; break; }
            if (peer && peer->pl && peer->rigOk) {
                InterlockedIncrement(&peer->calls);
                PeerApply(*peer, (uint8_t*)self, arr, data, n, post);
            } else {
                if (!peer) InterlockedIncrement(&g_otherCalls);
                peer = nullptr;
                ReleaseSRWLockShared(&g_peerLock);
            }
        }
    }
    if (self && self == g_mesh && g_player && g_lib) {
        InterlockedIncrement(&g_mine);
        if (TryAcquireSRWLockExclusive(&g_lock)) {   // never wait in the hook
            post = PostTarget((uint8_t*)self, arr, data, n);
            SafeApply(self);
            ReleaseSRWLockExclusive(&g_lock);
        } else InterlockedIncrement(&g_busy);
    }
    g_prev(self);
    if (peer) {
        if (post) PeerPost(*peer, arr, data, n);
        ReleaseSRWLockShared(&g_peerLock);
        return;
    }
    if (post && TryAcquireSRWLockExclusive(&g_lock)) {
        SafePost(arr, data, n);
        ReleaseSRWLockExclusive(&g_lock);
    }
}

// ------------------------------------------------------------------ installation into the slot (unchanged since P2)
bool EtEnabled() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return false;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"Mods\\ExpandedTricksNative\\enabled.txt");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

std::string ModuleOf(void* p) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m) || !m) return "?";
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(m, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "?";
    return std::string(path);
}

void SetWhy(int state, const char* fmt, ...) {
    char buf[200];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    if (state != g_state || strcmp(buf, g_why) != 0) Log("installation: %s", buf);
    g_state = state;
    strcpy_s(g_why, buf);
}

int TryInstallImpl(void* mesh) {
    uint8_t* exe = (uint8_t*)GetModuleHandleW(nullptr);
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(exe + ((const IMAGE_DOS_HEADER*)exe)->e_lfanew);
    const uintptr_t image = nt->OptionalHeader.SizeOfImage;
    uint8_t* vt = *(uint8_t**)mesh;
    if (vt < exe || vt + VT_FBT + 8 > exe + image) { SetWhy(-1, "refused: the mesh vtable (%p) is not inside the exe -- not installed", vt); return -1; }
    void** slot = (void**)(vt + VT_FBT);
    void* cur = *slot;
    if (cur == (void*)&hkFbt) { g_slot = slot; SetWhy(1, "already installed in the slot"); return 1; }
    const bool curInExe = (uint8_t*)cur >= exe && (uint8_t*)cur < exe + image;
    if (curInExe && EtEnabled()) {
        const ULONGLONG waited = GetTickCount64() - g_firstMeshAt;
        if (waited < 60000) { SetWhy(0, "the slot still holds the engine function (exe+%llx): waiting for ExpandedTricksNative to take it", (unsigned long long)((uint8_t*)cur - exe)); return 0; }
        SetWhy(-1, "refused: ExpandedTricksNative is enabled but did not take the slot within 60 s: not installing (it would switch its poses off)");
        return -1;
    }
    const std::string owner = curInExe ? std::string("the engine") : ModuleOf(cur);
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { SetWhy(-1, "refused: VirtualProtect failed (%lu)", GetLastError()); return -1; }
    g_prev = (FbtFn)cur;
    *slot = (void*)&hkFbt;
    VirtualProtect(slot, sizeof(void*), old, &old);
    g_slot = slot;
    if (curInExe) SetWhy(1, "installed in the slot @ %p, chained on the engine exe+%llx", slot, (unsigned long long)((uint8_t*)cur - exe));
    else SetWhy(1, "installed in the slot @ %p, chained behind %p (%s)", slot, cur, owner.c_str());
    return 1;
}

int TryInstall() {
    void* mesh = g_mesh;
    if (!mesh) return g_state;
    if (g_state == 1 || g_state == -1) return g_state;
    __try { return TryInstallImpl(mesh); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("fault while reading the mesh vtable (code %08x) -- not installed", GetExceptionCode()); g_state = -1; return -1; }
}

// ------------------------------------------------------------------ calls from the Lua (game thread)
bool ArgNum(const Lua& lua, double& out) {
    if (lua.get_stack_size() < 1) return false;
    if (!lua.is_number()) { lua.discard_value(); return false; }
    out = lua.get_number();
    return true;
}
bool ArgStr(const Lua& lua, std::string& out) {
    if (lua.get_stack_size() < 1) return false;
    if (!lua.is_string()) { lua.discard_value(); return false; }
    const std::string_view v = lua.get_string();
    out.assign(v.data(), v.size());
    return true;
}
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ') j++;
        if (j > i) v.push_back(s.substr(i, j - i));
        i = j;
    }
    return v;
}
void LogLines(const std::vector<std::string>& lines, std::string* joined) {
    for (auto& l : lines) {
        Log("clips: %s", l.c_str());
        if (joined) { if (!joined->empty()) *joined += '\n'; *joined += l; }
    }
}

int nVersion(const Lua& lua) { lua.set_integer(kVersion); return 1; }

// ------------------------------------------------------------------ files for the editor (game thread)
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
// writes <path>.tmp then renames: a reader never sees a half-written file
bool WriteFileAtomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfsopen(tmp.c_str(), L"wb", _SH_DENYNO);
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
void WriteSkeletonIfChanged() {
    if (g_names.empty() || !g_lib) return;
    SkeletonFile s;
    s.names = g_names;
    s.parents = g_parents;
    s.refPos = g_refPos.size() == g_names.size() * 3 ? g_refPos : std::vector<float>(g_names.size() * 3, 0.f);
    s.conv = EffectiveConv(g_map, g_lib->Current()->convOverride);
    const std::string text = SkeletonToJson(s);
    if (text == g_lastSkeleton) return;
    CreateDirectoryW(g_lib->Dir().c_str(), nullptr);
    if (WriteFileAtomic(g_lib->Dir() + L"\\_skeleton.json", text)) { g_lastSkeleton = text; Log("skeleton written for the editor (_skeleton.json, %d bones)", (int)s.names.size()); }
    else Log("skeleton: cannot write _skeleton.json");
}

// ASN_SetRig("p0 p1 ... pN-1", "name0 name1 ... nameN-1", "x0 y0 z0 x1 ...") -> bone count, 0 if refused
// 3rd argument: reference local positions (GetRefPosePosition) in HUNDREDTHS of a cm, integers (no decimal
// separator to parse: the process locale plays no part). Used to deduce the mirror conventions.
// 2026-11-26 multiplayer: a peer's Player gets OUR skeleton (game thread, g_peerLock held exclusive by the caller).
// A peer mesh with another bone count is refused frame by frame (ApplyRemote checks it).
void ConfigurePeer(PeerSlot& p) {
    const int n = (int)g_parents.size();
    p.rigOk = false;
    if (!p.pl || n <= 0 || (int)g_names.size() != n) return;
    p.pl->SetRig(g_parents.data(), n, g_map);
    std::vector<bool> isLeg((size_t)n);
    for (int i = 0; i < n; i++) isLeg[i] = IsLegBone(g_names[i]);
    p.pl->SetRefPositions(g_refPos);
    p.pl->SetLegBones(isLeg);
    p.pl->SetPelvisRig(BuildPelvisRig(g_names, g_parents.data(), n));
    p.rigOk = true;
}

int nSetRig(const Lua& lua) {
    std::string par, nm, pos;
    ArgStr(lua, par);
    ArgStr(lua, nm);
    ArgStr(lua, pos);
    const std::vector<std::string> ps = Split(par);
    std::vector<std::string> names = Split(nm);
    const std::vector<std::string> pp = Split(pos);
    std::vector<float> refPos;
    if (pp.size() == ps.size() * 3) for (auto& s : pp) refPos.push_back((float)atoi(s.c_str()) / 100.f);
    const int n = (int)ps.size();
    std::vector<int> parent(n);
    bool ok = n > 0 && n <= MAX_BONES && (int)names.size() == n;
    for (int i = 0; i < n && ok; i++) {
        parent[i] = atoi(ps[i].c_str());
        if (parent[i] >= i || parent[i] < -1) ok = false;   // a parent always comes before its children
    }
    if (!ok) { Log("bone table refused (%d parents, %d names)", n, (int)names.size()); lua.set_integer(0); return 1; }
    MirrorMap map = BuildMirrorMap(names, parent, refPos);
    AcquireSRWLockExclusive(&g_lock);    // the hook skips at most one frame
    g_player->SetRig(parent.data(), n, map);
    std::vector<bool> isLeg((size_t)n);
    for (int i = 0; i < n; i++) isLeg[i] = IsLegBone(names[i]);
    g_player->SetRefPositions(refPos);   // P6: own bone lengths for the legs our clips write
    g_player->SetLegBones(isLeg);        // leg bone mask (leg weight of a trick clip)
    g_player->SetPelvisRig(BuildPelvisRig(names, parent.data(), n));   // P6 lot 2: pelvis offset, feet kept
    std::vector<int> posBones;
    for (int i = 0; i < n; i++) if (IsPositionBone(names[i])) posBones.push_back(i);
    g_rec.Reset(n, posBones);           // a move recording never mixes two skeletons
    ReleaseSRWLockExclusive(&g_lock);
    g_map = map;
    g_names = names;
    DiagSetRig(names, refPos);
    g_parents = parent;
    g_refPos = refPos;
    AcquireSRWLockExclusive(&g_peerLock);   // 2026-11-26: the peers' players follow our skeleton
    for (PeerSlot& p : g_peers) if (p.pl) ConfigurePeer(p);
    ReleaseSRWLockExclusive(&g_peerLock);
    InterlockedExchange(&g_capReady, 0);
    int paired = 0;
    for (int i = 0; i < n; i++) if (!map.center[i] && map.other[i] != i) paired++;
    Log("bone table: %d bones (%d left/right bones paired; reference positions %s)", n, paired,
        refPos.empty() ? "MISSING: default mirror conventions" : "received");
    static const char* kConv = "IXYZ";
    std::string conv;
    for (int i = 0; i < n; i++) {
        if (names[i].rfind("SKXX_", 0) == 0 || names[i].find("_R_") != std::string::npos) continue;
        if (!conv.empty()) conv += ", ";
        conv += names[i] + "=" + kConv[map.conv[i] & 3];
    }
    Log("mirror conventions: %s", conv.c_str());
    LogLines(g_lib->SetRig(names), nullptr);
    WriteSkeletonIfChanged();
    lua.set_integer(n);
    return 1;
}

// ASN_SetMesh(address) -> state (1 installed, 0 waiting, -1 refused)
int nSetMesh(const Lua& lua) {
    double m = 0;
    ArgNum(lua, m);
    void* mesh = (void*)(uintptr_t)m;
    if (mesh != g_mesh) Log("player skater mesh: %p", mesh);
    if (mesh && !g_firstMeshAt) g_firstMeshAt = GetTickCount64();
    g_mesh = mesh;
    lua.set_integer(TryInstall());
    return 1;
}

// ASN_Poll() -> state; retries the installation while it is waiting
int nPoll(const Lua& lua) { lua.set_integer(TryInstall()); return 1; }

// ASN_Why() -> text of the state
int nWhy(const Lua& lua) { lua.set_string(g_why); return 1; }

// ASN_SetTrick(name): the FlipTrick that just popped ("RGS_KickFlip"...). Called by the Lua at the pop, before ASN_SetState.
int nSetTrick(const Lua& lua) {
    std::string name, base;
    ArgStr(lua, name);
    int stance = 0;
    if (!SplitTrickName(name, base, stance)) { base = name; stance = 0; }
    g_trickBase = base;
    g_trickStance = stance;
    g_trickHash = TrickHash(base);
    lua.set_integer(1);
    return 1;
}

// ASN_SetGrind(name): the grind the skater is on (the game's _replicatedGrindName), "" when unknown. Sent when it changes.
int nSetGrind(const Lua& lua) {
    std::string name;
    ArgStr(lua, name);
    if (name == "None") name.clear();
    if (name != g_grindName) Log("grind: %s", name.empty() ? "(unknown name)" : name.c_str());
    g_grindName = name;
    g_grindHash = name.empty() ? 0u : TrickHash(name);
    lua.set_integer(1);
    return 1;
}

// 2026-11-25 landing sync: time left to the ground contact during a trick (trick clips stretch their hang time on it),
// and one log line per trick with the real timings (pop height, takeoff, catch, IsLanding, ground, rise, gravity).
// v = the ASN_SetState numbers: [12] grounded, [19] vz cm/s, [20] dz cm above the takeoff, [21] jump read ok, [22] pop height.
void JumpStep(GameState& gs, const double* v) {
    static GravityMeter s_g;
    static double s_prevT = -1.0;
    struct J { bool on = false; uint32_t pop = 0; double popAt = 0, airAt = -1, catchAt = -1, landAt = -1; float vz0 = 0, maxDz = 0, popH = 0, predAtLand = -1; bool ok = false; uint64_t key = 0; };
    static J s_j;
    static std::map<uint64_t, LandLearn> s_learn;   // 2026-11-26: per trick and stance
    const double now = Now();
    const float dt = s_prevT < 0.0 ? 0.f : (float)(now - s_prevT);
    s_prevT = now;
    const bool ok = v[21] != 0, grounded = v[12] != 0;
    const float vz = (float)v[19], dz = (float)v[20];
    gs.predGround = -1.f;
    if (gs.kickflip) {
        if (!s_j.on || s_j.pop != gs.popSerial) { s_j = J{}; s_j.on = true; s_j.pop = gs.popSerial; s_j.popAt = now; s_j.key = ((uint64_t)gs.trickHash << 3) | (uint64_t)(gs.stance & 7); }
        const LandLearn& ll = s_learn[s_j.key];
        const double age = now - s_j.popAt;
        const bool air = !grounded && gs.trickPhase >= 1;
        s_g.Feed(vz, dt, ok && air);
        if (ok) { s_j.ok = true; if (dz > s_j.maxDz) s_j.maxDz = dz; s_j.popH = (float)v[22]; }
        if (gs.trickPhase >= 1 && s_j.airAt < 0) s_j.airAt = age;
        if (ok && vz > s_j.vz0) s_j.vz0 = vz;   // fastest rise (the first airborne frame still reads 0)
        if (gs.trickPhase == 3 && s_j.catchAt < 0) s_j.catchAt = age;
        if (ok && air) gs.predGround = PredictGround(vz, dz, s_g.G());
        if (gs.trickPhase >= 4 && s_j.landAt < 0) { s_j.landAt = age; s_j.predAtLand = gs.predGround; }
        // from IsLanding on, the prediction is corrected by what this trick really did the last times
        if (gs.trickPhase >= 4 && gs.predGround > 0.f && ll.n > 0) gs.predGround *= ll.factor;
        gs.landLead = ll.n > 0 ? ll.lead : -1.f;
    } else if (s_j.on) {
        s_j.on = false;
        const double g = now - s_j.popAt;
        LandLearn& ll = s_learn[s_j.key];
        if (s_j.landAt >= 0 && g < 2.5) ll.Learn((float)(g - s_j.landAt), s_j.predAtLand);
        Log("jump: %s_%s ground %.3f s after the pop | takeoff %.3f, catch %.3f, IsLanding %.3f (ground %.3f s later, predicted %.3f) | "
            "pop %.0f, rise %.0f cm, max vz %.0f cm/s, g %.0f (%d samples) | learned: IsLanding->ground %.3f s, factor %.2f (%d)%s",
            StancePrefix(g_trickStance), g_trickBase.c_str(), g, s_j.airAt, s_j.catchAt, s_j.landAt,
            s_j.landAt >= 0 ? g - s_j.landAt : -1.0, s_j.predAtLand, s_j.popH, s_j.maxDz, s_j.vz0, s_g.G(), s_g.Samples(), ll.lead, ll.factor, ll.n,
            s_j.ok ? "" : " | height unreadable: real time");
    }
}

// ASN_SetState(manual, noseManual, goofy, switch, manualRatio, trick, popSerial, phase, grind, powerslide, grab)
// numbers (0/1 for booleans); missing ones are 0 (older Lua).
int nSetState(const Lua& lua) {
    double v[23] = {};   // 2026-11-25 landing sync: + vz, dz, jump ok, pop height (an older Lua sends 19)
    v[11] = 1;    // P6 lot 4 (onBoard): an older Lua sends 11 numbers
    v[15] = -1;
    for (double& x : v) ArgNum(lua, x);
    // the game's replay (editor or filmer camera, CustomMovementMode 6): the skater is played back and its AnimInstance
    // stays frozen (it reads "riding, Speed 0" the whole replay). The recorded pose already holds our layers, so we stand
    // by: no state (every layer fades out) and no figure recorded from a replayed pose.
    {
        static bool s_replay = false;
        const bool replay = v[18] != 0;
        if (replay != s_replay) { s_replay = replay; Log(replay ? "game replay: the studio stands by" : "replay ended: the studio resumes"); }
        if (replay) {
            GameState gs{false, false, v[2] != 0, v[3] != 0, 0.f, false, 0u, 0};
            const LONG next = (g_gsIdx + 1) & 1;
            g_gs[next] = gs;
            InterlockedExchange(&g_gsIdx, next);
            lua.set_integer(1);
            return 1;
        }
    }
    GameState gs{v[0] != 0, v[1] != 0, v[2] != 0, v[3] != 0, (float)v[4],
                 v[5] != 0, (uint32_t)(v[6] < 0 ? 0 : v[6]), (int)v[7]};
    gs.grind = v[8] != 0;
    gs.grindHash = gs.grind ? g_grindHash : 0u;
    gs.powerslide = v[9] != 0;
    gs.grab = v[10] != 0;
    // P6 lot 4: base poses, each one only when nothing more specific is on
    //   on foot: off the board; crouch: IsCranking; landing: from a trick's return to the ground (or IsLanding /
    //   IsJustLanded without a trick) for 0.8 s, or as long as the landing clip up to 2 s (landing_hold.h);
    //   riding: on the board, on the ground, nothing else
    {
        constexpr double kDismountWindow = 1.2;   // 2026-11-25: s after the feet leave the board (dismount part, figure)
        const bool onBoard = v[11] != 0, grounded = v[12] != 0, crank = v[13] != 0, landFlag = v[14] != 0;
        static bool s_prevKick = false, s_prevLand = false, s_prevBoard = true;
        static double s_boardAt = -100.0, s_offAt = -100.0;
        static LandingWindow s_land;
        const double now = Now();
        if (onBoard && !s_prevBoard) s_boardAt = now;   // stepping back on the board raises IsLanding too: not a landing
        if (!onBoard && s_prevBoard && !gs.kickflip) s_offAt = now;   // 2026-11-25: stepping off the board
        s_prevBoard = onBoard;
        // 2026-11-25 landing sync: a landing that ends a trick may play that trick's own landing clip
        static bool s_landFromTrick = false;
        static uint32_t s_landHash = 0;
        static int s_landStance = 0;
        if (s_prevKick && !gs.kickflip) { s_land.Start(now); s_landFromTrick = true; s_landHash = g_trickHash; s_landStance = g_trickStance; }
        else if (landFlag && !s_prevLand && !gs.kickflip && now - s_boardAt > 0.6) { s_land.Start(now); s_landFromTrick = false; }
        if (s_landFromTrick) { gs.landTrickHash = s_landHash; gs.landStance = s_landStance; }
        s_prevKick = gs.kickflip;
        s_prevLand = landFlag;
        const bool special = gs.manual || gs.noseManual || gs.grind || gs.powerslide || gs.grab;
        // on foot: walking above 40 (Speed), back to standing below 25 (no flicker)
        static bool s_walk = false;
        const double speed = v[16];
        s_walk = s_walk ? speed > 25.0 : speed > 40.0;
        const bool push = v[17] != 0;
        // 2026-11-25: running above 150, back to walking below 110 (the lot 5 "mounting" at Speed > 200 was in fact running:
        // the riding clip played while running). Riding starts only once IsOnBoard says the feet are on the board.
        static bool s_run = false;
        s_run = s_run ? speed > 110.0 : speed > 150.0;
        // 2026-11-26: past 0.8 s the landing lasts as long as its clip (up to 2 s), and any other action ends it
        double landHold = kLandHoldMin, landClip = 0.0;
        const double landSince = s_land.Since(now);
        if (landSince >= kLandHoldMin && landSince < kLandHoldMax && g_player && g_lib) {
            GameState lg = gs;
            lg.groundStance = gs.sw ? STANCE_Switch : STANCE_Normal;
            landClip = g_player->LandingClipLength(g_lib->Current(), lg);
            landHold = LandingHold(landClip);
        }
        const bool landOn = s_land.Active(now, landHold, gs.kickflip || special || crank || push || !onBoard);
        if (!onBoard) { if (!gs.kickflip) { if (s_run) gs.running = true; else if (s_walk) gs.walking = true; else gs.onFoot = true; } }
        else if (!gs.kickflip && !special) {
            if (crank) gs.crouch = true;
            else if (landOn) gs.landing = true;
            else if (push) gs.pushing = true;
            else if (grounded) gs.riding = true;
        }
        // FootPositionType (test 2026-11-25): 1 normal, 2 fakie, 3 nollie, 4 switch. Fakie and nollie only exist crouched.
        const int fp = (int)v[15];
        gs.groundStance = gs.sw ? STANCE_Switch : STANCE_Normal;
        if (gs.crouch) gs.groundStance = fp == 1 ? STANCE_Normal : fp == 2 ? STANCE_Fakie : fp == 3 ? STANCE_Nollie : fp == 4 ? STANCE_Switch : gs.groundStance;
        gs.dismount = !onBoard && !gs.kickflip && now - s_offAt < kDismountWindow;
        gs.figShort = gs.crouch || gs.landing || gs.pushing || gs.dismount;
        // one line when the base pose changes (with FootPositionType, to learn the fakie / nollie values)
        static int s_prevBase = -1, s_prevFoot = -99;
        const int base = gs.onFoot ? 1 : gs.riding ? 2 : gs.crouch ? 3 : gs.landing ? 4 : gs.walking ? 5 : gs.pushing ? 6 : gs.running ? 7 : 0;
        const int foot = (int)v[15];
        if (s_prevBase == 4 && base != 4 && landSince < 10.0)   // 2026-11-26: how long the landing lasted (landing clip > 0.8 s)
            Log("landing: lasted %.2f s (landing clip %.2f s, at most %.1f s)", landSince, landClip, kLandHoldMax);
        if (base != s_prevBase || (base && foot != s_prevFoot)) {
            static const char* kBase[8] = {"other", "on foot (standing)", "riding", "crouch", "landing", "walking", "pushing", "running"};
            Log("base pose: %s (switch %d, goofy %d, FootPositionType %d, Speed %.0f)", kBase[base], gs.sw ? 1 : 0, gs.goofy ? 1 : 0, foot, speed);
            s_prevBase = base;
            s_prevFoot = foot;
        }
    }
    if (gs.kickflip) { gs.trickHash = g_trickHash ? g_trickHash : TrickHash("KickFlip"); gs.stance = g_trickStance; }
    JumpStep(gs, v);
    // figure (variant) on now, and whether its original animation must be recorded
    FigureVariant fv;
    bool have = true;
    if (gs.kickflip && !g_trickBase.empty()) { fv.state = ST_Trick; fv.trick = g_trickBase; fv.stance = g_trickStance; }
    else if (gs.kickflip) have = false;
    else {
        fv.stance = gs.sw ? STANCE_Switch : STANCE_Normal;
        if (gs.dismount) fv.state = ST_Dismount;   // 2026-11-25: the game's dismount, recorded once (riding stance)
        else if (gs.manual) fv.state = ST_Manual;
        else if (gs.onFoot) { fv.state = ST_OnFoot; fv.stance = STANCE_Normal; }
        else if (gs.walking) { fv.state = ST_Walking; fv.stance = STANCE_Normal; }
        else if (gs.running) { fv.state = ST_Running; fv.stance = STANCE_Normal; }
        else if (gs.pushing) { fv.state = ST_Pushing; fv.stance = gs.groundStance; }
        else if (gs.riding) { fv.state = ST_Riding; fv.stance = gs.groundStance; }
        else if (gs.crouch) { fv.state = ST_Crouch; fv.stance = gs.groundStance; }
        else if (gs.landing) { fv.state = ST_Landing; fv.stance = gs.groundStance; }
        else if (gs.noseManual) fv.state = ST_NoseManual;
        else if (gs.grind) { fv.state = ST_Grind; fv.trick = g_grindName; }   // 2026-11-25: one recording per grind
        else if (gs.powerslide) fv.state = ST_Powerslide;
        else if (gs.grab) fv.state = ST_Grab;
        else have = false;
    }
    if (have) {
        fv.goofy = gs.goofy;
        const std::string key = FigureKey(fv);
        gs.figId = FigId(key);
        const std::string k = Lower(key);
        gs.figWanted = g_figScanned && (!g_figHave.count(k) || g_figRedo.count(k));   // nothing before the first folder scan
    }
    const LONG next = (g_gsIdx + 1) & 1;
    g_gs[next] = gs;
    InterlockedExchange(&g_gsIdx, next);
    // one line per trick (when the pop number changes), never every frame
    static uint32_t s_loggedPop = 0;
    if (gs.kickflip && gs.popSerial != s_loggedPop) {
        s_loggedPop = gs.popSerial;
        Log("trick: pop #%u, %s_%s%s", (unsigned)gs.popSerial, StancePrefix(g_trickStance), g_trickBase.c_str(), gs.figWanted ? " (move to record)" : "");
    }
    lua.set_integer(1);
    return 1;
}

// ASN_ScanClips() -> log lines joined by \n ("" if nothing changed)
int nScanClips(const Lua& lua) {
    std::string joined;
    LogLines(g_lib->Scan(), &joined);
    g_lib->CollectRetired();
    WriteSkeletonIfChanged();   // the mirror overrides of _studio.json may have changed
    // P6 figures: one file per variant. Old names (v0.7) are renamed once to their variant.
    const std::wstring figDir = g_lib->Dir() + L"\\_figures";
    static bool s_migrated = false;
    if (!s_migrated) {
        s_migrated = true;
        const wchar_t* olds[3][2] = {{L"Manual", L"Manual_RGS_regular"}, {L"NoseManual", L"NoseManual_RGS_regular"}, {L"Kickflip", L"KickFlip_RGS_regular"}};
        for (auto& o : olds) {
            const std::wstring from = figDir + L"\\" + o[0] + L".json", to = figDir + L"\\" + o[1] + L".json";
            if (FileExists(from) && !FileExists(to) && MoveFileExW(from.c_str(), to.c_str(), 0)) Log("move recording renamed: %ls.json -> %ls.json", o[0], o[1]);
            DeleteFileW((figDir + L"\\" + o[0] + L".refaire").c_str());
        }
    }
    g_figHave.clear();
    g_figRedo.clear();
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((figDir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::string f = Lower(Utf8(fd.cFileName));
                if (f.size() > 5 && f.substr(f.size() - 5) == ".json") g_figHave.insert(f.substr(0, f.size() - 5));
                else if (f.size() > 8 && f.substr(f.size() - 8) == ".refaire") g_figRedo.insert(f.substr(0, f.size() - 8));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    g_figScanned = true;
    FigureRec rec;
    int figId = 0;
    while (g_rec.TakeFinished(rec, figId)) {
        if (figId <= 0 || figId > (int)g_figKeys.size()) continue;
        rec.figure = g_figKeys[figId - 1];
        CreateDirectoryW(g_lib->Dir().c_str(), nullptr);
        CreateDirectoryW(figDir.c_str(), nullptr);
        const std::wstring base = figDir + L"\\" + Widen(rec.figure);
        if (WriteFileAtomic(base + L".json", FigureToJson(rec))) {
            DeleteFileW((base + L".refaire").c_str());
            g_figHave.insert(Lower(rec.figure));
            g_figRedo.erase(Lower(rec.figure));
            const int nf = (int)rec.times.size();
            Log("move recorded: %s, %d frames, %.2f s", rec.figure.c_str(), nf, nf ? rec.times.back() : 0.f);
            std::string line = "move recorded: " + rec.figure + " (" + std::to_string(nf) + " frames)";
            if (!joined.empty()) joined += '\n';
            joined += line;
        } else Log("move %s: cannot write", rec.figure.c_str());
    }
    lua.set_string(joined.c_str());
    return 1;
}

// ASN_Set(key, value): "on" 0/1 (master switch, 0.25 s fade)
int nSet(const Lua& lua) {
    std::string k;
    double v = 0;
    ArgStr(lua, k);
    ArgNum(lua, v);
    if (k == "on") {
        g_player->SetMaster(v != 0);   // writing a float: no lock, the hook skips no frame
        Log("studio %s", v != 0 ? "ON" : "OFF");
        lua.set_integer(1);
        return 1;
    }
    lua.set_integer(0);
    return 1;
}

// ASN_Capture(): asks for a capture of the game's pose on the next frame
int nCapture(const Lua& lua) {
    InterlockedExchange(&g_capReady, 0);
    InterlockedExchange(&g_capReq, 1);
    lua.set_integer(1);
    return 1;
}

// ASN_TakeCapture() -> path of the written file if a capture is ready, else ""
int nTakeCapture(const Lua& lua) {
    if (!g_capReady) { lua.set_string(""); return 1; }
    InterlockedExchange(&g_capReady, 0);
    if (g_capN != (int)g_names.size() || g_capN <= 0) { Log("capture ignored: the skeleton changed"); lua.set_string(""); return 1; }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[64];
    swprintf_s(stamp, L"capture_%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::wstring dir = g_lib->Dir() + L"\\captures";
    CreateDirectoryW(g_lib->Dir().c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\" + stamp + L".sanim";
    const std::string json = CaptureToJson(g_capBuf, g_names, g_capMirrored, g_map, EffectiveConv(g_map, g_lib->Current()->convOverride), Utf8(stamp));
    FILE* f = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
    if (!f) { Log("capture: cannot write %s", Utf8(path).c_str()); lua.set_string(""); return 1; }
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    const std::string p8 = Utf8(path);
    Log("capture written (%s, %d bones): %s", g_capMirrored ? "taken goofy, brought back to regular" : "regular", g_capN, p8.c_str());
    lua.set_string(p8.c_str());
    return 1;
}

// ASN_Stats() -> one line of counters
int nStats(const Lua& lua) {
    char buf[600];
    const double avg = g_usCount ? g_usSum / (double)g_usCount : 0.0;
    const ClipSet* s = g_lib->Current();
    snprintf(buf, sizeof(buf), "state %d | calls %ld, our mesh %ld, applied %ld, skipped (lock) %ld | refused: bone count %ld (last %d, expected %d), index %ld | faults %ld | clips %d (gen. %llu), clip \"%s\", layers %d, leg post passes %ld | Apply avg %.1f us, max %.1f us | game steps: %.2f s, %d markers",
             g_state, g_calls, g_mine, g_applied, g_busy, g_badNum, g_lastNum, g_player->RigBones(), g_badEdit, g_faults,
             s ? (int)s->clips.size() : 0, s ? (unsigned long long)s->generation : 0ull, ClipNow(), (int)g_layers, g_post, avg, g_usMax, g_player->Cycle().Period(), g_player->Cycle().Markers());
    g_usMax = 0.0;   // the max is read per period
    lua.set_string(buf);
    return 1;
}

// ASN_Clip() -> name of the current clip ("" if none)
int nClip(const Lua& lua) { lua.set_string(ClipNow()); return 1; }

// ---- 2026-11-26 multiplayer (v0.19): called by the AnimStudio Lua (game thread)
// ASN_SetPeer(slot, meshAddress) -> 1 ok / 0 refused. Address 0 = forget the slot.
int nSetPeer(const Lua& lua) {
    double s = -1, m = 0;
    ArgNum(lua, s);
    ArgNum(lua, m);
    const int slot = (int)s;
    if (slot < 0 || slot >= kPeers) { lua.set_integer(0); return 1; }
    void* mesh = (void*)(uintptr_t)m;
    AcquireSRWLockExclusive(&g_peerLock);
    PeerSlot& p = g_peers[slot];
    if (mesh && !p.pl) p.pl = new Player();
    if (mesh != p.mesh && p.pl) ConfigurePeer(p);   // fresh state (layers, leg pass) for another skater
    p.mesh = mesh;
    p.calls = p.applied = p.faults = p.badNum = p.frames = p.posts = p.missing = 0;
    p.haveCache = false;
    p.last = 0.0;
    ReleaseSRWLockExclusive(&g_peerLock);
    AcquireSRWLockExclusive(&p.frameLock);
    p.haveFrame = false;
    p.frame = NetFrame{};
    ReleaseSRWLockExclusive(&p.frameLock);
    Log("net: peer slot %d -> mesh %p (skeleton %s)", slot, mesh, p.rigOk ? "ready" : "not ready yet");
    lua.set_integer(1);
    return 1;
}
// ASN_NetPacket() -> our layers as wire text ("S1..."); an empty frame when the studio has not run lately
int nNetPacket(const Lua& lua) {
    NetFrame f;
    double at = 0.0;
    AcquireSRWLockShared(&g_netLock);
    f = g_netOut;
    at = g_netAt;
    ReleaseSRWLockShared(&g_netLock);
    if (at <= 0.0 || Now() - at > 0.3) { f.n = 0; f.seq = g_netSeq; }
    const std::string s = NetEncode(f);
    lua.set_string(s.c_str());
    return 1;
}
// ASN_NetReceive(slot, text) -> number of layers in the frame, -1 refused (malformed), -2 old (out of order)
int nNetReceive(const Lua& lua) {
    double s = -1;
    std::string text;
    ArgNum(lua, s);
    ArgStr(lua, text);
    const int slot = (int)s;
    NetFrame f;
    if (slot < 0 || slot >= kPeers || !NetDecode(text, f)) { lua.set_integer(-1); return 1; }
    PeerSlot& p = g_peers[slot];
    int ret = f.n;
    AcquireSRWLockExclusive(&p.frameLock);
    const int32_t d = (int32_t)(f.seq - p.frame.seq);
    if (p.haveFrame && d <= 0 && d > -100000) ret = -2;   // reordered or duplicated (a restart of the sender jumps back far)
    else { p.frame = f; p.frameAt = Now(); p.haveFrame = true; InterlockedIncrement(&p.frames); }
    ReleaseSRWLockExclusive(&p.frameLock);
    lua.set_integer(ret);
    return 1;
}
// ASN_PeerStats(slot) -> one line; slot -1 = calls on unknown meshes
int nPeerStats(const Lua& lua) {
    double s = -1;
    ArgNum(lua, s);
    const int slot = (int)s;
    char buf[400];
    if (slot < 0 || slot >= kPeers) snprintf(buf, sizeof(buf), "unknown meshes: %ld calls, ours: %ld calls", g_otherCalls, g_mine);
    else {
        PeerSlot& p = g_peers[slot];
        const LONG jn = InterlockedExchange(&p.jumpN, 0), js = InterlockedExchange(&p.jumpSumMs, 0);
        const LONG jm = InterlockedExchange(&p.jumpMaxMs, 0), sn = InterlockedExchange(&p.snaps, 0);
        snprintf(buf, sizeof(buf), "frames %ld, hook calls %ld, posed %ld, leg pass %ld, clips missing here %ld, last frame age %ld ms, other bone count %ld, faults %ld | smoothed clock jumps: mean %ld ms, max %ld ms over %ld frames, snaps %ld",
                 p.frames, p.calls, p.applied, p.posts, p.missing, p.ageMs, p.badNum, p.faults,
                 jn > 0 ? js / jn : 0L, jm, jn, sn);
    }
    lua.set_string(buf);
    return 1;
}

} // namespace

class AnimStudioNative : public RC::CppUserModBase {
  public:
    AnimStudioNative() {
        ModName = STR("AnimStudioNative");
        ModVersion = STR("0.11");
        ModDescription = STR("AnimStudio: clip player (manuals, all flip tricks, per stance) + skeleton and recorded moves for the editor");
        ModAuthors = STR("AnimStudio");
        QueryPerformanceFrequency(&g_freq);
        g_player = new Player();
        const std::wstring dir = ExeDir() + L"Mods\\AnimStudio\\clips";
        g_lib = new ClipLibrary(dir);
        Log("---- AnimStudioNative v0.%d loaded (ExpandedTricksNative %s); clips: %s", kVersion,
            EtEnabled() ? "enabled: we will install behind it" : "absent", Utf8(dir).c_str());
    }
    ~AnimStudioNative() override {
        if (g_slot && *g_slot == (void*)&hkFbt && g_prev) {
            DWORD old = 0;
            if (VirtualProtect(g_slot, sizeof(void*), PAGE_READWRITE, &old)) { *g_slot = (void*)g_prev; VirtualProtect(g_slot, sizeof(void*), old, &old); }
        }
        // g_player and g_lib are not freed: a hook call may still be running on another thread
    }
    auto on_lua_start(RC::StringViewType mod_name, Lua&, Lua& main_lua, Lua&, std::vector<Lua*>&) -> void override {
        if (mod_name != STR("AnimStudio")) return;
        try {
            main_lua.register_function("ASN_Version", &nVersion);
            main_lua.register_function("ASN_SetRig", &nSetRig);
            main_lua.register_function("ASN_SetMesh", &nSetMesh);
            main_lua.register_function("ASN_Poll", &nPoll);
            main_lua.register_function("ASN_Why", &nWhy);
            main_lua.register_function("ASN_SetState", &nSetState);
            main_lua.register_function("ASN_SetTrick", &nSetTrick);
            main_lua.register_function("ASN_SetGrind", &nSetGrind);
            main_lua.register_function("ASN_ScanClips", &nScanClips);
            main_lua.register_function("ASN_Set", &nSet);
            main_lua.register_function("ASN_Capture", &nCapture);
            main_lua.register_function("ASN_TakeCapture", &nTakeCapture);
            main_lua.register_function("ASN_Stats", &nStats);
            main_lua.register_function("ASN_Clip", &nClip);
            main_lua.register_function("ASN_SetPeer", &nSetPeer);         // 2026-11-26 multiplayer
            main_lua.register_function("ASN_NetPacket", &nNetPacket);
            main_lua.register_function("ASN_NetReceive", &nNetReceive);
            main_lua.register_function("ASN_PeerStats", &nPeerStats);
            Log("ASN_* functions given to the AnimStudio Lua");
        } catch (...) {
            Log("failed to register the Lua functions");
        }
    }
};

#define ASN_API __declspec(dllexport)
extern "C" {
ASN_API RC::CppUserModBase* start_mod() { return new AnimStudioNative(); }
ASN_API void uninstall_mod(RC::CppUserModBase* m) { delete m; }
}
