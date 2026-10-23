#include "governor.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include "map.h"   // writeFileAtomic

const char* govChannelName(int ch) { return ch == GOV_TESTING ? "testing" : "prod"; }
int govChannelFromName(const std::string& s) {
    if (s == "prod" || s == "production") return GOV_PROD;
    if (s == "testing" || s == "test") return GOV_TESTING;
    return -1;
}
bool isTestingVersion(const char* v) { return v && strchr(v, '-') != nullptr; }

GovState governorInitial(const GovParams& p) {
    GovState s;
    s.calmWait = p.calm;
    s.reopenWait = p.reopenAfter;
    return s;
}

// Which server goes first when one must slow down (lower = sooner): the one nobody is on, then the one not playing a
// ranked match (0-1 players in it doesn't count), and between two alike, testing (production comes first).
static int throttleOrder(const GovSide& s, int ch) {
    return (s.players > 0 ? 4 : 0) + (s.rankedPlayers >= 2 ? 2 : 0) + (ch == GOV_PROD ? 1 : 0);
}

GovState governorStep(const GovState& prev, const GovSide s[2], double now, const GovParams& p) {
    const int floorLv = kTickRateLevels - 1;
    GovState st = prev;
    st.now[0] = st.now[1] = false;
    auto clampLv = [&](int lv, int top) { return std::max(std::min(lv, floorLv), std::max(0, std::min(top, floorLv))); };
    auto alone = [&] {   // production runs as a single server: its own tick-rate logic, no caps
        st.active = false;
        st.overSince = st.floorSince = -1;
        st.cap[0] = clampLv(s[0].top, s[0].top);
        st.cap[1] = st.testingClosed ? floorLv : clampLv(s[1].top, s[1].top);
    };
    if (st.testingClosed) {
        alone();
        if (!s[0].up || s[0].overloaded) st.prodCalmSince = now;
        if (now - st.prodCalmSince >= st.reopenWait) {   // production has been calm long enough: testing reopens at 125 Hz
            st.testingClosed = false;
            st.reopenedAt = st.lastChange = st.calmSince = now;
            st.active = s[0].up && s[1].up;
            st.cap[0] = clampLv(s[0].level, s[0].top);
            st.cap[1] = floorLv;
        }
        return st;
    }
    if (!s[0].up || !s[1].up) { alone(); return st; }   // the other one is down: no pressure from it
    if (!st.active) {   // both up again (or for the first time): start from where they are, nothing changes at once
        st.active = true;
        st.cap[0] = clampLv(s[0].level, s[0].top);
        st.cap[1] = clampLv(s[1].level, s[1].top);
        st.overSince = st.floorSince = -1;
        st.calmSince = st.lastChange = now;
    }
    for (int i = 0; i < 2; i++) st.cap[i] = clampLv(st.cap[i], s[i].top);   // (never above a server's own --tickrate)

    if (s[0].overloaded || s[1].overloaded) {
        if (st.overSince < 0) {
            st.overSince = now;
            if (now - st.lastRaise < st.calmWait) st.calmWait = std::min(st.calmWait * 2, p.calmMax);   // our last raise didn't hold
        }
        bool force = s[0].severe || s[1].severe || now - st.overSince >= p.forceAfter;
        st.now[0] = st.now[1] = force;
        // a lowered cap that a server with players hasn't reached yet (it waits for its next break): wait for it,
        // unless the overload is sustained / severe (then it applies at once anyway)
        bool pending = false;
        for (int i = 0; i < 2; i++) pending |= s[i].players > 0 && s[i].level < st.cap[i];
        if (now - st.lastChange >= p.stepGap && (!pending || force)) {
            int pick = -1;
            for (int i : {GOV_TESTING, GOV_PROD})
                if (st.cap[i] < floorLv && (pick < 0 || throttleOrder(s[i], i) < throttleOrder(s[pick], pick))) pick = i;
            if (pick >= 0) { st.cap[pick]++; st.lastChange = now; }
        }
        // both at 125 Hz (and running there) and production still overloaded: after closeAfter, testing closes
        bool atFloor = st.cap[0] == floorLv && st.cap[1] == floorLv && s[0].level == floorLv && s[1].level == floorLv;
        if (atFloor && s[0].overloaded) {
            if (st.floorSince < 0) st.floorSince = now;
            if (now - st.floorSince >= p.closeAfter) {
                st.reopenWait = now - st.reopenedAt < p.relapse ? std::min(st.reopenWait * 2, p.reopenMax) : p.reopenAfter;
                st.testingClosed = true;
                st.closedAt = st.prodCalmSince = now;
                st.now[0] = st.now[1] = false;
                alone();
            }
        } else st.floorSince = -1;
        return st;
    }
    // calm
    if (st.overSince >= 0) { st.overSince = -1; st.calmSince = now; }
    st.floorSince = -1;
    if (now - st.calmSince >= st.calmWait && now - st.lastChange >= st.calmWait) {
        int pick = -1;   // reverse order: the server that would be throttled last rises first
        for (int i : {GOV_PROD, GOV_TESTING})
            if (st.cap[i] > clampLv(s[i].top, s[i].top) && (pick < 0 || throttleOrder(s[i], i) > throttleOrder(s[pick], pick))) pick = i;
        if (pick >= 0) { st.cap[pick]--; st.lastChange = st.lastRaise = now; }
        else st.calmWait = p.calm;   // all the way back up: the backoff starts over
    }
    return st;
}

// ---- status files ----
std::string govStatusText(int channel, double now, const GovSide& me, const GovState* st) {
    char b[640];
    int n = snprintf(b, sizeof b, "quark-governor 1\nchannel %s\nt %.3f\nplayers %d\nranked %d\nlevel %d\ntop %d\nover %d\nsevere %d\nclosed %d\n",
                     govChannelName(channel), now, me.players, me.rankedPlayers, me.level, me.top, (int)me.overloaded, (int)me.severe, (int)me.closed);
    if (st && n > 0 && n < (int)sizeof b)
        snprintf(b + n, sizeof b - n, "state %d %d %d %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
                 (int)st->active, st->cap[0], st->cap[1], (int)st->now[0], (int)st->now[1], (int)st->testingClosed, st->overSince,
                 st->calmSince, st->lastChange, st->lastRaise, st->calmWait, st->floorSince, st->closedAt, st->reopenedAt,
                 st->reopenWait, st->prodCalmSince);
    return b;
}

bool govParseStatus(const std::string& text, int* channel, double* t, GovSide* side, GovState* st, bool* hasState) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "quark-governor 1") return false;
    *channel = -1; *t = -1; *hasState = false;
    *side = GovSide();
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string k;
        ls >> k;
        int v = 0;
        if (k == "channel") { std::string c; ls >> c; *channel = govChannelFromName(c); }
        else if (k == "t") ls >> *t;
        else if (k == "state") {
            int a, c0, c1, n0, n1, tc;
            GovState s;
            ls >> a >> c0 >> c1 >> n0 >> n1 >> tc >> s.overSince >> s.calmSince >> s.lastChange >> s.lastRaise >> s.calmWait >> s.floorSince >>
                s.closedAt >> s.reopenedAt >> s.reopenWait >> s.prodCalmSince;
            if (ls.fail() || c0 < 0 || c1 < 0 || c0 >= kTickRateLevels || c1 >= kTickRateLevels) return false;
            s.active = a; s.cap[0] = c0; s.cap[1] = c1; s.now[0] = n0; s.now[1] = n1; s.testingClosed = tc;
            *st = s;
            *hasState = true;
        } else if (ls >> v) {
            if (k == "players") side->players = std::max(0, v);
            else if (k == "ranked") side->rankedPlayers = std::max(0, v);
            else if (k == "level") side->level = std::clamp(v, 0, kTickRateLevels - 1);
            else if (k == "top") side->top = std::clamp(v, 0, kTickRateLevels - 1);
            else if (k == "over") side->overloaded = v;
            else if (k == "severe") side->severe = v;
            else if (k == "closed") side->closed = v;
        }
    }
    return *channel >= 0 && *t > 0;
}

// ---- the governor thread ----
double Governor::wallNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

static bool readSmallFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[2048];
    f.read(buf, sizeof buf);
    out.assign(buf, (size_t)f.gcount());
    return true;
}

void Governor::start(const std::string& dir, int channel, const GovParams& p, double interval) {
    stop();
    dir_ = dir;
    channel_ = channel == GOV_TESTING ? GOV_TESTING : GOV_PROD;
    p_ = p;
    interval_ = interval;
    st_ = governorInitial(p);
    if (channel_ == GOV_PROD) {   // production restarting (an update): carry on with the decision it published last
        std::string text;
        int ch; double t; GovSide side; GovState s; bool has = false;
        if (readSmallFile(dir_ + "/prod.status", text) && govParseStatus(text, &ch, &t, &side, &s, &has) && ch == GOV_PROD && has &&
            wallNow() - t < 120 && t <= wallNow() + 1)
            st_ = s;
    }
    { std::lock_guard<std::mutex> l(mu_); quit_ = false; }
    if (interval_ > 0) thread_ = std::thread([this] { run(); });
}

void Governor::stop() {
    if (!thread_.joinable()) return;
    { std::lock_guard<std::mutex> l(mu_); quit_ = true; }
    cv_.notify_all();
    thread_.join();
}

void Governor::run() {
    for (;;) {
        try { poll(wallNow()); } catch (const std::exception&) {}   // (ENG-2: a bad file or full disk never takes the server down)
        std::unique_lock<std::mutex> l(mu_);
        if (cv_.wait_for(l, std::chrono::duration<double>(interval_), [this] { return quit_; })) return;
    }
}

constexpr double kClosedGrace = 60;   // seconds a closed testing server waits for a restarting production server

static std::string capText(int lv) { return std::to_string(kTickRates[std::clamp(lv, 0, kTickRateLevels - 1)]) + " Hz"; }

void Governor::poll(double now) {
    GovSide me;
    GovState st;
    { std::lock_guard<std::mutex> l(mu_); me = me_; st = st_; }
    me.up = true;
    const int other = channel_ == GOV_PROD ? GOV_TESTING : GOV_PROD;
    GovSide peer;
    GovState peerSt;
    bool peerHas = false;
    double peerT = 0;
    {
        std::string text;
        int ch = -1;
        if (readSmallFile(dir_ + "/" + govChannelName(other) + ".status", text) && govParseStatus(text, &ch, &peerT, &peer, &peerSt, &peerHas)) {
            peer.up = ch == other && peerT <= now + 1 && now - peerT < p_.stale;
            peerHas = peerHas && ch == other;
        }
    }
    std::string msg;
    if (channel_ == GOV_PROD) {
        GovSide s[2] = {me, peer};
        GovState next = governorStep(st, s, now, p_);
        auto why = [&] {
            char b[160];
            snprintf(b, sizeof b, " (prod: %d players, %s; testing: %d players, %s)", me.players, me.overloaded ? "overloaded" : "ok",
                     peer.players, peer.overloaded ? "overloaded" : "ok");
            return std::string(b);
        };
        if (next.active != st.active && !next.testingClosed && !st.testingClosed)
            msg += next.active ? "governor: the testing server is up - sharing the machine with it\n" : "governor: the testing server is gone - running alone\n";
        if (next.testingClosed && !st.testingClosed)
            msg += "governor: CLOSING the testing server - both at 125 Hz and still overloaded" + why() + "; it reopens after " +
                   std::to_string((int)next.reopenWait) + " s without overload\n";
        if (!next.testingClosed && st.testingClosed) msg += "governor: reopening the testing server (at 125 Hz)\n";
        if (next.active && st.active)
            for (int i = 0; i < 2; i++)
                if (next.cap[i] != st.cap[i])
                    msg += std::string("governor: ") + govChannelName(i) + " tick rate cap " + capText(st.cap[i]) + " -> " + capText(next.cap[i]) + why() + "\n";
        active.store(next.active, std::memory_order_relaxed);
        cap.store(next.cap[GOV_PROD], std::memory_order_relaxed);
        applyNow.store(next.now[GOV_PROD], std::memory_order_relaxed);
        closed.store(false, std::memory_order_relaxed);
        st = next;
        writeFileAtomic(dir_ + "/prod.status", govStatusText(GOV_PROD, now, me, &st));
    } else {
        bool follow = peer.up && peerHas;   // production's decision (production down: we run alone)
        // (closed stays closed while production restarts, e.g. for an update: its decision survives the restart)
        bool cl = peerHas && peerSt.testingClosed && (follow || (peerT <= now + 1 && now - peerT < kClosedGrace));
        if (cl != closed.load(std::memory_order_relaxed))
            msg += cl ? "governor: production says close (the machine is overloaded)\n" : "governor: production says reopen\n";
        active.store(follow && peerSt.active, std::memory_order_relaxed);
        cap.store(follow ? peerSt.cap[GOV_TESTING] : 0, std::memory_order_relaxed);
        applyNow.store(follow && peerSt.now[GOV_TESTING], std::memory_order_relaxed);
        closed.store(cl, std::memory_order_relaxed);
        if (follow) st = peerSt;
        writeFileAtomic(dir_ + "/testing.status", govStatusText(GOV_TESTING, now, me, nullptr));
    }
    std::lock_guard<std::mutex> l(mu_);
    st_ = st;
    if (!msg.empty()) { msg.pop_back(); log_ += (log_.empty() ? "" : "\n") + msg; }
}
