// DISCORD STATUS (src/client/discordrpc.*): the texts for every screen and mode, the JSON (escaping, Discord's limits),
// the score taken from the mode state, and on Linux / macOS a fake Discord (a Unix socket listener) the client talks to:
// the handshake, SET_ACTIVITY, changes merged under the rate limit, PING / PONG, reconnecting, the status cleared on
// stop. Plus `quark_tests mappreviews <dir> [size]`: every official map's preview as <map>.png (the website's img/rpc/).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "client/discordrpc.h"
#include "client/mappreview.h"
#include "game/gamedefs.h"
#include "game/map.h"
#include "game/protocol.h"
#include "official_map_data.h"
#ifndef _WIN32
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

int g_rpcFail = 0;
#define RCHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_rpcFail++; } } while (0)

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

drpc::Presence gamePresence(int mode, uint8_t kind) {
    drpc::Presence p;
    p.where = drpc::W_GAME;
    p.mode = (uint8_t)mode;
    p.scoreKind = kind;
    return p;
}

void testTexts() {
    using namespace drpc;
    // the menus: "Main Menu", what the player is doing, the game's icon, no server or party
    for (int w = 0; w < W_COUNT; w++) {
        if (w == W_GAME) continue;
        Presence p;
        p.where = (uint8_t)w;
        p.start = 1700000000;
        const Activity a = buildActivity(p);
        RCHECK(a.details == (w == W_TUTORIAL ? "Tutorial" : "Main Menu"));
        RCHECK(a.state == whereLabel(w));
        RCHECK(w == W_MENU || a.state.size() >= 2);
        RCHECK(a.largeImage == kIconUrl && a.largeText == "Quark");
        RCHECK(a.smallImage.empty() && a.smallText.empty() && a.partyMax == 0);
        RCHECK(a.start == 1700000000);
    }
    RCHECK(std::string(whereLabel(W_BROWSING)) == "Browsing servers");
    RCHECK(std::string(whereLabel(W_HOSTING)) == "Hosting a game");
    RCHECK(std::string(whereLabel(W_SETTINGS)) == "In settings");
    RCHECK(std::string(whereLabel(W_EDITOR)) == "Editing a map");
    RCHECK(std::string(whereLabel(W_DEMO)) == "Watching a demo");
    RCHECK(std::string(whereLabel(W_TRAINER)) == "Aim trainer");
    RCHECK(std::string(whereLabel(W_CONNECTING)) == "Connecting\xE2\x80\xA6");
    // every mode has a label of its own
    for (int m = 0; m < MODE_COUNT; m++) {
        const std::string l = modeLabel(m);
        RCHECK(l.size() >= 2 && l != "Playing");
        for (int k = 0; k < m; k++) RCHECK(l != modeLabel(k));
        const Activity a = buildActivity(gamePresence(m, SC_NONE));
        RCHECK(a.details == l && a.state.empty());
    }
    RCHECK(std::string(modeLabel(MODE_BOMB)) == "Bomb" && std::string(modeLabel(MODE_KOTH)) == "King of the Hill");
    // the scores
    Presence p = gamePresence(MODE_BOMB, SC_VERSUS);
    p.ours = 3; p.theirs = 2;
    RCHECK(buildActivity(p).state == "3 - 2");
    p.ranked = true;
    RCHECK(buildActivity(p).details == "Bomb \xC2\xB7 Ranked");
    p = gamePresence(MODE_FFA, SC_PLACE);
    p.place = 2; p.of = 8;
    RCHECK(buildActivity(p).state == "2nd of 8");
    for (auto [k, s] : std::vector<std::pair<int, const char*>>{{1, "1st"}, {3, "3rd"}, {4, "4th"}, {11, "11th"}, {12, "12th"}, {13, "13th"}, {21, "21st"}, {22, "22nd"}}) {
        p.place = (uint8_t)k; p.of = 30;
        RCHECK(buildActivity(p).state == std::string(s) + " of 30");
    }
    RCHECK(buildActivity(gamePresence(MODE_VS, SC_WARMUP)).state == "Warmup");
    RCHECK(buildActivity(gamePresence(MODE_ARENA, SC_SPECTATING)).state == "Spectating");
    p = gamePresence(MODE_RACE, SC_PLACE);
    p.timeTrial = true; p.place = 1; p.of = 2;
    RCHECK(buildActivity(p).details == "Race \xC2\xB7 Time Trial");
    // the map: an official one's picture, any other the icon; the name as the menus show it
    p = gamePresence(MODE_BOMB, SC_NONE);
    snprintf(p.map, sizeof p.map, "datacenter"); snprintf(p.mapTitle, sizeof p.mapTitle, "Datacenter");
    p.officialMap = true;
    Activity a = buildActivity(p);
    RCHECK(a.largeImage == std::string(kMapImageBase) + "datacenter.png" && a.largeText == "Datacenter");
    p.officialMap = false;
    RCHECK(buildActivity(p).largeImage == kIconUrl);
    snprintf(p.map, sizeof p.map, "my map?x=1"); p.officialMap = true;   // (never a name that isn't a plain file name in a URL)
    RCHECK(buildActivity(p).largeImage == kIconUrl);
    // the server: its name, "Online" (unknown or SHOW SERVER NAME off), the local games
    p = gamePresence(MODE_FFA, SC_NONE);
    RCHECK(buildActivity(p).smallText == "Online" && buildActivity(p).smallImage == kServerIconUrl);
    snprintf(p.server, sizeof p.server, "Official Dallas");
    RCHECK(buildActivity(p).smallText == "Official Dallas");
    p.showServer = false;
    RCHECK(buildActivity(p).smallText == "Online");
    p.serverKind = SV_LOCAL;
    RCHECK(buildActivity(p).smallText == "Local game");
    p.serverKind = SV_BOTS;
    RCHECK(buildActivity(p).smallText == "Vs bots");
    // the party: only with a known limit
    p.players = 5;
    RCHECK(buildActivity(p).partyMax == 0);
    p.maxPlayers = 8;
    a = buildActivity(p);
    RCHECK(a.partySize == 5 && a.partyMax == 8);
    p.players = 12;
    RCHECK(buildActivity(p).partySize == 8);
    // == sees every field
    Presence x, y;
    RCHECK(x == y);
    y.server[0] = 'a';
    RCHECK(x != y);
    y = x; y.start = 1;
    RCHECK(x != y);
}

void testScores() {
    using namespace drpc;
    auto player = [](int id, int team, int score) {
        PlayerSnap s;
        s.id = (uint8_t)id; s.score = (int16_t)score;
        s.flags = team == TEAM_SPECTATOR ? PS_SPECTATOR : team == TEAM_B ? PS_TEAM_B : 0;
        return s;
    };
    ModeState ms;
    ms.mode = MODE_BOMB; ms.phase = PHASE_LIVE; ms.scoreA = 2; ms.scoreB = 3;
    std::vector<PlayerSnap> pl = {player(0, TEAM_A, 0), player(1, TEAM_B, 0), player(2, TEAM_SPECTATOR, 0)};
    Presence p;
    scoreFromMode(ms, pl, 1, p);   // (team B: our side first)
    RCHECK(p.scoreKind == SC_VERSUS && p.ours == 3 && p.theirs == 2);
    scoreFromMode(ms, pl, 0, p);
    RCHECK(p.scoreKind == SC_VERSUS && p.ours == 2 && p.theirs == 3);
    scoreFromMode(ms, pl, 2, p);
    RCHECK(p.scoreKind == SC_SPECTATING);
    scoreFromMode(ms, pl, 9, p);   // (not in the list yet)
    RCHECK(p.scoreKind == SC_SPECTATING);
    ms.phase = PHASE_WARMUP;
    scoreFromMode(ms, pl, 0, p);
    RCHECK(p.scoreKind == SC_WARMUP);
    ms.phase = PHASE_LIVE;
    for (int m : {MODE_ARENA, MODE_DUEL, MODE_VS, MODE_KOTH, MODE_BALL}) {   // (1v1 and duel are sides too: round wins)
        ms.mode = (uint8_t)m;
        scoreFromMode(ms, pl, 1, p);
        RCHECK(p.scoreKind == SC_VERSUS && p.ours == 3 && p.theirs == 2);
    }
    ms.mode = MODE_TOURNEY;   // (own round score vs the best other)
    pl = {player(0, TEAM_A, 2), player(1, TEAM_A, 4), player(2, TEAM_A, 1), player(3, TEAM_SPECTATOR, 9)};
    scoreFromMode(ms, pl, 0, p);
    RCHECK(p.scoreKind == SC_VERSUS && p.ours == 2 && p.theirs == 4);
    for (int m : {MODE_FFA, MODE_RACE, MODE_SURF, MODE_HIDE, MODE_TAG}) {
        ms.mode = (uint8_t)m;
        scoreFromMode(ms, pl, 0, p);
        RCHECK(p.scoreKind == SC_PLACE && p.place == 2 && p.of == 3);
        scoreFromMode(ms, pl, 1, p);
        RCHECK(p.place == 1 && p.of == 3);
        std::vector<PlayerSnap> alone = {player(0, TEAM_A, 5)};
        scoreFromMode(ms, alone, 0, p);
        RCHECK(p.scoreKind == SC_NONE);
    }
}

void testJson() {
    using namespace drpc;
    RCHECK(jsonEscape("a\"b\\c\nd\te\x01" "f") == "\"a\\\"b\\\\c\\nd\\te\\u0001f\"");
    RCHECK(jsonEscape("caf\xC3\xA9") == "\"caf\xC3\xA9\"");
    RCHECK(clampText(std::string(200, 'a')).size() == kMaxText);
    RCHECK(clampText(std::string(127, 'a') + "\xC3\xA9") == std::string(127, 'a'));   // (never half a character)
    RCHECK(clampText(std::string(126, 'a') + "\xC3\xA9").size() == 128);
    RCHECK(clampText("ab\xFF" "c") == "ab?c");                 // (a bad byte)
    RCHECK(clampText("\xC0\xAF") == "??");                     // (overlong)
    RCHECK(clampText("x") == "x\xE2\xA0\x80");                 // (Discord's 2 characters)
    RCHECK(clampText("") == "");
    RCHECK(validAppId("1234567890123456789") && !validAppId("") && !validAppId("12a4") && !validAppId(std::string(33, '1')));
#ifndef _WIN32
    unsetenv("QUARK_DISCORD_APP_ID");
    RCHECK(resolveAppId("42") == "42");
    RCHECK(resolveAppId("4x2") == std::string(kDiscordAppId));
    setenv("QUARK_DISCORD_APP_ID", "777", 1);
    RCHECK(resolveAppId("42") == "777");
    setenv("QUARK_DISCORD_APP_ID", "bad", 1);
    RCHECK(resolveAppId("42") == "42");
    unsetenv("QUARK_DISCORD_APP_ID");
#endif
    // a whole activity: the user's text escaped and clamped, the fields Discord reads
    Presence p = gamePresence(MODE_BOMB, SC_VERSUS);
    p.ours = 3; p.theirs = 2; p.start = 1700000000; p.players = 5; p.maxPlayers = 8;
    snprintf(p.server, sizeof p.server, "my \"best\" \\ server");
    snprintf(p.map, sizeof p.map, "mine"); snprintf(p.mapTitle, sizeof p.mapTitle, "Mine"); p.officialMap = true;
    const Activity a = buildActivity(p);
    const std::string j = activityJson(a);
    RCHECK(has(j, "\"details\":\"Bomb\""));
    RCHECK(has(j, "\"state\":\"3 - 2\""));
    RCHECK(has(j, "\"timestamps\":{\"start\":1700000000}"));
    RCHECK(has(j, "\"large_image\":\"https://floration.github.io/quark/img/rpc/mine.png\""));
    RCHECK(has(j, "\"large_text\":\"Mine\""));
    RCHECK(has(j, "\"small_text\":\"my \\\"best\\\" \\\\ server\""));
    RCHECK(has(j, "\"party\":{\"size\":[5,8]}"));
    RCHECK(has(j, "\"buttons\":[{\"label\":\"Get Quark\",\"url\":\"https://floration.github.io/quark\"}]"));
    RCHECK(j.front() == '{' && j.back() == '}');
    Activity big = a;
    big.state = std::string(300, 'z');
    RCHECK(has(activityJson(big), "\"state\":\"" + std::string(128, 'z') + "\""));
    const std::string s = setActivityJson(&a, 1234, 7);
    RCHECK(s.rfind("{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":1234,\"activity\":{", 0) == 0 && has(s, "\"nonce\":\"7\"}"));
    RCHECK(setActivityJson(nullptr, 1234, 8) == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":1234},\"nonce\":\"8\"}");
    RCHECK(handshakeJson("42") == "{\"v\":1,\"client_id\":\"42\"}");
    const std::string f = frame(1, "{}");
    RCHECK(f.size() == 10 && f[0] == 1 && f[1] == 0 && f[4] == 2 && f[5] == 0 && f.substr(8) == "{}");
}

void testPreviewPng() {
    const OfficialMapData* d = kOfficialMapData;
    RCHECK(d->name);
    if (!d->name) return;
    Map m;
    RCHECK(m.parse(std::string((const char*)d->data, d->size)));
    const std::vector<uint8_t> img = makeMapPreview(m, 64, 64);
    RCHECK(img.size() == 64u * 64 * 4);
    const std::string png = encodePreviewPng(img, 64, 64);
    RCHECK(png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
    RCHECK(png.size() > 64u * 64 * 3 && png.size() < 64u * 64 * 3 + 64 * 2 + 200);
    RCHECK(png.substr(12, 4) == "IHDR" && (unsigned char)png[19] == 64 && (unsigned char)png[23] == 64);
    RCHECK(encodePreviewPng(img, 65, 64).empty());
}

#ifndef _WIN32
// ---- a fake Discord: a Unix socket listener in a temp folder
bool readFrame(int fd, int timeoutMs, uint32_t& op, std::string& json) {
    std::string buf;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t want = 8;
    while (buf.size() < want) {
        const int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, left) <= 0) return false;
        char c[4096];
        const ssize_t r = recv(fd, c, std::min(sizeof c, want - buf.size()), 0);
        if (r <= 0) return false;
        buf.append(c, (size_t)r);
        if (buf.size() == 8 && want == 8) {
            uint32_t n = 0;
            op = 0;
            for (int i = 0; i < 4; i++) { op |= (uint32_t)(unsigned char)buf[i] << (8 * i); n |= (uint32_t)(unsigned char)buf[4 + i] << (8 * i); }
            want = 8 + n;
        }
    }
    json = buf.substr(8);
    return true;
}

int acceptWithin(int lfd, int timeoutMs) {
    pollfd p{lfd, POLLIN, 0};
    if (poll(&p, 1, timeoutMs) <= 0) return -1;
    return accept(lfd, nullptr, nullptr);
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   // (macOS: no such flag)
#endif
void sendRaw(int fd, const std::string& s) { (void)!send(fd, s.data(), s.size(), MSG_NOSIGNAL); }

void testFakeDiscord() {
    using namespace drpc;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("quark_rpc_" + std::to_string(getpid()));
    fs::create_directories(dir);
    const std::string path = (dir / "discord-ipc-0").string();
    unlink(path.c_str());
    const int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path.c_str());
    RCHECK(lfd >= 0 && bind(lfd, (sockaddr*)&a, sizeof a) == 0 && listen(lfd, 4) == 0);
    Tuning saved = tuning();
    tuning().onlyDir = dir.string();
    tuning().sendGap = 0.6; tuning().retryGap = 0.3; tuning().pollMs = 20;

    Client c;
    c.start("123456");
    int fd = acceptWithin(lfd, 3000);
    RCHECK(fd >= 0);
    uint32_t op = 99;
    std::string j;
    RCHECK(readFrame(fd, 2000, op, j) && op == 0 && j == "{\"v\":1,\"client_id\":\"123456\"}");
    RCHECK(!readFrame(fd, 150, op, j));   // (nothing to show yet: nothing sent)
    Presence p = gamePresence(MODE_BOMB, SC_VERSUS);
    p.ours = 1;
    c.set(p);   // the first state after connecting: at once
    RCHECK(readFrame(fd, 1000, op, j) && op == 1 && has(j, "\"cmd\":\"SET_ACTIVITY\"") && has(j, "\"state\":\"1 - 0\"") && has(j, "\"pid\":"));
    // rapid changes inside the rate limit: one frame, the newest
    p.ours = 2; c.set(p);
    p.ours = 3; c.set(p);
    RCHECK(!readFrame(fd, 300, op, j));
    RCHECK(readFrame(fd, 1500, op, j) && op == 1 && has(j, "\"state\":\"3 - 0\""));
    RCHECK(!readFrame(fd, 700, op, j));   // (and nothing after it: the state didn't change)
    RCHECK(c.sends() == 2);
    // PING is answered with PONG (the same payload)
    sendRaw(fd, frame(3, "{\"x\":1}"));
    RCHECK(readFrame(fd, 1000, op, j) && op == 4 && j == "{\"x\":1}");
    // Discord gone: the client tries again (here after 0.3 s), handshakes and sends the newest state at once
    close(fd);
    fd = acceptWithin(lfd, 3000);
    RCHECK(fd >= 0);
    RCHECK(readFrame(fd, 2000, op, j) && op == 0);
    RCHECK(readFrame(fd, 1000, op, j) && op == 1 && has(j, "\"state\":\"3 - 0\""));
    RCHECK(c.connects() == 2);
    // CLOSE from Discord (a bad id): dropped, tried again later
    sendRaw(fd, frame(2, "{\"code\":4000}"));
    const int fd2 = acceptWithin(lfd, 3000);
    RCHECK(fd2 >= 0);
    if (fd >= 0) close(fd);
    fd = fd2;
    RCHECK(readFrame(fd, 2000, op, j) && op == 0);
    RCHECK(readFrame(fd, 1000, op, j) && op == 1);
    // DISCORD STATUS off / quitting: the status cleared, the connection closed, quickly
    const auto t0 = std::chrono::steady_clock::now();
    c.stop();
    RCHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(400));
    RCHECK(!c.running());
    RCHECK(readFrame(fd, 1000, op, j) && op == 1 && has(j, "\"args\":{\"pid\":") && !has(j, "activity"));
    char x;
    pollfd pf{fd, POLLIN, 0};
    RCHECK(poll(&pf, 1, 1000) == 1 && recv(fd, &x, 1, 0) == 0);   // (closed)
    // no Discord at all: nothing happens, stop is still quick
    if (fd >= 0) close(fd);
    close(lfd);
    unlink(path.c_str());
    Client none;
    none.start("123456");
    none.set(p);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    RCHECK(none.connects() == 0);
    none.stop();
    Client noId;
    noId.start("");   // (no application id: no thread)
    RCHECK(!noId.running());
    tuning() = saved;
    std::error_code ec;
    fs::remove_all(dir, ec);
}
#endif

}   // namespace

int discordRpcTests() {
    g_rpcFail = 0;
    testTexts();
    testScores();
    testJson();
    testPreviewPng();
#ifndef _WIN32
    testFakeDiscord();
#endif
    printf("discordrpc: %s\n", g_rpcFail ? "FAILED" : "ok");
    return g_rpcFail;
}

// quark_tests mappreviews <dir> [size]: every official map's top-down preview, <dir>/<map>.png (size x size, RGB;
// stored deflate: tools/png_squeeze.py compresses them for the website). Returns the failures.
int exportMapPreviews(const char* dir, int size) {
    namespace fs = std::filesystem;
    if (size < 16 || size > 2048) size = 512;
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    int n = 0, bad = 0;
    for (const OfficialMapData* d = kOfficialMapData; d->name; d++) {
        Map m;
        std::string err;
        if (!m.parse(std::string((const char*)d->data, d->size), &err)) { printf("mappreviews: %s: %s\n", d->name, err.c_str()); bad++; continue; }
        const std::string png = encodePreviewPng(makeMapPreview(m, size, size), size, size);
        std::ofstream f(fs::u8path(dir) / (std::string(d->name) + ".png"), std::ios::binary);
        f.write(png.data(), (std::streamsize)png.size());
        if (!f) { printf("mappreviews: can't write %s.png\n", d->name); bad++; continue; }
        n++;
    }
    printf("mappreviews: %d maps (%d x %d) -> %s\n", n, size, size, dir);
    return bad;
}
