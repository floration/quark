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
// The installed launcher always has this fixed name, whatever the download was called
// ("quark_launcher (1).exe" etc.), so re-downloads replace it instead of piling up copies.
std::filesystem::path installedLauncher();
// Only one launcher at a time (a second start just exits). Returns false if another is running.
bool acquireSingleInstance(bool waitForPrevious); // wait: a hand-over waits up to 5 s for its parent to exit

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

// The game's per-user data (settings, ranked key, demos, your maps...): %APPDATA%\quark, ~/Library/Application
// Support/quark, $XDG_DATA_HOME/quark. Must match userDataPath() in src/game/map.cpp. On macOS / Linux it is the same
// folder as appDir(), so uninstalling removes only the files the launcher put there.
std::filesystem::path userDataDir();

// Removes shortcuts, the programs entry, and the install (the running launcher is removed right after it exits).
// The player's data is kept unless removeUserData.
void uninstallAll(bool removeUserData);

} // namespace install
