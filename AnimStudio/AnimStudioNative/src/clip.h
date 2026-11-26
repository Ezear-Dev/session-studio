// .sanim clips (format v1): model, reading + validation, binding to the skeleton, evaluation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "math.h"
#include "json.h"
#include "trick.h"
#include "trick_sync.h"

enum class Mode { Additive, Replace };
// Cycle (2026-11-25): the clip time follows the game's step cycle (cycle.h), one clip = one stride, always looping
enum class Drive { Time, ManualRatio, Cycle };
// P6: feet targeted by a clip
enum class Feet { Any = 0, Regular = 1, Goofy = 2 };

// lot 5: step = constant interpolation ("interp": "step"): the value stays until the next key (smooth is then false)
struct Key { float t; Quat q; bool smooth; bool step = false; };
struct Track { std::string bone; int index = -1; float weight = 1.f; std::vector<Key> keys; };
// P6 lot 2: pelvis offset key (component space, cm), added to the game's pelvis position; the legs are re-solved so
// that the feet stay where they were (ApplyPelvisOffset, pelvis.h)
struct PosKey { float t; float v[3]; bool smooth; bool step = false; };
struct Clip {
    std::string file, name;
    bool enabled = true;
    uint32_t states = 0;
    // P6 (ST_Trick): targeted trick by its base name ("" = every flip trick); trickHash = TrickHash(trick)
    std::string trick;
    uint32_t trickHash = 0;
    uint8_t stances = kAllStances;   // bits (1 << Stance); for Manual & co, only Normal and Switch exist
    Feet feet = Feet::Any;
    int priority = 0;
    Drive drive = Drive::Time;
    float duration = 1.f;
    bool loop = true;
    // held pose (play.hold): the keys at t = 0 apply all the time, over whatever the game plays (base poses: riding,
    // walking...). No clock, so nothing to loop or to keep in phase with the game's own cycle.
    bool hold = false;
    // lot 5 (OBSOLETE since the landing sync, 2026-11-25): landing part of a trick clip. Still read so the editor /
    // converter can split old files into an air clip + a landing clip; the player ignores it.
    float landing = 0.f, groundAt = 0.f;
    // 2026-11-25 (ST_Trick only): instants of the game's events in the recorded figure (play.markers); the player warps
    // the clip time onto the live events (trick_sync.h). ok = false: old clip, real time from the pop.
    TrickMarkers markers;
    // 2026-11-25 (ST_Riding only): dismount part of a riding clip. The last `dismount` seconds of the clip are played once
    // when the feet leave the board; the loop is [0, duration - dismount). 0 = none.
    float dismount = 0.f;
    float r0 = 0.f, r1 = 1.f;
    float blendIn = 0.25f, blendOut = 0.30f;
    Mode mode = Mode::Additive;
    bool mirror = true;
    uint64_t stamp = 0;        // content version (given by ClipLibrary): changes when the file changes
    // 2026-11-26 multiplayer: identity on the wire (ClipLibrary): NetId(file) and the low 32 bits of the file hash
    uint32_t netId = 0, netHash = 0;
    std::vector<std::pair<std::string, JValue>> extra;   // unknown root fields, in file order (kept)
    std::vector<Track> tracks;
    std::vector<PosKey> pelvis;   // P6 lot 2: "pelvis" (optional); a clip needs tracks or pelvis keys
};

// P6: are the clip keys written for a "goofy" body (goofy feet, or regular feet in switch only)?
// The player mirrors when this differs from the skater's body (and mirrorForGoofy is true).
bool AuthorMirrored(const Clip& c);
// P6: the more precisely a clip targets (named trick, feet, few stances), the more it wins at equal priority
int ClipSpecificity(const Clip& c);

// Reads and validates a .sanim. On refusal, err names the faulty field.
bool ParseClip(const std::string& text, const std::string& file, Clip& out, std::string& err);
// Binds each track to its bone index. Refuses an unknown bone and board bones (SKXX_*).
bool BindClip(Clip& c, const std::vector<std::string>& boneNames, std::string& err);

// 2026-11-25: length of the looping part (duration minus the dismount part)
inline float LoopLength(const Clip& c) { return c.dismount > 0.f && c.dismount < c.duration ? c.duration - c.dismount : c.duration; }
// evaluation that knows the dismount part: t < LoopLength -> the loop (keys up to its end, wrapping on its length when
// loop), t >= LoopLength -> the dismount part (its own keys, played once). Without a dismount part: EvalTrack / EvalPelvis.
Quat EvalClipTrack(const Clip& c, const Track& tr, float t, bool loop);
void EvalClipPelvis(const Clip& c, float t, bool loop, float out[3]);
// Local rotation of the track at time t (seconds in the clip).
Quat EvalTrack(const Track& tr, float t, float duration, bool loop);
// P6 lot 2: pelvis offset at t (0 0 0 without keys); same spline as EvalTrack
void EvalPelvis(const std::vector<PosKey>& keys, float t, float duration, bool loop, float out[3]);
// Time in the clip: Time -> elapsed (modulo duration if loop, else capped); ManualRatio -> clamp((r-r0)/(r1-r0)) * duration
float ClipTime(const Clip& c, float elapsed, float manualRatio, float cyclePhase = 0.f);   // Cycle -> phase * duration

// Writes a clip in format v1 (keys as "q", interp written, weights != 1 in "weights", ratioRange for ManualRatio only,
// unknown fields copied at the end). ParseClip(ClipToJson(c)) gives c back.
std::string ClipToJson(const Clip& c);
