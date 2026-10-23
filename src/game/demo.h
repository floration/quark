// Demo files (.qdemo, written by NetClient's recorder) across versions (owner decision C13): a demo is stamped with the
// network protocol it was recorded with, and playback only reads demos of this build's protocol. When an update moves
// the protocol on, the demos recorded before are converted to the current one (at the game's first start after the
// update, on a background thread), each original kept under demos/old/<version>/. A demo that can't be converted stays
// as it is (logged in demos/old/convert.log) and the demo list says which version recorded it instead of failing.
//
// File: "QDEM", u16 protocol, then records: u8 type, f64 seconds since the start, u32 length, payload.
//   type 1 (map): name, source text, client id. Written first and whenever the map changes (or is edited).
//   type 2 (snapshot): client header + world (delta against the previous recorded snapshot) + events.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace demo {

constexpr uint32_t kMagic = 0x4D454451;        // "QDEM"
constexpr uint64_t kMaxBytes = 512ull << 20;   // (about 9 h of play; playback reads the whole file)

// The oldest protocol whose records today's reader reads as they are. Every protocol since the first demos (24, game
// 1.11.0) only added things a record of an older one simply doesn't have: P_KEYS (27), event types and KILL_ flags
// appended at the end, stricter range checks (28-33). So those demos convert by restamping them after checking every
// record reads. When a protocol changes what a record holds, raise this to it and add a step to convert() that
// rewrites the older records (or leave them: they then show "recorded with version X").
constexpr uint16_t kRecordFormatSince = 24;

// The game version(s) that recorded with `protocol`, for the demo list and the backup folder: from the built-in table
// for the versions before 1.17 (24: "1.11.0", 26: "1.12", 27: "1.13-1.16"), else from what this game has seen itself
// (noteVersion), else "protocol N".
std::string versionForProtocol(uint16_t protocol);
// "version 1.13-1.16", or "an unknown version (protocol N)": what the demo list and errors say recorded a demo.
std::string recordedWith(uint16_t protocol);
// Records that this game (version, protocol) ran, in `file` (user data versions.txt), so a later version can name the
// version that recorded an old demo. Loaded by startUpgrade; versionForProtocol uses it.
void noteVersion(const std::string& file, const std::string& version, uint16_t protocol);
void loadVersions(const std::string& file);

// The protocol a demo was recorded with (its header). False if it isn't a readable demo.
bool readProtocol(const std::string& path, uint16_t* protocol);
// Whether convert() can bring a demo of `protocol` up to the current one.
bool convertible(uint16_t protocol);

// Converts a whole demo file's bytes (`in`, of a convertible older protocol) to the current protocol: every record is
// read the way playback reads it, the first map must load in this version, at least one snapshot must read. False
// (err = why) if it doesn't convert.
bool convert(const std::string& in, std::string* out, std::string* err);

struct UpgradeReport {
    int converted = 0, failed = 0, newer = 0;
    std::vector<std::string> log;
};
// Every .qdemo directly in `dir` recorded with another protocol: a convertible one is converted in place (the original
// moved, by a hard link when it can, to dir/old/<versionForProtocol>/), any other one is left as it is. Each outcome is
// appended to dir/old/convert.log; a demo that failed once (same name and size) isn't tried again. `stop` (may be null)
// ends it between files.
UpgradeReport upgradeDir(const std::string& dir, const std::atomic<bool>* stop = nullptr);

// The game's side: notes this version, then runs upgradeDir(demoDir) on a background thread. finishUpgrade (at exit)
// lets the file being converted finish and joins, waiting at most `maxWaitSeconds`.
void startUpgrade(const std::string& demoDir, const std::string& versionsFile);
bool upgradeRunning();
bool finishUpgrade(double maxWaitSeconds);

} // namespace demo
