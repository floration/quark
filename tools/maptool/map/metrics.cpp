// metrics (time to points, routes, jump reach, the map report's numbers) and route (one walk grid route).
#include <algorithm>
#include <cmath>
#include "analysis.h"
#include "maptool.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"

static constexpr float kRun = 9.5f;   // pmove kGroundSpeed: walking distance -> seconds (the map report's kRunSpeed)

static Json jpath(const std::vector<Vec3>& p) { Json a = Json::array(); for (auto& v : p) a.push(jvec(v)); return a; }

// One objective's arrivals: each side's spawns, each spawn's walk (the report's nav graph distances, without clip),
// the side's mean (unreachable from any spawn of it: null, as the report reads it).
struct Side { std::vector<int> spawns; };
struct ObjectiveOut { Json obj; Json routes = Json::array(); };

static Vec3 floorCentre(const AABB& b) { return {b.center().x, b.mn.y, b.center().z}; }

// time to points for a mode: the report's lines (07ef254 mapreport.cpp measure(), "modes"), with every route
Json Analysis::objectives(int mode, Json& routes, bool withPaths) {
    Analysis& a = *this;
    const Map& m = a.m;
    const NavGraph& g = a.navOpen();
    std::vector<int> spA, spB, race;
    for (int i = 0; i < (int)m.spawns.size(); i++) {
        if (m.spawns[i].team == SPAWN_A) spA.push_back(i);
        if (m.spawns[i].team == SPAWN_B) spB.push_back(i);
        if (m.spawns[i].team == SPAWN_RACE) race.push_back(i);
    }
    Json objs = Json::array();
    char tb[16];
    // the side's mean walk (m) to a box / a point, each spawn's route listed; -1: some spawn can't get there
    std::string objective;   // the objectives[].name the routes being added measure
    auto sideTo = [&](const std::vector<int>& side, const char* role, const std::string& what, const AABB* box, const Vec3& pt) {
        float sum = 0; int n = 0; bool ok = !side.empty();
        for (int s : side) {
            std::vector<Vec3> path;
            const Vec3& from = m.spawns[s].pos;
            float d = box ? navRouteToBox(g, from, *box, withPaths ? &path : nullptr) : navRoute(g, from, pt, withPaths ? &path : nullptr);
            Json r = Json::object();
            r.set("from", s); r.set("team", spawnTeamName(m.spawns[s].team, tb)); r.set("role", role); r.set("to", what); r.set("objective", objective);
            r.set("metres", d >= 0 ? Json(d) : Json()); r.set("seconds", d >= 0 ? Json(d / kRun) : Json());
            if (withPaths) r.set("path", jpath(path));
            routes.push(std::move(r));
            if (d < 0) ok = false; else { sum += d; n++; }
        }
        return ok && n ? sum / n : -1.0f;
    };
    auto add = [&](const std::string& name, const char* kind, const Vec3& pos, float att, float def, const char* attWho, const char* defWho) {
        Json o = Json::object();
        o.set("name", name); o.set("kind", kind); o.set("pos", jvec(pos));
        o.set("attackersAre", attWho); o.set("defendersAre", defWho ? Json(defWho) : Json());
        o.set("attackersM", att >= 0 ? Json(att) : Json()); o.set("defendersM", def >= 0 ? Json(def) : Json());
        o.set("attackers", att >= 0 ? Json(att / kRun) : Json()); o.set("defenders", def >= 0 ? Json(def / kRun) : Json());
        // attackers' arrival minus defenders' (s): positive, the defenders are there first (owner B2: -1..+1 s at sites / points)
        o.set("diff", att >= 0 && def >= 0 ? Json((att - def) / kRun) : Json());
        objs.push(std::move(o));
    };
    auto staged = [&](size_t i, bool defending) {   // the report's pointDist: the stage that side spawns at for point i
        int stage = -1;
        for (auto& sp : m.spawns) if (isStagedSpawn(sp.team) && spawnDefends(sp.team) == defending && spawnStage(sp.team) <= (int)i) stage = std::max(stage, spawnStage(sp.team));
        std::vector<int> v;
        for (int k = 0; k < (int)m.spawns.size(); k++) if (m.spawns[k].team == stagedSpawn(std::max(stage, 0), defending)) v.push_back(k);
        return v;
    };
    char nb[64];
    switch (mode) {
        case MODE_BOMB:
            for (size_t k = 0; k < m.bombSites.size(); k++) {
                snprintf(nb, sizeof nb, "site %c", char('A' + k));   // the game's names: SITE A, POINT A
                objective = nb;
                float da = sideTo(spA, "attack", nb, &m.bombSites[k], Vec3()), db = sideTo(spB, "defend", nb, &m.bombSites[k], Vec3());
                add(nb, "site", floorCentre(m.bombSites[k]), da, db, "a", "b");
            }
            break;
        case MODE_POINTS:
            for (size_t k = 0; k < m.points.size(); k++) {
                snprintf(nb, sizeof nb, "point %c", char('A' + k));
                objective = nb;
                float da = sideTo(staged(k, false), "attack", nb, &m.points[k], Vec3()), db = sideTo(staged(k, true), "defend", nb, &m.points[k], Vec3());
                add(nb, "point", floorCentre(m.points[k]), da, db, "att", "def");
            }
            break;
        case MODE_KOTH:
            if (m.hasHill) {
                objective = "hill";
                float da = sideTo(spA, "a", "hill", &m.hill, Vec3()), db = sideTo(spB, "b", "hill", &m.hill, Vec3());
                add("hill", "hill", floorCentre(m.hill), da, db, "a", "b");
            }
            break;
        case MODE_PAYLOAD:
            if (m.payloadPath.size() >= 2) {   // one-sided (sides swap): A to the cart's start, B to the halfway point
                Vec3 start = m.payloadPath.front(), half = m.payloadPoint(m.payloadLength() * 0.5f);
                objective = "cart start / halfway";
                float da = sideTo(spA, "attack", "cart start", nullptr, start), db = sideTo(spB, "defend", "track halfway", nullptr, half);
                add("cart start / halfway", "cart", half, da, db, "a", "b");
            }
            break;
        case MODE_NPAYLOAD:
            if (m.payloadPath.size() >= 2) {
                Vec3 half = m.payloadPoint(m.payloadLength() * 0.5f);
                objective = "the cart (halfway)";
                float da = sideTo(spA, "a", "the cart", nullptr, half), db = sideTo(spB, "b", "the cart", nullptr, half);
                add("the cart (halfway)", "cart", half, da, db, "a", "b");
            }
            break;
        case MODE_VS: case MODE_DUEL: case MODE_ARENA: case MODE_TOURNEY: {
            // each side's walk to the nearest of the other side's spawns (the report's `across`)
            auto across = [&](const std::vector<int>& from, const std::vector<int>& to, const char* role, const char* what) {
                float sum = 0; int n = 0; bool ok = !from.empty() && !to.empty();
                for (int s : from) {
                    float best = -1; std::vector<Vec3> bp; int bt = -1;
                    for (int t : to) {
                        std::vector<Vec3> p;
                        float d = navRoute(g, m.spawns[s].pos, m.spawns[t].pos, withPaths ? &p : nullptr);
                        if (d >= 0 && (best < 0 || d < best)) { best = d; bp = p; bt = t; }
                    }
                    Json r = Json::object();
                    r.set("from", s); r.set("team", spawnTeamName(m.spawns[s].team, tb)); r.set("role", role); r.set("to", what); r.set("toSpawn", bt); r.set("objective", "enemy spawns");
                    r.set("metres", best >= 0 ? Json(best) : Json()); r.set("seconds", best >= 0 ? Json(best / kRun) : Json());
                    if (withPaths) r.set("path", jpath(bp));
                    routes.push(std::move(r));
                    if (best < 0) ok = false; else { sum += best; n++; }
                }
                return ok && n ? sum / n : -1.0f;
            };
            float ab = across(spA, spB, "a", "b spawns"), ba = across(spB, spA, "b", "a spawns");
            Vec3 mid;
            for (auto& s : m.spawns) mid += s.pos / (float)std::max<size_t>(1, m.spawns.size());
            add("enemy spawns", "spawns", mid, ab, ba, "a", "b");
            break;
        }
        case MODE_RACE: case MODE_SURF: {
            int s0 = race.empty() ? (m.spawns.empty() ? -1 : 0) : race[0];
            if (s0 < 0) break;
            Vec3 at = m.spawns[s0].pos;
            float total = 0; bool ok = true;
            for (size_t k = 0; k < m.checkpoints.size(); k++) {
                std::vector<Vec3> p;
                float d = navRoute(g, at, m.checkpoints[k].center(), withPaths ? &p : nullptr);
                snprintf(nb, sizeof nb, "checkpoint %zu", k + 1);
                Json r = Json::object();
                r.set("from", k == 0 ? Json(s0) : Json(std::string("checkpoint ") + std::to_string(k))); r.set("team", "race"); r.set("role", "race"); r.set("to", nb); r.set("objective", nb);
                r.set("metres", d >= 0 ? Json(d) : Json()); r.set("seconds", d >= 0 ? Json(d / kRun) : Json());
                if (withPaths) r.set("path", jpath(p));
                routes.push(std::move(r));
                if (d < 0) ok = false; else total += d;
                add(nb, "checkpoint", floorCentre(m.checkpoints[k]), ok ? total : -1.0f, -1, "race", nullptr);
                at = m.checkpoints[k].center();
            }
            break;
        }
        default: break;   // free for all, hide, tag: no objectives (spawn spread is in "spawns")
    }
    return objs;
}

static int runMetrics(const Json& req, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    Analysis a(std::move(m));
    int mode = modeFromJson(req["mode"], a.m.preferredMode >= 0 ? a.m.preferredMode : MODE_FFA);
    bool paths = req["paths"].boolean(true);
    out = Json::object();
    out.set("map", a.m.name);
    out.set("mode", modeName(mode));
    out.set("modeSupported", a.m.supportsMode(mode));
    Json routes = Json::array();
    out.set("objectives", a.objectives(mode, routes, paths));
    out.set("routes", std::move(routes));
    {   // spawn spread for the mode's pools: closest pair, pairs in sight (eye to eye)
        Json sp = Json::object();
        std::vector<const SpawnPoint*> pool = spawnPool(a.m, modeSpawnType(a.m, mode, TEAM_A, TEAM_A, false), 0);
        float minD = 1e9f; int seen = 0, n = 0;
        for (size_t i = 0; i < pool.size(); i++)
            for (size_t j = i + 1; j < pool.size(); j++) {
                minD = std::min(minD, length(pool[i]->pos - pool[j]->pos));
                Vec3 e1 = pool[i]->pos + Vec3(0, kEyeHeight, 0), e2 = pool[j]->pos + Vec3(0, kEyeHeight, 0), d = e2 - e1;
                float L = length(d);
                seen += L < 0.01f || !a.m.raycastAny(e1, d / L, L);
                n++;
            }
        sp.set("pool", (int)pool.size()); sp.set("closestPair", n ? Json(minD) : Json()); sp.set("pairsInSight", n ? Json((float)seen / n) : Json());
        out.set("spawns", std::move(sp));
    }
    double tObj = maptoolNow();
    out.set("reach", envelopeJson());
    double tEnv = maptoolNow();
    Json timings = Json::object();
    if (req["numbers"].boolean(true)) out.set("numbers", measureJson(a, &timings));
    timings.set("objectives", (tObj - t0) * 1000);
    timings.set("envelope", (tEnv - tObj) * 1000);
    Json at = a.timings();
    for (auto& [k, v] : at.members()) timings.set(k, v);
    timings.set("total", (maptoolNow() - t0) * 1000);
    out.set("ms", std::move(timings));
    return 0;
}
MAPTOOL_COMMAND("metrics", runMetrics, "{map, mode?, paths?, numbers?} -> objectives (attackers/defenders arrival s), routes, reach, the map report's numbers");

static int runRoute(const Json& req, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    if (!req["from"].isArr() || !req["to"].isArr()) return cmdError(out, "route needs \"from\" and \"to\" [x,y,z] (feet)");
    Analysis a(std::move(m));
    Vec3 from = toVec(req["from"]), to = toVec(req["to"]);
    float radius = req["radius"].f(0.6f);
    std::vector<Vec3> path;
    WalkGrid& w = a.walk();
    int na = w.nodeAt(from, radius), nb = w.nodeAt(to, radius);
    float d = walkRoute(w, from, to, &path, radius);
    out = Json::object();
    out.set("reachable", d >= 0);
    out.set("metres", d >= 0 ? Json(d) : Json());
    out.set("seconds", d >= 0 ? Json(d / kRun) : Json());
    out.set("path", jpath(path));
    out.set("fromStands", na >= 0);   // a standing spot within `radius` (0.6 m) of the point, its floor 0.3 m above to 1 m below
    out.set("toStands", nb >= 0);
    if (req["nav"].boolean()) {   // the bots' nav graph route too (what the bots walk; distances as the map report)
        std::vector<Vec3> np;
        float nd = navRoute(a.nav(), from, to, &np);
        Json n = Json::object();
        n.set("metres", nd >= 0 ? Json(nd) : Json()); n.set("seconds", nd >= 0 ? Json(nd / kRun) : Json()); n.set("path", jpath(np));
        out.set("nav", std::move(n));
    }
    Json t = a.timings();
    t.set("total", (maptoolNow() - t0) * 1000);
    out.set("ms", std::move(t));
    return 0;
}
MAPTOOL_COMMAND("route", runRoute, "{map, from:[x,y,z], to:[x,y,z], radius?, nav?} -> one walk grid route {reachable, seconds, path}");
