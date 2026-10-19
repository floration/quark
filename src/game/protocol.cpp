#include "protocol.h"

static int16_t angQ(float a) { return (int16_t)std::lround(std::remainder(a, 2 * PI) * (32767.0f / PI)); }
static float angD(int16_t q) { return q * (PI / 32767.0f); }

void writeSnapshot(Writer& w, const Snapshot& s) {
    writeSnapshotClient(w, s);
    writeSnapshotShared(w, s);
    writeSnapshotEvents(w, s.events);
}

void writeSnapshotClient(Writer& w, const Snapshot& s) {
    w.u32(s.tick);
    w.u32(s.timeMs);
    w.u16(s.tickRate);
    w.u32(s.ackCmdSeq);
    w.u16(s.ackStringCmd);
    w.u8(s.clientId);
    w.vec(s.self.pos); w.vec(s.self.vel); w.u8(s.self.flags); w.f32(s.self.dashCooldown); w.vec(s.self.hookPoint);
    w.f32(s.self.dashTimer); w.f32(s.self.dashDirX); w.f32(s.self.dashDirZ); w.f32(s.self.dashExitSpeed); w.f32(s.self.hookCooldown);
    w.u8(s.selfHealth); w.u8(s.selfWeapon);
}

void writeSnapshotShared(Writer& w, const Snapshot& s) {
    const ModeState& m = s.ms;
    w.u8(m.mode); w.u8(m.phase); w.f32(m.timeLeft); w.u8(m.round); w.u8(m.roundsToWin);
    w.i16(m.scoreA); w.i16(m.scoreB); w.u8(m.attackers); w.u8(m.flags);
    w.u8(m.bombState); w.u8(m.bombCarrier); w.vecq(m.bombPos); w.f32(m.bombTimer);
    w.f32(m.actionProgress); w.u8(m.actionPlayer);
    w.u8(m.hillOwner); w.i16(m.target);
    w.f32(m.payloadDist); w.f32(m.payloadLen);
    w.u8(m.voteActive); w.u8(m.voteYes); w.u8(m.voteNo); w.u8(m.voteVoters); w.f32(m.voteTimeLeft);

    w.u8((uint8_t)s.players.size());
    for (auto& p : s.players) {
        w.u8(p.id); w.u8(p.flags); w.vecq(p.pos); w.i16(angQ(p.yaw)); w.i16(angQ(p.pitch));
        w.u8(p.health); w.u8(p.weapon); w.i16(p.kills); w.i16(p.deaths); w.i16(p.score);
        w.u16(p.ping); w.u8(p.checkpoint);
        w.u8(p.hooked); if (p.hooked) w.vecq(p.hookPoint);
    }
    w.u8((uint8_t)s.projectiles.size());
    for (auto& p : s.projectiles) {
        w.u16(p.id); w.u8(p.type); w.u8(p.owner); w.vecq(p.pos); w.vecq(p.vel);
    }
}

void writeSnapshotEvents(Writer& w, const std::vector<GameEvent>& events) {
    w.u8((uint8_t)events.size());
    for (auto& e : events) {
        w.u32(e.id); w.u8(e.type); w.u8(e.a); w.u8(e.b); w.i16(e.c); w.str(e.text);
    }
}

bool readSnapshot(Reader& r, Snapshot& s) {
    s.tick = r.u32();
    s.timeMs = r.u32();
    s.tickRate = r.u16();
    s.ackCmdSeq = r.u32();
    s.ackStringCmd = r.u16();
    s.clientId = r.u8();
    s.self.pos = r.vec(); s.self.vel = r.vec(); s.self.flags = r.u8(); s.self.dashCooldown = r.f32(); s.self.hookPoint = r.vec();
    s.self.dashTimer = r.f32(); s.self.dashDirX = r.f32(); s.self.dashDirZ = r.f32(); s.self.dashExitSpeed = r.f32(); s.self.hookCooldown = r.f32();
    s.selfHealth = r.u8(); s.selfWeapon = r.u8();

    ModeState& m = s.ms;
    m.mode = r.u8(); m.phase = r.u8(); m.timeLeft = r.f32(); m.round = r.u8(); m.roundsToWin = r.u8();
    m.scoreA = r.i16(); m.scoreB = r.i16(); m.attackers = r.u8(); m.flags = r.u8();
    m.bombState = r.u8(); m.bombCarrier = r.u8(); m.bombPos = r.vecq(); m.bombTimer = r.f32();
    m.actionProgress = r.f32(); m.actionPlayer = r.u8();
    m.hillOwner = r.u8(); m.target = r.i16();
    m.payloadDist = r.f32(); m.payloadLen = r.f32();
    m.voteActive = r.u8(); m.voteYes = r.u8(); m.voteNo = r.u8(); m.voteVoters = r.u8(); m.voteTimeLeft = r.f32();

    s.players.resize(r.u8());
    for (auto& p : s.players) {
        p.id = r.u8(); p.flags = r.u8(); p.pos = r.vecq(); p.yaw = angD(r.i16()); p.pitch = angD(r.i16());
        p.health = r.u8(); p.weapon = r.u8(); p.kills = r.i16(); p.deaths = r.i16(); p.score = r.i16();
        p.ping = r.u16(); p.checkpoint = r.u8();
        p.hooked = r.u8(); if (p.hooked) p.hookPoint = r.vecq();
    }
    s.projectiles.resize(r.u8());
    for (auto& p : s.projectiles) {
        p.id = r.u16(); p.type = r.u8(); p.owner = r.u8(); p.pos = r.vecq(); p.vel = r.vecq();
    }
    s.events.resize(r.u8());
    for (auto& e : s.events) {
        e.id = r.u32(); e.type = r.u8(); e.a = r.u8(); e.b = r.u8(); e.c = r.i16(); e.text = r.str();
    }
    return !r.bad();
}
