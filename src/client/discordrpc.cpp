// Discord status (discordrpc.h): the texts, the JSON, and the IPC thread.
#include "discordrpc.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include "../game/gamedefs.h"
#include "../game/gamemodes.h"
#include "../game/protocol.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif

namespace drpc {

bool validAppId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id) if (c < '0' || c > '9') return false;
    return true;
}

std::string resolveAppId(const std::string& cfgValue) {
    if (const char* e = getenv("QUARK_DISCORD_APP_ID"); e && validAppId(e)) return e;
    if (validAppId(cfgValue)) return cfgValue;
    return validAppId(kDiscordAppId) ? std::string(kDiscordAppId) : std::string();
}

bool operator==(const Presence& a, const Presence& b) {
    return a.where == b.where && a.mode == b.mode && a.scoreKind == b.scoreKind && a.serverKind == b.serverKind && a.ranked == b.ranked &&
           a.timeTrial == b.timeTrial && a.officialMap == b.officialMap && a.showServer == b.showServer && a.ours == b.ours && a.theirs == b.theirs &&
           a.place == b.place && a.of == b.of && a.players == b.players && a.maxPlayers == b.maxPlayers && a.start == b.start &&
           strcmp(a.map, b.map) == 0 && strcmp(a.mapTitle, b.mapTitle) == 0 && strcmp(a.server, b.server) == 0;
}

void scoreFromMode(const ModeState& ms, const std::vector<PlayerSnap>& players, int me, Presence& p) {
    p.ours = p.theirs = 0; p.place = p.of = 0;
    const PlayerSnap* mep = nullptr;
    for (const PlayerSnap& q : players) if (q.id == me) mep = &q;
    if (!mep || (mep->flags & PS_SPECTATOR)) { p.scoreKind = SC_SPECTATING; return; }
    if (ms.phase == PHASE_WARMUP) { p.scoreKind = SC_WARMUP; return; }
    if (modeIsTeamBased(ms.mode)) {   // (team modes, 1v1 and duel: rounds or points per side)
        const bool a = mep->team() == TEAM_A;
        p.ours = a ? ms.scoreA : ms.scoreB; p.theirs = a ? ms.scoreB : ms.scoreA;
        p.scoreKind = SC_VERSUS;
        return;
    }
    if (ms.mode == MODE_TOURNEY) {   // (yours vs the best other's, as the HUD's score bar)
        int best = 0;
        for (const PlayerSnap& q : players) if (q.id != me && !(q.flags & PS_SPECTATOR)) best = std::max<int>(best, q.score);
        p.ours = mep->score; p.theirs = (int16_t)best;
        p.scoreKind = SC_VERSUS;
        return;
    }
    int n = 0, above = 0;   // (free for all, race, surf, hide and seek, tag: the place, as the HUD's ffaPlace)
    for (const PlayerSnap& q : players) {
        if (q.flags & PS_SPECTATOR) continue;
        n++;
        if (q.id != me && q.score > mep->score) above++;
    }
    if (n < 2) { p.scoreKind = SC_NONE; return; }
    p.place = (uint8_t)std::min(above + 1, 255); p.of = (uint8_t)std::min(n, 255);
    p.scoreKind = SC_PLACE;
}

const char* modeLabel(int m) {
    static const char* n[] = {"VS", "Bomb", "Race", "King of the Hill", "Payload", "Free for All", "Neutral Payload", "Duel", "1v1", "Tourney",
                              "Hide and Seek", "Tag", "Multi Point", "Surf", "Power", "Winner Stays", "Ball"};
    static_assert(sizeof n / sizeof *n == MODE_COUNT, "a Discord label per mode");
    return m >= 0 && m < MODE_COUNT ? n[m] : "Playing";
}

const char* whereLabel(int w) {
    static const char* n[] = {"", "Browsing servers", "Hosting a game", "Setting up a bot match", "In settings", "Editing a map", "Watching a demo",
                              "Aim trainer", "Connecting\xE2\x80\xA6", "Viewing the leaderboard", "Making a custom mode", "Reading the info pages",
                              "Browsing demos", "Running a benchmark", "Learning the movement", ""};
    static_assert(sizeof n / sizeof *n == W_COUNT, "a label per Where");
    return w >= 0 && w < W_COUNT ? n[w] : "";
}

static const char* ordinal(int k) {
    const int t = k % 100;
    if (t >= 11 && t <= 13) return "th";
    return k % 10 == 1 ? "st" : k % 10 == 2 ? "nd" : k % 10 == 3 ? "rd" : "th";
}

static bool safeMapFile(const char* s) {   // (only these go into a URL)
    if (!*s) return false;
    for (const char* c = s; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return false;
    return true;
}

Activity buildActivity(const Presence& p) {
    Activity a;
    char b[160];
    const bool game = p.where == W_GAME;
    if (game) {
        a.details = modeLabel(p.mode);
        if (p.timeTrial) a.details += " \xC2\xB7 Time Trial";
        if (p.ranked) a.details += " \xC2\xB7 Ranked";
        switch (p.scoreKind) {
            case SC_VERSUS: snprintf(b, sizeof b, "%d - %d", p.ours, p.theirs); a.state = b; break;
            case SC_PLACE: snprintf(b, sizeof b, "%d%s of %d", p.place, ordinal(p.place), p.of); a.state = b; break;
            case SC_WARMUP: a.state = "Warmup"; break;
            case SC_SPECTATING: a.state = "Spectating"; break;
            default: break;
        }
    } else {
        a.details = p.where == W_TUTORIAL ? "Tutorial" : "Main Menu";
        a.state = whereLabel(p.where);
    }
    if (p.map[0]) {
        a.largeImage = p.officialMap && safeMapFile(p.map) ? std::string(kMapImageBase) + p.map + ".png" : std::string(kIconUrl);
        a.largeText = p.mapTitle[0] ? p.mapTitle : p.map;
    } else {
        a.largeImage = kIconUrl;
        a.largeText = "Quark";
    }
    if (game) {
        a.smallImage = kServerIconUrl;
        a.smallText = p.serverKind == SV_LOCAL ? "Local game" : p.serverKind == SV_BOTS ? "Vs bots" : p.showServer && p.server[0] ? p.server : "Online";
        if (p.maxPlayers > 0 && p.players > 0) { a.partyMax = p.maxPlayers; a.partySize = std::min(p.players, p.maxPlayers); }
    }
    a.start = p.start;
    return a;
}

std::string clampText(const std::string& s, size_t max) {
    std::string o;
    o.reserve(std::min(s.size(), max) + 3);
    size_t chars = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; k++) ok = ((unsigned char)s[i + k] >> 6) == 2;
        if (ok && n == 2) ok = c >= 0xC2;                                                    // (overlong)
        if (ok && n == 3) { const unsigned char d = (unsigned char)s[i + 1]; ok = !(c == 0xE0 && d < 0xA0) && !(c == 0xED && d >= 0xA0); }   // (overlong, surrogates)
        if (ok && n == 4) { const unsigned char d = (unsigned char)s[i + 1]; ok = c <= 0xF4 && !(c == 0xF0 && d < 0x90) && !(c == 0xF4 && d >= 0x90); }
        if (!ok) n = 1;
        if (o.size() + (ok ? n : 1) > max) break;
        if (ok) o.append(s, i, n); else o += '?';
        i += n;
        chars++;
    }
    if (chars == 1) o += "\xE2\xA0\x80";   // (U+2800: Discord wants at least 2 characters)
    return o;
}

std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F) { char u[8]; snprintf(u, sizeof u, "\\u%04x", c); o += u; }
                else o += (char)c;
        }
    }
    return o + "\"";
}

std::string activityJson(const Activity& a) {
    std::string j = "{";
    bool first = true;
    auto field = [&](const char* k, const std::string& v, std::string& into, bool& f) {
        const std::string t = clampText(v);
        if (t.empty()) return;
        into += f ? "" : ",";
        into += "\""; into += k; into += "\":"; into += jsonEscape(t);
        f = false;
    };
    field("details", a.details, j, first);
    field("state", a.state, j, first);
    if (a.start > 0) { j += first ? "" : ","; j += "\"timestamps\":{\"start\":" + std::to_string((long long)a.start) + "}"; first = false; }
    std::string as;
    bool af = true;
    field("large_image", a.largeImage, as, af);   // (URLs: well under the limit)
    field("large_text", a.largeText, as, af);
    field("small_image", a.smallImage, as, af);
    field("small_text", a.smallText, as, af);
    if (!as.empty()) { j += first ? "" : ","; j += "\"assets\":{" + as + "}"; first = false; }
    if (a.partyMax > 0 && a.partySize > 0) {
        j += first ? "" : ",";
        j += "\"party\":{\"size\":[" + std::to_string(a.partySize) + "," + std::to_string(a.partyMax) + "]}";
        first = false;
    }
    j += first ? "" : ",";
    j += "\"buttons\":[{\"label\":\"Get Quark\",\"url\":" + jsonEscape(kSiteUrl) + "}],\"instance\":false}";
    return j;
}

std::string setActivityJson(const Activity* a, long pid, uint64_t nonce) {
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + (a ? ",\"activity\":" + activityJson(*a) : std::string()) +
           "},\"nonce\":\"" + std::to_string((unsigned long long)nonce) + "\"}";
}

std::string handshakeJson(const std::string& appId) { return "{\"v\":1,\"client_id\":" + jsonEscape(appId) + "}"; }

std::string frame(uint32_t op, const std::string& json) {
    std::string f(8, '\0');
    const uint32_t n = (uint32_t)json.size();
    for (int i = 0; i < 4; i++) { f[i] = (char)(op >> (8 * i)); f[4 + i] = (char)(n >> (8 * i)); }
    return f + json;
}

Tuning& tuning() { static Tuning t; return t; }

// ---- the connection (one at a time, on the thread only)
namespace {
long processId() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

void lowerThreadPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);   // (Linux: a thread's own nice value)
#endif
}

struct Conn {
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
    bool open() const { return h != INVALID_HANDLE_VALUE; }
    void close() { if (open()) CloseHandle(h); h = INVALID_HANDLE_VALUE; }
    bool connect() {
        for (int i = 0; i < 10; i++) {
            wchar_t name[] = L"\\\\.\\pipe\\discord-ipc-0";
            name[sizeof name / sizeof *name - 2] = (wchar_t)(L'0' + i);
            h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (open()) return true;
        }
        return false;
    }
    bool write(const std::string& d) {
        size_t at = 0;
        while (at < d.size()) {
            DWORD w = 0;
            if (!WriteFile(h, d.data() + at, (DWORD)(d.size() - at), &w, nullptr)) return false;
            at += w;
        }
        return true;
    }
    int read(char* buf, int max) {   // bytes read, 0 none waiting, -1 broken
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return -1;
        if (!avail) return 0;
        DWORD got = 0;
        if (!ReadFile(h, buf, std::min<DWORD>(avail, (DWORD)max), &got, nullptr)) return -1;
        return (int)got;
    }
#else
    int fd = -1;
    bool open() const { return fd >= 0; }
    void close() { if (fd >= 0) ::close(fd); fd = -1; }
    bool tryPath(const std::string& path) {
        sockaddr_un a{};
        if (path.size() >= sizeof a.sun_path) return false;
        a.sun_family = AF_UNIX;
        memcpy(a.sun_path, path.c_str(), path.size() + 1);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);   // (macOS: a closed socket's write returns EPIPE)
#endif
        if (::connect(fd, (sockaddr*)&a, sizeof a) != 0) { close(); return false; }   // (a local socket: at once)
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        return true;
    }
    bool connect() {
        std::vector<std::string> dirs;
        if (!tuning().onlyDir.empty()) dirs.push_back(tuning().onlyDir);
        else {
            for (const char* v : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
                if (const char* e = getenv(v); e && *e) dirs.push_back(e);
            dirs.push_back("/tmp");
        }
        static const char* const subs[] = {"", "/app/com.discordapp.Discord", "/snap.discord"};   // (Flatpak, Snap)
        for (const std::string& d : dirs)
            for (const char* sub : tuning().onlyDir.empty() ? std::vector<const char*>(subs, subs + 3) : std::vector<const char*>{""})
                for (int i = 0; i < 10; i++)
                    if (tryPath(d + sub + "/discord-ipc-" + std::to_string(i))) return true;
        return false;
    }
    bool write(const std::string& d) {
        size_t at = 0;
        int waits = 0;
        while (at < d.size()) {
#ifdef MSG_NOSIGNAL
            const ssize_t w = ::send(fd, d.data() + at, d.size() - at, MSG_NOSIGNAL);
#else
            const ssize_t w = ::send(fd, d.data() + at, d.size() - at, 0);
#endif
            if (w > 0) { at += (size_t)w; continue; }
            if (w < 0 && errno == EINTR) continue;
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && waits++ < 10) {   // (full: up to 1 s, then it's broken)
                pollfd p{fd, POLLOUT, 0};
                poll(&p, 1, 100);
                continue;
            }
            return false;
        }
        return true;
    }
    int read(char* buf, int max) {
        const ssize_t r = ::recv(fd, buf, (size_t)max, 0);
        if (r > 0) return (int)r;
        if (r == 0) return -1;   // (closed)
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    }
#endif
};
}   // namespace

struct Client::Shared {
    std::mutex m;
    std::condition_variable cv, doneCv;
    bool quit = false, done = false;
    Presence pending;
    uint64_t gen = 0;               // (bumped by every set)
    std::string appId, lastJson;
    std::atomic<int> connects{0}, sends{0};
};

static void runThread(std::shared_ptr<Client::Shared> sh) {
    lowerThreadPriority();
    using clock = std::chrono::steady_clock;
    auto now = [] { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); };
    Conn c;
    std::string in;
    double nextTry = 0, lastSend = -1e18;
    bool sentSinceConnect = false;
    uint64_t sentGen = 0, nonce = 0;
    const long pid = processId();
    auto drop = [&](double t) { c.close(); in.clear(); nextTry = t + tuning().retryGap; };
    for (;;) {
        Presence p;
        uint64_t g;
        {
            std::unique_lock<std::mutex> lk(sh->m);
            sh->cv.wait_for(lk, std::chrono::milliseconds(tuning().pollMs), [&] { return sh->quit || (c.open() && sh->gen != sentGen && !sentSinceConnect); });
            if (sh->quit) break;
            p = sh->pending; g = sh->gen;
        }
        const double t = now();
        if (!c.open()) {
            if (t < nextTry) continue;
            nextTry = t + tuning().retryGap;
            if (!c.connect()) continue;
            if (!c.write(frame(0, handshakeJson(sh->appId)))) { drop(t); continue; }
            sh->connects++;
            sentSinceConnect = false;
            sentGen = 0;   // (the newest state goes again on a new connection)
        }
        // what Discord sent: PING answered with PONG, CLOSE (a bad id, Discord quitting) drops the connection
        bool broken = false;
        for (int k = 0; k < 16; k++) {
            char buf[4096];
            const int r = c.read(buf, sizeof buf);
            if (r < 0) { broken = true; break; }
            if (r == 0) break;
            in.append(buf, (size_t)r);
        }
        while (!broken && in.size() >= 8) {
            uint32_t op = 0, n = 0;
            for (int i = 0; i < 4; i++) { op |= (uint32_t)(unsigned char)in[i] << (8 * i); n |= (uint32_t)(unsigned char)in[4 + i] << (8 * i); }
            if (n > (1u << 20)) { broken = true; break; }
            if (in.size() < 8 + (size_t)n) break;
            if (op == 2) broken = true;
            else if (op == 3 && !c.write(frame(4, in.substr(8, n)))) broken = true;
            in.erase(0, 8 + (size_t)n);
        }
        if (broken) { drop(t); continue; }
        if (g != sentGen && g != 0 && (!sentSinceConnect || t - lastSend >= tuning().sendGap)) {
            const Activity a = buildActivity(p);
            const std::string j = setActivityJson(&a, pid, ++nonce);
            if (!c.write(frame(1, j))) { drop(t); continue; }
            sentGen = g; lastSend = t; sentSinceConnect = true;
            sh->sends++;
            std::lock_guard<std::mutex> lk(sh->m);
            sh->lastJson = j;
        }
    }
    if (c.open()) { c.write(frame(1, setActivityJson(nullptr, pid, ++nonce))); c.close(); }   // (off or quitting: the status cleared)
    std::lock_guard<std::mutex> lk(sh->m);
    sh->done = true;
    sh->doneCv.notify_all();
}

void Client::start(const std::string& appId) {
    if (sh_ || !validAppId(appId)) return;
    sh_ = std::make_shared<Shared>();
    sh_->appId = appId;
    std::thread(runThread, sh_).detach();   // (stop waits for it; the shared state outlives a thread left behind)
}

void Client::stop(int waitMs) {
    if (!sh_) return;
    {
        std::unique_lock<std::mutex> lk(sh_->m);
        sh_->quit = true;
        sh_->cv.notify_all();
        sh_->doneCv.wait_for(lk, std::chrono::milliseconds(waitMs), [&] { return sh_->done; });
    }
    sh_.reset();
}

void Client::set(const Presence& p) {
    if (!sh_) return;
    std::lock_guard<std::mutex> lk(sh_->m);
    sh_->pending = p;
    sh_->gen++;
    sh_->cv.notify_all();
}

int Client::connects() const { return sh_ ? sh_->connects.load() : 0; }
int Client::sends() const { return sh_ ? sh_->sends.load() : 0; }
std::string Client::lastJson() const {
    if (!sh_) return std::string();
    std::lock_guard<std::mutex> lk(sh_->m);
    return sh_->lastJson;
}

}   // namespace drpc
