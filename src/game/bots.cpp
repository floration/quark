#include "bots.h"
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
    {0.45f, 0.050f, 0.010f, 0.12f, 0.30f, 4.5f,  25.0f, 0.09f,  true,  false, false, false, false}, // beginner
    {0.36f, 0.044f, 0.008f, 0.10f, 0.22f, 6.0f,  40.0f, 0.07f,  true,  false, false, true,  true},  // intermediate
    {0.26f, 0.020f, 0.004f, 0.04f, 0.14f, 9.0f,  75.0f, 0.05f,  true,  true,  true,  true,  true},  // advanced
    {0.17f, 0.010f, 0.002f, 0.015f, 0.07f, 15.0f, 150.0f, 0.035f, true, true,  true,  true,  true},  // expert
};

float rnd(BotBrain& b) { // xorshift, 0..1
    b.rng ^= b.rng << 13; b.rng ^= b.rng >> 17; b.rng ^= b.rng << 5;
    return (b.rng & 0xFFFFFF) / 16777216.0f;
}

float angleDiff(float a, float b) { return std::remainder(a - b, 2 * PI); }

Vec3 eyeOf(const ServerPlayer& p) {
    return p.move.pos + Vec3(0, (p.move.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight, 0);
}

bool isEnemy(const Server& s, int a, int b) {
    if (a == b) return false;
    const auto& pb = s.players_[b];
    if (!pb.connected || !pb.alive || pb.team == TEAM_SPECTATOR) return false;
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
            if (map.payloadPath.size() >= 2) return map.payloadPoint(m.payloadDist);
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
    static const int close[] = {WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_ROCKET, WPN_RIFLE, WPN_STICKY, WPN_CROSSBOW, WPN_SNIPER};
    static const int mid[]   = {WPN_ROCKET, WPN_LASER, WPN_RIFLE, WPN_STICKY, WPN_CROSSBOW, WPN_SHOTGUN, WPN_SNIPER, WPN_KNIFE};
    static const int midNoLead[] = {WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_ROCKET, WPN_STICKY, WPN_CROSSBOW, WPN_SNIPER, WPN_KNIFE};
    static const int far[]   = {WPN_CROSSBOW, WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_KNIFE};
    static const int farNoLead[] = {WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_CROSSBOW, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_KNIFE};
    static const int vfar[]  = {WPN_SNIPER, WPN_CROSSBOW, WPN_RIFLE, WPN_ROCKET, WPN_STICKY, WPN_LASER, WPN_SHOTGUN, WPN_KNIFE};
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
        if (a.mx.x - a.mn.x < 0.6f || a.mx.z - a.mn.z < 0.6f) continue;
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
    // Dijkstra (maps have at most a few hundred surfaces)
    std::vector<float> dist(nodes.size(), 1e30f);
    std::vector<int> prev(nodes.size(), -1);
    std::vector<bool> done(nodes.size(), false);
    dist[a] = 0;
    for (;;) {
        int u = -1;
        for (size_t i = 0; i < nodes.size(); i++) if (!done[i] && (u < 0 || dist[i] < dist[u])) u = (int)i;
        if (u < 0 || dist[u] >= 1e30f || u == b) break;
        done[u] = true;
        for (auto& [v, c] : links[u]) if (dist[u] + c < dist[v]) { dist[v] = dist[u] + c; prev[v] = u; }
    }
    if (prev[b] < 0) return to;
    std::vector<int> path; // a (exclusive) .. b
    for (int v = b; v != a; v = prev[v]) path.push_back(v);
    // Walk to the closest point of the next surface (a little inside its edge). If that point is
    // right where we stand (overlapping surfaces, e.g. stepping off a wall onto the floor below),
    // look further along the path.
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const Node& n = nodes[*it];
        float inset = 0.5f;
        float x0 = std::min(n.x0 + inset, (n.x0 + n.x1) * 0.5f), x1 = std::max(n.x1 - inset, (n.x0 + n.x1) * 0.5f);
        float z0 = std::min(n.z0 + inset, (n.z0 + n.z1) * 0.5f), z1 = std::max(n.z1 - inset, (n.z0 + n.z1) * 0.5f);
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
    Vec3 eye = eyeOf(me);

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
            if (d < bestD && canSee(s, eye, chest)) { bestD = d; best = i; }
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
        bool throwKnife = w == WPN_KNIFE && dist > wd.range + 0.5f && dist < 18.0f && sp.lead; // out of reach: throw it
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
                    : w == WPN_STICKY ? 10.0f : w == WPN_CROSSBOW ? 22.0f : w == WPN_RIFLE ? 18.0f : 14.0f;
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
        if ((me.move.flags & MF_ONGROUND) && !s.map_.raycastAny(ahead, {0, -1, 0}, 1.5f)) cmd.buttons |= BTN_JUMP;
        if (sp.bhop && s.ms_.mode != MODE_RACE) cmd.buttons |= BTN_JUMP;
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
