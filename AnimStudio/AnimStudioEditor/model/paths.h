// AnimStudio Editor -- find the game's Mods\AnimStudio\clips folder, and remember a manual choice.
#pragma once
#include <string>
#include <vector>

std::vector<std::wstring> SteamLibraryPaths(const std::string& libraryFoldersVdf);   // "path" values (\\ decoded)
std::wstring ClipsDirIn(const std::wstring& steamLibrary);
// 1) saved setting, if it still exists; 2) Steam libraries (registry + libraryfolders.vdf); 3) default Steam path; "" otherwise
std::wstring FindClipsDir(const std::wstring& settingsFile);
bool SaveClipsDir(const std::wstring& settingsFile, const std::wstring& dir);
bool DirExists(const std::wstring& dir);
