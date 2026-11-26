// The clip library: reads Mods\SessionAnimLoader\clips\*.sanim (+ _studio.json), keeps the last good version of
// each file, binds the clips to the skeleton and publishes a read-only ClipSet by atomic swap.
// Scan() and SetRig(): game thread (called by the Lua). Current(): any thread (the FBT hook).
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "player.h"

// 2026-11-26 v0.21: clips of other players go in clips\others\ (sub-folders allowed, 3 levels): loaded for the
// multiplayer (found by content), never chosen for our own skater, never shown in the editor.
constexpr const wchar_t* kOthersDir = L"others";
constexpr const char* kOthersDirA = "others";

class ClipLibrary {
public:
    explicit ClipLibrary(std::wstring dir);
    ~ClipLibrary();
    ClipLibrary(const ClipLibrary&) = delete;
    ClipLibrary& operator=(const ClipLibrary&) = delete;

    // Binds the clips to the skeleton again and publishes. Returns the log lines (clips refused by this skeleton).
    std::vector<std::string> SetRig(std::vector<std::string> names);
    // Reads the folder again if a file changed (date or size), was added or removed, and publishes. Log lines.
    std::vector<std::string> Scan();
    const ClipSet* Current() const { return cur_.load(std::memory_order_acquire); }
    // Frees the ClipSets retired more than 2 s ago (no hook call can still be using them).
    void CollectRetired();
    const std::wstring& Dir() const { return dir_; }

private:
    struct Entry {
        uint64_t hash = 0;
        bool hashed = false, readFail = false;
        bool hasGood = false;
        Clip good;      // last valid version (not bound)
        bool seen = false;
        bool foreign = false;   // clips\others\...
    };
    std::vector<std::string> Publish();
    void ScanClip(const std::wstring& path, const std::string& key, bool foreign, std::vector<std::string>& log, bool& changed);
    void ScanOthers(const std::wstring& dir, const std::string& rel, int depth, std::vector<std::string>& log, bool& changed);

    std::wstring dir_;
    std::vector<std::string> names_;
    bool haveRig_ = false;
    std::map<std::string, Entry> files_;          // key: file name in UTF-8 (sorted)
    uint64_t studioStamp_ = 0, studioSize_ = 0;
    bool studioSeen_ = false;
    std::map<std::string, int> convByName_;      // _studio.json "mirrorConv"
    uint64_t mirrorStamp_ = 0;
    bool legPostPass_ = true;                     // P6 : _studio.json "legPostPass"
    uint64_t clipStamp_ = 0;                      // content version counter (Clip::stamp)
    uint64_t gen_ = 0;
    std::atomic<ClipSet*> cur_{nullptr};
    struct Retired { ClipSet* set; uint64_t at; };
    std::vector<Retired> retired_;
};
