#include "random_draw.h"
#include "json.h"
#include <cstdio>

bool DrawSlotOf(const std::string& figureKey, DrawSlot& out) {
    FigureVariant v;
    if (!ParseFigureKey(figureKey, v) || v.state != ST_Trick || v.trick.empty()) return false;
    out.trickHash = TrickHash(v.trick);
    out.stance = v.stance;
    out.goofy = v.goofy;
    return true;
}

bool ParseGameDraw(const std::string& text, std::vector<std::string>& keys, std::string& err) {
    const std::string body = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? text.substr(3) : text;
    JValue v;
    if (!JParse(body, v, err)) return false;
    if (v.t != JValue::Obj) { err = "an object is expected"; return false; }
    std::vector<std::string> out;
    if (const JValue* a = v.get("gameAnimation")) {
        if (a->t != JValue::Arr) { err = "gameAnimation: a list is expected"; return false; }
        for (const JValue& e : a->a) {
            if (e.t != JValue::Str) { err = "gameAnimation: a list of names is expected"; return false; }
            out.push_back(e.s);
        }
    }
    keys = std::move(out);
    return true;
}

std::string GameDrawToJson(const std::vector<std::string>& keys) {
    std::string s = "{\n  \"gameAnimation\": [";
    bool first = true;
    for (const std::string& k : keys) {
        DrawSlot slot;
        if (!DrawSlotOf(k, slot) || k.find('"') != std::string::npos || k.find('\\') != std::string::npos) continue;
        s += std::string(first ? "\n    \"" : ",\n    \"") + k + "\"";
        first = false;
    }
    s += first ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}
