#pragma once
// Shareable UI / control profiles: .cfg files in huds/ and controls/ next to quark.cfg. Import takes the
// clipboard when it holds a valid profile, otherwise a file from the folder; export writes both.
#include <string>
#include <vector>
#include "binds.h"

std::string profileDir(const char* sub);                     // creates it if missing
std::vector<std::string> listProfiles(const char* sub);      // *.cfg file names, sorted
std::string readTextFile(const std::string& path);
bool writeTextFile(const std::string& path, const std::string& text);
std::string stampedName(const char* prefix);                 // prefix-YYYYMMDD-HHMMSS.cfg

std::string controlsToText(const Binds& b, int socd, bool altToggle);
bool controlsFromText(const std::string& text, Binds& b, int& socd, bool& altToggle); // false: no bind lines
