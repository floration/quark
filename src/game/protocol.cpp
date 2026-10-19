#include <cstring>
#include "protocol.h"

static int16_t angQ(float a) { return (int16_t)std::lround(std::remainder(a, 2 * PI) * (32767.0f / PI)); }
static float angD(int16_t q) { return q * (PI / 32767.0f); }

void writeSnapshotClient(Writer& w, const Snapshot& s) {
    w.u32(s.tick);
    w.u32(s.timeMs);
    w.u16(s.tickRate);
    w.u32(s.ackCmdSeq);
    w.u16(s.ackStringCmd);
    w.u8(s.clientId);
    w.vec(s.self.pos); w.vec(s.self.vel); w.u8(s.self.flags); w.f32(s.self.dashCooldown); w.vec(s.self.hookPoint);
    w.f32(s.self.dashTimer); w.f32(s.self.dashDirX); w.f32(s.self.dashDirZ); w.f32(s.self.dashExitSpeed); w.f32(s.self.dashUp); w.f32(s.self.hookCooldown); w.u8(s.self.mods);
    w.u8(s.selfHealth); w.u8(s.selfWeapon);
    w.u32(s.baseTick);
}

bool readSnapshotClient(Reader& r, Snapshot& s) {
    s.tick = r.u32();
    s.timeMs = r.u32();
    s.tickRate = r.u16();
    s.ackCmdSeq = r.u32();
    s.ackStringCmd = r.u16();
    s.clientId = r.u8();
    s.self.pos = r.vec(); s.self.vel = r.vec(); s.self.flags = r.u8(); s.self.dashCooldown = r.f32(); s.self.hookPoint = r.vec();
    s.self.dashTimer = r.f32(); s.self.dashDirX = r.f32(); s.self.dashDirZ = r.f32(); s.self.dashExitSpeed = r.f32(); s.self.dashUp = r.f32(); s.self.hookCooldown = r.f32(); s.self.mods = r.u8();
    s.selfHealth = r.u8(); s.selfWeapon = r.u8();
    s.baseTick = r.u32();
    return !r.bad();
}

static void writeMode(Writer& w, const ModeState& m) {
    // timers in tenths of a second (the HUD shows whole seconds): the mode state then changes at most
    // 10 times a second instead of every tick, so most snapshots send it as "unchanged" (1 byte)
    auto ds = [&](float t) { w.u16((uint16_t)clampf(std::round(t * 10.0f), 0, 65535)); };
    w.u8(m.mode); w.u8(m.phase); ds(m.timeLeft); w.u8(m.round); w.u8(m.roundsToWin);
    w.i16(m.scoreA); w.i16(m.scoreB); w.u8(m.attackers); w.u8(m.flags);
    w.u8(m.bombState); w.u8(m.bombCarrier); w.vecq(m.bombPos); ds(m.bombTimer);
    w.u8((uint8_t)clampf(std::round(m.actionProgress * 255.0f), 0, 255)); w.u8(m.actionPlayer);
    w.u8(m.hillOwner); w.i16(m.target);
    w.f32(m.payloadDist); w.f32(m.payloadLen); // exact: the cart is solid and prediction must match
    w.u8(m.voteActive); w.u8(m.voteYes); w.u8(m.voteNo); w.u8(m.voteVoters); ds(m.voteTimeLeft); w.u8(m.duelMod); w.u8(m.arenaVariant); w.u16(m.weaponMask);
}
static void readMode(Reader& r, ModeState& m) {
    m.mode = r.u8(); m.phase = r.u8(); m.timeLeft = r.u16() / 10.0f; m.round = r.u8(); m.roundsToWin = r.u8();
    m.scoreA = r.i16(); m.scoreB = r.i16(); m.attackers = r.u8(); m.flags = r.u8();
    m.bombState = r.u8(); m.bombCarrier = r.u8(); m.bombPos = r.vecq(); m.bombTimer = r.u16() / 10.0f;
    m.actionProgress = r.u8() / 255.0f; m.actionPlayer = r.u8();
    m.hillOwner = r.u8(); m.target = r.i16();
    m.payloadDist = r.f32(); m.payloadLen = r.f32();
    m.voteActive = r.u8(); m.voteYes = r.u8(); m.voteNo = r.u8(); m.voteVoters = r.u8(); m.voteTimeLeft = r.u16() / 10.0f; m.duelMod = r.u8(); m.arenaVariant = r.u8(); m.weaponMask = r.u16();
}

// Player field bits. P_FULL: every field follows (new player, or visibility changed). P_HIDDEN is
// state, not a field: a hidden player carries only scoreboard fields.
enum : uint16_t {
    P_FLAGS = 1 << 0, P_POS = 1 << 1, P_YAW = 1 << 2, P_PITCH = 1 << 3, P_HEALTH = 1 << 4, P_WEAPON = 1 << 5,
    P_KILLS = 1 << 6, P_DEATHS = 1 << 7, P_SCORE = 1 << 8, P_PING = 1 << 9, P_CHECK = 1 << 10, P_HOOK = 1 << 11,
    P_HIDDEN = 1 << 12, P_FULL = 1 << 15,
};
static int16_t q16(float v) { return (int16_t)clampf(std::round(v * 64.0f), -32767, 32767); }
static bool samePos(const Vec3& a, const Vec3& b) { return q16(a.x) == q16(b.x) && q16(a.y) == q16(b.y) && q16(a.z) == q16(b.z); }

void writeSnapshotWorld(Writer& w, const Snapshot& s, const Snapshot* base) {
    // mode state: 0 = same as base, 1 = full copy follows
    uint8_t mb[96], bb[96];
    Writer mw(mb, sizeof mb), bw(bb, sizeof bb);
    writeMode(mw, s.ms);
    if (base) writeMode(bw, base->ms);
    bool modeSame = base && mw.size() == bw.size() && memcmp(mb, bb, mw.size()) == 0;
    w.u8(modeSame ? 0 : 1);
    if (!modeSame) w.bytes(mb, mw.size());

    w.u8((uint8_t)s.players.size());
    for (auto& p : s.players) {
        const PlayerSnap* b = nullptr;
        if (base) for (auto& o : base->players) if (o.id == p.id) { b = &o; break; }
        uint16_t m = p.hidden ? P_HIDDEN : 0;
        if (!b || b->hidden != p.hidden) m |= P_FULL | 0x0FFF;
        else {
            if (b->flags != p.flags) m |= P_FLAGS;
            if (!p.hidden && !samePos(b->pos, p.pos)) m |= P_POS;
            if (!p.hidden && angQ(b->yaw) != angQ(p.yaw)) m |= P_YAW;
            if (!p.hidden && angQ(b->pitch) != angQ(p.pitch)) m |= P_PITCH;
            if (b->health != p.health) m |= P_HEALTH;
            if (b->weapon != p.weapon) m |= P_WEAPON;
            if (b->kills != p.kills) m |= P_KILLS;
            if (b->deaths != p.deaths) m |= P_DEATHS;
            if (b->score != p.score) m |= P_SCORE;
            if (b->ping != p.ping) m |= P_PING;
            if (b->checkpoint != p.checkpoint) m |= P_CHECK;
            if (!p.hidden && (b->hooked != p.hooked || (p.hooked && !samePos(b->hookPoint, p.hookPoint)))) m |= P_HOOK;
        }
        if (p.hidden) m &= ~(P_POS | P_YAW | P_PITCH | P_HOOK);
        w.u8(p.id); w.u16(m);
        if (m & P_FLAGS) w.u8(p.flags);
        if (m & P_POS) w.vecq(p.pos);
        if (m & P_YAW) w.i16(angQ(p.yaw));
        if (m & P_PITCH) w.i16(angQ(p.pitch));
        if (m & P_HEALTH) w.u8(p.health);
        if (m & P_WEAPON) w.u8(p.weapon);
        if (m & P_KILLS) w.i16(p.kills);
        if (m & P_DEATHS) w.i16(p.deaths);
        if (m & P_SCORE) w.i16(p.score);
        if (m & P_PING) w.u16(p.ping);
        if (m & P_CHECK) w.u8(p.checkpoint);
        if (m & P_HOOK) { w.u8(p.hooked); if (p.hooked) w.vecq(p.hookPoint); }
    }
    // projectiles: id, 1 = new (type, owner follow); position and velocity always (they move every tick)
    w.u8((uint8_t)s.projectiles.size());
    for (auto& p : s.projectiles) {
        bool known = false;
        if (base) for (auto& o : base->projectiles) if (o.id == p.id) { known = true; break; }
        w.u16(p.id); w.u8(known ? 0 : 1);
        if (!known) { w.u8(p.type); w.u8(p.owner); }
        w.vecq(p.pos); w.vecq(p.vel);
    }
}

bool readSnapshotWorld(Reader& r, Snapshot& s, const Snapshot* base) {
    if (r.u8()) readMode(r, s.ms);
    else if (base) s.ms = base->ms;
    else return false;
    s.players.resize(r.u8());
    for (auto& p : s.players) {
        p = PlayerSnap();
        p.id = r.u8();
        uint16_t m = r.u16();
        if (!(m & P_FULL)) {
            const PlayerSnap* b = nullptr;
            if (base) for (auto& o : base->players) if (o.id == p.id) { b = &o; break; }
            if (!b) return false; // delta against a player the base doesn't have
            p = *b;
        }
        p.hidden = (m & P_HIDDEN) != 0;
        if (m & P_FLAGS) p.flags = r.u8();
        if (m & P_POS) p.pos = r.vecq();
        if (m & P_YAW) p.yaw = angD(r.i16());
        if (m & P_PITCH) p.pitch = angD(r.i16());
        if (m & P_HEALTH) p.health = r.u8();
        if (m & P_WEAPON) p.weapon = r.u8();
        if (m & P_KILLS) p.kills = r.i16();
        if (m & P_DEATHS) p.deaths = r.i16();
        if (m & P_SCORE) p.score = r.i16();
        if (m & P_PING) p.ping = r.u16();
        if (m & P_CHECK) p.checkpoint = r.u8();
        if (m & P_HOOK) { p.hooked = r.u8(); if (p.hooked) p.hookPoint = r.vecq(); }
        if (p.hidden) p.hooked = 0;
    }
    s.projectiles.resize(r.u8());
    for (auto& p : s.projectiles) {
        p.id = r.u16();
        if (r.u8()) { p.type = r.u8(); p.owner = r.u8(); }
        else {
            const ProjectileSnap* b = nullptr;
            if (base) for (auto& o : base->projectiles) if (o.id == p.id) { b = &o; break; }
            if (!b) return false;
            p.type = b->type; p.owner = b->owner;
        }
        p.pos = r.vecq(); p.vel = r.vecq();
    }
    return !r.bad();
}

void writeSnapshotEvents(Writer& w, const std::vector<GameEvent>& events) {
    w.u8((uint8_t)events.size());
    for (auto& e : events) {
        w.u32(e.id); w.u8(e.type); w.u8(e.a); w.u8(e.b); w.i16(e.c); w.str(e.text);
    }
}

bool readSnapshotEvents(Reader& r, Snapshot& s) {
    s.events.resize(r.u8());
    for (auto& e : s.events) {
        e.id = r.u32(); e.type = r.u8(); e.a = r.u8(); e.b = r.u8(); e.c = r.i16(); e.text = r.str();
    }
    return !r.bad();
}
