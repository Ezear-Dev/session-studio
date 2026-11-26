#include "figure.h"
#include "json.h"
#include <cmath>

namespace {
constexpr float kStep = 1.f / 30.f;
JValue Num(double v) { JValue j; j.t = JValue::Num; j.n = v; return j; }
JValue Str(const std::string& s) { JValue j; j.t = JValue::Str; j.s = s; return j; }
JValue Arr() { JValue j; j.t = JValue::Arr; return j; }
bool Fail(std::string& err, const std::string& m) { err = m; return false; }
} // namespace

bool IsPositionBone(const std::string& n) { return n == "AMXX_Pelvis" || n.rfind("SKXX_", 0) == 0; }

std::string FigureToJson(const FigureRec& r) {
    JValue root; root.t = JValue::Obj;
    root.o.emplace_back("format", Str("animstudio-figure"));
    root.o.emplace_back("version", Num(r.version));
    root.o.emplace_back("figure", Str(r.figure));
    root.o.emplace_back("fps", Num(r.fps));
    root.o.emplace_back("bones", Num(r.bones));
    JValue times = Arr(), ratios = Arr(), frames = Arr();
    for (float t : r.times) times.a.push_back(Num(t));
    for (float x : r.ratios) ratios.a.push_back(Num(x));
    const size_t n = r.bones > 0 ? r.frames.size() / (size_t)r.bones : 0;
    for (size_t i = 0; i < n; i++) {
        JValue fr = Arr();
        for (int b = 0; b < r.bones; b++) {
            const Quat& q = r.frames[i * r.bones + b];
            fr.a.push_back(Num(q.x)); fr.a.push_back(Num(q.y)); fr.a.push_back(Num(q.z)); fr.a.push_back(Num(q.w));
        }
        frames.a.push_back(std::move(fr));
    }
    if (r.bones > 0 && r.frames.size() % (size_t)r.bones) frames.a.push_back(Arr());   // inconsistent: reported when read back
    root.o.emplace_back("times", std::move(times));
    root.o.emplace_back("ratios", std::move(ratios));
    root.o.emplace_back("frames", std::move(frames));
    if (!r.phases.empty()) {
        JValue ph = Arr();
        for (int p : r.phases) ph.a.push_back(Num(p));
        root.o.emplace_back("phases", std::move(ph));
    }
    if (!r.posBones.empty()) {
        JValue pb = Arr(), pv = Arr();
        for (int b : r.posBones) pb.a.push_back(Num(b));
        for (float v : r.positions) pv.a.push_back(Num(std::round(v * 100.f) / 100.f));
        root.o.emplace_back("posBones", std::move(pb));
        root.o.emplace_back("positions", std::move(pv));
    }
    return JWrite(root, 0) + "\n";
}

bool ParseFigure(const std::string& text, FigureRec& out, std::string& err) {
    JValue v;
    std::string jerr;
    if (!JParse(text, v, jerr)) return Fail(err, "unreadable move recording: " + jerr);
    if (v.t != JValue::Obj) return Fail(err, "figure: object expected");
    const JValue* ver = v.get("version");
    if (!ver || ver->t != JValue::Num || ver->n != 1.0) return Fail(err, "move recording: version 1 expected");
    const JValue* fig = v.get("figure");
    if (!fig || fig->t != JValue::Str || fig->s.empty()) return Fail(err, "move recording: move name expected");
    const JValue* bones = v.get("bones");
    if (!bones || bones->t != JValue::Num || bones->n <= 0 || bones->n > 256) return Fail(err, "move recording: invalid bone count");
    const JValue* times = v.get("times");
    const JValue* ratios = v.get("ratios");
    const JValue* frames = v.get("frames");
    if (!times || !ratios || !frames || times->t != JValue::Arr || ratios->t != JValue::Arr || frames->t != JValue::Arr)
        return Fail(err, "move recording: fields times, ratios, frames expected");
    const size_t n = frames->a.size();
    if (n < 2) return Fail(err, "move recording: at least 2 frames expected");
    if (times->a.size() != n || ratios->a.size() != n) return Fail(err, "figure: inconsistent sizes");
    FigureRec r;
    r.figure = fig->s;
    r.bones = (int)bones->n;
    const JValue* fps = v.get("fps");
    if (fps && fps->t == JValue::Num && fps->n > 0) r.fps = (float)fps->n;
    for (size_t i = 0; i < n; i++) {
        const JValue& t = times->a[i];
        const JValue& x = ratios->a[i];
        if (t.t != JValue::Num || x.t != JValue::Num || !std::isfinite(t.n) || !std::isfinite(x.n)) return Fail(err, "move recording: invalid time or ratio");
        if (i && !(t.n > r.times.back())) return Fail(err, "figure: times must increase");
        r.times.push_back((float)t.n);
        r.ratios.push_back((float)x.n);
        const JValue& fr = frames->a[i];
        if (fr.t != JValue::Arr || fr.a.size() != (size_t)r.bones * 4) return Fail(err, "figure: frame " + std::to_string(i) + " has an inconsistent size");
        for (int b = 0; b < r.bones; b++) {
            double q[4];
            for (int k = 0; k < 4; k++) {
                const JValue& e = fr.a[(size_t)b * 4 + k];
                if (e.t != JValue::Num || !std::isfinite(e.n)) return Fail(err, "move recording: invalid quaternion");
                q[k] = e.n;
            }
            r.frames.push_back(QNorm(Quat{(float)q[0], (float)q[1], (float)q[2], (float)q[3]}));
        }
    }
    const JValue* phases = v.get("phases");
    if (phases) {
        if (phases->t != JValue::Arr || phases->a.size() != n) return Fail(err, "figure: phases must have one value per frame");
        for (const JValue& p : phases->a) {
            if (p.t != JValue::Num || !std::isfinite(p.n)) return Fail(err, "move recording: invalid phase");
            r.phases.push_back((int)p.n);
        }
    }
    const JValue* pb = v.get("posBones");
    const JValue* pv = v.get("positions");
    if (pb && pv && pb->t == JValue::Arr && pv->t == JValue::Arr) {   // optional (P6): ignored if inconsistent
        std::vector<int> bs;
        bool ok = true;
        for (const JValue& b : pb->a) { if (b.t != JValue::Num || b.n < 0 || b.n >= r.bones) { ok = false; break; } bs.push_back((int)b.n); }
        if (ok && pv->a.size() == n * bs.size() * 3) {
            std::vector<float> ps;
            for (const JValue& x : pv->a) { if (x.t != JValue::Num || !std::isfinite(x.n)) { ok = false; break; } ps.push_back((float)x.n); }
            if (ok) { r.posBones = std::move(bs); r.positions = std::move(ps); }
        }
    }
    out = std::move(r);
    return true;
}

// ------------------------------------------------------------------ the recorder
void FigureRecorder::Reset(int bones, const std::vector<int>& posBones) {
    if (bones < 0) bones = 0;
    if (bones > kMaxBones) bones = kMaxBones;
    bones_ = bones;
    buf_.assign((size_t)kMaxFrames * (size_t)bones_, QIdent);
    posBones_.clear();
    for (int b : posBones) if (b >= 0 && b < bones_ && (int)posBones_.size() < kMaxPos) posBones_.push_back(b);
    posBuf_.assign((size_t)kMaxFrames * posBones_.size() * 3, 0.f);
    active_ = 0;
    finishedId_ = 0;
    frames_ = 0;
    accum_ = 0.f;
    prevFig_ = 0;
    havePop_ = false;
    pop_ = 0;
    finished_.store(false);
}

void FigureRecorder::Sample(const GameState& gs, const Quat* locals, const float (*pos)[3], int phase) {
    Quat* dst = buf_.data() + (size_t)frames_ * (size_t)bones_;
    for (int b = 0; b < bones_; b++) dst[b] = locals[b];
    float* pd = posBuf_.data() + (size_t)frames_ * posBones_.size() * 3;
    for (size_t k = 0; k < posBones_.size(); k++)
        for (int c = 0; c < 3; c++) pd[k * 3 + c] = pos ? pos[posBones_[k]][c] : 0.f;
    times_[frames_] = (float)frames_ * kStep;
    ratios_[frames_] = std::isfinite(gs.manualRatio) ? gs.manualRatio : 0.f;
    phases_[frames_] = phase >= 0 ? phase : gs.trickPhase < 0 ? 0 : gs.trickPhase > 4 ? 4 : gs.trickPhase;
    frames_++;
}

void FigureRecorder::Finish() {
    // lot 5: a trick is judged on its air part (the ground tail does not make a too short trick long enough)
    const int have = (!activeLoops_ && tail_ >= 0) ? frames_ - tail_ : frames_;
    if (have >= (activeLoops_ && !activeShort_ ? kMinFrames : kKickMin)) {
        finishedId_ = active_;
        finishedLoops_ = activeLoops_;
        finished_.store(true);
    }
    active_ = 0;   // too short: dropped (the game thread still wants it)
}

void FigureRecorder::Start(const GameState& gs, bool loops, const Quat* locals, const float (*pos)[3]) {
    active_ = gs.figId;
    activeLoops_ = loops;
    activeShort_ = gs.figShort;
    frames_ = 0;
    tail_ = -1;
    accum_ = 0.f;
    Sample(gs, locals, pos);
}

void FigureRecorder::Tick(const GameState& gs, const Quat* gameLocals, const float (*gamePos)[3], int bones, float dt) {
    if (bones_ <= 0 || bones != bones_ || !gameLocals) {   // unexpected skeleton: drop the recording in progress
        active_ = 0;
        return;
    }
    if (!std::isfinite(dt) || dt < 0.f || dt > 0.1f) dt = 0.f;
    const bool newPop = havePop_ ? gs.popSerial != pop_ : gs.kickflip;
    havePop_ = true;
    pop_ = gs.popSerial;
    if (active_ != 0) {
        // trick: the air part ends on the ground (or at 2 s), then kGroundTail frames on the ground (lot 5)
        if (!activeLoops_ && tail_ < 0 && (!gs.kickflip || frames_ >= kKickMax)) tail_ = 0;
        const bool stop = activeLoops_ ? (gs.figId != active_ || gs.kickflip) : (newPop || (tail_ >= 0 && (gs.kickflip || tail_ >= kGroundTail || gs.onFoot || gs.walking || gs.running)));
        if (stop) Finish();
        else {
            accum_ += dt;
            const int cap = activeLoops_ ? kMaxFrames : (tail_ < 0 ? kKickMax : kMaxFrames);
            while (accum_ >= kStep && frames_ < cap && (tail_ < 0 || tail_ < kGroundTail)) {
                accum_ -= kStep;
                Sample(gs, gameLocals, gamePos, tail_ >= 0 ? kGroundPhase : -1);
                if (tail_ >= 0) tail_++;
            }
            if (frames_ >= cap || (tail_ >= 0 && tail_ >= kGroundTail)) Finish();
        }
    }
    // a new figure is wanted from the frame its id changes, even if the previous recording is still to be taken
    // (the first on-foot pose right after a landing was lost)
    if (gs.figId != prevFig_) wantStart_ = true;
    if (active_ == 0 && !finished_.load() && gs.figId != 0 && gs.figWanted) {
        if (gs.kickflip) { if (newPop) Start(gs, false, gameLocals, gamePos); }
        else if (wantStart_) Start(gs, true, gameLocals, gamePos);
    }
    if (active_ != 0) wantStart_ = false;
    prevFig_ = gs.figId;
}

bool FigureRecorder::TakeFinished(FigureRec& out, int& figId) {
    if (!finished_.load()) return false;
    FigureRec r;
    r.bones = bones_;
    r.times.assign(times_, times_ + frames_);
    r.ratios.assign(ratios_, ratios_ + frames_);
    r.frames.assign(buf_.begin(), buf_.begin() + (size_t)frames_ * (size_t)bones_);
    if (!finishedLoops_) r.phases.assign(phases_, phases_ + frames_);
    r.posBones = posBones_;
    r.positions.assign(posBuf_.begin(), posBuf_.begin() + (size_t)frames_ * posBones_.size() * 3);
    figId = finishedId_;
    finishedId_ = 0;
    frames_ = 0;
    finished_.store(false);
    out = std::move(r);
    return true;
}
