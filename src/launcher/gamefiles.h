// The files an official package put in the game folder, so an update deletes only retired game files (owner decision
// C13) and never anything else: configs, your maps, demos, screenshots, anything you made or changed.
//
// installed-files.txt in the game folder lists every file the installed package put there, with its sha256:
//   quark installed files 1
//   <sha256 hex> <path, '/' separators>
// The launcher writes it at every install (from the package it just extracted). Packages from 1.17.0 on also carry one
// (tools/pack.py), so an install made by an older launcher has the list of its package too. An install with no list
// (a package from before 1.17.0, installed by an older launcher) keeps every file: nothing is known to be retired.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gamefiles {

inline const char* const kListName = "installed-files.txt";

using List = std::map<std::string, std::string>; // path -> lowercase sha256 hex

std::string format(const List& files);
// False (and an empty list) when the text isn't a list (wrong first line).
bool parse(const std::string& text, List* out);

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
// On failure the old install is put back.
bool swapInstall(const std::filesystem::path& stage, const std::filesystem::path& dir, std::string* err,
                 SwapReport* report = nullptr);

} // namespace gamefiles
