// The clip library: reads Mods\AnimStudio\clips\*.sanim (+ _studio.json), keeps the last good version of
// each file, binds the clips to the skeleton and publishes a read-only ClipSet by atomic swap.
// Scan() and SetRig(): game thread (called by the Lua). Current(): any thread (the FBT hook).
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "player.h"

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
    };
    std::vector<std::string> Publish();

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
