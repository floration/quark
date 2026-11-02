// AIM TRAINER (src/game/trainer.h): the scenario table matches the owner's spec (6 per mode, category and difficulty),
// every scenario runs its 30 s, its targets stay where the spec puts them, a perfect aim scores and no aim doesn't.
#pragma once
#include "../src/game/trainer.h"
#include <set>

inline void testTrainer() {
    using namespace trainer;
    printf("trainer\n");
    std::set<std::string> ids;
    for (auto& s : scenarios()) { CHECK(ids.insert(s.id).second); CHECK(findScenario(s.id) == &s); }
    for (int m = 0; m < TR_MODES; m++)
        for (int c = 0; c < CAT_COUNT; c++) {
            const auto l = list(m, c);
            CHECK(l.size() == 6);   // (owner: "6 scenarios per category per mode per difficulty")
            for (auto* s : l) CHECK(s->laser == (c != CAT_CLICK));   // (clicking: the rifle; switching, tracking: the laser)
        }
    for (auto& s : scenarios())
        for (int d = 0; d < kDiffs; d++) {
            for (int aimMode = 0; aimMode < 2; aimMode++) {   // 0: a perfect aim, 1: aiming at nothing
                Run run;
                run.start(s, d, 0x5EED0000u + d * 7 + aimMode);
                CHECK((int)run.targets().size() == s.count[d]);
                const Room rm = run.room();
                float minAir = 1e9f, minH = 1e9f, maxH = -1e9f, minShell = 1e9f, maxShell = -1e9f;
                bool offWall = false, outside = false, badR = false;
                const float dt = 1.0f / 240;
                int steps = 0;
                while (!run.finished() && steps < 240 * 40) {
                    Vec3 aim = {0, 1, 0};   // (straight up: nothing there)
                    bool seen = aimMode == 1;   // (no aim: fires anyway; a perfect one only at a target it sees)
                    if (aimMode == 0) {
                        for (auto& t : run.targets()) if (t.hide <= 0) { aim = normalize(t.pos); seen = true; break; }
                    }
                    run.step(dt, aim, seen, seen);
                    steps++;
                    for (auto& t : run.targets()) {
                        if (t.hide > 0) continue;
                        if (s.motion != MO_PULSE && std::fabs(t.r - s.width[d] * 0.5f) > 1e-4f) badR = true;
                        if (run.flat() && std::fabs(t.pos.z - (kWallZ + t.r)) > 1e-3f) offWall = true;
                        const float m = t.r + 0.05f;
                        if (t.pos.x < rm.mn.x - m || t.pos.x > rm.mx.x + m || t.pos.y < rm.mn.y - m || t.pos.y > rm.mx.y + m || t.pos.z < rm.mn.z - m || t.pos.z > rm.mx.z + m) outside = true;
                        if (s.motion == MO_AIR && !run.flat()) minAir = std::min(minAir, length(t.pos));
                        if (s.motion == MO_BOUNCY) { minH = std::min(minH, t.pos.y - kArenaFloor); maxH = std::max(maxH, t.pos.y - kArenaFloor); }
                        if (s.motion == MO_SHELL) { minShell = std::min(minShell, length(t.pos)); maxShell = std::max(maxShell, length(t.pos)); }
                    }
                }
                CHECK(run.finished());
                CHECK(std::fabs(steps * dt - (kCountdown + kRunTime)) < 0.05f);
                CHECK(!offWall);
                CHECK(!outside);
                CHECK(!badR);
                if (s.motion == MO_AIR && !run.flat()) CHECK(minAir >= 10.0f - 1e-3f);   // (owner: at least 10 m away)
                if (s.motion == MO_BOUNCY) { CHECK(minH >= 6.0f - 1e-3f); CHECK(maxH <= 24.0f + 0.1f); }   // (owner: 6..24 m high)
                if (s.motion == MO_SHELL) { CHECK(minShell >= 12.0f - 0.05f); CHECK(maxShell <= 24.0f + 0.05f); }   // (owner: 12..24 m)
                if (aimMode == 0) {
                    if (run.score() <= 0) printf("  FAIL trainer: %s %s scored %d with a perfect aim\n", s.id, diffName(d), run.score());
                    CHECK(run.score() > 0);
                    if (!s.laser && run.accuracy() <= 0.99f) printf("  FAIL trainer: %s %s accuracy %.3f (%d/%d)\n", s.id, diffName(d), run.accuracy(), run.stats().hits, run.stats().shots);
                    if (!s.laser) CHECK(run.accuracy() > 0.99f);
                    if (s.cat == CAT_TRACK && s.motion != MO_PULSE) {
                        if (run.accuracy() < 0.95f) printf("  FAIL trainer: %s %s tracked %.2f with a perfect aim\n", s.id, diffName(d), run.accuracy());
                        CHECK(run.accuracy() > 0.95f);
                    }
                    if (s.cat != CAT_TRACK) CHECK(run.stats().kills > 0);
                } else {
                    CHECK(run.score() == 0);
                    CHECK(run.stats().kills == 0);
                }
            }
        }
    {   // clicking: one shot a click, never faster than the rifle; held without new clicks fires nothing more
        Run run;
        run.start(*findScenario("3d-click-wallball"), 0, 7);
        for (int k = 0; k < 4 * 240; k++) run.step(1.0f / 240, {0, 1, 0}, false, false);   // (the countdown, then 1 s)
        const int before = run.stats().shots;
        int fired = 0;
        for (int k = 0; k < 240; k++) fired += run.step(1.0f / 240, {0, 1, 0}, true, k == 0).shot;
        CHECK(before == 0 && fired == 1);
        int clicks = 0;
        for (int k = 0; k < 240; k++) clicks += run.step(1.0f / 240, {0, 1, 0}, true, true).shot;   // (a click every step: the refire caps it)
        CHECK(clicks <= (int)(1.0f / kRifleRefire) + 1);
    }
    {   // the reactor: gone 1 s after a kill
        Run run;
        run.start(*findScenario("3d-click-reactor"), 0, 9);
        for (int k = 0; k < 3 * 240 + 2; k++) run.step(1.0f / 240, {0, 1, 0}, false, false);
        const Vec3 aim = normalize(run.targets()[0].pos);
        CHECK(run.step(1.0f / 240, aim, true, true).kills == 1);
        CHECK(run.targets()[0].hide > 0.9f);
    }
}
