// 2026-11-26 multiplayer (SessionOpenMP mod channel): what one player sends so the others can play the same clips on
// that player's skater. Not the pose: the layers of our player (which clip, clip time, weight), ~15 times a second.
// The receiver needs the SAME clip file (same name, same content); a missing or different clip is skipped there.
// Wire text: "S1" + hex of the binary frame (OpenMP carries Lua strings; hex keeps it plain text). Little endian.
#pragma once
#include <cstdint>
#include <string>

constexpr int kNetMaxLayers = 4;

struct NetLayer {
    uint32_t id = 0;        // NetId(file name)
    uint32_t hash = 0;      // low 32 bits of the file content hash (Fnv1a)
    bool mirrored = false, dis = false, trick = false;
    float t = 0.f;          // clip time evaluated this frame (after every mapping: cycle, manual ratio, trick warp, dismount)
    float speed = 0.f;      // clip seconds per real second (receiver extrapolates between frames)
    float w = 0.f;          // final layer weight (fade x master), 0..1
    float wSpeed = 0.f;     // weight change per second
    float legW = 1.f;       // leg multiplier of a trick layer (1 otherwise)
};

struct NetFrame {
    uint32_t seq = 0;       // sender's frame counter (older or equal = ignored)
    int n = 0;
    NetLayer l[kNetMaxLayers];
};

uint32_t NetId(const std::string& file);             // FNV-1a 32 of the lower-case file name
std::string NetEncode(const NetFrame& f);            // "S1" + hex
bool NetDecode(const std::string& s, NetFrame& out); // false on anything malformed (data from other players: untrusted)
