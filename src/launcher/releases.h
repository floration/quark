// The launcher's release list: GitHub releases JSON -> the versions of one channel, newest first.
// Pure logic (no network, no files), so quark_tests can check it against a fixture.
#pragma once
#include <string>
#include <vector>

namespace releases {

// PRODUCTION = normal GitHub releases (tag v<VERSION>); TESTING = GitHub *prereleases* (tag test-<VERSION>-test.<N>).
enum class Channel { Production, Testing };
const char* channelName(Channel c);            // "production" / "testing" (launcher.cfg)
Channel channelFromName(const std::string& s); // anything unknown = production

// Semantic version compare: "1.17.0" > "1.17.0-test.3" > "1.17.0-test.2" > "1.16.9". A missing part counts as 0;
// prerelease identifiers compare dot by dot (numbers numerically, numbers before words). <0, 0, >0.
int versionCmp(const std::string& a, const std::string& b);
// The version a tag names: "v1.16.0" -> "1.16.0", "test-1.17.0-test.3" -> "1.17.0-test.3". Empty if no digit.
std::string versionFromTag(const std::string& tag);

struct Release {
    std::string tag, version, name, body;
    std::string url, sigUrl;   // this platform's quark-<platform>.qpak and its Ed25519 .sig (docs/SIGNING.md)
    bool prerelease = false, draft = false;
};

// Every release in a GitHub "list releases" response (a JSON array). `platform` = windows / linux / macos.
// Returns false (and nothing) if the text isn't a JSON array.
bool parse(const std::string& json, const std::string& platform, std::vector<Release>* out);
// A channel's installable releases, newest first: no drafts, prerelease flag matching the channel, a package for
// this platform AND its signature (the launcher refuses unsigned packages, so they're never offered).
std::vector<Release> forChannel(const std::vector<Release>& all, Channel c);

// Release notes for the launcher's ASCII UI font: dashes, quotes, ellipses and bullets become ASCII, any other
// non-ASCII character one '?'.
std::string asciiText(const std::string& utf8);

// What startup installs on its own. Following the newest: only a newer version than the installed one (switching
// to production while a newer test build is installed never downgrades by itself). A pinned version was picked
// by the player, so it's installed whenever it differs. Nothing installed yet: always.
bool shouldAutoInstall(const std::string& target, const std::string& installed, bool pinned);

// launcher.cfg: "channel=testing\npin=latest\n". Older launchers wrote just "pin=<version|latest>".
struct Config {
    Channel channel = Channel::Production;
    std::string pin = "latest";
    bool pinned() const { return !pin.empty() && pin != "latest"; }
};
Config parseConfig(const std::string& text);
std::string formatConfig(const Config& c);
// The index in `list` the config selects: the pinned version if it's there, else 0 (the newest).
int selectIndex(const std::vector<Release>& list, const Config& c);

} // namespace releases
