#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <locale.h>

const JValue* JValue::get(const char* key) const {
    if (t != Obj) return nullptr;
    for (auto& kv : o) if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

// "C" locale for numbers: the /MD CRT is shared with the game and UE4SS, another DLL may change LC_NUMERIC
_locale_t CLocale() {
    static _locale_t loc = _create_locale(LC_NUMERIC, "C");
    return loc;
}

struct Parser {
    const char* p; const char* beg; const char* end;
    std::string err;
    bool fail(const char* why) {
        if (err.empty()) {
            int line = 1, col = 1;
            for (const char* q = beg; q < p && q < end; q++) { if (*q == '\n') { line++; col = 1; } else col++; }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "line %d col %d: %s", line, col, why);
            err = buf;
        }
        return false;
    }
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool lit(const char* w) {
        const size_t n = std::strlen(w);
        if ((size_t)(end - p) >= n && std::memcmp(p, w, n) == 0) { p += n; return true; }
        return false;
    }
    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (end - p < 4) return fail("\\u incomplet");
        v = 0;
        for (int i = 0; i < 4; i++) {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return fail("\\u invalide");
        }
        return true;
    }
    bool str(std::string& s) {
        p++;   // guillemet ouvrant
        while (true) {
            if (p >= end) return fail("unterminated string");
            const char c = *p++;
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return fail("control character in a string");
            if (c != '\\') { s += c; continue; }
            if (p >= end) return fail("incomplete escape");
            const char e = *p++;
            switch (e) {
                case '"': s += '"'; break;
                case '\\': s += '\\'; break;
                case '/': s += '/'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        unsigned lo = 0;
                        if (!(lit("\\u") && hex4(lo)) || lo < 0xDC00 || lo > 0xDFFF) return fail("invalid surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) return fail("invalid surrogate pair");
                    utf8(s, cp);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
    }
    bool num(double& out) {
        const char* s = p;
        if (p < end && *p == '-') p++;
        if (p >= end || !(*p >= '0' && *p <= '9')) { p = s; return fail("valeur invalide"); }
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
        const std::string tok(s, p);
        char* e = nullptr;
        out = _strtod_l(tok.c_str(), &e, CLocale());   // independent of the process locale (decimal comma)
        if (!e || *e != '\0') { p = s; return fail("nombre invalide"); }
        return true;
    }
    bool value(JValue& v, int depth) {
        if (depth > 64) return fail("maximum depth (64) exceeded");
        ws();
        if (p >= end) return fail("value expected, end of text");
        const char c = *p;
        if (c == '{') {
            v.t = JValue::Obj; p++; ws();
            if (p < end && *p == '}') { p++; return true; }
            while (true) {
                ws();
                if (p >= end || *p != '"') return fail("field name expected");
                std::string k;
                if (!str(k)) return false;
                ws();
                if (p >= end || *p != ':') return fail("':' expected");
                p++;
                JValue child;
                if (!value(child, depth + 1)) return false;
                v.o.emplace_back(std::move(k), std::move(child));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return fail("',' or '}' expected");
            }
        }
        if (c == '[') {
            v.t = JValue::Arr; p++; ws();
            if (p < end && *p == ']') { p++; return true; }
            while (true) {
                JValue child;
                if (!value(child, depth + 1)) return false;
                v.a.push_back(std::move(child));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return fail("',' or ']' expected");
            }
        }
        if (c == '"') { v.t = JValue::Str; return str(v.s); }
        if (lit("true")) { v.t = JValue::Bool; v.b = true; return true; }
        if (lit("false")) { v.t = JValue::Bool; v.b = false; return true; }
        if (lit("null")) { v.t = JValue::Null; return true; }
        v.t = JValue::Num;
        return num(v.n);
    }
};

void WStr(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    o += '"';
}

bool Flat(const JValue& v) {   // array of scalars: written on one line
    if (v.t != JValue::Arr) return false;
    for (auto& e : v.a) if (e.t == JValue::Arr || e.t == JValue::Obj) return false;
    return true;
}

void W(std::string& o, const JValue& v, int indent, int level) {
    const std::string pad(indent > 0 ? (size_t)indent * (level + 1) : 0, ' ');
    const std::string padEnd(indent > 0 ? (size_t)indent * level : 0, ' ');
    const char* nl = indent > 0 ? "\n" : "";
    switch (v.t) {
        case JValue::Null: o += "null"; break;
        case JValue::Bool: o += v.b ? "true" : "false"; break;
        case JValue::Num: {
            if (!std::isfinite(v.n)) { o += "null"; break; }
            char b[40];
            _snprintf_l(b, sizeof(b), "%.9g", CLocale(), v.n);
            o += b;
            break;
        }
        case JValue::Str: WStr(o, v.s); break;
        case JValue::Arr:
            if (v.a.empty()) { o += "[]"; break; }
            if (Flat(v)) {
                o += '[';
                for (size_t i = 0; i < v.a.size(); i++) { if (i) o += ", "; W(o, v.a[i], indent, level + 1); }
                o += ']';
                break;
            }
            o += '['; o += nl;
            for (size_t i = 0; i < v.a.size(); i++) { o += pad; W(o, v.a[i], indent, level + 1); if (i + 1 < v.a.size()) o += ','; o += nl; }
            o += padEnd; o += ']';
            break;
        case JValue::Obj:
            if (v.o.empty()) { o += "{}"; break; }
            o += '{'; o += nl;
            for (size_t i = 0; i < v.o.size(); i++) {
                o += pad; WStr(o, v.o[i].first); o += indent > 0 ? ": " : ":";
                W(o, v.o[i].second, indent, level + 1);
                if (i + 1 < v.o.size()) o += ',';
                o += nl;
            }
            o += padEnd; o += '}';
            break;
    }
}

} // namespace

bool JParse(const std::string& text, JValue& out, std::string& err) {
    Parser ps{text.data(), text.data(), text.data() + text.size(), {}};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) ps.p += 3;
    out = JValue{};
    if (!ps.value(out, 0)) { err = ps.err; return false; }
    ps.ws();
    if (ps.p != ps.end) { ps.fail("extra text after the value"); err = ps.err; return false; }
    return true;
}

std::string JWrite(const JValue& v, int indent) {
    std::string o;
    W(o, v, indent, 0);
    return o;
}
