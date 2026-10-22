// Keeping the launcher itself current. The launcher binary ships inside every signed update package
// (game/quark_launcher[.exe]), so whoever installs a package - this launcher, or any launcher back to 0.1.0 -
// also delivers the next launcher. Two places put it in use:
//  - the launcher, after installing (or finding) a package with a newer launcher: swaps itself and relaunches;
//  - the game, at startup: replaces every older launcher it can find (wherever any launcher version installed
//    itself) and points the shortcuts at the installed one. So nobody ever downloads a launcher by hand again.
#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace selfupdate {

// This build's launcher version (QUARK_VERSION). The same text sits in the binary behind a marker, so the version
// of a launcher file can be read without running it.
const char* ownVersion();
// The version a launcher file was built as, read from its marker. "" for launchers from before the marker
// (<= 1.16.x: all older than any launcher that has one), and for anything that isn't an executable.
std::string fileVersion(const std::filesystem::path& file);
// True when `packaged` (a marker version) should replace a launcher of version `installed` ("" = pre-marker).
bool isNewer(const std::string& packaged, const std::string& installed);

// The launcher's file name inside a package and in the install folder.
const char* exeName(); // quark_launcher.exe / quark_launcher

// Every launcher copy in `dir` (not recursive): quark_launcher*, quark-launcher* (the release asset names old
// launchers installed under, e.g. "quark-launcher-windows (1).exe"), executables only, never *.old / *.new.
std::vector<std::filesystem::path> launchersIn(const std::filesystem::path& dir);

// Where a swap of `target` stages the new file and parks the old one.
struct SwapPaths { std::filesystem::path staged, old; };
SwapPaths swapPaths(const std::filesystem::path& target);

// Lets the game quit in the middle of updating launchers without leaving one half swapped: each step that changes
// a file the player runs (the rename pair of a swap, the shortcuts, the programs entry) runs under `mu` and only
// if `stop` isn't set. Once shutdown has set `stop` and taken `mu` once, no step is running and none will start.
struct ShutdownGate {
    std::timed_mutex mu;
    std::atomic<bool> stop{false};
};
// Puts a copy of `newFile` at `target`, safely while `target` may be running: the copy is staged next to it first;
// POSIX renames it over the target (atomic), Windows renames the target to .old (allowed for a running exe), then the
// staged copy into place (the old file is put back if that fails). The .old is removed now or by removeLeftovers.
// With a `gate`: the copy is staged first (slow, harmless), then the renames run under gate->mu, and not at all once
// gate->stop is set (the staged copy is removed; false, err "shutting down").
bool replaceFile(const std::filesystem::path& newFile, const std::filesystem::path& target, std::string* err,
                 ShutdownGate* gate = nullptr);
// Deletes the .old / .new a previous swap of `target` left (call at launcher start).
void removeLeftovers(const std::filesystem::path& target);

// The game's install root: the folder holding version.txt (written by every launcher into its game folder), found
// from the game executable's folder upwards (macOS: quark.app/Contents/MacOS). Empty when the game isn't run from a
// launcher install (a build folder, an unpacked zip), and then nothing below happens.
std::filesystem::path gameRootFrom(const std::filesystem::path& exeDir);

struct Report { int replaced = 0; bool installedFresh = false; std::vector<std::string> notes; };
// The game's side (runs on a background thread at startup): with the launcher packaged in `gameRoot`, replace every
// older launcher - in the folder above the game (where each launcher keeps its game), the install folder, and the
// pre-0.12 Windows folder - install it if none is installed, and repoint the desktop / menu shortcuts.
// `gate`: stop early (between steps, never inside one) when the game shuts down.
Report updateLaunchersFromGame(const std::filesystem::path& gameRoot, ShutdownGate* gate = nullptr);
// Starts it from the game on a background thread; QUARK_NO_LAUNCHER_UPDATE=1 turns it off. Not for automated runs
// (--screenshot / --bench / --edscript ...): the caller skips it there.
void startFromGame(const char* argv0);
// At game shutdown: no new step starts, the one running (a rename pair takes microseconds) finishes, and the thread
// ends and is joined, all within `maxWaitSeconds`. If it's still busy then (a stalled disk or shortcut call) it is
// left to the process exit. Returns true if the thread ended (or never ran).
bool finishFromGame(double maxWaitSeconds);

} // namespace selfupdate
