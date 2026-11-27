#include "clip.h"
#include "json.h"
#include <cmath>

namespace {

constexpr double kMaxAbs = 1e6;

bool Fail(std::string& err, const std::string& msg) { err = msg; return false; }

bool Finite(double v) { return std::isfinite(v) && std::fabs(v) <= kMaxAbs; }

// optional number: absent -> def; present -> must be a finite number
bool OptNum(const JValue* obj, const char* key, double def, double& out, std::string& err, const std::string& field) {
    out = def;
    const JValue* v = obj ? obj->get(key) : nullptr;
    if (!v) return true;
    if (v->t != JValue::Num || !Finite(v->n)) return Fail(err, field + ": finite number expected");
    out = v->n;
    return true;
}
bool OptBool(const JValue* obj, const char* key, bool def, bool& out, std::string& err, const std::string& field) {
    out = def;
    const JValue* v = obj ? obj->get(key) : nullptr;
    if (!v) return true;
    if (v->t != JValue::Bool) return Fail(err, field + ": true or false expected");
    out = v->b;
    return true;
}
bool OptStr(const JValue* obj, const char* key, const char* def, std::string& out, std::string& err, const std::string& field) {
    out = def;
    const JValue* v = obj ? obj->get(key) : nullptr;
    if (!v) return true;
    if (v->t != JValue::Str) return Fail(err, field + ": text expected");
    out = v->s;
    return true;
}
const JValue* OptObj(const JValue& root, const char* key, std::string& err, bool& ok) {
    const JValue* v = root.get(key);
    ok = true;
    if (v && v->t != JValue::Obj) { ok = false; Fail(err, std::string(key) + ": object expected"); return nullptr; }
    return v;
}
bool NumArray(const JValue* v, size_t n, double* out, std::string& err, const std::string& field) {
    if (!v || v->t != JValue::Arr || v->a.size() != n) return Fail(err, field + ": array of " + std::to_string(n) + " numbers expected");
    for (size_t i = 0; i < n; i++) {
        if (v->a[i].t != JValue::Num || !Finite(v->a[i].n)) return Fail(err, field + ": value not finite or out of range (|v| <= 1e6)");
        out[i] = v->a[i].n;
    }
    return true;
}

bool ParseKey(const JValue& k, const std::string& field, Key& out, std::string& err) {
    if (k.t != JValue::Obj) return Fail(err, field + ": object expected");
    const JValue* t = k.get("t");
    if (!t || t->t != JValue::Num || !Finite(t->n)) return Fail(err, field + ".t: finite number expected");
    out.t = (float)t->n;
    const JValue* rot = k.get("rot");
    const JValue* q = k.get("q");
    if ((rot != nullptr) == (q != nullptr)) return Fail(err, field + ": give either rot (degrees) or q (quaternion), not both nor neither");
    if (rot) {
        double r[3];
        if (!NumArray(rot, 3, r, err, field + ".rot")) return false;
        out.q = QFromRotatorDeg((float)r[0], (float)r[1], (float)r[2]);
    } else {
        double v[4];
        if (!NumArray(q, 4, v, err, field + ".q")) return false;
        const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        if (n < 1e-6) return Fail(err, field + ".q: null quaternion");
        out.q = Quat{(float)(v[0] / n), (float)(v[1] / n), (float)(v[2] / n), (float)(v[3] / n)};
    }
    std::string interp;
    if (!OptStr(&k, "interp", "smooth", interp, err, field + ".interp")) return false;
    if (interp == "smooth") out.smooth = true;
    else if (interp == "linear") out.smooth = false;
    else if (interp == "step") { out.smooth = false; out.step = true; }
    else return Fail(err, field + ".interp: 'smooth', 'linear' or 'step' expected");
    return true;
}

} // namespace

bool ParseClip(const std::string& text, const std::string& file, Clip& out, std::string& err) {
    JValue root;
    std::string jerr;
    if (!JParse(text, root, jerr)) return Fail(err, "unreadable JSON: " + jerr);
    if (root.t != JValue::Obj) return Fail(err, "JSON: an object is expected at the root");
    Clip c;
    c.file = file;
    const JValue* fmt = root.get("format");
    if (!fmt || fmt->t != JValue::Str || fmt->s != "sanim") return Fail(err, "format: \"sanim\" expected");
    const JValue* ver = root.get("version");
    if (!ver || ver->t != JValue::Num || ver->n != 1.0) return Fail(err, "version: 1 expected");
    if (!OptStr(&root, "name", file.c_str(), c.name, err, "name")) return false;
    if (!OptBool(&root, "enabled", true, c.enabled, err, "enabled")) return false;
    if (!OptBool(&root, "mirrorForGoofy", true, c.mirror, err, "mirrorForGoofy")) return false;

    bool ok;
    const JValue* trig = OptObj(root, "trigger", err, ok);
    if (!ok) return false;
    const JValue* states = trig ? trig->get("states") : nullptr;
    if (!states || states->t != JValue::Arr || states->a.empty()) return Fail(err, "trigger.states: non-empty list expected (Manual, NoseManual, Trick...)");
    bool legacyKick = false;
    for (auto& s : states->a) {
        const uint32_t b = s.t == JValue::Str ? StateFromName(s.s) : 0u;
        if (!b) return Fail(err, "trigger.states: unknown state (Manual, NoseManual, Trick, Grind, Powerslide, Grab, OnFoot, Riding, Crouch, Landing)");
        if (s.s == "Kickflip") legacyKick = true;
        c.states |= b;
    }
    // P6: targeted trick, stances, feet
    if (!OptStr(trig, "trick", "", c.trick, err, "trigger.trick")) return false;
    const JValue* st = trig ? trig->get("stances") : nullptr;
    if (st) {
        if (st->t != JValue::Arr || st->a.empty()) return Fail(err, "trigger.stances: non-empty list expected (Normal, Switch, Fakie, Nollie)");
        c.stances = 0;
        for (auto& s : st->a) {
            const int k = s.t == JValue::Str ? StanceFromLabel(s.s) : -1;
            if (k < 0) return Fail(err, "trigger.stances: Normal, Switch, Fakie or Nollie expected");
            c.stances |= (uint8_t)(1u << k);
        }
    }
    std::string feet;
    if (!OptStr(trig, "feet", "any", feet, err, "trigger.feet")) return false;
    if (feet == "any") c.feet = Feet::Any;
    else if (feet == "regular") c.feet = Feet::Regular;
    else if (feet == "goofy") c.feet = Feet::Goofy;
    else return Fail(err, "trigger.feet: 'any', 'regular' or 'goofy' expected");
    if (legacyKick && c.trick.empty()) {   // "Kickflip" clip of v0.7: RGS_KickFlip and SWS_KickFlip
        c.trick = "KickFlip";
        if (!st) c.stances = (1u << STANCE_Normal) | (1u << STANCE_Switch);
    }
    if (!c.trick.empty() && !(c.states & (ST_Trick | ST_Grind | ST_Landing))) return Fail(err, "trigger.trick: only with the Trick, Grind or Landing state");
    c.trickHash = TrickHash(c.trick);
    double d;
    if (!OptNum(trig, "priority", 0, d, err, "trigger.priority")) return false;
    c.priority = (int)d;

    const JValue* play = OptObj(root, "play", err, ok);
    if (!ok) return false;
    std::string drive;
    if (!OptStr(play, "drive", "time", drive, err, "play.drive")) return false;
    if (drive == "time") c.drive = Drive::Time;
    else if (drive == "ManualRatio") c.drive = Drive::ManualRatio;
    else if (drive == "cycle") c.drive = Drive::Cycle;
    else return Fail(err, "play.drive: 'time', 'ManualRatio' or 'cycle' expected");
    const JValue* dur = play ? play->get("duration") : nullptr;
    if (!dur || dur->t != JValue::Num || !Finite(dur->n) || dur->n <= 0) return Fail(err, "play.duration: number > 0 expected");
    c.duration = (float)dur->n;
    if (!OptBool(play, "loop", true, c.loop, err, "play.loop")) return false;
    if (!OptBool(play, "hold", false, c.hold, err, "play.hold")) return false;
    if (!OptBool(play, "legsAfterCatch", false, c.legsAfterCatch, err, "play.legsAfterCatch")) return false;
    {   // lot 5: landing part of a trick clip
        double la, ga;
        if (!OptNum(play, "landing", 0, la, err, "play.landing") || !OptNum(play, "groundAt", 0, ga, err, "play.groundAt")) return false;
        if (la < 0 || ga < 0 || la > 5) return Fail(err, "play.landing / groundAt: durations >= 0 expected");
        if (la > 0 && ga + la > c.duration + 1e-3) return Fail(err, "play.landing: groundAt + landing exceeds duration");
        c.landing = (float)la; c.groundAt = (float)ga;
    }
    {   // 2026-11-25: markers of the game's events (trick clips), see trick_sync.h
        const JValue* mk = play ? play->get("markers") : nullptr;
        if (mk) {
            if (mk->t != JValue::Obj) return Fail(err, "play.markers: object expected (air, catch, land, ground)");
            double a, ca, la, gr;
            if (!OptNum(mk, "air", 0, a, err, "play.markers.air") || !OptNum(mk, "catch", a, ca, err, "play.markers.catch") ||
                !OptNum(mk, "land", ca, la, err, "play.markers.land") || !OptNum(mk, "ground", c.duration, gr, err, "play.markers.ground")) return false;
            if (a < 0 || ca < a - 1e-4 || la < ca - 1e-4 || gr < la - 1e-4 || gr > c.duration + 1e-3 || gr <= 0)
                return Fail(err, "play.markers: 0 <= air <= catch <= land <= ground <= duration expected");
            c.markers.ok = true;
            c.markers.air = (float)a; c.markers.catchT = (float)(ca < a ? a : ca);
            c.markers.land = (float)(la < c.markers.catchT ? c.markers.catchT : la);
            c.markers.ground = (float)(gr < c.markers.land ? c.markers.land : gr);
        }
    }
    {   // 2026-11-25: dismount part of a riding clip
        double dm;
        if (!OptNum(play, "dismount", 0, dm, err, "play.dismount")) return false;
        if (dm < 0 || (dm > 0 && dm >= c.duration - 0.05)) return Fail(err, "play.dismount: a duration >= 0 and shorter than duration is expected");
        c.dismount = (float)dm;
    }
    const JValue* rr = play ? play->get("ratioRange") : nullptr;
    if (rr) {
        double r[2];
        if (!NumArray(rr, 2, r, err, "play.ratioRange")) return false;
        if (r[0] == r[1]) return Fail(err, "play.ratioRange: the two bounds must differ");
        c.r0 = (float)r[0]; c.r1 = (float)r[1];
    }

    const JValue* blend = OptObj(root, "blend", err, ok);
    if (!ok) return false;
    double bi, bo;
    if (!OptNum(blend, "in", 0.25, bi, err, "blend.in") || !OptNum(blend, "out", 0.30, bo, err, "blend.out")) return false;
    if (bi < 0 || bo < 0) return Fail(err, "blend: durations >= 0 expected");
    c.blendIn = (float)bi; c.blendOut = (float)bo;

    std::string mode;
    if (!OptStr(&root, "mode", "additive", mode, err, "mode")) return false;
    if (mode == "additive") c.mode = Mode::Additive;
    else if (mode == "replace") c.mode = Mode::Replace;
    else return Fail(err, "mode: 'additive' or 'replace' expected");

    // P6 lot 2: pelvis offset keys
    const JValue* pel = root.get("pelvis");
    if (pel) {
        if (pel->t != JValue::Arr) return Fail(err, "pelvis: a list of keys is expected");
        for (size_t i = 0; i < pel->a.size(); i++) {
            const std::string field = "pelvis[" + std::to_string(i) + "]";
            const JValue& k = pel->a[i];
            if (k.t != JValue::Obj) return Fail(err, field + ": object expected");
            const JValue* t = k.get("t");
            if (!t || t->t != JValue::Num || !Finite(t->n)) return Fail(err, field + ".t: finite number expected");
            PosKey pk{};
            pk.t = (float)t->n;
            double v[3];
            if (!NumArray(k.get("offset"), 3, v, err, field + ".offset")) return false;
            for (int j = 0; j < 3; j++) pk.v[j] = (float)v[j];
            std::string interp;
            if (!OptStr(&k, "interp", "smooth", interp, err, field + ".interp")) return false;
            if (interp != "smooth" && interp != "linear" && interp != "step") return Fail(err, field + ".interp: 'smooth', 'linear' or 'step' expected");
            pk.smooth = interp == "smooth";
            pk.step = interp == "step";
            if (pk.t < 0 || pk.t > c.duration) return Fail(err, field + ".t: outside [0, duration]");
            if (!c.pelvis.empty() && !(pk.t > c.pelvis.back().t)) return Fail(err, field + ".t: t values must be strictly increasing");
            c.pelvis.push_back(pk);
        }
    }
    const JValue* tracks = root.get("tracks");
    if (tracks && tracks->t != JValue::Obj) return Fail(err, "tracks: object expected");
    if ((!tracks || tracks->o.empty()) && c.pelvis.empty()) return Fail(err, "tracks: at least one track is expected");
    if (tracks) for (auto& kv : tracks->o) {
        const std::string field = "tracks." + kv.first;
        if (kv.second.t != JValue::Arr || kv.second.a.empty()) return Fail(err, field + ": a non-empty list of keys is expected");
        Track tr;
        tr.bone = kv.first;
        for (size_t i = 0; i < kv.second.a.size(); i++) {
            Key k{};
            if (!ParseKey(kv.second.a[i], field + "[" + std::to_string(i) + "]", k, err)) return false;
            if (k.t < 0 || k.t > c.duration) return Fail(err, field + "[" + std::to_string(i) + "].t: outside [0, duration]");
            if (!tr.keys.empty() && !(k.t > tr.keys.back().t)) return Fail(err, field + "[" + std::to_string(i) + "].t: t values must be strictly increasing");
            tr.keys.push_back(k);
        }
        c.tracks.push_back(std::move(tr));
    }

    const JValue* weights = OptObj(root, "weights", err, ok);
    if (!ok) return false;
    if (weights) {
        for (auto& kv : weights->o) {
            const std::string field = "weights." + kv.first;
            if (kv.second.t != JValue::Num || !Finite(kv.second.n) || kv.second.n < 0 || kv.second.n > 1) return Fail(err, field + ": weight in [0, 1] expected");
            for (auto& tr : c.tracks) if (tr.bone == kv.first) tr.weight = (float)kv.second.n;
        }
    }
    if (const JValue* link = root.get("link")) {   // 2026-11-27 v0.29
        if (link->t != JValue::Str) return Fail(err, "link: text expected");
        c.link = link->s;
    }
    // unknown root fields: kept as they are (the editor writes them back on save)
    static const char* kKnown[] = {"format", "version", "name", "enabled", "mirrorForGoofy", "trigger", "play", "blend", "mode", "tracks", "weights", "pelvis", "link"};
    for (auto& kv : root.o) {
        bool known = false;
        for (const char* k : kKnown) if (kv.first == k) { known = true; break; }
        if (!known) c.extra.emplace_back(kv.first, kv.second);
    }
    out = std::move(c);
    return true;
}

namespace {
JValue JNum(double v) { JValue j; j.t = JValue::Num; j.n = v; return j; }
JValue JStr(const std::string& s) { JValue j; j.t = JValue::Str; j.s = s; return j; }
JValue JBool(bool b) { JValue j; j.t = JValue::Bool; j.b = b; return j; }
JValue JObj() { JValue j; j.t = JValue::Obj; return j; }
JValue JArr() { JValue j; j.t = JValue::Arr; return j; }
void JPut(JValue& o, const std::string& k, JValue v) { o.o.emplace_back(k, std::move(v)); }
} // namespace

std::string ClipToJson(const Clip& c) {
    JValue root = JObj();
    JPut(root, "format", JStr("sanim"));
    JPut(root, "version", JNum(1));
    JPut(root, "name", JStr(c.name));
    JPut(root, "enabled", JBool(c.enabled));
    JValue trig = JObj();
    JValue states = JArr();
    for (uint32_t b : kStateOrder)
        if (c.states & b) states.a.push_back(JStr(StateName(b)));
    JPut(trig, "states", std::move(states));
    if ((c.states & (ST_Trick | ST_Grind | ST_Landing)) && !c.trick.empty()) JPut(trig, "trick", JStr(c.trick));
    if ((c.stances & kAllStances) != kAllStances) {
        JValue st = JArr();
        for (int s = 0; s < STANCE_Count; s++) if (c.stances & (1u << s)) st.a.push_back(JStr(Lower(StanceLabel(s))));
        JPut(trig, "stances", std::move(st));
    }
    if (c.feet != Feet::Any) JPut(trig, "feet", JStr(c.feet == Feet::Goofy ? "goofy" : "regular"));
    JPut(trig, "priority", JNum(c.priority));
    JPut(root, "trigger", std::move(trig));
    JValue play = JObj();
    JPut(play, "drive", JStr(c.drive == Drive::ManualRatio ? "ManualRatio" : c.drive == Drive::Cycle ? "cycle" : "time"));
    JPut(play, "duration", JNum(c.duration));
    JPut(play, "loop", JBool(c.loop));
    if (c.hold) JPut(play, "hold", JBool(true));
    if (c.legsAfterCatch) JPut(play, "legsAfterCatch", JBool(true));   // written only when on: older files unchanged
    if (c.landing > 0.f) { JPut(play, "landing", JNum(c.landing)); JPut(play, "groundAt", JNum(c.groundAt)); }
    if (c.markers.ok) {
        JValue mk = JObj();
        JPut(mk, "air", JNum(c.markers.air)); JPut(mk, "catch", JNum(c.markers.catchT));
        JPut(mk, "land", JNum(c.markers.land)); JPut(mk, "ground", JNum(c.markers.ground));
        JPut(play, "markers", std::move(mk));
    }
    if (c.dismount > 0.f) JPut(play, "dismount", JNum(c.dismount));
    if (c.drive == Drive::ManualRatio) {
        JValue rr = JArr(); rr.a.push_back(JNum(c.r0)); rr.a.push_back(JNum(c.r1));
        JPut(play, "ratioRange", std::move(rr));
    }
    JPut(root, "play", std::move(play));
    JValue blend = JObj();
    JPut(blend, "in", JNum(c.blendIn)); JPut(blend, "out", JNum(c.blendOut));
    JPut(root, "blend", std::move(blend));
    JPut(root, "mode", JStr(c.mode == Mode::Replace ? "replace" : "additive"));
    JPut(root, "mirrorForGoofy", JBool(c.mirror));
    if (!c.link.empty()) JPut(root, "link", JStr(c.link));   // written only when there is one: older files unchanged
    JValue weights = JObj();
    for (auto& tr : c.tracks) if (tr.weight != 1.f) JPut(weights, tr.bone, JNum(tr.weight));
    if (!weights.o.empty()) JPut(root, "weights", std::move(weights));
    JValue tracks = JObj();
    for (auto& tr : c.tracks) {
        JValue keys = JArr();
        for (auto& k : tr.keys) {
            JValue key = JObj();
            JPut(key, "t", JNum(k.t));
            JValue q = JArr();
            q.a.push_back(JNum(k.q.x)); q.a.push_back(JNum(k.q.y)); q.a.push_back(JNum(k.q.z)); q.a.push_back(JNum(k.q.w));
            JPut(key, "q", std::move(q));
            JPut(key, "interp", JStr(k.step ? "step" : k.smooth ? "smooth" : "linear"));
            keys.a.push_back(std::move(key));
        }
        JPut(tracks, tr.bone, std::move(keys));
    }
    JPut(root, "tracks", std::move(tracks));
    if (!c.pelvis.empty()) {
        JValue pel = JArr();
        for (auto& k : c.pelvis) {
            JValue key = JObj();
            JPut(key, "t", JNum(k.t));
            JValue v = JArr();
            for (float x : k.v) v.a.push_back(JNum(x));
            JPut(key, "offset", std::move(v));
            JPut(key, "interp", JStr(k.step ? "step" : k.smooth ? "smooth" : "linear"));
            pel.a.push_back(std::move(key));
        }
        JPut(root, "pelvis", std::move(pel));
    }
    for (auto& kv : c.extra) JPut(root, kv.first, kv.second);
    return JWrite(root) + "\n";
}

bool BindClip(Clip& c, const std::vector<std::string>& boneNames, std::string& err) {
    for (auto& tr : c.tracks) {
        if (tr.bone.rfind("SKXX_", 0) == 0) return Fail(err, "tracks." + tr.bone + ": board bones (SKXX_*) are not supported");
        tr.index = -1;
        for (size_t i = 0; i < boneNames.size(); i++) if (boneNames[i] == tr.bone) { tr.index = (int)i; break; }
        if (tr.index < 0) return Fail(err, "tracks." + tr.bone + ": bone unknown in the skater skeleton");
    }
    return true;
}

// P6: "smooth" keys follow a C1 spline through the keys (cubic Hermite on the quaternion, Catmull-Rom tangents
// scaled by the real key spacing): the motion no longer stops at every key (the old smoothstep per segment eased
// in and out of each key, which read as a stutter). The ends of a one-shot clip keep a zero tangent (ease in / out).
static Quat Align(Quat q, const Quat& ref) {
    if (q.x * ref.x + q.y * ref.y + q.z * ref.z + q.w * ref.w < 0.f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
    return q;
}

static Quat EvalKeys(const Key* k, int n, float t, float duration, bool loop) {
    if (n == 0) return QIdent;
    if (n == 1) return k[0].q;
    if (!std::isfinite(t)) t = 0.f;
    const bool wrap = loop && duration > 0.f;
    // key j for any integer j: in a loop the keys repeat every `duration`
    auto KeyAt = [&](int j, Quat& q) -> float {
        if (!wrap) { j = j < 0 ? 0 : (j >= n ? n - 1 : j); q = k[j].q; return k[j].t; }
        const int idx = ((j % n) + n) % n;
        q = k[idx].q;
        return k[idx].t + (float)((j - idx) / n) * duration;
    };
    int j;
    if (wrap) {
        t = std::fmod(t, duration);
        if (t < 0.f) t += duration;
        if (t < k[0].t) j = -1;
        else { j = 0; while (j + 1 < n && k[j + 1].t <= t) j++; }
    } else {
        if (t <= k[0].t) return k[0].q;
        if (t >= k[n - 1].t) return k[n - 1].q;
        j = 0;
        while (j + 2 < n && k[j + 1].t <= t) j++;
    }
    Quat q0, q1;
    const float t0 = KeyAt(j, q0), t1 = KeyAt(j + 1, q1);
    const float span = t1 - t0;
    if (!(span > 1e-6f)) return q1;
    float u = (t - t0) / span;
    u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    const Key& kj = k[((j % n) + n) % n];
    if (kj.step) return q0;   // lot 5: constant until the next key
    if (!kj.smooth) return QSlerp(q0, q1, u);
    q1 = Align(q1, q0);
    float m0[4] = {0, 0, 0, 0}, m1[4] = {0, 0, 0, 0};   // tangents (per second)
    if (wrap || j >= 1) {
        Quat qp;
        const float tp = KeyAt(j - 1, qp);
        qp = Align(qp, q0);
        const float d = t1 - tp;
        if (d > 1e-6f) { m0[0] = (q1.x - qp.x) / d; m0[1] = (q1.y - qp.y) / d; m0[2] = (q1.z - qp.z) / d; m0[3] = (q1.w - qp.w) / d; }
    }
    if (wrap || j + 2 < n) {
        Quat qn;
        const float tn = KeyAt(j + 2, qn);
        qn = Align(qn, q1);
        const float d = tn - t0;
        if (d > 1e-6f) { m1[0] = (qn.x - q0.x) / d; m1[1] = (qn.y - q0.y) / d; m1[2] = (qn.z - q0.z) / d; m1[3] = (qn.w - q0.w) / d; }
    }
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = (u3 - 2.f * u2 + u) * span, h01 = -2.f * u3 + 3.f * u2, h11 = (u3 - u2) * span;
    Quat r{h00 * q0.x + h10 * m0[0] + h01 * q1.x + h11 * m1[0],
           h00 * q0.y + h10 * m0[1] + h01 * q1.y + h11 * m1[1],
           h00 * q0.z + h10 * m0[2] + h01 * q1.z + h11 * m1[2],
           h00 * q0.w + h10 * m0[3] + h01 * q1.w + h11 * m1[3]};
    return QNorm(r);
}

Quat EvalTrack(const Track& tr, float t, float duration, bool loop) { return EvalKeys(tr.keys.data(), (int)tr.keys.size(), t, duration, loop); }

static void EvalPosKeys(const PosKey* k, int n, float t, float duration, bool loop, float out[3]) {
    out[0] = out[1] = out[2] = 0.f;
    if (n == 0) return;
    if (n == 1) { for (int i = 0; i < 3; i++) out[i] = k[0].v[i]; return; }
    if (!std::isfinite(t)) t = 0.f;
    const bool wrap = loop && duration > 0.f;
    auto KeyAt = [&](int j, const float*& v) -> float {
        if (!wrap) { j = j < 0 ? 0 : (j >= n ? n - 1 : j); v = k[j].v; return k[j].t; }
        const int idx = ((j % n) + n) % n;
        v = k[idx].v;
        return k[idx].t + (float)((j - idx) / n) * duration;
    };
    int j;
    if (wrap) {
        t = std::fmod(t, duration);
        if (t < 0.f) t += duration;
        if (t < k[0].t) j = -1;
        else { j = 0; while (j + 1 < n && k[j + 1].t <= t) j++; }
    } else {
        if (t <= k[0].t) { for (int i = 0; i < 3; i++) out[i] = k[0].v[i]; return; }
        if (t >= k[n - 1].t) { for (int i = 0; i < 3; i++) out[i] = k[n - 1].v[i]; return; }
        j = 0;
        while (j + 2 < n && k[j + 1].t <= t) j++;
    }
    const float *p0, *p1;
    const float t0 = KeyAt(j, p0), t1 = KeyAt(j + 1, p1);
    const float span = t1 - t0;
    if (!(span > 1e-6f)) { for (int i = 0; i < 3; i++) out[i] = p1[i]; return; }
    float u = (t - t0) / span;
    u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    if (k[((j % n) + n) % n].step) { for (int i = 0; i < 3; i++) out[i] = p0[i]; return; }
    if (!k[((j % n) + n) % n].smooth) { for (int i = 0; i < 3; i++) out[i] = p0[i] + (p1[i] - p0[i]) * u; return; }
    float m0[3] = {0, 0, 0}, m1[3] = {0, 0, 0};
    if (wrap || j >= 1) {
        const float* pp; const float tp = KeyAt(j - 1, pp);
        const float d = t1 - tp;
        if (d > 1e-6f) for (int i = 0; i < 3; i++) m0[i] = (p1[i] - pp[i]) / d;
    }
    if (wrap || j + 2 < n) {
        const float* pn; const float tn = KeyAt(j + 2, pn);
        const float d = tn - t0;
        if (d > 1e-6f) for (int i = 0; i < 3; i++) m1[i] = (pn[i] - p0[i]) / d;
    }
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = (u3 - 2.f * u2 + u) * span, h01 = -2.f * u3 + 3.f * u2, h11 = (u3 - u2) * span;
    for (int i = 0; i < 3; i++) out[i] = h00 * p0[i] + h10 * m0[i] + h01 * p1[i] + h11 * m1[i];
}

void EvalPelvis(const std::vector<PosKey>& k, float t, float duration, bool loop, float out[3]) { EvalPosKeys(k.data(), (int)k.size(), t, duration, loop, out); }

float ClipTime(const Clip& c, float elapsed, float manualRatio, float cyclePhase) {
    if (c.hold) return 0.f;   // held pose: always the keys at t = 0
    if (c.drive == Drive::Cycle) {
        if (!std::isfinite(cyclePhase)) cyclePhase = 0.f;
        cyclePhase -= std::floor(cyclePhase);
        return cyclePhase * LoopLength(c);
    }
    if (c.drive == Drive::ManualRatio) {
        if (!std::isfinite(manualRatio)) manualRatio = c.r0;
        float u = (manualRatio - c.r0) / (c.r1 - c.r0);
        if (u < 0.f) u = 0.f;
        if (u > 1.f) u = 1.f;
        return u * c.duration;
    }
    if (!std::isfinite(elapsed) || elapsed < 0.f) elapsed = 0.f;
    const float le = LoopLength(c);   // 2026-11-25: the dismount part is not part of the loop
    if (c.loop) return std::fmod(elapsed, le);
    return elapsed < le ? elapsed : le;
}

bool AuthorMirrored(const Clip& c) {
    const bool switchOnly = c.stances == (1u << STANCE_Switch);
    return (c.feet == Feet::Goofy) != switchOnly;
}

int ClipSpecificity(const Clip& c) {
    int s = 0;
    if ((c.states & (ST_Trick | ST_Grind | ST_Landing)) && c.trickHash) s += 100;   // a named trick / grind / trick landing
    if (c.feet != Feet::Any) s += 10;
    int n = 0;
    for (int k = 0; k < STANCE_Count; k++) if (c.stances & (1u << k)) n++;
    return s + (STANCE_Count - n);
}

// 2026-11-25: dismount part of a riding clip (see clip.h)
namespace {
template <class K> int SplitAt(const std::vector<K>& k, float loopEnd) {
    int i = 0;
    while (i < (int)k.size() && k[i].t < loopEnd - 1e-3f) i++;
    return i;
}
}
Quat EvalClipTrack(const Clip& c, const Track& tr, float t, bool loop) {
    if (!(c.dismount > 0.f) || c.dismount >= c.duration) return EvalTrack(tr, t, c.duration, loop);
    const float le = LoopLength(c);
    const int n = (int)tr.keys.size(), s = SplitAt(tr.keys, le);
    if (t < le - 1e-4f) {
        if (s == 0) return n ? tr.keys[0].q : QIdent;
        return EvalKeys(tr.keys.data(), s, t, le, loop);
    }
    if (s >= n) return s ? EvalKeys(tr.keys.data(), s, le, le, loop) : QIdent;   // no dismount keys: the loop's end
    return EvalKeys(tr.keys.data() + s, n - s, t, c.duration, false);
}
void EvalClipPelvis(const Clip& c, float t, bool loop, float out[3]) {
    if (!(c.dismount > 0.f) || c.dismount >= c.duration) { EvalPelvis(c.pelvis, t, c.duration, loop, out); return; }
    const float le = LoopLength(c);
    const int n = (int)c.pelvis.size(), s = SplitAt(c.pelvis, le);
    if (t < le - 1e-4f) {
        if (s == 0) { out[0] = out[1] = out[2] = 0.f; if (n) for (int i = 0; i < 3; i++) out[i] = c.pelvis[0].v[i]; return; }
        EvalPosKeys(c.pelvis.data(), s, t, le, loop, out);
        return;
    }
    if (s >= n) { if (s) EvalPosKeys(c.pelvis.data(), s, le, le, loop, out); else out[0] = out[1] = out[2] = 0.f; return; }
    EvalPosKeys(c.pelvis.data() + s, n - s, t, c.duration, false, out);
}
