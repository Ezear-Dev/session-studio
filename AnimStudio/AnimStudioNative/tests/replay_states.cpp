// Diagnostic: replays the base-pose states of AnimStudio.native.log through the Player with the real clips, over a fixed
// game pose (first frame of the OnFoot figure), and reports every frame where our layer makes a bone jump.
//   asn_tests --replay-states <clips dir> <log file> <from hh:mm:ss> <to hh:mm:ss>
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/mirror.h"
#include "../src/pelvis.h"
#include "../src/player.h"
#include "../src/skeleton.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>

namespace {
bool Slurp(const std::string& p, std::string& out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::stringstream s; s << f.rdbuf(); out = s.str(); return true; }
double Secs(const std::string& hms) { int h = 0, m = 0; double s = 0; sscanf_s(hms.c_str(), "%d:%d:%lf", &h, &m, &s); return h * 3600.0 + m * 60.0 + s; }
float Ang(Quat a, Quat b) { const float d = std::fabs(QDot(QNorm(a), QNorm(b))); return 2.f * std::acos(d > 1.f ? 1.f : d) * 57.29578f; }
}

int ReplayStatesMain(const char* dir, const char* logPath, const char* from, const char* to) {
    const std::string d(dir);
    std::string sk, fg, lg, err;
    if (!Slurp(d + "\\_skeleton.json", sk) || !Slurp(d + "\\_figures\\OnFoot_RGS_regular.json", fg) || !Slurp(logPath, lg)) { std::printf("cannot read\n"); return 2; }
    SkeletonFile s; FigureRec fr;
    if (!ParseSkeleton(sk, s, err) || !ParseFigure(fg, fr, err)) { std::printf("%s\n", err.c_str()); return 2; }
    const int n = (int)s.names.size();
    auto pl = std::make_unique<Player>();
    pl->SetRig(s.parents.data(), n, BuildMirrorMap(s.names, s.parents, s.refPos));
    pl->SetRefPositions(s.refPos);
    std::vector<bool> leg(n); for (int i = 0; i < n; i++) leg[i] = IsLegBone(s.names[i]);
    pl->SetLegBones(leg);
    pl->SetPelvisRig(BuildPelvisRig(s.names, s.parents.data(), n));
    ClipSet set; set.generation = 1;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((d + "\\*.sanim").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string txt; Clip c;
            if (Slurp(d + "\\" + fd.cFileName, txt) && ParseClip(txt, fd.cFileName, c, err) && BindClip(c, s.names, err)) set.clips.push_back(std::move(c));
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(set.clips.begin(), set.clips.end(), [](const Clip& a, const Clip& b) { return a.file < b.file; });
    // fixed game pose
    std::vector<FTr> game(n, FTr{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
    { std::vector<Quat> lq(n); std::vector<char> dirty(n, 1);
      for (int b = 0; b < n; b++) lq[b] = fr.frames[b];
      ToComponent(game.data(), s.parents.data(), n, lq.data(), (const float (*)[3])s.refPos.data(), (const bool*)dirty.data()); }
    // states from the log
    struct Ev { double t; GameState g; std::string label; };
    std::vector<Ev> evs;
    std::istringstream in(lg);
    std::string line;
    uint32_t pop = 0;
    const double t0 = Secs(from), t1 = Secs(to);
    while (std::getline(in, line)) {
        if (line.size() < 13) continue;
        const double t = Secs(line.substr(0, 12));
        if (t < t0 - 30.0 || t > t1) continue;
        GameState g{}; g.popSerial = pop;
        bool is = false;
        if (line.find("base pose") != std::string::npos) {
            is = true;
            if (line.find("on foot") != std::string::npos) g.onFoot = true;
            else if (line.find(": riding") != std::string::npos) g.riding = true;
            else if (line.find("crouch") != std::string::npos) g.crouch = true;
            else if (line.find("landing") != std::string::npos) g.landing = true;
            else if (line.find("walking") != std::string::npos) g.walking = true;
            else if (line.find("pushing") != std::string::npos) g.pushing = true;
            else if (line.find("running") != std::string::npos) g.running = true;
            else if (!evs.empty() && evs.back().g.kickflip) { g = evs.back().g; }   // "other" during a trick
        } else if (line.find("trick: pop") != std::string::npos) { is = true; pop++; g = GameState{}; g.kickflip = true; g.popSerial = pop; g.trickPhase = 1; }
        else if (line.find("game replay") != std::string::npos) is = true;   // stand-by: no state
        if (is) evs.push_back(Ev{t, g, line.substr(13, 60)});
    }
    if (evs.empty()) { std::printf("no state in the range\n"); return 3; }
    std::vector<Quat> prevL(n), lq(n); std::vector<float> lp(n * 3);
    bool havePrev = false;
    size_t k = 0;
    GameState g{};
    const float dt = 1.f / 60.f;
    int spikes = 0;
    for (double t = evs.front().t; t <= t1; t += dt) {
        while (k < evs.size() && evs[k].t <= t) { g = evs[k].g; if (t >= t0) std::printf("%.3f  -> %s\n", t, evs[k].label.c_str()); k++; }
        std::vector<FTr> cs = game;
        pl->Apply(cs.data(), n, &set, g, dt);
        ToLocal(cs.data(), s.parents.data(), n, lq.data(), (float (*)[3])lp.data());
        if (havePrev && t >= t0) {
            float worst = 0.f; int wb = -1;
            for (int b = 0; b < n; b++) { const float a = Ang(lq[b], prevL[b]); if (a > worst) { worst = a; wb = b; } }
            if (worst > 2.5f && spikes < 200) { spikes++; std::printf("      %.3f  JUMP %.1f deg on %s (clip \"%s\", %d layers)\n", t, worst, s.names[wb].c_str(), pl->CurrentClipName(), pl->LayerCount()); }
        }
        prevL = lq; havePrev = true;
    }
    std::printf("%d jump(s) > 2.5 deg/frame\n", spikes);
    return 0;
}
