// 2026-11-26 multiplayer (SessionOpenMP mod channel): what one player sends so the others can play the same clips on
// that player's skater. Not the pose: the layers of our player (which clip, clip time, weight), ~15 times a second.
// The receiver needs a clip with the SAME content (any file name: in clips\ or clips\others\); a missing or different
// clip is skipped there.
// Wire text: "S2" + hex of the binary frame (OpenMP carries Lua strings; hex keeps it plain text). Little endian.
// "S1" (v0.19 / v0.20) identified clips by file name + hash; it is not read any more.
#pragma once
#include <cstdint>
#include <string>

constexpr int kNetMaxLayers = 4;

struct NetLayer {
    uint32_t id = 0;        // ContentId of the clip file: high 32 bits
    uint32_t hash = 0;      // ContentId: low 32 bits
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

// Identity of a clip file by its content: FNV-1a 64 of the bytes, without a UTF-8 BOM and without '\r' (a file that
// went through a tool turning LF into CRLF, or back, keeps its identity).
uint64_t ContentId(const std::string& text);
std::string NetEncode(const NetFrame& f);            // "S1" + hex
bool NetDecode(const std::string& s, NetFrame& out); // false on anything malformed (data from other players: untrusted)
