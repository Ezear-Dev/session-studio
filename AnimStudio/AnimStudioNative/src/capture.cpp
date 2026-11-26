#include "capture.h"
#include "json.h"
#include <utility>

namespace {
JValue Num(double v) { JValue j; j.t = JValue::Num; j.n = v; return j; }
JValue Str(const std::string& s) { JValue j; j.t = JValue::Str; j.s = s; return j; }
JValue Bool(bool b) { JValue j; j.t = JValue::Bool; j.b = b; return j; }
JValue Obj() { JValue j; j.t = JValue::Obj; return j; }
JValue Arr() { JValue j; j.t = JValue::Arr; return j; }
void Put(JValue& o, const char* k, JValue v) { o.o.emplace_back(k, std::move(v)); }
} // namespace

std::string CaptureToJson(const Quat* local, const std::vector<std::string>& names, bool mirrored,
                          const MirrorMap& map, const std::vector<int>& conv, const std::string& name) {
    const int n = (int)names.size();
    std::vector<int> src(n, -1);      // src[b]: the captured bone that gives the key of bone b
    std::vector<Quat> key(n, QIdent);
    for (int i = 0; i < n; i++) {
        int b = i;
        Quat q = QNorm(local[i]);
        if (mirrored && i < (int)map.other.size() && i < (int)conv.size()) {
            const int p = (i < (int)map.parent.size()) ? map.parent[i] : -1;
            q = MirrorKey(q, p >= 0 ? conv[p] : conv[i], conv[i]);
            b = map.other[i];
        }
        src[b] = i;
        key[b] = q;
    }
    JValue root = Obj();
    Put(root, "format", Str("sanim"));
    Put(root, "version", Num(1));
    Put(root, "name", Str(name));
    Put(root, "enabled", Bool(false));
    Put(root, "capturedStance", Str(mirrored ? "goofy" : "regular"));
    JValue trig = Obj();
    JValue states = Arr(); states.a.push_back(Str("Manual"));
    Put(trig, "states", std::move(states));
    Put(trig, "priority", Num(0));
    Put(root, "trigger", std::move(trig));
    JValue play = Obj();
    Put(play, "drive", Str("time")); Put(play, "duration", Num(1)); Put(play, "loop", Bool(true));
    Put(root, "play", std::move(play));
    JValue blend = Obj();
    Put(blend, "in", Num(0.25)); Put(blend, "out", Num(0.30));
    Put(root, "blend", std::move(blend));
    Put(root, "mode", Str("replace"));
    Put(root, "mirrorForGoofy", Bool(true));
    JValue tracks = Obj();
    for (int b = 0; b < n; b++) {
        if (src[b] < 0 || names[b].rfind("SKXX_", 0) == 0) continue;
        JValue k = Obj();
        Put(k, "t", Num(0));
        JValue q = Arr();
        q.a.push_back(Num(key[b].x)); q.a.push_back(Num(key[b].y)); q.a.push_back(Num(key[b].z)); q.a.push_back(Num(key[b].w));
        Put(k, "q", std::move(q));
        JValue list = Arr(); list.a.push_back(std::move(k));
        Put(tracks, names[b].c_str(), std::move(list));
    }
    Put(root, "tracks", std::move(tracks));
    return JWrite(root) + "\n";
}
