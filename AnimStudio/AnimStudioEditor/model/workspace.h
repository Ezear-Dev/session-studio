// AnimStudio Editor -- workspace services without UI (UI redesign, Phase 1, 2026-11-25):
//   * settings file (key=value lines, unknown keys kept when one key is written)
//   * automatic backup of a clip's previous version before it is overwritten
//   * reading the game's AnimStudio.native.log: is the studio installed / hooked, did the game reload a saved clip
// Tested offline by asn_tests (tests/test_workspace.cpp).
#pragma once
#include <string>
#include <utility>
#include <vector>

// ------------------------------------------------------------------ settings
// Text format: one "key=value" per line; other lines are kept as they are.
struct Settings {
    std::vector<std::pair<std::string, std::string>> items;   // in file order
    std::string Get(const std::string& key, const std::string& def = "") const;
    float GetFloat(const std::string& key, float def) const;
    void Set(const std::string& key, const std::string& value);   // replaces the first occurrence, else appends
    void SetFloat(const std::string& key, float v);
};
Settings ParseSettings(const std::string& text);
std::string SettingsText(const Settings& s);
bool LoadSettings(const std::wstring& path, Settings& out);   // false if the file cannot be read (out left empty)
bool SaveSettings(const std::wstring& path, const Settings& s);
// read-modify-write of one key (keeps every other line)
bool SaveSetting(const std::wstring& path, const std::string& key, const std::string& value);

// ------------------------------------------------------------------ backups
// Default backup root: <Documents>\AnimStudio\backups (outside the game folder). "" if Documents is unknown.
std::wstring DefaultBackupRoot();
// "20261125_175210" from the local clock
std::string BackupStamp();
// Copies the current content of clipPath (if it exists) to <root>\<stem>\<stem>_<stamp>.sanim, then keeps only the
// `keep` newest copies of that clip. Returns false with err set on failure; true and *written = "" when there was
// nothing to back up (no file yet).
bool BackupClip(const std::wstring& clipPath, const std::wstring& root, const std::string& stamp, int keep,
                std::wstring* written, std::string& err);
std::vector<std::wstring> ListBackups(const std::wstring& root, const std::string& stem);   // oldest first

// ------------------------------------------------------------------ game folder and log
// <...>\Win64\Mods\AnimStudio\clips -> <...>\Win64 ; "" if the clips folder is not inside a Mods\AnimStudio folder
std::wstring GameBinDirFromClips(const std::wstring& clipsDir);
bool IsGameRunning();   // SessionGame-Win64-Shipping.exe is running

struct ModInstall {
    bool native = false;   // Mods\AnimStudioNative\dlls\main.dll + enabled.txt
    bool lua = false;      // Mods\AnimStudio\Scripts\main.lua + enabled.txt
    bool ue4ss = false;    // UI Phase 5: UE4SS.dll next to the game exe (the mod loader)
};
ModInstall CheckModInstall(const std::wstring& gameBinDir);

// What the last game session said (the log is rewritten at each game launch).
struct GameLogInfo {
    bool found = false;      // the log has a session header
    int version = -1;        // "AnimStudioNative v0.<n>"
    bool hooked = false;     // "installation: installed in the slot ..."
    bool refused = false;    // installation refused (game updated?)
    int studioOn = -1;       // last "studio ON/OFF" (-1 never toggled: the studio starts ON)
    std::string lastInstallLine;   // text after "installation: " (raw)
};
GameLogInfo ParseGameLog(const std::string& text);

enum class ReloadKind { None, Loaded, Refused, KeptPrevious, ReadFailed, Removed };
struct ReloadEvent {
    ReloadKind kind = ReloadKind::None;
    std::string time;      // "17:44:53" as written by the game
    std::string detail;    // the rest of the line (reason for a refusal)
};
// Last event for this clip file name in a piece of log text ("clips: <file>: ...").
ReloadEvent FindReload(const std::string& text, const std::string& file);
// Tail of a growing file: bytes from `from` (or the whole file if it is now shorter, i.e. the game restarted).
bool ReadFrom(const std::wstring& path, unsigned long long from, std::string& out, unsigned long long* sizeNow);
unsigned long long FileSizeOf(const std::wstring& path);   // 0 if missing

// ------------------------------------------------------------------ clip files (UI Phase 2: Library)
struct Clip;
// Cleans a clip file name typed by the user: trims, adds ".sanim", refuses forbidden characters and a leading "_"
// (reserved for the studio's own files). false with err in French (shown to the user).
bool NormalizeClipName(std::string& name, std::string& err);
// "<stem><suffix>.sanim", then "<stem><suffix>2.sanim"... the first that does not exist in dir
std::string UniqueClipName(const std::wstring& dir, const std::string& stem, const std::string& suffix);
// Moves a file into <root>\<stem>\<stem>_<tag>_<stamp>.sanim (recoverable delete). false with err.
bool MoveToBackups(const std::wstring& path, const std::wstring& root, const std::string& tag, const std::string& stamp,
                   std::wstring* dest, std::string& err);
// Rewrites a clip file with another "enabled" (parse -> change -> ClipToJson: nothing else changes). The previous
// version goes to the backups first. false with err.
bool SetClipEnabledFile(const std::wstring& path, bool enabled, const std::wstring& backupRoot, std::string& err);
// Copies a clip to dst under a new clip name, disabled (so it does not compete with the original in game).
bool DuplicateClipFile(const std::wstring& src, const std::wstring& dst, std::string& err);
// Renames a clip file (refuses to replace an existing file). The clip's own "name" field is left as it is.
bool RenameClipFile(const std::wstring& from, const std::wstring& to, std::string& err);
// Does this clip change anything in game? Additive clips whose every key is (almost) no rotation and whose pelvis keys
// are (almost) zero add nothing. Replace clips always do something (they impose a pose). Weight-0 tracks count as nothing.
bool ClipHasNoEffect(const Clip& c);
