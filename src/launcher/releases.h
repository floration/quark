// The launcher's release list: GitHub releases JSON -> the versions of one channel, newest first.
// Pure logic (no network, no files), so quark_tests can check it against a fixture.
#pragma once
#include <functional>
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
    std::string asset;         // that package's name, "quark-<platform>.qpak"
    // The release's signed manifest (quark-release.txt + .sig; a testing build's macOS package, added after the
    // release went out, comes with its own quark-release-macos.txt): names this release and every package's sha256.
    std::string manifestUrl, manifestSigUrl;
    bool prerelease = false, draft = false;
    // Every release from 1.17 on is signed (owner decision B6). The ones before signing (<= 1.16.x) have no .sig:
    // listed and installable like any other version (no warning), but never the automatic target (LATEST).
    bool isSigned() const { return !sigUrl.empty(); }
};

// The signed release manifest (release.yml writes it, sign_release.sh signs it with the release key):
//   quark release / channel <production|testing> / tag <tag> / version <version> / commit <sha> /
//   sha256 <package name> <hex>   (one line per package)
// The package's own .sig only says the release key signed *some* package; the manifest ties it to this release:
// checked before installing, so a validly signed package can't be passed off as another version or channel.
struct Manifest {
    std::string channel, tag, version, commit;
    std::vector<std::pair<std::string, std::string>> sha256; // package name -> lowercase hex
};
// False if the text isn't a manifest (no "quark release" first line).
bool parseManifest(const std::string& text, Manifest* m);
// Whether a signed manifest vouches for installing package `packageSha256` (hex) as release `r`: right tag, version,
// channel (a prerelease = testing) and that package's hash. Empty: it does; else the reason it doesn't.
std::string checkManifest(const Manifest& m, const Release& r, const std::string& packageSha256);
// The whole check: `sigHex` is a release-key (`pubHex`) signature of `text`, which is a manifest that vouches for
// the package as above. Empty: install it; else why not.
std::string verifyManifest(const std::string& text, const std::string& sigHex, const std::string& pubHex, const Release& r,
                           const std::string& packageSha256);

// Every release in a GitHub "list releases" response (a JSON array). `platform` = windows / linux / macos.
// Returns false (and nothing) if the text isn't a JSON array.
bool parse(const std::string& json, const std::string& platform, std::vector<Release>* out);
// The whole release list, page by page: `getPage(n, &json, &err)` fetches page n (1, 2, ...) of kPageSize releases.
// Reads every page until one is short (no cap: production releases can sit behind any number of test builds), or
// one only repeats releases already read (a server ignoring the page parameter). False only if page 1 fails;
// `complete` is false when a later page failed or looped, so the list may be missing older releases.
const int kPageSize = 100;
using PageGetter = std::function<bool(int page, std::string* json, std::string* err)>;
bool fetchAll(const PageGetter& getPage, const std::string& platform, std::vector<Release>* out, bool* complete, std::string* err);

// A channel's installable releases, newest first: no drafts, prerelease flag matching the channel, a package for
// this platform. Signed ones, and unsigned ones from before signing (older than kFirstSigned: an unsigned release
// claiming a later version can't be genuine, so it's never offered); for a version listed twice the signed one wins.
extern const char* const kFirstSigned; // "1.17.0-0": below every 1.17.0 build, above every 1.16.x
// Whether installing `r` needs its signature and signed manifest to verify: every version from kFirstSigned on.
bool needsSignature(const Release& r);
std::vector<Release> forChannel(const std::vector<Release>& all, Channel c);
// The row "LATEST" stands for: the newest signed release (an unsigned one is never the automatic target). -1: none.
int latestIndex(const std::vector<Release>& list);

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
// The index in `list` the config selects: the pinned version if it's there, else latestIndex (0 if nothing is signed).
int selectIndex(const std::vector<Release>& list, const Config& c);

// What the launcher does at startup with a channel's list, its config and the installed version.
struct Plan {
    int index = -1;       // the selected row (-1: the list is empty)
    bool pinned = false;  // the row is the pinned version
    bool install = false; // install it now (the rules above say so)
    bool offer = false;   // the pinned version isn't installed, but only the player's INSTALL installs it
};
// A pinned version that's listed stays pinned, signed or not (an old launcher.cfg pinned to a pre-signing version
// keeps it: never silently moved to the newest). A pinned version from before signing installs on its own only when
// nothing or an older version is installed: it never replaces a newer one by itself (the player presses INSTALL).
// A pin that isn't listed counts as following the newest, unless the list is incomplete (`complete` false: it may
// just be on a page that failed), then nothing is installed.
Plan startupPlan(const std::vector<Release>& list, const Config& c, const std::string& installed, bool complete);

} // namespace releases
