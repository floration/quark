#include "gamemodes.h"
#include "../common/rng.h"
#include "modeparams.h"
#include "server.h"
#include <cstdlib>

// Layout: the engine helpers (phases, round / match end), the objects (native mechanics with their events), each
// slot's native version (a switch on the mode: the same jump table the old per-mode code compiled to, and the hot
// per-command queries stay inlinable), then the engine's entry points (rulesTick and the rules* calls server.cpp
// makes), which test the ModeHooks before each slot.

namespace {
const ModeParams& P(const Server& s) { return *s.mp_; }

// ---- the hook (phase 3): one null test per slot, predicted not taken
// An event slot through the logic: false if it isn't interpreted (or couldn't run: slots nested too deep), and the
// caller runs the native version. A lifetime slot (reset, matchStart, roundSetup) that runs natively still resets the
// logic's state of that lifetime.
bool hookEvent(Server& s, ModeSlot k, int a = -1, int b = -1, int c = -1) {
    ModeHooks* h = s.modeHooks_;
    if (__builtin_expect(h == nullptr, 1)) return false;
    if (h->has(k)) return h->event(s, k, a, b, c);
    if (k == MSLOT_RESET || k == MSLOT_MATCH_START || k == MSLOT_ROUND_SETUP) h->nativeLifetime(k);
    return false;
}
// A query slot through the logic: false if it isn't interpreted (or couldn't run), and the caller asks the native one.
bool hookQuery(const Server& s, ModeSlot k, double& out, int a = -1, int b = -1, int c = -1) {
    ModeHooks* h = s.modeHooks_;
    return __builtin_expect(h != nullptr, 0) && h->has(k) && h->query(s, k, a, b, c, out);
}

// slot dispatch, for the slots the objects and the engine call from more than one place
void nativeEvent(Server& s, ModeSlot k, int a, int b);
void raise(Server& s, ModeSlot k, int a = -1, int b = -1) {
    if (!hookEvent(s, k, a, b)) nativeEvent(s, k, a, b);
}

int nativeRoundsToWin(const Server& s) { return s.cfg_.roundsToWin > 0 ? s.cfg_.roundsToWin : P(s).rounds; }
int roundsToWin(const Server& s) { double v; return hookQuery(s, MSLOT_ROUNDS_TO_WIN, v) ? (int)v : nativeRoundsToWin(s); }

float nativeRoundLength(const Server& s) {
    if (s.ms_.mode == MODE_FFA) {
        if (s.cfg_.ranked) return P(s).rankedTime;
        return s.cfg_.timeLimit > 0 ? (float)s.cfg_.timeLimit : P(s).matchTime;
    }
    return P(s).roundTime;   // (0: no limit)
}
float roundLength(const Server& s) { double v; return hookQuery(s, MSLOT_ROUND_LENGTH, v) ? (float)v : nativeRoundLength(s); }

float nativeFreezeLength(const Server& s) { return s.ms_.mode == MODE_HIDE ? P(s).hideTime : P(s).freezeTime; }   // hide and seek: the hiders' head start
float freezeLength(const Server& s) { double v; return hookQuery(s, MSLOT_FREEZE_LENGTH, v) ? (float)v : nativeFreezeLength(s); }

int other(int t) { return t == TEAM_A ? TEAM_B : TEAM_A; }
int defenders(const Server& s) { return other(s.ms_.attackers); }

// The clock has run out (call only then): true while overtime holds the round open (the objective was touched in
// the last `overtime` seconds), with the overtime countdown in timeLeft. False: time's up for real.
bool overtime(Server& s) {
    ModeState& m = s.ms_;
    double left = s.objTouch_ + P(s).overtime - s.time();
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
    s.phaseSerial_++;   // (a slot that ended the round or the match: its caller stops, as after a native endRound)
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
    setPhase(s, PHASE_MATCH_END, P(s).matchEndTime);
}

// ---- hide and seek / tag ----
bool inPlay(const Server& s, int i) { const auto& p = s.players_[i]; return p.connected && p.team != TEAM_SPECTATOR && !(s.ms_.mode == MODE_TAG && p.out); }
int playersInPlay(const Server& s) { int n = 0; for (int i = 0; i < kMaxPlayers; i++) n += inPlay(s, i); return n; }

// The "it" object. Hide and seek (pickSeeker): everyone takes a turn seeking, in slot order, each once a match
// (someone who joins late gets a turn too). Tag (pickRandomIt): a random player still in. Both draw one qrand (the
// seeker's draw is unused, kept so the native and the logic's random streams stay the same as before).
void pickIt(Server& s, bool seeker) {
    for (auto& p : s.players_) p.it = false;
    std::vector<int> c;
    for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i) && !(seeker && s.players_[i].sought)) c.push_back(i);
    if (c.empty()) return;
    int pick = c[qrand() % c.size()];
    if (seeker) {
        pick = c[0];
        for (int i : c) if (i > s.lastIt_) { pick = i; break; }
        s.players_[pick].sought = true;
    }
    s.lastIt_ = pick;
    s.players_[pick].it = true;
}
void pickSeeker(Server& s) { pickIt(s, true); }
void pickRandomIt(Server& s) { pickIt(s, false); }

// A round result with no team score (the scores are per player in these modes).
void endRoundText(Server& s, const std::string& text) {
    GameEvent e;
    e.type = EV_ROUND_WIN;
    e.a = 255;
    e.text = text;
    s.broadcastEvent(e);
    s.log(text);
    s.glog(LOG_SIMPLE, "round %d end: %s", s.ms_.round, text.c_str());
    setPhase(s, PHASE_ROUND_END, P(s).roundEndTime);
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
// each hider found (the kill slot); a found hider scores nothing, and neither does a fall (nor the seeker for it).
// Every player plays the same rounds (one seeking, the rest hiding).
void payUnfound(Server& s) { for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i) && !s.players_[i].it && s.players_[i].alive) s.players_[i].score += 1; }

void tickHide(Server& s) {
    int seeker = -1, hiding = 0, found = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!inPlay(s, i)) continue;
        if (s.players_[i].it) seeker = i;
        else if (s.players_[i].alive) hiding++;
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

// ---- the tiebreak object: hide and seek's rock paper scissors (owner B8) ----
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
    r.stageEnd = s.time() + P(s).rpsPickTime;
    s.phaseEnd_ = r.stageEnd;
    for (auto& p : r.pick) p = RPS_NONE;
    for (int i = 0; i < kMaxPlayers; i++)   // bots throw at random
        if (rpsActive(s, i) && s.players_[i].isBot) r.pick[i] = (uint8_t)(RPS_ROCK + qrand() % 3);
    rpsBroadcast(s);
}

// After the last round: true (and the tiebreak is on) when two or more players share the top score and the most
// time survived hiding (T23: that decides first; rock paper scissors decided 4 of 6 bot matches, two of them 5-way
// ties after 15-20 min). Also true when time settled it: the match is over then (the same for the native roundOver
// and the logic's startTiebreak()).
bool rpsStartIfTied(Server& s) {
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
            int w = -1;
            for (int i = 0; i < kMaxPlayers; i++) if (still >> i & 1) w = i;
            s.message("tied for first - " + s.players_[w].name + " stayed hidden longest (" + std::to_string((int)most) + " s)");
            enterMatchEnd(s, s.players_[w].name + " wins the match", w);
            return true;
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
    r.stageEnd = s.time() + P(s).rpsRevealTime;
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
    if (playersInPlay(s) <= 1) {                  // everyone else left: the last one standing wins
        int w = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i)) w = i;
        enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins tag" : "nobody left", w);
        return;
    }
    int it = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i) && s.players_[i].it) it = i;
    if (it < 0) {                                 // "it" left: someone else is it now (same short freeze as a tag)
        pickRandomIt(s);
        for (auto& p : s.players_) if (p.connected && p.it) p.itFreezeUntil = s.time() + P(s).tagFreeze;
        return;
    }
    if (s.time() < s.phaseEnd_) return;
    tagEliminateIt(s, it, "out");                 // time's up: whoever is it is out
}

// ---- the duel modifier object: a new modifier every life, never one already played this match
void pickDuelModifier(Server& s) {
    ModeState& m = s.ms_;
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

// ---- the bomb object's round start: a random living attacker carries it (none alive: nobody)
void giveBombToRandomAttacker(Server& s) {
    ModeState& m = s.ms_;
    std::vector<int> cands;
    for (int i = 0; i < kMaxPlayers; i++)
        if (s.players_[i].connected && s.players_[i].alive && s.players_[i].team == m.attackers) cands.push_back(i);
    if (!cands.empty()) {
        m.bombCarrier = (uint8_t)cands[qrand() % cands.size()];
        m.bombState = BOMB_CARRIED;
    }
}

// The objects' round reset (the carts and the points start over; keyed on the base mode)
void objectsRoundReset(Server& s) {
    ModeState& m = s.ms_;
    if (m.mode == MODE_PAYLOAD) {
        m.payloadDist = 0;
        m.payloadLen = s.map_.payloadLength();
        m.point = 0;   // (checkpoints reached)
        m.pointCount = (uint8_t)s.map_.cartChecks.size();
    } else if (m.mode == MODE_POINTS) { // the attack starts at the first point
        m.point = 0;
        m.capture = 0;
        m.pointCount = (uint8_t)s.map_.points.size();
    } else if (m.mode == MODE_NPAYLOAD) {
        m.payloadLen = s.map_.payloadLength();
        m.payloadDist = m.payloadLen * 0.5f; // starts in the middle; team A pushes to the end, B to the start
    }
}

void nativeRoundSetup(Server& s) {
    ModeState& m = s.ms_;
    switch (m.mode) {
        case MODE_DUEL: pickDuelModifier(s); break;
        case MODE_BOMB: { int half = roundsToWin(s) - 1; m.attackers = (half > 0 && m.round > half) ? TEAM_B : TEAM_A; break; }
        case MODE_PAYLOAD:
        case MODE_POINTS: m.attackers = (m.round % 2 == 1) ? TEAM_A : TEAM_B; break;   // the sides swap every round
        case MODE_HIDE: for (auto& p : s.players_) p.out = false; pickSeeker(s); break;  // (tag: eliminations last the match)
        case MODE_TAG: pickRandomIt(s); break;
        default: break;
    }
}

void nativeRoundStart(Server& s) {
    switch (s.ms_.mode) {
        case MODE_BOMB: giveBombToRandomAttacker(s); break;
        case MODE_HIDE:
            for (auto& p : s.players_) if (p.connected && p.it) s.message(p.name + " is seeking - hide! (" + std::to_string((int)P(s).hideTime) + " s)");
            break;
        case MODE_TAG:
            for (auto& p : s.players_) if (p.connected && p.it) s.message(p.name + " is it - run!");
            break;
        default: break;
    }
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
    s.cartDelivered_ = false;
    for (auto& p : s.players_) {
        p.finished = false;
        p.checkpoint = 0;
        p.respawnAt = -1;
        p.it = false;
        p.found = false;
        p.itFreezeUntil = 0;
    }
    objectsRoundReset(s);
    if (!hookEvent(s, MSLOT_ROUND_SETUP)) nativeRoundSetup(s);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected) continue;
        if (p.team == TEAM_SPECTATOR || (m.mode == MODE_TAG && p.out)) { p.alive = false; continue; }
        s.spawnPlayer(i);
    }
    // Remove leftover projectiles between rounds.
    for (auto& pr : s.projectiles_) pr.active = false;
    float freeze = freezeLength(s);
    s.roundStart_ = s.time() + freeze;
    s.lastProgress_ = s.roundStart_;
    s.payloadIdleSince_ = s.roundStart_;
    setPhase(s, PHASE_FREEZE, freeze);
    if (!hookEvent(s, MSLOT_ROUND_START)) nativeRoundStart(s);
}

// ---- the bracket object (tourney): single elimination, one 1v1 at a time, everyone else spectates ----
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

// A finalist left mid match: walkover, straight to the next match. True when it happened (the tick is over).
bool tourneyWalkover(Server& s) {
    if (!s.tourney_.running) return false;
    auto& t = s.tourney_;
    int a = t.rounds[t.round][2 * t.match], b = t.rounds[t.round][2 * t.match + 1];
    auto in = [&](int i) { return i >= 0 && s.players_[i].connected && s.players_[i].joinId == t.join[i]; };
    if (in(a) && in(b)) return false;
    int w = in(a) ? a : in(b) ? b : -1;
    t.rounds[t.round + 1][t.match] = w;
    t.played++;
    if (w >= 0) s.message(tourneyName(s, w) + " advances (walkover)");
    int champ = tourneyAdvance(s);
    s.broadcastBracket();
    if (champ != -1) { t.running = false; enterMatchEnd(s, champ >= 0 ? tourneyName(s, champ) + " wins the tourney!" : "tourney over", champ); return true; }
    tourneySetupMatch(s);
    startRound(s);
    return true;
}

// Seed the bracket and set up its first match; false: not enough players (the bracket is finished already).
bool tourneyStart(Server& s) {
    tourneyBegin(s);
    s.message("tourney started - " + std::to_string(s.tourney_.rounds[0].size()) + " slot bracket");
    if (tourneyAdvance(s) != -1) return false;
    tourneySetupMatch(s);
    return true;
}

// The match's winning side (TEAM_A / TEAM_B) goes through: the next match is set up, or the tourney ends (true).
bool tourneyMatchWon(Server& s, int side) {
    auto& t = s.tourney_;
    if (!t.running || t.round + 1 >= (int)t.rounds.size() || 2 * t.match + 1 >= (int)t.rounds[t.round].size()) return false;
    int a = t.rounds[t.round][2 * t.match], b = t.rounds[t.round][2 * t.match + 1];
    int w = side == TEAM_A ? a : b;
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
    return false;
}

void nativeMatchStart(Server& s) {
    ModeState& m = s.ms_;
    if (m.mode == MODE_KOTH) m.target = (int16_t)(s.cfg_.scoreLimit > 0 ? s.cfg_.scoreLimit : P(s).target);
    if (m.mode == MODE_TOURNEY && !tourneyStart(s)) enterMatchEnd(s, "not enough players for a tourney", -1);
}

void startMatch(Server& s) {
    s.clearProps();   // (a new match, whatever phase it starts from)
    s.short_ = false;
    s.matchManned_ = false;
    ModeState& m = s.ms_;
    m.round = 0;
    m.scoreA = m.scoreB = 0;
    m.roundsToWin = (uint8_t)roundsToWin(s);
    m.target = 0;
    s.kothPoints_[0] = s.kothPoints_[1] = 0;
    s.bestProgress_[0] = s.bestProgress_[1] = 0;
    s.completeTime_[0] = s.completeTime_[1] = 0;
    s.bestTime_[0] = s.bestTime_[1] = 0;
    for (auto& p : s.players_) { p.kills = p.deaths = p.score = 0; p.dmgDealt = p.objTime = 0; p.hideSurvived = 0; p.shotsFired = p.shotsHit = 0; p.out = false; p.it = false; }
    s.lastIt_ = -1;
    s.tagOut_.clear();   // tag knockouts last one match: everyone is back in for the next
    for (auto& p : s.players_) p.sought = false;
    if (s.modeHooks_) s.glog(LOG_SIMPLE, "match start: mode logic %s, interpreted: %s", s.modeLabel().c_str(), modeHookedSlots(*s.modeHooks_).c_str());
    const uint32_t serial = s.phaseSerial_;
    if (!hookEvent(s, MSLOT_MATCH_START)) nativeMatchStart(s);
    if (s.phaseSerial_ != serial) return;   // (it ended the match: a tourney without enough players)
    if (m.mode != MODE_TOURNEY) s.message(std::string(modeTitle(m.mode)) + " - match started");
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
    // T17: level: whoever got there first (tiebreaker rounds used to add 8-11 min, in 6 of 16 Midtown matches)
    if (s.bestProgress_[0] > 0 && s.bestTime_[0] != s.bestTime_[1]) return s.bestTime_[0] < s.bestTime_[1] ? TEAM_A : TEAM_B;
    return -1;
}
// A new best for the attack: its progress, and when it got there (T17's tiebreak)
void noteBest(Server& s, int att, float v) {
    if (v <= s.bestProgress_[att]) return;
    s.bestProgress_[att] = v;
    s.bestTime_[att] = (float)(s.time() - s.roundStart_);
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
    const float a = s.bestProgress_[att], f = s.bestProgress_[first];   // (level but there sooner: theirs too, T17)
    return a > f || (a == f && a > 0 && s.bestTime_[att] < s.bestTime_[first]) ? att : -1;
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
        // (by when: a level attack is decided on it, T17)
        auto by = [&](int t) { return s.completeTime_[t] > 0 || s.bestProgress_[t] <= 0 ? std::string() : " by " + clockText(s.bestTime_[t]); };
        if (m.round % 2 == 1) { text += by(att) + " - " + std::string(teamName(first)) + " must beat it"; winner = -1; }
        else {
            winner = payloadWinner(s);
            const bool level = s.completeTime_[att] <= 0 && s.completeTime_[first] <= 0 && s.bestProgress_[att] == s.bestProgress_[first];
            if (level && winner >= 0)
                text += by(att) + " - level with " + teamName(first) + "'s " + markText(s, first) + (winner == att ? ", there sooner than its " : ", which got there sooner, by ") +
                        clockText(s.bestTime_[first]);
            else {
                text += winner == att ? " - beats " : winner < 0 ? " - level with " : " - short of ";
                text += std::string(teamName(first)) + "'s " + markText(s, first);
            }
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
    setPhase(s, PHASE_ROUND_END, P(s).roundEndTime);
}

// The roundOver slot, when the round-end screen is over: true if the match is decided (or its tiebreak started).
bool nativeRoundOver(Server& s) {
    ModeState& m = s.ms_;
    int rtw = roundsToWin(s);
    switch (m.mode) {
        case MODE_TOURNEY:   // match decided: the winner advances
            if (m.scoreA >= rtw || m.scoreB >= rtw) return tourneyMatchWon(s, m.scoreA >= rtw ? TEAM_A : TEAM_B);
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
            if (rpsStartIfTied(s)) return true;   // tied for first: time hidden decides, then rock paper scissors (tickRps ends the match)
            { int w = bestScorer(s); enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins the match" : "match over", w); }
            return true;
        case MODE_TAG:
            if (playersInPlay(s) > 1) return false;
            { int w = -1; for (int i = 0; i < kMaxPlayers; i++) if (inPlay(s, i)) w = i; enterMatchEnd(s, w >= 0 ? s.players_[w].name + " wins tag" : "nobody left", w); }
            return true;
        case MODE_PAYLOAD:
        case MODE_POINTS: {   // (multi point: progress = points taken + the capture on the next one)
            if (m.round < 2 || m.round % 2 != 0) return false;
            int w = payloadWinner(s);   // (level on progress: the sooner there, T17; no tiebreaker rounds: dead level is a draw)
            enterMatchEnd(s, w >= 0 ? std::string(teamName(w)) + " wins the match" : "match draw", w);
            return true;
        }
    }
    return true;   // (koth, ffa, neutral payload: no round ends; see 03-possible-bugs: this would sit in round end)
}

bool roundOver(Server& s) {
    const uint32_t serial = s.phaseSerial_;
    if (!hookEvent(s, MSLOT_ROUND_OVER)) return nativeRoundOver(s);
    return s.phaseSerial_ != serial || (s.ms_.flags & MS_RPS);   // (ended the match, or started the tiebreak)
}

bool nativeCanAutoStart(const Server& s) {
    switch (s.ms_.mode) {
        case MODE_RACE: return false;   // race never starts itself: its warmup is practice until someone starts it
        case MODE_FFA:
        case MODE_HIDE:
        case MODE_TAG:
        case MODE_TOURNEY: return s.teamCount(TEAM_A) >= 2;
        default: return s.teamCount(TEAM_A) >= 1 && s.teamCount(TEAM_B) >= 1;
    }
}

// A mode that can't be played on this map at all: multi point with no points (it would never end).
bool unplayable(const Server& s) { return s.ms_.mode == MODE_POINTS && s.map_.points.empty(); }

// Enough players for a match that's on (F5's short-handed hold): canAutoStart, except that a race needs one racer
// (it never starts itself, but one racer can race).
bool enoughPlayers(const Server& s) {
    if (s.ms_.mode == MODE_RACE) return s.teamCount(TEAM_A) >= 1;
    double v;
    return hookQuery(s, MSLOT_CAN_AUTO_START, v) ? v != 0 : nativeCanAutoStart(s);
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

// ---- the drain object (vs, duel, 1v1, tourney): no damage for `stalemate` s, then everyone loses health
void stalemateDrain(Server& s) {
    ModeState& m = s.ms_;
    float idle = s.time() - s.lastProgress_;
    m.timeLeft = std::max(0.0f, P(s).stalemate - idle);
    if (idle < P(s).stalemate) { m.flags &= ~MS_DRAIN; return; }
    if (!(m.flags & MS_DRAIN)) s.message("stalemate - health draining");
    m.flags |= MS_DRAIN;
    s.drainAccum_ += P(s).drainRate * s.dt();
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

void tickVS(Server& s) {
    if (s.ms_.mode == MODE_TOURNEY && tourneyWalkover(s)) return;
    int a = s.aliveCount(TEAM_A), b = s.aliveCount(TEAM_B);
    if (a == 0 || b == 0) {
        endRound(s, a > 0 ? TEAM_A : b > 0 ? TEAM_B : -1, "");
        return;
    }
    stalemateDrain(s);
}

// ---- the bomb object: pickup, return, plant, fuse, defuse, the blast. Raises plant / defuse / explode.
void bombStep(Server& s) {
    ModeState& m = s.ms_;
    const int att = m.attackers, def = defenders(s);
    const float dt = s.dt();
    if (m.bombState == BOMB_PLANTED) m.timeLeft = m.bombTimer;   // (the fuse is the clock, before it ticks down)

    if (m.bombState == BOMB_DROPPED) {
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = s.players_[i];
            if (p.connected && p.alive && p.team == att && length(p.move.pos - m.bombPos) < P(s).pickupRange) {
                m.bombState = BOMB_CARRIED;
                m.bombCarrier = (uint8_t)i;
                break;
            }
        }
    }
    if (m.bombState == BOMB_DROPPED && s.time() - s.bombDroppedAt_ >= P(s).bombReturn) {   // out of reach, or nobody went for it
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
            m.actionProgress += dt / P(s).plantTime;
            if (m.actionProgress >= 1.0f) {
                m.bombState = BOMB_PLANTED;
                m.bombPos = p.move.pos;
                m.bombTimer = P(s).bombFuse;
                m.bombCarrier = 255;
                m.actionPlayer = 255;
                m.actionProgress = 0;
                s.lastProgress_ = s.time();
                raise(s, MSLOT_PLANT, c);
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
            if (p.connected && p.alive && (p.buttons & BTN_USE) && length(p.move.pos - m.bombPos) < P(s).defuseRange) defuser = m.actionPlayer;
        }
        if (defuser < 0) {
            m.actionProgress = 0;
            m.actionPlayer = 255;
            for (int i = 0; i < kMaxPlayers; i++) {
                auto& p = s.players_[i];
                if (p.connected && p.alive && p.team == def && (p.buttons & BTN_USE) && length(p.move.pos - m.bombPos) < P(s).defuseRange) {
                    defuser = i;
                    m.actionPlayer = (uint8_t)i;
                    break;
                }
            }
        }
        if (defuser >= 0) {
            m.actionProgress += dt / P(s).defuseTime;
            if (m.actionProgress >= 1.0f) {
                m.bombState = BOMB_DEFUSED;
                raise(s, MSLOT_DEFUSE, defuser);
                return;
            }
        }
        if (m.bombTimer <= 0) {
            m.bombState = BOMB_EXPLODED;
            for (int i = 0; i < kMaxPlayers; i++) {
                auto& p = s.players_[i];
                if (p.connected && p.alive && length(p.move.pos - m.bombPos) < P(s).bombRadius) s.killPlayer(i, -1, 253);
            }
            if (m.phase != PHASE_LIVE) return;   // (a kill slot ended the round)
            raise(s, MSLOT_EXPLODE);
            return;
        }
    }
}

void tickBomb(Server& s) {
    ModeState& m = s.ms_;
    bombStep(s);
    if (m.phase != PHASE_LIVE) return;
    const int att = m.attackers, def = defenders(s);
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

// ---- the race object: checkpoints in order, splits, time trial runs. Raises checkpoint / finish.
void raceStep(Server& s) {
    const auto& cps = s.map_.checkpoints;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || p.finished) continue;
        if (p.checkpoint < (int)cps.size() && feetIn(s, i, cps[p.checkpoint])) {
            p.checkpoint++;
            GameEvent e; // the split: race time at this checkpoint, to the ms (time trials log these)
            e.type = EV_RACE_SPLIT; e.a = (uint8_t)i; e.b = (uint8_t)p.checkpoint;
            long long ms = std::lround((s.liveLength_ - (s.phaseEnd_ - s.time())) * 1000.0);
            e.text = std::to_string(ms);
            s.broadcastEvent(e);
            if (p.checkpoint == 1) p.raceSplits.clear();
            if ((int)p.raceSplits.size() == p.checkpoint - 1) p.raceSplits.push_back((int)ms);
            else p.raceSplits.clear(); // (joined mid-run: this one doesn't count)
            if (hookEvent(s, MSLOT_CHECKPOINT, i, p.checkpoint) && s.ms_.phase != PHASE_LIVE) return;   // (no native version)
            if (p.checkpoint == (int)cps.size()) {
                p.finished = true;
                if ((int)p.raceSplits.size() == p.checkpoint) s.finishRaceRun(i);
                raise(s, MSLOT_FINISH, i);
                return;
            }
        }
    }
}

void tickRace(Server& s) {
    raceStep(s);
    if (s.ms_.phase != PHASE_LIVE) return;
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

// ---- the cart object (payload): occupancy, push, contest, rollback, checkpoints, objective time and touches, best
// progress. Raises cartCheckpoint / delivered.
void cartStep(Server& s) {
    ModeState& m = s.ms_;
    const int att = m.attackers, def = defenders(s);
    const float range = P(s).cartRange, speed0 = P(s).cartSpeed;
    Vec3 cart = s.map_.cartPoint(m.mode, m.attackers, m.payloadDist);
    int na = 0, nd = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive) continue;
        if (length(p.move.pos - cart) >= range) continue;
        if (p.team == att) na++;
        else if (p.team == def) nd++;
    }
    s.cartNear_[att] = na;
    s.cartNear_[def] = nd;
    // objective time: attackers by the cart; defenders only while blocking it (an attacker is there too)
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.alive && length(p.move.pos - cart) < range && (p.team == att || (p.team == def && na > 0))) p.objTime += s.dt();
    }
    m.flags &= ~MS_CONTESTED;
    if (na > 0) s.objTouch_ = s.time();   // (overtime: an attacker at the cart, contested or not)
    if (na > 0 && nd == 0) {
        float speed = std::min(speed0 * (1.0f + 0.25f * (na - 1)), speed0 * 2.0f);
        m.payloadDist = std::min(m.payloadDist + speed * s.dt(), m.payloadLen);
        s.payloadIdleSince_ = s.time();
    } else if (na > 0 && nd > 0) {
        m.flags |= MS_CONTESTED;
        s.payloadIdleSince_ = s.time();
    } else if (na == 0 && s.time() - s.payloadIdleSince_ > P(s).cartIdle) {
        float floor = m.point > 0 && m.point <= s.map_.cartChecks.size() ? s.map_.cartChecks[m.point - 1] * m.payloadLen : 0.0f;
        m.payloadDist = std::max(floor, m.payloadDist - P(s).cartRollback * s.dt());   // (never back past a checkpoint)
    }
    // checkpoints: the cart stays past it, and the spawns move up a stage (staged maps); the slot adds time
    while (m.point < s.map_.cartChecks.size() && m.payloadDist >= s.map_.cartChecks[m.point] * m.payloadLen) {
        m.point++;
        raise(s, MSLOT_CART_CHECKPOINT, m.point);
        if (m.phase != PHASE_LIVE) return;
    }
    noteBest(s, att, m.payloadDist);
    // (once a round: a delivered slot that doesn't end the round isn't raised again every tick the cart sits there)
    if (m.payloadDist >= m.payloadLen - 1e-3f && !s.cartDelivered_) { s.cartDelivered_ = true; raise(s, MSLOT_DELIVERED); }
}

void tickPayload(Server& s) {
    ModeState& m = s.ms_;
    cartStep(s);
    if (m.phase != PHASE_LIVE) return;
    if (int w = stopwatchLocked(s); w >= 0) { endRound(s, w, "", false); return; }   // (T15: the result is locked)
    if (s.time() < s.phaseEnd_) m.flags &= ~MS_OVERTIME;
    else if (!overtime(s)) endRound(s, defenders(s), "time", false);
}

// ---- the points object (multi point): the attackers take the map's points one at a time, in order. Only they
// capture: standing on the point with no defender on it fills it (more attackers fill it faster); a defender on it
// holds it; left alone it slowly drains. Raises capture (the slot adds time, or ends the round on the last point).
// True: the tick ends here (a point was taken, or there's none left).
bool pointStep(Server& s) {
    ModeState& m = s.ms_;
    const int att = m.attackers, def = defenders(s);
    m.pointCount = (uint8_t)s.map_.points.size();
    if (m.point >= m.pointCount) return true;
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
        m.capture += std::min(1.0f + 0.5f * (na - 1), 2.0f) * s.dt() / P(s).captureTime;
        m.hillOwner = (uint8_t)att;
    } else if (na > 0) m.flags |= MS_CONTESTED;
    else m.capture = std::max(0.0f, m.capture - P(s).captureDecay * s.dt());
    if (m.capture >= 1.0f) {
        m.capture = 0;
        m.point++;
        noteBest(s, att, (float)m.point);
        raise(s, MSLOT_CAPTURE, m.point);   // (the points taken now: 1 = the first)
        return true;
    }
    noteBest(s, att, m.point + m.capture * 0.99f);
    return false;
}

void tickPoints(Server& s) {
    ModeState& m = s.ms_;
    if (pointStep(s)) return;
    if (int w = stopwatchLocked(s); w >= 0) { endRound(s, w, "", false); return; }   // (T15: the result is locked)
    if (s.time() < s.phaseEnd_) m.flags &= ~MS_OVERTIME;
    else if (!overtime(s)) endRound(s, defenders(s), "time", false);   // (overtime: the attack goes on while it's on the point)
}

// ---- the neutral cart object: whichever team is alone at the cart pushes it toward the far end of the track (team A
// toward the end, team B toward the start), and the team that would lose at time-out touching it holds overtime.
void neutralCartStep(Server& s, bool touch) {
    ModeState& m = s.ms_;
    const float range = P(s).cartRange, speed0 = P(s).cartSpeed;
    Vec3 cart = s.map_.cartPoint(m.mode, m.attackers, m.payloadDist);
    int na = 0, nb = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || length(p.move.pos - cart) >= range) continue;
        p.objTime += s.dt();
        if (p.team == TEAM_A) na++;
        else if (p.team == TEAM_B) nb++;
    }
    s.cartNear_[TEAM_A] = na;
    s.cartNear_[TEAM_B] = nb;
    m.flags &= ~MS_CONTESTED;
    m.hillOwner = 255; // reused: which team is moving the cart
    if (na > 0 && nb > 0) m.flags |= MS_CONTESTED;
    else if (na > 0 || nb > 0) {
        int n = na > 0 ? na : nb;
        float speed = std::min(speed0 * (1.0f + 0.25f * (n - 1)), speed0 * 2.0f);
        m.payloadDist = clampf(m.payloadDist + (na > 0 ? speed : -speed) * s.dt(), 0, m.payloadLen);
        m.hillOwner = (uint8_t)(na > 0 ? TEAM_A : TEAM_B);
    }
    if (touch) {   // overtime: the team that would lose at time-out (the cart on the other side) is at the cart (the
                   // logic's neutralCartStep() leaves this to the tick slot: touchObjective)
        float mid = m.payloadLen * 0.5f;
        bool even = std::fabs(m.payloadDist - mid) < 0.05f;
        if ((na > 0 && (even || m.payloadDist < mid)) || (nb > 0 && (even || m.payloadDist > mid))) s.objTouch_ = s.time();
    }
}

// Neutral payload: reaching an end wins; at time-out the side of the track the cart is on wins.
void tickNeutralPayload(Server& s) {
    ModeState& m = s.ms_;
    neutralCartStep(s, true);
    if (m.payloadDist >= m.payloadLen - 1e-3f) { enterMatchEnd(s, "TEAM A delivered the payload", TEAM_A); return; }
    if (m.payloadDist <= 1e-3f) { enterMatchEnd(s, "TEAM B delivered the payload", TEAM_B); return; }
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
    if (s.cfg_.scoreLimit > 0)
        for (auto& p : s.players_)
            if (p.connected && p.score >= s.cfg_.scoreLimit) { finishFFA(s); return; }
    if (s.time() >= s.phaseEnd_) finishFFA(s);
}

void nativeTick(Server& s) {
    switch (s.ms_.mode) {
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
}

// The objects' event slots (raised from their steps), native.
void nativeEvent(Server& s, ModeSlot k, int a, int b) {
    (void)b;
    ModeState& m = s.ms_;
    switch (k) {
        case MSLOT_PLANT: s.message("bomb planted"); break;
        case MSLOT_DEFUSE: endRound(s, defenders(s), "bomb defused"); break;
        case MSLOT_EXPLODE: endRound(s, m.attackers, "bomb exploded"); break;
        case MSLOT_CART_CHECKPOINT: {   // more time for the attack
            const float bonus = P(s).cartCheckBonus;
            s.phaseEnd_ = std::max(s.phaseEnd_, s.time()) + bonus;   // (in overtime the clock is past: from now)
            s.message(std::string(teamName(m.attackers)) + " reached checkpoint " + std::to_string(a) + " - +" + std::to_string((int)bonus) + " s");
            break;
        }
        case MSLOT_DELIVERED:
            s.completeTime_[m.attackers] = s.time() - s.roundStart_;
            endRound(s, m.attackers, "payload delivered");
            break;
        case MSLOT_CAPTURE: {   // a = points taken now: on to the next (+time, the spawns move up), or the round is won
            a--;                // (the index of the point just taken)
            if (m.point >= m.pointCount) {
                s.completeTime_[m.attackers] = s.time() - s.roundStart_;
                endRound(s, m.attackers, "every point taken");
                break;
            }
            const float bonus = P(s).pointBonus;
            s.phaseEnd_ = std::max(s.phaseEnd_, s.time()) + bonus;   // (in overtime the clock is past: from now)
            s.message(std::string(teamName(m.attackers)) + " took point " + char('A' + a) + " - +" + std::to_string((int)bonus) + " s, on to point " + char('A' + m.point));
            s.glog(LOG_SIMPLE, "point %d taken by team %d", m.point, m.attackers);
            break;
        }
        case MSLOT_FINISH: endRound(s, a, ""); break;
        default: break;   // (checkpoint: nothing native)
    }
}

// The kill slot, native: frags and teamkills (hide and seek: finds), suicides in free for all, free for all overtime.
void nativeKill(Server& s, int victim, int killer) {
    ModeState& m = s.ms_;
    ServerPlayer& v = s.players_[victim];
    if (killer >= 0 && killer != victim) {
        ServerPlayer& k = s.players_[killer];
        bool teamkill = modeIsTeamBased(s.cfg_.mode) && k.team == v.team;
        if (teamkill) k.score--;
        else k.score += m.mode == MODE_HIDE ? 0 : 1;   // (hide and seek: 1 a find, below)
    } else if (s.cfg_.mode == MODE_FFA && m.phase == PHASE_LIVE) {
        v.score--; // suicide
    }
    if (m.mode == MODE_HIDE && m.phase == PHASE_LIVE && !v.it)   // a hider is out: found scores the seeker 1, a fall nothing
        if (killer >= 0 && s.players_[killer].it && v.found) s.players_[killer].score += 1;
    if (m.mode == MODE_FFA && m.phase == PHASE_LIVE && (m.flags & MS_OVERTIME) && killer >= 0 && killer != victim)
        finishFFA(s);
}

// The objects' death hooks (a death or a leave): the bomb carrier drops it, whoever was planting / defusing stops.
void objectsOnDeath(Server& s, int victim) {
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
}

// Tag (REVIEW GAME-1): leaving or going to spectate during a match is elimination. "It" doing it ends the round like
// time running out (it used to hand "it" to someone else and come back in later, never out).
void nativeSpectate(Server& s, int id) {
    if (!tagMatchOn(s)) return;
    auto& p = s.players_[id];
    if (!p.connected || p.team == TEAM_SPECTATOR || p.out) return;
    if (p.it && s.ms_.phase != PHASE_ROUND_END) tagEliminateIt(s, id, "out");
    else tagOut(s, id);
}

bool nativeCanDamage(const Server& s, int attacker, int victim) {
    if (s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END || s.ms_.phase == PHASE_FREEZE) return false;
    if (s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP) return false; // race: knockback only
    if (attacker < 0 || attacker == victim) return true;
    if (modeIsTeamBased(s.ms_.mode) && s.players_[attacker].team == s.players_[victim].team) return false;
    return true;
}

bool nativeCanHit(const Server& s, int attacker, int victim, int weapon) {
    if (attacker < 0 || attacker == victim) return true;
    // Race: you can only affect yourself (rocket jumps etc.), never other racers.
    if (s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP) return false;
    // hide and seek / tag: only "it" can touch anyone, with the knife, once the round is live.
    if (isItMode(s.ms_.mode)) return s.ms_.phase == PHASE_LIVE && s.players_[attacker].it && !s.players_[victim].it && weapon == WPN_KNIFE;
    return true;
}

// Hide and seek: one stab finds (kills) a hider. Tag: the stab passes "it" on.
void nativeStab(Server& s, int attacker, int victim) {
    ServerPlayer& a = s.players_[attacker];
    ServerPlayer& v = s.players_[victim];
    if (s.ms_.mode == MODE_TAG) {
        a.it = false;
        v.it = true;
        v.weapon = WPN_KNIFE;   // (the new tagger has the knife out; the old one holds nothing now)
        v.locateAt = 0;
        v.itFreezeUntil = s.time() + P(s).tagFreeze;
        a.score++;
        s.message(v.name + " is it");
        s.glog(LOG_SIMPLE, "tag %d -> %d", attacker, victim);
        return;
    }
    v.found = true;
    s.killPlayer(victim, attacker, WPN_KNIFE); // found
}

bool nativeFrozen(const Server& s, int id) {
    if (s.ms_.mode == MODE_HIDE && s.ms_.phase == PHASE_FREEZE) return s.players_[id].it; // hiders hide, the seeker faces the wall
    if (s.ms_.mode == MODE_TAG && s.ms_.phase == PHASE_LIVE) return s.players_[id].it && s.time() < s.players_[id].itFreezeUntil;
    if (s.ms_.phase == PHASE_FREEZE) return true;
    if (s.ms_.mode == MODE_BOMB && s.ms_.actionPlayer == id && s.ms_.phase == PHASE_LIVE) return true;
    return false;
}

} // namespace

// Tourney warmup is a free for all; during matches the two players are on teams A and B.
bool modeIsTeamBased(int mode) { return mode != MODE_FFA && mode != MODE_RACE && mode != MODE_TOURNEY && !isItMode(mode); }

void rulesReset(Server& s) {
    s.clearProps();   // (setPhase below can't tell: the mode state is reset to warmup first)
    ModeState& m = s.ms_;
    m = ModeState();
    m.mode = (uint8_t)s.cfg_.mode;
    // A custom mode's logic plays only its own base mode, and never ranked or a time trial (a vote or the console may
    // have changed either since the server started: then the native rules play, by the built-in numbers).
    const ModeLogic* L = s.modeLogic_.get();
    if (s.modeLogicHooks_) s.modeHooks_ = L && L->base == m.mode && !s.cfg_.ranked && !s.cfg_.timeTrial ? s.modeLogicHooks_.get() : nullptr;   // (tests may set modeHooks_ themselves)
    const ModeHooks* h = s.modeHooks_;
    s.mp_ = h && h->params ? h->params : &builtinModeParams(m.mode);   // (the mode's numbers: the custom mode's, or the built-in ones)
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
    hookEvent(s, MSLOT_RESET);   // (no native reset slot)
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
            // Warmup lasts until players vote to start (F4). Play-vs-bots / autostart servers start by themselves
            // once both sides have someone (canAutoStart). Race never starts itself: its warmup is practice (the
            // course, untimed) until someone starts the race.
            if (!hookEvent(s, MSLOT_WARMUP) && m.mode == MODE_RACE) tickRacePractice(s);
            if (m.phase != PHASE_WARMUP) break;
            double v;
            if (!s.cfg_.autoStart || unplayable(s) || !(hookQuery(s, MSLOT_CAN_AUTO_START, v) ? v != 0 : nativeCanAutoStart(s))) { s.phaseEnd_ = 0; m.timeLeft = 0; break; }
            if (s.phaseEnd_ == 0) {
                s.phaseEnd_ = s.time() + P(s).warmupCountdown;
                s.message("match starting");
            }
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) startMatch(s);
            break;
        case PHASE_FREEZE:
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) {
                float rt = roundLength(s);
                setPhase(s, PHASE_LIVE, rt);
                s.liveLength_ = rt;
                s.lastProgress_ = s.time();
                s.roundStart_ = s.time();   // (the stopwatch times run from here, even if the freeze was cut short)
                hookEvent(s, MSLOT_LIVE);   // (no native live slot)
            }
            break;
        case PHASE_LIVE:
            if (s.phaseEnd_ > 0) m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());   // (the round clock; a slot may show another)
            if (unplayable(s)) { enterMatchEnd(s, "this map has no points - multi point can't be played here", -1); break; }   // (F23: it never ended)
            if (m.mode == MODE_HIDE)   // the time tiebreak (T23): seconds hidden, alive and not seeking (the engine counts it for the logic too)
                for (int i = 0; i < kMaxPlayers; i++)
                    if (inPlay(s, i) && !s.players_[i].it && s.players_[i].alive) s.players_[i].hideSurvived += s.dt();
            if (hookEvent(s, MSLOT_TIMER) && m.phase != PHASE_LIVE) break;   // (the timers that ran out: no native version)
            if (!hookEvent(s, MSLOT_TICK)) nativeTick(s);
            if (m.phase == PHASE_LIVE && s.phaseEnd_ > 0 && s.time() >= s.phaseEnd_) hookEvent(s, MSLOT_TIME_UP);
            break;
        case PHASE_ROUND_END:
            if (m.flags & MS_RPS) { tickRps(s); break; }   // hide and seek: the tiebreak after the last round
            m.timeLeft = (float)std::max(0.0, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_ && !roundOver(s)) startRound(s);
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

void rulesOnDeath(Server& s, int victim, int killer, int weapon) {
    objectsOnDeath(s, victim);
    if (!hookEvent(s, MSLOT_KILL, victim, killer, weapon)) nativeKill(s, victim, killer);
}

void rulesOnLeave(Server& s, int id) {
    if (!hookEvent(s, MSLOT_LEAVE, id)) nativeSpectate(s, id);   // (tag: leaving is like going to spectate)
    if (s.ms_.mode == MODE_HIDE && !s.players_[id].alive) return;   // (already out: scored when found)
    objectsOnDeath(s, id);
}

void rulesOnSpectate(Server& s, int id) {
    if (!hookEvent(s, MSLOT_SPECTATE, id)) nativeSpectate(s, id);
}

bool rulesTeamLocked(const Server& s) { double v; return hookQuery(s, MSLOT_TEAM_LOCKED, v) ? v != 0 : tagMatchOn(s); }

void rulesEndMatch(Server& s, const std::string& text, int winner) {
    if (s.ms_.phase == PHASE_MATCH_END || s.ms_.phase == PHASE_WARMUP) return;
    s.tourney_.running = false;
    enterMatchEnd(s, text, winner);
}

namespace {
bool stagedSpawns(const Map& map) { for (auto& sp : map.spawns) if (isStagedSpawn(sp.team)) return true; return false; }
}

float rulesRespawnDelay(const Server& s, int id) {
    if (s.ms_.phase == PHASE_WARMUP) return P(s).warmupRespawn;
    if (s.ms_.phase == PHASE_MATCH_END) return -1.0f;
    double v;
    if (hookQuery(s, MSLOT_RESPAWN_DELAY, v, id)) return (float)v;
    // hide and seek: a seeker who falls (or dies) is back after kSeekerRespawn; it used to stay dead and the round
    // idled out its 300 s with every hider scoring (owner's pick, F6; hide.qlogic's respawnDelay says the same)
    if (s.ms_.mode == MODE_HIDE && id >= 0 && id < kMaxPlayers && s.players_[id].it && (s.ms_.phase == PHASE_LIVE || s.ms_.phase == PHASE_FREEZE))
        return kSeekerRespawn;
    // payload attackers past the last checkpoint, on a map whose spawns don't move up, are back after kLastLegRespawn,
    // not 5 s (T18: on Railyard's last leg they spawned 55 m from the cart, the defenders 37 m, and 3 of 40 bot rounds
    // were delivered; payload.qlogic's respawnDelay says the same)
    const ModeState& m = s.ms_;
    if (m.mode == MODE_PAYLOAD && m.phase == PHASE_LIVE && id >= 0 && id < kMaxPlayers && s.players_[id].team == m.attackers &&
        !s.map_.cartChecks.empty() && m.point >= s.map_.cartChecks.size() && !stagedSpawns(s.map_))
        return std::min(P(s).respawnTime, kLastLegRespawn);
    return P(s).respawnTime;
}

bool rulesCanDamage(const Server& s, int attacker, int victim, int weapon) {
    double v;
    return hookQuery(s, MSLOT_CAN_DAMAGE, v, attacker, victim, weapon) ? v != 0 : nativeCanDamage(s, attacker, victim);
}

bool rulesCanHit(const Server& s, int attacker, int victim, int weapon) {
    double v;
    return hookQuery(s, MSLOT_CAN_HIT, v, attacker, victim, weapon) ? v != 0 : nativeCanHit(s, attacker, victim, weapon);
}

void rulesStab(Server& s, int attacker, int victim) {
    if (!hookEvent(s, MSLOT_STAB, attacker, victim)) nativeStab(s, attacker, victim);
}

// (asked per command, as the native one is: a once-a-tick mask would lag a tag's freeze or a planter's lock by a tick)
bool rulesFrozen(const Server& s, int id) {
    double v;
    return hookQuery(s, MSLOT_FROZEN, v, id) ? v != 0 : nativeFrozen(s, id);
}

int rulesSpawnType(const Server& s, int id) {
    double v;
    if (hookQuery(s, MSLOT_SPAWN_TYPE, v, id)) return (int)v;
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

// =====================================================================================================================
// A custom mode's logic on the server (mode editor phase 3): ModeHooks for the engine, ModeHost for the interpreter.
// Each interpreted slot runs runModeSlot with this as its host; the fields, actions and objects below are the same
// engine state and native mechanics the native slots above use, so the 13 built-in logics forced through the
// interpreter play exactly as the native code (tests/mode_tests.cpp, lockstep).
// =====================================================================================================================
namespace {
class LogicHooks final : public ModeHooks, public ModeHost {
public:
    explicit LogicHooks(std::shared_ptr<const ModeLogic> logic) : L_(std::move(logic)) {
        const ModeLogic& L = *L_;
        for (int k = 0; k < MSLOT_COUNT; k++) if (L.interp[k] && L.start[k] >= 0) interp |= 1ull << k;
        if (interp >> MSLOT_TIMER) interp |= 1ull << MSLOT_TIMER;   // (bit MSLOT_TIMER: some timer slot; event() runs them all)
        // the native slots' numbers: the base mode's, with every param the logic declares
        mp_ = builtinModeParams(L.base);
        for (const ModeParamValue& pv : L.params) {
#define QUARK_MP_SET(nm, ctype, ltype, unit, lo, hi, def, doc) if (pv.name == #nm) mp_.nm = (ctype)pv.value;
            QUARK_MODE_PARAMS(QUARK_MP_SET)
#undef QUARK_MP_SET
        }
        params = &mp_;
        initModeState(L, st_);
    }

    // ---- ModeHooks
    bool event(Server& s, ModeSlot slot, int a, int b, int c) override {
        Server* prevS = s_;
        bool prevRo = readOnly_;
        s_ = &s;
        readOnly_ = false;
        bool ran = true;
        if (slot == MSLOT_TIMER) runModeTimers(*L_, st_, *this);
        else { ModeArgs args; args.v[0] = a; args.v[1] = b; args.v[2] = c; ran = runModeSlot(*L_, st_, *this, slot, args); }
        s_ = prevS;
        readOnly_ = prevRo;
        return ran;
    }
    bool query(const Server& s, ModeSlot slot, int a, int b, int c, double& out) override {
        Server* prevS = s_;
        bool prevRo = readOnly_;
        s_ = const_cast<Server*>(&s);   // (read-only: set / act / say do nothing in a query)
        readOnly_ = true;
        ModeArgs args; args.v[0] = a; args.v[1] = b; args.v[2] = c;
        bool ran = runModeSlot(*L_, st_, *this, slot, args, &out);
        s_ = prevS;
        readOnly_ = prevRo;
        return ran;
    }
    void nativeLifetime(ModeSlot slot) override {
        if (slot == MSLOT_RESET || slot == MSLOT_MATCH_START) resetModeState(*L_, st_, 0);
        else if (slot == MSLOT_ROUND_SETUP) resetModeState(*L_, st_, 1);
    }

    // ---- ModeHost
    double get(int f, int a, int b) override {
        const Server& s = *s_;
        const ModeState& m = s.ms_;
        auto team = [](int t) { return t == TEAM_A || t == TEAM_B; };
        auto id = [](uint8_t v) { return v == 255 ? -1.0 : (double)v; };
        switch (f) {
            case MF_NOW: return s.time();
            case MF_DT: return s.dt();
            case MF_PHASE: return m.phase;
            case MF_ROUND_NO: return m.round;
            case MF_PHASE_END: return s.phaseEnd_;
            case MF_LIVE_SINCE: return s.liveSince_;
            case MF_ROUND_START_TIME: return s.roundStart_;
            case MF_LAST_PROGRESS: return s.lastProgress_;
            case MF_TEAMS: return modeIsTeamBased(m.mode);
            case MF_RANKED: return s.cfg_.ranked;
            case MF_TICK_RATE: return s.tickRate();
            case MF_CFG_TIME_LIMIT: return s.cfg_.timeLimit;
            case MF_CFG_SCORE_LIMIT: return s.cfg_.scoreLimit;
            case MF_CFG_ROUNDS_TO_WIN: return s.cfg_.roundsToWin;
            case MF_HAS_HILL: return s.map_.hasHill;
            case MF_HAS_CART: return s.map_.payloadPath.size() >= 2;
            case MF_SITE_COUNT: return (double)s.map_.bombSites.size();
            case MF_POINT_COUNT: return (double)s.map_.points.size();
            case MF_CHECKPOINT_COUNT: return (double)s.map_.checkpoints.size();
            case MF_ATTACKERS: return m.attackers;
            case MF_HUD_CLOCK: return m.timeLeft;
            case MF_HUD_OVERTIME: return (m.flags & MS_OVERTIME) != 0;
            case MF_HUD_CONTESTED: return (m.flags & MS_CONTESTED) != 0;
            case MF_HUD_DRAIN: return (m.flags & MS_DRAIN) != 0;
            case MF_HUD_TARGET: return m.target;
            case MF_HUD_OWNER: return id(m.hillOwner);
            case MF_HUD_ROUNDS_TO_WIN: return m.roundsToWin;
            case MF_BOMB_STATE: return m.bombState;
            case MF_BOMB_CARRIER: return id(m.bombCarrier);
            case MF_BOMB_ACTING: return id(m.actionPlayer);
            case MF_BOMB_TIMER: return m.bombTimer;
            case MF_CART_DIST: return m.payloadDist;
            case MF_CART_LENGTH: return m.payloadLen;
            case MF_CART_CHECKPOINT: return m.point;
            case MF_CART_CHECKPOINT_COUNT: return (double)s.map_.cartChecks.size();
            case MF_STAGED_SPAWNS: return stagedSpawns(s.map_);
            case MF_POINT_INDEX: return m.point;
            case MF_POINT_CAPTURE: return m.capture;
            // per team (out of range: 0)
            case MF_ALIVE_COUNT: return team(a) ? s.aliveCount(a) : 0;
            case MF_TEAM_COUNT: return team(a) ? s.teamCount(a) : 0;
            case MF_TEAM_SCORE: return team(a) ? (a == TEAM_A ? m.scoreA : m.scoreB) : 0;
            case MF_CART_BEST: return team(a) ? s.bestProgress_[a] : 0;
            case MF_CART_COMPLETED: return team(a) ? s.completeTime_[a] : 0;
            case MF_CART_NEAR: return team(a) ? s.cartNear_[a] : 0;
            case MF_CART_BEST_TIME: return team(a) ? s.bestTime_[a] : 0;
            default: break;
        }
        // per player (out of range: 0; team: none)
        if ((unsigned)a >= (unsigned)kMaxPlayers) return f == MF_TEAM ? -1 : 0;
        const ServerPlayer& p = s.players_[a];
        switch (f) {
            case MF_ALIVE: return p.alive;
            case MF_TEAM: return p.team;
            case MF_IS_BOT: return p.isBot;
            case MF_ON_GROUND: return (p.move.flags & MF_ONGROUND) != 0;
            case MF_USE_HELD: return (p.buttons & BTN_USE) != 0;
            case MF_HEALTH: return p.health;
            case MF_NEAR_CART:
                return s.map_.payloadPath.size() >= 2 && length(p.move.pos - s.map_.cartPoint(m.mode, m.attackers, m.payloadDist)) < P(s).cartRange;
            case MF_MAP_SPAWN: return modeSpawnType(s.map_, m.mode, p.team, m.attackers, s.tourney_.running);
            case MF_SCORE: return p.score;
            case MF_OBJ_TIME: return p.objTime;
            case MF_IT: return p.it;
            case MF_OUT: return p.out;
            case MF_FOUND: return p.found;
            case MF_SOUGHT: return p.sought;
            case MF_KILLS: return p.kills;
            case MF_DEATHS: return p.deaths;
            case MF_FREEZE_UNTIL: return p.itFreezeUntil;
            case MF_RACE_CHECKPOINT: return p.checkpoint;
            case MF_RACE_FINISHED: return p.finished;
            case MF_IN_ZONE: {
                const Map& mp = s.map_;
                int k = modeZoneIndex(b);
                switch (modeZoneKind(b)) {
                    case MZ_HILL: return mp.hasHill && feetIn(s, a, mp.hill);
                    case MZ_SITES: for (auto& z : mp.bombSites) if (feetIn(s, a, z)) return 1; return 0;
                    case MZ_SITE: return k < (int)mp.bombSites.size() && feetIn(s, a, mp.bombSites[k]);
                    case MZ_POINT: return k < (int)mp.points.size() && feetIn(s, a, mp.points[k]);
                    case MZ_CHECKPOINT: return k < (int)mp.checkpoints.size() && feetIn(s, a, mp.checkpoints[k]);
                }
                return 0;
            }
            default: return 0;
        }
    }

    void set(int f, int a, double v) override {
        if (readOnly_) return;
        Server& s = *s_;
        ModeState& m = s.ms_;
        auto team = [](int t) { return t == TEAM_A || t == TEAM_B; };
        auto i16 = [](double x) { return (int16_t)std::max(-32768.0, std::min(32767.0, x)); };
        auto flag = [&](uint8_t bit) { if (v != 0) m.flags |= bit; else m.flags &= (uint8_t)~bit; };
        switch (f) {
            case MF_ATTACKERS: if (team((int)v)) m.attackers = (uint8_t)v; return;
            case MF_HUD_CLOCK: m.timeLeft = (float)v; return;
            case MF_HUD_OVERTIME: flag(MS_OVERTIME); return;
            case MF_HUD_CONTESTED: flag(MS_CONTESTED); return;
            case MF_HUD_DRAIN: flag(MS_DRAIN); return;
            case MF_HUD_TARGET: m.target = i16(v); return;
            case MF_HUD_OWNER: m.hillOwner = team((int)v) ? (uint8_t)v : 255; return;
            case MF_TEAM_SCORE: if (team(a)) (a == TEAM_A ? m.scoreA : m.scoreB) = i16(v); return;
            case MF_CART_BEST: if (team(a)) s.bestProgress_[a] = (float)v; return;
            case MF_CART_COMPLETED: if (team(a)) s.completeTime_[a] = (float)v; return;
            default: break;
        }
        if ((unsigned)a >= (unsigned)kMaxPlayers) return;
        ServerPlayer& p = s.players_[a];
        switch (f) {
            case MF_SCORE: p.score = (int)v; return;
            case MF_OBJ_TIME: p.objTime = (float)v; return;
            case MF_IT: p.it = v != 0; return;
            case MF_OUT: p.out = v != 0; return;
            case MF_FOUND: p.found = v != 0; return;
            case MF_SOUGHT: p.sought = v != 0; return;
            default: return;
        }
    }

    uint32_t players(int set, int t) override {
        const Server& s = *s_;
        uint32_t mask = 0;
        for (int i = 0; i < kMaxPlayers; i++) {
            const ServerPlayer& p = s.players_[i];
            if (!p.connected) continue;
            if (set == MSET_EVERYONE || (set == MSET_PLAYING && p.team != TEAM_SPECTATOR) || (set == MSET_TEAM && p.team == t)) mask |= 1u << i;
        }
        return mask;
    }

    double act(int action, const double* r, int n) override {
        if (readOnly_) return 0;
        Server& s = *s_;
        auto arg = [&](int k) { return k < n ? r[k] : -1.0; };
        auto pl = [&](double x) { return x >= 0 && x < kMaxPlayers && s.players_[(int)x].connected ? (int)x : -1; };
        switch (action) {
            case MA_SET_CLOCK: s.phaseEnd_ = s.time() + (float)arg(0); return 0;
            case MA_ADD_TIME: s.phaseEnd_ = std::max(s.phaseEnd_, s.time()) + (float)arg(0); return 0;
            case MA_TOUCH_OBJECTIVE: s.objTouch_ = s.time(); return 0;
            case MA_HOLD_OVERTIME: return overtime(s);
            case MA_KILL: {   // (runs slot kill: a re-entrant runModeSlot)
                int v = pl(arg(0));
                if (v >= 0 && s.players_[v].alive) s.killPlayer(v, pl(arg(1)), (int)arg(2));
                return 0;
            }
            case MA_DAMAGE: {
                int v = pl(arg(0));
                if (v < 0 || !s.players_[v].alive) return 0;
                s.players_[v].health -= (float)arg(1);
                if (s.players_[v].health <= 0) s.killPlayer(v, -1, 254);
                return 0;
            }
            case MA_RESPAWN: { int v = pl(arg(0)); if (v >= 0 && s.players_[v].team != TEAM_SPECTATOR) s.spawnPlayer(v); return 0; }
            case MA_FREEZE: { int v = pl(arg(0)); if (v >= 0) s.players_[v].itFreezeUntil = s.time() + (float)arg(1); return 0; }
            case MA_SWAP_TEAMS: return 0;   // (not available yet: a compile error)
            case MA_BOMB_STEP: bombStep(s); return 0;
            case MA_GIVE_BOMB: {
                int v = pl(arg(0));
                s.ms_.bombCarrier = v >= 0 ? (uint8_t)v : 255;
                s.ms_.bombState = v >= 0 ? BOMB_CARRIED : BOMB_NONE;
                return 0;
            }
            case MA_CART_STEP: cartStep(s); return 0;
            case MA_NEUTRAL_CART_STEP: neutralCartStep(s, false); return 0;
            case MA_POINT_STEP: return pointStep(s);
            case MA_RACE_STEP: raceStep(s); return 0;
            case MA_RACE_PRACTICE: tickRacePractice(s); return 0;
            case MA_STALEMATE_DRAIN: stalemateDrain(s); return 0;
            case MA_PICK_SEEKER: pickSeeker(s); return 0;
            case MA_PICK_RANDOM_IT: pickRandomIt(s); return 0;
            case MA_TAG_OUT: { int v = pl(arg(0)); if (v >= 0) tagOut(s, v); return 0; }
            case MA_PASS_IT: {
                int from = pl(arg(0)), to = pl(arg(1));
                if (from >= 0) s.players_[from].it = false;
                if (to >= 0) {
                    ServerPlayer& v = s.players_[to];
                    v.it = true;
                    v.weapon = WPN_KNIFE;   // (the new tagger has the knife out)
                    v.locateAt = 0;
                }
                return 0;
            }
            case MA_START_TIEBREAK: return rpsStartIfTied(s);
            case MA_TOURNEY_BEGIN: return tourneyStart(s);
            case MA_TOURNEY_WALKOVER: return tourneyWalkover(s);
            case MA_TOURNEY_ADVANCE: { int t = (int)arg(0); if (t == TEAM_A || t == TEAM_B) tourneyMatchWon(s, t); return 0; }
            case MA_PICK_DUEL_MODIFIER: pickDuelModifier(s); return 0;
            default: return 0;
        }
    }

    void say(int action, int winner, const ModeTextPart* parts, int n) override {
        if (readOnly_) return;
        Server& s = *s_;
        std::string text;
        for (int i = 0; i < n; i++) {
            const ModeTextPart& p = parts[i];
            switch (p.kind) {
                case ModeTextPart::STR: text.append(p.s, p.len); break;
                case ModeTextPart::PLAYER: text += p.v >= 0 && p.v < kMaxPlayers && s.players_[p.v].connected ? s.players_[p.v].name : std::string("nobody"); break;
                case ModeTextPart::TEAM: text += p.v == TEAM_A || p.v == TEAM_B ? sideName(s, p.v) : std::string("nobody"); break;   // (1v1, duel, tourney: the player, as natively)
                case ModeTextPart::INT: text += std::to_string(p.v); break;
                case ModeTextPart::LETTER: text += p.v >= 0 && p.v < 26 ? (char)('A' + p.v) : '?'; break;
                case ModeTextPart::NAMES: {
                    bool first = true;
                    for (int k = 0; k < kMaxPlayers; k++)
                        if (((uint64_t)p.v >> k & 1) && s.players_[k].connected) { text += (first ? "" : ", ") + s.players_[k].name; first = false; }
                    break;
                }
            }
        }
        switch (action) {
            case MA_END_ROUND:
            case MA_END_ROUND_NO_SCORE: {   // (the winner: a player in race, else a team)
                int w = s.ms_.mode == MODE_RACE ? (winner >= 0 && winner < kMaxPlayers && s.players_[winner].connected ? winner : -1)
                                                : (winner == TEAM_A || winner == TEAM_B ? winner : -1);
                endRound(s, w, text, action == MA_END_ROUND);
                return;
            }
            case MA_END_ROUND_TEXT: endRoundText(s, text); return;
            case MA_END_MATCH: enterMatchEnd(s, text, winner >= 0 && winner < kMaxPlayers ? winner : -1); return;
            case MA_ANNOUNCE:   // (also in the game log: headless runs read what happened from it)
                s.message(text);
                s.glog(LOG_SIMPLE, "announce: %s", text.c_str());
                return;
            case MA_LOG:
                if (text.compare(0, 11, "mode logic:") == 0) s.log(text);   // (the interpreter's own notes: the host sees them)
                s.glog(LOG_SIMPLE, "%s", text.c_str());
                return;
            default: return;
        }
    }

    uint32_t qrand() override { return ::qrand(); }

private:
    std::shared_ptr<const ModeLogic> L_;
    ModeLogicState st_;
    ModeParams mp_;
    Server* s_ = nullptr;
    bool readOnly_ = false;
};
} // namespace

std::shared_ptr<ModeHooks> makeModeLogicHooks(std::shared_ptr<const ModeLogic> logic) {
    if (!logic) return nullptr;
    return std::make_shared<LogicHooks>(std::move(logic));
}

std::string modeHookedSlots(const ModeHooks& h) {
    std::string o;
    for (int k = 0; k <= MSLOT_TIMER; k++) {
        if (!h.has(k)) continue;
        if (!o.empty()) o += ' ';
        o += k == MSLOT_TIMER ? "timers" : modeSlotName(k);
    }
    return o.empty() ? "none" : o;
}
