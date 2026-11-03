// TUTORIAL (src/game/tutorial.h): the lessons, the map, and every lesson's ghost script played by the real server
// (the ghost bot on the tutorial map, every tick on the manual clock) passing the very check the player has to pass.
// A change to movement, weapons or the map that breaks a lesson's demo fails here.
#pragma once
#include "../src/game/tutorial.h"
#include <set>

inline void testTutorial() {
    printf("tutorial: lessons, scripts, the map\n");
    std::set<std::string> ids;
    for (int l = 0; l < tut::L_COUNT; l++) {
        const tut::LessonDef& d = tut::lesson(l);
        CHECK(ids.insert(d.id).second);
        CHECK(tut::lessonFromName(d.id) == l && tut::lessonFromName(std::to_string(l).c_str()) == l);
        CHECK(tut::loopMs(l) >= 1000 && tut::loopMs(l) <= 12000);
        CHECK(d.len >= 10 && d.height >= 10);
        // the script's command: the lesson's weapon, a start facing +x, nothing pressed before its loop
        const UserCmd idle = tut::ghostCmd(l, -1);
        CHECK(idle.buttons == 0 && idle.forward == 0 && std::fabs(idle.yaw - tut::kStartYaw) < 1e-6f && idle.weapon == d.weapon);
        CHECK(tut::ghostCmd(l, tut::loopMs(l) - 1).weapon == d.weapon);
    }
    CHECK(tut::lessonFromName("nope") < 0 && tut::lessonFromName("") < 0 && tut::lessonFromName(std::to_string(tut::L_COUNT).c_str()) < 0);
    // (q: the big ones, marked KEY TECH: the launch, grappling a player, dashing out of a grapple, the rocket grapple)
    for (int l : {tut::L_LAUNCH, tut::L_HOOKPLAYER, tut::L_BREAKFREE, tut::L_ROCKETGRAPPLE, tut::L_BUBBLELAUNCH}) CHECK(tut::lesson(l).key);
    // the PARTNER's lessons: its script, its spot on the bay's floor ahead of you, its aim at the other one
    for (int l = 0; l < tut::L_COUNT; l++) {
        const tut::LessonDef& d = tut::lesson(l);
        CHECK((d.partner != nullptr) == (l == tut::L_HOOKPLAYER || l == tut::L_BREAKFREE));
        if (!d.partner) { CHECK(!tut::partnerAimsAt(l, 500) && tut::partnerCmd(l, 500).buttons == 0); continue; }
        CHECK(d.partnerIn > tut::kStartIn + 10 && d.partnerIn < d.len - 2);
        const UserCmd pc = tut::partnerCmd(l, -1);
        CHECK(pc.buttons == 0 && std::fabs(pc.yaw - tut::kPartnerYaw) < 1e-6f);
    }
    CHECK(tut::ghostAimsAt(tut::L_HOOKPLAYER, 1000) && !tut::ghostAimsAt(tut::L_HOOKPLAYER, 3500) && tut::partnerAimsAt(tut::L_BREAKFREE, 700));
    CHECK(std::fabs(tut::ghostCmd(tut::L_HOOKPLAYER, 1000).pitch) < 1e-6f);   // (kAimAt is a marker, never a pitch)
    {   // aimAt: the yaw convention of kStartYaw (+x) and kPartnerYaw (-x)
        float y, p;
        tut::aimAt({0, 0, 0}, {10, 0, 0}, y, p);
        CHECK(std::fabs(y - tut::kStartYaw) < 1e-5f && std::fabs(p) < 1e-5f);
        tut::aimAt({0, 0, 0}, {-10, 10, 0}, y, p);
        CHECK(std::fabs(y - tut::kPartnerYaw) < 1e-5f && std::fabs(p - PI * 0.25f) < 1e-5f);
    }
    CHECK(tut::lessonFromName("rocketgrapple") == tut::L_ROCKETGRAPPLE);
    // (yaw: the strafe script turns both ways and ends near where it started)
    {
        float lo = 0, hi = 0;
        for (int t = 0; t < tut::loopMs(tut::L_STRAFE); t += 10) { float y = tut::ghostCmd(tut::L_STRAFE, t).yaw - tut::kStartYaw; lo = std::min(lo, y); hi = std::max(hi, y); }
        CHECK(lo < -0.3f && hi > 0.3f);
    }
    // the map: loads, hidden from every map list, each bay's two starts standing room on its floor
    Map m;
    std::string err;
    CHECK(m.load(tut::kMapName, &err));
    if (!err.empty()) printf("  %s\n", err.c_str());
    CHECK(mapHiddenFromLists(tut::kMapName) && !mapHiddenFromLists("sewer"));
    for (int l = 0; l < tut::L_COUNT; l++)
        for (Vec3 p : {tut::ghostStart(l), tut::playerStart(l), tut::lesson(l).partner ? tut::partnerStart(l) : tut::playerStart(l)}) {
            const bool room = !m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false)) && m.boxSolid(playerBox(p - Vec3(0, 0.05f, 0), false));
            if (!room) printf("  %s: no floor / room at %.1f %.1f %.1f\n", tut::lesson(l).id, p.x, p.y, p.z);
            CHECK(room);
            // the bay is its own: its far wall where the lesson's length says
            const float far = m.raycast(p + Vec3(0, 1.0f, 0), {1, 0, 0}, 400.0f);
            const float want = tut::bayX0(l) + tut::lesson(l).len - p.x;
            if (l != tut::L_MOVE && l != tut::L_GRAPPLE && l != tut::L_ROCKETJUMP && l != tut::L_STICKYJUMP && l != tut::L_RAMPDASH && std::fabs(far - want) > 0.1f)
                { printf("  %s: the far wall %.1f m ahead, the lesson says %.1f\n", tut::lesson(l).id, far, want); g_fail++; }
        }
    // the check on made-up movement: a strafe speed with a dash in it doesn't count; landing on the floor before the gap
    // starts a new try; side dashes need both sides
    {
        tut::Check c;
        c.start(tut::L_STRAFE);
        MoveState s; s.flags = 0; s.vel = {13, 0, 0};
        c.frame(s, ME_DASH, 0, 0, 0, 0.01f);
        CHECK(!c.done);
        s.flags = MF_ONGROUND; s.vel = {};
        for (int i = 0; i < 40; i++) c.frame(s, 0, 0, 0, 0, 0.01f);   // (stood still: a new try)
        s.flags = 0; s.vel = {13, 0, 0};
        c.frame(s, 0, 0, 127, 0, 0.01f);
        CHECK(c.done);
        c.start(tut::L_SIDEDASH);
        c.frame(s, ME_DASH, 0, 127, 0, 0.01f);
        c.frame(s, ME_DASH, 127, -127, 0, 0.01f);   // (forward held too: not a side dash)
        CHECK(!c.done && c.count == 1);
        c.frame(s, ME_DASH, 0, -127, 0, 0.01f);
        CHECK(c.done);
        c.start(tut::L_ROCKETJUMP);   // a jump isn't a blast; a blast is
        MoveState q; q.flags = MF_ONGROUND;
        c.frame(q, 0, 0, 0, WPN_ROCKET, 0.01f);
        q.flags = 0; q.vel.y = 7.6f;
        c.frame(q, ME_JUMP, 0, 0, WPN_ROCKET, 0.01f);
        CHECK(!c.knocked);
        q.vel.y = 16.0f;
        c.frame(q, 0, 0, 0, WPN_ROCKET, 0.01f);
        CHECK(c.knocked && !c.done);
        q.pos.y = tut::lesson(tut::L_ROCKETJUMP).target + 0.2f;
        c.frame(q, 0, 0, 0, WPN_ROCKET, 0.01f);
        CHECK(c.done);
    }
    {   // grapple a player: the map or a rocket isn't one; dash out of a grapple: a dash with nobody holding you isn't one,
        // nor is being let go without a dash
        tut::Check c;
        c.start(tut::L_HOOKPLAYER);
        MoveState m; m.flags = MF_HOOKED; m.hookTarget = 0;
        c.frame(m, ME_HOOK, 0, 0, 0, 0.01f);
        m.hookTarget = kHookRocket;
        c.frame(m, 0, 0, 0, 0, 0.01f);
        CHECK(!c.done);
        m.hookTarget = 3;
        c.frame(m, 0, 0, 0, 0, 0.01f);
        CHECK(c.done);
        c.start(tut::L_BREAKFREE);
        MoveState b; b.flags = 0;
        c.frame(b, ME_DASH, 0, -127, 0, 0.01f);
        b.flags = MF_PULLED;
        c.frame(b, 0, 0, 0, 0, 0.01f);
        b.flags = 0;
        c.frame(b, 0, 0, 0, 0, 0.01f);   // (let go: met, or out of reach)
        CHECK(!c.done);
        b.flags = MF_PULLED;
        c.frame(b, 0, 0, 0, 0, 0.01f);
        b.flags = 0;
        c.frame(b, ME_DASH, 0, -127, 0, 0.01f);
        CHECK(c.done);
    }
}

// Every lesson's ghost, as the tutorial's server plays it: the lesson's check, fed the ghost's own movement each tick
// (what the game feeds it from yours each frame), is passed within one loop. The launch's numbers are printed and
// checked against the same launch without the rocket (q: "grapple to 24m/s, rocket straight down, hyper straight down").
inline void testTutorialGhosts(uint16_t port) {
    printf("tutorial: every lesson's ghost does its tech on the map\n");
    SimClock clock;
    Server s;
    ServerConfig cfg;
    cfg.map = tut::kMapName; cfg.mode = MODE_FFA; cfg.port = port; cfg.tutorial = true; cfg.maxPlayers = 3;   // (you, the ghost, the partner)
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const int g = s.tutGhost_;
    CHECK(g >= 0 && s.players_[g].isBot && s.players_[g].name == "GHOST");
    if (g < 0) { s.stop(); return; }
    simRun(s, 1.0);
    CHECK(s.ms_.phase == PHASE_WARMUP && s.players_[g].alive);
    struct Run { bool done = false; float best = 0, peak = 0, top = 0, dist = 0, launchUp = 0, launchH = 0, up = 0, turn = 0; int doneMs = -1; };
    auto play = [&](int l, bool noFire) {
        Run r;
        s.tutLesson_ = l;
        s.tutEpoch_ = s.timeMs_ + 5;
        s.tutLoop_ = -2;
        tut::Check c;
        c.start(l);
        simRun(s, 0.004);   // (the reset at the start)
        const ServerPlayer& p = s.players_[g];
        const Vec3 start = p.move.pos;
        Vec3 launchAt;
        bool launched = false;
        const int loop = tut::loopMs(l);
        simRun(s, loop * 0.001 - 0.02, [&] {
            if (noFire) s.players_[g].nextFire = s.time() + 10;
            const int t = (int)((int64_t)s.timeMs_ - (int64_t)s.tutEpoch_) - 1;
            const UserCmd cmd = tut::ghostCmd(l, t);
            c.frame(p.move, p.move.events, cmd.forward, cmd.side, p.weapon, 1.0f / s.tickRate());
            if (c.done && r.doneMs < 0) r.doneMs = t;
            if (const char* tr = std::getenv("TUT_TRACE"); tr && std::string(tr) == tut::lesson(l).id && (t % 20 == 0 || p.move.events))
                printf("    %5d ms  x %6.2f y %5.2f z %5.2f  h %5.2f vy %6.2f  %s%s%s ev %2d ch %d cd %3d slide %d bnc %d  [%d %d %d]\n", t, p.move.pos.x - tut::bayX0(l),
                       p.move.pos.y, p.move.pos.z, std::sqrt(p.move.vel.x * p.move.vel.x + p.move.vel.z * p.move.vel.z), p.move.vel.y,
                       (p.move.flags & MF_ONGROUND) ? "G" : "-", (p.move.flags & MF_HOOKED) ? "H" : "-", p.move.dashTimer > 0 ? "D" : "-",
                       p.move.events, p.move.dashCharges, p.move.dashCooldownMs, (int)p.move.dashSlide, p.move.dashBounces, cmd.forward, cmd.side, cmd.buttons);
            r.best = std::max(r.best, c.best);
            r.peak = std::max(r.peak, std::sqrt(p.move.vel.x * p.move.vel.x + p.move.vel.z * p.move.vel.z));
            r.top = std::max(r.top, p.move.pos.y);
            r.up = std::max(r.up, p.move.vel.y);
            if (std::sqrt(p.move.vel.x * p.move.vel.x + p.move.vel.z * p.move.vel.z) > 20) r.turn = std::max(r.turn, std::fabs(std::atan2(p.move.vel.z, p.move.vel.x)) * 180.0f / PI);   // (from +x)
            r.dist = std::max(r.dist, p.move.pos.x - start.x);
            if ((p.move.events & ME_HYPER) && !launched) { launched = true; launchAt = p.move.pos; }
            if (launched && p.move.vel.y > r.launchUp) { r.launchUp = p.move.vel.y; r.launchH = std::sqrt(p.move.vel.x * p.move.vel.x + p.move.vel.z * p.move.vel.z); }
        });
        r.done = c.done;
        if (launched) r.dist = p.move.pos.x - launchAt.x;
        printf("  %-11s %s at %5d ms of %5d: best %.2f (needs %.2f), top speed %.1f m/s, highest %.1f m, %.1f m on\n", tut::lesson(l).id,
               c.done ? "done" : "MISSED", r.doneMs, loop, c.best, tut::lesson(l).target, r.peak, r.top, r.dist);
        return r;
    };
    for (int l = 0; l < tut::L_COUNT; l++) {
        const Run r = play(l, false);
        CHECK(r.done);
        CHECK(s.players_[g].alive);   // (no lava, no fall out)
        // the combos' numbers (a research run measured them through the same server; the lessons' texts quote them)
        auto near = [&](const char* what, float got, float want, float tol) {
            printf("  %-11s %s %.2f (measured %.1f +- %.1f)\n", tut::lesson(l).id, what, got, want, tol);
            CHECK(std::fabs(got - want) <= tol);
        };
        if (l == tut::L_POP) { near("up m/s", r.up, 26.1f, 1.0f); near("high m", r.top, 14.2f, 1.0f); }
        if (l == tut::L_ROCKETPOP) { near("up m/s", r.up, 47.4f, 3.0f); near("high m", r.top, 48.0f, 4.0f); }
        if (l == tut::L_STICKYSTACK) { near("up m/s", r.up, 50.5f, 1.5f); near("high m", r.top, 53.2f, 2.0f); }
        if (l == tut::L_BUBBLELAUNCH) { near("up m/s", r.up, 58.4f, 1.5f); near("high m", r.top, 72.8f, 2.0f); }
        if (l == tut::L_ROCKETSUPER) { near("level m/s", r.peak, 33.3f, 0.6f); CHECK(r.peak > 32.0f); }   // (past the dash's 32 cap)
        if (l == tut::L_HOOKBOOST) { near("level m/s", r.peak, 40.4f, 1.0f); }
        if (l == tut::L_HOOKCHAIN) { near("level m/s", r.peak, 35.0f, 0.5f); CHECK(r.doneMs > 0 && r.doneMs < 3000); }   // (34 from the hook's 24 in ~1.3 s)
        if (l == tut::L_SWING) { near("turned deg", r.turn, 41.0f, 4.0f); CHECK(r.peak < 24.5f); }   // (round the pillar at its speed)
        if (l == tut::L_RAMPDASH) near("high m", r.top, 9.0f, 0.5f);
        if (l == tut::L_FALLBOUNCE) { near("hop up m/s", r.best, 15.7f, 0.2f); CHECK(r.top > 11.7f); }
        if (l == tut::L_LAUNCH) {
            const Run bare = play(l, true);   // the same, the rocket held back: the hyper alone
            printf("  launch: up %.1f m/s and %.1f m/s level off the floor, %.1f m high, %.1f m on (the hyper alone: up %.1f, %.1f m high)\n",
                   r.launchUp, r.launchH, r.top, r.dist, bare.launchUp, bare.top);
            CHECK(r.launchUp > bare.launchUp + 5 && r.top > bare.top + 4);   // (the rocket is what makes it a launch)
            CHECK(!bare.done);                                               // (and the lesson asks for it)
        }
    }
    s.stop();
}

// The PARTNER's lessons with the real grapple (q: "grappling other players, and when you get grappled, dashing out of
// it"): the ghost's hook latches onto the running partner and the server holds the latch both ways; the partner's hook
// on you pulls you, and a dash breaks it at once (pmove clears MF_PULLED, the server ends the latch the same tick and
// the partner's hook goes to its cooldown); with no dash the pull holds as long as the partner holds its grapple. Who may hook whom: only the
// lesson's pair, and in no other lesson. The numbers are printed for the changelog.
inline void testTutorialPartner(uint16_t port) {
    printf("tutorial: the partner's grapple lessons, the real latch\n");
    SimClock clock;
    Server s;
    ServerConfig cfg;
    cfg.map = tut::kMapName; cfg.mode = MODE_FFA; cfg.port = port; cfg.tutorial = true; cfg.maxPlayers = 4;   // (+ a bot as you)
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const int g = s.tutGhost_, pa = s.tutPartner_;
    CHECK(g >= 0 && pa >= 0 && s.players_[pa].isBot && s.players_[pa].name == "PARTNER");
    if (g < 0 || pa < 0) { s.stop(); return; }
    // "you": a bot standing in for the player (the server takes the commands the same way), its input set per run
    const int me = s.addBot(TEAM_A, BOT_BEGINNER);
    CHECK(me >= 0);
    if (me < 0) { s.stop(); return; }
    int dashAt = -1;   // ms after the partner's hook catches you: dash (left); -1: never
    uint64_t caughtMs = 0;
    s.bots_[me].script = [&](Server& sv, int id) {
        UserCmd c; c.yaw = tut::kStartYaw;
        const ServerPlayer& p = sv.players_[id];
        if ((p.move.flags & MF_PULLED) && caughtMs == 0) caughtMs = sv.timeMs_;
        if (dashAt >= 0 && caughtMs && sv.timeMs_ >= caughtMs + (uint64_t)dashAt && sv.timeMs_ < caughtMs + (uint64_t)dashAt + 100) { c.side = -127; c.buttons = BTN_DASH; }
        return c;
    };
    s.tutPlayer_ = me;
    simRun(s, 1.0);
    auto lessonStart = [&](int l, bool tryPhase) {
        s.tutLesson_ = l; s.tutEpoch_ = s.timeMs_ + 5; s.tutLoop_ = -2; s.tutTry_ = tryPhase;
        s.spawnPlayer(me);
        simRun(s, 0.004);
    };
    // nobody may hook anybody outside the partner's two lessons; in them only the lesson's pair
    lessonStart(tut::L_GRAPPLE, false);
    CHECK(s.players_[pa].team == TEAM_SPECTATOR && !s.players_[pa].alive);   // (not in this lesson: watching)
    CHECK(!rulesCanDamage(s, g, me, kDamageGrapple) && !rulesCanDamage(s, me, g, kDamageGrapple) && !rulesCanDamage(s, pa, me, kDamageGrapple));
    lessonStart(tut::L_HOOKPLAYER, false);
    CHECK(s.players_[pa].alive && s.players_[pa].team != TEAM_SPECTATOR);
    CHECK(rulesCanDamage(s, me, pa, kDamageGrapple) && rulesCanDamage(s, g, pa, kDamageGrapple));
    CHECK(!rulesCanDamage(s, g, me, kDamageGrapple) && !rulesCanDamage(s, pa, me, kDamageGrapple) && !rulesCanDamage(s, me, g, kDamageGrapple));
    // grapple a player: the ghost latches onto the running partner, both are pulled together, the partner takes no damage
    {
        bool latched = false;
        float runSpeed = 0, top = 0, healthLost = 0;
        int latchMs = -1, metMs = -1;
        simRun(s, tut::loopMs(tut::L_HOOKPLAYER) * 0.001 - 0.02, [&] {
            const ServerPlayer& G = s.players_[g];
            const ServerPlayer& P = s.players_[pa];
            const int t = (int)((int64_t)s.timeMs_ - (int64_t)s.tutEpoch_);
            const bool on = G.hookVictim == pa && P.hookedBy == g && (P.move.flags & MF_PULLED) && (G.move.flags & MF_HOOKED) && G.move.hookTarget == pa + 1;
            if (on && !latched) { latched = true; latchMs = t; runSpeed = std::sqrt(P.move.vel.x * P.move.vel.x + P.move.vel.z * P.move.vel.z); }
            if (latched && !on && metMs < 0) metMs = t;
            if (on) top = std::max(top, length(G.move.vel));
            healthLost = std::max(healthLost, s.lifeMaxHealth() - P.health);
        });
        printf("  hookplayer: latched at %d ms onto the partner running at %.1f m/s, pulled up to %.1f m/s, let go at %d ms (met)\n", latchMs, runSpeed, top, metMs);
        CHECK(latched && runSpeed > 3.0f && top > 15.0f && metMs > latchMs && healthLost == 0);
    }
    // dash out of a grapple, the demo: the partner hooks the ghost, the ghost dashes and is free that tick
    lessonStart(tut::L_BREAKFREE, false);
    CHECK(rulesCanDamage(s, pa, g, kDamageGrapple) && rulesCanDamage(s, pa, me, kDamageGrapple) && !rulesCanDamage(s, g, pa, kDamageGrapple));
    {
        int caught = -1, freed = -1, dashT = -1;
        float before = 0, after = 0, cooldown = 0;
        simRun(s, tut::loopMs(tut::L_BREAKFREE) * 0.001 - 0.02, [&] {
            const ServerPlayer& G = s.players_[g];
            const ServerPlayer& P = s.players_[pa];
            const int t = (int)((int64_t)s.timeMs_ - (int64_t)s.tutEpoch_);
            const bool held = G.hookedBy == pa && (G.move.flags & MF_PULLED);
            if (held && caught < 0) caught = t;
            if (held) before = length(G.move.vel);
            if ((G.move.events & ME_DASH) && dashT < 0) { dashT = t; after = length(G.move.vel); }
            if (caught >= 0 && !held && freed < 0) { freed = t; cooldown = P.move.hookCooldown; CHECK(P.hookVictim < 0 && !(P.move.flags & MF_HOOKED)); }
        });
        printf("  breakfree (ghost): caught at %d ms, pulled at %.1f m/s, dashed at %d ms (%.1f m/s), free at %d ms, the partner's hook cooldown %.2f s\n",
               caught, before, dashT, after, freed, cooldown);
        CHECK(caught >= 0 && dashT > caught && freed == dashT && before > 5.0f && cooldown > 0.9f);
    }
    // your turn: the partner hooks you. No dash: pulled toward it as long as it holds its grapple (0.9 s); a dash 300 ms
    // in: free that tick
    for (int run = 0; run < 2; run++) {
        dashAt = run == 0 ? -1 : 300;
        caughtMs = 0;
        lessonStart(tut::L_BREAKFREE, true);
        tut::Check c;
        c.start(tut::L_BREAKFREE);
        int caught = -1, freed = -1;
        float top = 0, travelled = 0;
        Vec3 at;
        simRun(s, tut::loopMs(tut::L_BREAKFREE) * 0.001 - 0.02, [&] {
            const ServerPlayer& M = s.players_[me];
            const int t = (int)((int64_t)s.timeMs_ - (int64_t)s.tutEpoch_);
            const bool held = M.hookedBy == pa && (M.move.flags & MF_PULLED);
            if (held && caught < 0) { caught = t; at = M.move.pos; }
            if (held) { top = std::max(top, length(M.move.vel)); travelled = length(M.move.pos - at); }
            if (caught >= 0 && !held && freed < 0) freed = t;
            c.frame(M.move, M.move.events, 0, -127, M.weapon, 1.0f / s.tickRate());
        });
        printf("  breakfree (you, %s): caught at %d ms, pulled up to %.1f m/s over %.1f m, free at %d ms; the lesson %s\n",
               run == 0 ? "no dash" : "a dash 300 ms in", caught, top, travelled, freed, c.done ? "done" : "not done");
        CHECK(caught >= 0 && s.players_[g].hookedBy < 0);   // (your turn: the partner's hook is for you, not the ghost)
        if (run == 0) CHECK(!c.done && freed > caught + 500 && travelled > 10.0f);   // (held till it lets go)
        else CHECK(c.done && freed >= caught + 300 && freed <= caught + 320);
    }
    // grapple a player, then dash while your hook holds them: the latch holds (only the held one's dash breaks it) and
    // they're dragged after you (q's "dash while hooked drags them")
    {
        lessonStart(tut::L_HOOKPLAYER, true);
        uint64_t latchAt = 0;
        const uint64_t from = s.timeMs_ + 200;   // (the partner at its start first: a hook fired before that takes the wall)
        s.bots_[me].script = [&](Server& sv, int id) {
            UserCmd c; c.yaw = tut::kStartYaw;
            const ServerPlayer& p = sv.players_[id];
            const ServerPlayer& P = sv.players_[pa];
            tut::aimAt(p.move.pos + Vec3(0, kEyeHeight, 0), P.move.pos + Vec3(0, 1.0f, 0), c.yaw, c.pitch);
            if (P.hookedBy == id && !latchAt) latchAt = sv.timeMs_;
            c.buttons = sv.timeMs_ >= from ? BTN_GRAPPLE : 0;
            if (latchAt && sv.timeMs_ >= latchAt + 150 && sv.timeMs_ < latchAt + 250) { c.side = 127; c.buttons |= BTN_DASH; }   // (a dash sideways)
            return c;
        };
        bool dashed = false, heldAfter = false;
        float partnerSide = 0;
        Vec3 meAtDash;
        simRun(s, 1.0, [&] {
            const ServerPlayer& M = s.players_[me];
            const ServerPlayer& P = s.players_[pa];
            if ((M.move.events & ME_DASH) && latchAt && !dashed) { dashed = true; meAtDash = M.move.pos; }
            if (dashed && s.timeMs_ < latchAt + 400) {
                heldAfter = P.hookedBy == me && (P.move.flags & MF_PULLED);
                partnerSide = std::max(partnerSide, std::fabs(P.move.vel.z));
            }
        });
        printf("  hookplayer + a dash while holding: latched %s, dashed %s, still held 400 ms on %s, the partner's sideways speed up to %.1f m/s\n",
               latchAt ? "yes" : "no", dashed ? "yes" : "no", heldAfter ? "yes" : "no", partnerSide);
        CHECK(latchAt && dashed && heldAfter && partnerSide > 1.5f);   // (pulled toward where you are now: 2.5 m/s sideways after a sideways dash)
    }
    s.stop();
}

// The rules and variants the combo lessons' texts state, through the same server (the ghost given other inputs): one,
// two and three stickies; the rocket pop's timing (fired with the dash vs 10 / 20 ms late); a dash right after your own
// blast erases its push; an air dash above 32 m/s caps you to 32 (the rocket super's warning).
inline void testTutorialCombos(uint16_t port) {
    printf("tutorial: the combos' rules and variants\n");
    SimClock clock;
    Server s;
    ServerConfig cfg;
    cfg.map = tut::kMapName; cfg.mode = MODE_FFA; cfg.port = port; cfg.tutorial = true; cfg.maxPlayers = 3;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const int g = s.tutGhost_;
    if (g < 0) { s.stop(); return; }
    simRun(s, 1.0);
    std::vector<tut::Seg> sc;
    uint8_t weapon = kNoWeaponChange;
    uint64_t t0 = 0;
    int fireOnHyper = -1, hyperAt = -1;   // (>= 0: fire this many ms after the hyper, as a player reacting to it)
    s.bots_[g].script = [&](Server& sv, int) {
        UserCmd c; c.yaw = tut::kStartYaw; c.weapon = weapon;
        const int t = (int)((int64_t)sv.timeMs_ - (int64_t)t0);
        int a = 0;
        for (size_t i = 0; i < sc.size(); i++) {
            if (t < a + sc[i].ms || i + 1 == sc.size()) {
                c.forward = sc[i].fwd; c.side = sc[i].side; c.buttons = sc[i].btn; c.pitch = sc[i].pitch * (PI / 180.0f);
                break;
            }
            a += sc[i].ms;
        }
        if (fireOnHyper >= 0 && hyperAt >= 0 && t >= hyperAt + fireOnHyper && t < hyperAt + fireOnHyper + 40) c.buttons |= BTN_FIRE;
        return c;
    };
    struct Out { float up = 0, top = 0, level = 0, lastLevel = 0, minHp = 1e9f; };
    auto play = [&](int lesson, uint8_t wpn, std::vector<tut::Seg> script) {
        s.tutLesson_ = lesson; s.tutEpoch_ = s.timeMs_; s.tutLoop_ = -2;
        weapon = wpn; sc = std::move(script); hyperAt = -1;
        s.tutorialLoop();      // (the reset: back at the lesson's start, its weapon)
        s.tutLesson_ = -1;     // (no more resets: the run is ours)
        t0 = s.timeMs_;
        int total = 0;
        for (const tut::Seg& x : sc) total += x.ms;
        Out o;
        const ServerPlayer& p = s.players_[g];
        simRun(s, total * 0.001, [&] {
            if ((p.move.events & ME_HYPER) && hyperAt < 0) hyperAt = (int)((int64_t)s.timeMs_ - (int64_t)t0);
            const float h = std::sqrt(p.move.vel.x * p.move.vel.x + p.move.vel.z * p.move.vel.z);
            o.up = std::max(o.up, p.move.vel.y); o.top = std::max(o.top, p.move.pos.y); o.level = std::max(o.level, h); o.lastLevel = h;
            o.minHp = std::min(o.minHp, p.health);
        });
        return o;
    };
    constexpr int8_t F = 127;
    constexpr uint16_t J = BTN_JUMP, D = BTN_DASH, FI = BTN_FIRE, AL = BTN_ALTFIRE;
    // stickies: 1, 2, 3 at your feet, then jump + set off
    const float stickyUp[3] = {21.9f, 36.2f, 50.5f};
    for (int n = 1; n <= 3; n++) {
        std::vector<tut::Seg> v = {{300, 0, 0, 0, 0, -89}};
        for (int k = 0; k < n; k++) { v.push_back({60, 0, 0, FI, 0, -89}); v.push_back({700, 0, 0, 0, 0, -89}); }
        v.push_back({80, 0, 0, J | AL, 0, -89});
        v.push_back({3000, 0, 0, 0, 0, 0});
        const Out o = play(tut::L_STICKYSTACK, WPN_STICKY, v);
        printf("  sticky stack x%d: up %.1f m/s, %.1f m high (measured %.1f)\n", n, o.up, o.top, stickyUp[n - 1]);
        CHECK(std::fabs(o.up - stickyUp[n - 1]) <= 1.5f);
    }
    // the rocket pop's timing: a super, the down-dash held, the rocket fired 0 / 10 / 20 ms after the hyper
    float popUp[3] = {};
    for (int k = 0; k < 3; k++) {
        fireOnHyper = k * 10;
        const Out o = play(tut::L_ROCKETPOP, WPN_ROCKET, {{300, 0, 0, 0, 0, 0}, {30, F, 0, D, 0, 0}, {80, F, 0, D | J, 0, 0}, {220, F, 0, 0, 0, -89},
                                                          {600, F, 0, D | J, 0, -89}, {3000, 0, 0, 0, 0, -89}});
        popUp[k] = o.up;
        printf("  rocket pop, fired %d ms after the hyper: up %.1f m/s, %.1f m high\n", k * 10, o.up, o.top);
    }
    fireOnHyper = -1;
    CHECK(popUp[0] > 44.0f && popUp[0] - popUp[1] > 2.0f && popUp[1] - popUp[2] > 2.0f);   // (strict: ~3 m/s a 10 ms)
    // a dash right after your own blast erases its push: a rocket at your feet, then a dash 35 ms on
    const Out rj = play(tut::L_ROCKETJUMP, WPN_ROCKET, {{600, 0, 0, 0, 0, -89}, {60, 0, 0, FI, 0, -89}, {2500, 0, 0, 0, 0, 0}});
    const Out eaten = play(tut::L_ROCKETJUMP, WPN_ROCKET, {{600, 0, 0, 0, 0, -89}, {20, 0, 0, FI, 0, -89}, {15, 0, 0, 0, 0, -89}, {150, F, 0, D, 0, 0}, {2000, 0, 0, 0, 0, 0}});
    printf("  rocket at your feet: %.1f m high; with a dash 35 ms after the shot: %.1f m high\n", rj.top, eaten.top);
    CHECK(rj.top > 12.0f && eaten.top < 2.0f);
    // the rocket super, then an air dash: the 33 m/s is capped back to 32
    const Out capped = play(tut::L_ROCKETSUPER, WPN_ROCKET, {{300, 0, 0, 0, 0, 0}, {30, F, 0, D, 0, 0}, {80, F, 0, D | J, 0, 0}, {50, F, 0, 0, 0, -89},
                                                            {40, F, 0, FI, 0, -89}, {600, F, 0, D, 0, 0}, {30, 0, 0, 0, 0, 0}});
    printf("  rocket super %.1f m/s, then an air dash: %.1f m/s\n", capped.level, capped.lastLevel);
    CHECK(capped.level > 32.5f && std::fabs(capped.lastLevel - 32.0f) < 0.3f);
    // the bubble launch (q: "if it doesnt [kill you] thats cool"): standing, a jump first, from a run; then with "you" (a
    // bot) in your lane 12 m off: no enemy in the tutorial, the full stream still; then out of the tutorial's rules with
    // an enemy 3 m off: the stream is 5 bubbles. And what it costs.
    const std::vector<tut::Seg> stand = {{600, 0, 0, 0, 0, -89}, {60, 0, 0, AL, 0, -89}, {3000, 0, 0, 0, 0, 0}};
    const Out bl = play(tut::L_BUBBLELAUNCH, WPN_BUBBLE, stand);
    const Out blJump = play(tut::L_BUBBLELAUNCH, WPN_BUBBLE, {{600, 0, 0, 0, 0, -89}, {40, 0, 0, AL | J, 0, -89}, {3000, 0, 0, 0, 0, 0}});
    const Out blRun = play(tut::L_BUBBLELAUNCH, WPN_BUBBLE, {{700, F, 0, 0, 0, -89}, {40, F, 0, AL | J, 0, -89}, {1500, F, 0, 0, 0, -89}});
    printf("  bubble launch standing: up %.1f m/s, %.1f m high; a jump first: up %.1f; from a run: up %.1f, %.1f m/s along\n",
           bl.up, bl.top, blJump.up, blRun.up, blRun.level);
    CHECK(std::fabs(bl.up - 58.4f) <= 1.5f && std::fabs(blJump.up - 48.0f) <= 1.5f && blJump.up < bl.up - 6);
    CHECK(std::fabs(blRun.up - 42.6f) <= 1.5f && std::fabs(blRun.level - 25.3f) <= 1.0f);
    const int me = s.addBot(TEAM_A, BOT_BEGINNER);
    CHECK(me >= 0);
    if (me >= 0) {
        s.bots_[me].script = [](Server&, int) { UserCmd c; c.yaw = tut::kStartYaw; return c; };
        simRun(s, 0.5);
        auto put = [&](const Vec3& at) { s.players_[me].move.pos = at; s.players_[me].move.vel = {}; };
        const Vec3 gs = tut::ghostStart(tut::L_BUBBLELAUNCH);
        put(tut::playerStart(tut::L_BUBBLELAUNCH));
        const Out lane = play(tut::L_BUBBLELAUNCH, WPN_BUBBLE, stand);
        s.cfg_.tutorial = false;   // (the real rules: an enemy, damage)
        put(gs + Vec3(3, 0, 0));
        const Out close = play(tut::L_BUBBLELAUNCH, WPN_BUBBLE, stand);
        put(gs + Vec3(0, 0, 40));   // (out of range: the other side of the bay's wall, 40 m off)
        s.players_[g].health = s.lifeMaxHealth();
        const Out alone = play(tut::L_BUBBLELAUNCH, WPN_BUBBLE, stand);
        s.cfg_.tutorial = true;
        printf("  bubble launch, \"you\" in your lane: up %.1f m/s; out of the tutorial: an enemy 3 m off: up %.1f m/s; none: up %.1f, %.1f hp\n",
               lane.up, close.up, alone.up, s.lifeMaxHealth() - alone.minHp);
        CHECK(s.players_[me].alive && std::fabs(lane.up - bl.up) < 0.5f);
        CHECK(close.up < bl.up - 15);
        CHECK(std::fabs(alone.up - bl.up) < 0.5f && std::fabs(s.lifeMaxHealth() - alone.minHp - 11.9f) <= 1.5f);
    }
    s.stop();
}
