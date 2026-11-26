#include "trick.h"
#include <cctype>

namespace {
const char* kPrefix[STANCE_Count] = {"RGS", "SWS", "FKS", "NLS"};
const char* kLabel[STANCE_Count] = {"Normal", "Switch", "Fakie", "Nollie"};
bool IEq(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; i++) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return i == a.size() && !b[i];
}
} // namespace

const char* StancePrefix(int s) { return s >= 0 && s < STANCE_Count ? kPrefix[s] : "RGS"; }
const char* StanceLabel(int s) { return s >= 0 && s < STANCE_Count ? kLabel[s] : "?"; }
int StanceFromPrefix(const std::string& p) {
    for (int s = 0; s < STANCE_Count; s++) if (IEq(p, kPrefix[s])) return s;
    return -1;
}
int StanceFromLabel(const std::string& l) {
    for (int s = 0; s < STANCE_Count; s++) if (IEq(l, kLabel[s])) return s;
    return -1;
}

std::string Lower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

bool SplitTrickName(const std::string& full, std::string& base, int& stance) {
    std::string s = full;
    if (s.size() > 6 && IEq(s.substr(0, 6), "TRICK_")) s = s.substr(6);
    if (s.size() < 5 || s[3] != '_') return false;
    const int st = StanceFromPrefix(s.substr(0, 3));
    if (st < 0) return false;
    base = s.substr(4);
    stance = st;
    return !base.empty();
}

uint32_t TrickHash(const std::string& base) {
    if (base.empty()) return 0;
    uint32_t h = 2166136261u;
    for (unsigned char c : base) { h ^= (uint32_t)std::tolower(c); h *= 16777619u; }
    return h ? h : 1u;
}

const char* StateName(uint32_t bit) {
    switch (bit) {
        case ST_Manual: return "Manual";
        case ST_NoseManual: return "NoseManual";
        case ST_Trick: return "Trick";
        case ST_Grind: return "Grind";
        case ST_Powerslide: return "Powerslide";
        case ST_Grab: return "Grab";
        case ST_OnFoot: return "OnFoot";
        case ST_Riding: return "Riding";
        case ST_Crouch: return "Crouch";
        case ST_Landing: return "Landing";
        case ST_Walking: return "Walking";
        case ST_Pushing: return "Pushing";
        case ST_Running: return "Running";
        case ST_Dismount: return "Dismount";
        default: return "?";
    }
}
uint32_t StateFromName(const std::string& n) {
    for (uint32_t b : kStateOrder) if (n == StateName(b)) return b;
    if (n == "Kickflip") return ST_Trick;
    return 0;
}
bool StateLoops(uint32_t bit) { return bit != ST_Trick && bit != ST_Landing && bit != ST_Dismount; }
bool StateAllStances(uint32_t bit) { return bit == ST_Trick || bit == ST_Crouch; }   // fakie / nollie only exist crouched

bool StateNamed(uint32_t bit) { return bit == ST_Trick || bit == ST_Grind || bit == ST_Landing; }   // landing: of one trick (2026-11-25)

std::string FigureKey(const FigureVariant& v) {
    // 2026-11-25: a named grind has its own recording ("Grind-Crooked"); a grind with no name keeps "Grind"
    const std::string base = v.state == ST_Trick ? v.trick : v.state == ST_Grind && !v.trick.empty() ? "Grind-" + v.trick
                           : v.state == ST_Landing && !v.trick.empty() ? "Landing-" + v.trick : std::string(StateName(v.state));
    return base + "_" + StancePrefix(v.stance) + "_" + (v.goofy ? "goofy" : "regular");
}

bool ParseFigureKey(const std::string& key, FigureVariant& out) {
    const size_t b = key.rfind('_');
    if (b == std::string::npos || b < 5) return false;
    const std::string feet = key.substr(b + 1);
    if (feet != "regular" && feet != "goofy") return false;
    const size_t a = key.rfind('_', b - 1);
    if (a == std::string::npos || a == 0) return false;
    const int st = StanceFromPrefix(key.substr(a + 1, b - a - 1));
    if (st < 0) return false;
    const std::string base = key.substr(0, a);
    FigureVariant v;
    const uint32_t s = StateFromName(base);
    if (base.rfind("Grind-", 0) == 0 && base.size() > 6) { v.state = ST_Grind; v.trick = base.substr(6); }
    else if (base.rfind("Landing-", 0) == 0 && base.size() > 8) { v.state = ST_Landing; v.trick = base.substr(8); }   // landing of one trick
    else if (s && s != ST_Trick) { v.state = s; }
    else { v.state = ST_Trick; v.trick = base; }
    v.stance = st;
    v.goofy = feet == "goofy";
    out = v;
    return true;
}

std::string TrickDisplay(const std::string& base) {
    std::string r = base;
    for (char& c : r) if (c == '_') c = ' ';
    return r;
}

std::string VariantTitle(const FigureVariant& v) {
    std::string name;
    switch (v.state) {
        case ST_Manual: name = "Manual"; break;
        case ST_NoseManual: name = "Nose manual"; break;
        case ST_Grind: name = v.trick.empty() ? std::string("Grind") : "Grind " + TrickDisplay(v.trick); break;
        case ST_Powerslide: name = "Powerslide"; break;
        case ST_Grab: name = "Grab"; break;
        case ST_OnFoot: name = "On foot (standing)"; break;
        case ST_Riding: name = "Riding"; break;
        case ST_Crouch: name = "Crouch"; break;
        case ST_Landing: name = v.trick.empty() ? std::string("Landing") : "Landing " + TrickDisplay(v.trick); break;
        case ST_Walking: name = "Walking"; break;
        case ST_Pushing: name = "Pushing"; break;
        case ST_Running: name = "Running"; break;
        case ST_Dismount: name = "Dismount"; break;
        default: name = TrickDisplay(v.trick); break;
    }
    return name + "  ·  " + StanceLabel(v.stance) + "  ·  " + (v.goofy ? "goofy" : "regular");
}

const std::vector<CatalogTrick>& TrickCatalog() {
    static const std::vector<CatalogTrick> k = {
        // ollies
        {"RGS_Ollie", "Ollies"}, {"SWS_Ollie", "Ollies"}, {"FKS_Ollie", "Ollies"}, {"NLS_Nollie", "Ollies"},
        {"RGS_ShittyOllie", "Ollies"}, {"SWS_ShittySwitchOllie", "Ollies"}, {"FKS_ShittyOllie", "Ollies"}, {"NLS_ShittyNollie", "Ollies"},
        {"RGS_NoComply", "Ollies"},
        // flips
        {"RGS_KickFlip", "Flips"}, {"SWS_KickFlip", "Flips"}, {"FKS_Kickflip", "Flips"}, {"NLS_Kickflip", "Flips"},
        {"RGS_HeelFlip", "Flips"}, {"SWS_HeelFlip", "Flips"}, {"FKS_heelflip", "Flips"}, {"NLS_Heelflip", "Flips"},
        {"RGS_Treflip", "Flips"}, {"SWS_Treflip", "Flips"}, {"FKS_TreFlip", "Flips"}, {"NLS_Treflip", "Flips"},
        {"RGS_VarialFlip", "Flips"}, {"SWS_VarialFlip", "Flips"}, {"FKS_VarialFlip", "Flips"}, {"NLS_VarialFlip", "Flips"},
        {"RGS_VarialHeel", "Flips"}, {"SWS_VarialHeel", "Flips"}, {"FKS_VarialHeel", "Flips"}, {"NLS_VarialHeel", "Flips"},
        {"RGS_HardFlip", "Flips"}, {"SWS_HardFlip", "Flips"}, {"FKS_HardFlip", "Flips"},
        {"SWS_Hardflip360", "Flips"}, {"FKS_HardFlip360", "Flips"},
        {"RGS_InwardHeel", "Flips"}, {"SWS_InwardHeel", "Flips"}, {"SWS_Inward360", "Flips"},
        {"RGS_LaserFlip", "Flips"}, {"SWS_Laserflip", "Flips"},
        {"RGS_Pressure_KickFlip", "Flips"}, {"SWS_Pressure_KickFlip", "Flips"},
        // shove-its
        {"RGS_BS_ShoveIt", "Shove-its"}, {"FKS_BS_ShoveIt", "Shove-its"}, {"RGS_BS_PopShuvIt", "Shove-its"}, {"RGS_BS_ShoveIt_360", "Shove-its"},
        // late tricks
        {"RGS_Late_LF_OllieNorth", "Late tricks"}, {"RGS_Late_RF_OllieSouth", "Late tricks"},
        {"SWS_Late_LF_OllieSouth", "Late tricks"}, {"SWS_Late_RF_OllieNorth", "Late tricks"},
        {"FKS_Late_LF_OllieNorth", "Late tricks"}, {"FKS_Late_RF_OllieSouth", "Late tricks"},
        {"NLS_Late_LF_NollieNorth", "Late tricks"}, {"NLS_Late_RF_NollieSouth", "Late tricks"},
    };
    return k;
}
