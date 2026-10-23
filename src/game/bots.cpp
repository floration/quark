#include <algorithm>
#include <atomic>
#include <thread>
#include "bots.h"
#include "../common/profile.h"
#include "gamemodes.h"
#include "server.h"

static_assert(kMaxPlayers == 32, "BotBrain::aware is sized for kMaxPlayers");

// =====================================================================================================
// Navigation graph
// =====================================================================================================

struct NavJob {
    Map map;
    MapId id;   // map.id(), taken before the thread starts
    NavGraph result;
    std::atomic<bool> done{false};
};

namespace {
constexpr float kNavCell = 4.0f;
constexpr float kLinkGap = 2.6f;   // farthest jump between surfaces
constexpr float kNodePad = 0.3f;   // nodeAt counts a point this close to a surface's edge as on it
}

int NavGraph::cellOf(float x, float z) const {
    int cx = (int)std::floor((x - gx0) / kNavCell), cz = (int)std::floor((z - gz0) / kNavCell);
    if (cx < 0 || cz < 0 || cx >= gw || cz >= gh) return -1;
    return cz * gw + cx;
}

void NavGraph::build(const Map& m) {
    nodes.clear();
    links.clear();
    mapId = m.id();
    generation++;
    for (const Brush& b : m.brushes) {
        const AABB& a = b.box;
        if (b.material >= 5) continue; // far scenery, water / lava
        if (a.mx.x - a.mn.x < 0.4f || a.mx.z - a.mn.z < 0.4f) continue; // (0.5 m stair steps are floor)
        // standable if a player fits on top somewhere (5x5 samples: other blocks may cover parts of it)
        bool free = false;
        for (int k = 0; k < 25 && !free; k++) {
            Vec3 top = {lerpf(a.mn.x, a.mx.x, 0.1f + 0.2f * (k % 5)), a.mx.y + 0.02f, lerpf(a.mn.z, a.mx.z, 0.1f + 0.2f * (k / 5))};
            AABB probe = {{top.x - 0.05f, top.y, top.z - 0.05f}, {top.x + 0.05f, top.y + kPlayerCrouchHeight, top.z + 0.05f}};
            free = !m.boxSolid(probe);
        }
        if (!free) continue;
        nodes.push_back({a.mn.x, a.mn.z, a.mx.x, a.mx.z, a.mx.y});
    }
    // Grid of the surfaces (each in every cell its padded rectangle touches): nodeAt looks in one cell, and the
    // link pass only compares surfaces within jumping distance instead of every pair (REVIEW PERF-10).
    cellStart.clear(); cellNodes.clear(); gw = gh = 0;
    if (!nodes.empty()) {
        float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
        for (auto& n : nodes) { x0 = std::min(x0, n.x0); z0 = std::min(z0, n.z0); x1 = std::max(x1, n.x1); z1 = std::max(z1, n.z1); }
        gx0 = x0 - kNodePad - 0.01f; gz0 = z0 - kNodePad - 0.01f;
        gw = std::max(1, (int)std::ceil((x1 + kNodePad + 0.01f - gx0) / kNavCell));
        gh = std::max(1, (int)std::ceil((z1 + kNodePad + 0.01f - gz0) / kNavCell));
        if ((size_t)gw * gh > 4u * 1024 * 1024) gw = gh = 0; // absurd extents: no grid (checks every surface instead)
        auto range = [&](const Node& n, float pad, int& cx0, int& cz0, int& cx1, int& cz1) {
            cx0 = std::clamp((int)std::floor((n.x0 - pad - gx0) / kNavCell), 0, gw - 1);
            cx1 = std::clamp((int)std::floor((n.x1 + pad - gx0) / kNavCell), 0, gw - 1);
            cz0 = std::clamp((int)std::floor((n.z0 - pad - gz0) / kNavCell), 0, gh - 1);
            cz1 = std::clamp((int)std::floor((n.z1 + pad - gz0) / kNavCell), 0, gh - 1);
        };
        if (gw > 0) {
        std::vector<uint32_t> count((size_t)gw * gh + 1, 0);
        for (auto& n : nodes) {
            int a, b, c, d; range(n, kNodePad, a, b, c, d);
            for (int z = b; z <= d; z++) for (int x = a; x <= c; x++) count[(size_t)z * gw + x]++;
        }
        cellStart.assign((size_t)gw * gh + 1, 0);
        for (size_t i = 0; i < (size_t)gw * gh; i++) cellStart[i + 1] = cellStart[i] + count[i];
        cellNodes.resize(cellStart.back());
        std::vector<uint32_t> fill(cellStart.begin(), cellStart.end() - 1);
        for (size_t i = 0; i < nodes.size(); i++) { // (ascending: each cell's list is in node order)
            int a, b, c, d; range(nodes[i], kNodePad, a, b, c, d);
            for (int z = b; z <= d; z++) for (int x = a; x <= c; x++) cellNodes[fill[(size_t)z * gw + x]++] = (int)i;
        }
        }
    }
    links.resize(nodes.size());
    std::vector<uint32_t> stamp(nodes.size(), 0);
    std::vector<int> cand;
    hScale = 1.0f;
    for (size_t i = 0; i < nodes.size(); i++) {
        cand.clear();
        if (gw > 0) {
            const Node& a = nodes[i];
            int cx0 = std::clamp((int)std::floor((a.x0 - kLinkGap - gx0) / kNavCell), 0, gw - 1), cx1 = std::clamp((int)std::floor((a.x1 + kLinkGap - gx0) / kNavCell), 0, gw - 1);
            int cz0 = std::clamp((int)std::floor((a.z0 - kLinkGap - gz0) / kNavCell), 0, gh - 1), cz1 = std::clamp((int)std::floor((a.z1 + kLinkGap - gz0) / kNavCell), 0, gh - 1);
            for (int z = cz0; z <= cz1; z++)
                for (int x = cx0; x <= cx1; x++)
                    for (uint32_t k = cellStart[(size_t)z * gw + x]; k < cellStart[(size_t)z * gw + x + 1]; k++) {
                        int j = cellNodes[k];
                        if (stamp[j] != i + 1) { stamp[j] = (uint32_t)i + 1; cand.push_back(j); }
                    }
            std::sort(cand.begin(), cand.end()); // same order as checking every node
        } else {
            for (size_t j = 0; j < nodes.size(); j++) cand.push_back((int)j);
        }
        for (int j : cand) {
            if ((size_t)j == i) continue;
            const Node &a = nodes[i], &b = nodes[j];
            float gx = std::max(0.0f, std::max(a.x0, b.x0) - std::min(a.x1, b.x1));
            float gz = std::max(0.0f, std::max(a.z0, b.z0) - std::min(a.z1, b.z1));
            float gap = std::sqrt(gx * gx + gz * gz);
            float up = b.y - a.y;
            if (gap > kLinkGap || up > 1.15f || up < -8.0f) continue;
            float cx = ((a.x0 + a.x1) - (b.x0 + b.x1)) * 0.5f, cz = ((a.z0 + a.z1) - (b.z0 + b.z1)) * 0.5f;
            links[i].push_back({j, std::sqrt(cx * cx + cz * cz) + gap * 2 + std::max(up, 0.0f) * 2});
        }
    }
    // ladders: from the surface at the foot of each ladder up to the surfaces at its top
    ladderLinks.clear();
    for (const AABB& l : m.ladders) {
        Vec3 c = l.center();
        int bottom = nodeAt({c.x, l.mn.y + 0.05f, c.z});
        if (bottom < 0) continue;
        for (size_t j = 0; j < nodes.size(); j++) {
            const Node& n = nodes[j];
            if (n.y > l.mx.y + 0.2f || n.y < l.mx.y - 1.8f || (int)j == bottom) continue; // the climb volume ends ~1 m over the top floor
            float gx = std::max(0.0f, std::max(l.mn.x, n.x0) - std::min(l.mx.x, n.x1));
            float gz = std::max(0.0f, std::max(l.mn.z, n.z0) - std::min(l.mx.z, n.z1));
            if (gx * gx + gz * gz > 1.5f * 1.5f) continue;
            links[bottom].push_back({(int)j, (l.mx.y - l.mn.y) * 2 + 2});
            ladderLinks.push_back({{bottom, (int)j}, Vec3(c.x, l.mn.y, c.z)});
        }
    }
    // A* heuristic scale: the smallest cost per metre of straight-line distance between surface centres over all
    // links (ladders can be cheap for how far apart their surfaces' centres are), so it never overestimates.
    for (size_t i = 0; i < nodes.size(); i++)
        for (auto& [j, c] : links[i]) {
            const Node &a = nodes[i], &b = nodes[j];
            float dx = ((a.x0 + a.x1) - (b.x0 + b.x1)) * 0.5f, dz = ((a.z0 + a.z1) - (b.z0 + b.z1)) * 0.5f;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > 1e-3f) hScale = std::min(hScale, c / d);
        }
    hScale = std::max(0.0f, hScale * 0.999f);
}

int NavGraph::nodeAt(const Vec3& p) const {
    int best = -1;
    float bestY = -1e9f;
    auto test = [&](int i) {
        const Node& n = nodes[i];
        if (p.x < n.x0 - kNodePad || p.x > n.x1 + kNodePad || p.z < n.z0 - kNodePad || p.z > n.z1 + kNodePad) return;
        if (n.y > p.y + 0.6f || n.y < p.y - 6.0f) return; // the surface below us, not one overhead
        if (n.y > bestY) { bestY = n.y; best = i; }
    };
    if (gw > 0) {
        int c = cellOf(p.x, p.z);
        if (c < 0) return -1; // (every surface's padded rectangle is inside the grid)
        for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) test(cellNodes[k]);
    } else {
        for (size_t i = 0; i < nodes.size(); i++) test((int)i);
    }
    return best;
}

bool NavGraph::findPath(int a, int b, std::vector<int>& out) const {
    out.clear();
    if (a < 0 || b < 0 || a >= (int)nodes.size() || b >= (int)nodes.size()) return false;
    if (a == b) return true;
    // scratch reused between searches (stamped, so nothing is cleared per search)
    thread_local std::vector<float> g;
    thread_local std::vector<int> prev;
    thread_local std::vector<uint32_t> stamp;
    thread_local std::vector<std::pair<float, int>> heap;
    thread_local uint32_t cur = 0;
    if (g.size() < nodes.size()) { g.resize(nodes.size()); prev.resize(nodes.size()); stamp.assign(nodes.size(), 0); cur = 0; }
    if (++cur == 0) { std::fill(stamp.begin(), stamp.end(), 0); cur = 1; }
    auto cx = [&](int i) { return (nodes[i].x0 + nodes[i].x1) * 0.5f; };
    auto cz = [&](int i) { return (nodes[i].z0 + nodes[i].z1) * 0.5f; };
    const float bx = cx(b), bz = cz(b);
    auto h = [&](int i) { float dx = cx(i) - bx, dz = cz(i) - bz; return std::sqrt(dx * dx + dz * dz) * hScale; };
    heap.clear();
    stamp[a] = cur; g[a] = 0; prev[a] = -1;
    heap.push_back({-h(a), a});
    auto cmp = [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first < y.first; }; // max-heap on -f
    bool found = false;
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), cmp);
        auto [nf, u] = heap.back();
        heap.pop_back();
        if (-nf > g[u] + h(u) + 1e-4f) continue; // stale entry
        if (u == b) { found = true; break; }
        for (auto& [v, c] : links[u]) {
            float ng = g[u] + c;
            if (stamp[v] != cur || ng < g[v]) {
                stamp[v] = cur; g[v] = ng; prev[v] = u;
                heap.push_back({-(ng + h(v)), v});
                std::push_heap(heap.begin(), heap.end(), cmp);
            }
        }
    }
    if (!found) return false;
    for (int v = b; v != a; v = prev[v]) out.push_back(v);
    std::reverse(out.begin(), out.end());
    return true;
}

Vec3 NavGraph::steer(const Vec3& from, int prevNode, const std::vector<int>& path, const Vec3& to) const {
    // Walk through the "portal" between consecutive surfaces: the stretch of their shared edge (or the
    // next surface itself when they overlap, e.g. stepping down off a block). Clamping to the shared edge
    // keeps bots from cutting corners across a hole between two surfaces. If that point is right where we
    // stand, look further along the path.
    if (prevNode < 0) return to;
    for (int nodeIdx : path) {
        const Node& n = nodes[nodeIdx];
        const Node& pn = nodes[prevNode];
        bool ladder = false;
        for (auto& ll : ladderLinks)
            if (ll.first.first == prevNode && ll.first.second == nodeIdx) { // climb: walk into the ladder
                float dx = ll.second.x - from.x, dz = ll.second.z - from.z;
                if (dx * dx + dz * dz > 0.36f) return ll.second;
                ladder = true;
            }
        prevNode = nodeIdx;
        if (ladder) return {(n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f}; // on it: keep pushing toward the top
        auto span = [](float a0, float a1, float b0, float b1, float& lo, float& hi) {
            lo = std::max(a0, b0); hi = std::min(a1, b1);
            if (lo > hi) { lo = hi = (a1 < b0 ? b0 : b1); return; }        // no overlap: the near edge of the next surface
            float inset = std::min(0.5f, (hi - lo) * 0.5f);                  // keep away from the ends of the shared edge
            lo += inset; hi -= inset;
        };
        float x0, x1, z0, z1;
        span(pn.x0, pn.x1, n.x0, n.x1, x0, x1);
        span(pn.z0, pn.z1, n.z0, n.z1, z0, z1);
        Vec3 p = {clampf(from.x, x0, x1), n.y, clampf(from.z, z0, z1)};
        float dx = p.x - from.x, dz = p.z - from.z;
        if (dx * dx + dz * dz > 1.0f) return p;
    }
    return to;
}

void NavGraph::buildAsync(Map m) {
    auto j = std::make_shared<NavJob>();
    j->id = m.id();   // (hashed here, once: the thread and ready() only read it)
    j->map = std::move(m);
    job = j;
    std::thread([j] { j->result.build(j->map); j->done.store(true, std::memory_order_release); }).detach();
}

bool NavGraph::ready(const Map& m) {
    if (job && job->done.load(std::memory_order_acquire)) {
        auto j = std::move(job);
        if (j->result.mapId == m.id()) {
            uint32_t gen = generation;
            NavGraph& r = j->result;
            nodes = std::move(r.nodes); links = std::move(r.links); ladderLinks = std::move(r.ladderLinks);
            cellStart = std::move(r.cellStart); cellNodes = std::move(r.cellNodes);
            gx0 = r.gx0; gz0 = r.gz0; gw = r.gw; gh = r.gh; hScale = r.hScale; mapId = r.mapId;
            generation = gen + 1;
        }
    }
    if (mapId == m.id()) return true;
    if (!job || job->id != m.id()) buildAsync(m); // changed under us (map edits): rebuild in the background
    return false;
}

// =====================================================================================================
// Bots
// =====================================================================================================

namespace {

// Everything that makes one level play differently. Times are game seconds (or ms where marked).
struct SkillParams {
    // perception
    float fov;          // rad: full horizontal view cone in which someone is noticed
    float hearSteps;    // m: footsteps / dashes heard within this
    float hearGun;      // m: gunfire heard within this
    float soundErr;     // a heard position is off by about this fraction of the distance
    float memory;       // s: a last-seen position is hunted / aimed at for this long
    // reaction
    float reactMs;      // newly seen target -> starts aiming at it
    float reacquire;    // fraction of the reaction time for a target lost under a second ago
    // aim
    float visDelayMs;   // sees the target as it was this long ago...
    float predict;      // ...and extrapolates this fraction of that delay from its perceived velocity
    float velTau;       // s: perceived velocity catches up with a change of direction this fast
    float speedJudge;   // sd of the perceived speed (fraction)
    float trackBase;    // rad: tracking error sd on a still target
    float trackPerRad;  // rad of extra error sd per rad/s the target crosses the view
    float noiseTau;     // s: how slowly that error wanders
    float flickErr;     // sd of where a flick lands, as a fraction of its size
    float overshoot;    // mean overshoot of a flick (fraction)
    float settleTau;    // s: the flick's error is corrected this fast
    float trackGain;    // 1/s: correction gain while tracking
    float turnRate;     // rad/s: top hand speed
    float turnAccel;    // rad/s^2
    float fireCone;     // rad: clicks when the crosshair looks this close to the target
    float leadMean, leadSd; // projectile lead: fraction of the needed lead given, per engagement
    float aimHeight;    // m above the feet
    // movement
    float strafe;       // share of fight time spent dodging sideways
    float jukeRate;     // dodge direction changes per second
    float jumpRate;     // fight jumps per second
    float dashRate;     // fight dashes per second
    bool bhop;          // bunny-hops on routes
    float airStrafe;    // 0..1: how well it air-strafes while hopping
    bool grapple;       // hooks onto ledges / across open ground
    float runDash;      // dashes per second on long straight routes
    bool weapons;       // picks weapons by range (else rifle only)
};
const SkillParams kSkills[BOT_SKILLS] = {
    // beginner: slow to notice and react, aims late and wide, stands and shoots, walks everywhere
    {1.55f, 12, 45, 0.30f, 1.5f,   450, 0.75f,   160, 0.00f, 0.40f, 0.30f, 0.100f, 0.150f, 0.25f, 0.22f, 0.12f, 0.30f, 7,  4.0f, 25,  0.15f, 0.30f, 0.35f, 1.0f,
     0.35f, 1.0f, 0.0f, 0.0f, false, 0.0f, false, 0.0f, false},
    // intermediate
    {1.70f, 16, 55, 0.22f, 2.5f,   300, 0.65f,   120, 0.30f, 0.28f, 0.20f, 0.060f, 0.100f, 0.20f, 0.15f, 0.09f, 0.22f, 10, 6.0f, 40,  0.10f, 0.60f, 0.30f, 1.1f,
     0.70f, 1.4f, 0.10f, 0.05f, false, 0.0f, false, 0.0f, true},
    // advanced
    {1.85f, 20, 65, 0.15f, 3.5f,   200, 0.55f,    90, 0.60f, 0.16f, 0.12f, 0.045f, 0.075f, 0.15f, 0.10f, 0.06f, 0.15f, 15, 9.0f, 70,  0.07f, 0.85f, 0.20f, 1.2f,
     1.00f, 2.0f, 0.35f, 0.20f, true, 0.6f, true, 0.15f, true},
    // expert: strong but human (a 150 ms reaction, 60 ms visual lag, misjudges jukes)
    {2.00f, 24, 75, 0.10f, 5.0f,   150, 0.45f,    60, 0.80f, 0.09f, 0.07f, 0.030f, 0.055f, 0.12f, 0.06f, 0.03f, 0.09f, 22, 13.0f, 120, 0.04f, 0.95f, 0.12f, 1.3f,
     1.00f, 2.6f, 0.60f, 0.35f, true, 0.9f, true, 0.35f, true},
};

float rnd(BotBrain& b) { // xorshift, 0..1
    b.rng ^= b.rng << 13; b.rng ^= b.rng >> 17; b.rng ^= b.rng << 5;
    return (b.rng & 0xFFFFFF) / 16777216.0f;
}
float gauss(BotBrain& b) { // Box-Muller
    float u = std::max(rnd(b), 1e-7f), v = rnd(b);
    return std::sqrt(-2.0f * std::log(u)) * std::cos(2 * PI * v);
}
// An event that happens `rate` times a second on average: the chance over this tick (any tick length)
bool chance(BotBrain& b, float rate, float dt) { return rate > 0 && rnd(b) < 1.0f - std::exp(-rate * dt); }

float angleDiff(float a, float b) { return std::remainder(a - b, 2 * PI); }
float yawOf(const Vec3& d) { return std::atan2(-d.x, -d.z); }

Vec3 eyeOf(const ServerPlayer& p, float scale = 1.0f) {
    return p.move.pos + Vec3(0, ((p.move.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * scale, 0);
}

bool isEnemy(const Server& s, int a, int b) {
    if (a == b) return false;
    const auto& pb = s.players_[b];
    if (!pb.connected || !pb.alive || pb.team == TEAM_SPECTATOR) return false;
    if (s.players_[a].isBot && s.bots_[a].onlyEnemy >= 0) return b == s.bots_[a].onlyEnemy;
    if (isItMode(s.ms_.mode)) return s.players_[a].it && !pb.it; // only "it" hunts; everyone else runs
    if (!modeIsTeamBased(s.ms_.mode)) return true;
    return s.players_[a].team != pb.team;
}
// someone this bot watches out for: an enemy, or (hide and seek / tag, as a runner) whoever is "it"
bool isThreat(const Server& s, int a, int b) {
    if (isEnemy(s, a, b)) return true;
    const auto& pb = s.players_[b];
    return a != b && isItMode(s.ms_.mode) && pb.connected && pb.alive && pb.it && !s.players_[a].it;
}

bool canSee(const Server& s, const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float len = length(d);
    if (len < 0.01f) return true;
    return !s.map_.raycastAny(from, d / len, len - 0.1f);
}

// Where a player was `ms` before the last recorded tick (lag-compensation history): what the bot's eyes are
// reporting now. (Counted from the last recorded tick, which is also what a shot fired now is tested against,
// so the bot's lag behind the target is the same at every tick rate.)
Vec3 seenAgo(const Server& s, const ServerPlayer& t, float ms) {
    int ticks = 1 + (int)std::lround(ms * s.tickRate() * 0.001f);
    if (ticks >= ServerPlayer::kHistory || (int64_t)s.tick_ < ticks) return t.move.pos;
    const auto& h = t.history[(s.tick_ - ticks) % ServerPlayer::kHistory];
    int64_t want = (int64_t)s.timeMs_ - 1000 / s.tickRate() - (int64_t)ms;
    if (!h.alive || std::llabs((int64_t)h.tick - want) > 12) return t.move.pos; // (spawned since, or the rate changed)
    return h.pos;
}

// Ground all along a straight burst (dash): floor within a short drop every couple of metres, so a dash on a map
// with pits never ends over one.
bool groundAlong(const Server& s, const Vec3& from, const Vec3& dir, float len) {
    for (float t = 2.0f; t <= len + 0.01f; t += 2.0f)
        if (!s.map_.raycastAny(from + dir * t + Vec3(0, 0.6f, 0), {0, -1, 0}, 2.5f, true)) return false;
    return !s.map_.raycastAny(from + Vec3(0, 0.6f, 0), dir, len, true); // (and nothing in the way)
}

// Dropping straight down from p lands on something to stand on: not the void below the map, not lava.
bool safeDrop(const Server& s, const Vec3& p) {
    float depth = std::max(p.y - s.map_.killY(), 0.1f);
    float t = s.map_.raycast(p, {0, -1, 0}, depth);
    if (t < 0) return false;
    if (s.map_.lava.empty()) return true;
    Vec3 h = p - Vec3(0, t, 0);
    return !s.map_.inLava({h + Vec3(-0.3f, 0.0f, -0.3f), h + Vec3(0.3f, 0.6f, 0.3f)});
}

// A grapple pull from where the bot is, with the speed it has, toward `anchor`: momentum is kept and the pull only
// bends it, so the path curves (a sideways run swings wide). Simulated roughly (the pull, its top speed, half
// gravity, sliding along walls; 50 ms steps) up to where the bot lets go, then on in the direction it's flying for
// the skid. Fine on maps without pits; elsewhere every point must be over ground that's safe to drop onto. Checked
// when a hook is planned and again when it catches (the bot has moved by then, and the catch may be off the aim).
bool safeHook(const Server& s, const ServerPlayer& me, const Vec3& anchor) {
    if (!s.map_.hasVoid && s.map_.lava.empty()) return true;
    const auto& mt = moveTuning().m;
    const float accel = 38.0f * mt[MV_HOOK_ACCEL], top = 24.0f * mt[MV_HOOK_MAX], fall = 22.0f * 0.5f * mt[MV_HOOK_GRAVITY]; // (pmove.cpp)
    Vec3 p = me.move.pos, v = me.move.vel;
    const bool level = anchor.y < p.y + 1.0f;
    // a long pull strays metres off any line (one across a whole map: into whatever pit is near it)
    if ((anchor.x - p.x) * (anchor.x - p.x) + (anchor.z - p.z) * (anchor.z - p.z) > 22.0f * 22.0f) return false;
    {   // the anchor's near side must be ground too: a hook in the side of a pit drags you into it
        Vec3 back = {p.x - anchor.x, 0, p.z - anchor.z};
        float bl = length(back);
        if (bl > 0.1f && !safeDrop(s, anchor + back * (0.6f / bl) + Vec3(0, 0.4f, 0))) return false;
    }
    const float floorY = std::min(p.y, anchor.y) - 0.3f;   // (the ground under the pull holds it up)
    const float dt = 0.05f;
    for (int k = 0; k < 32; k++) {   // (1.6 s: as long as a bot holds on)
        Vec3 to = anchor - (p + Vec3(0, 1.0f, 0));
        float dist = length(to);
        float hsNow = std::sqrt(v.x * v.x + v.z * v.z);   // (where botThink lets go: a level pull sooner when fast)
        if (dist < 1.2f || length(anchor - p) < (level ? std::max(3.0f, hsNow * 0.3f) : 3.0f)) break;
        float before = length(v);
        v += to / dist * (accel * dt);
        float sp = length(v), cap = std::max(top, before);
        if (sp > cap) v = v * (cap / sp);
        v.y -= fall * dt;
        Vec3 hv = {v.x, 0, v.z};
        float hl = length(hv) * dt;
        Vec3 n;
        if (hl > 1e-3f && s.map_.raycast(p + Vec3(0, 0.6f, 0), hv / (hl / dt), hl + 0.4f, &n) >= 0 && n.y == 0) {
            float into = dot(v, n);   // a wall in the way: slide along it (that's where the pull takes you then)
            if (into < 0) v = v - n * into;
        }
        p += v * dt;
        p.y = std::max(p.y, floorY);
        // (and a metre either side: the real pull strays from this by about that, more on long pulls)
        Vec3 off = hl > 1e-3f ? Vec3(-hv.z, 0, hv.x) * (1.0f / (hl / dt)) : Vec3();
        if (!safeDrop(s, p + Vec3(0, 0.3f, 0)) || (k > 2 && (!safeDrop(s, p + off + Vec3(0, 0.3f, 0)) || !safeDrop(s, p - off + Vec3(0, 0.3f, 0))))) return false;
    }
    // let go here: it flies and skids on, give or take 17 degrees, unless a wall stops it first
    Vec3 d = {v.x, 0, v.z};
    float hs = length(d);
    if (hs < 1.0f) return true;
    d = d / hs;
    Vec3 side = {-d.z, 0, d.x};
    float reach = std::min(12.0f, 2.0f + hs * 0.45f);   // (in the air a moment, then a skid: ~0.45 s worth)
    float wall = s.map_.raycast(p + Vec3(0, 0.5f, 0), d, reach);
    float over = wall >= 0 ? wall : reach;
    for (float k = 1.0f; k <= over; k += 1.0f) {
        Vec3 q = p + d * k + Vec3(0, 0.3f, 0);
        if (!safeDrop(s, q) || !safeDrop(s, q + side * (0.3f * k)) || !safeDrop(s, q - side * (0.3f * k))) return false;
    }
    return true;
}

// ---- perception: look (view cone + line of sight) and listen, at 20 Hz of game time
constexpr double kLookEvery = 0.05;
void perceive(Server& s, int id, BotBrain& b, const SkillParams& sp, const Vec3& eye, double now) {
    const float scale = s.bodyScale();
    for (int i = 0; i < kMaxPlayers; i++) {
        BotAwareness& a = b.aware[i];
        if (!isThreat(s, id, i)) { a.visible = false; continue; }
        const ServerPlayer& o = s.players_[i];
        Vec3 chest = o.move.pos + Vec3(0, 1.1f * scale, 0), head = o.move.pos + Vec3(0, 1.6f * scale, 0);
        Vec3 d = chest - eye;
        float dist = length(d);
        bool vis = false;
        if (dist < 150.0f) {
            float off = std::fabs(angleDiff(yawOf(d), b.aimYaw));
            float pOff = std::fabs(std::asin(clampf(d.y / std::max(dist, 0.01f), -1, 1)) - b.aimPitch);
            bool inCone = (off <= sp.fov * 0.5f && pOff < 0.95f) || dist < 1.2f; // (bumping into someone: felt)
            if (inCone) vis = canSee(s, eye, head) || canSee(s, eye, chest);
            if (vis && !a.visible) { // just came into view: the reaction starts now (slower out of the corner of the eye)
                float react = sp.reactMs * 0.001f * (1.0f + 0.5f * off / (sp.fov * 0.5f)) * (dist > 40 ? 1.25f : 1.0f);
                if (now - a.seenAt < 1.0) react *= sp.reacquire; // lost behind cover a moment ago: expected back
                a.reactedAt = now + react;
            }
        }
        a.visible = vis;
        if (vis) {
            a.seenAt = now;
            a.seenPos = o.move.pos;
            a.seenVel = o.move.vel;
            continue;
        }
        // hearing: gunfire, running footsteps, dashes; the direction (and distance) only roughly
        float hs = std::sqrt(o.move.vel.x * o.move.vel.x + o.move.vel.z * o.move.vel.z);
        bool gun = now - o.lastFire < 0.2 && dist < sp.hearGun;
        bool steps = (o.move.flags & MF_ONGROUND) && hs > 3.0f && dist < sp.hearSteps;
        bool dash = o.move.dashTimer > 0 && dist < sp.hearSteps * 1.5f;
        if (gun || steps || dash) {
            if (now - a.heardAt > 0.6) { // a new sound: a fresh guess at where it came from
                float err = dist * sp.soundErr;
                a.heardPos = o.move.pos + Vec3(gauss(b) * err, 0, gauss(b) * err);
            } else { // the same source again: the guess firms up a little
                a.heardPos = a.heardPos + (o.move.pos - a.heardPos) * 0.15f;
            }
            a.heardAt = now;
        }
    }
}

// Where the bot wants to go when it isn't fighting: the mode's objective, else roam.
Vec3 objective(Server& s, int id, BotBrain& b, const SkillParams& sp) {
    const ModeState& m = s.ms_;
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
    const double now = s.time();
    switch (m.mode) {
        case MODE_RACE:
            if (me.checkpoint < (int)map.checkpoints.size()) return map.checkpoints[me.checkpoint].center();
            break;
        case MODE_KOTH:
            if (map.hasHill) return {map.hill.center().x, map.hill.mn.y, map.hill.center().z}; // the hill's floor
            break;
        case MODE_PAYLOAD:
        case MODE_NPAYLOAD:
            if (map.payloadPath.size() >= 2) return map.cartPoint(m.mode, m.attackers, m.payloadDist);
            break;
        case MODE_POINTS:   // both sides go to the point under attack (its floor)
            if (m.point < map.points.size()) { const AABB& p = map.points[m.point]; return {p.center().x, p.mn.y, p.center().z}; }
            break;
        case MODE_BOMB: {
            bool att = me.team == m.attackers;
            if (m.bombState == BOMB_PLANTED) return m.bombPos;
            if (m.bombState == BOMB_DROPPED && att) return m.bombPos;
            if (!map.bombSites.empty()) {
                // carrier (and attackers) head for the nearest site; defenders guard a site each
                const AABB& site = map.bombSites[(att ? 0 : id) % map.bombSites.size()];
                if (att && m.bombState == BOMB_CARRIED && m.bombCarrier != id) {
                    const auto& c = s.players_[m.bombCarrier];
                    if (c.connected && c.alive) return c.move.pos; // escort (a teammate: shown on the HUD)
                }
                return site.center();
            }
            break;
        }
        case MODE_HIDE:
        case MODE_TAG: {
            // (the graph is still the previous map's while this one's builds: roam between spawns until it's ready)
            if (s.nav_.nodes.empty() || s.nav_.mapId != map.id()) break;
            auto randomNode = [&]() { const auto& n = s.nav_.nodes[(size_t)(rnd(b) * s.nav_.nodes.size()) % s.nav_.nodes.size()];
                                      return Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f); };
            if (me.it) { // seek: wander everywhere, toward anything heard or the locator's last find (targets are chased by the fight code once seen)
                if (now - me.locatedAt < 0.1) { b.goal = me.locatedPos; b.goalUntil = now + 6.0; }
                for (int i = 0; i < kMaxPlayers; i++)
                    if (isEnemy(s, id, i) && now - b.aware[i].heardAt < 1.0 && now - b.aware[i].seenAt > 1.0) { b.goal = b.aware[i].heardPos; b.goalUntil = now + 4.0; }
                if (now > b.goalUntil || length(b.goal - me.move.pos) < 2.0f) { b.goal = randomNode(); b.goalUntil = now + 12.0; }
                return b.goal;
            }
            // only what this runner has seen or heard of "it" (no knowing where it is through walls)
            int it = -1;
            for (int i = 0; i < kMaxPlayers; i++) if (i != id && s.players_[i].connected && s.players_[i].alive && s.players_[i].it) it = i;
            Vec3 itPos;
            bool threat = false;
            if (it >= 0) {
                const BotAwareness& a = b.aware[it];
                float range = m.mode == MODE_TAG ? 30.0f : 18.0f;
                if (now - a.seenAt < 0.8 && length(a.seenPos - me.move.pos) < range) { threat = true; itPos = a.seenPos; }
                else if (now - a.heardAt < 0.8 && length(a.heardPos - me.move.pos) < range * 0.5f) { threat = true; itPos = a.heardPos; }
            }
            if (threat && now > b.fleeUntil) { // run: the reachable-looking spot farthest from it
                Vec3 best = b.goal;
                float bestScore = -1e9f;
                for (int k = 0; k < 12; k++) {
                    Vec3 c = randomNode();
                    float sc = length(c - itPos) - 0.5f * length(c - me.move.pos);
                    if (sc > bestScore) { bestScore = sc; best = c; }
                }
                b.goal = best; b.goalUntil = now + 6.0; b.fleeUntil = now + 1.5 + (1.0f - sp.predict) * 0.8f;
                return b.goal;
            }
            if (m.mode == MODE_HIDE && !threat) { // pick a hiding spot once and stay put
                if (now > b.goalUntil) { b.goal = randomNode(); b.goalUntil = now + 90.0; }
                if (length(b.goal - me.move.pos) < 1.2f) return me.move.pos;
                return b.goal;
            }
            if (now > b.goalUntil || length(b.goal - me.move.pos) < 2.0f) { b.goal = randomNode(); b.goalUntil = now + 10.0; }
            return b.goal;
        }
        default: break;
    }
    // no objective: go after what was last seen or heard, else roam between spawn points
    double bestT = -1e9; Vec3 lead;
    for (int i = 0; i < kMaxPlayers; i++) {
        const BotAwareness& a = b.aware[i];
        if (now - a.seenAt >= sp.memory && now - a.heardAt >= 3.0) continue; // (nothing recent: skip the checks)
        if (!isEnemy(s, id, i)) continue;
        if (now - a.seenAt < sp.memory && a.seenAt > bestT) { bestT = a.seenAt; lead = a.seenPos; }
        else if (now - a.heardAt < 3.0 && a.heardAt > bestT) { bestT = a.heardAt; lead = a.heardPos; }
    }
    if (bestT > -1e9 && length(lead - me.move.pos) > 2.0f) return lead;
    if (map.spawns.empty()) return me.move.pos;
    if (now > b.goalUntil || length(b.goal - me.move.pos) < 2.0f) {
        const auto& spn = map.spawns[(size_t)(rnd(b) * map.spawns.size()) % map.spawns.size()];
        b.goal = spn.pos;
        b.goalUntil = now + 8.0 + rnd(b) * 6.0;
    }
    return b.goal;
}

// Best weapon for the range that this mode allows (1v1 rulesets / duel lives restrict the pool).
// Beginners stick to the rifle when they can; levels that can't lead well prefer hitscan.
int pickWeapon(const Server& s, const SkillParams& sp, float dist) {
    static const int close[] = {WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_BUBBLE, WPN_ROCKET, WPN_RIFLE, WPN_STICKY, WPN_CROSSBOW, WPN_SNIPER, WPN_SYRINGE};
    static const int mid[]   = {WPN_ROCKET, WPN_LASER, WPN_RIFLE, WPN_STICKY, WPN_BUBBLE, WPN_CROSSBOW, WPN_SHOTGUN, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE};
    static const int midNoLead[] = {WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_ROCKET, WPN_STICKY, WPN_CROSSBOW, WPN_BUBBLE, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE};
    static const int far[]   = {WPN_CROSSBOW, WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE};
    static const int farNoLead[] = {WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_CROSSBOW, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE};
    static const int vfar[]  = {WPN_SNIPER, WPN_CROSSBOW, WPN_RIFLE, WPN_ROCKET, WPN_STICKY, WPN_LASER, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE};
    if (!sp.weapons && s.weaponAllowed(WPN_RIFLE)) return WPN_RIFLE;
    bool lead = sp.leadMean >= 0.8f;
    const int* order = dist < 7 ? close : dist < 18 ? (lead ? mid : midNoLead) : dist < 35 ? (lead ? far : farNoLead) : (lead ? vfar : farNoLead);
    for (size_t k = 0; k < sizeof close / sizeof close[0]; k++) if (s.weaponAllowed(order[k])) return order[k];   // (every list has every weapon but the locator)
    return WPN_RIFLE;
}

// Repath budget: route searches per server tick across all bots (the rest wait a tick: no spikes).
constexpr int kPathsPerTick = 2;
thread_local uint32_t g_pathTick = ~0u;   // (per thread: a process can host several servers)
thread_local int g_pathsThisTick = 0;

} // namespace

const char* botSkillName(int skill) {
    static const char* n[BOT_SKILLS] = {"BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"};
    return n[skill < 0 || skill >= BOT_SKILLS ? 1 : skill];
}

UserCmd botThink(Server& s, int id) {
    ServerPlayer& me = s.players_[id];
    BotBrain& b = s.bots_[id];
    const SkillParams& sp = kSkills[b.skill < 0 || b.skill >= BOT_SKILLS ? 1 : b.skill];
    const double now = s.time(); const float dt = s.dt();
    if (b.script) return b.script(s, id);
    UserCmd cmd;
    cmd.weapon = kNoWeaponChange;
    bool dashJump = false;   // this tick's dash is meant as a dash jump (experts on long straight routes)
    bool navOk = s.nav_.ready(s.map_);
    // a goal from the previous map, or picked before this map's graph was ready, is picked again
    if (b.goalMapId != s.map_.id() || (navOk && b.goalNavGen != s.nav_.generation)) {
        b.goalMapId = s.map_.id();
        if (navOk) b.goalNavGen = s.nav_.generation;
        b.goalUntil = 0; b.fleeUntil = 0;
    }
    if (!me.alive) {
        b.target = -1;
        b.engagedAt = -1;
        for (auto& a : b.aware) { a.visible = false; a.seenAt = -1e9; a.heardAt = -1e9; }
        b.path.clear(); b.needPath = true;
        cmd.yaw = b.aimYaw;
        return cmd;
    }
    Vec3 eye = eyeOf(me, s.bodyScale());

    // --- perception (20 Hz of game time, each bot at its own phase)
    bool damageOn = (s.ms_.phase == PHASE_LIVE || s.ms_.phase == PHASE_WARMUP) && !(s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP);
    if (b.nextLook <= 0) b.nextLook = now + (id % 10) * (kLookEvery / 10); // spread the bots' looks over the 50 ms
    if (now >= b.nextLook) {
        b.nextLook = std::max(b.nextLook + kLookEvery, now + kLookEvery * 0.5);
        { QZONE("bot perceive"); perceive(s, id, b, sp, eye, now); }
        // Edges: the void or lava within a stumble, a knockback or a hop (maps that have them). Eight directions at
        // 1.5 and 3 m (near: no jumping about), and 5 m (a fight drifts away from it: a laser or a blast pushes you
        // that far); the nearer one weighs more in which way is away.
        b.nearEdge = b.edgeFar = false;
        if (s.map_.hasVoid || !s.map_.lava.empty()) {
            Vec3 away;
            for (int k = 0; k < 8; k++) {
                Vec3 dir = {std::cos(k * PI / 4), 0, std::sin(k * PI / 4)};
                if (!safeDrop(s, me.move.pos + dir * 1.5f + Vec3(0, 0.5f, 0))) { away = away - dir * 2.0f; b.nearEdge = true; }
                else if (!safeDrop(s, me.move.pos + dir * 3.0f + Vec3(0, 0.5f, 0))) { away = away - dir; b.nearEdge = true; }
                else if (!safeDrop(s, me.move.pos + dir * 5.0f + Vec3(0, 0.5f, 0))) { away = away - dir * 0.5f; b.edgeFar = true; }
            }
            float l = length(away);
            b.edgeAway = l > 1e-3f ? away / l : Vec3();
        }
        // target: someone in view (nearest to the crosshair and closest, the current one preferred), else the
        // last one seen while it's still remembered (hunted)
        int best = -1;
        float bestScore = 1e9f;
        for (int i = 0; i < kMaxPlayers && damageOn; i++) {
            if (!b.aware[i].visible || !isEnemy(s, id, i)) continue;
            Vec3 d = s.players_[i].move.pos + Vec3(0, 1.1f, 0) - eye;
            float sc = length(d) + std::fabs(angleDiff(yawOf(d), b.aimYaw)) * 12.0f - (i == b.target ? 6.0f : 0.0f);
            if (sc < bestScore) { bestScore = sc; best = i; }
        }
        if (best < 0 && b.target >= 0 && damageOn && isEnemy(s, id, b.target) && now - b.aware[b.target].seenAt < sp.memory) best = b.target;
        if (best != b.target) {
            b.target = best;
            b.engagedAt = -1;
            b.rejudgeAt = 0;
            if (best >= 0) b.velEst = b.aware[best].seenVel * 0.5f;
        }
    }
    if (b.target >= 0 && !isEnemy(s, id, b.target)) { b.target = -1; b.engagedAt = -1; }

    // Objective movement (nav route to the mode's goal), also used while fighting when the
    // objective can't wait: carrying the bomb, or defusing a planted one.
    auto objectiveMove = [&](Vec3& dirOut, uint16_t& buttons, const Vec3* override) {
        Vec3 goal = override ? *override : objective(s, id, b, sp);
        if (navOk && b.pathGen != s.nav_.generation) { b.path.clear(); b.pathPrev = -1; b.needPath = true; b.pathGen = s.nav_.generation; }
        Vec3 toWp = b.waypoint - me.move.pos;
        bool steerNow = now >= b.nextSteer || b.needPath || toWp.x * toWp.x + toWp.z * toWp.z < 1.0f || length(goal - b.pathGoal) > 1.0f;
        if (navOk && steerNow) { // (route progress and the next waypoint: every 20 ms of game time, or when needed)
            b.nextSteer = now + 0.02;
            int cur = s.nav_.nodeAt(me.move.pos + Vec3(0, 0.1f, 0));
            // follow the kept route: drop the surfaces already reached; off it (fell, pushed): find a new one
            if (cur >= 0 && cur != b.pathPrev) {
                auto it = std::find(b.path.begin(), b.path.end(), cur);
                if (it != b.path.end()) { b.path.erase(b.path.begin(), it + 1); b.pathPrev = cur; }
                else b.needPath = true;
            }
            if (length(goal - b.pathGoal) > 1.0f) {
                int gn = s.nav_.nodeAt(goal + Vec3(0, 0.5f, 0));
                if (gn != b.pathGoalNode) b.needPath = true;
                b.pathGoal = goal;
            }
            if (now >= b.repathAt) b.needPath = true;
            if (b.needPath && cur >= 0) {
                if (g_pathTick != s.tick_) { g_pathTick = s.tick_; g_pathsThisTick = 0; }
                if (g_pathsThisTick < kPathsPerTick) {
                    g_pathsThisTick++;
                    QZONE("bot nav path");
                    b.pathGoalNode = s.nav_.nodeAt(goal + Vec3(0, 0.5f, 0));
                    b.pathGoal = goal;
                    if (!s.nav_.findPath(cur, b.pathGoalNode, b.path)) b.path.clear();
                    b.pathPrev = cur;
                    b.needPath = false;
                    b.repathAt = now + 1.5 + rnd(b) * 1.0; // (a moving goal re-plans when its surface changes)
                }
            }
            b.waypoint = b.path.empty() || b.pathPrev < 0 ? goal : s.nav_.steer(me.move.pos, b.pathPrev, b.path, goal);
        } else if (!navOk) {
            b.waypoint = goal; // (the graph is still building: head straight for it)
            b.pathGoal = goal;
        }
        Vec3 to = b.waypoint - me.move.pos;
        to.y = 0;
        float d = length(to);
        dirOut = d > 0.6f ? to / d : Vec3();
        if (d > 0.6f && now < b.unstickUntil && b.target < 0) dirOut += Vec3(-dirOut.z, 0, dirOut.x) * b.strafeDir; // unsticking
        const ModeState& m = s.ms_;
        const AABB hull = playerBox(me.move);   // (the server's own zone test: size modifiers and crouching included)
        if (m.mode == MODE_BOMB) {
            bool inSite = false;
            for (auto& site : s.map_.bombSites) inSite |= hull.overlaps(site);
            if (m.bombState == BOMB_CARRIED && m.bombCarrier == id && inSite) { buttons |= BTN_USE; dirOut = {}; }
            if (m.bombState == BOMB_PLANTED && me.team != m.attackers && length(m.bombPos - me.move.pos) < 1.5f) { buttons |= BTN_USE; dirOut = {}; }
        }
        if (m.mode == MODE_KOTH && s.map_.hasHill && hull.overlaps(s.map_.hill)) dirOut = dirOut * 0.3f;
        if (m.mode == MODE_POINTS && m.point < s.map_.points.size() && hull.overlaps(s.map_.points[m.point])) dirOut = dirOut * 0.3f;
    };
    const ModeState& ms = s.ms_;
    bool urgent = ms.mode == MODE_BOMB && ((ms.bombState == BOMB_CARRIED && ms.bombCarrier == id) ||
                                           (ms.bombState == BOMB_PLANTED && me.team != ms.attackers));

    float wantYaw = b.aimYaw, wantPitch = 0;
    float ffYaw = 0;             // feed-forward turn speed (tracking a target crossing the view)
    float gain = 20.0f;          // correction gain (aim: the level's; just looking around: relaxed)
    Vec3 moveDir;                // world-space desired movement (xz)
    // Hooked on a grapple (fighting or not: letting go mid-pull flings you off at full speed wherever you are).
    auto holdHook = [&] {
        const Vec3& at = me.move.hookPoint;
        cmd.buttons |= BTN_GRAPPLE;
        // caught: the pull checked again from here, as we move now (it was planned before the aim got there, and
        // the catch may not be quite where it was aimed); not safe: let go at once, while still slow
        if (!b.hookChecked) {
            b.hookChecked = true;
            b.hookUntil = now + 1.6;   // (the 1.6 s of pull the check simulates count from the catch, not from the plan)
            // (latched onto a player: both are pulled to the middle, so that's where the pull ends)
            Vec3 ends = me.move.hookTarget ? (me.move.pos + Vec3(0, 1.0f, 0) + at) * 0.5f : at;
            if (!safeHook(s, me, ends)) { b.hookUntil = 0; cmd.buttons &= ~BTN_GRAPPLE; return; }
        }
        // keys along the pull: air control would bend the path off it
        Vec3 along = {at.x - me.move.pos.x, 0, at.z - me.move.pos.z};
        if (length(along) > 0.5f) moveDir = normalize(along);
        // arrived: let go (a level pull at speed sooner, on maps with pits: the momentum carries on past it)
        float hsNow = std::sqrt(me.move.vel.x * me.move.vel.x + me.move.vel.z * me.move.vel.z);
        float letGo = (s.map_.hasVoid || !s.map_.lava.empty()) && at.y < me.move.pos.y + 1.0f ? std::max(3.0f, hsNow * 0.3f) : 3.0f;
        if (length(at - me.move.pos) < letGo) { b.hookUntil = 0; cmd.buttons &= ~BTN_GRAPPLE; }
    };
    bool fighting = false;
    const BotAwareness* ta = b.target >= 0 ? &b.aware[b.target] : nullptr;
    bool reacted = ta && ta->visible && now >= ta->reactedAt;
    if (b.target >= 0 && (reacted || !ta->visible)) {
        // ---- fight: aim at the target (seen), or where it went (remembered)
        fighting = true;
        const ServerPlayer& t = s.players_[b.target];
        if (now >= b.rejudgeAt) { // misjudgements, rolled per engagement and every second or so
            b.leadFactor = std::max(0.0f, sp.leadMean + gauss(b) * sp.leadSd);
            b.dropFactor = std::max(0.3f, 1.0f + gauss(b) * sp.leadSd * 0.7f);
            b.speedJudge = std::max(0.3f, 1.0f + gauss(b) * sp.speedJudge);
            b.rejudgeAt = now + 0.7 + rnd(b) * 0.8;
        }
        Vec3 perceived;
        if (ta->visible) {
            perceived = seenAgo(s, t, sp.visDelayMs);
            Vec3 older = seenAgo(s, t, sp.visDelayMs + 16.0f);
            Vec3 vObs = (perceived - older) / 0.016f;
            if (length(vObs) < 40.0f) { // (not a respawn / teleport)
                float k = 1.0f - std::exp(-dt / sp.velTau);
                b.velEst += (vObs * b.speedJudge - b.velEst) * k;
            }
            perceived += b.velEst * (sp.visDelayMs * 0.001f * sp.predict);
        } else { // behind cover: where it should be by now if it kept going
            perceived = ta->seenPos + ta->seenVel * (float)std::min(now - ta->seenAt, 0.5) * sp.predict;
        }
        Vec3 aimAt = perceived + Vec3(0, sp.aimHeight * s.bodyScale(), 0);
        float dist = length(aimAt - eye);
        int w = b.forceWeapon >= 0 ? b.forceWeapon : pickWeapon(s, sp, dist);
        if (now < me.backAt(w) && me.weapon != w) w = me.weapon; // thrown it (knife / syringe): use what we switched to
        const WeaponDef& wd = weaponDef(w);
        bool throwKnife = w == WPN_KNIFE && dist > wd.range + 0.5f && dist < 18.0f && sp.leadMean >= 0.8f && !isItMode(s.ms_.mode); // out of reach: throw it
        if (throwKnife) {
            float tf = dist / kKnifeThrowSpeed;
            aimAt += b.velEst * (tf * b.leadFactor);
            aimAt.y += 0.5f * kKnifeThrowGravity * tf * tf * b.dropFactor;
        }
        if (wd.projectile) {
            float tf = dist / wd.projSpeed;
            aimAt += b.velEst * (tf * b.leadFactor);
            if (w == WPN_ROCKET && sp.leadMean >= 0.8f) aimAt.y = perceived.y + 0.1f; // splash at the feet
            if (wd.gravity > 0) aimAt.y += 0.5f * wd.gravity * tf * tf * b.dropFactor;
        }
        Vec3 d = normalize(aimAt - eye);
        float tgtYaw = yawOf(d), tgtPitch = std::asin(clampf(d.y, -1, 1));
        // how fast the target crosses the view (perceived): tracking error grows with it; the hand follows it
        Vec3 rel = b.velEst - me.move.vel;
        float h2 = d.x * d.x + d.z * d.z;   // (d/dt of yawOf: (dz vx - dx vz) / horizontal distance^2)
        float angSpeed = h2 > 1e-4f ? (d.z * rel.x - d.x * rel.z) / (h2 * std::max(dist, 1.0f)) : 0.0f; // rad/s of yaw
        // new engagement: the flick lands off by a fraction of its size (overshooting more often than not)
        if (b.engagedAt < 0 && ta->visible) {
            b.engagedAt = now;
            float fy = angleDiff(tgtYaw, b.aimYaw), fp = tgtPitch - b.aimPitch;
            float amp = std::sqrt(fy * fy + fp * fp);
            if (amp > 0.05f) {
                float along = sp.overshoot + gauss(b) * sp.flickErr, across = gauss(b) * sp.flickErr * 0.5f;
                b.flickYaw = (fy * along - fp * across);
                b.flickPitch = (fp * along + fy * across) * 0.6f;
            }
        }
        // tracking error: a random process in game time (exact for any tick length)
        float sigma = sp.trackBase + sp.trackPerRad * std::fabs(angSpeed);
        float a = std::exp(-dt / sp.noiseTau), kk = std::sqrt(std::max(0.0f, 1.0f - a * a));
        b.noiseYaw = b.noiseYaw * a + sigma * kk * gauss(b);
        b.noisePitch = b.noisePitch * a + sigma * 0.6f * kk * gauss(b);
        float settle = std::exp(-dt / sp.settleTau);
        b.flickYaw *= settle; b.flickPitch *= settle;
        wantYaw = tgtYaw + b.noiseYaw + b.flickYaw;
        wantPitch = tgtPitch + b.noisePitch + b.flickPitch;
        ffYaw = angSpeed * sp.predict;
        gain = sp.trackGain;
        if (me.weapon != w) cmd.weapon = (uint8_t)w;
        if (ta->visible) {
            // click once the crosshair looks on the target (where the bot sees it, not where it really is)
            float err = std::fabs(angleDiff(b.aimYaw, tgtYaw)) + std::fabs(b.aimPitch - tgtPitch);
            bool ready = err < sp.fireCone + 0.35f / std::max(dist, 1.0f);
            bool knifeOut = w == WPN_KNIFE && !throwKnife && dist > wd.range + 0.3f; // closing in to stab: don't swing at air
            // a blast weapon with a wall (or the floor) right in front: the splash would be ours (a self-kill)
            const WeaponDef& held = weaponDef(me.weapon);
            auto ownBlast = [&] { return held.splashRadius > 0 && !held.sticky && !(s.arenaRules() & AR_NO_SELF_DAMAGE) &&
                                         s.map_.raycast(eye, angleForward(b.aimYaw, b.aimPitch), held.splashRadius * 0.8f) >= 0; };
            if (ready && !knifeOut && !ownBlast()) cmd.buttons |= throwKnife ? BTN_ALTFIRE : BTN_FIRE;
            if (w == WPN_SNIPER) cmd.buttons |= BTN_ALTFIRE;
            if (w == WPN_RIFLE && dist > 30.0f && sp.leadMean >= 0.8f) cmd.buttons |= BTN_ALTFIRE; // scope in for long shots
            if (me.weapon == WPN_STICKY) { // set off our stickies when the target (as seen) is in the blast, or one is stuck on an enemy
                for (const auto& pr : s.projectiles_)
                    if (pr.active && pr.owner == id && pr.type == WPN_STICKY &&
                        (length(pr.pos - (perceived + Vec3(0, 0.9f, 0))) < weaponDef(WPN_STICKY).splashRadius * 0.7f || (pr.onPlayer >= 0 && isEnemy(s, id, pr.onPlayer))) &&
                        length(pr.pos - (me.move.pos + Vec3(0, 0.9f, 0))) > weaponDef(WPN_STICKY).splashRadius) {   // (and we're clear of it)
                        cmd.buttons |= BTN_ALTFIRE;
                        break;
                    }
            }
            // semi-auto: let go between shots (a click is buffered until the weapon is ready)
            if (weaponDef(me.weapon).semiAuto && (me.buttons & BTN_FIRE)) cmd.buttons &= ~BTN_FIRE;
            // switching weapons: let go first (a button held through a switch is locked until it's released)
            if (me.weapon != w || me.fireLocked) cmd.buttons &= ~(BTN_FIRE | BTN_ALTFIRE);
            // advanced and up, a close-range weapon out: now and then hook the target to drag the fight in (the hook
            // latches onto an enemy it hits and pulls the two together). Held by holdHook below once it catches.
            const bool closeWeapon = w == WPN_SHOTGUN || w == WPN_KNIFE || w == WPN_LASER || w == WPN_BUBBLE;
            const float hookRange = kGrappleRange * moveTuning().m[MV_HOOK_RANGE];
            const bool onTarget = err < 0.35f / std::max(dist, 1.0f);   // (the hook needs the body under the crosshair)
            if (now < b.fightHookUntil) {
                if (me.move.flags & MF_HOOKED) {   // caught: them (held by holdHook below), or the map behind them (let go)
                    b.fightHookUntil = 0;
                    if (me.move.hookTarget) { b.hookUntil = now + 1.6; b.hookChecked = false; b.hookAnchor = me.move.hookPoint; }
                    else b.hookUntil = 0;
                } else if (onTarget && !(me.move.flags & MF_HOOKHELD)) {
                    cmd.buttons |= BTN_GRAPPLE;   // (fires once the aim is on them)
                }
            } else if (sp.grapple && closeWeapon && moveTuning().grapple && !(me.move.mods & MOVE_RUN_ONLY) && s.ms_.mode != MODE_RACE &&
                !(me.move.flags & (MF_HOOKED | MF_HOOKHELD)) && me.move.hookCooldown <= 0 && now >= b.nextHookTry &&
                dist > 7.0f && dist < hookRange * 0.8f) {
                b.nextHookTry = now + 0.5;
                // (both are pulled to the middle: that's where the pull ends, and it must be safe ground)
                if (rnd(b) < (sp.airStrafe > 0.8f ? 0.5f : 0.3f) && safeHook(s, me, (me.move.pos + perceived) * 0.5f + Vec3(0, 1.0f, 0))) {
                    b.fightHookUntil = now + 0.3;
                    if (onTarget) cmd.buttons |= BTN_GRAPPLE;
                }
            }
        }
        // fight movement: keep a good range for the weapon, dodge
        Vec3 to = perceived - me.move.pos;
        to.y = 0;
        Vec3 toN = normalize(to);
        float ideal = w == WPN_SHOTGUN ? 4.0f : w == WPN_SNIPER ? 40.0f : w == WPN_KNIFE ? 1.5f : w == WPN_LASER ? 12.0f
                    : w == WPN_STICKY ? 10.0f : w == WPN_BUBBLE ? 7.0f : w == WPN_CROSSBOW ? 22.0f : w == WPN_RIFLE ? 18.0f : 14.0f;
        if (ta->visible) {
            moveDir = toN * (dist > ideal + 3 ? 1.0f : dist < ideal - 3 ? -1.0f : 0.0f);
            // decisions at a fixed game-time rate (not per tick): strafe or plant your feet, change direction
            if (now >= b.strafeSwitchAt) {
                b.strafeDir = rnd(b) < sp.strafe ? (rnd(b) < 0.5f ? -1.0f : 1.0f) : 0.0f;
                b.strafeSwitchAt = now + (0.15 + rnd(b) * 2.0 / sp.jukeRate);
            }
            Vec3 side = Vec3(-toN.z, 0, toN.x) * b.strafeDir;
            if (b.nearEdge || b.edgeFar) { // fighting by a drop: dodge along it or away, never toward it, and step back from it
                if (dot(side, b.edgeAway) < -0.2f) { b.strafeDir = -b.strafeDir; side = side * -1.0f; }
                moveDir += b.edgeAway * (b.nearEdge ? 1.5f : 0.7f);
            }
            moveDir += side;
            // held by the target's grapple and wanting range (no close-range weapon): advanced and up dash out of it
            if ((me.move.flags & MF_PULLED) && sp.grapple && ideal > 6.0f && me.move.dashCooldown <= 0 && !b.nearEdge && !b.edgeFar &&
                chance(b, 3.0f, dt)) {
                Vec3 out = Vec3(-toN.z, 0, toN.x) * (b.strafeDir != 0 ? b.strafeDir : 1.0f);
                if (groundAlong(s, me.move.pos, out, 10.0f)) { moveDir = out; cmd.buttons |= BTN_DASH; }
            }
            // (no jumps or dashes within 5 m of a drop: a hit in the air there, a blast or a laser push, is a fall)
            if ((me.move.flags & MF_ONGROUND) && !b.nearEdge && !b.edgeFar) {
                if (chance(b, sp.jumpRate, dt)) cmd.buttons |= BTN_JUMP;
                if (me.move.dashCooldown <= 0 && chance(b, sp.dashRate, dt)) {
                    if (dist > 20) moveDir = toN;                                  // close in
                    else moveDir = Vec3(-toN.z, 0, toN.x) * (b.strafeDir != 0 ? b.strafeDir : 1.0f); // dodge sideways
                    if (groundAlong(s, me.move.pos, normalize(moveDir), 10.0f)) cmd.buttons |= BTN_DASH;
                }
            }
        } else { // hunt: go to where it was last seen
            Vec3 lastSeen = ta->seenPos;
            objectiveMove(moveDir, cmd.buttons, &lastSeen);
        }
        if (urgent) { // keep going for the objective, shooting on the way
            uint16_t extra = 0;
            objectiveMove(moveDir, extra, nullptr);
            if (extra & BTN_USE) cmd.buttons = (uint16_t)((cmd.buttons & ~BTN_FIRE) | BTN_USE);
        }
        if ((me.move.flags & MF_HOOKED) && now < b.hookUntil) holdHook();
    } else {
        if (b.target < 0 || !ta->visible) b.engagedAt = -1;
        b.flickYaw = b.flickPitch = 0;
        uint16_t extra = 0;
        objectiveMove(moveDir, extra, nullptr);
        cmd.buttons |= extra;
        if (isItMode(ms.mode) && me.it && ms.phase == PHASE_LIVE) {   // seeking / tagging, nobody in sight: the locator when it's ready
            if (now >= me.locateAt) {
                if (me.weapon != WPN_LOCATOR) cmd.weapon = WPN_LOCATOR;
                else if (!me.fireLocked && !(me.buttons & BTN_FIRE)) cmd.buttons |= BTN_FIRE;
            } else if (me.weapon != WPN_KNIFE) cmd.weapon = WPN_KNIFE;
        }
        gain = 10.0f;
        bool idle = length(moveDir) < 1e-3f;
        if (!idle) wantYaw = yawOf(normalize(moveDir));
        wantPitch = 0;
        // look toward a fresh sound, else glance around now and then (that's how flanks get noticed)
        double heardT = -1e9; Vec3 heard;
        for (int i = 0; i < kMaxPlayers; i++)
            if (b.aware[i].heardAt > heardT && now - b.aware[i].heardAt < 1.2 && isThreat(s, id, i)) { heardT = b.aware[i].heardAt; heard = b.aware[i].heardPos; }
        bool spotted = b.target >= 0 && ta->visible; // seen, still reacting: eyes stay on it, not on sounds
        if (spotted) {
            wantYaw = b.aimYaw;
            wantPitch = b.aimPitch;
        } else if (heardT > -1e9) {
            Vec3 d = heard + Vec3(0, 1.1f, 0) - eye;
            wantYaw = yawOf(d);
            wantPitch = std::asin(clampf(d.y / std::max(length(d), 0.01f), -1, 1));
            gain = 14.0f;
        } else {
            if (now >= b.nextGlance) { // (moving: off the route's heading; standing: a new direction to face)
                float off = (rnd(b) * 2 - 1) * (idle ? 2.6f : 1.6f);
                b.glanceYaw = idle ? std::remainder(b.aimYaw + off, 2 * PI) : off;
                b.glanceUntil = now + (idle ? 1.0 + rnd(b) * 1.5 : 0.35 + rnd(b) * 0.4);
                b.nextGlance = now + 1.5 + rnd(b) * 3.0;
            }
            if (now < b.glanceUntil) wantYaw = idle ? b.glanceYaw : wantYaw + b.glanceYaw;
        }
        // Grapple (advanced and up, or when it's the only way to move): up to a ledge on the route, or across a
        // long open stretch. Only when the hook would catch near that point; hold until close or out of time.
        bool grappleOnly = (me.move.mods & MOVE_GRAPPLE_ONLY) != 0;
        if ((sp.grapple || grappleOnly) && !(me.move.mods & MOVE_RUN_ONLY)) {
            if (now < b.hookUntil) {
                Vec3 d = normalize(b.hookAnchor - eye);
                wantYaw = yawOf(d);
                wantPitch = std::asin(clampf(d.y, -1, 1));
                gain = 25.0f;
                // (the hook needs the aim on the anchor before it's fired)
                if (me.move.flags & MF_HOOKED) holdHook();
                else if (std::fabs(angleDiff(b.aimYaw, wantYaw)) + std::fabs(b.aimPitch - wantPitch) < 0.08f) cmd.buttons |= BTN_GRAPPLE;
            } else if (me.move.hookCooldown <= 0 && now >= b.nextHookTry) {
                b.nextHookTry = now + (grappleOnly ? 0.2 : sp.airStrafe > 0.8f ? 0.25 : 0.5); // (expert: hooks around all the time)
                // the next waypoint if it's a ledge up, else the end of the route if it's a long open stretch
                bool expertish = sp.airStrafe > 0.8f;
                // (and, on maps with pits, a few surfaces further along the route: the line to the route's end often
                // crosses a pit where one to a point along the route doesn't)
                Vec3 cands[5] = {b.waypoint, b.pathGoal};
                int nc = 2;
                if (s.map_.hasVoid || !s.map_.lava.empty())
                    for (int k : {5, 3, 1})
                        if ((size_t)k < b.path.size() && b.path[k] < (int)s.nav_.nodes.size()) {
                            const auto& n = s.nav_.nodes[b.path[k]];
                            cands[nc++] = {(n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f};
                        }
                for (int ci = 0; ci < nc; ci++) {
                    const Vec3& cand = cands[ci];
                    Vec3 off = cand - me.move.pos;
                    float rise = off.y, hd = std::sqrt(off.x * off.x + off.z * off.z);
                    bool want = grappleOnly ? hd > 2.0f : (rise > (expertish ? 1.2f : 2.0f) && hd < 30.0f) || (hd > (expertish ? 8.0f : 14.0f) && rise > -3.0f);
                    if (!want) continue;
                    Vec3 tgt = cand - Vec3(0, 0.1f, 0); // just into the walkable surface, so the hook catches on it
                    Vec3 d = normalize(tgt - eye);
                    float t = s.map_.raycast(eye, d, kGrappleRange * moveTuning().m[MV_HOOK_RANGE]);
                    if (t >= 0 && length(eye + d * t - tgt) < 3.0f && safeHook(s, me, eye + d * t)) {
                        b.hookAnchor = eye + d * t;
                        b.hookUntil = now + 1.6;
                        b.hookChecked = false;
                        break;
                    }
                }
            }
        }
    }

    // Turn like a hand on a mouse: speed capped, changing at a limited rate, so long flicks take time and
    // settle; while tracking, the hand also moves with the target. Stepped in 1 ms of game time whatever the
    // tick length, so the hand is the same at every server tick rate.
    auto turn = [&](float& ang, float& vel, float want, float ff, bool wrap) {
        int steps = std::max(1, (int)std::lround(dt * 1000.0f));
        const float h = dt / steps;
        for (int k = 0; k < steps; k++) {
            float diff = wrap ? angleDiff(want, ang) : want - ang;
            float brake = std::sqrt(2.0f * sp.turnAccel * std::fabs(diff)) * 0.85f;
            float corr = clampf(diff * gain, -brake, brake);
            float target = clampf(ff + corr, -sp.turnRate, sp.turnRate);
            float dv = sp.turnAccel * h;
            vel += clampf(target - vel, -dv, dv);
            ang += vel * h;
            if (wrap) ang = std::remainder(ang, 2 * PI);
        }
    };
    // The command carries the view as it is at the start of this tick (the hand then moves on through the tick),
    // the same instant a shot fired now is tested at: so the tick length doesn't change how well it hits.
    cmd.yaw = b.aimYaw;
    cmd.pitch = clampf(b.aimPitch, -1.5f, 1.5f);
    turn(b.aimYaw, b.yawVel, wantYaw, ffYaw, true);
    turn(b.aimPitch, b.pitchVel, clampf(wantPitch, -1.5f, 1.5f), 0.0f, false);

    // movement relative to the view direction
    float ml = length(moveDir);
    Vec3 fwd = {-std::sin(cmd.yaw), 0, -std::cos(cmd.yaw)}, right = {std::cos(cmd.yaw), 0, -std::sin(cmd.yaw)};
    if (ml > 1e-3f) {
        moveDir = moveDir / ml;
        Vec3 wish = moveDir;
        bool onGround = (me.move.flags & MF_ONGROUND) != 0;
        bool hopping = !fighting && sp.bhop && s.ms_.mode != MODE_RACE && length(b.waypoint - me.move.pos) > 3.0f &&
                       !(me.move.flags & MF_HOOKED);   // (hooked: no air-strafing off the pull)
        // air-strafe: push at the angle to the current velocity that gains speed, curving toward the route
        Vec3 hv = {me.move.vel.x, 0, me.move.vel.z};
        float hs = length(hv);
        if (hopping && !onGround && hs > 4.0f && sp.airStrafe > 0) {
            Vec3 vn = hv / hs;
            float phi = std::atan2(vn.x * moveDir.z - vn.z * moveDir.x, dot(vn, moveDir)); // signed angle velocity -> route
            if (std::fabs(phi) < 1.2f) {
                float th = std::acos(clampf(2.3f / hs, 0.0f, 1.0f)) * sp.airStrafe;
                if (std::fabs(phi) > 0.05f) b.strafeDir = phi > 0 ? 1.0f : -1.0f;
                float ang = th * b.strafeDir, c = std::cos(ang), sn = std::sin(ang);
                wish = {vn.x * c - vn.z * sn, 0, vn.x * sn + vn.z * c};
            }
        }
        cmd.forward = (int8_t)clampf(dot(wish, fwd) * 127, -127, 127);
        cmd.side = (int8_t)clampf(dot(wish, right) * 127, -127, 127);
        // jump over obstacles (something knee-high ahead) and across gaps (no floor ahead)
        Vec3 knee = me.move.pos + Vec3(0, 0.6f, 0);
        // (movement probes: clip blocks players, so it counts here)
        if (s.map_.raycastAny(knee, moveDir, 1.2f, true)) cmd.buttons |= BTN_JUMP;
        Vec3 ahead = me.move.pos + moveDir * 0.7f + Vec3(0, 0.2f, 0);
        const bool pits = s.map_.hasVoid || !s.map_.lava.empty();
        // (lava can lie on a floor: a step into it is a drop too)
        if (onGround && (!s.map_.raycastAny(ahead, {0, -1, 0}, 1.5f, true) || (!s.map_.lava.empty() && !safeDrop(s, ahead)))) {
            // a drop ahead: on maps with pits or lava, only go if there's safe ground below, or a safe
            // landing within jumping distance; otherwise stop at the edge (the stuck check re-plans)
            Vec3 far = me.move.pos + moveDir * 2.6f + Vec3(0, 0.6f, 0);
            bool safe = !pits || safeDrop(s, ahead);
            bool landing = s.map_.raycastAny(far, {0, -1, 0}, 2.0f, true) && (!pits || safeDrop(s, far));
            if (safe || landing) cmd.buttons |= BTN_JUMP;
            else { cmd.forward = 0; cmd.side = 0; hopping = false; }   // (and no bunny hop off it)
        }
        if (hopping) cmd.buttons |= BTN_JUMP;
        // long straight stretch: dash along it (levels that would), when it lands on something
        if (!fighting && onGround && sp.runDash > 0 && me.move.dashCooldown <= 0 && !(me.move.mods & MOVE_RUN_ONLY) &&
            length(b.waypoint - me.move.pos) > 9.0f && dot(moveDir, fwd) > 0.95f && chance(b, sp.runDash, dt)) {
            if (groundAlong(s, me.move.pos, moveDir, 12.0f)) {
                cmd.buttons |= BTN_DASH;
                // experts with the room make it a dash jump: jump with it and keep the dash's 24 m/s through a
                // jump's ~16 m flight (ground all the way, and the waypoint further than that)
                dashJump = b.skill == BOT_EXPERT && hopping && length(b.waypoint - me.move.pos) > 20.0f &&
                           groundAlong(s, me.move.pos, moveDir, 20.0f);
            }
        }
    }
    // Maps with pits or lava: look ahead along where we're actually going (momentum included: on the ground, mid-hop,
    // or just let go of a grapple, steering or not). If there's no safe ground there and nothing safe to land on,
    // brake hard and stay on the ground (friction stops you, a hop at speed carries you on). Fast (a dash, a
    // grapple's pull) looks further ahead: about what it takes to stop.
    if (s.map_.hasVoid || !s.map_.lava.empty()) {
        Vec3 hv = {me.move.vel.x, 0, me.move.vel.z};
        float hs = length(hv);
        if (ml > 1e-3f || hs > 1.0f) {
            bool onGround = (me.move.flags & MF_ONGROUND) != 0;
            Vec3 ahead = me.move.pos + hv * (hs > 10.0f ? 0.6f : 0.35f);
            bool ground = safeDrop(s, ahead + moveDir * 0.6f + Vec3(0, 0.3f, 0));
            if (hs > 10.0f) // (fast: the whole way there, a probe every ~1.5 m, so a pit on the way isn't skipped over)
                for (int k = 1, n = std::min(6, (int)(hs * 0.6f / 1.5f)); k < n && ground; k++)
                    ground = safeDrop(s, me.move.pos + hv * (0.6f * k / n) + Vec3(0, 0.3f, 0));
            Vec3 far = me.move.pos + moveDir * 2.6f + Vec3(0, 0.6f, 0);   // (a gap a jump from here clears)
            // (a gap jump: at a run, heading where we're going; flung along by a grapple or a dash isn't one)
            bool landing = onGround && ml > 1e-3f && hs < 10.0f && dot(hv, moveDir) >= 0.5f * hs &&
                           s.map_.raycastAny(far, {0, -1, 0}, 2.0f, true) && safeDrop(s, far);
            if (!onGround && !ground) { // mid-jump over a gap: fine if the arc comes down on safe ground (braking now drops us in)
                float vy = me.move.vel.y, g = 22.0f;   // (kGravity)
                float tl = (vy + std::sqrt(std::max(0.0f, vy * vy + 2 * g * 0.5f))) / g;   // back down to about this height
                Vec3 at = me.move.pos + hv * tl + Vec3(0, 0.6f, 0);
                landing = s.map_.raycastAny(at, {0, -1, 0}, 2.0f, true) && safeDrop(s, at) &&
                          (hs < 6.0f || safeDrop(s, at + hv * (std::min(hs * 0.2f, 5.0f) / hs)));   // (and the skid after it)
                cmd.buttons &= ~BTN_JUMP;   // (either way: no hop on landing, toward a drop)
                if (landing) { cmd.forward = 0; cmd.side = 0; }   // (and no air-strafing on toward it)
            }
            if (!ground && !landing) {
                Vec3 back = hs > 0.5f ? hv / hs * -1.0f : moveDir * -1.0f;
                cmd.forward = (int8_t)clampf(dot(back, fwd) * 127, -127, 127);
                cmd.side = (int8_t)clampf(dot(back, right) * 127, -127, 127);
                cmd.buttons &= ~(BTN_JUMP | BTN_DASH);
                b.goalUntil = 0; // re-plan
            }
        }
    }

    // unstick: if we've barely moved for a while, hop and sidestep; when just roaming, pick somewhere else
    if (now >= b.stuckCheckAt) {
        if (ml > 1e-3f && length(me.move.pos - b.lastPos) < 0.5f && b.target < 0) {
            b.strafeDir = rnd(b) < 0.5f ? -1.0f : 1.0f;
            if (!b.nearEdge) cmd.buttons |= BTN_JUMP;
            else if (dot(Vec3(-moveDir.z, 0, moveDir.x) * b.strafeDir, b.edgeAway) < 0) b.strafeDir = -b.strafeDir; // by a drop: no hop, sidestep away from it
            b.unstickUntil = now + 0.8;
            b.goalUntil = 0;
            b.needPath = true;
        }
        b.lastPos = me.move.pos;
        b.stuckCheckAt = now + 1.5;
    }
    // jump with our own dash on the floor (a hop's landing, a fight jump) turns it into a dash jump: a 16 m flight
    // at dash speed, past what the dash checked for ground. Only when meant.
    if (!dashJump && ((cmd.buttons & BTN_DASH) || me.move.dashTimer > 0)) cmd.buttons &= ~BTN_JUMP;
    return cmd;
}
