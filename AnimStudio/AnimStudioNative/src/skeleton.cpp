#include "skeleton.h"
#include "json.h"
#include <cmath>
#include <string>

namespace {
JValue Num(double v) { JValue j; j.t = JValue::Num; j.n = v; return j; }
JValue Str(const std::string& s) { JValue j; j.t = JValue::Str; j.s = s; return j; }
JValue Arr() { JValue j; j.t = JValue::Arr; return j; }
bool Fail(std::string& err, const std::string& m) { err = m; return false; }
} // namespace

bool IsLegBone(const std::string& name) {
    for (const char* bad : {"IK_UPNODE", "SKXX_", "Anchor"}) if (name.find(bad) != std::string::npos) return false;
    for (const char* ok : {"Thigh", "Calf", "Foot", "Toe"}) if (name.find(ok) != std::string::npos) return true;
    return false;
}

std::string SkeletonToJson(const SkeletonFile& s) {
    JValue root; root.t = JValue::Obj;
    root.o.emplace_back("format", Str("animstudio-skeleton"));
    root.o.emplace_back("version", Num(s.version));
    JValue names = Arr(), parents = Arr(), pos = Arr(), conv = Arr();
    for (auto& n : s.names) names.a.push_back(Str(n));
    for (int p : s.parents) parents.a.push_back(Num(p));
    for (float v : s.refPos) pos.a.push_back(Num(std::round((double)v * 1000.0) / 1000.0));
    for (int c : s.conv) conv.a.push_back(Num(c));
    root.o.emplace_back("names", std::move(names));
    root.o.emplace_back("parents", std::move(parents));
    root.o.emplace_back("refPos", std::move(pos));
    root.o.emplace_back("conv", std::move(conv));
    return JWrite(root) + "\n";
}

bool ParseSkeleton(const std::string& text, SkeletonFile& out, std::string& err) {
    JValue v;
    std::string jerr;
    if (!JParse(text, v, jerr)) return Fail(err, "unreadable skeleton: " + jerr);
    if (v.t != JValue::Obj) return Fail(err, "squelette: object expected");
    const JValue* ver = v.get("version");
    if (!ver || ver->t != JValue::Num || ver->n != 1.0) return Fail(err, "skeleton: version 1 expected (file from another studio version)");
    const JValue* names = v.get("names");
    const JValue* parents = v.get("parents");
    const JValue* pos = v.get("refPos");
    const JValue* conv = v.get("conv");
    if (!names || !parents || !pos || !conv || names->t != JValue::Arr || parents->t != JValue::Arr || pos->t != JValue::Arr || conv->t != JValue::Arr)
        return Fail(err, "skeleton: fields names, parents, refPos, conv expected");
    const size_t n = names->a.size();
    if (n == 0 || n > 256) return Fail(err, "skeleton: invalid bone count");
    if (parents->a.size() != n || conv->a.size() != n || pos->a.size() != 3 * n) return Fail(err, "skeleton: inconsistent sizes");
    SkeletonFile s;
    s.version = 1;
    for (size_t i = 0; i < n; i++) {
        const JValue& nm = names->a[i];
        const JValue& p = parents->a[i];
        const JValue& c = conv->a[i];
        if (nm.t != JValue::Str || nm.s.empty()) return Fail(err, "skeleton: invalid bone name");
        if (p.t != JValue::Num || p.n < -1 || p.n >= (double)i || p.n != std::floor(p.n)) return Fail(err, "skeleton: invalid parent for " + nm.s);
        if (c.t != JValue::Num || c.n < 0 || c.n > 3 || c.n != std::floor(c.n)) return Fail(err, "skeleton: invalid mirror convention for " + nm.s);
        s.names.push_back(nm.s);
        s.parents.push_back((int)p.n);
        s.conv.push_back((int)c.n);
    }
    for (auto& e : pos->a) {
        if (e.t != JValue::Num || !std::isfinite(e.n)) return Fail(err, "skeleton: invalid position");
        s.refPos.push_back((float)e.n);
    }
    out = std::move(s);
    return true;
}
