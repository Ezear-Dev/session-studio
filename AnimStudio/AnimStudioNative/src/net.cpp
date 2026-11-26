#include "net.h"
#include <cmath>
#include <cstring>

namespace {

void PutU32(std::string& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((char)((v >> (8 * i)) & 0xff)); }
void PutF(std::string& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); PutU32(b, v); }
struct Rd {
    const uint8_t* p; size_t n, i = 0;
    bool ok = true;
    uint8_t U8() { if (i + 1 > n) { ok = false; return 0; } return p[i++]; }
    uint32_t U32() { if (i + 4 > n) { ok = false; return 0; } uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)p[i + k] << (8 * k); i += 4; return v; }
    float F() { const uint32_t v = U32(); float f; std::memcpy(&f, &v, 4); return f; }
};
int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool Finite(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

} // namespace

uint64_t ContentId(const std::string& text) {
    size_t i = 0;
    if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) i = 3;
    uint64_t h = 1469598103934665603ull;
    for (; i < text.size(); i++) {
        const uint8_t c = (uint8_t)text[i];
        if (c == '\r') continue;
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string NetEncode(const NetFrame& f) {
    std::string b;
    PutU32(b, f.seq);
    const int n = f.n < 0 ? 0 : (f.n > kNetMaxLayers ? kNetMaxLayers : f.n);
    b.push_back((char)n);
    for (int i = 0; i < n; i++) {
        const NetLayer& L = f.l[i];
        PutU32(b, L.id);
        PutU32(b, L.hash);
        b.push_back((char)((L.mirrored ? 1 : 0) | (L.dis ? 2 : 0) | (L.trick ? 4 : 0)));
        PutF(b, L.t); PutF(b, L.speed); PutF(b, L.w); PutF(b, L.wSpeed); PutF(b, L.legW);
    }
    static const char* kHex = "0123456789abcdef";
    std::string s = "S2";
    for (unsigned char c : b) { s.push_back(kHex[c >> 4]); s.push_back(kHex[c & 15]); }
    return s;
}

bool NetDecode(const std::string& s, NetFrame& out) {
    if (s.size() < 2 + 10 || s.size() > 2 + 2 * 512 || s[0] != 'S' || s[1] != '2' || (s.size() & 1)) return false;
    uint8_t buf[512];
    const size_t nb = (s.size() - 2) / 2;
    for (size_t i = 0; i < nb; i++) {
        const int a = Hex(s[2 + 2 * i]), b = Hex(s[3 + 2 * i]);
        if (a < 0 || b < 0) return false;
        buf[i] = (uint8_t)(a * 16 + b);
    }
    Rd r{buf, nb};
    NetFrame f;
    f.seq = r.U32();
    f.n = r.U8();
    if (!r.ok || f.n > kNetMaxLayers) return false;
    for (int i = 0; i < f.n; i++) {
        NetLayer& L = f.l[i];
        L.id = r.U32();
        L.hash = r.U32();
        const uint8_t fl = r.U8();
        L.mirrored = (fl & 1) != 0; L.dis = (fl & 2) != 0; L.trick = (fl & 4) != 0;
        L.t = r.F(); L.speed = r.F(); L.w = r.F(); L.wSpeed = r.F(); L.legW = r.F();
        if (!r.ok || (fl & ~7) != 0) return false;
        if (!Finite(L.t, 0.f, 3600.f) || !Finite(L.speed, -10.f, 10.f) || !Finite(L.w, 0.f, 1.f) || !Finite(L.wSpeed, -100.f, 100.f) ||
            !Finite(L.legW, 0.f, 1.f)) return false;
    }
    if (r.i != nb) return false;
    out = f;
    return true;
}
