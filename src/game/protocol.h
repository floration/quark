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
    PKT_CONNECT = 1,     // u16 version, u32 nonce, u32 cookie (0 = none yet, from PKT_CHALLENGE), str name
    PKT_INPUT,           // u32 nonce, u32 ackTimeMs, u32 ackEvent, u8 lossPct, u8 ncmds, cmds..., u8 nStr [u16 firstSeq, nStr x str]
    PKT_DISCONNECT,      // u32 nonce
    PKT_INFO_REQUEST,    // padded with zeros to kInfoRequestBytes (no reply bigger than the request)
    PKT_MAP_REQUEST,     // u32 nonce, u32 mapCrc, u32 offset: ask for a piece of the server's map file
    PKT_PROBE_REQUEST,   // u32 nonce, u16 port, padded to kProbeRequestBytes: "is <my ip>:<port> reachable?" (any server answers)
    // server -> client
    PKT_ACCEPT = 64,     // u32 nonce, u8 clientId, str map, u32 mapCrc, u8 mode
    PKT_REJECT,          // str reason
    PKT_SNAPSHOT,        // u32 nonce, see Snapshot
    PKT_INFO_REPLY,      // str serverName, str map, u8 mode, u8 players, u8 maxPlayers, u16 port
    PKT_KICKED,          // u32 nonce, str reason
    PKT_MAP_DATA,        // u32 nonce, u32 mapCrc (its id: edits chain it), u32 textCrc (crc32 of the text sent), u32 totalBytes, u32 offset, u16 n, n bytes
    PKT_CHALLENGE,       // u32 nonce, u32 cookie: echo the cookie in PKT_CONNECT to prove you own your address
    PKT_PROBE_REPLY,     // u32 nonce: sent by a prober to the requester's own IP at the port it asked about
};

constexpr int kInfoRequestBytes = 160;      // PKT_INFO_REQUEST size; the reply is trimmed to fit (no amplification)
constexpr int kInfoNameMax = 96, kInfoMapMax = 40; // 5 + 97 + 41 + 5 <= kInfoRequestBytes
constexpr int kProbeRequestBytes = 32;      // PKT_PROBE_REQUEST size (the 9 byte reply is smaller: no amplification)
constexpr int kMapChunk = 1200;              // map file bytes per PKT_MAP_DATA
constexpr uint32_t kMaxMapBytes = 8u << 20;  // refuse to download anything bigger

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
    int16_t dmg = 0, obj = 0;
    uint8_t acc = 0;            // accuracy, %   // damage dealt to enemies, whole seconds on the objective
    uint16_t ping = 0;
    uint8_t checkpoint = 0;
    uint8_t hooked = 0;
    uint16_t keys = 0;          // held keys (KEY_*), for the spectators' key overlay
    Vec3 hookPoint;
    bool hidden = false;    // not visible to this client: only scoreboard fields are valid (no position)
    int team() const { return (flags & PS_SPECTATOR) ? TEAM_SPECTATOR : (flags & PS_TEAM_B) ? TEAM_B : TEAM_A; }
};

struct ProjectileSnap {
    uint16_t id = 0;
    uint8_t type = 0;
    uint8_t owner = 0;
    Vec3 pos, vel;
};

struct GameEvent {
    double sentAt = -1;      // server only (not sent): when this copy last went out, for resend pacing
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
    // multi point: the point being attacked (= points captured so far), how many the map has, its capture 0..1
    uint8_t point = 0, pointCount = 0;
    float capture = 0;
    // vote in progress (text arrives as an EV_VOTE event)
    uint8_t voteActive = 0, voteYes = 0, voteNo = 0, voteVoters = 0;
    float voteTimeLeft = 0;
    uint8_t duelMod = 255;    // duel: this round's DUEL_* modifier (255 = none)
    uint8_t arenaVariant = 0; // 1v1: ArenaVariant index
    uint16_t weaponMask = 0;  // weapons allowed right now (bit per weapon); 0 = all
};
enum : uint8_t { MS_DRAIN = 1, MS_OVERTIME = 2, MS_CONTESTED = 4, MS_EDITING = 8, MS_RANKED = 16, MS_TEMP_EDIT = 32, MS_TIMETRIAL = 64 }; // MS_TIMETRIAL: solo ranked race (times go to the race board) // MS_TEMP_EDIT: voted editing, reverts at the next map change // MS_EDITING: map edit session, nobody plays

struct Snapshot {
    uint32_t tick = 0;        // server tick counter (one snapshot per tick; gaps = loss)
    uint32_t timeMs = 0;      // server clock in ms
    uint16_t tickRate = 1000; // current server tick rate (Hz)
    uint16_t snapRate = 1000; // snapshots per second this client gets (interpolation buffers 2 of them)
    uint32_t ackCmdSeq = 0;
    uint32_t baseTick = 0;    // the players / projectiles / mode part is a delta against this tick (0 = full)
    uint16_t ackStringCmd = 0;
    uint8_t clientId = 0;
    MoveState self;
    uint8_t selfHealth = 0;
    uint8_t selfWeapon = 0;
    uint8_t knifeAway = 0;    // tenths of a second until your thrown knife is back (0 = have it)
    ModeState ms;
    std::vector<PlayerSnap> players;
    std::vector<ProjectileSnap> projectiles;
    std::vector<GameEvent> events;
};

// Wire order: client header (own movement state, baseTick), then the world part (mode state, players,
// projectiles) delta-encoded against `base` (the client's last acknowledged snapshot; nullptr = full),
// then reliable events. Unchanged players cost 3 bytes, unchanged mode state 1 byte.
void writeSnapshotClient(Writer& w, const Snapshot& s);
void writeSnapshotWorld(Writer& w, Snapshot& s, const Snapshot* base); // also records into s what the client will have (delta base)
void writeSnapshotEvents(Writer& w, const std::vector<GameEvent>& events);
bool readSnapshotClient(Reader& r, Snapshot& s);              // header: tells which base the world part needs
bool readSnapshotWorld(Reader& r, Snapshot& s, const Snapshot* base);
bool readSnapshotEvents(Reader& r, Snapshot& s);
// EV_BRACKET text (one "|"-separated row of entrants per round, then "@round,match") -> the tourney bracket.
// Entrants are player ids, -1 a bye, -2 not decided yet; anything else becomes -2, the current round and match
// are clamped into the bracket, and a malformed bracket comes back empty (false).
bool parseBracket(const std::string& text, std::vector<std::vector<int>>& rounds, int& round, int& match);
