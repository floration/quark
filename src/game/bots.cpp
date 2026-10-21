#include <queue>
#include "bots.h"
#include "../common/profile.h"
#include "gamemodes.h"
#include "server.h"

namespace {

struct SkillParams {
    float reaction;   // s before firing at a newly seen target
    float aimError;   // rad: typical drifting aim error
    float jitter;     // rad: per-tick hand shake
    float trackLag;   // s: aims where a moving target was this long ago (hitscan) / under-leads (projectiles)
    float lapseRate;  // chance per second of an aim lapse (0.4-0.9 s of 3x error)
    float turnRate;   // rad/s: max turn speed
    float turnAccel;  // rad/s^2: how fast turning speeds up / slows down (limits flicks, causes overshoot)
    float fireCone;   // rad: fire when aim is this close to the aim point
    bool strafe;      // dodge sideways while fighting
    bool bhop;        // hold jump while running
    bool dash;        // use dash to close distance / dodge
    bool lead;        // lead moving targets with projectiles
    bool weapons;     // pick weapons by range (else rifle only)
};
const SkillParams kSkills[BOT_SKILLS] = {
    // react  error  jitter trkLag lapse  turn   accel  cone    strafe bhop   dash   lead   weapons
    // (each level is what the one above it used to be; beginner is a notch below the old beginner)
    {0.60f, 0.085f, 0.016f, 0.19f, 0.40f, 3.4f,  18.0f, 0.12f,  true,  false, false, false, false}, // beginner
    {0.48f, 0.064f, 0.012f, 0.14f, 0.32f, 4.2f,  24.0f, 0.10f,  true,  false, false, false, false}, // intermediate
    {0.34f, 0.032f, 0.007f, 0.08f, 0.22f, 6.0f,  40.0f, 0.07f,  true,  false, false, true,  true},  // advanced
    {0.20f, 0.012f, 0.003f, 0.02f, 0.08f, 10.0f, 90.0f, 0.04f,  true,  true,  true,  true,  true},  // expert (1.10: sharper aim)
};

float rnd(BotBrain& b) { // xorshift, 0..1
    b.rng ^= b.rng << 13; b.rng ^= b.rng >> 17; b.rng ^= b.rng << 5;
    return (b.rng & 0xFFFFFF) / 16777216.0f;
}

float angleDiff(float a, float b) { return std::remainder(a - b, 2 * PI); }

Vec3 eyeOf(const ServerPlayer& p, float scale = 1.0f) {
    return p.move.pos + Vec3(0, ((p.move.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * scale, 0);
}

bool isEnemy(const Server& s, int a, int b) {
    if (a == b) return false;
    const auto& pb = s.players_[b];
    if (!pb.connected || !pb.alive || pb.team == TEAM_SPECTATOR) return false;
    if (isItMode(s.ms_.mode)) return s.players_[a].it && !pb.it; // only "it" hunts; everyone else runs
    if (!modeIsTeamBased(s.ms_.mode)) return true;
    return s.players_[a].team != pb.team;
}

bool canSee(const Server& s, const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float len = length(d);
    if (len < 0.01f) return true;
    return !s.map_.raycastAny(from, d / len, len - 0.1f);
}

// Where the bot wants to go when it isn't fighting: the mode's objective, else roam.
Vec3 objective(Server& s, int id, BotBrain& b) {
    const ModeState& m = s.ms_;
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
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
                    if (c.connected && c.alive) return c.move.pos; // escort
                }
                return site.center();
            }
            break;
        }
        case MODE_HIDE:
        case MODE_TAG: {
            if (s.nav_.nodes.empty()) break;
            auto randomNode = [&]() { const auto& n = s.nav_.nodes[(size_t)(rnd(b) * s.nav_.nodes.size()) % s.nav_.nodes.size()];
                                      return Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f); };
            if (me.it) { // seek: wander everywhere (targets are chased by the fight code once seen)
                if (s.time() > b.goalUntil || length(b.goal - me.move.pos) < 2.0f) { b.goal = randomNode(); b.goalUntil = s.time() + 12.0f; }
                return b.goal;
            }
            int it = -1;
            for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].alive && s.players_[i].it) it = i;
            Vec3 eye = me.move.pos + Vec3(0, kEyeHeight, 0);
            bool threat = it >= 0 && length(s.players_[it].move.pos - me.move.pos) < (m.mode == MODE_TAG ? 30.0f : 18.0f) &&
                          canSee(s, eye, s.players_[it].move.pos + Vec3(0, 1.1f, 0));
            if (threat && s.time() > b.fleeUntil) { // run: the reachable-looking spot farthest from it
                Vec3 itPos = s.players_[it].move.pos, best = b.goal;
                float bestScore = -1e9f;
                for (int k = 0; k < 12; k++) {
                    Vec3 c = randomNode();
                    float sc = length(c - itPos) - 0.5f * length(c - me.move.pos);
                    if (sc > bestScore) { bestScore = sc; best = c; }
                }
                b.goal = best; b.goalUntil = s.time() + 6.0f; b.fleeUntil = s.time() + 1.5f;
                return b.goal;
            }
            if (m.mode == MODE_HIDE && !threat) { // pick a hiding spot once and stay put
                if (s.time() > b.goalUntil) { b.goal = randomNode(); b.goalUntil = s.time() + 90.0f; }
                if (length(b.goal - me.move.pos) < 1.2f) return me.move.pos;
                return b.goal;
            }
            if (s.time() > b.goalUntil || length(b.goal - me.move.pos) < 2.0f) { b.goal = randomNode(); b.goalUntil = s.time() + 10.0f; }
            return b.goal;
        }
        default: break;
    }
    // roam between spawn points
    if (s.time() > b.goalUntil || length(b.goal - me.move.pos) < 2.0f) {
        const auto& sp = map.spawns[(size_t)(rnd(b) * map.spawns.size()) % map.spawns.size()];
        b.goal = sp.pos;
        b.goalUntil = s.time() + 8.0f + rnd(b) * 6.0f;
    }
    return b.goal;
}

// Best weapon for the range that this mode allows (1v1 rulesets / duel lives restrict the pool).
// Beginners stick to the rifle when they can.
int pickWeapon(const Server& s, const SkillParams& sp, float dist) {
    static const int close[] = {WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_BUBBLE, WPN_ROCKET, WPN_RIFLE, WPN_STICKY, WPN_CROSSBOW, WPN_SNIPER, WPN_SYRINGE};
    static const int mid[]   = {WPN_ROCKET, WPN_LASER, WPN_RIFLE, WPN_STICKY, WPN_BUBBLE, WPN_CROSSBOW, WPN_SHOTGUN, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE};
    static const int midNoLead[] = {WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_ROCKET, WPN_STICKY, WPN_CROSSBOW, WPN_BUBBLE, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE};
    static const int far[]   = {WPN_CROSSBOW, WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE};
    static const int farNoLead[] = {WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_CROSSBOW, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE};
    static const int vfar[]  = {WPN_SNIPER, WPN_CROSSBOW, WPN_RIFLE, WPN_ROCKET, WPN_STICKY, WPN_LASER, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE};
    if (!sp.weapons && s.weaponAllowed(WPN_RIFLE)) return WPN_RIFLE;
    const int* order = dist < 7 ? close : dist < 18 ? (sp.lead ? mid : midNoLead) : dist < 35 ? (sp.lead ? far : farNoLead) : (sp.lead ? vfar : farNoLead);
    for (int k = 0; k < WPN_COUNT; k++) if (s.weaponAllowed(order[k])) return order[k];
    return WPN_RIFLE;
}

} // namespace

void NavGraph::build(const Map& m) {
    nodes.clear();
    links.clear();
    mapCrc = m.crc;
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
    links.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++)
        for (size_t j = 0; j < nodes.size(); j++) {
            if (i == j) continue;
            const Node &a = nodes[i], &b = nodes[j];
            float gx = std::max(0.0f, std::max(a.x0, b.x0) - std::min(a.x1, b.x1));
            float gz = std::max(0.0f, std::max(a.z0, b.z0) - std::min(a.z1, b.z1));
            float gap = std::sqrt(gx * gx + gz * gz);
            float up = b.y - a.y;
            if (gap > 2.6f || up > 1.15f || up < -8.0f) continue;
            float cx = ((a.x0 + a.x1) - (b.x0 + b.x1)) * 0.5f, cz = ((a.z0 + a.z1) - (b.z0 + b.z1)) * 0.5f;
            links[i].push_back({(int)j, std::sqrt(cx * cx + cz * cz) + gap * 2 + std::max(up, 0.0f) * 2});
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
}

int NavGraph::nodeAt(const Vec3& p) const {
    int best = -1;
    float bestY = -1e9f;
    for (size_t i = 0; i < nodes.size(); i++) {
        const Node& n = nodes[i];
        if (p.x < n.x0 - 0.3f || p.x > n.x1 + 0.3f || p.z < n.z0 - 0.3f || p.z > n.z1 + 0.3f) continue;
        if (n.y > p.y + 0.6f || n.y < p.y - 6.0f) continue; // the surface below us, not one overhead
        if (n.y > bestY) { bestY = n.y; best = (int)i; }
    }
    return best;
}

Vec3 NavGraph::nextWaypoint(const Vec3& from, const Vec3& to) const {
    int a = nodeAt(from), b = nodeAt(to + Vec3(0, 0.5f, 0));
    if (a < 0 || b < 0 || a == b) return to;
    // Dijkstra with a binary heap (big maps have thousands of surfaces; stops once the goal is settled)
    std::vector<float> dist(nodes.size(), 1e30f);
    std::vector<int> prev(nodes.size(), -1);
    std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> open;
    dist[a] = 0;
    open.push({0.0f, a});
    while (!open.empty()) {
        auto [d, u] = open.top();
        open.pop();
        if (d > dist[u]) continue;
        if (u == b) break;
        for (auto& [v, c] : links[u]) if (d + c < dist[v]) { dist[v] = d + c; prev[v] = u; open.push({dist[v], v}); }
    }
    if (prev[b] < 0) return to;
    std::vector<int> path; // a (exclusive) .. b
    for (int v = b; v != a; v = prev[v]) path.push_back(v);
    // Walk through the "portal" between consecutive surfaces: the stretch of their shared edge (or the
    // next surface itself when they overlap, e.g. stepping down off a block). Clamping to the shared edge
    // keeps bots from cutting corners across a hole between two surfaces. If that point is right where we
    // stand, look further along the path.
    int prevNode = a;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const Node& n = nodes[*it];
        const Node& pn = nodes[prevNode];
        bool ladder = false;
        for (auto& ll : ladderLinks)
            if (ll.first.first == prevNode && ll.first.second == *it) { // climb: walk into the ladder
                float dx = ll.second.x - from.x, dz = ll.second.z - from.z;
                if (dx * dx + dz * dz > 0.36f) return ll.second;
                ladder = true;
            }
        prevNode = *it;
        if (ladder) { // on it: keep pushing toward the surface at the top (that climbs)
            return {(n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f};
        }
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

const char* botSkillName(int skill) {
    static const char* n[BOT_SKILLS] = {"BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"};
    return n[skill < 0 || skill >= BOT_SKILLS ? 1 : skill];
}

UserCmd botThink(Server& s, int id) {
    ServerPlayer& me = s.players_[id];
    BotBrain& b = s.bots_[id];
    const SkillParams& sp = kSkills[b.skill < 0 || b.skill >= BOT_SKILLS ? 1 : b.skill];
    const float now = s.time(), dt = s.dt();
    UserCmd cmd;
    cmd.weapon = kNoWeaponChange;
    if (!me.alive) {
        b.target = -1;
        cmd.yaw = b.aimYaw;
        return cmd;
    }
    Vec3 eye = eyeOf(me, s.bodyScale());

    // --- target selection (10 Hz): nearest visible enemy
    bool damageOn = (s.ms_.phase == PHASE_LIVE || s.ms_.phase == PHASE_WARMUP) && !(s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP);
    if (now >= b.nextTargetScan) {
        b.nextTargetScan = now + 0.1f;
        int best = -1;
        float bestD = 80.0f;
        for (int i = 0; i < kMaxPlayers && damageOn; i++) {
            if (!isEnemy(s, id, i)) continue;
            Vec3 chest = s.players_[i].move.pos + Vec3(0, 1.1f, 0);
            float d = length(chest - eye);
            if (d < bestD && canSee(s, eye, chest)) { bestD = d; best = i; } // (distance first: no raycast for farther ones)
        }
        if (best != b.target) {
            b.target = best;
            b.seenAt = now;
            b.errorRetargetAt = 0;
        }
    }
    if (b.target >= 0 && (!isEnemy(s, id, b.target))) b.target = -1;

    // Objective movement (nav path to the mode's goal), also used while fighting when the
    // objective can't wait: carrying the bomb, or defusing a planted one.
    auto objectiveMove = [&](Vec3& dirOut, uint16_t& buttons) {
        Vec3 goal = objective(s, id, b);
        if (s.nav_.mapCrc != s.map_.crc) s.nav_.build(s.map_);
        if (now >= b.repathAt || length(goal - b.pathGoal) > 1.0f || length(b.waypoint - me.move.pos) < 0.8f) {
            QZONE("bot nav path");
            b.waypoint = s.nav_.nextWaypoint(me.move.pos, goal);
            b.pathGoal = goal;
            b.repathAt = now + 0.25f;
        }
        Vec3 to = b.waypoint - me.move.pos;
        to.y = 0;
        float d = length(to);
        dirOut = d > 0.6f ? to / d : Vec3();
        if (d > 0.6f && now < b.strafeSwitchAt && b.target < 0) dirOut += Vec3(-dirOut.z, 0, dirOut.x) * b.strafeDir; // unsticking
        const ModeState& m = s.ms_;
        if (m.mode == MODE_BOMB) {
            bool inSite = false;
            for (auto& site : s.map_.bombSites) inSite |= playerBox(me.move.pos, false).overlaps(site);
            if (m.bombState == BOMB_CARRIED && m.bombCarrier == id && inSite) { buttons |= BTN_USE; dirOut = {}; }
            if (m.bombState == BOMB_PLANTED && me.team != m.attackers && length(m.bombPos - me.move.pos) < 1.5f) { buttons |= BTN_USE; dirOut = {}; }
        }
        if (m.mode == MODE_KOTH && s.map_.hasHill && playerBox(me.move.pos, false).overlaps(s.map_.hill)) dirOut = dirOut * 0.3f;
        if (m.mode == MODE_POINTS && m.point < s.map_.points.size() && playerBox(me.move.pos, false).overlaps(s.map_.points[m.point])) dirOut = dirOut * 0.3f;
    };
    const ModeState& ms = s.ms_;
    bool urgent = ms.mode == MODE_BOMB && ((ms.bombState == BOMB_CARRIED && ms.bombCarrier == id) ||
                                           (ms.bombState == BOMB_PLANTED && me.team != ms.attackers));

    // --- aim
    float wantYaw = b.aimYaw, wantPitch = 0;
    float dist = 0;
    Vec3 moveDir; // world-space desired movement (xz)
    if (b.target >= 0) {
        const ServerPlayer& t = s.players_[b.target];
        Vec3 aimAt = t.move.pos + Vec3(0, 1.1f, 0);
        dist = length(aimAt - eye);
        int w = b.forceWeapon >= 0 ? b.forceWeapon : pickWeapon(s, sp, dist);
        if (w == WPN_KNIFE && now < me.knifeBackAt && me.weapon != WPN_KNIFE) w = me.weapon; // thrown it: use what we switched to
        const WeaponDef& wd = weaponDef(w);
        bool throwKnife = w == WPN_KNIFE && dist > wd.range + 0.5f && dist < 18.0f && sp.lead && !isItMode(s.ms_.mode); // out of reach: throw it
        aimAt -= t.move.vel * sp.trackLag; // reacts to movement late: trails strafing targets
        if (throwKnife) {
            float tf = dist / kKnifeThrowSpeed;
            aimAt += t.move.vel * tf;
            aimAt.y += 0.5f * kKnifeThrowGravity * tf * tf;
        }
        if (wd.projectile) {
            if (sp.lead) aimAt += t.move.vel * (dist / wd.projSpeed);
            if (w == WPN_ROCKET) aimAt.y = t.move.pos.y + 0.1f; // splash at the feet
            if (wd.gravity > 0) aimAt.y += 0.5f * wd.gravity * (dist / wd.projSpeed) * (dist / wd.projSpeed);
        }
        // Human-like aim problems: an angular error that drifts to a new random offset every
        // 0.3-0.7 s, occasional lapses with much worse aim, and per-tick shake.
        if (now >= b.nextLapseRoll) {
            b.nextLapseRoll = now + 0.25f;
            if (rnd(b) < sp.lapseRate * 0.25f) b.lapseUntil = now + 0.4f + rnd(b) * 0.5f;
        }
        float errMag = sp.aimError * (now < b.lapseUntil ? 3.0f : 1.0f);
        if (now >= b.errorRetargetAt) {
            b.errToYaw = (rnd(b) * 2 - 1) * errMag;
            b.errToPitch = (rnd(b) * 2 - 1) * errMag * 0.6f;
            b.errorRetargetAt = now + 0.3f + rnd(b) * 0.4f;
        }
        float drift = std::min(1.0f, dt * 4.0f);
        b.errYaw += (b.errToYaw - b.errYaw) * drift;
        b.errPitch += (b.errToPitch - b.errPitch) * drift;
        Vec3 d = normalize(aimAt - eye);
        wantYaw = std::atan2(-d.x, -d.z) + b.errYaw + (rnd(b) * 2 - 1) * sp.jitter;
        wantPitch = std::asin(clampf(d.y, -1, 1)) + b.errPitch + (rnd(b) * 2 - 1) * sp.jitter;
        if (me.weapon != w) cmd.weapon = (uint8_t)w;
        // fire once the reaction time has passed and the aim is close enough
        float err = std::fabs(angleDiff(b.aimYaw, wantYaw)) + std::fabs(b.aimPitch - wantPitch);
        bool ready = now - b.seenAt >= sp.reaction && err < sp.fireCone + 0.3f / std::max(dist, 1.0f);
        bool knifeOut = w == WPN_KNIFE && !throwKnife && dist > wd.range + 0.3f; // closing in to stab: don't swing at air
        if (ready && !knifeOut) cmd.buttons |= throwKnife ? BTN_ALTFIRE : BTN_FIRE;
        if (w == WPN_SNIPER) cmd.buttons |= BTN_ALTFIRE;
        if (w == WPN_RIFLE && dist > 30.0f && sp.lead) cmd.buttons |= BTN_ALTFIRE; // scope in for long shots
        if (me.weapon == WPN_STICKY) { // set off our stickies when the target is in the blast
            for (const auto& pr : s.projectiles_)
                if (pr.active && pr.owner == id && pr.type == WPN_STICKY && length(pr.pos - (t.move.pos + Vec3(0, 0.9f, 0))) < weaponDef(WPN_STICKY).splashRadius * 0.7f) {
                    cmd.buttons |= BTN_ALTFIRE;
                    break;
                }
        }
        // semi-auto: let go between shots (a click is buffered until the weapon is ready)
        if (weaponDef(me.weapon).semiAuto && (me.buttons & BTN_FIRE)) cmd.buttons &= ~BTN_FIRE;
        // fight movement: keep a good range for the weapon, strafe to dodge
        Vec3 to = t.move.pos - me.move.pos;
        to.y = 0;
        Vec3 toN = normalize(to);
        float ideal = w == WPN_SHOTGUN ? 4.0f : w == WPN_SNIPER ? 40.0f : w == WPN_KNIFE ? 1.5f : w == WPN_LASER ? 12.0f
                    : w == WPN_STICKY ? 10.0f : w == WPN_BUBBLE ? 7.0f : w == WPN_CROSSBOW ? 22.0f : w == WPN_RIFLE ? 18.0f : 14.0f;
        moveDir = toN * (dist > ideal + 3 ? 1.0f : dist < ideal - 3 ? -1.0f : 0.0f);
        if (sp.strafe) {
            if (now >= b.strafeSwitchAt) { b.strafeDir = rnd(b) < 0.5f ? -1.0f : 1.0f; b.strafeSwitchAt = now + 0.25f + rnd(b) * 0.6f; }
            moveDir += Vec3(-toN.z, 0, toN.x) * b.strafeDir;
        }
        if (sp.dash && me.move.dashCooldown <= 0 && dist > 20 && rnd(b) < 0.01f) cmd.buttons |= BTN_DASH;
        if (urgent) { // keep going for the objective, shooting on the way
            uint16_t extra = 0;
            objectiveMove(moveDir, extra);
            if (extra & BTN_USE) cmd.buttons = (uint16_t)((cmd.buttons & ~BTN_FIRE) | BTN_USE);
        }
    } else {
        uint16_t extra = 0;
        objectiveMove(moveDir, extra);
        cmd.buttons |= extra;
        if (length(moveDir) > 1e-3f) {
            Vec3 md = normalize(moveDir);
            wantYaw = std::atan2(-md.x, -md.z);
        }
        wantPitch = 0;
        // Grapple (advanced and up, or when it's the only way to move): up to a ledge on the route, or across a
        // long open stretch. Only when the hook would catch near that point; hold until close or out of time.
        bool grappleOnly = (me.move.mods & MOVE_GRAPPLE_ONLY) != 0;
        if ((sp.dash || grappleOnly) && !(me.move.mods & MOVE_RUN_ONLY)) {
            if (now < b.hookUntil) {
                Vec3 d = normalize(b.hookAnchor - eye);
                wantYaw = std::atan2(-d.x, -d.z);
                wantPitch = std::asin(clampf(d.y, -1, 1));
                cmd.buttons |= BTN_GRAPPLE;
                if ((me.move.flags & MF_HOOKED) && length(b.hookAnchor - me.move.pos) < 3.0f) b.hookUntil = 0; // arrived: let go
            } else if (me.move.hookCooldown <= 0 && now >= b.nextHookTry) {
                b.nextHookTry = now + (grappleOnly ? 0.2f : sp.bhop ? 0.25f : 0.5f); // (expert: hooks around all the time)
                // the next waypoint if it's a ledge up, else the end of the route if it's a long open stretch
                for (const Vec3& cand : {b.waypoint, b.pathGoal}) {
                    Vec3 off = cand - me.move.pos;
                    float rise = off.y, hd = std::sqrt(off.x * off.x + off.z * off.z);
                    bool want = grappleOnly ? hd > 2.0f : (rise > (sp.bhop ? 1.2f : 2.0f) && hd < 30.0f) || (hd > (sp.bhop ? 8.0f : 14.0f) && rise > -3.0f);
                    if (!want) continue;
                    Vec3 tgt = cand - Vec3(0, 0.1f, 0); // just into the walkable surface, so the hook catches on it
                    Vec3 d = normalize(tgt - eye);
                    float t = s.map_.raycast(eye, d, kGrappleRange * moveTuning().m[MV_HOOK_RANGE]);
                    if (t >= 0 && length(eye + d * t - tgt) < 3.0f) {
                        b.hookAnchor = eye + d * t;
                        b.hookUntil = now + 1.6f;
                        break;
                    }
                }
            }
        }
    }

    // Turn like a hand on a mouse: turn speed is capped and can only change at a limited rate,
    // so long flicks take time and overshoot a little before settling.
    auto turn = [&](float diff, float& vel) {
        // fastest speed that can still brake in time (turnAccel), capped by turnRate; gentle near the target
        float mag = std::min({sp.turnRate, std::sqrt(2.0f * sp.turnAccel * std::fabs(diff)) * 0.85f, std::fabs(diff) * 20.0f});
        float want = diff < 0 ? -mag : mag;
        float dv = sp.turnAccel * dt;
        vel += clampf(want - vel, -dv, dv);
        return vel * dt;
    };
    b.aimYaw += turn(angleDiff(wantYaw, b.aimYaw), b.yawVel);
    b.aimPitch += turn(wantPitch - b.aimPitch, b.pitchVel);
    b.aimYaw = std::remainder(b.aimYaw, 2 * PI);
    cmd.yaw = b.aimYaw;
    cmd.pitch = clampf(b.aimPitch, -1.5f, 1.5f);

    // movement relative to the view direction
    float ml = length(moveDir);
    if (ml > 1e-3f) {
        moveDir = moveDir / ml;
        Vec3 fwd = {-std::sin(cmd.yaw), 0, -std::cos(cmd.yaw)}, right = {std::cos(cmd.yaw), 0, -std::sin(cmd.yaw)};
        cmd.forward = (int8_t)clampf(dot(moveDir, fwd) * 127, -127, 127);
        cmd.side = (int8_t)clampf(dot(moveDir, right) * 127, -127, 127);
        // jump over obstacles (something knee-high ahead) and across gaps (no floor ahead)
        Vec3 knee = me.move.pos + Vec3(0, 0.6f, 0);
        if (s.map_.raycastAny(knee, moveDir, 1.2f)) cmd.buttons |= BTN_JUMP;
        Vec3 ahead = me.move.pos + moveDir * 0.7f + Vec3(0, 0.2f, 0);
        if ((me.move.flags & MF_ONGROUND) && !s.map_.raycastAny(ahead, {0, -1, 0}, 1.5f)) {
            // a drop ahead: on maps with pits, only go if there's ground below before the void, or a
            // landing within jumping distance; otherwise stop at the edge (the stuck check re-plans)
            float depth = ahead.y - s.map_.killY();
            bool safe = !s.map_.hasVoid || s.map_.raycastAny(ahead, {0, -1, 0}, std::max(depth, 0.1f));
            bool landing = s.map_.raycastAny(me.move.pos + moveDir * 2.6f + Vec3(0, 0.6f, 0), {0, -1, 0}, 2.0f);
            if (safe || landing) cmd.buttons |= BTN_JUMP;
            else { cmd.forward = 0; cmd.side = 0; }
        }
        if (sp.bhop && s.ms_.mode != MODE_RACE) cmd.buttons |= BTN_JUMP;
        // maps with pits: look ahead along where we're actually going (momentum included, on the ground or
        // mid-hop); if there's no ground before the void there and nothing to land on, brake hard
        if (s.map_.hasVoid) {
            Vec3 hv = {me.move.vel.x, 0, me.move.vel.z};
            Vec3 probe = me.move.pos + hv * 0.35f + moveDir * 0.6f + Vec3(0, 0.3f, 0);
            float depth = std::max(probe.y - s.map_.killY(), 0.1f);
            bool ground = s.map_.raycastAny(probe, {0, -1, 0}, depth);
            Vec3 far = me.move.pos + hv * 0.35f + moveDir * 2.6f + Vec3(0, 0.6f, 0);
            bool landing = (me.move.flags & MF_ONGROUND) && s.map_.raycastAny(far, {0, -1, 0}, 2.0f);
            if (!ground && !landing) {
                Vec3 back = length(hv) > 0.5f ? normalize(hv) * -1.0f : moveDir * -1.0f;
                cmd.forward = (int8_t)clampf(dot(back, fwd) * 127, -127, 127);
                cmd.side = (int8_t)clampf(dot(back, right) * 127, -127, 127);
                cmd.buttons &= ~BTN_JUMP;
                b.goalUntil = 0; // re-plan
            }
        }
    }

    // unstick: if we've barely moved for a while, pick a random place to go
    if (now >= b.stuckCheckAt) {
        if (ml > 1e-3f && length(me.move.pos - b.lastPos) < 0.5f && b.target < 0) {
            // stuck: hop and sidestep; when just roaming, also pick somewhere else to go
            cmd.buttons |= BTN_JUMP;
            b.strafeDir = rnd(b) < 0.5f ? -1.0f : 1.0f;
            b.strafeSwitchAt = now + 0.8f;
            b.goalUntil = 0;
        }
        b.lastPos = me.move.pos;
        b.stuckCheckAt = now + 1.5f;
    }
    return cmd;
}
