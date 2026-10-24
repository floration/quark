// Out of bounds: nothing outside a map's playable floor may be reachable with the full movement envelope (clip, or
// walls too high to dash up, keep everyone in). The envelope is measured with the game's own movement code, so the
// check follows any change to movement; the leaps and grapple pulls it allows are walked over samples of the floor.
// Debugging: OOB_TRACE=<map> lists the spots out of bounds.
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <queue>
#include <string>
#include <vector>
#include "game/bots.h"
#include "game/gamedefs.h"
#include "game/map.h"
#include "game/pmove.h"
#include "walkgrid.h"

namespace {

constexpr float kRunSpeed = 9.5f;   // pmove kGroundSpeed (1.17: 8 -> 9.5)
constexpr float kEye = 1.6f;

// Spots of the walk grid (tests/walkgrid.h) a player can get to from some spawn without movement tech: the playable
// floor, whose footprint is what's in bounds.
std::vector<char> walkReachable(const Map& m, const WalkGrid& w) {
    std::vector<char> comps(w.compLinks.size(), 0), out(w.nodes.size(), 0);
    for (auto& sp : m.spawns) {
        int n = w.nodeAt(sp.pos);
        if (n < 0 || comps[w.comp[n]]) continue;
        auto seen = w.reach(n);
        for (size_t c = 0; c < seen.size(); c++) comps[c] |= seen[c];
    }
    for (size_t a = 0; a < w.nodes.size(); a++) out[a] = comps[w.comp[a]];
    return out;
}

// ---- the movement envelope, measured with the game's own movement code -----------------------------------------
// What a good player can do as the game plays (owner decision 5.4: auto-bhop, unbounded strafing and the dash's limit
// are intended): build speed by bunny-hopping, strafing and dashing; turn speed into distance and height with a jump
// and an upward dash; ride the grapple. Simulated with pmove once per run, so it follows any change to movement.
struct MoveEnvelope {
    static constexpr float kBin = 0.5f;          // horizontal distance bins
    static constexpr int kBins = 200;            // (100 m)
    static constexpr float kLowest = -40.0f;     // drops followed this far (there's no fall damage)
    std::vector<float> speed;                    // tier speeds, m/s (tier 0: running)
    std::vector<float> runUp;                    // metres of moving from a standstill it takes to reach each tier
    std::vector<std::vector<float>> top;         // [tier][bin]: the highest an arc from that speed (or slower) gets at that distance, relative to take-off
    std::vector<std::vector<float>> apexLevel;   // [tier][bin]: the lowest apex among those arcs that are still at take-off height or above there
    std::vector<std::vector<float>> pullExit;    // [tier][k]: speed after a grapple pull of 5k m toward an anchor ahead, from that tier's speed
    int tiers() const { return (int)speed.size(); }
    int tierFor(float metres) const { int k = 0; while (k + 1 < tiers() && runUp[k + 1] <= metres) k++; return k; }
    int tierForSpeed(float v) const { int k = 0; while (k + 1 < tiers() && speed[k + 1] <= v + 0.01f) k++; return k; }
    // the slowest tier whose arcs reach (h across, dy up), or -1
    int need(float h, float dy) const {
        int b = (int)(h / kBin);
        if (b >= kBins || dy < kLowest) return -1;
        for (int k = 0; k < tiers(); k++) if (top[k][b] >= dy) return k;
        return -1;
    }
    float reach(int k, float dy) const { float r = 0; for (int b = 0; b < kBins; b++) if (top[k][b] >= dy) r = (b + 1) * kBin; return r; }
    float maxReach() const { return reach(tiers() - 1, kLowest); }
    int pullTier(int k, float len) const {
        size_t i = std::min(pullExit[k].size() - 1, (size_t)std::lround(len / 5.0f));
        return tierForSpeed(pullExit[k][i]);
    }
};

static float hspeed(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

const MoveEnvelope& moveEnvelope() {
    static const MoveEnvelope env = [] {
        auto t0 = std::chrono::steady_clock::now();
        auto lap = [&](const char* what) { if (std::getenv("OOB_TIME")) fprintf(stderr, "envelope: %s by %.0f ms\n", what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); };
        MoveEnvelope e;
        // (past 28 m/s an upward dash gets no higher: its exit speed is capped; 32 and 36 m/s: super dashes)
        e.speed = {kRunSpeed, 12, 16, 20, 24, 28, 32, 36};
        // speed from a standstill: jump held (auto-bhop), dashing whenever a charge is there and it adds speed, and each
        // millisecond the strafe / turn that gains the most (greedy: a good player, not a tool-assisted one). Two
        // ways, the better per tier: dashing as soon as it adds speed (a dash jump when it starts on the floor), and
        // super dashes (looking down, dashing on a bhop's landing tick whenever the dash is ready).
        Map flat;
        flat.parse("box -600 -1 -600 600 0 600\nspawn any 0 0 0\n");
        e.runUp.assign(e.speed.size(), 1e9f);
        e.runUp[0] = 0;
        for (int superDash = 0; superDash < 2; superDash++) {
            MoveState s;
            s.flags = MF_ONGROUND;
            float dist = 0;
            bool dashed = false;
            size_t next = 1;
            for (int t = 0; t < 30000 && next < e.speed.size(); t++) {
                float h = hspeed(s.vel), vyaw = h > 0.1f ? std::atan2(-s.vel.x, -s.vel.z) : 0;
                // (charges: 2, the ground refills them, a bhop landing one, a dash jump all, a super dash leaves one, each
                // only once it has recharged (1.2 s from the end of the dash that spent it); and the 0.6 s cooldown from
                // the end of the last dash (dashReady: pmove's rule); above 31 m/s a dash slows you: it leaves at your
                // speed + 1, at most 32)
                bool dash = !dashed && dashReady(s) && h < 31.0f;
                if (superDash) dash = !dashed && dashReady(s) && (s.flags & MF_ONGROUND) && h < 35.9f;
                MoveState best;
                float bestH = -1;
                bool bestDash = false;
                for (int side = 0; side < 2; side++)
                    for (int a = -90; a <= 90; a += 3) {
                        UserCmd c;
                        c.msec = 1;
                        c.buttons = BTN_JUMP | (dash ? BTN_DASH : 0);
                        c.pitch = superDash ? -1.2f : 0.0f;
                        if (side) c.side = 127; else c.forward = 127;
                        c.yaw = vyaw + a * 0.0174533f + (side ? 1.5707963f : 0.0f);
                        MoveState q = s;
                        pmove(q, c, flat);
                        if (hspeed(q.vel) > bestH) { bestH = hspeed(q.vel); best = q; bestDash = dash; }
                    }
                dashed = bestDash;
                dist += std::sqrt((best.pos.x - s.pos.x) * (best.pos.x - s.pos.x) + (best.pos.z - s.pos.z) * (best.pos.z - s.pos.z));
                s = best;
                if (s.dashTimer <= 0 && hspeed(s.vel) >= e.speed[next]) {
                    if (dist < e.runUp[next]) e.runUp[next] = dist;
                    ++next;
                }
            }   // (a tier neither way reaches keeps 1e9: never)
        }
        lap("speed build-up");
        // take-offs: moving at the tier's speed, jump, then (or not) an upward dash after tau at pitch p, and with the
        // charges left (two; a dash jump off the take-off refills what has recharged) more dashes the same way: chained
        // (each as the last ends), one 0.3 s later, or one as soon as pmove lets it (0.6 s after the first ends). Each arc
        // is followed until it is kLowest below the take-off; the envelope is the highest any of them is at each distance.
        Map pad;
        pad.parse("box -1 -1 -1 1 0 1\nspawn any 0 0 0\n");
        e.top.assign(e.speed.size(), std::vector<float>(MoveEnvelope::kBins, -1e9f));
        e.apexLevel.assign(e.speed.size(), std::vector<float>(MoveEnvelope::kBins, 1e9f));
        for (size_t k = 0; k < e.speed.size(); k++) {
            for (int ti = 0; ti <= 5; ti++)
                for (int pi = -2; pi <= 9; pi++)   // (no dash, a down dash (a super dash off the take-off; aimed as far down as the
                                                   // mouse goes, 88.8 deg: its hop is highest there, a full jump), or one at
                                                   // 0-90 degrees up, 0-0.4 s after the jump)
                for (int more = 0; more < (pi == -1 ? 1 : 5); more++) {   // dashes after it: none, 1 or 2 chained, one 0.3 s later, one at the earliest
                    float tau = ti * 0.08f, pitch = pi == -2 ? -1.55f : pi < 0 ? 0 : pi * 10 * 0.0174533f;
                    MoveState s;
                    s.pos = {0, 0, 0.9f};
                    s.vel = {0, 0, -e.speed[k]};
                    s.flags = MF_ONGROUND;
                    std::vector<float> y(MoveEnvelope::kBins, -1e9f);
                    float apex = 0;
                    int pressed = 0;   // (more == 4: the dashes pressed, each the moment pmove allows it)
                    for (int t = 0; t < 6000; t++) {
                        UserCmd c;
                        c.msec = 1;
                        c.forward = 127;
                        c.pitch = pitch;
                        int t0 = (int)(tau * 1000), n = more == 3 ? 1 : more, gapMs = more == 3 ? 300 : 125;
                        bool press = pi != -1 && t >= t0 && (t - t0) % gapMs < 5 && (t - t0) / gapMs <= n;
                        if (more == 4) { press = pi != -1 && t >= t0 && pressed < 2 && dashReady(s) && !(s.flags & MF_DASHHELD); pressed += press; }
                        c.buttons = BTN_JUMP | (press ? BTN_DASH : 0);
                        pmove(s, c, pad);
                        apex = std::max(apex, s.pos.y);
                        int b = (int)((0.9f - s.pos.z) / MoveEnvelope::kBin);
                        if (b >= 0 && b < MoveEnvelope::kBins) y[b] = std::max(y[b], s.pos.y);
                        if (s.pos.y < MoveEnvelope::kLowest) break;
                    }
                    for (int b = 0; b < MoveEnvelope::kBins; b++) {
                        e.top[k][b] = std::max(e.top[k][b], y[b]);
                        if (y[b] >= 0) e.apexLevel[k][b] = std::min(e.apexLevel[k][b], apex);
                    }
                }
            if (k > 0)   // anything a slower take-off reaches, a faster player reaches too (slowing down is free)
                for (int b = 0; b < MoveEnvelope::kBins; b++) {
                    e.top[k][b] = std::max(e.top[k][b], e.top[k - 1][b]);
                    e.apexLevel[k][b] = std::min(e.apexLevel[k][b], e.apexLevel[k - 1][b]);
                }
        }
        lap("take-offs");
        // grapple pulls: an anchor 5k m ahead (on a wall, at eye height), from each tier's speed: the speed when it lets go
        e.pullExit.assign(e.speed.size(), std::vector<float>(10, 0));
        for (size_t k = 0; k < e.speed.size(); k++)
            for (int i = 0; i < 10; i++) {
                float L = std::max(2.0f, i * 5.0f);
                char txt[160];
                snprintf(txt, sizeof txt, "box -20 -1 -60 20 0 20\nbox -10 0 %g 10 20 %g\nspawn any 0 0 0\n", -L - 1, -L);
                Map wall;
                wall.parse(txt);
                MoveState s;
                s.flags = MF_ONGROUND;
                s.vel = {0, 0, -e.speed[k]};
                float best = -1;   // the fastest it gets you going while it pulls (you let go before the wall)
                for (int t = 0; t < 4000; t++) {
                    UserCmd c;
                    c.msec = 1;
                    c.buttons = BTN_GRAPPLE;
                    pmove(s, c, wall);
                    if (s.flags & MF_HOOKED) best = std::max(best, hspeed(s.vel));
                    else if (best >= 0) break;
                }
                e.pullExit[k][i] = best >= 0 ? best : e.speed[k];
            }
        lap("grapple pulls");
        return e;
    }();
    return env;
}

// Movement-tech graph over the floor samples: the run (what the bots' nav calls walkable: running, steps, ordinary
// jumps, ladders) plus leaps and grapple pulls, with the speed you carry (MoveEnvelope tiers) as part of the state:
// moving builds speed (runUp: metres from a standstill, so circling to gain speed costs the metres it takes), a ladder
// costs all of it, a pull leaves you at what the pull gives, and a leap needs the speed its arc takes. What it
// reaches is what a good player can get to.
struct TechGraph {
    enum { RUN, LEAP, PULL, LADDER };
    struct Edge { int to; float cost; uint8_t need, kind; };
    std::vector<Vec3> p;
    std::vector<std::vector<Edge>> adj;
    std::vector<char> clipped;                  // sample is inside clip (a wall top under the invisible ceiling)
    const MoveEnvelope* env = nullptr;
    static constexpr float kHubLeap = 24.0f;    // leaps longer than this only between hub samples
    // samples by 6 m column
    float cx0 = 0, cz0 = 0;
    int cw = 0, ch = 0;
    std::vector<std::vector<int>> cells;
    int cellOf(float x, float z) const {
        int i = (int)std::floor((x - cx0) / 6), j = (int)std::floor((z - cz0) / 6);
        return i < 0 || j < 0 || i >= cw || j >= ch ? -1 : j * cw + i;
    }
    int nearest(const Vec3& q, float maxD = 1e9f) const {
        int best = -1; float bd = maxD;
        int ci = (int)std::floor((q.x - cx0) / 6), cj = (int)std::floor((q.z - cz0) / 6);
        for (int dx = -2; dx <= 2; dx++)
            for (int dz = -2; dz <= 2; dz++) {
                int i = ci + dx, j = cj + dz;
                if (i < 0 || j < 0 || i >= cw || j >= ch) continue;
                for (int s : cells[j * cw + i]) { float d = length(p[s] - q); if (d < bd) { bd = d; best = s; } }
            }
        if (best < 0 && maxD >= 1e8f) for (size_t s = 0; s < p.size(); s++) { float d = length(p[s] - q); if (d < bd) { bd = d; best = (int)s; } }
        return best;
    }
    // m: the map as players collide with it (clip included: it blocks leaps). runReach: how far apart two samples on
    // one floor (or two linked floors) are still a run (6 m; more where the samples are sparse, on the biggest maps,
    // so the run isn't cut into pieces by the sampling, REVIEW MAP-4)
    void build(const Map& m, const NavGraph& g, const std::vector<Vec3>& pts, float runReach = 6.0f) {
        env = &moveEnvelope();
        const MoveEnvelope& E = *env;
        p = pts;
        std::vector<int> node(p.size());
        clipped.assign(p.size(), 0);
        for (size_t i = 0; i < p.size(); i++) {
            clipped[i] = m.boxSolid(playerBox(p[i] + Vec3(0, 0.02f, 0), false));   // (inside clip: nobody stands there)
            node[i] = g.nodeAt(p[i] + Vec3(0, 0.05f, 0));
        }
        auto navLinked = [&](int a, int b) {
            if (a < 0 || b < 0) return false;
            if (a == b) return true;
            for (auto& l : g.links[a]) if (l.first == b) return true;
            return false;
        };
        adj.assign(p.size(), {});
        float x1 = -1e9f, z1 = -1e9f;
        cx0 = cz0 = 1e9f;
        for (auto& q : p) { cx0 = std::min(cx0, q.x); cz0 = std::min(cz0, q.z); x1 = std::max(x1, q.x); z1 = std::max(z1, q.z); }
        cw = p.empty() ? 0 : (int)((x1 - cx0) / 6) + 1;
        ch = p.empty() ? 0 : (int)((z1 - cz0) / 6) + 1;
        cells.assign((size_t)cw * ch, {});
        for (size_t i = 0; i < p.size(); i++) cells[cellOf(p[i].x, p[i].z)].push_back((int)i);
        auto clear = [&](const Vec3& a, const Vec3& b) { Vec3 d = b - a; float l = length(d); return l < 0.01f || !m.raycastAny(a, d / l, l, true); };
        // the climb out of / down into a sample, to a chest-height line at y top (cached: every leap from here asks)
        std::map<std::pair<int, int>, bool> riseMemo;
        auto rise = [&](int i, float top) {
            auto key = std::make_pair(i, (int)std::ceil(top * 4));
            auto it = riseMemo.find(key);
            if (it != riseMemo.end()) return it->second;
            bool ok = clear(p[i] + Vec3(0, 1.0f, 0), Vec3(p[i].x, key.second / 4.0f, p[i].z));
            riseMemo[key] = ok;
            return ok;
        };
        const float reachMax = E.maxReach();
        const int span = (int)std::ceil(reachMax / 6) + 1;
        for (size_t i = 0; i < p.size(); i++) {
            int ci = (int)std::floor((p[i].x - cx0) / 6), cj = (int)std::floor((p[i].z - cz0) / 6);
            for (int dz = -span; dz <= span; dz++)
                for (int dx = -span; dx <= span; dx++) {
                    int a = ci + dx, b = cj + dz;
                    if (a < 0 || b < 0 || a >= cw || b >= ch) continue;
                    if ((std::max(0, std::abs(dx) - 1) * 6.0f) * (std::max(0, std::abs(dx) - 1) * 6.0f) + (std::max(0, std::abs(dz) - 1) * 6.0f) * (std::max(0, std::abs(dz) - 1) * 6.0f) > reachMax * reachMax) continue;
                    for (int j : cells[b * cw + a]) {
                        if (j == (int)i || clipped[i] || clipped[j]) continue;   // (clip: nobody stands there, whatever the nav says)
                        Vec3 d = p[j] - p[i];
                        float h = std::sqrt(d.x * d.x + d.z * d.z);
                        // the run: what the bots' nav calls walkable (same surface, or a step / ordinary jump between two)
                        if (h <= runReach && navLinked(node[i], node[j])) {
                            adj[i].push_back({j, length(d), 0, RUN});
                            continue;
                        }
                        // a leap: the slowest take-off whose arcs get there. Past standing dash-jump range only between
                        // every third sample (hubs a few metres apart: it's the long way across that matters, and it
                        // keeps the check quick)
                        if (h > kHubLeap && (i % 3 != 0 || j % 3 != 0)) continue;
                        if (clipped[i] || clipped[j]) continue;
                        int k = E.need(h, d.y);
                        if (k < 0) continue;
                        // the arc must be clear: up out of i, across at chest height over the higher end (or the arc's
                        // own apex, for a leap only a fast take-off makes), and down into j: three lines (the middle and
                        // both shoulders, so a player-wide body has to fit; a slit doesn't count as a way through).
                        // Within dash-jump range the shoulders' climbs and drops are checked too.
                        int bin = std::min(MoveEnvelope::kBins - 1, (int)(h / MoveEnvelope::kBin));
                        float apex = E.apexLevel[k][bin] < 1e8f ? E.apexLevel[k][bin] : 0;
                        float top = std::max(std::max(p[i].y, p[j].y) + 1.2f, p[i].y + apex + 1.0f);
                        bool fits = rise((int)i, top) && rise(j, top);
                        if (fits) {
                            Vec3 side = h > 0.01f ? Vec3(-d.z / h, 0, d.x / h) * 0.35f : Vec3();
                            for (float o : {0.0f, -1.0f, 1.0f}) {
                                Vec3 a0 = p[i] + side * o, b0 = p[j] + side * o;
                                if (o != 0 && h <= 16.0f && (!clear(a0 + Vec3(0, 1.0f, 0), Vec3(a0.x, top, a0.z)) || !clear(Vec3(b0.x, top, b0.z), b0 + Vec3(0, 1.0f, 0)))) { fits = false; break; }
                                if (!clear(Vec3(a0.x, top, a0.z), Vec3(b0.x, top, b0.z))) { fits = false; break; }
                            }
                        }
                        if (!fits) continue;
                        adj[i].push_back({j, length(d), (uint8_t)k, LEAP});
                    }
                }
            // grapple pulls
            Vec3 eye = p[i] + Vec3(0, kEye, 0);
            for (int k = 0; k < 16; k++)
                for (float el : {0.3f, 0.7f, 1.1f}) {
                    float a = k * 6.2831853f / 16;
                    Vec3 d = normalize(Vec3(std::sin(a) * std::cos(el), std::sin(el), std::cos(a) * std::cos(el)));
                    float t = m.raycast(eye, d, kGrappleRange);
                    if (t < 2.0f) continue;
                    Vec3 hit = eye + d * t;
                    int j = nearest(hit, 4.0f);
                    if (j < 0 || j == (int)i || p[j].y > hit.y + 0.5f || clipped[j]) continue;
                    // the hook goes through clip, but you don't: the way to where you land must be open to a player
                    if (!clear(eye, hit - d * 0.5f) || !clear(hit - d * 0.5f, p[j] + Vec3(0, 1.0f, 0))) continue;
                    adj[i].push_back({j, t + length(p[j] - hit), 0, PULL});
                }
        }
        // ladders: from the floor at the foot to the floor at the top, both ways
        for (const AABB& l : m.ladders) {
            Vec3 c = l.center();
            int lo = nearest({c.x, l.mn.y, c.z}, 2.5f);
            int hi = -1; float bd = 3.0f;
            for (size_t j = 0; j < p.size(); j++) {
                if (std::fabs(p[j].y - l.mx.y) > 0.8f) continue;
                float dd = std::hypot(p[j].x - c.x, p[j].z - c.z);
                if (dd < bd) { bd = dd; hi = (int)j; }
            }
            if (lo < 0 || hi < 0 || lo == hi) continue;
            float cost = (l.mx.y - l.mn.y) * 1.8f + bd; // climbing is slower than running
            adj[lo].push_back({hi, cost, 0, LADDER});
            adj[hi].push_back({lo, cost, 0, LADDER});
        }
    }
    // Shortest paths from sample s (standing still: tier 0) with every move; per sample, the shortest over the
    // speeds you can be there with (1e30: out of reach).
    std::vector<float> from(int s) const {
        const int T = env->tiers(), N = (int)p.size();
        std::vector<float> d((size_t)N * T, 1e30f), out(N, 1e30f);
        if (s >= 0) {
            using QE = std::pair<float, int>;
            std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
            d[(size_t)s * T] = 0; q.push({0, s * T});
            while (!q.empty()) {
                auto [du, u] = q.top(); q.pop();
                if (du > d[u]) continue;
                int i = u / T, k = u % T;
                for (const Edge& e : adj[i]) {
                    if (e.need > k) continue;
                    int k2 = e.kind == LADDER ? 0 : e.kind == PULL ? env->pullTier(k, e.cost) : std::max(k, env->tierFor(env->runUp[k] + e.cost));
                    int v = e.to * T + k2;
                    if (du + e.cost < d[v]) { d[v] = du + e.cost; q.push({d[v], v}); }
                }
            }
        }
        for (int i = 0; i < N; i++)
            for (int k = 0; k < T; k++) out[i] = std::min(out[i], d[(size_t)i * T + k]);
        return out;
    }
};

} // namespace

bool isHumanMadeMap(const std::string& name, const Map& m);   // tests.cpp

// Every map (but the zz_ ones other tests write): with every move a good player has, from the first spawn, no floor
// outside the playable floor's footprint (the floor you can walk to from a spawn) is reachable. Returns the failures;
// a human-made map's are printed and reported, not counted. part / parts: every parts-th map from the part-th (the
// suite runs it as two groups, ~12 s of CPU each: one was the longest of all the groups).
int outOfBoundsTests(int part, int parts) {
    printf("out of bounds: nothing outside the playable floor reachable with every move\n");
    int fails = 0, index = -1;
    for (const std::string& name : listMaps()) {
        if (name.rfind("zz_", 0) == 0) continue;
        if (++index % parts != part) continue;
        auto t0 = std::chrono::steady_clock::now();
        Map m;
        if (!m.load(name)) continue;   // (testAllMaps fails a map that doesn't load)
        const bool human = isHumanMadeMap(name, m);
        const Map solid = m;   // (with clip: leaps can't go through it)
        // clip (invisible, out of reach above the walls) is no part of the floor anyone sees or plays on
        m.brushes.erase(std::remove_if(m.brushes.begin(), m.brushes.end(), [](const Brush& b) { return b.material == 9; }), m.brushes.end());
        m.rebuild();
        NavGraph g;
        g.build(m);
        WalkGrid w;   // (walked with the player's hull against the real collision)
        w.build(solid);
        w.components();
        const std::vector<char> reach = walkReachable(solid, w);
        // the playable floor: nav surfaces with a walk-grid spot on them a player can get to (alive)
        std::vector<char> playable(g.nodes.size(), 0);
        for (size_t i = 0; i < g.nodes.size(); i++) {
            auto& n = g.nodes[i];
            if (n.y <= m.killY()) continue;   // (below the kill height you're dead)
            AABB box = {{n.x0 - 0.4f, n.y - 0.2f, n.z0 - 0.4f}, {n.x1 + 0.4f, n.y + 0.2f, n.z1 + 0.4f}};
            w.forColumnsNear(box, 0, [&](int a) {
                const Vec3& q = w.nodes[a].p;
                if (reach[a] && std::fabs(q.y - n.y) < 0.15f && q.x >= box.mn.x && q.x <= box.mx.x && q.z >= box.mn.z && q.z <= box.mx.z) playable[i] = 1;
            });
        }
        // samples of every nav surface (playable or not: roofs and wall tops only movement tech reaches are what this
        // looks for), every 2 m with standing room; a surface under 2 m across (a stair step, a beam) gets one row
        // down its middle
        std::vector<std::pair<Vec3, int>> pts;
        for (size_t i = 0; i < g.nodes.size(); i++) {
            auto& n = g.nodes[i];
            float sx = n.x1 - n.x0 < 2 ? (n.x0 + n.x1) / 2 : n.x0 + 1, sz = n.z1 - n.z0 < 2 ? (n.z0 + n.z1) / 2 : n.z0 + 1;
            for (float x = sx; x < n.x1; x += 2)
                for (float z = sz; z < n.z1; z += 2) {
                    Vec3 p = {x, n.y, z};
                    if (!m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false))) pts.push_back({p, (int)i});
                }
        }
        TechGraph tech;
        std::vector<Vec3> tp;
        std::vector<char> techPlayable;
        size_t stride = pts.size() / 2500 + 1;   // up to ~2500 samples
        for (size_t i = 0; i < pts.size(); i += stride) { tp.push_back(pts[i].first); techPlayable.push_back(playable[pts[i].second]); }
        const float techCell = 4.0f * stride;   // m2 of floor per sample
        // and a sample on every floor inside each objective (race checkpoints, bomb sites, the hill, capture points)
        // and under every spawn, so a thin sampling doesn't cut the way there
        std::vector<AABB> zones = m.checkpoints;
        zones.insert(zones.end(), m.bombSites.begin(), m.bombSites.end());
        zones.insert(zones.end(), m.points.begin(), m.points.end());
        if (m.hasHill) zones.push_back(m.hill);
        for (const AABB& z : zones)
            for (size_t i = 0; i < g.nodes.size(); i++) {
                auto& n = g.nodes[i];
                if (n.x1 < z.mn.x || n.x0 > z.mx.x || n.z1 < z.mn.z || n.z0 > z.mx.z || n.y < z.mn.y - 1.0f || n.y > z.mx.y) continue;
                Vec3 q = {std::clamp(z.center().x, n.x0, n.x1), n.y, std::clamp(z.center().z, n.z0, n.z1)};
                if (m.boxSolid(playerBox(q + Vec3(0, 0.02f, 0), false))) continue;
                tp.push_back(q); techPlayable.push_back(playable[i]);
            }
        for (auto& sp : m.spawns) {
            int n = g.nodeAt(sp.pos + Vec3(0, 0.05f, 0));
            if (n < 0) continue;
            tp.push_back({sp.pos.x, g.nodes[n].y, sp.pos.z}); techPlayable.push_back(playable[n]);
        }
        tech.build(solid, g, tp, std::max(6.0f, 3.0f * std::sqrt(techCell)));
        // the playable floor's footprint: every surface of it (not the samples, whose edge moves with the sampling)
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (size_t i = 0; i < g.nodes.size(); i++)
            if (playable[i]) { auto& n = g.nodes[i]; x0 = std::min(x0, n.x0); x1 = std::max(x1, n.x1); z0 = std::min(z0, n.z0); z1 = std::max(z1, n.z1); }
        if (m.spawns.empty() || x0 > x1) continue;   // (no spawn, no floor to walk to: testAllMaps fails those)
        // what the full envelope opens up from the first spawn: surfaces the run never reaches (roofs, wall tops), and
        // any of them outside the footprint (out of bounds)
        const std::vector<float> d = tech.from(tech.nearest(m.spawns[0].pos));
        int extra = 0, out = 0;
        for (size_t i = 0; i < tech.p.size(); i++) {
            if (d[i] > 1e29f || techPlayable[i]) continue;
            extra++;
            const Vec3& q = tech.p[i];
            bool o = q.x < x0 - 1 || q.x > x1 + 1 || q.z < z0 - 1 || q.z > z1 + 1;
            out += o;
            if (o && std::getenv("OOB_TRACE") && name == std::getenv("OOB_TRACE")) printf("    out of bounds: %.1f %.1f %.1f (floor spans x %.1f..%.1f z %.1f..%.1f)\n", q.x, q.y, q.z, x0, x1, z0, z1);
        }
        printf("  %-10s %6.0f m2 only movement tech reaches, %.0f m2 of it out of bounds (%.0f ms)\n", name.c_str(), extra * techCell, out * techCell,
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (!out) continue;
        if (human) printf("  %s is HUMAN-MADE: %.0f m2 out of bounds - reported, not failing the run\n", name.c_str(), out * techCell);
        else { printf("  FAIL %s: %.0f m2 outside the playable floor is reachable (OOB_TRACE=%s lists it)\n", name.c_str(), out * techCell, name.c_str()); fails++; }
    }
    return fails;
}
