#include "gamemodes.h"
#include "../common/rng.h"
#include "server.h"
#include <cstdlib>

namespace {
constexpr float kFreezeTime = 3.0f;
constexpr float kRoundEndTime = 4.0f;
constexpr float kDuelRoundEndTime = 2.5f;   // 1v1, duel and tourney: short rounds, a shorter break (T22: 4 s)
constexpr float kMatchEndTime = 10.0f;
constexpr float kWarmupCountdown = 5.0f;
// VS
constexpr float kStalemateTime = 30.0f;
constexpr float kDrainPerSecond = 5.0f;
// BOMB
constexpr float kPlantTime = 3.0f;
constexpr float kDefuseTime = 6.0f;
constexpr float kBombFuse = 40.0f;
constexpr float kBombRadius = 18.0f;
constexpr float kDefuseRange = 1.8f;
constexpr float kPickupRange = 1.2f;
constexpr float kBombReturn = 15.0f;   // a dropped bomb nobody picks up goes back to an attacker after this
// PAYLOAD
constexpr float kCartRange = 4.0f;
constexpr float kCartSpeed = 1.6f;
constexpr float kCartRollback = 0.6f;
constexpr float kCartIdleBeforeRollback = 10.0f;
constexpr float kCartCheckBonus = 60.0f;   // time added when the cart reaches a checkpoint (it can't roll back past one)
// MULTI POINT
constexpr float kPointRoundTime = 150.0f;  // the attackers' clock at the start
constexpr float kPointBonus = 90.0f;       // added for every point taken
constexpr float kCaptureTime = 12.0f;      // one attacker alone; more go faster (up to twice as fast)
constexpr float kCaptureDecay = 0.05f;     // a point the attackers leave loses this much capture a second
// OVERTIME (every mode with a site or a cart): when the clock runs out, the round goes on while the side that needs
// time is on the objective, or touched it in the last kOvertime seconds; it ends kOvertime s after their last touch.
constexpr float kOvertime = 5.0f;

int roundsToWin(const Server& s) {
    if (s.cfg_.roundsToWin > 0) return s.cfg_.roundsToWin;
    switch (s.ms_.mode) {
        case MODE_VS: return 5;
        case MODE_DUEL: return 11; // first to 11 lives
        case MODE_ARENA: return 5;
        case MODE_TOURNEY: return 3; // best of 5 per match
        case MODE_BOMB: return 7;
        case MODE_RACE: return 3;
        case MODE_PAYLOAD:
        case MODE_POINTS: return 1; // each team attacks once
        default: return 1;
    }
}

float roundTime(const Server& s) {
    switch (s.ms_.mode) {
        case MODE_BOMB: return 105.0f;
        case MODE_RACE: return 300.0f; // two loops of Viaduct
        case MODE_PAYLOAD: return 240.0f;
        case MODE_POINTS: return kPointRoundTime;
        case MODE_NPAYLOAD: return 300.0f;
        case MODE_FFA: if (s.cfg_.ranked) return kRankedFfaTime;
            return s.cfg_.timeLimit > 0 ? (float)s.cfg_.timeLimit : 600.0f;
        case MODE_HIDE: return kHideRoundTime;
        case MODE_TAG: return kTagRoundTime;
        default: return 0.0f; // no limit
    }
}

int other(int t) { return t == TEAM_A ? TEAM_B : TEAM_A; }
int defenders(const Server& s) { return other(s.ms_.attackers); }

// The clock has run out (call only then): true while overtime holds the round open (the objective was touched in
// the last kOvertime seconds), with the overtime countdown in timeLeft. False: time's up for real.
bool overtime(Server& s) {
    ModeState& m = s.ms_;
    double left = s.objTouch_ + kOvertime - s.time();
    if (left <= 0) { m.flags &= ~MS_OVERTIME; return false; }
    if (!(m.flags & MS_OVERTIME)) { s.message("overtime!"); s.glog(LOG_SIMPLE, "overtime"); }
    m.flags |= MS_OVERTIME;
    m.timeLeft = (float)left;
    return true;
}

void setPhase(Server& s, Phase p, float duration) {
    const int old = s.ms_.phase;
    // the match is over, or a new one starts (out of warmup or the end screen): stuck knives / bolts / syringes go
    if (p != old && (p == PHASE_WARMUP || p == PHASE_MATCH_END || old == PHASE_WARMUP || old == PHASE_MATCH_END)) s.clearProps();
    s.ms_.phase = p;
    s.phaseEnd_ = duration > 0 ? s.time() + duration : 0;
    if (p == PHASE_LIVE) {
        s.liveSince_ = s.time();
        if (s.cfg_.ranked) s.openRankedTicket();   // (once per match: the first round going live)
        if (s.cfg_.timeTrial) s.openRaceTicket();
    }
}

void enterMatchEnd(Server& s, const std::string& text, int winner) {
    s.ms_.flags &= ~MS_RPS;   // (a hide and seek tiebreak is over too)
    GameEvent e;
    e.type = EV_ROUND_WIN;
    e.a = (uint8_t)(winner < 0 ? 255 : winner);
    e.b = 1; // match over
    e.text = text;
    s.broadcastEvent(e);
    s.log(text);
    s.glog(LOG_SIMPLE, "match end: %s", text.c_str());
    if (s.cfg_.ranked) {
        std::string why, res = s.rankedResult(winner, &why);
        if (res.empty()) s.message("not rated: " + why);
        else if (!s.rated()) {   // a testing server: no EV_RANKED (players' games would report it), no result posted
            s.message(std::string(kUnratedNotice) + ": the result isn't sent to the ranking server");
            s.glog(LOG_SIMPLE, "ranked (not rated: testing server) %s", res.c_str());
        } else {
            GameEvent r; r.type = EV_RANKED; r.text = res; s.broadcastEvent(r); s.glog(LOG_SIMPLE, "ranked %s", res.c_str());
            // the server's own account goes to the Worker under the ticket; players' reports must agree with it
            s.postRankedResult(res);
        }
        s.rankedMatch_.clear(); // the next match gets a new ticket
    }
    setPhase(s, PHASE_MATCH_END, kMatchEndTime);
}

// ---- hide and seek / tag ----
bool inPlay(const Server& s, int i) { const auto& p = s.players_[i]; return p.connected && p.team != TEAM_SPECTATOR && !(s.ms_.mode == MODE_TAG && p.out); }
int playersInPlay(const Server& s) { int n = 0; for (int i = 0; i < kMaxPlayers; i++) n += inPlay(s, i); return n; }

// Hide and seek: everyone takes a turn seeking, in slot order, each once a match (someone who joins late gets a
// turn too). Tag: a random player still in.
void pickIt(Server& s) {
    for (auto& p : s.players_) p.it = false;
    std::vector<int> c;
    for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i) && !(s.ms_.mode == MODE_HIDE && s.players_[i].sought)) c.push_back(i);
    if (c.empty()) return;
    int pick = c[qrand() % c.size()];
    if (s.ms_.mode == MODE_HIDE) {
        pick = c[0];
        for (int i : c) if (i > s.lastIt_) { pick = i; break; }
        s.players_[pick].sought = true;
    }
    s.lastIt_ = pick;
    s.players_[pick].it = true;
}

// A round result with no team score (the scores are per player in these modes).
void endRoundText(Server& s, const std::string& text) {
    GameEvent e;
    e.type = EV_ROUND_WIN;
    e.a = 255;
    e.text = text;
    s.broadcastEvent(e);
    s.log(text);
    s.glog(LOG_SIMPLE, "round %d end: %s", s.ms_.round, text.c_str());
    setPhase(s, PHASE_ROUND_END, kRoundEndTime);
}

int bestScorer(const Server& s) {
    int best = -1;
    for (int i = 0; i < kMaxPlayers; i++) {
        const auto& p = s.players_[i];
        if (!p.connected || p.team == TEAM_SPECTATOR) continue;
        if (best < 0 || p.score > s.players_[best].score) best = i;
    }
    return best;
}

// Hide and seek: one round per player in play, each seeking once (was clamp(players, 3, 6) in slot order: with 7+
// players some never sought, with 2 someone sought twice). The match ends once nobody in play is left to seek.
int hidersYetToSeek(const Server& s) { int n = 0; for (int i = 0; i < kMaxPlayers; i++) n += inPlay(s, i) && !s.players_[i].sought; return n; }

// Hide and seek scoring (owner B8): a hider still unfound when the round ends scores 1; the seeker scores 1 for
// each hider found (rulesOnDeath); a found hider scores nothing, and neither does a fall (nor the seeker for it).
// Every player plays the same rounds (one seeking, the rest hiding).
void payUnfound(Server& s) { for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i) && !s.players_[i].it && s.players_[i].alive) s.players_[i].score += 1; }

void tickHide(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    int seeker = -1, hiding = 0, found = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!inPlay(s, i)) continue;
        if (s.players_[i].it) seeker = i;
        else if (s.players_[i].alive) { hiding++; s.players_[i].hideSurvived += s.dt(); }
        else found += s.players_[i].found;
    }
    if (seeker < 0) { payUnfound(s); endRoundText(s, "the seeker left"); return; }   // (still unfound when the round ends)
    if (hiding == 0) {                            // nobody left hiding (each find was scored as it happened)
        endRoundText(s, found > 0 ? s.players_[seeker].name + " found everyone" : "no hiders left");
        return;
    }
    if (s.time() >= s.phaseEnd_) {
        std::string who;
        for (int i = 0; i < kMaxPlayers; i++)
            if (inPlay(s, i) && !s.players_[i].it && s.players_[i].alive) who += (who.empty() ? "" : ", ") + s.players_[i].name;
        payUnfound(s);
        endRoundText(s, "the hiders win - " + who + " stayed hidden");
    }
}

// ---- hide and seek tiebreak: rock paper scissors (owner B8) ----
bool rpsActive(const Server& s, int i) { return (s.rps_.in >> i & 1) && !(s.rps_.out >> i & 1); }
int rpsLeft(const Server& s) { int n = 0; for (int i = 0; i < kMaxPlayers; i++) n += rpsActive(s, i); return n; }

// Everyone sees the same state; picks stay hidden (RPS_PICKED) until the reveal.
void rpsBroadcast(Server& s) {
    const auto& r = s.rps_;
    GameEvent e;
    e.type = EV_RPS;
    e.a = (uint8_t)r.stage;
    e.b = (uint8_t)std::min(255, r.throwNo);
    e.text = std::to_string(r.bestOf3 ? 1 : 0) + " " + std::to_string(r.winner);
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!(r.in >> i & 1)) continue;
        int shape = r.pick[i];
        if (r.stage == RPS_PICK && shape != RPS_NONE) shape = RPS_PICKED;
        e.text += " " + std::to_string(i) + ":" + std::to_string(shape) + ":" + std::to_string(r.wins[i]) + ":" + std::to_string((r.out >> i & 1) ? 1 : 0);
    }
    e.text += "\n" + r.result;
    s.rpsEvent_ = e;
    s.broadcastEvent(e);
}

void rpsNewThrow(Server& s) {
    auto& r = s.rps_;
    r.stage = RPS_PICK;
    r.throwNo++;
    r.stageEnd = s.time() + kRpsPickTime;
    s.phaseEnd_ = r.stageEnd;
    for (auto& p : r.pick) p = RPS_NONE;
    for (int i = 0; i < kMaxPlayers; i++)   // bots throw at random
        if (rpsActive(s, i) && s.players_[i].isBot) r.pick[i] = (uint8_t)(RPS_ROCK + qrand() % 3);
    rpsBroadcast(s);
}

// After the last round: true (and the tiebreak is on) when two or more players share the top score and the most
// time survived hiding (T23: that decides first; rock paper scissors decided 4 of 6 bot matches, two of them 5-way
// ties after 15-20 min). *winner: a tie that time settled, else -1.
bool rpsStartIfTied(Server& s, int* winner) {
    *winner = -1;
    int best = bestScorer(s);
    if (best < 0) return false;
    uint32_t tied = 0;
    int n = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        const auto& p = s.players_[i];
        if (p.connected && p.team != TEAM_SPECTATOR && p.score == s.players_[best].score) { tied |= 1u << i; n++; }
    }
    if (n < 2) return false;
    {   // the longest hidden among them goes on (a tick's worth apart counts as equal)
        double most = 0;
        for (int i = 0; i < kMaxPlayers; i++) if (tied >> i & 1) most = std::max(most, s.players_[i].hideSurvived);
        uint32_t still = 0; int m = 0;
        for (int i = 0; i < kMaxPlayers; i++)
            if ((tied >> i & 1) && s.players_[i].hideSurvived >= most - 0.5 * s.dt()) { still |= 1u << i; m++; }
        if (m == 1) {
            for (int i = 0; i < kMaxPlayers; i++) if (still >> i & 1) *winner = i;
            s.message("tied for first - " + s.players_[*winner].name + " stayed hidden longest (" + std::to_string((int)most) + " s)");
            return false;
        }
        tied = still; n = m;
    }
    s.rps_ = Server::Rps();
    s.rps_.in = tied;
    s.rps_.bestOf3 = n == 2;
    s.ms_.flags |= MS_RPS;
    std::string who;
    for (int i = 0; i < kMaxPlayers; i++) if (tied >> i & 1) who += (who.empty() ? "" : ", ") + s.players_[i].name;
    s.message("tied for first: " + who + " - rock paper scissors decides it");
    s.glog(LOG_SIMPLE, "rps tiebreak: %s", who.c_str());
    rpsNewThrow(s);
    return true;
}

void rpsReveal(Server& s) {
    auto& r = s.rps_;
    for (int i = 0; i < kMaxPlayers; i++)   // no pick in time: a random one
        if (rpsActive(s, i) && r.pick[i] == RPS_NONE) r.pick[i] = (uint8_t)(RPS_ROCK + qrand() % 3);
    std::vector<int> ids;
    for (int i = 0; i < kMaxPlayers; i++) if (rpsActive(s, i)) ids.push_back(i);
    if (ids.size() == 2 && r.bestOf3) {
        int a = ids[0], b = ids[1];
        if (r.pick[a] == r.pick[b]) r.result = std::string("draw (") + rpsName(r.pick[a]) + ") - throw again";
        else {
            int w = rpsBeats(r.pick[a], r.pick[b]) ? a : b, l = w == a ? b : a;
            r.wins[w]++;
            r.result = s.players_[w].name + "'s " + rpsName(r.pick[w]) + " beats " + rpsName(r.pick[l]) + " - " +
                       std::to_string(r.wins[w]) + "-" + std::to_string(r.wins[l]);
            if (r.wins[w] >= 2) r.winner = w;
        }
    } else {
        uint8_t picks[kMaxPlayers];
        for (size_t k = 0; k < ids.size(); k++) picks[k] = r.pick[ids[k]];
        int lose = rpsLosingShape(picks, (int)ids.size());
        if (lose == RPS_NONE) r.result = "nobody is out - throw again";
        else {
            std::string outs;
            for (int i : ids) if (r.pick[i] == lose) { r.out |= 1u << i; outs += (outs.empty() ? "" : ", ") + s.players_[i].name; }
            r.result = outs + " out (" + rpsName(lose) + ")";
            int left = rpsLeft(s);
            if (left == 1) for (int i : ids) if (rpsActive(s, i)) r.winner = i;
            if (left == 2) { r.bestOf3 = true; r.result += " - best of 3 between the last two"; }
        }
    }
    r.stage = RPS_REVEAL;
    r.stageEnd = s.time() + kRpsRevealTime;
    s.phaseEnd_ = r.stageEnd;
    s.message("rock paper scissors: " + r.result);
    rpsBroadcast(s);
}

void tickRps(Server& s) {
    auto& r = s.rps_;
    ModeState& m = s.ms_;
    for (int i = 0; i < kMaxPlayers; i++)   // left, or went to spectate: out of it
        if (rpsActive(s, i) && (!s.players_[i].connected || s.players_[i].team == TEAM_SPECTATOR)) r.out |= 1u << i;
    if (r.winner < 0 && rpsLeft(s) <= 1) {
        for (int i = 0; i < kMaxPlayers; i++) if (rpsActive(s, i)) r.winner = i;
        if (r.winner < 0) { m.flags &= ~MS_RPS; enterMatchEnd(s, "match over", -1); return; }
        r.stageEnd = s.time();   // (the last one left wins now)
    }
    if (r.winner < 0 && rpsLeft(s) == 2 && !r.bestOf3) { r.bestOf3 = true; for (auto& w : r.wins) w = 0; }
    m.timeLeft = (float)std::max(0.0, r.stageEnd - s.time());
    if (r.stage == RPS_PICK) {
        bool all = true;
        for (int i = 0; i < kMaxPlayers; i++) if (rpsActive(s, i) && r.pick[i] == RPS_NONE) all = false;
        if (s.time() < r.stageEnd && !all) return;
        if (r.winner < 0) { rpsReveal(s); return; }
    } else if (s.time() < r.stageEnd) return;
    if (r.winner >= 0) {   // decided: the tiebreak's winner takes the match
        r.stage = RPS_DONE;
        rpsBroadcast(s);
        m.flags &= ~MS_RPS;
        enterMatchEnd(s, s.players_[r.winner].name + " wins the match (rock paper scissors tiebreak)", r.winner);
        return;
    }
    rpsNewThrow(s);
}

bool tagMatchOn(const Server& s) { return s.ms_.mode == MODE_TAG && (s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_LIVE || s.ms_.phase == PHASE_ROUND_END); }

// Tag: out for the rest of the match, a human remembered by rank id and name (a reconnect doesn't clear it). Bots
// aren't: none ever comes back, and a new bot reusing an eliminated one's name isn't it (FINAL_REVIEW 6.5).
void tagOut(Server& s, int id) {
    auto& p = s.players_[id];
    p.out = true;
    p.it = false;
    p.alive = false;
    p.respawnAt = -1;
    if (!p.isBot) s.tagOut_.push_back({p.rankId, p.name});
}
bool tagWasOut(const Server& s, const ServerPlayer& p) {
    if (p.isBot) return false;
    for (auto& o : s.tagOut_) if ((!o.first.empty() && o.first == p.rankId) || o.second == p.name) return true;
    return false;
}

// Whoever is "it" is out (time's up, or they left / went to spectate while it): the rest score and the round ends.
void tagEliminateIt(Server& s, int it, const std::string& why) {
    std::string name = s.players_[it].name;
    tagOut(s, it);
    int left = playersInPlay(s);
    for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i)) s.players_[i].score += 1; // survived the round
    if (left <= 1) {
        int w = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i)) w = i;
        enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins tag" : "nobody left", w);
        return;
    }
    endRoundText(s, name + " was it - " + why + "! " + std::to_string(left) + " left");
}

void tickTag(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    if (playersInPlay(s) <= 1) {                  // everyone else left: the last one standing wins
        int w = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i)) w = i;
        enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins tag" : "nobody left", w);
        return;
    }
    int it = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i) && s.players_[i].it) it = i;
    if (it < 0) {                                 // "it" left: someone else is it now (same short freeze as a tag)
        pickIt(s);
        for (auto& p : s.players_) if (p.connected && p.it) p.itFreezeUntil = s.time() + kTagFreeze;
        return;
    }
    if (s.time() < s.phaseEnd_) return;
    tagEliminateIt(s, it, "out");                 // time's up: whoever is it is out
}

void startRound(Server& s) {
    ModeState& m = s.ms_;
    m.round++;
    m.flags = 0;
    m.bombState = BOMB_NONE;
    m.bombCarrier = 255;
    m.bombTimer = 0;
    m.actionProgress = 0;
    m.actionPlayer = 255;
    m.hillOwner = 255;
    s.drainAccum_ = 0;
    s.objTouch_ = -1e9;   // (m.flags = 0 above ended any overtime)
    if (m.mode == MODE_DUEL) { // a new modifier every life, never one already played this match
        static_assert(DUEL_MOD_COUNT <= 32, "duelUsed_ is a 32-bit mask");
        const uint32_t all = DUEL_MOD_COUNT == 32 ? 0xFFFFFFFFu : (1u << DUEL_MOD_COUNT) - 1;
        if ((s.duelUsed_ & all) == all) s.duelUsed_ = 0; // only if a match ever outlasts the list
        int left = 0, pick = 0;
        for (int i = 0; i < DUEL_MOD_COUNT; i++) left += !(s.duelUsed_ >> i & 1);
        int n = qrand() % left;
        for (int i = 0; i < DUEL_MOD_COUNT; i++)
            if (!(s.duelUsed_ >> i & 1) && n-- == 0) { pick = i; break; }
        s.duelUsed_ |= 1u << pick;
        m.duelMod = (uint8_t)pick;
        s.message("round " + std::to_string(m.round) + ": " + duelModTitle(pick));
        s.glog(LOG_SIMPLE, "round %d modifier %s", m.round, duelModTitle(pick));
    }
    if (m.mode == MODE_BOMB) {
        int half = roundsToWin(s) - 1;
        m.attackers = (half > 0 && m.round > half) ? TEAM_B : TEAM_A;
    } else if (m.mode == MODE_PAYLOAD) {
        m.attackers = (m.round % 2 == 1) ? TEAM_A : TEAM_B;
        m.payloadDist = 0;
        m.payloadLen = s.map_.payloadLength();
        m.point = 0;   // (checkpoints reached)
        m.pointCount = (uint8_t)s.map_.cartChecks.size();
    } else if (m.mode == MODE_POINTS) { // the sides swap every round; the attack starts at the first point
        m.attackers = (m.round % 2 == 1) ? TEAM_A : TEAM_B;
        m.point = 0;
        m.capture = 0;
        m.pointCount = (uint8_t)s.map_.points.size();
    } else if (m.mode == MODE_NPAYLOAD) {
        m.payloadLen = s.map_.payloadLength();
        m.payloadDist = m.payloadLen * 0.5f; // starts in the middle; team A pushes to the end, B to the start
    }
    for (auto& p : s.players_) {
        p.finished = false;
        p.checkpoint = 0;
        p.respawnAt = -1;
        p.it = false;
        p.found = false;
        p.itFreezeUntil = 0;
        if (m.mode == MODE_HIDE) p.out = false; // tag: eliminations last the whole match
    }
    if (isItMode(m.mode)) pickIt(s);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected) continue;
        if (p.team == TEAM_SPECTATOR || (m.mode == MODE_TAG && p.out)) { p.alive = false; continue; }
        s.spawnPlayer(i);
    }
    // Remove leftover projectiles between rounds.
    for (auto& pr : s.projectiles_) pr.active = false;

    if (m.mode == MODE_BOMB) {
        std::vector<int> cands;
        for (int i = 0; i < kMaxPlayers; i++)
            if (s.players_[i].connected && s.players_[i].alive && s.players_[i].team == m.attackers) cands.push_back(i);
        if (!cands.empty()) {
            m.bombCarrier = (uint8_t)cands[qrand() % cands.size()];
            m.bombState = BOMB_CARRIED;
        }
    }
    float freeze = m.mode == MODE_HIDE ? kHideTime : kFreezeTime; // hide and seek: the hiders' head start
    s.roundStart_ = s.time() + freeze;
    s.lastProgress_ = s.roundStart_;
    s.payloadIdleSince_ = s.roundStart_;
    setPhase(s, PHASE_FREEZE, freeze);
    if (m.mode == MODE_HIDE)
        for (auto& p : s.players_) if (p.connected && p.it) s.message(p.name + " is seeking - hide! (" + std::to_string((int)kHideTime) + " s)");
    if (m.mode == MODE_TAG)
        for (auto& p : s.players_) if (p.connected && p.it) s.message(p.name + " is it - run!");
}

// ---- tourney: single elimination, one 1v1 at a time, everyone else spectates ----
std::string tourneyName(const Server& s, int id) { return id >= 0 && s.players_[id].connected ? s.players_[id].name : "?"; }

// Fills in byes / players who left, then sets up the next playable match. Returns the champion's id
// when the bracket is finished (else -1).
int tourneyAdvance(Server& s) {
    auto& t = s.tourney_;
    for (;;) {
        auto& cur = t.rounds[t.round];
        if (cur.size() == 1) return cur[0] >= 0 ? cur[0] : -2; // champion (or nobody)
        auto& next = t.rounds[t.round + 1];
        bool played = false;
        for (int m = 0; m < (int)cur.size() / 2; m++) {
            if (next[m] != -2) continue; // decided
            int a = cur[2 * m], b = cur[2 * m + 1];
            auto in = [&](int i) { return i >= 0 && s.players_[i].connected && s.players_[i].joinId == t.join[i]; };
            bool aIn = in(a), bIn = in(b);
            if (aIn && bIn) { t.match = m; played = true; break; }
            next[m] = aIn ? a : bIn ? b : -1; // walkover (bye or left)
        }
        if (played) return -1;
        t.round++;
        t.match = 0;
    }
}

void tourneySetupMatch(Server& s) {
    auto& t = s.tourney_;
    ModeState& m = s.ms_;
    int a = t.rounds[t.round][2 * t.match], b = t.rounds[t.round][2 * t.match + 1];
    if (!s.cfg_.tourneyMaps.empty()) s.switchMapKeepRules(s.cfg_.tourneyMaps[t.played % s.cfg_.tourneyMaps.size()]);
    int v = s.cfg_.tourneyVariants.empty() ? s.cfg_.arenaVariant : s.cfg_.tourneyVariants[t.played % s.cfg_.tourneyVariants.size()];
    m.arenaVariant = (uint8_t)std::max(0, v);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected) continue;
        p.team = i == a ? TEAM_A : i == b ? TEAM_B : TEAM_SPECTATOR;
        p.alive = false;
    }
    m.scoreA = m.scoreB = 0;
    m.round = 0;
    s.broadcastBracket();
    std::string what = tourneyName(s, a) + " vs " + tourneyName(s, b) + " - " + arenaVariant(m.arenaVariant).title + " on " + s.cfg_.map;
    s.message(what);
    s.glog(LOG_SIMPLE, "tourney match: %s", what.c_str());
}

void tourneyBegin(Server& s) {
    auto& t = s.tourney_;
    t = Server::Tourney();
    std::vector<int> entrants;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].team != TEAM_SPECTATOR) entrants.push_back(i);
    // seeded by rating (the average of the three ladders; Server::tourneySeeding): top seeds get the byes
    std::vector<int> first = s.tourneySeeding(entrants);
    for (int i : entrants) t.join[i] = s.players_[i].joinId;
    t.rounds.push_back(first);
    for (size_t n = first.size() / 2; n >= 1; n /= 2) t.rounds.push_back(std::vector<int>(n, -2));
    t.running = true;
}

void startMatch(Server& s) {
    s.clearProps();   // (a new match, whatever phase it starts from)
    s.short_ = false;
    s.matchManned_ = false;
    ModeState& m = s.ms_;
    m.round = 0;
    m.scoreA = m.scoreB = 0;
    m.roundsToWin = (uint8_t)roundsToWin(s);
    m.target = (int16_t)(m.mode == MODE_KOTH ? (s.cfg_.scoreLimit > 0 ? s.cfg_.scoreLimit : 100) : 0);
    s.kothPoints_[0] = s.kothPoints_[1] = 0;
    s.bestProgress_[0] = s.bestProgress_[1] = 0;
    s.completeTime_[0] = s.completeTime_[1] = 0;
    for (auto& p : s.players_) { p.kills = p.deaths = p.score = 0; p.dmgDealt = p.objTime = 0; p.hideSurvived = 0; p.shotsFired = p.shotsHit = 0; p.out = false; p.it = false; }
    s.lastIt_ = -1;
    s.tagOut_.clear();   // tag knockouts last one match: everyone is back in for the next
    for (auto& p : s.players_) p.sought = false;
    if (m.mode == MODE_TOURNEY) {
        tourneyBegin(s);
        s.message("tourney started - " + std::to_string(s.tourney_.rounds[0].size()) + " slot bracket");
        if (tourneyAdvance(s) != -1) { enterMatchEnd(s, "not enough players for a tourney", -1); return; }
        tourneySetupMatch(s);
        startRound(s);
        return;
    }
    s.message(std::string(modeTitle(m.mode)) + " - match started");
    startRound(s);
}

// Payload match result after both teams attacked.
int payloadWinner(const Server& s) {
    const ModeState& m = s.ms_;
    if (m.scoreA != m.scoreB) return m.scoreA > m.scoreB ? TEAM_A : TEAM_B;
    if (m.scoreA > 0) { // both completed: faster wins
        if (s.completeTime_[0] != s.completeTime_[1]) return s.completeTime_[0] < s.completeTime_[1] ? TEAM_A : TEAM_B;
        return -1;
    }
    if (s.bestProgress_[0] != s.bestProgress_[1]) return s.bestProgress_[0] > s.bestProgress_[1] ? TEAM_A : TEAM_B;
    return -1;
}

const char* teamName(int t) { return t == TEAM_A ? "TEAM A" : "TEAM B"; }
// Who won, as the calls name them: the player in 1v1, duel and tourney (both, in a 2v2 duel), else the team.
std::string sideName(const Server& s, int team) {
    const int mode = s.ms_.mode;
    if (mode != MODE_ARENA && mode != MODE_TOURNEY && mode != MODE_DUEL) return teamName(team);
    std::string who;
    for (auto& p : s.players_) if (p.connected && p.team == team) who += (who.empty() ? "" : " & ") + p.name;
    return who.empty() ? std::string(teamName(team)) : who;
}

// ---- payload / multi point: a stopwatch pair of rounds (each team attacks once; the better attack wins) ----
std::string clockText(double t) { int n = (int)std::lround(std::max(0.0, t)); return std::to_string(n / 60) + ":" + (n % 60 < 10 ? "0" : "") + std::to_string(n % 60); }
// What `team`'s attack did this pair of rounds, as a mark ("57 m", "a 3:12 delivery", "2 points and 40% of C") and
// as what they did ("pushed 57 m", "delivered the payload in 3:12", "took 2 points and 40% of C").
std::string markText(const Server& s, int team) {
    const float best = s.bestProgress_[team];
    if (s.completeTime_[team] > 0) return (s.ms_.mode == MODE_PAYLOAD ? "delivery in " : "capture of every point in ") + clockText(s.completeTime_[team]);
    if (s.ms_.mode == MODE_PAYLOAD) return std::to_string((int)best) + " m";
    int pts = (int)best, pct = (int)((best - pts) / 0.99f * 100.0f);
    std::string part = std::to_string(pct) + "% of " + char('A' + pts);
    if (pts == 0) return pct > 0 ? part : "no points";
    return std::to_string(pts) + (pts == 1 ? " point" : " points") + (pct > 0 ? " and " + part : "");
}
std::string attackText(const Server& s, int team) {
    if (s.completeTime_[team] > 0) return (s.ms_.mode == MODE_PAYLOAD ? "delivered the payload in " : "took every point in ") + clockText(s.completeTime_[team]);
    return (s.ms_.mode == MODE_PAYLOAD ? "pushed " : "took ") + markText(s, team);
}
// The second attack of a pair (T15, TF2's stopwatch): the result is locked once the attackers have beaten the first
// attack's mark (the cart or the capture past it: they win) or, against one that finished, once their clock is past
// its time (they lose). The round ends then, not minutes later at time out. -1: not decided yet.
int stopwatchLocked(const Server& s) {
    const ModeState& m = s.ms_;
    if (m.round % 2 != 0) return -1;
    const int att = m.attackers, first = other(att);
    if (s.completeTime_[first] > 0) return s.time() - s.roundStart_ > s.completeTime_[first] ? first : -1;
    return s.bestProgress_[att] > s.bestProgress_[first] ? att : -1;
}

// winner: team for team modes, player id for race, -1 = draw/no winner.
void endRound(Server& s, int winner, const std::string& why, bool award = true) {
    ModeState& m = s.ms_;
    std::string text;
    if (m.mode == MODE_RACE) {
        if (winner >= 0) { s.players_[winner].score++; text = s.players_[winner].name + " wins the race"; }
        else text = "nobody finished";
    } else if (m.mode == MODE_PAYLOAD || m.mode == MODE_POINTS) {
        // F9: no "round won" in a pair's first round (the defenders heard ROUND WON there, then YOU LOSE): the mark
        // to beat. The second names the attack against it, as won by whoever the pair's result is (the match end).
        if (winner >= 0 && award) (winner == TEAM_A ? m.scoreA : m.scoreB)++;
        const int att = m.attackers, first = other(att);
        text = std::string(teamName(att)) + " " + attackText(s, att);
        if (m.round % 2 == 1) { text += " - " + std::string(teamName(first)) + " must beat it"; winner = -1; }
        else {
            winner = payloadWinner(s);
            text += winner == att ? " - beats " : winner < 0 ? " - level with " : " - short of ";
            text += std::string(teamName(first)) + "'s " + markText(s, first);
        }
    } else if (winner >= 0) {
        if (award) (winner == TEAM_A ? m.scoreA : m.scoreB)++;
        text = sideName(s, winner) + " wins the round";   // (1v1, duel, tourney: the player's name)
    } else {
        text = "round draw";
    }
    if (!why.empty() && m.mode != MODE_PAYLOAD && m.mode != MODE_POINTS) text += " (" + why + ")";
    GameEvent e;
    e.type = EV_ROUND_WIN;
    e.a = (uint8_t)(winner < 0 ? 255 : winner);
    e.text = text;
    s.broadcastEvent(e);
    s.log(text);
    s.glog(LOG_SIMPLE, "round %d end: %s (score %d-%d)", m.round, text.c_str(), m.scoreA, m.scoreB);
    m.actionPlayer = 255;
    m.actionProgress = 0;
    const bool duelish = m.mode == MODE_ARENA || m.mode == MODE_DUEL || m.mode == MODE_TOURNEY;
    setPhase(s, PHASE_ROUND_END, duelish ? kDuelRoundEndTime : kRoundEndTime);
}

// Called when ROUND_END finishes: returns true if the match is decided.
bool checkMatchOver(Server& s) {
    ModeState& m = s.ms_;
    int rtw = roundsToWin(s);
    switch (m.mode) {
        case MODE_TOURNEY:
            if (m.scoreA >= rtw || m.scoreB >= rtw) { // match decided: winner advances
                auto& t = s.tourney_;
                int a = t.rounds[t.round][2 * t.match], b = t.rounds[t.round][2 * t.match + 1];
                int w = m.scoreA >= rtw ? a : b;
                t.rounds[t.round + 1][t.match] = w;
                t.played++;
                s.message(tourneyName(s, w) + " advances");
                int champ = tourneyAdvance(s);
                s.broadcastBracket();
                if (champ != -1) {
                    t.running = false;
                    enterMatchEnd(s, champ >= 0 ? tourneyName(s, champ) + " wins the tourney!" : "tourney over", champ);
                    return true;
                }
                tourneySetupMatch(s);
            }
            return false;
        case MODE_VS:
        case MODE_DUEL:
        case MODE_ARENA:
        case MODE_BOMB:
            if (m.scoreA >= rtw || m.scoreB >= rtw) {
                int w = m.scoreA >= rtw ? TEAM_A : TEAM_B;
                enterMatchEnd(s, sideName(s, w) + " wins the match", w);
                return true;
            }
            return false;
        case MODE_RACE:
            for (int i = 0; i < kMaxPlayers; i++)
                if (s.players_[i].connected && s.players_[i].score >= rtw) {
                    enterMatchEnd(s, s.players_[i].name + " wins the match", i);
                    return true;
                }
            return false;
        case MODE_HIDE:
            if (hidersYetToSeek(s) > 0 && playersInPlay(s) > 1) return false;   // (everyone has had a turn seeking)
            { int w; if (rpsStartIfTied(s, &w)) return true;   // tied for first: time hidden, then rock paper scissors (tickRps ends the match)
              if (w < 0) w = bestScorer(s); enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins the match" : "match over", w); }
            return true;
        case MODE_TAG:
            if (playersInPlay(s) > 1) return false;
            { int w = -1; for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i)) w = i; enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins tag" : "nobody left", w); }
            return true;
        case MODE_PAYLOAD:
        case MODE_POINTS: {   // (multi point: progress = points taken + the capture on the next one)
            if (m.round < 2 || m.round % 2 != 0) return false;
            int w = payloadWinner(s);
            if (w < 0 && m.round < 4) { // the tiebreaker is decided on its own two rounds
                s.message("tied - tiebreaker rounds");
                m.scoreA = m.scoreB = 0;
                s.bestProgress_[0] = s.bestProgress_[1] = 0;
                s.completeTime_[0] = s.completeTime_[1] = 0;
                return false;
            }
            enterMatchEnd(s, w >= 0 ? std::string(teamName(w)) + " wins the match" : "match draw", w);
            return true;
        }
    }
    return true;
}

// A mode that can't be played on this map at all: multi point with no points (it would never end).
bool unplayable(const Server& s) { return s.ms_.mode == MODE_POINTS && s.map_.points.empty(); }

bool enoughPlayers(const Server& s) {
    switch (s.ms_.mode) {
        case MODE_RACE: return s.teamCount(TEAM_A) >= 1;
        case MODE_FFA:
        case MODE_HIDE:
        case MODE_TAG:
        case MODE_TOURNEY: return s.teamCount(TEAM_A) >= 2;
        default: return s.teamCount(TEAM_A) >= 1 && s.teamCount(TEAM_B) >= 1;
    }
}

// F5 (not ranked; ranked has its own leaver rules): a match that had enough players and lost them, or that nobody is
// playing any more, holds still (no rounds awarded to an empty side, no draws looping on an empty server) and after
// kShortHanded s without enough players ends: a forfeit win for the side still there in team modes, else back to
// warmup. Someone joining in time picks it up again. Tourney (walkovers), tag and hide and seek settle it themselves.
// True while the match is on hold (the mode's own tick is skipped).
constexpr float kShortHanded = 10.0f;
bool shortHanded(Server& s) {
    const int mode = s.ms_.mode;
    if (s.cfg_.ranked || mode == MODE_TOURNEY || isItMode(mode)) return false;
    if (enoughPlayers(s)) { s.matchManned_ = true; if (s.short_) s.message("enough players again - the match goes on"); s.short_ = false; return false; }
    int playing = s.teamCount(TEAM_A) + (modeIsTeamBased(mode) ? s.teamCount(TEAM_B) : 0);
    if (!s.matchManned_ && playing > 0) return false;   // (started short-handed on purpose: forcestart, a lone vote)
    if (!s.short_) {
        s.short_ = true;
        s.shortSince_ = s.time();
        if (playing > 0) s.message("not enough players - the match ends in " + std::to_string((int)kShortHanded) + " s unless someone joins");
        s.glog(LOG_SIMPLE, "short-handed: %d playing", playing);
    }
    if (s.time() - s.shortSince_ < kShortHanded) return true;
    s.short_ = false;
    int w = -1;
    if (modeIsTeamBased(mode) && playing > 0) w = s.teamCount(TEAM_A) > 0 ? TEAM_A : TEAM_B;
    if (w >= 0) { enterMatchEnd(s, sideName(s, w) + " wins - the other side left", w); return true; }
    for (auto& p : s.players_) p.alive = false;
    rulesReset(s);
    s.message(playing > 0 ? "not enough players - back to warmup" : "everyone left - back to warmup");
    return true;
}

bool feetIn(const Server& s, int id, const AABB& zone) {
    return playerBox(s.players_[id].move).overlaps(zone);
}

void tickVS(Server& s) {
    ModeState& m = s.ms_;
    if (m.mode == MODE_TOURNEY && s.tourney_.running) { // a finalist left: walkover, straight to the next match
        auto& t = s.tourney_;
        int a = t.rounds[t.round][2 * t.match], b = t.rounds[t.round][2 * t.match + 1];
        auto in = [&](int i) { return i >= 0 && s.players_[i].connected && s.players_[i].joinId == t.join[i]; };
        if (!in(a) || !in(b)) {
            int w = in(a) ? a : in(b) ? b : -1;
            t.rounds[t.round + 1][t.match] = w;
            t.played++;
            if (w >= 0) s.message(tourneyName(s, w) + " advances (walkover)");
            int champ = tourneyAdvance(s);
            s.broadcastBracket();
            if (champ != -1) { t.running = false; enterMatchEnd(s, champ >= 0 ? tourneyName(s, champ) + " wins the tourney!" : "tourney over", champ); return; }
            tourneySetupMatch(s);
            startRound(s);
            return;
        }
    }
    int a = s.aliveCount(TEAM_A), b = s.aliveCount(TEAM_B);
    if (a == 0 || b == 0) {
        endRound(s, a > 0 ? TEAM_A : b > 0 ? TEAM_B : -1, "");
        return;
    }
    float idle = s.time() - s.lastProgress_;
    m.timeLeft = std::max(0.0f, kStalemateTime - idle);
    if (idle < kStalemateTime) { m.flags &= ~MS_DRAIN; return; }
    if (!(m.flags & MS_DRAIN)) s.message("stalemate - health draining");
    m.flags |= MS_DRAIN;
    s.drainAccum_ += kDrainPerSecond * s.dt();
    while (s.drainAccum_ >= 1.0f) {
        s.drainAccum_ -= 1.0f;
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = s.players_[i];
            if (!p.connected || !p.alive) continue;
            p.health -= 1.0f;
            if (p.health <= 0) s.killPlayer(i, -1, 254);
        }
    }
}

void tickBomb(Server& s) {
    ModeState& m = s.ms_;
    const int att = m.attackers, def = defenders(s);
    const float dt = s.dt();
    m.timeLeft = m.bombState == BOMB_PLANTED ? m.bombTimer : (float)std::max(0.0, s.phaseEnd_ - s.time());

    if (m.bombState == BOMB_DROPPED) {
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = s.players_[i];
            if (p.connected && p.alive && p.team == att && length(p.move.pos - m.bombPos) < kPickupRange) {
                m.bombState = BOMB_CARRIED;
                m.bombCarrier = (uint8_t)i;
                break;
            }
        }
    }
    if (m.bombState == BOMB_DROPPED && s.time() - s.bombDroppedAt_ >= kBombReturn) {   // out of reach, or nobody went for it
        std::vector<int> c;
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].alive && s.players_[i].team == att) c.push_back(i);
        if (!c.empty()) {
            int k = c[qrand() % c.size()];
            m.bombState = BOMB_CARRIED;
            m.bombCarrier = (uint8_t)k;
            s.message("the bomb went back to " + s.players_[k].name);
        }
    }
    if (m.bombState == BOMB_CARRIED) {
        int c = m.bombCarrier;
        auto& p = s.players_[c];
        bool inSite = false;
        for (auto& site : s.map_.bombSites) inSite |= feetIn(s, c, site);
        bool onGround = p.move.flags & MF_ONGROUND;
        if ((p.buttons & BTN_USE) && inSite && onGround) {
            m.actionPlayer = (uint8_t)c;
            m.actionProgress += dt / kPlantTime;
            if (m.actionProgress >= 1.0f) {
                m.bombState = BOMB_PLANTED;
                m.bombPos = p.move.pos;
                m.bombTimer = kBombFuse;
                m.bombCarrier = 255;
                m.actionPlayer = 255;
                m.actionProgress = 0;
                s.lastProgress_ = s.time();
                s.message("bomb planted");
            }
        } else if (m.actionPlayer == c) {
            m.actionPlayer = 255;
            m.actionProgress = 0;
        }
    } else if (m.bombState == BOMB_PLANTED) {
        m.bombTimer -= dt;
        int defuser = -1;
        if (m.actionPlayer != 255) {
            auto& p = s.players_[m.actionPlayer];
            if (p.connected && p.alive && (p.buttons & BTN_USE) && length(p.move.pos - m.bombPos) < kDefuseRange) defuser = m.actionPlayer;
        }
        if (defuser < 0) {
            m.actionProgress = 0;
            m.actionPlayer = 255;
            for (int i = 0; i < kMaxPlayers; i++) {
                auto& p = s.players_[i];
                if (p.connected && p.alive && p.team == def && (p.buttons & BTN_USE) && length(p.move.pos - m.bombPos) < kDefuseRange) {
                    defuser = i;
                    m.actionPlayer = (uint8_t)i;
                    break;
                }
            }
        }
        if (defuser >= 0) {
            m.actionProgress += dt / kDefuseTime;
            if (m.actionProgress >= 1.0f) {
                m.bombState = BOMB_DEFUSED;
                endRound(s, def, "bomb defused");
                return;
            }
        }
        if (m.bombTimer <= 0) {
            m.bombState = BOMB_EXPLODED;
            for (int i = 0; i < kMaxPlayers; i++) {
                auto& p = s.players_[i];
                if (p.connected && p.alive && length(p.move.pos - m.bombPos) < kBombRadius) s.killPlayer(i, -1, 253);
            }
            endRound(s, att, "bomb exploded");
            return;
        }
    }

    if (s.aliveCount(def) == 0) { endRound(s, att, "defenders eliminated"); return; }
    if (s.aliveCount(att) == 0 && m.bombState != BOMB_PLANTED) { endRound(s, def, "attackers eliminated"); return; }
    if (m.bombState == BOMB_PLANTED) { m.flags &= ~MS_OVERTIME; return; }   // (the fuse is the clock now)
    for (int i = 0; i < kMaxPlayers; i++) {   // overtime: an attacker on a bomb site
        const auto& p = s.players_[i];
        if (!p.connected || !p.alive || p.team != att) continue;
        for (auto& site : s.map_.bombSites) if (feetIn(s, i, site)) s.objTouch_ = s.time();
    }
    if (s.time() < s.phaseEnd_) m.flags &= ~MS_OVERTIME;
    else if (!overtime(s)) endRound(s, def, "time");
}

void tickRace(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    const auto& cps = s.map_.checkpoints;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || p.finished) continue;
        if (p.checkpoint < (int)cps.size() && feetIn(s, i, cps[p.checkpoint])) {
            p.checkpoint++;
            GameEvent e; // the split: race time at this checkpoint, to the ms (time trials log these)
            e.type = EV_RACE_SPLIT; e.a = (uint8_t)i; e.b = (uint8_t)p.checkpoint;
            long long ms = std::lround((roundTime(s) - (s.phaseEnd_ - s.time())) * 1000.0);
            e.text = std::to_string(ms);
            s.broadcastEvent(e);
            if (p.checkpoint == 1) p.raceSplits.clear();
            if ((int)p.raceSplits.size() == p.checkpoint - 1) p.raceSplits.push_back((int)ms);
            else p.raceSplits.clear(); // (joined mid-run: this one doesn't count)
            if (p.checkpoint == (int)cps.size()) {
                p.finished = true;
                if ((int)p.raceSplits.size() == p.checkpoint) s.finishRaceRun(i);
                endRound(s, i, "");
                return;
            }
        }
    }
    if (s.time() >= s.phaseEnd_) endRound(s, -1, "time");
}

// Race practice (warmup): run the course, checkpoints in order (a fall puts you back at the last one), no clock,
// nothing recorded. The finish sends you round again.
void tickRacePractice(Server& s) {
    const auto& cps = s.map_.checkpoints;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || p.checkpoint >= (int)cps.size() || !feetIn(s, i, cps[p.checkpoint])) continue;
        if (++p.checkpoint == (int)cps.size()) {
            s.message(p.name + " finished the course (practice) - vote start to race");
            p.checkpoint = 0;
        }
    }
}

void tickKoth(Server& s) {
    ModeState& m = s.ms_;
    int in[2] = {0, 0};
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.alive && p.team < 2 && feetIn(s, i, s.map_.hill)) { in[p.team]++; p.objTime += s.dt(); }
    }
    m.flags &= ~MS_CONTESTED;
    if (in[0] > 0 && in[1] > 0) { m.flags |= MS_CONTESTED; m.hillOwner = 255; }
    else if (in[0] > 0 || in[1] > 0) {
        int t = in[0] > 0 ? 0 : 1;
        m.hillOwner = (uint8_t)t;
        s.kothPoints_[t] += s.dt();
    } else m.hillOwner = 255;
    m.scoreA = (int16_t)s.kothPoints_[0];
    m.scoreB = (int16_t)s.kothPoints_[1];
    m.timeLeft = 0;
    if (m.scoreA >= m.target || m.scoreB >= m.target) {
        int w = m.scoreA >= m.target ? TEAM_A : TEAM_B;
        enterMatchEnd(s, std::string(teamName(w)) + " wins the match", w);
    }
}

void tickPayload(Server& s) {
    ModeState& m = s.ms_;
    const int att = m.attackers, def = defenders(s);
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    Vec3 cart = s.map_.cartPoint(m.mode, m.attackers, m.payloadDist);
    int na = 0, nd = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive) continue;
        if (length(p.move.pos - cart) >= kCartRange) continue;
        if (p.team == att) na++;
        else if (p.team == def) nd++;
    }
    // objective time: attackers by the cart; defenders only while blocking it (an attacker is there too)
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.alive && length(p.move.pos - cart) < kCartRange && (p.team == att || (p.team == def && na > 0))) p.objTime += s.dt();
    }
    m.flags &= ~MS_CONTESTED;
    if (na > 0) s.objTouch_ = s.time();   // (overtime: an attacker at the cart, contested or not)
    if (na > 0 && nd == 0) {
        float speed = std::min(kCartSpeed * (1.0f + 0.25f * (na - 1)), kCartSpeed * 2.0f);
        m.payloadDist = std::min(m.payloadDist + speed * s.dt(), m.payloadLen);
        s.payloadIdleSince_ = s.time();
    } else if (na > 0 && nd > 0) {
        m.flags |= MS_CONTESTED;
        s.payloadIdleSince_ = s.time();
    } else if (na == 0 && s.time() - s.payloadIdleSince_ > kCartIdleBeforeRollback) {
        float floor = m.point > 0 && m.point <= s.map_.cartChecks.size() ? s.map_.cartChecks[m.point - 1] * m.payloadLen : 0.0f;
        m.payloadDist = std::max(floor, m.payloadDist - kCartRollback * s.dt());   // (never back past a checkpoint)
    }
    // checkpoints: more time for the attack, the cart stays past it, and the spawns move up a stage (staged maps)
    while (m.point < s.map_.cartChecks.size() && m.payloadDist >= s.map_.cartChecks[m.point] * m.payloadLen) {
        m.point++;
        s.phaseEnd_ = std::max(s.phaseEnd_, s.time()) + kCartCheckBonus;   // (in overtime the clock is past: from now)
        s.message(std::string(teamName(att)) + " reached checkpoint " + std::to_string(m.point) + " - +" + std::to_string((int)kCartCheckBonus) + " s");
    }
    float& best = s.bestProgress_[att];
    best = std::max(best, m.payloadDist);
    if (m.payloadDist >= m.payloadLen - 1e-3f) {
        s.completeTime_[att] = s.time() - s.roundStart_;
        endRound(s, att, "payload delivered");
        return;
    }
    if (int w = stopwatchLocked(s); w >= 0) { endRound(s, w, "", false); return; }
    if (s.time() < s.phaseEnd_) m.flags &= ~MS_OVERTIME;
    else if (!overtime(s)) endRound(s, def, "time", false);
}

// Multi point: the attackers take the map's points one at a time, in order. Only they capture: standing on the
// point with no defender on it fills it (more attackers fill it faster); a defender on it holds it; left alone it
// slowly drains. Each point taken adds time to the clock and moves both teams' spawns up (staged spawns). All
// points taken: the attack wins the round. Time out: the defence holds.
void tickPoints(Server& s) {
    ModeState& m = s.ms_;
    const int att = m.attackers, def = defenders(s);
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    m.pointCount = (uint8_t)s.map_.points.size();
    if (m.pointCount == 0) { enterMatchEnd(s, "this map has no points - multi point can't be played here", -1); return; }   // (it never ended)
    if (m.point >= m.pointCount) return;
    const AABB& zone = s.map_.points[m.point];
    int na = 0, nd = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || !feetIn(s, i, zone)) continue;
        if (p.team == att) na++;
        else if (p.team == def) nd++;
    }
    // objective time: attackers on the point; defenders only while contesting it (an attacker is on it too)
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.alive && feetIn(s, i, zone) && (p.team == att || (p.team == def && na > 0))) p.objTime += s.dt();
    }
    m.flags &= ~MS_CONTESTED;
    m.hillOwner = 255;
    if (na > 0) s.objTouch_ = s.time();   // (overtime: an attacker on the point, contested or not)
    if (na > 0 && nd == 0) {
        m.capture += std::min(1.0f + 0.5f * (na - 1), 2.0f) * s.dt() / kCaptureTime;
        m.hillOwner = (uint8_t)att;
    } else if (na > 0) m.flags |= MS_CONTESTED;
    else m.capture = std::max(0.0f, m.capture - kCaptureDecay * s.dt());
    if (m.capture >= 1.0f) {
        m.capture = 0;
        m.point++;
        float& best = s.bestProgress_[att];
        best = std::max(best, (float)m.point);
        if (m.point >= m.pointCount) {
            s.completeTime_[att] = s.time() - s.roundStart_;
            endRound(s, att, "every point taken");
            return;
        }
        s.phaseEnd_ = std::max(s.phaseEnd_, s.time()) + kPointBonus;   // (in overtime the clock is past: from now)
        s.message(std::string(teamName(att)) + " took point " + char('A' + m.point - 1) + " - +" + std::to_string((int)kPointBonus) + " s, on to point " + char('A' + m.point));
        s.glog(LOG_SIMPLE, "point %d taken by team %d", m.point, att);
        return;
    }
    s.bestProgress_[att] = std::max(s.bestProgress_[att], m.point + m.capture * 0.99f);
    if (int w = stopwatchLocked(s); w >= 0) { endRound(s, w, "", false); return; }
    if (s.time() < s.phaseEnd_) m.flags &= ~MS_OVERTIME;
    else if (!overtime(s)) endRound(s, def, "time", false);   // (overtime: the attack goes on while it's on the point)
}

// Neutral payload: whichever team is alone at the cart pushes it toward the far end of the track
// (team A toward the end, team B toward the start). Reaching an end wins; at time-out the side of
// the track the cart is on wins.
void tickNeutralPayload(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    Vec3 cart = s.map_.cartPoint(m.mode, m.attackers, m.payloadDist);
    int na = 0, nb = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || length(p.move.pos - cart) >= kCartRange) continue;
        p.objTime += s.dt();
        if (p.team == TEAM_A) na++;
        else if (p.team == TEAM_B) nb++;
    }
    m.flags &= ~MS_CONTESTED;
    m.hillOwner = 255; // reused: which team is moving the cart
    if (na > 0 && nb > 0) m.flags |= MS_CONTESTED;
    else if (na > 0 || nb > 0) {
        int n = na > 0 ? na : nb;
        float speed = std::min(kCartSpeed * (1.0f + 0.25f * (n - 1)), kCartSpeed * 2.0f);
        m.payloadDist = clampf(m.payloadDist + (na > 0 ? speed : -speed) * s.dt(), 0, m.payloadLen);
        m.hillOwner = (uint8_t)(na > 0 ? TEAM_A : TEAM_B);
    }
    if (m.payloadDist >= m.payloadLen - 1e-3f) { enterMatchEnd(s, "TEAM A delivered the payload", TEAM_A); return; }
    if (m.payloadDist <= 1e-3f) { enterMatchEnd(s, "TEAM B delivered the payload", TEAM_B); return; }
    {   // overtime: the team that would lose at time-out (the cart on the other side) is at the cart
        float mid = m.payloadLen * 0.5f;
        bool even = std::fabs(m.payloadDist - mid) < 0.05f;
        if ((na > 0 && (even || m.payloadDist < mid)) || (nb > 0 && (even || m.payloadDist > mid))) s.objTouch_ = s.time();
    }
    if (s.time() < s.phaseEnd_) m.flags &= ~MS_OVERTIME;
    else if (!overtime(s)) {
        float mid = m.payloadLen * 0.5f;
        if (std::fabs(m.payloadDist - mid) < 0.05f) enterMatchEnd(s, "draw - the payload is in the middle", -1);
        else {
            int w = m.payloadDist > mid ? TEAM_A : TEAM_B;
            enterMatchEnd(s, std::string(teamName(w)) + " wins - payload on their side", w);
        }
    }
}

// The cart is solid (cover, blocks movement). Keep the collision box where the cart is and push
// anyone it moved into out of the way.
void updateCartSolid(Server& s) {
    s.map_.dynSolids.clear();
    if (!isPayloadMode(s.ms_.mode) || s.map_.payloadPath.size() < 2) return;
    // (where the visible cart is: in team B's attack on an unstaged map it runs the track from the other end)
    AABB cart = s.map_.payloadCartBox(s.map_.cartTrack(s.ms_.mode, s.ms_.attackers, s.ms_.payloadDist));
    s.map_.dynSolids.push_back(cart);
    for (auto& p : s.players_) {
        if (!p.connected || !p.alive) continue;
        AABB b = playerBox(p.move);
        if (!b.overlaps(cart)) continue;
        // smallest push out: sideways either way, or up onto the cart
        float pushes[5] = {cart.mx.x - b.mn.x, b.mx.x - cart.mn.x, cart.mx.z - b.mn.z, b.mx.z - cart.mn.z, cart.mx.y - b.mn.y};
        int k = 0;
        for (int i = 1; i < 5; i++) if (pushes[i] < pushes[k]) k = i;
        float d = pushes[k] + 0.01f;
        if (k == 0) p.move.pos.x += d; else if (k == 1) p.move.pos.x -= d;
        else if (k == 2) p.move.pos.z += d; else if (k == 3) p.move.pos.z -= d;
        else p.move.pos.y += d;
    }
}

void finishFFA(Server& s) {
    int best = -1, bestScore = -100000, ties = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || p.team == TEAM_SPECTATOR) continue;
        if (p.score > bestScore) { bestScore = p.score; best = i; ties = 1; }
        else if (p.score == bestScore) ties++;
    }
    if (best < 0) {
        enterMatchEnd(s, "match over", -1); // nobody left playing
    } else if (ties == 1) {
        enterMatchEnd(s, s.players_[best].name + " wins the match", best);
    } else if (!(s.ms_.flags & MS_OVERTIME)) {
        s.ms_.flags |= MS_OVERTIME;
        s.message("tie - overtime! next kill wins");
    }
}

void tickFFA(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
    if (s.cfg_.scoreLimit > 0)
        for (auto& p : s.players_)
            if (p.connected && p.score >= s.cfg_.scoreLimit) { finishFFA(s); return; }
    if (s.time() >= s.phaseEnd_) finishFFA(s);
}
} // namespace

// Tourney warmup is a free for all; during matches the two players are on teams A and B.
bool modeIsTeamBased(int mode) { return mode != MODE_FFA && mode != MODE_RACE && mode != MODE_TOURNEY && !isItMode(mode); }

void rulesReset(Server& s) {
    s.clearProps();   // (setPhase below can't tell: the mode state is reset to warmup first)
    ModeState& m = s.ms_;
    m = ModeState();
    m.mode = (uint8_t)s.cfg_.mode;
    m.roundsToWin = (uint8_t)roundsToWin(s);
    m.payloadLen = s.map_.payloadLength();
    m.attackers = TEAM_A;
    m.duelMod = 255;
    s.duelUsed_ = 0; // a new match: every modifier is available again
    m.arenaVariant = (uint8_t)s.cfg_.arenaVariant;
    if (s.tourney_.running || m.mode == MODE_TOURNEY) { // back to free play: everyone in again
        s.tourney_.running = false;
        for (auto& p : s.players_) if (p.connected && m.mode == MODE_TOURNEY) p.team = TEAM_A;
    }
    for (auto& p : s.players_) { p.it = false; p.out = false; p.found = false; } // nothing from the last match carries into warmup
    s.tagOut_.clear();   // (the last tag match's knockouts, remembered across reconnects, end with it)
    // Joined mid tag match (or were playing it when a map / mode vote passed): placed like a newcomer once the tag
    // match is over, so the next mode's seat limits (1v1, duel) and team balance hold, not all on team A.
    uint32_t waiting = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.waitingToPlay) continue;
        p.waitingToPlay = false;
        if (p.connected && p.team == TEAM_SPECTATOR) waiting |= 1u << i;
    }
    setPhase(s, PHASE_WARMUP, 0);
    for (int i = 0; i < kMaxPlayers; i++) if (waiting >> i & 1) s.autoTeam(i);   // (warmup: the team lock is off)
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.team != TEAM_SPECTATOR && !p.alive) s.spawnPlayer(i);
    }
}

void rulesForceStart(Server& s) {
    if (unplayable(s)) { s.message("this map has no points - multi point can't be played here (vote another map or mode)"); return; }
    for (auto& p : s.players_) p.alive = false;
    startMatch(s);
}

void rulesTick(Server& s) {
    ModeState& m = s.ms_;
    m.weaponMask = s.weaponMask();
    updateCartSolid(s);
    if (tagMatchOn(s) && !s.tagOut_.empty())
        for (auto& p : s.players_)   // tag: back after being eliminated (reconnected): still out
            if (p.connected && !p.out && p.team != TEAM_SPECTATOR && tagWasOut(s, p)) { p.out = true; p.it = false; p.alive = false; p.respawnAt = -1; }
    if ((m.phase == PHASE_FREEZE || m.phase == PHASE_LIVE || m.phase == PHASE_ROUND_END) && !(m.flags & MS_RPS) && shortHanded(s)) {
        if (m.phase == PHASE_FREEZE || m.phase == PHASE_ROUND_END) s.phaseEnd_ += s.dt();   // (on hold: the clocks wait)
        else if (m.phase == PHASE_LIVE) { s.phaseEnd_ += s.dt(); s.lastProgress_ += s.dt(); s.roundStart_ += s.dt(); s.payloadIdleSince_ += s.dt(); }
        return;
    }
    switch (m.phase) {
        case PHASE_WARMUP:
            // Warmup lasts until players vote to start (F4). Play-vs-bots / autostart servers
            // start by themselves once both sides have someone. Race never starts itself: its warmup is
            // practice (the course, untimed) until someone starts the race.
            if (m.mode == MODE_RACE) tickRacePractice(s);
            if (!s.cfg_.autoStart || !enoughPlayers(s) || m.mode == MODE_RACE || unplayable(s)) { s.phaseEnd_ = 0; m.timeLeft = 0; break; }
            if (s.phaseEnd_ == 0) {
                s.phaseEnd_ = s.time() + kWarmupCountdown;
                s.message("match starting");
            }
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) startMatch(s);
            break;
        case PHASE_FREEZE:
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) {
                float rt = roundTime(s);
                setPhase(s, PHASE_LIVE, rt);
                s.lastProgress_ = s.time();
                s.roundStart_ = s.time();   // (the stopwatch times run from here, even if the freeze was cut short)
            }
            break;
        case PHASE_LIVE:
            switch (m.mode) {
                case MODE_VS:
                case MODE_DUEL:
                case MODE_ARENA:
                case MODE_TOURNEY: tickVS(s); break;
                case MODE_BOMB: tickBomb(s); break;
                case MODE_RACE: tickRace(s); break;
                case MODE_KOTH: tickKoth(s); break;
                case MODE_PAYLOAD: tickPayload(s); break;
                case MODE_NPAYLOAD: tickNeutralPayload(s); break;
                case MODE_FFA: tickFFA(s); break;
                case MODE_HIDE: tickHide(s); break;
                case MODE_TAG: tickTag(s); break;
                case MODE_POINTS: tickPoints(s); break;
            }
            break;
        case PHASE_ROUND_END:
            if (m.flags & MS_RPS) { tickRps(s); break; }   // hide and seek: the tiebreak after the last round
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_ && !checkMatchOver(s)) startRound(s);
            break;
        case PHASE_MATCH_END:
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) {
                for (auto& p : s.players_) p.alive = false;
                const CustomRules& c = s.cfg_.custom;
                if (c.active() && c.maps.size() > 1) { // a custom mode's map pool: on to the next map
                    auto it = std::find(c.maps.begin(), c.maps.end(), s.cfg_.map);
                    std::string next = it == c.maps.end() || it + 1 == c.maps.end() ? c.maps[0] : *(it + 1);
                    s.command("map " + next + " " + modeName(c.baseMode));
                    if (s.cfg_.map == next) break;
                }
                rulesReset(s);
            }
            break;
    }
}

void rulesOnDeath(Server& s, int victim, int killer) {
    ModeState& m = s.ms_;
    if (m.mode == MODE_BOMB && m.bombState == BOMB_CARRIED && m.bombCarrier == victim) {
        m.bombState = BOMB_DROPPED;
        const auto& v = s.players_[victim];
        // It falls to the floor under where they died (REVIEW GAME-5: it used to hang in the air where an airborne
        // carrier died, out of reach). Over the void, or into lava: where they last stood.
        Vec3 at = v.move.pos;
        float down = s.map_.raycast(at + Vec3(0, 0.05f, 0), Vec3(0, -1, 0), std::max(0.0f, at.y - s.map_.killY()) + 0.05f);
        Vec3 floor = at - Vec3(0, std::max(0.0f, down - 0.05f), 0);
        bool lava = !s.map_.lava.empty() && s.map_.inLava(AABB{floor - Vec3(0.2f, 0, 0.2f), floor + Vec3(0.2f, 0.3f, 0.2f)});
        m.bombPos = (at.y < s.map_.killY() + 2.0f || down < 0 || lava) ? v.lastGround : floor;
        m.bombCarrier = 255;
        s.bombDroppedAt_ = s.time();
    }
    if (m.actionPlayer == victim) { m.actionPlayer = 255; m.actionProgress = 0; }
    if (m.mode == MODE_HIDE && m.phase == PHASE_LIVE && !s.players_[victim].it)   // a hider is out: found scores the seeker 1, a fall nothing
        if (killer >= 0 && s.players_[killer].it && s.players_[victim].found) s.players_[killer].score += 1;
    if (m.mode == MODE_FFA && m.phase == PHASE_LIVE && (m.flags & MS_OVERTIME) && killer >= 0 && killer != victim)
        finishFFA(s);
}

void rulesOnLeave(Server& s, int id) {
    rulesOnSpectate(s, id);   // (tag: leaving is like going to spectate)
    if (s.ms_.mode == MODE_HIDE && !s.players_[id].alive) return;   // (already out: scored when found)
    rulesOnDeath(s, id, -1);
}

// Tag (REVIEW GAME-1): leaving or going to spectate during a match is elimination. "It" doing it ends the round like
// time running out (it used to hand "it" to someone else and come back in later, never out).
void rulesOnSpectate(Server& s, int id) {
    if (!tagMatchOn(s)) return;
    auto& p = s.players_[id];
    if (!p.connected || p.team == TEAM_SPECTATOR || p.out) return;
    if (p.it && s.ms_.phase != PHASE_ROUND_END) tagEliminateIt(s, id, "out");
    else tagOut(s, id);
}

bool rulesTeamLocked(const Server& s) { return tagMatchOn(s); }

void rulesEndMatch(Server& s, const std::string& text, int winner) {
    if (s.ms_.phase == PHASE_MATCH_END || s.ms_.phase == PHASE_WARMUP) return;
    s.tourney_.running = false;
    enterMatchEnd(s, text, winner);
}

float rulesRespawnDelay(const Server& s) {
    if (s.ms_.phase == PHASE_WARMUP) return 1.5f;
    if (s.ms_.phase == PHASE_MATCH_END) return -1.0f;
    return modeRespawnDelay(s.ms_.mode);
}

bool rulesCanDamage(const Server& s, int attacker, int victim) {
    if (s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END || s.ms_.phase == PHASE_FREEZE) return false;
    if (s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP) return false; // race: knockback only
    if (attacker < 0 || attacker == victim) return true;
    if (modeIsTeamBased(s.ms_.mode) && s.players_[attacker].team == s.players_[victim].team) return false;
    return true;
}

bool rulesFrozen(const Server& s, int id) {
    if (s.ms_.mode == MODE_HIDE && s.ms_.phase == PHASE_FREEZE) return s.players_[id].it; // hiders hide, the seeker faces the wall
    if (s.ms_.mode == MODE_TAG && s.ms_.phase == PHASE_LIVE) return s.players_[id].it && s.time() < s.players_[id].itFreezeUntil;
    if (s.ms_.phase == PHASE_FREEZE) return true;
    if (s.ms_.mode == MODE_BOMB && s.ms_.actionPlayer == id && s.ms_.phase == PHASE_LIVE) return true;
    return false;
}

int rulesSpawnType(const Server& s, int id) {
    return modeSpawnType(s.map_, s.ms_.mode, s.players_[id].team, s.ms_.attackers, s.tourney_.running);
}

int modeSpawnType(const Map& m, int mode, int team, int attackers, bool tourneyMatch) {
    // (canSupportMode, not supportsMode: whether the map HAS these spawns, not whether its "modes" line offers the
    // mode. Colosseum is built for tourney and doesn't offer vs, so its a/b spawns were never used: REVIEW MAP-7.)
    if (mode == MODE_RACE && m.canSupportMode(MODE_RACE)) return SPAWN_RACE;
    if (mode == MODE_TOURNEY && tourneyMatch && m.canSupportMode(MODE_VS))
        return team == TEAM_B ? SPAWN_B : SPAWN_A; // a match: opposite ends
    if (!modeIsTeamBased(mode)) return SPAWN_ANY;
    bool staged = false;   // (payload maps may stage their spawns by checkpoint, like multi point)
    for (auto& sp : m.spawns) staged |= isStagedSpawn(sp.team);
    if (mode == MODE_PAYLOAD && staged) return stagedSpawn(0, team != attackers);
    if (mode == MODE_POINTS && m.canSupportMode(MODE_POINTS)) // staged: the spawns for how far the attack has got
        return stagedSpawn(0, team != attackers);
    return team == TEAM_B ? SPAWN_B : SPAWN_A;
}

std::vector<const SpawnPoint*> spawnPool(const Map& m, int want, int pointsTaken) {
    std::vector<const SpawnPoint*> cands;
    if (isStagedSpawn(want)) { // multi point: the latest stage reached (points taken) that has spawns for this side
        bool defending = spawnDefends(want);
        int stage = -1;
        for (auto& sp : m.spawns)
            if (isStagedSpawn(sp.team) && spawnDefends(sp.team) == defending && spawnStage(sp.team) <= pointsTaken) stage = std::max(stage, spawnStage(sp.team));
        want = stagedSpawn(std::max(stage, 0), defending);
    }
    for (auto& sp : m.spawns) if (sp.team == want) cands.push_back(&sp);
    if (cands.empty()) for (auto& sp : m.spawns) if (sp.team == SPAWN_ANY) cands.push_back(&sp);
    if (cands.empty()) for (auto& sp : m.spawns) cands.push_back(&sp);
    return cands;
}

void rulesRpsPick(Server& s, int id, int shape) {
    auto& r = s.rps_;
    if (!(s.ms_.flags & MS_RPS) || s.ms_.phase != PHASE_ROUND_END || r.stage != RPS_PICK || r.winner >= 0) return;
    if (id < 0 || id >= kMaxPlayers || !rpsActive(s, id) || shape < RPS_ROCK || shape > RPS_SCISSORS) return;
    bool first = r.pick[id] == RPS_NONE;
    r.pick[id] = (uint8_t)shape;   // (a change of mind is fine until the reveal)
    if (first) rpsBroadcast(s);    // (everyone sees who has chosen, not what)
}
