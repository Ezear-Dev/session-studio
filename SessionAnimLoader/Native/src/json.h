// Minimal JSON of SessionAnimLoaderNative (reading + writing), hand-written: no downloaded library.
#pragma once
#include <string>
#include <utility>
#include <vector>

struct JValue {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JValue> a;
    std::vector<std::pair<std::string, JValue>> o;
    const JValue* get(const char* key) const;   // nullptr if absent or if this is not an object
};

// UTF-8, BOM skipped, maximum depth 64. err: "line L col C: <reason>".
bool JParse(const std::string& text, JValue& out, std::string& err);
// numbers as %.9g; NaN and inf written as null
std::string JWrite(const JValue& v, int indent = 2);
