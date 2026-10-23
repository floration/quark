// Authoritative game server. Used by the standalone quark_server executable and by
// the client when hosting a game (runs on its own thread, client connects via loopback).
#pragma once
#include "governor.h"
#include <cmath>
#include <atomic>
#include <cstdio>
#include <memory>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>
#include "../common/net.h"
#include "../common/upnp.h"
#include "map.h"
#include "protocol.h"
#include "bots.h"
#include "master.h"
#include "customrules.h"
#include "ranked.h"

// Every game: a player whose connection drops keeps their slot, team, score and state this long (seconds) and gets
// them back by reconnecting (matched by their proven ranked id, else by the token from PKT_ACCEPT).
constexpr double kReconnectGrace = 10.0;

// Per-player (and connect) token buckets: each starts full, at its burst.
constexpr int kMaxQueuedCmds = 12;                          // input commands queued per player at most
constexpr float kCatchUpMax = (float)kMaxQueuedCmds;        // backlog catch-up allowance (see server.cpp kDeepQueue)
constexpr float kChatBurst = 5, kChatPerSecond = 1;         // say: a burst of 5, then one a second (REVIEW NET-7)
constexpr float kMapReqBurst = 64, kMapReqPerSec = 400;     // PKT_MAP_REQUEST chunks (a download asks ~128/s)
constexpr float kVerifyBurst = 50, kVerifyPerSec = 50;      // connect signature checks (each ~0.1 ms of CPU)

// Tests running on a sped-up clock turn off the warmup tick-rate check (it would see "late" ticks).
extern bool g_serverRateCheck;

struct ServerConfig {
    std::string name = "quark server";
    std::string map = "bastion";
    int mode = MODE_FFA;
    uint16_t port = kDefaultPort;
    int maxPlayers = 8;         // up to kMaxPlayers (32); big servers start at a lower tick rate
    bool upnp = false;          // try to open the port on the router automatically
    int maxTickRate = 1000;     // starting (highest) tick rate; lowered automatically if the host can't keep up
    int snapRate = 500;         // snapshots per second to each client (<= tick rate): 1000 = lowest latency, lower = less CPU / bandwidth
    // Bots
    bool balanceBots = true;    // team modes: an extra bot joins the side with fewer humans (vote can turn off)
    int balanceBotSkill = BOT_INTERMEDIATE;
    bool vsBots = false;        // "play vs bots": fixed bots, humans always on team A
    bool autoStart = false;     // start the match automatically once both sides have players (else: vote)
    int botsEnemy = 0, botsAlly = 0, botSkill = BOT_INTERMEDIATE;
    bool lanOnly = false;
    // More addresses ("host:port") that reach this server, for checking ranked signatures (REVIEW 2.2): needed only
    // when players dial an address the server can't see itself (a router forwarding another port to it, a container).
    // Its own interface addresses, loopback and its public IPv4 (looked up at start, or from UPnP) are known anyway.
    std::vector<std::string> ownAddrs;
    // Reachability check (in-game hosting): ask a public server to send one packet to our public ip:port.
    bool probe = false;
    std::string prober;         // "host:port" of the prober; empty = QUARK_PROBER env, else a server from the internet list
    uint16_t probeAskPort = 0;  // tests only: ask about this port instead of ours (simulates an unforwarded port)
    // Mode tuning (0 = mode default)
    int roundsToWin = 0;
    int timeLimit = 0;          // seconds (ffa)
    int scoreLimit = 0;         // koth points
    // Game log: 0 off, 1 simple (deaths, map changes, chat, wins), 2 verbose (+ 125 Hz gameplay),
    // 3 extreme (every tick: inputs, movement, projectiles). File defaults to logs/quark-<time>.log.
    int logLevel = 0;
    std::string logFile;
    int duelTeamSize = 1;       // duel: players per side, 1 (1v1) or 2 (2v2)
    int arenaVariant = 0;       // 1v1 mode: ArenaVariant index
    bool publicList = false;    // announce on the internet server list (Join screen)
    // tourney: maps and 1v1 rulesets the matches rotate through (empty = current map / arenaVariant)
    std::vector<std::string> tourneyMaps;
    std::vector<int> tourneyVariants;
    bool editMode = false;      // start as a map edit session (anyone connected can edit; the host autosaves)
    std::string restartFlag;    // if this file appears (an update was installed), restart once no match is being played
    bool timeTrial = false;     // solo ranked race on Viaduct: no bots, checkpoint splits sent to the racer
    bool ranked = false;        // matches are rated (OpenSkill) once every player reports the same result; no bots
    // Release channel this server runs for (GOV_PROD / GOV_TESTING; -1: by the build, testing for a -test. version).
    // A testing server (or any testing build) plays ranked normally but never sends anything to the ranking server.
    int channel = -1;
    // Load governor (governor.h): a directory shared with the other server on this machine (tools/server/install.sh
    // runs a production and a testing server per box). Empty: none (a single server, in-game hosting).
    std::string governorDir;
    GovParams governor;              // (tests shorten its windows)
    double governorInterval = 1.0;   // seconds between status rounds
    CustomRules custom;         // a custom mode (name empty = none): its mode, weapons, movement, map pool, ...
};
enum LogLevel { LOG_OFF = 0, LOG_SIMPLE, LOG_VERBOSE, LOG_EXTREME };
inline int logLevelFromName(const std::string& s) {
    return s == "simple" ? LOG_SIMPLE : s == "verbose" ? LOG_VERBOSE : s == "extreme" ? LOG_EXTREME : s == "off" ? LOG_OFF : -1;
}

struct ServerPlayer {
    uint32_t sentSeq = 0;     // snapshots sent (indexes the delta-base ring)
    bool connected = false;
    bool isBot = false;
    bool balanceBot = false;    // added automatically to even out teams
    bool letIn = false;         // duel: voted in past the side's size (joined when the sides were even and full)
    NetAddr addr;
    uint32_t nonce = 0;
    std::string name;
    std::string rankId;         // 32 hex chars (ranked), proven at connect by signing the challenge; empty if none
    std::string rankPub;        // the Ed25519 public key behind rankId (checks the match ticket signature)
    std::string ticketSig;      // this player's signature of the current ranked match id (forwarded with the ticket)
    NetAddr dialled;            // the server address this player's game signed at connect (one of ours): its ticket signature names it too
    uint64_t reconnectToken = 0; // given in PKT_ACCEPT: a reconnect that shows it gets this slot back
    double awayUntil = 0;       // > 0: the connection dropped; slot, team, score and state kept until then (timeNow())
    std::string awayReason;
    bool dropPending = false;   // over the unacked-event cap: dropped in frame(), not inside whatever was sending (REVIEW 2.1)
    double voteAt = 0;           // no new vote from this player before then (after one of theirs failed)
    float mapTokens = kMapReqBurst; // PKT_MAP_REQUEST rate limit (token bucket)
    double mapTokensAt = 0;
    int team = TEAM_A;
    bool alive = false;
    bool finished = false;      // race
    MoveState move;
    float yaw = 0, pitch = 0;
    uint16_t buttons = 0, prevButtons = 0;
    float health = 0;
    int weapon = WPN_ROCKET;
    int selectedWeapon = WPN_ROCKET; // last weapon the player picked (slot 1 to start); kept across respawns
    double knifeBackAt = 0;      // a thrown knife is away (disabled) until then: it can't be drawn or used before it
    double syringeBackAt = 0;    // ...and a thrown syringe
    double backAt(int w) const { return w == WPN_KNIFE ? knifeBackAt : w == WPN_SYRINGE ? syringeBackAt : 0; }
    double knifeSwingUntil = -1; // a swing that missed keeps cutting along your aim until then
    bool fireLocked = false;    // after a knife throw: nothing fires until both buttons are let go
    double nextFire = 0;
    double lastFire = -10;
    uint32_t burstShot = 0;     // shots in the current held burst - 1: indexes the fixed spread sequence (burstOffset)
    int burstWeapon = -1;       // the weapon that burst is with
    double fireQueuedUntil = -1; // semi-auto: a click waiting for the weapon to be ready
    int kills = 0, deaths = 0, score = 0;
    double respawnAt = -1;
    int checkpoint = 0;         // race: next checkpoint index
    std::vector<int> raceSplits;   // race: ms at each checkpoint of the current run (time trials report it)
    uint32_t joinId = 0;        // unique per connection (a new player in a reused slot gets a new one)
    float dmgDealt = 0, objTime = 0;    // scoreboard: damage to enemies, seconds on the objective (score = dmg / 10 + obj)
    int shotsFired = 0, shotsHit = 0;   // accuracy: shots (hitscan or projectile) that damaged an enemy
    bool hitSinceShot = false;
    double typingAt = 0, quipAt = 0, locateAt = 0;   // next "typing" relay allowed (and quip pings, the locator ready)
    Vec3 locatedPos;            // the locator's last find (bots head there)
    double locatedAt = -1e9;
    int model = 0;              // player model (cosmetic, kPlayerModels looks, same hitbox)
    int soundPack = 0;      // cosmetic: the sound pack others hear this player with
    bool it = false;            // hide and seek / tag: the seeker / tagger (has the knife)
    bool found = false;         // hide and seek: found by the seeker this round (falling off doesn't count)
    bool sought = false;        // hide and seek: has had their turn seeking this match
    bool waitingToPlay = false; // joined during a tag match: spectates it, joins team A when it's over
    bool specLocked = false;    // voted to spectate (callvote spec): can't rejoin play until the next map
    uint32_t specVoted = 0;     // players (bit per slot) this one has called a spectate vote on this map: once each
    Vec3 lastGround;            // where the player last stood (a dropped bomb goes here, not into a pit)
    int hookVictim = -1;        // this player's grapple is latched onto that player (pulling the two together)
    int hookedBy = -1;          // ...held by that player's grapple (move: MF_PULLED)
    double hookSince = 0;       // when the latch caught
    bool out = false;           // hide and seek: found this round; tag: eliminated for the match
    double itFreezeUntil = 0;    // tag: just got tagged, stands still until then
    uint32_t lastCmdSeq = 0;
    uint16_t keys = 0;          // KEY_* from the last command
    float chatTokens = kChatBurst; // say: a burst, then one a second (REVIEW NET-7)
    double chatAt = 0;
    uint32_t strTick = 0;       // string commands handled this tick (a budget: the rest wait, the client resends them)
    int strCount = 0, edCount = 0;
    std::deque<UserCmd> cmdQueue;
    float cmdTokensMs = 16.0f;
    int deepTicks = 0;          // ticks in a row the command queue has held a backlog (see kDeepQueue)
    float catchUp = kCatchUpMax; // extra commands it may run to clear a backlog (refills very slowly)
    uint32_t cmdsRun = 0;       // commands simulated (nobody gets more than real time: see the tests)
    uint8_t reportedLoss = 0;   // % of snapshots the client says it missed
    uint64_t lastViewTick = 0;   // the server ms its last command was looking at (unwrapped, see fromWireMs)
    uint16_t interpMs = 0;        // the fixed interpolation delay the client reported ("interp"; 0 = adaptive)
    double lastRecv = 0;
    float ping = 0;
    uint16_t lastStringCmd = 0;
    uint32_t nextEventId = 1;
    uint32_t ackedEvent = 0;
    std::deque<GameEvent> events; // unacknowledged reliable events
    double lastDamagedAt = -1e9;   // (regeneration waits 3 s after this: seconds, not ticks, so a rate change doesn't move it)
    // Delta snapshots: what we sent this client recently (ring by tick) and the newest one it confirmed.
    static constexpr int kSentRing = 256;
    std::vector<Snapshot> sent;
    uint64_t ackSnapMs = 0;
    // Visibility (anti-wallhack + bandwidth): when this client last had a line of sight to each player, and when
    // each line is next checked (0: at once).
    double lastSeen[kMaxPlayers] = {};
    double visDue[kMaxPlayers] = {};
    // ... and to each projectile slot (Server::projVisId_ says which projectile the slot's times are about)
    double projSeen[kMaxProjectiles] = {};
    double projDue[kMaxProjectiles] = {};
    int specTarget = -1;        // dead: whose view this client watches ("spectate <id>"; the killer at first): what it's sent
    // lag compensation history
    struct Hist { uint64_t tick; Vec3 pos; bool crouched; bool alive; };   // tick: timeMs_ when recorded (kNoHistory: none)
    static constexpr uint64_t kNoHistory = ~0ull;
    static constexpr int kHistory = 1024; // >= 1 s at any tick rate
    Hist history[kHistory];
};

struct Projectile {
    bool active = false;
    uint16_t id = 0;
    int type = WPN_ROCKET;
    int owner = 0;
    Vec3 pos, vel;
    double dieAt = 0;
    double armAt = 0;    // stickies can't be detonated before this
    float gravMul = 1;  // a rocket lob (alt-fire): kRocketLobGravity (it drops; the primary flies straight)
    bool stuck = false; // stickies: attached to a surface
    bool onCart = false; // stuck to the payload cart: rides along at cartOffset from its center
    Vec3 cartOffset;
};

class Server {
public:
    using LogFn = std::function<void(const std::string&)>;

    ~Server() { stopMapInfo(); }
    bool start(const ServerConfig& cfg, std::string* err = nullptr);
    void stop();
    // Blocking loop for a dedicated server or a hosting thread.
    void run(std::atomic<bool>& quit);
    // Process network + advance as many ticks as are due. Returns seconds until next tick.
    double frame();
    void idle(double secondsUntilNextTick);
    void command(const std::string& line, int fromPlayer = -1); // console / admin commands

    void setLog(LogFn fn) { log_ = std::move(fn); }
    // stop() never waits on the network (FINAL_REVIEW 2.10: leaving a hosted game froze the menu up to ~15 s): what
    // still has HTTP in flight (UPnP removal, the list's /remove, rating lookups) finishes on a detached thread.
    // A process about to exit can give those a moment: true if all finished within `seconds`.
    static bool waitBackground(double seconds);
    static int backgroundJobs();
    // The in-process host's client (its session nonce): the only admin (REVIEW NET-8). Thread safe.
    void setHostNonce(uint32_t n) { hostNonce_ = n; }
    void handlePacket(const NetAddr& from, const uint8_t* data, int len); // (public: tests feed it spoofed sources)
    void receivePacket(const NetAddr& from, const uint8_t* data, int len); // handlePacket inside an exception boundary (ENG-2)
    float voteCooldown_ = 5.0f; // seconds a player waits to call another vote after theirs failed (tests set 0)
    uint16_t port() const { return sock_.localPort(); }
    int playerCount() const;
    bool away(int id) const { return players_[id].awayUntil > 0; }   // dropped, slot held for the reconnect grace
    std::string statusText();
    const ServerConfig& config() const { return cfg_; }
    std::string upnpStatus() { return upnp_ ? upnp_->status() : std::string(); }
    // Reachability from the internet (cfg.probe). Thread safe: the host's menu reads it while the server thread runs.
    enum Reach { REACH_OFF, REACH_CHECKING, REACH_OK, REACH_BLOCKED, REACH_UNKNOWN };
    Reach reachability() const { return (Reach)reach_.load(); }
    std::string reachStatus();
    void recheckReachability() { reachAgain_ = true; }
    bool upnpDone() { return upnp_ && upnp_->done(); }
    bool upnpOk() { return upnp_ && upnp_->ok(); }
    std::string listStatus() { return master_ ? master_->status() : std::string(); }
    // The address a ranked player's signature last named that this server couldn't confirm as its own (REVIEW 2.2;
    // empty: none). If players do reach the server there (a router forwarding another port, a container), starting
    // it with --own-addr <that address> lets their ranked ids work. Thread safe (the host's menu shows it).
    std::string ownAddrHint() { std::lock_guard<std::mutex> l(reachMu_); return ownAddrHint_; }

    // ---- state used by the game mode rules (gamemodes.cpp) ----
    ServerConfig cfg_;
    Map map_;
    uint32_t tick_ = 0;
    // Server clock: advances 1000/rate ms per tick. 64-bit: it never wraps (32 bits did after 49.7 days of uptime).
    // On the wire (snapshot time, acks, view ticks) it stays 32 bits; each side unwraps what it gets to the value
    // nearest its own clock (fromWireMs here, NetClient::onSnapshot there), so the protocol is unchanged.
    uint64_t timeMs_ = 0;
    uint64_t fromWireMs(uint32_t w) const {
        int64_t t = (int64_t)timeMs_ + (int32_t)(w - (uint32_t)timeMs_);
        return t < 0 ? 0 : (uint64_t)t;
    }
    int rateLevel_ = 0;         // index into kTickRates
    int rateTopLevel_ = 0;      // the highest rate allowed (maxTickRate)
    double rateUpAt_ = 0;       // after stepping down: when to try the next rate up again (backs off each time it fails)
    double rateUpWait_ = 60;
    bool rateDownPending_ = false; // the host fell behind mid-round: step down at the next break (REVIEW GAME-11)
    double rateRaisedAt_ = -1e9;   // the last step up: undone in any phase if it doesn't hold for 3 windows (REVIEW 6.1)
    int tickRate() const { return kTickRates[rateLevel_]; }
    float dt() const { return 1.0f / tickRate(); }
    double time() const { return timeMs_ * 0.001; } // double: a float runs out of millisecond precision after a few hours of uptime
    ServerPlayer players_[kMaxPlayers];
    Projectile projectiles_[kMaxProjectiles];
    uint16_t nextProjId_ = 1;
    ModeState ms_;
    double phaseEnd_ = 0;
    double liveSince_ = 0;        // when the round went live (the map's holdfire counts from it)
    double roundStart_ = 0;
    double lastProgress_ = 0;    // vs: last time damage was dealt
    int lastIt_ = -1;
    // tag: who has been eliminated this match ({rank id, name}): someone who leaves and comes back is still out
    std::vector<std::pair<std::string, std::string>> tagOut_;
    // voted to spectate this map ({rank id, name}): leaving and coming back doesn't get them back into play
    std::vector<std::pair<std::string, std::string>> specOut_;
    void moveToSpectator(int id);   // out of play (as "team spec" does: tag counts it as elimination)
    std::vector<float>* tickTimes_ = nullptr;  // benchmarks: every tick's simulate + send time goes here
    // Snapshot building reuses these and each client's delta-base ring (ServerPlayer::sent): the per-client snapshot
    // is built in the ring slot it's remembered in, so a send copies the shared lists once and allocates nothing
    // once the ring's vectors have grown (PERF-8).
    std::vector<PlayerSnap> snapPlayers_;
    std::vector<ProjectileSnap> snapProj_;
    int snapEvery() const {
        int rate = std::min(cfg_.snapRate, tickRate());
        if (cfg_.maxPlayers > 24) rate = std::min(rate, 250);       // big servers: upload bandwidth grows with players x rate
        else if (cfg_.maxPlayers > 16) rate = std::min(rate, 333);
        // round up (500 Hz at a 333 cap -> every 2nd tick, not every tick), but 1000 / 333 = 3.003 stays 3
        return std::max(1, (int)std::ceil((float)tickRate() / std::max(1, rate) - 0.02f));
    }
    // Hide and seek tiebreak (owner B8; gamemodes.cpp): rock paper scissors among those tied for first, in the match's
    // last round end (ModeState MS_RPS). Every change goes out as an EV_RPS (and to anyone joining: rpsEvent_).
    struct Rps {
        uint32_t in = 0, out = 0;           // players in the tiebreak / knocked out of it (bit per id)
        uint8_t pick[kMaxPlayers] = {};     // RpsShape this throw (RPS_NONE: not yet)
        uint8_t wins[kMaxPlayers] = {};     // best of 3, once two are left
        bool bestOf3 = false;
        int stage = RPS_PICK, throwNo = 0, winner = -1;
        double stageEnd = 0;
        std::string result;                 // what the last reveal did ("draw - throw again", ...)
    } rps_;
    GameEvent rpsEvent_;
    uint32_t nextJoinId_ = 1;   // ServerPlayer::joinId of the next connection (a newcomer in a reused slot isn't its old owner)
    float drainAccum_ = 0;
    uint32_t duelUsed_ = 0;      // duel: modifiers already played this match (bit per modifier), never repeated
    // payload/race per-round bookkeeping
    double payloadIdleSince_ = 0;
    float bestProgress_[2] = {0, 0};
    float completeTime_[2] = {0, 0};
    float kothPoints_[2] = {0, 0};
    double bombDroppedAt_ = 0;   // bomb: when it was dropped (it goes back to an attacker if nobody picks it up)
    double objTouch_ = -1e9;     // payload / multi point / bomb / neutral payload: when the side that needs time last touched the objective (overtime)

    BotBrain bots_[kMaxPlayers];
    NavGraph nav_;
    int addBot(int team, int skill, bool balance = false);
    void removeBot(int id);
    int humanCount(int team = -1) const;
    void updateBalanceBots(bool matchStarting = false);
    // Votes (bots on/off, bot difficulty): majority of human players decides.
    // VOTE_SPEC: move a player (arg; arg2 = their joinId) to spectator until the next map. Never a kick: kicking stays
    // with the dedicated server's console and the host (isAdmin), and isn't votable (OWNER_DECISIONS_2 B10).
    enum VoteKind { VOTE_NONE = 0, VOTE_BOTS, VOTE_BOTSKILL, VOTE_START, VOTE_STARTBOTS, VOTE_MAP, VOTE_TMAPS, VOTE_TMODES, VOTE_RANKED, VOTE_EDIT, VOTE_LETIN, VOTE_SPEC };
    static constexpr int kSpecVoteMinPlayers = 3;   // connected human players a spectate vote needs (so never in a 1v1)
    struct Vote { int kind = VOTE_NONE; int caller = -1; int arg = 0, arg2 = 0; std::string map; uint32_t yes = 0, no = 0; double endsAt = 0; std::string text; } vote_;
    void callVote(int caller, int kind, int arg, const std::string& map = "", int arg2 = 0);
    // Tourney: single elimination bracket of everyone in the server, one 1v1 at a time.
    struct Tourney {
        bool running = false;
        std::vector<std::vector<int>> rounds; // rounds[r]: entrants (player id, -1 bye, -2 not decided yet)
        int round = 0, match = 0;             // current match: rounds[round][2*match] vs [2*match+1]
        int played = 0;                       // matches played (map / ruleset rotation)
        uint32_t join[kMaxPlayers] = {};      // each entrant's joinId: a newcomer in a departed entrant's slot isn't them
    } tourney_;
    void broadcastBracket();
    bool switchMapKeepRules(const std::string& name); // change map for the next tourney match
    void castVote(int id, bool yes);
    void tickVote();
    double nextBalanceCheck_ = 0;

    void broadcastEvent(const GameEvent& e);
    void sendEvent(int player, const GameEvent& e);
    void message(const std::string& text);
    void spawnPlayer(int id);
    void autoTeam(int id);      // a newcomer's team (or spectator): mode, seat limits, balance
    void killPlayer(int victim, int killer, int weapon);
    void useLocator(int id);    // hide and seek / tag: "it" fired the locator (WPN_LOCATOR)
    void damagePlayer(int victim, int attacker, float dmg, int weapon, const Vec3& knock);
    int aliveCount(int team) const;
    int teamCount(int team) const;
    void log(const std::string& s);
    size_t snapBytes_ = 0, snapCount_ = 0; // snapshot bytes sent (stats for tests / logs)
    float hitAccum_[kMaxPlayers][kMaxPlayers] = {};
    double hitFlushAt_[kMaxPlayers][kMaxPlayers] = {};
    bool hitHead_[kMaxPlayers][kMaxPlayers] = {};   // the batched hit is a headshot (EV_HIT text "h": a red number)
    bool hitPending_ = false;
    void flushHit(int attacker, int victim);
    void bounceShot(int id, Vec3 o, Vec3 dir, float range, uint64_t vt, float* dmgPer, float dmg);
    // Map edit sessions: everyone flies around and edits the map together; edits go through here so all
    // copies stay identical. The map file is autosaved (it's the host's copy) while dirty.
    bool editing_ = false, editDirty_ = false;
    bool tempEdit_ = false;     // editing was voted on a normal server: never saved, reverts at the next map change
    bool mapEdited_ = false;    // map_ differs from its file (temporary edits being played)
    bool autosaveBackedUp_ = false; // this edit session's first autosave copied the file to <name>.qmap.bak
    void playMap(int mode);
    double nextAutosave_ = 0;
    void setEditing(bool on);
    // What votes and ranked need to know about a map file, without parsing it on the tick (REVIEW NET-6): read in
    // the background when the server starts, re-read only when the file changes (size or time).
    struct MapInfo { int64_t stamp = 0; bool ok = false; uint32_t supports = 0; uint16_t ranked = 0; };
    MapInfo mapInfo(const std::string& name);
    uint32_t mapInfoLoads_ = 0;  // map files actually parsed for it (tests)
    size_t mapInfoCached() { std::lock_guard<std::mutex> lk(mapInfoMu_); return mapInfo_.size(); }   // (tests)
    void stopMapInfo();
    void setRanked(bool on, int ladder);            // ranked on (1v1 on that ladder, official map) / off
    bool rankedBlocks(int id, const std::string& what); // tells the player and returns true if ranked forbids it
    bool balanceBotsUnranked_ = true;                // balance bots setting to go back to when ranked is voted off
    std::string listName() const;                    // server name for the browser ("[RANKED ...]" prefixed)
    // Updates: restartFlag appeared -> restart (exit) as soon as no match is in progress, telling players first
    bool restartRequested() const { return restartNow_; }
    bool matchInProgress() const;
    bool restartNow_ = false, restartAnnounced_ = false;
    // Ranked here is rated: not a testing server (--channel testing) and not a testing build (a -test. version).
    // Otherwise ranked plays normally (queue, lock, everything) but no ticket, result or race run leaves the server.
    bool rated() const;
    // Load governor (cfg_.governorDir): its thread decides; the tick thread reports a few times a second and applies
    // the cap (governorFrame). closed_: a testing server the governor closed (no ticks, connects refused, unlisted).
    std::unique_ptr<Governor> gov_;
    bool closed_ = false;
    double govReportAt_ = 0;
    bool govPendingLogged_ = false;
    std::string govLog_;
    bool windowBad_ = false, windowSevere_ = false;   // the last rate window checked (and when): what the governor sees
    double windowCheckedAt_ = -1e9;
    void governorFrame(double now);
    bool applyGovernorCap(bool between);
    void setGovernorClosed(bool on);
    double nextFlagCheck_ = 0, restartAt_ = 0;
    void checkRestartFlag();
    std::string rankedResult(int winner, std::string* whyNot); // EV_RANKED text for the match that just ended
    // Match tickets (tools/master/worker.js /ticket): a ranked match is rated only under a ticket this server opened
    // with its list token when the match went live, binding the match id, players, ladder and map. Time trials get a
    // race ticket for the racer, and this server (not the racer's game) posts each finished run. All HTTP runs on
    // the MasterAnnouncer thread; replies are picked up in run().
    std::string rankedMatch_, raceTicket_, raceTicketFor_;
    // the race ticket goes to the Worker once the racer's game has signed its id (like a match ticket): the Worker
    // takes runs (and the racer's name) only for a player who is really on this server
    bool raceTicketSent_ = false;
    void sendRaceTicket(const std::string& pub, const std::string& sig, const std::string& addr);
    // While a ranked match is live (ticket opened at the first round going live, until the match ends) nothing may
    // change it: no votes, no map / mode / ranked changes, no team switches, no kicking its players (OWNER 3.4).
    // Exactly the three rated ladders are locked (JUDGMENT_CALLS C15): 1v1 vanilla, 1v1 laser vampire and instagib free
    // for all. The Viaduct time trial (best times only, no rating), tourneys and everything else never are.
    bool rankedLadderPlayed() const { return cfg_.ranked && arenaVariantRanked(cfg_.arenaVariant) && cfg_.mode == rankedMode(cfg_.arenaVariant) && !cfg_.timeTrial; }
    bool rankedLive() const { return rankedLadderPlayed() && !rankedMatch_.empty() && ms_.phase != PHASE_WARMUP && ms_.phase != PHASE_MATCH_END; }
    bool rankedTicketSent_ = false;   // the ticket went out (or was given up on): no more signatures needed
    double rankedSigDeadline_ = 0;    // players must sign the match id by then, else the match isn't rated
    void maybeSendRankedTicket(bool giveUp = false);
    // Players who left the live ranked match (gone past the reconnect grace), first leaver first. The first leaver
    // takes the loss (a ranked 1v1 ends there); whoever stays and leaves later isn't punished.
    struct RankedLeaver { int slot; std::string rankId; int team, score, kills; };
    std::vector<RankedLeaver> rankedLeft_;
    bool isRankedParticipant(int id) const;
    // The player "kick <who>" means: a connected slot's id (all digits), else an exact name (any case), else the
    // start of exactly one name. -1 and *err when there's none or several (REVIEW 4.1).
    int playerFromArg(const std::string& who, std::string* err) const;
    // Tourney seeding: the average of each entrant's three ladder ratings (kUnratedSeed for a ladder without games,
    // or no ranked id), fetched in the background (never on the tick) whenever someone with a ranked id joins a
    // tourney server or one starts; a rating still on its way counts as unrated.
    std::vector<int> tourneySeeding(const std::vector<int>& entrants); // the first round (rounds[0]) for these players
    int seedRating(int id);
    void prefetchSeedRatings();
    std::unique_ptr<RankService> seedRatings_;
    std::map<std::string, int> seedFixed_;   // tests: ranked id -> seed rating (instead of asking the Worker)
    std::vector<int> raceFloors_;
    void openRankedTicket();
    void openRaceTicket();
    void finishRaceRun(int id);
    void postRankedResult(const std::string& eventText);
    bool timeTrialCounts() const;
    void ensureMaster();
    // Is `dialled` (the address a connecting client signed, REVIEW 2.2) this server? See server.cpp.
    bool ownAddress(const NetAddr& dialled, const NetAddr& from);
    struct PublicIpLookup { std::atomic<uint32_t> ip{0}; std::atomic<bool> quit{false}; };
    std::shared_ptr<PublicIpLookup> publicIp_;   // looked up on a detached thread at start (stop() never waits for it)
    std::vector<NetAddr> ownAddrs_;              // cfg_.ownAddrs, resolved at start
    std::vector<uint32_t> localIps_;             // this machine's interface addresses (re-read on a miss, at most every 10 s)
    double localIpsAt_ = -1e9;
    void handleMasterReply(const std::string& key, const std::string& reply);
    bool applyEdit(int from, const std::string& op);
    // Saves the edited map now, on this thread (stop, "editmode saveas / off": they need the answer), after any
    // background save still running. The periodic autosave runs in the background instead (startAutosave).
    bool autosave();
    // Edit-session autosave off the tick thread: the tick only copies the map into a buffer kept warm between saves
    // (~0.03 ms on Midtown); serializing (~10 ms) and the parse check (~2.5 ms) run on a detached thread, and the
    // tick picks up the result (and the text, for map downloads, if nothing was edited meanwhile).
    struct AutosaveJob {
        std::mutex mu;
        std::condition_variable cv;
        bool running = false, finished = false, ok = false, backedUp = false;
        Map map;                // the map as it was when the save started (its vectors keep their capacity)
        std::string path, text, err;
        MapId id;   // the map it saves (still the map's: nothing edited since)
    };
    std::shared_ptr<AutosaveJob> autosaveJob_ = std::make_shared<AutosaveJob>();
    void startAutosave();
    void pollAutosave();        // a finished background save: its result
    void waitAutosave();        // until no background save is running
    bool autosaveRunning() { std::lock_guard<std::mutex> l(autosaveJob_->mu); return autosaveJob_->running; }
    // map edits: this tick's time spent on them, and whether some player is too far behind on reliable events
    uint32_t editTick_ = 0;
    double editSpentMs_ = 0;
    bool editBacklogged() const;
    void handleStringCmdForTest(int id, const std::string& s) { handleStringCmd(id, s); }
    bool fireWeaponForTest(int id, const UserCmd& c) { return fireWeapon(id, c); }
    void tickProjectilesForTest() { tickProjectiles(); }
    void sendSnapshotsForTest() { sendSnapshots(); }   // (benchmarks: tests/server_tests.h snapbench)
    void glog(int level, const char* fmt, ...);   // game log file (no-op below cfg_.logLevel)
    float bodyScale() const { return duelModDef(duelMod()).size; } // duel "double size": hull, hitbox and eye height (MOVE_BIG / MOVE_TINY)
    int duelMod() const { return ms_.mode == MODE_DUEL ? ms_.duelMod : 255; } // 255 in warmup (rulesReset clears it)
    // Weapons allowed right now (bit per weapon, 0 = all) and the 1v1 ruleset's AR_* rules.
    uint16_t weaponMask() const;
    bool headHit_[kMaxPlayers] = {};   // this shot hit that player in the head (for the kill event)
    bool shotHit(int id) { bool h = players_[id].hitSinceShot; players_[id].hitSinceShot = false; return h; } // and reset for the next shot
    uint8_t arenaRules() const {
        uint8_t r = ms_.mode == MODE_ARENA || ms_.mode == MODE_TOURNEY || (cfg_.ranked && ms_.mode == MODE_FFA) ? arenaVariant(ms_.arenaVariant).rules : duelModDef(duelMod()).rules;
        const CustomRules& c = cfg_.custom;
        if (c.active()) r |= (c.instagib ? AR_INSTAGIB : 0) | (c.vampire ? AR_VAMPIRE : 0) | (c.regen ? AR_REGEN : 0) | (!c.selfDamage ? AR_NO_SELF_DAMAGE : 0);
        return r;
    }
    float maxHealth() const { return cfg_.custom.active() ? (float)cfg_.custom.health : (float)kMaxHealth; }
    // This life's full health (what you spawn with): a duel life's ("1 HP", "glass cannons" 50, "tanks" 250), else
    // maxHealth(). Heals (syringe, vampire, regeneration) stop here (REVIEW GAME-2a: they capped at maxHealth()).
    float lifeMaxHealth() const { float h = duelModDef(duelMod()).health; return h == kMaxHealth ? maxHealth() : h; }
    void sendRules(int id);     // a custom mode's rules to one player (they set them before playing)
    bool weaponAllowed(int w) const { uint16_t m = weaponMask(); return !m || (m >> w & 1); }
    AABB hitBox(const Vec3& feet, bool crouched) const; // damage hull (scaled by the duel modifier's size)

private:
    std::atomic<uint32_t> hostNonce_{0};
    // reachability probe: host side
    std::atomic<int> reach_{REACH_OFF};
    std::atomic<bool> reachAgain_{false};
    std::mutex reachMu_;
    std::string reachDetail_;
    std::string ownAddrHint_;   // (under reachMu_)
    // Picking a prober from the internet list (blocking HTTP) runs on a detached thread that writes only this
    // shared state, so stop() never waits for it (FINAL_REVIEW 2.10).
    struct ProberPick { std::mutex mu; NetAddr prober; std::atomic<bool> picked{false}; };
    std::shared_ptr<ProberPick> reachPick_;
    UdpSocket probeSock_;                     // separate ephemeral socket: no NAT mapping toward the prober for the game port
    uint32_t probeNonce_ = 0;
    double probeSentAt_ = 0, probeNextAt_ = 0;
    int probeTries_ = 0;
    double probeStartedAt_ = 0;
    bool upnpWasDone_ = false;
    void tickReachability();
    void startProberPick();
    void sendProbe(const NetAddr& prober);
    void setReach(Reach r, const std::string& detail);
    // prober side: per source IP rate limit (fixed table, no per-request allocation)
    struct ProbeSlot { uint32_t ip = 0; double at = -1e9; };
    ProbeSlot probeSlots_[64];
    double probeWindowAt_ = 0; int probeWindowCount_ = 0;
public:
    void handleProbeRequest(const NetAddr& from, Reader& r, int len); // (public: tests feed it spoofed sources)
private:
    uint8_t cookieKey_[16] = {};              // secret for the stateless connect challenge
    std::vector<std::string> mapList_;        // maps on disk (cached at start: votes check names against it)
    uint32_t challengeCookie(const NetAddr& a, uint32_t nonce, int64_t bucket) const;
    bool isAdmin(int id) const;
    std::map<std::string, MapInfo> mapInfo_;
    std::mutex mapInfoMu_;
    std::thread mapInfoThread_;
    std::atomic<bool> mapInfoQuit_{false};
    static int64_t mapStamp(const std::string& name);
    static MapInfo readMapInfo(const std::string& name, int64_t stamp);
    void handleConnect(const NetAddr& from, Reader& r, int len);
    void handleInput(int id, Reader& r);
    void handleStringCmd(int id, const std::string& s);
    // Every drop of a live ranked participant counts as leaving the match (the first leaver loses), whatever the path:
    // the reconnect grace running out, a new session from its address, the unacked-event cap. Only the server
    // stopping (shutdown) doesn't (REVIEW 2.1).
    void dropPlayer(int id, const std::string& reason, bool notify, bool shutdown = false);
    void holdPlayer(int id, const std::string& reason);   // connection lost: keep the slot for kReconnectGrace
    bool restorePlayer(int id, const NetAddr& from, uint32_t nonce, const std::string& rankPub, const NetAddr& dialled = NetAddr{});
    void sendWelcome(int id);   // what a (re)joining client needs to know about everyone
    float verifyTokens_ = kVerifyBurst; // connect signature checks (kVerifyPerSec; a flood waits and retries)
    double verifyAt_ = 0;
    void simulate();
    void drainCmds(int id);
    void checkTickRate();
    void stepRateDown(const char* why);
    void resetRateWindow();
    void runPlayerCmd(int id, const UserCmd& cmd);
    // The grapple latched onto a player: the candidates a hook fired in this command can catch (where the shooter saw
    // them), the catch, keeping both anchors on the bodies, and letting go (release, meeting, break free, death...).
    void grappleTargets(int id, const UserCmd& cmd, HookProbe& probe);
    void startLatch(int attacker, int victim, bool head);
    bool latchHolds(int attacker) const;
    void syncLatch(int attacker);
    void endLatch(int attacker);
    void tickLatches();
    uint64_t rewindViewMs(int id, uint32_t viewTick) const;   // the view time lag compensation tests at (capped)
    Vec3 rewoundPos(int i, uint64_t viewMs, bool* crouched) const;   // where player i was at that view time
    bool fireWeapon(int id, const UserCmd& cmd);   // true if it hit a player (hitscan weapons)
    void tickProjectiles();
    void stepProjectile(Projectile& pr);   // one fixed 1 ms step (kMoveStep): gravity, move, hit a wall / player
    int projTargets_ = 0;                  // tickProjectiles: the live players' hitboxes this tick
    uint8_t projTargetId_[kMaxPlayers] = {};
    AABB projTargetBox_[kMaxPlayers];
    void explode(const Vec3& at, int owner, int weapon, int direct = -1);   // direct: the player a rocket hit itself
    void hitscan(int id, const Vec3& eye, const Vec3& dir, float range, uint64_t viewMs, float* dmgPerPlayer, float dmg, float headDmg = 0, float grow = 0, bool pierce = false);
    void sendSnapshots();
    void updateVisibility();
    uint32_t visCursor_ = 0;   // the viewer x target pair the next tick's checks start from (round robin)
    uint32_t projVisCursor_ = 0;   // the same for viewer x projectile pairs
    // Hidden information (NET-10, JUDGMENT_CALLS C12). Each client is sent what it could see: through its own eyes when
    // alive; when dead, through the eyes of the player it watches (and in team modes its living teammates'); everything
    // as a free spectator. Players it can't see go without position, health, weapon, firing or the bomb; projectiles
    // it can't see aren't sent (its own always are); the hide and seek seeker gets nothing about the hiders in the freeze.
    uint32_t perspectives(int id) const;   // the players whose sight a client gets (bit per player), 0 = sees everything
    void updatePlayerVisibility(uint32_t need, int viewers, int targets, int& budget, float eyeH);
    void updateProjectileVisibility(uint32_t need, int viewers, int& budget, float eyeH);
    uint32_t viewNeeded_ = 0;              // alive players whose lines of sight are checked (humans, and bots someone watches)
    uint16_t projVisId_[kMaxProjectiles] = {};   // the projectile each slot's ServerPlayer::projSeen times are about
    uint32_t projShown_[kMaxProjectiles] = {};   // clients (bit each) that have been sent that projectile in sight
    uint32_t projHidden_[kMaxProjectiles] = {};  // clients whose last snapshot had it out of sight (or not at all)
    // A projectile that dies out of a client's sight is shown there for a moment at the spot (then gone): the client
    // plays its explosion (sound and flash) where it really went off, as before, without having seen its flight.
    struct ProjSlotPrev { bool active = false; uint16_t id = 0; uint8_t type = 0, owner = 0; Vec3 pos; } projPrev_[kMaxProjectiles];
    struct ProjDeath { ProjectileSnap snap; double until = 0; uint32_t revealTo = 0; };
    ProjDeath projDeaths_[kMaxProjectiles];
    int projDeathCount_ = 0;
    std::vector<int> snapProjSlot_;        // snapProj_[k]'s projectile slot
public:
    uint64_t visRays_ = 0;     // lines of sight cast (tests, benchmarks)
    void updateVisibilityForTest() { updateVisibility(); }
private:
    void openGameLog();
    void logTick();
    int findPlayer(const NetAddr& a) const;
    std::string uniqueName(std::string name, int self) const;

    UdpSocket sock_;
    const GameEvent* eventPtrs_[16] = {}; // scratch for snapshot building: the events going out (in the player's queue)
    bool running_ = false;
    double nextTick_ = 0;
    double rateWindowStart_ = 0;
    int windowTicks_ = 0, windowLate_ = 0, windowSendFail_ = 0, windowWorstLoss_ = 0;
    double windowBusy_ = 0;   // seconds spent in simulate + snapshots this window
    LogFn log_;
    std::unique_ptr<PortMapper> upnp_;
    std::unique_ptr<MasterAnnouncer> master_;
    FILE* glog_ = nullptr;
};
