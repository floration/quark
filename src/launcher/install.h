// Desktop integration for the launcher: install location, shortcuts, "installed programs" entry, uninstall.
#pragma once
#include <filesystem>
#include <string>

namespace install {

// Per-user install folder: %LOCALAPPDATA%\quark, ~/Library/Application Support/quark, ~/.local/share/quark.
std::filesystem::path appDir();
inline std::filesystem::path gameDir() { return appDir() / "game"; }

// Copies the running launcher into appDir() (once), so shortcuts don't depend on the download location.
// Returns the installed launcher path.
std::filesystem::path installSelf(const char* argv0);
// True if `self` is not the installed copy (e.g. run from Downloads).
bool runningFromOutside(const char* argv0);
// Starts the installed launcher, asking it to delete the copy we were started from.
bool relaunchInstalled(const std::filesystem::path& installed, const std::filesystem::path& original);
// Called by the installed launcher: deletes the downloaded copy once that process has exited.
void cleanupDownloaded(const std::filesystem::path& original);
std::filesystem::path selfPath(const char* argv0);

// What the platform calls each integration (empty = not offered on this platform).
const char* desktopLabel();   // "DESKTOP ICON"
const char* menuLabel();      // "START MENU" / "APP MENU" / "APPLICATIONS FOLDER"
const char* programsLabel();  // "INSTALLED PROGRAMS" (Windows only)

bool hasDesktop();
bool hasMenu();
bool hasPrograms();
bool setDesktop(bool on, const std::filesystem::path& launcher);
bool setMenu(bool on, const std::filesystem::path& launcher);
bool setPrograms(bool on, const std::filesystem::path& launcher, const std::string& version);

// Removes shortcuts, the programs entry, and the install folder (the running launcher is removed
// right after it exits).
void uninstallAll();

} // namespace install
