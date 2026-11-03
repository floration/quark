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
    CHECK(tut::lesson(tut::L_LAUNCH).key);   // (q: the big one, marked KEY TECH)
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
        for (Vec3 p : {tut::ghostStart(l), tut::playerStart(l)}) {
            const bool room = !m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false)) && m.boxSolid(playerBox(p - Vec3(0, 0.05f, 0), false));
            if (!room) printf("  %s: no floor / room at %.1f %.1f %.1f\n", tut::lesson(l).id, p.x, p.y, p.z);
            CHECK(room);
            // the bay is its own: its far wall where the lesson's length says
            const float far = m.raycast(p + Vec3(0, 1.0f, 0), {1, 0, 0}, 400.0f);
            const float want = tut::bayX0(l) + tut::lesson(l).len - p.x;
            if (l != tut::L_MOVE && l != tut::L_GRAPPLE && l != tut::L_ROCKETJUMP && l != tut::L_STICKYJUMP && std::fabs(far - want) > 0.1f)
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
}

// Every lesson's ghost, as the tutorial's server plays it: the lesson's check, fed the ghost's own movement each tick
// (what the game feeds it from yours each frame), is passed within one loop. The launch's numbers are printed and
// checked against the same launch without the rocket (q: "grapple to 24m/s, rocket straight down, hyper straight down").
inline void testTutorialGhosts(uint16_t port) {
    printf("tutorial: every lesson's ghost does its tech on the map\n");
    SimClock clock;
    Server s;
    ServerConfig cfg;
    cfg.map = tut::kMapName; cfg.mode = MODE_FFA; cfg.port = port; cfg.tutorial = true; cfg.maxPlayers = 2;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const int g = s.tutGhost_;
    CHECK(g >= 0 && s.players_[g].isBot && s.players_[g].name == "GHOST");
    if (g < 0) { s.stop(); return; }
    simRun(s, 1.0);
    CHECK(s.ms_.phase == PHASE_WARMUP && s.players_[g].alive);
    struct Run { bool done = false; float best = 0, peak = 0, top = 0, dist = 0, launchUp = 0, launchH = 0; int doneMs = -1; };
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
