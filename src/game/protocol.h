// Wire protocol shared by server and client.
//
// Every packet starts with: u32 magic, u8 type. Connected packets then carry the
// u32 session nonce from PKT_ACCEPT so stray/spoofed datagrams are dropped.
#pragma once
#include <string>
#include <vector>
#include "../common/buffer.h"
#include "gamedefs.h"
#include "pmove.h"

enum PacketType : uint8_t {
    // client -> server
    PKT_CONNECT = 1,     // u16 version, u32 nonce, str name
    PKT_INPUT,           // u32 nonce, u32 ackTimeMs, u32 ackEvent, u8 lossPct, u8 ncmds, cmds..., u8 hasCmd [u16 seq, str]
    PKT_DISCONNECT,      // u32 nonce
    PKT_INFO_REQUEST,    // (none)
    // server -> client
    PKT_ACCEPT = 64,     // u32 nonce, u8 clientId, str map, u32 mapCrc, u8 mode
    PKT_REJECT,          // str reason
    PKT_SNAPSHOT,        // u32 nonce, see Snapshot
    PKT_INFO_REPLY,      // str serverName, str map, u8 mode, u8 players, u8 maxPlayers, u16 port
    PKT_KICKED,          // u32 nonce, str reason
};

inline void writeHeader(Writer& w, PacketType t) { w.u32(kProtocolMagic); w.u8(t); }

inline void writeCmd(Writer& w, const UserCmd& c) {
    w.u32(c.seq); w.u32(c.viewTick); w.i8(c.forward); w.i8(c.side); w.u16(c.buttons); w.u8(c.msec); w.u8(c.weapon);
    w.f32(c.yaw); w.f32(c.pitch);
}
inline UserCmd readCmd(Reader& r) {
    UserCmd c;
    c.seq = r.u32(); c.viewTick = r.u32(); c.forward = r.i8(); c.side = r.i8(); c.buttons = r.u16(); c.msec = r.u8(); c.weapon = r.u8();
    c.yaw = r.f32(); c.pitch = clampf(r.f32(), -1.55f, 1.55f);
    return c;
}

enum PlayerSnapFlags : uint8_t {
    PS_ALIVE = 1, PS_CROUCHED = 2, PS_HASBOMB = 4, PS_SPECTATOR = 8, PS_TEAM_B = 16, PS_CONNECTED = 32, PS_FINISHED = 64, PS_FIRING = 128,
};

struct PlayerSnap {
    uint8_t id = 0;
    uint8_t flags = 0;
    Vec3 pos;
    float yaw = 0, pitch = 0;
    uint8_t health = 0;
    uint8_t weapon = 0;
    int16_t kills = 0, deaths = 0, score = 0;
    uint16_t ping = 0;
    uint8_t checkpoint = 0;
    uint8_t hooked = 0;
    Vec3 hookPoint;
    int team() const { return (flags & PS_SPECTATOR) ? TEAM_SPECTATOR : (flags & PS_TEAM_B) ? TEAM_B : TEAM_A; }
};

struct ProjectileSnap {
    uint16_t id = 0;
    uint8_t type = 0;
    uint8_t owner = 0;
    Vec3 pos, vel;
};

struct GameEvent {
    uint32_t id = 0;
    uint8_t type = 0;
    uint8_t a = 0, b = 0;
    int16_t c = 0;
    std::string text;
};

enum BombState : uint8_t { BOMB_NONE = 0, BOMB_CARRIED, BOMB_DROPPED, BOMB_PLANTED, BOMB_EXPLODED, BOMB_DEFUSED };

struct ModeState {
    uint8_t mode = 0;
    uint8_t phase = 0;
    float timeLeft = 0;       // seconds left in current phase (or round)
    uint8_t round = 0;
    uint8_t roundsToWin = 0;
    int16_t scoreA = 0, scoreB = 0;
    uint8_t attackers = TEAM_A;
    uint8_t flags = 0;        // MS_*
    // bomb
    uint8_t bombState = BOMB_NONE;
    uint8_t bombCarrier = 255;
    Vec3 bombPos;
    float bombTimer = 0;
    float actionProgress = 0; // 0..1 plant/defuse progress of the acting player
    uint8_t actionPlayer = 255;
    // koth
    uint8_t hillOwner = 255;
    int16_t target = 0;
    // payload
    float payloadDist = 0;
    float payloadLen = 0;
    // vote in progress (text arrives as an EV_VOTE event)
    uint8_t voteActive = 0, voteYes = 0, voteNo = 0, voteVoters = 0;
    float voteTimeLeft = 0;
    uint8_t duelMod = 255;    // duel: this round's DUEL_* modifier (255 = none)
};
enum : uint8_t { MS_DRAIN = 1, MS_OVERTIME = 2, MS_CONTESTED = 4 };

struct Snapshot {
    uint32_t tick = 0;        // server tick counter (one snapshot per tick; gaps = loss)
    uint32_t timeMs = 0;      // server clock in ms
    uint16_t tickRate = 1000; // current server tick rate (Hz)
    uint32_t ackCmdSeq = 0;
    uint16_t ackStringCmd = 0;
    uint8_t clientId = 0;
    MoveState self;
    uint8_t selfHealth = 0;
    uint8_t selfWeapon = 0;
    ModeState ms;
    std::vector<PlayerSnap> players;
    std::vector<ProjectileSnap> projectiles;
    std::vector<GameEvent> events;
};

void writeSnapshot(Writer& w, const Snapshot& s);
// The pieces of writeSnapshot, in wire order: the server encodes the shared part once per tick.
void writeSnapshotClient(Writer& w, const Snapshot& s);   // per-client header + own movement state
void writeSnapshotShared(Writer& w, const Snapshot& s);   // mode state, players, projectiles
void writeSnapshotEvents(Writer& w, const std::vector<GameEvent>& events);
bool readSnapshot(Reader& r, Snapshot& s);
