// Launcher logic: release channels from a GitHub releases fixture, version order, the "newer launcher" decision,
// the self-swap, and the game replacing old launchers wherever they were installed (with their shortcuts).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../src/launcher/install.h"
#include "../src/launcher/releases.h"
#include "../src/launcher/selfupdate.h"
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static int g_lfail = 0;
#define LCHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_lfail++; } } while (0)

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
// A stand-in launcher binary: an ELF / PE header, filler, and (if given) the version marker.
void fakeLauncher(const fs::path& p, const std::string& version, bool pe = false) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << (pe ? std::string("MZ\x90\x00", 4) : std::string("\x7f" "ELF", 4)) << std::string(1000, 'x');
    f << std::string("QUARK-LAUNCHER-VERSION:", 23) << '\0' << "junk"; // the reader's own prefix literal: not a version
    if (!version.empty()) f << "QUARK-LAUNCHER-" << "VERSION:" << version << '\0';
    f << std::string(500, 'y');
}
std::vector<std::string> versions(const std::vector<releases::Release>& l) {
    std::vector<std::string> v;
    for (auto& r : l) v.push_back(r.version);
    return v;
}
#ifdef __linux__
struct EnvGuard { // points HOME / XDG at a temp folder, restores on exit
    std::vector<std::pair<std::string, std::string>> saved;
    std::vector<std::string> unset;
    void set(const char* k, const std::string& v) {
        if (const char* o = getenv(k)) saved.push_back({k, o}); else unset.push_back(k);
        setenv(k, v.c_str(), 1);
    }
    ~EnvGuard() {
        for (auto& s : saved) setenv(s.first.c_str(), s.second.c_str(), 1);
        for (auto& u : unset) unsetenv(u.c_str());
    }
};
#endif
} // namespace

static void testVersions() {
    using releases::versionCmp;
    LCHECK(versionCmp("1.16.0", "1.16.0") == 0);
    LCHECK(versionCmp("1.10.0", "1.9.0") > 0);           // numeric, not text
    LCHECK(versionCmp("1.16", "1.16.0") == 0);
    LCHECK(versionCmp("1.17.0", "1.17.0-test.9") > 0);   // a release beats its prereleases
    LCHECK(versionCmp("1.17.0-test.10", "1.17.0-test.9") > 0);
    LCHECK(versionCmp("1.17.0-test.1", "1.16.9") > 0);
    LCHECK(versionCmp("1.17.0-test", "1.17.0-test.1") < 0);
    LCHECK(versionCmp("1.17.0-1", "1.17.0-test") < 0);   // numbers before words
    LCHECK(versionCmp("2.0.0", "1.99.99") > 0);
    LCHECK(versionCmp("1.16.0+abc", "1.16.0") == 0);
    LCHECK(releases::versionFromTag("v1.16.0") == "1.16.0");
    LCHECK(releases::versionFromTag("test-1.17.0-test.3") == "1.17.0-test.3");
    LCHECK(releases::versionFromTag("nightly").empty());
}

static void testChannels() {
    std::string json = slurp(std::string(QUARK_TEST_DATA) + "/github_releases.json");
    LCHECK(!json.empty());
    std::vector<releases::Release> all;
    LCHECK(releases::parse(json, "linux", &all));
    LCHECK(all.size() == 7);
    using V = std::vector<std::string>;
    // production: no prereleases, no draft, no unsigned 1.9.0, newest first (1.10 above 1.9 numerically)
    LCHECK((versions(releases::forChannel(all, releases::Channel::Production)) == V{"1.16.0", "1.15.1", "1.10.0"}));
    LCHECK((versions(releases::forChannel(all, releases::Channel::Testing)) == V{"1.17.0-test.2", "1.17.0-test.1"}));
    auto test = releases::forChannel(all, releases::Channel::Testing);
    LCHECK(!test.empty() && test[0].url == "https://github.com/floration/quark/releases/download/test-1.17.0-test.2/quark-linux.qpak");
    LCHECK(!test.empty() && test[0].sigUrl == test[0].url + ".sig");
    // escapes decoded; quoted JSON inside the notes doesn't split the release
    LCHECK(!test.empty() && test[0].body.find("\"tag_name\": \"v9.9.9\"") != std::string::npos);
    LCHECK(!test.empty() && test[0].body.find("\xe2\x80\x94") != std::string::npos);      // —
    LCHECK(!test.empty() && test[0].body.find("\xf0\x9f\x9a\x80") != std::string::npos);  // surrogate pair
    LCHECK(releases::asciiText("a \xe2\x80\x94 b \xe2\x80\x9cq\xe2\x80\x9d \xf0\x9f\x9a\x80!") == "a - b \"q\" ?!");
    std::vector<releases::Release> win, mac;
    LCHECK(releases::parse(json, "windows", &win) && releases::parse(json, "macos", &mac));
    using V2 = std::vector<std::string>;
    LCHECK((versions(releases::forChannel(win, releases::Channel::Production)) == V2{"1.16.0", "1.15.1"}));
    LCHECK((versions(releases::forChannel(mac, releases::Channel::Production)) == V2{"1.16.0"}));
    LCHECK(releases::forChannel(mac, releases::Channel::Testing).empty());
    std::vector<releases::Release> none;
    LCHECK(!releases::parse("{\"message\": \"API rate limit exceeded\"}", "linux", &none) && none.empty());
    LCHECK(!releases::parse("[{\"tag_name\": \"v1\"", "linux", &none));

    // no silent downgrade: following the newest installs only newer; a pinned (picked) version installs as asked
    LCHECK(releases::shouldAutoInstall("1.16.0", "", false));
    LCHECK(releases::shouldAutoInstall("1.16.0", "1.15.1", false));
    LCHECK(!releases::shouldAutoInstall("1.16.0", "1.16.0", false));
    LCHECK(!releases::shouldAutoInstall("1.16.0", "1.17.0-test.2", false)); // back to production with a newer test build
    LCHECK(releases::shouldAutoInstall("1.15.1", "1.16.0", true));
    LCHECK(!releases::shouldAutoInstall("", "1.16.0", false));

    // launcher.cfg: old single "pin=" line still read; channel + pin round trip
    releases::Config old = releases::parseConfig("pin=1.15.1\n");
    LCHECK(old.channel == releases::Channel::Production && old.pinned() && old.pin == "1.15.1");
    LCHECK(!releases::parseConfig("pin=latest\r\n").pinned());
    releases::Config c;
    c.channel = releases::Channel::Testing;
    releases::Config back = releases::parseConfig(releases::formatConfig(c));
    LCHECK(back.channel == releases::Channel::Testing && !back.pinned());
    auto prod = releases::forChannel(all, releases::Channel::Production);
    LCHECK(releases::selectIndex(prod, old) == 1);
    LCHECK(releases::selectIndex(prod, releases::parseConfig("pin=1.2.3")) == 0); // gone: the newest
}

static void testLauncherSwap() {
    fs::path t = fs::temp_directory_path() / ("quark_launcher_test_" + std::to_string((long)getpid()));
    std::error_code ec;
    fs::remove_all(t, ec);
    fs::create_directories(t);
    // this build's marker is readable from its own binary (the launcher scans files the same way)
    LCHECK(std::string(selfupdate::ownVersion()) == QUARK_VERSION);
#ifdef __linux__
    LCHECK(selfupdate::fileVersion("/proc/self/exe") == QUARK_VERSION);
#endif
    fakeLauncher(t / "new" / "quark_launcher", "1.17.0");
    fakeLauncher(t / "old" / "quark_launcher", "");
    fakeLauncher(t / "pe.exe", "1.17.0-test.2", true);
    std::ofstream(t / "notes.txt") << "QUARK-LAUNCHER-" << "VERSION:" << "9.9.9" << '\0';
    LCHECK(selfupdate::fileVersion(t / "new" / "quark_launcher") == "1.17.0");
    LCHECK(selfupdate::fileVersion(t / "old" / "quark_launcher").empty());
    LCHECK(selfupdate::fileVersion(t / "pe.exe") == "1.17.0-test.2");
    LCHECK(selfupdate::fileVersion(t / "notes.txt").empty()); // not an executable
    LCHECK(selfupdate::fileVersion(t / "missing").empty());
    LCHECK(selfupdate::isNewer("1.17.0", ""));           // any pre-marker launcher is older
    LCHECK(selfupdate::isNewer("1.17.0", "1.17.0-test.2"));
    LCHECK(!selfupdate::isNewer("1.17.0", "1.17.0"));
    LCHECK(!selfupdate::isNewer("1.16.0", "1.17.0"));    // an older package (picked by hand) never downgrades it
    LCHECK(!selfupdate::isNewer("", ""));

    // which files count as launchers (the names every launcher version installed under)
    fs::path d = t / "appdir";
#ifdef _WIN32
    const char* names[] = {"quark_launcher.exe", "quark_launcher (1).exe", "quark-launcher-windows.exe"};
    const char* skip[] = {"quark_launcher.exe.old", "quark_launcher.exe.new", "quark.exe"};
#else
    const char* names[] = {"quark_launcher", "quark_launcher (1)", "quark-launcher-linux"};
    const char* skip[] = {"quark_launcher.old", "quark_launcher.new", "quark-launcher-macos.zip"};
#endif
    for (const char* n : names) fakeLauncher(d / n, "");
    for (const char* n : skip) fakeLauncher(d / n, "");
    std::ofstream(d / "quark_launcher_notes") << "text, not a program";
    LCHECK(selfupdate::launchersIn(d).size() == 3);

    // the swap: staged next to the target, then renamed over it; leftovers cleaned
    fs::path target = d / names[0];
    selfupdate::SwapPaths sp = selfupdate::swapPaths(target);
    LCHECK(sp.staged == d / (std::string(names[0]) + ".new") && sp.old == d / (std::string(names[0]) + ".old"));
    std::string err;
    LCHECK(selfupdate::replaceFile(t / "new" / "quark_launcher", target, &err));
    LCHECK(slurp(target) == slurp(t / "new" / "quark_launcher"));
    LCHECK(!fs::exists(sp.staged));
#ifndef _WIN32
    LCHECK((fs::status(target).permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
    std::ofstream(sp.old) << "x";
    selfupdate::removeLeftovers(target);
    LCHECK(!fs::exists(sp.old));
    LCHECK(selfupdate::replaceFile(t / "new" / "quark_launcher", d / "fresh" / "quark_launcher", &err)); // into a new folder
    LCHECK(!selfupdate::replaceFile(t / "nope", d / "x", &err) && !fs::exists(d / "x"));

    // the game's install root: version.txt in the game folder (macOS: three levels above the executable)
    fs::create_directories(t / "g" / "quark.app" / "Contents" / "MacOS");
    std::ofstream(t / "g" / "version.txt") << "1.17.0\n";
    LCHECK(selfupdate::gameRootFrom(t / "g") == t / "g");
    LCHECK(selfupdate::gameRootFrom(t / "g" / "quark.app" / "Contents" / "MacOS") == t / "g");
    LCHECK(selfupdate::gameRootFrom(t / "new").empty()); // a build folder: nothing to do

#ifdef __linux__
    { // the game updating every earlier launcher generation, in a fake home
        EnvGuard env;
        fs::path home = t / "home";
        env.set("HOME", home.string());
        env.set("XDG_DATA_HOME", (home / ".local/share").string());
        env.set("XDG_CONFIG_HOME", (home / ".config").string());
        fs::create_directories(home / "Desktop");
        fs::path app = install::appDir();
        LCHECK(app == home / ".local/share/quark");
        // 0.5 - 0.16.0: launcher installed under the download's name, menu entry pointing at it; game in appDir/game
        fakeLauncher(app / "quark-launcher-linux", "");
        fs::create_directories(app / "game");
        std::ofstream(app / "game" / "version.txt") << "1.17.0\n";
        fakeLauncher(app / "game" / "quark_launcher", "1.17.0"); // what the new package carries
        fs::create_directories(home / ".local/share/applications");
        std::ofstream(home / ".local/share/applications/quark.desktop") << "[Desktop Entry]\nExec=\"" << (app / "quark-launcher-linux").string() << "\"\n";
        // a player file an old version kept in the game folder
        std::ofstream(app / "game" / "quark.key") << "secret";
        selfupdate::Report r = selfupdate::updateLaunchersFromGame(app / "game");
        LCHECK(r.replaced == 1 && r.installedFresh);
        LCHECK(selfupdate::fileVersion(app / "quark-launcher-linux") == "1.17.0"); // old name: replaced in place
        LCHECK(selfupdate::fileVersion(app / "quark_launcher") == "1.17.0");       // installed under the fixed name
        std::string menu = slurp(home / ".local/share/applications/quark.desktop");
        LCHECK(menu.find("Exec=\"" + (app / "quark_launcher").string() + "\"") != std::string::npos);
        LCHECK(fs::exists(home / "Desktop/quark.desktop")); // a fresh install gets its desktop icon
        // second start: nothing left to do
        r = selfupdate::updateLaunchersFromGame(app / "game");
        LCHECK(r.replaced == 0 && !r.installedFresh);
        // an older package (picked by hand) leaves the newer launcher alone
        fakeLauncher(app / "game" / "quark_launcher", "1.16.5");
        r = selfupdate::updateLaunchersFromGame(app / "game");
        LCHECK(r.replaced == 0 && selfupdate::fileVersion(app / "quark_launcher") == "1.17.0");
        // 0.1.0: launcher and its game folder wherever the tarball was unpacked
        fs::path dl = home / "Downloads/quark-linux";
        fakeLauncher(dl / "quark_launcher", "");
        std::ofstream(dl / "README.md") << "readme";
        fs::create_directories(dl / "game");
        std::ofstream(dl / "game" / "version.txt") << "1.17.0\n";
        fakeLauncher(dl / "game" / "quark_launcher", "1.17.0");
        r = selfupdate::updateLaunchersFromGame(dl / "game");
        LCHECK(r.replaced == 1 && selfupdate::fileVersion(dl / "quark_launcher") == "1.17.0");
        LCHECK(slurp(dl / "README.md") == "readme");
        // uninstall: shortcuts and install gone, the player's data kept (copied out of the game folder first)
        std::ofstream(app / "quark.cfg") << "sens 2";
        install::uninstallAll();
        LCHECK(!fs::exists(app / "game") && !fs::exists(app / "quark_launcher") && !fs::exists(app / "quark-launcher-linux"));
        LCHECK(!fs::exists(home / ".local/share/applications/quark.desktop") && !fs::exists(home / "Desktop/quark.desktop"));
        LCHECK(slurp(app / "quark.cfg") == "sens 2");
        LCHECK(slurp(app / "quark.key") == "secret");
    }
#endif
    fs::remove_all(t, ec);
}

int launcherTests() {
    g_lfail = 0;
    printf("launcher\n");
    testVersions();
    testChannels();
    testLauncherSwap();
    return g_lfail;
}
