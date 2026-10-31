// Two servers on one machine (governor.h): the load governor's policy, two in-process servers sharing a governor
// directory, and ranked on a testing server (played, never rated). Included by tests.cpp (it shares CHECK, pump, idle).

static GovSide govSide(int players, int rankedPlayers, int level, bool over, bool severe = false, int top = 0) {
    GovSide s;
    s.up = true; s.players = players; s.rankedPlayers = rankedPlayers; s.level = level; s.top = top;
    s.overloaded = over; s.severe = severe;
    return s;
}

// The owner's order, step by step (pure function: same inputs, same answer).
static void testGovernorPolicy() {
    printf("load governor: the owner's order, hysteresis, closing / reopening testing\n");
    const GovParams P;   // the shipped values
    const int F = kTickRateLevels - 1;   // 125 Hz
    CHECK(kTickRates[F] == 125 && kTickRates[0] == 1000);
    CHECK(isTestingVersion("1.17.0-test.42") && !isTestingVersion("1.17.0") && !isTestingVersion(""));
    CHECK(isTestingVersion("1.19.0.testing.5") && isTestingVersion("1.19.0.testing.5+abc") && !isTestingVersion("1.19.0+abc"));
    CHECK(govChannelFromName("prod") == GOV_PROD && govChannelFromName("testing") == GOV_TESTING && govChannelFromName("x") < 0);
    // A run of the policy: `s` are the statuses; the servers follow the caps (an empty one at once, one with players
    // only when the governor says now or `brk` (a break)), like Server::applyGovernorCap.
    struct Run {
        GovState st;
        GovSide s[2];
        double t = 1e9;
        bool brk = false;
        GovParams p;
        void step(double dt = 1) {
            t += dt;
            st = governorStep(st, s, t, p);
            for (int i = 0; i < 2; i++)
                if (st.active && st.cap[i] != s[i].level && (s[i].players == 0 || brk || (st.cap[i] > s[i].level && st.now[i]))) s[i].level = st.cap[i];
        }
        void steps(double secs) { for (double k = 0; k < secs; k += 1) step(); }
    };
    auto fresh = [&](GovSide prod, GovSide test) { Run r; r.p = P; r.st = governorInitial(P); r.s[0] = prod; r.s[1] = test; r.step(0); return r; };

    {   // 1. the empty one first, a step every stepGap, down to 125 Hz; then the other (here: a ranked match, the only one left)
        Run r = fresh(govSide(2, 2, 0, false), govSide(0, 0, 0, false));
        CHECK(r.st.active && r.st.cap[0] == 0 && r.st.cap[1] == 0);   // both up: active, starting from where they are
        r.s[0].overloaded = true;
        r.step();
        CHECK(r.st.cap[1] == 0);                    // (the step gap counts from activation)
        r.steps(P.stepGap);
        CHECK(r.st.cap[1] == 1 && r.st.cap[0] == 0 && r.s[1].level == 1);   // testing (empty) first, applied at once
        r.steps(P.stepGap); r.steps(P.stepGap);
        CHECK(r.st.cap[1] == F && r.st.cap[0] == 0);
        r.brk = true;                               // (a break: prod's caps apply as they come)
        r.steps(P.stepGap);
        CHECK(r.st.cap[0] == 1 && r.st.cap[1] == F); // the empty one is at 125 and it's still overloaded: the other
    }
    {   // 2. nobody's empty: the one NOT in a ranked match first (testing has one here), whatever the channel
        Run r = fresh(govSide(3, 0, 0, true), govSide(2, 2, 0, false));
        r.brk = true;
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[0] == 1 && r.st.cap[1] == 0);
        r.steps(P.stepGap * 2);
        CHECK(r.st.cap[0] == F && r.st.cap[1] == 0);
        r.steps(P.stepGap);
        CHECK(r.st.cap[1] == 1);                    // 3. still overloaded: the other too
    }
    {   // a ranked match of 0-1 players isn't one: prod has a lone ranked player, testing a real match -> prod first
        Run r = fresh(govSide(1, 1, 0, true), govSide(2, 2, 0, false));
        r.brk = true;
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[0] == 1 && r.st.cap[1] == 0);
    }
    {   // both alike (both playing, neither ranked; or both empty): testing first, production comes first
        Run r = fresh(govSide(2, 0, 0, true), govSide(2, 0, 0, false));
        r.brk = true;
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[1] == 1 && r.st.cap[0] == 0);
        Run e = fresh(govSide(0, 0, 0, false), govSide(0, 0, 0, true));
        e.steps(P.stepGap + 1);
        CHECK(e.st.cap[1] == 1 && e.st.cap[0] == 0);
    }
    {   // empty prod, busy testing: the empty one first even though it's production
        Run r = fresh(govSide(0, 0, 0, false), govSide(4, 0, 0, true));
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[0] == 1 && r.st.cap[1] == 0);
    }
    {   // mid-round (GAME-11): a lowered cap waits for the break, and nothing more is lowered meanwhile ...
        Run r = fresh(govSide(3, 0, 0, true), govSide(2, 2, 0, false));
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[0] == 1 && r.s[0].level == 0 && !r.st.now[0]);   // (waits for prod's break)
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[0] == 1);                    // pending: no second step yet
        // ... unless the overload is sustained (forceAfter): then caps apply at once and steps go on
        r.steps(P.forceAfter);
        CHECK(r.st.now[0] && r.s[0].level >= 1 && r.st.cap[0] >= 2);
    }
    {   // a severe overload (busy > 90%) applies at once
        Run r = fresh(govSide(3, 0, 0, true, true), govSide(2, 2, 0, false));
        r.steps(P.stepGap + 1);
        CHECK(r.st.now[0] && r.st.cap[0] == 1 && r.s[0].level == 1);
    }
    {   // 4. both at 125 Hz, production still overloaded for closeAfter: testing closes; production never does
        Run r = fresh(govSide(2, 2, F, true), govSide(2, 0, F, false));
        CHECK(r.st.cap[0] == F && r.st.cap[1] == F);
        r.steps(P.closeAfter - 2);
        CHECK(!r.st.testingClosed);
        r.steps(3);
        CHECK(r.st.testingClosed && !r.st.active && r.st.cap[0] == 0);   // production runs alone again (its own rate logic)
        r.steps(600);                               // a machine that can't carry production alone: production stays open
        CHECK(r.st.testingClosed && !r.st.active);
        // only testing overloaded at the floor (prod fine, or empty): no point closing it
        Run q = fresh(govSide(2, 0, F, false), govSide(2, 0, F, true));
        q.steps(P.closeAfter * 3);
        CHECK(!q.st.testingClosed && q.st.cap[0] == F && q.st.cap[1] == F);
        // a calm window at the floor restarts the close clock
        Run w = fresh(govSide(2, 2, F, true), govSide(2, 0, F, false));
        w.steps(P.closeAfter - 5);
        w.s[0].overloaded = false; w.step();
        w.s[0].overloaded = true;
        w.steps(P.closeAfter - 5);
        CHECK(!w.st.testingClosed);
    }
    {   // reopen: production calm for reopenAfter (an overload meanwhile restarts the wait); testing comes back at 125 Hz
        Run r = fresh(govSide(2, 2, F, true), govSide(0, 0, F, false));
        r.steps(P.closeAfter + 1);
        CHECK(r.st.testingClosed);
        r.s[1].closed = true;
        r.s[0].level = F;
        r.steps(P.reopenAfter / 2);
        CHECK(r.st.testingClosed);                  // (still overloaded: no reopening)
        r.s[0].overloaded = false;
        r.steps(P.reopenAfter - 2);
        CHECK(r.st.testingClosed);
        r.steps(3);
        CHECK(!r.st.testingClosed && r.st.active && r.st.cap[1] == F && r.st.cap[0] == F);
        // closing again soon after reopening doubles the next wait
        r.s[1].closed = false;
        r.s[0].overloaded = true;
        r.steps(P.closeAfter + 2);
        CHECK(r.st.testingClosed && r.st.reopenWait == P.reopenAfter * 2);
        r.s[0].overloaded = false;
        r.steps(P.reopenAfter + 2);
        CHECK(r.st.testingClosed);
        r.steps(P.reopenAfter);
        CHECK(!r.st.testingClosed);
        // a close long after the last reopen starts the wait over
        r.s[0].overloaded = true;
        r.t += P.relapse;
        r.steps(P.closeAfter + 2);
        CHECK(r.st.testingClosed && r.st.reopenWait == P.reopenAfter);
    }
    {   // recovery: one step per calm period, in reverse order (the server throttled last, i.e. the important one, first)
        Run r = fresh(govSide(3, 2, F, false), govSide(0, 0, F, false));
        r.brk = true;
        r.steps(P.calm - 2);
        CHECK(r.st.cap[0] == F && r.st.cap[1] == F);   // not before a full calm period
        r.steps(3);
        CHECK(r.st.cap[0] == F - 1 && r.st.cap[1] == F);   // the ranked server first
        r.steps(P.calm);
        CHECK(r.st.cap[0] == F - 2 && r.st.cap[1] == F);
        r.steps(P.calm * 2 - 2);
        CHECK(r.st.cap[0] == 0 && r.st.cap[1] == F);
        r.steps(2);
        CHECK(r.st.cap[1] == F - 1);                // then the other
        // a raise that brings the overload back doubles the calm wait (no flapping)
        r.s[1].overloaded = true;
        r.step();
        CHECK(r.st.calmWait == P.calm * 2);
        r.steps(P.stepGap + 1);
        CHECK(r.st.cap[1] == F);                    // back down (the empty one)
        r.s[1].overloaded = false;
        r.steps(P.calm + 2);
        CHECK(r.st.cap[1] == F);                    // one calm period isn't enough any more
        r.steps(P.calm);
        CHECK(r.st.cap[1] == F - 1);
        r.steps(P.calm * 2 * 3 + 4);
        CHECK(r.st.cap[0] == 0 && r.st.cap[1] == 0 && r.st.calmWait == P.calm);   // all the way up: the backoff starts over
    }
    {   // a stale / missing peer (the other server is down): no pressure from it, no caps, nothing closes
        Run r = fresh(govSide(3, 0, 0, true), govSide(0, 0, 0, false));
        r.s[1].up = false;
        r.steps(P.closeAfter * 3);
        CHECK(!r.st.active && r.st.cap[0] == 0 && !r.st.testingClosed);
        r.s[1].up = true;                           // back: active from where both are, no sudden change
        r.s[0].level = 2;
        r.step();
        CHECK(r.st.active && r.st.cap[0] == 2 && r.st.cap[1] == 0);
    }
    {   // a server's own --tickrate is its ceiling: caps never go above it, raises stop there
        Run r = fresh(govSide(2, 0, 1, false, false, 1), govSide(0, 0, F, false, false, 2));
        CHECK(r.st.cap[0] == 1 && r.st.cap[1] == F);
        r.steps(P.calm * 4);
        CHECK(r.st.cap[0] == 1 && r.st.cap[1] == 2);
    }
    {   // the status file round trip (what both servers write and read)
        GovSide me = govSide(5, 2, 2, true, false, 1);
        me.closed = true;
        GovState st = governorInitial(P);
        st.active = true; st.cap[0] = 2; st.cap[1] = 3; st.now[1] = true; st.overSince = 1234.5; st.reopenWait = 600;
        std::string text = govStatusText(GOV_PROD, 1700000000.25, me, &st);
        int ch; double t; GovSide got; GovState gs; bool has;
        CHECK(govParseStatus(text, &ch, &t, &got, &gs, &has) && ch == GOV_PROD && std::fabs(t - 1700000000.25) < 1e-3 && has);
        CHECK(got.players == 5 && got.rankedPlayers == 2 && got.level == 2 && got.top == 1 && got.overloaded && !got.severe && got.closed);
        CHECK(gs.active && gs.cap[0] == 2 && gs.cap[1] == 3 && !gs.now[0] && gs.now[1] && gs.overSince == 1234.5 && gs.reopenWait == 600);
        CHECK(govParseStatus(govStatusText(GOV_TESTING, 5, me, nullptr), &ch, &t, &got, &gs, &has) && ch == GOV_TESTING && !has);
        CHECK(!govParseStatus("garbage\n", &ch, &t, &got, &gs, &has));
        CHECK(!govParseStatus("quark-governor 1\nchannel prod\n", &ch, &t, &got, &gs, &has));   // (no time: never fresh)
    }
}

// Two servers in this process sharing a governor directory, production overloaded (its clients losing snapshots): the
// empty testing server is throttled to 125 Hz first, then production; both at 125 and production still overloaded:
// testing closes (refuses connects); calm again: testing reopens and both climb back, production first.
static void testGovernorTwoServers(uint16_t port) {
    printf("load governor: two servers sharing a machine\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    namespace fs = std::filesystem;
    std::string dir = (fs::temp_directory_path() / ("quark_gov_" + std::to_string(getpid()) + "_" + std::to_string(qrand()))).string();
    struct Rm { std::string d; ~Rm() { std::error_code ec; fs::remove_all(d, ec); } } rm{dir};
    GovParams gp;   // the same policy on a short clock
    gp.stepGap = 0.5; gp.forceAfter = 1.5; gp.closeAfter = 1.0; gp.calm = 1.0; gp.calmMax = 4; gp.reopenAfter = 2.0; gp.relapse = 5; gp.stale = 1.5;
    Server prod, test;
    ServerConfig pc, tc;
    pc.map = tc.map = "bastion"; pc.mode = tc.mode = MODE_FFA;
    pc.port = port; tc.port = (uint16_t)(port + 1);
    pc.channel = GOV_PROD; tc.channel = GOV_TESTING;
    pc.governorDir = tc.governorDir = dir;
    pc.governor = tc.governor = gp;
    pc.governorInterval = tc.governorInterval = 0.1;
    std::vector<std::string> logs;
    std::mutex logMu;
    prod.setLog([&](const std::string& l) { std::lock_guard<std::mutex> g(logMu); logs.push_back("prod: " + l); });
    test.setLog([&](const std::string& l) { std::lock_guard<std::mutex> g(logMu); logs.push_back("testing: " + l); });
    std::string e;
    if (!prod.start(pc, &e) || !test.start(tc, &e)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e.c_str()); g_fail++; prod.stop(); test.stop(); return; }
    // The load signal is the players' reported loss alone: this machine's own stalls (other test groups run beside
    // this one) would read as overload on top of it, or keep "calm" from coming.
    prod.rateStalls_ = test.rateStalls_ = false;
    NetClient a, a2;   // (two: one player losing snapshots is that player's connection, not the machine)
    a.connect("127.0.0.1:" + std::to_string(pc.port), "player");
    a2.connect("127.0.0.1:" + std::to_string(pc.port), "player2");
    std::vector<NetClient*> cls = {&a, &a2};
    std::vector<NetClient*> none;
    auto both = [&](double secs, const std::function<bool()>& until) {   // both servers' loops (and the client)
        double end = timeNow() + secs, nextTick = timeNow();
        while (timeNow() < end && !until()) {
            prod.frame(); test.frame();
            a.poll(); a2.poll();
            while (timeNow() >= nextTick) { a.tick(UserCmd()); a2.tick(UserCmd()); nextTick += a.tickPeriod(); }
            sleepMs(1);
        }
    };
    both(1.0, [&] { return a.state() == ConnState::Connected && a.hasSnapshot() && a2.hasSnapshot(); });
    CHECK(a.state() == ConnState::Connected && a2.hasSnapshot() && prod.tickRate() == 1000 && test.tickRate() == 1000);
    both(1.0, [&] { return prod.gov_->active.load() && test.gov_->active.load(); });
    CHECK(prod.gov_->active.load() && test.gov_->active.load());   // each sees the other: governed
    a.overrideReportedLoss(50); a2.overrideReportedLoss(50);   // production's players lose snapshots: the machine is "overloaded"
    // testing (empty) goes to 125 Hz before production moves at all
    double prodFirstDrop = -1, testAtFloor = -1;
    double t0 = timeNow();
    both(30.0, [&] {
        if (testAtFloor < 0 && test.tickRate() == 125) testAtFloor = timeNow() - t0;
        if (prodFirstDrop < 0 && prod.tickRate() < 1000) prodFirstDrop = timeNow() - t0;
        return prod.tickRate() == 125 && test.tickRate() == 125;
    });
    printf("  testing at 125 Hz after %.1f s, production's first step down after %.1f s\n", testAtFloor, prodFirstDrop);
    CHECK(testAtFloor > 0 && prodFirstDrop > 0 && testAtFloor <= prodFirstDrop);
    CHECK(prod.tickRate() == 125 && test.tickRate() == 125);
    // both at the floor, production still overloaded: testing closes; production never does
    both(15.0, [&] { return test.closed_; });
    CHECK(test.closed_ && !prod.closed_);
    CHECK(!prod.gov_->active.load());   // production runs alone again
    {   // a closed server refuses players with the reason
        NetClient b;
        b.connect("127.0.0.1:" + std::to_string(tc.port), "late");
        std::vector<NetClient*> bc = {&b};
        double end = timeNow() + 3;
        while (timeNow() < end && b.state() != ConnState::Disconnected) { prod.frame(); test.frame(); b.poll(); a.poll(); a2.poll(); b.tick(UserCmd()); sleepMs(1); }
        printf("  connecting to the closed testing server: %s\n", b.error().c_str());
        CHECK(b.error().find("testing server closed") != std::string::npos);
    }
    // calm again: testing reopens (at 125 Hz), then caps rise one step per calm period, production first
    a.overrideReportedLoss(0); a2.overrideReportedLoss(0);
    both(20.0, [&] { return !test.closed_; });
    CHECK(!test.closed_ && test.tickRate() == 125);
    double prodTop = -1, testRise = -1;
    t0 = timeNow();
    both(40.0, [&] {
        if (prodTop < 0 && prod.tickRate() == 1000) prodTop = timeNow() - t0;
        if (testRise < 0 && test.tickRate() > 125) testRise = timeNow() - t0;
        return prod.tickRate() == 1000 && test.tickRate() == 1000;
    });
    printf("  recovery: production back at 1000 Hz after %.1f s, testing starts rising after %.1f s\n", prodTop, testRise);
    CHECK(prod.tickRate() == 1000 && test.tickRate() == 1000);
    CHECK(prodTop > 0 && testRise > 0 && prodTop <= testRise);
    {   // the governor said what it did
        std::lock_guard<std::mutex> g(logMu);
        bool closing = false, reopening = false;
        for (auto& l : logs) { closing |= l.find("CLOSING the testing server") != std::string::npos; reopening |= l.find("reopening the testing server") != std::string::npos; }
        CHECK(closing && reopening);
    }
    // production stops: testing's peer goes stale and it runs alone (no governor pressure)
    prod.stop();
    double end = timeNow() + 5;
    while (timeNow() < end && test.gov_->active.load()) { test.frame(); sleepMs(2); }
    CHECK(!test.gov_->active.load() && !test.closed_);
    test.stop();
}

// A testing server (or testing build) plays ranked as usual, lock included, but never rates it: no ticket signatures
// asked, no result, everyone told. (QUARK_MASTER=off in tests: nothing could reach a Worker anyway; what's checked is
// that the server never tries.)
static void testRankedNotRated(uint16_t port) {
    printf("ranked on a testing server: played, never rated\n");
    {   // a testing build (a -test. version) never rates, whatever channel it's given (the tests run pinned to production)
        pinTestingBuildForTests(1);
        { Server t; CHECK(testingBuild() && !t.rated()); }
        pinTestingBuildForTests(0);
        { Server t; CHECK(!testingBuild() && t.rated()); }
    }
    for (int ch : {GOV_PROD, GOV_TESTING}) {
        Server s;
        ServerConfig cfg;
        cfg.map = "qk1"; cfg.port = (uint16_t)(port + ch); cfg.ranked = true; cfg.arenaVariant = 0; cfg.channel = ch;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        CHECK(s.rated() == (ch == GOV_PROD));
        CHECK((s.listName().find("NOT RATED") != std::string::npos) == (ch == GOV_TESTING));
        NetClient c[2];
        std::vector<NetClient*> cls;
        for (int i = 0; i < 2; i++) {
            c[i].setRankKey(rankKeyFromSecret(sha256Hex("unrated" + std::to_string(i) + std::to_string(qrand()))));
            c[i].connect("127.0.0.1:" + std::to_string(cfg.port), "p" + std::to_string(i));
            cls.push_back(&c[i]);
        }
        pump(s, cls, 1.0, idle);
        bool told = false, signAsked = false, rankedEvent = false;
        auto drain = [&] {
            for (auto* cl : cls) for (auto& ev : cl->takeEvents()) {
                if (ev.type == EV_MESSAGE && ev.text.rfind(kUnratedNotice, 0) == 0) told = true;
                if (ev.type == EV_RANKED) rankedEvent = true;
            }
        };
        drain();
        CHECK(told == (ch == GOV_TESTING));   // said on joining
        s.command("forcestart");
        for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { if (s.ms_.phase == PHASE_FREEZE) s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
        CHECK(s.ms_.phase == PHASE_LIVE);
        pump(s, cls, 0.3, idle);
        drain();
        CHECK(s.rankedLive());                // the match is locked like any ranked match
        for (auto& p : s.players_) signAsked |= p.connected && !p.ticketSig.empty();   // (games sign what EV_RANKSIGN asks)
        CHECK(signAsked == (ch == GOV_PROD)); // a ticket only where it's rated
        CHECK(ch == GOV_PROD || s.rankedTicketSent_);   // (testing: no signatures to wait for)
        told = false;
        rulesEndMatch(s, "match over", TEAM_A);
        pump(s, cls, 0.3, idle);
        drain();
        CHECK(rankedEvent == (ch == GOV_PROD));   // no EV_RANKED: players' games have nothing to report
        CHECK(told == (ch == GOV_TESTING));
        s.stop();
    }
}
