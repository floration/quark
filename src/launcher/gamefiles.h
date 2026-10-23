// The files an official package put in the game folder, so an update deletes only retired game files (owner decision
// C13) and never anything else: configs, your maps, demos, screenshots, anything you made or changed. The same list
// lets the launcher check an install: the stage before it is swapped in, and the game files before every start.
//
// installed-files.txt in the game folder lists every file the installed package put there, with its sha256 and size:
//   quark installed files 2
//   <sha256 hex> <size> <path, '/' separators>
// The launcher writes it at every install (from the package it just extracted). Packages from 1.17.0 on also carry one
// in the first format (tools/pack.py: "quark installed files 1", lines "<sha256 hex> <path>", no sizes), so an install
// made by an older launcher has the list of its package too; both formats are read. An install with no list (a package
// from before 1.17.0, installed by an older launcher) keeps every file: nothing is known to be retired.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gamefiles {

inline const char* const kListName = "installed-files.txt";

// The game executable inside the game folder (the file the launcher starts; macOS: inside the app bundle).
#if defined(_WIN32)
inline const char* const kGameExe = "quark.exe";
#elif defined(__APPLE__)
inline const char* const kGameExe = "quark.app/Contents/MacOS/quark";
#else
inline const char* const kGameExe = "quark";
#endif

using List = std::map<std::string, std::string>; // path -> lowercase sha256 hex
using Sizes = std::map<std::string, uint64_t>;   // path -> size in bytes

// With `sizes` (one for every file): the current format 2; without: format 1 (what tools/pack.py writes).
std::string format(const List& files, const Sizes* sizes = nullptr);
// False (and an empty list) when the text isn't a list (wrong first line). `sizes` (optional): the sizes a format 2
// list has (none for format 1).
bool parse(const std::string& text, List* out, Sizes* sizes = nullptr);
// Reads `dir`/installed-files.txt. False (empty lists) when there is none or it isn't a list.
bool readList(const std::filesystem::path& dir, List* out, Sizes* sizes = nullptr);

// Files no update ever deletes, whatever a list says: configs and keys (*.cfg, *.key), demos, screenshots, logs, light
// caches, downloaded maps, and the folders the game keeps the player's things in (demos/, screenshots/, groups/,
// custom_modes/, huds/, controls/, logs/, maps/downloads/).
bool isPlayerFile(const std::string& rel);

struct SwapReport {
    std::vector<std::string> retired; // old game files the new package doesn't carry: left behind (deleted)
    int kept = 0;                     // other files of the old install carried into the new one
};

// Swaps a fully extracted `stage` into `dir`. Every file of the old install the new package doesn't carry is copied
// into the stage first (quark.cfg always wins over a packaged one), except a retired game file: one the old list
// names, unchanged since (same sha256), and not a player file. Then dir -> dir.old, stage -> dir, dir.old removed.
// On failure the old install is put back. A file of the old install that has to be carried over but can't be copied
// (locked, unreadable) stops the swap before anything changes, and err names it. The renames are retried for a few
// seconds (an antivirus scanning the new files holds them open for a moment); if the new version can't be moved into
// place and the old one can't be put back either, the old one stays in <dir>.old and recoverSwap puts it back.
bool swapInstall(const std::filesystem::path& stage, const std::filesystem::path& dir, std::string* err,
                 SwapReport* report = nullptr);
// A swap cut off between its two renames (power loss, or both renames refused): `dir` is missing but <dir>.old is
// there. Puts it back (call at launcher start); true when it did.
bool recoverSwap(const std::filesystem::path& dir);

// ---- checking files ----------------------------------------------------------------------------------------------
// An antivirus false positive is the one way a game file disappears that the launcher can't prevent: an unsigned new
// build gets quarantined (deleted) right after it is written, or later. The launcher checks at two points: every file
// of a new version in the stage before the swap (full sha256; reading a file back also makes an on-access scanner
// decide on it now), and the installed files before every start (existence and size: cheap), fully on demand.
enum class Problem {
    Missing, // not there (or not a file)
    Changed, // a different size or sha256 than the package's
    Blocked, // there but can't be read: Windows refuses to open a file its antivirus flagged (ERROR_VIRUS_INFECTED)
};
struct Issue {
    std::string path;
    Problem problem;
};
// Checks the files of `list` under `root`. full: every file's sha256 and size; else existence and size (where `sizes`
// has it). skipPlayerFiles: leaves out isPlayerFile ones (an install keeps the player's quark.cfg over a packaged one).
std::vector<Issue> verify(const std::filesystem::path& root, const List& list, const Sizes* sizes, bool full,
                          bool skipPlayerFiles);
// "quark.exe is missing" / "quark.exe was changed" / "quark.exe is blocked" (the file's name, not its path).
std::string describe(const Issue& issue);

struct Health {
    bool hasList = false;      // the install has an installed-files list (without one only the exe is checked)
    size_t checked = 0;        // files checked
    std::vector<Issue> issues; // every problem found, the exe's included
    bool exeBad = false;       // the game executable is one of them: the game can't be started
    Issue exe;                 // (its problem, when exeBad)
};
// The installed game in `dir`: the files its list names (player files left out), and always `exe` (relative path,
// e.g. kGameExe), which must exist even when there is no list. full: sha256 too (else existence and size).
Health checkInstall(const std::filesystem::path& dir, const std::string& exe, bool full);

// What the player is told when a game file vanished, was changed or is blocked: most likely an antivirus false
// positive, how to restore the file and how to keep it from happening again (an exclusion for `installRoot`, the
// folder holding the game). Paragraphs separated by '\n'; platform specific (Windows Security's menus on Windows).
std::string antivirusHelp(const std::string& file, const std::filesystem::path& installRoot);

// ---- installing ----------------------------------------------------------------------------------------------------
struct InstallReport {
    std::vector<Issue> stageIssues; // files of the stage that didn't match the package: nothing was swapped
    SwapReport swap;
};
// Called with the stage once the package is extracted, before it is checked (tests: an antivirus taking a file).
using StageHook = std::function<void(const std::filesystem::path& stage)>;
// The stage folder an install of `dir` extracts into (<dir>.new).
std::filesystem::path stagePath(const std::filesystem::path& dir);
// Installs package `pak` into `dir`: extracts it into the stage, checks every extracted file against the package
// (size and sha256, read back from disk), writes installed-files.txt, and swaps the stage in (swapInstall). On any
// failure the installed version stays as it was and the stage is removed: err says why, report->stageIssues lists
// the files that didn't match.
bool installPackage(const std::filesystem::path& pak, const std::filesystem::path& dir, std::string* err,
                    InstallReport* report = nullptr, const StageHook& afterExtract = {});

} // namespace gamefiles
