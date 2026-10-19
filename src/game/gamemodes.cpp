#include "gamemodes.h"
#include "server.h"
#include <cstdlib>

namespace {
constexpr float kFreezeTime = 3.0f;
constexpr float kRoundEndTime = 4.0f;
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
// PAYLOAD
constexpr float kCartRange = 4.0f;
constexpr float kCartSpeed = 1.6f;
constexpr float kCartRollback = 0.6f;
constexpr float kCartIdleBeforeRollback = 10.0f;

int roundsToWin(const Server& s) {
    if (s.cfg_.roundsToWin > 0) return s.cfg_.roundsToWin;
    switch (s.ms_.mode) {
        case MODE_VS: return 5;
        case MODE_BOMB: return 7;
        case MODE_RACE: return 3;
        case MODE_PAYLOAD: return 1; // each team attacks once
        default: return 1;
    }
}

float roundTime(const Server& s) {
    switch (s.ms_.mode) {
        case MODE_BOMB: return 105.0f;
        case MODE_RACE: return 180.0f;
        case MODE_PAYLOAD: return 240.0f;
        case MODE_FFA: return s.cfg_.timeLimit > 0 ? (float)s.cfg_.timeLimit : 600.0f;
        default: return 0.0f; // no limit
    }
}

int other(int t) { return t == TEAM_A ? TEAM_B : TEAM_A; }
int defenders(const Server& s) { return other(s.ms_.attackers); }

void setPhase(Server& s, Phase p, float duration) {
    s.ms_.phase = p;
    s.phaseEnd_ = duration > 0 ? s.time() + duration : 0;
}

void enterMatchEnd(Server& s, const std::string& text, int winner) {
    GameEvent e;
    e.type = EV_ROUND_WIN;
    e.a = (uint8_t)(winner < 0 ? 255 : winner);
    e.b = 1; // match over
    e.text = text;
    s.broadcastEvent(e);
    s.log(text);
    setPhase(s, PHASE_MATCH_END, kMatchEndTime);
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
    if (m.mode == MODE_BOMB) {
        int half = roundsToWin(s) - 1;
        m.attackers = (half > 0 && m.round > half) ? TEAM_B : TEAM_A;
    } else if (m.mode == MODE_PAYLOAD) {
        m.attackers = (m.round % 2 == 1) ? TEAM_A : TEAM_B;
        m.payloadDist = 0;
        m.payloadLen = s.map_.payloadLength();
    }
    for (auto& p : s.players_) {
        p.finished = false;
        p.checkpoint = 0;
        p.respawnAt = -1;
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected) continue;
        if (p.team == TEAM_SPECTATOR) { p.alive = false; continue; }
        s.spawnPlayer(i);
    }
    // Remove leftover projectiles between rounds.
    for (auto& pr : s.projectiles_) pr.active = false;

    if (m.mode == MODE_BOMB) {
        std::vector<int> cands;
        for (int i = 0; i < kMaxPlayers; i++)
            if (s.players_[i].connected && s.players_[i].alive && s.players_[i].team == m.attackers) cands.push_back(i);
        if (!cands.empty()) {
            m.bombCarrier = (uint8_t)cands[rand() % cands.size()];
            m.bombState = BOMB_CARRIED;
        }
    }
    s.roundStart_ = s.time() + kFreezeTime;
    s.lastProgress_ = s.roundStart_;
    s.payloadIdleSince_ = s.roundStart_;
    setPhase(s, PHASE_FREEZE, kFreezeTime);
}

void startMatch(Server& s) {
    ModeState& m = s.ms_;
    m.round = 0;
    m.scoreA = m.scoreB = 0;
    m.roundsToWin = (uint8_t)roundsToWin(s);
    m.target = (int16_t)(m.mode == MODE_KOTH ? (s.cfg_.scoreLimit > 0 ? s.cfg_.scoreLimit : 100) : 0);
    s.kothPoints_[0] = s.kothPoints_[1] = 0;
    s.bestProgress_[0] = s.bestProgress_[1] = 0;
    s.completeTime_[0] = s.completeTime_[1] = 0;
    for (auto& p : s.players_) { p.kills = p.deaths = p.score = 0; }
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

// winner: team for team modes, player id for race, -1 = draw/no winner.
void endRound(Server& s, int winner, const std::string& why, bool award = true) {
    ModeState& m = s.ms_;
    std::string text;
    if (m.mode == MODE_RACE) {
        if (winner >= 0) { s.players_[winner].score++; text = s.players_[winner].name + " wins the race"; }
        else text = "nobody finished";
    } else if (winner >= 0) {
        if (award) (winner == TEAM_A ? m.scoreA : m.scoreB)++;
        text = std::string(teamName(winner)) + " wins the round";
    } else {
        text = "round draw";
    }
    if (!why.empty()) text += " (" + why + ")";
    GameEvent e;
    e.type = EV_ROUND_WIN;
    e.a = (uint8_t)(winner < 0 ? 255 : winner);
    e.text = text;
    s.broadcastEvent(e);
    s.log(text);
    m.actionPlayer = 255;
    m.actionProgress = 0;
    setPhase(s, PHASE_ROUND_END, kRoundEndTime);
}

// Called when ROUND_END finishes: returns true if the match is decided.
bool checkMatchOver(Server& s) {
    ModeState& m = s.ms_;
    int rtw = roundsToWin(s);
    switch (m.mode) {
        case MODE_VS:
        case MODE_BOMB:
            if (m.scoreA >= rtw || m.scoreB >= rtw) {
                int w = m.scoreA >= rtw ? TEAM_A : TEAM_B;
                enterMatchEnd(s, std::string(teamName(w)) + " wins the match", w);
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
        case MODE_PAYLOAD: {
            if (m.round < 2 || m.round % 2 != 0) return false;
            int w = payloadWinner(s);
            if (w < 0 && m.round < 4) { s.message("tied - tiebreaker rounds"); return false; }
            enterMatchEnd(s, w >= 0 ? std::string(teamName(w)) + " wins the match" : "match draw", w);
            return true;
        }
    }
    return true;
}

bool enoughPlayers(const Server& s) {
    switch (s.ms_.mode) {
        case MODE_RACE: return s.teamCount(TEAM_A) >= 1;
        case MODE_FFA: return s.teamCount(TEAM_A) >= 2;
        default: return s.teamCount(TEAM_A) >= 1 && s.teamCount(TEAM_B) >= 1;
    }
}

bool feetIn(const Server& s, int id, const AABB& zone) {
    return playerBox(s.players_[id].move.pos, s.players_[id].move.flags & MF_CROUCHED).overlaps(zone);
}

void tickVS(Server& s) {
    ModeState& m = s.ms_;
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
    m.timeLeft = m.bombState == BOMB_PLANTED ? m.bombTimer : std::max(0.0f, s.phaseEnd_ - s.time());

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
    if (m.bombState != BOMB_PLANTED && s.time() >= s.phaseEnd_) endRound(s, def, "time");
}

void tickRace(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
    const auto& cps = s.map_.checkpoints;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive || p.finished) continue;
        if (p.checkpoint < (int)cps.size() && feetIn(s, i, cps[p.checkpoint])) {
            p.checkpoint++;
            if (p.checkpoint == (int)cps.size()) {
                p.finished = true;
                endRound(s, i, "");
                return;
            }
        }
    }
    if (s.time() >= s.phaseEnd_) endRound(s, -1, "time");
}

void tickKoth(Server& s) {
    ModeState& m = s.ms_;
    int in[2] = {0, 0};
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.alive && p.team < 2 && feetIn(s, i, s.map_.hill)) in[p.team]++;
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
    m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
    Vec3 cart = s.map_.payloadPoint(m.payloadDist);
    int na = 0, nd = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || !p.alive) continue;
        if (length(p.move.pos - cart) >= kCartRange) continue;
        if (p.team == att) na++;
        else if (p.team == def) nd++;
    }
    m.flags &= ~MS_CONTESTED;
    if (na > 0 && nd == 0) {
        float speed = std::min(kCartSpeed * (1.0f + 0.25f * (na - 1)), kCartSpeed * 2.0f);
        m.payloadDist = std::min(m.payloadDist + speed * s.dt(), m.payloadLen);
        s.payloadIdleSince_ = s.time();
    } else if (na > 0 && nd > 0) {
        m.flags |= MS_CONTESTED;
        s.payloadIdleSince_ = s.time();
    } else if (na == 0 && s.time() - s.payloadIdleSince_ > kCartIdleBeforeRollback) {
        m.payloadDist = std::max(0.0f, m.payloadDist - kCartRollback * s.dt());
    }
    float& best = s.bestProgress_[att];
    best = std::max(best, m.payloadDist);
    if (m.payloadDist >= m.payloadLen - 1e-3f) {
        s.completeTime_[att] = s.time() - s.roundStart_;
        endRound(s, att, "payload delivered");
        return;
    }
    if (s.time() >= s.phaseEnd_) endRound(s, def, "time", false);
}

void finishFFA(Server& s) {
    int best = -1, bestScore = -100000, ties = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (!p.connected || p.team == TEAM_SPECTATOR) continue;
        if (p.score > bestScore) { bestScore = p.score; best = i; ties = 1; }
        else if (p.score == bestScore) ties++;
    }
    if (best >= 0 && ties == 1) {
        enterMatchEnd(s, s.players_[best].name + " wins the match", best);
    } else if (!(s.ms_.flags & MS_OVERTIME)) {
        s.ms_.flags |= MS_OVERTIME;
        s.message("tie - overtime! next kill wins");
    }
}

void tickFFA(Server& s) {
    ModeState& m = s.ms_;
    m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
    if (s.cfg_.scoreLimit > 0)
        for (auto& p : s.players_)
            if (p.connected && p.score >= s.cfg_.scoreLimit) { finishFFA(s); return; }
    if (s.time() >= s.phaseEnd_) finishFFA(s);
}
} // namespace

bool modeIsTeamBased(int mode) { return mode != MODE_FFA && mode != MODE_RACE; }

void rulesReset(Server& s) {
    ModeState& m = s.ms_;
    m = ModeState();
    m.mode = (uint8_t)s.cfg_.mode;
    m.roundsToWin = (uint8_t)roundsToWin(s);
    m.payloadLen = s.map_.payloadLength();
    m.attackers = TEAM_A;
    setPhase(s, PHASE_WARMUP, 0);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        if (p.connected && p.team != TEAM_SPECTATOR && !p.alive) s.spawnPlayer(i);
    }
}

void rulesForceStart(Server& s) {
    for (auto& p : s.players_) p.alive = false;
    startMatch(s);
}

void rulesTick(Server& s) {
    ModeState& m = s.ms_;
    switch (m.phase) {
        case PHASE_WARMUP:
            if (!enoughPlayers(s)) { s.phaseEnd_ = 0; m.timeLeft = 0; break; }
            if (s.phaseEnd_ == 0) {
                s.phaseEnd_ = s.time() + kWarmupCountdown;
                s.message("match starting");
            }
            m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) startMatch(s);
            break;
        case PHASE_FREEZE:
            m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) {
                float rt = roundTime(s);
                setPhase(s, PHASE_LIVE, rt);
                s.lastProgress_ = s.time();
            }
            break;
        case PHASE_LIVE:
            switch (m.mode) {
                case MODE_VS: tickVS(s); break;
                case MODE_BOMB: tickBomb(s); break;
                case MODE_RACE: tickRace(s); break;
                case MODE_KOTH: tickKoth(s); break;
                case MODE_PAYLOAD: tickPayload(s); break;
                case MODE_FFA: tickFFA(s); break;
            }
            break;
        case PHASE_ROUND_END:
            m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_ && !checkMatchOver(s)) startRound(s);
            break;
        case PHASE_MATCH_END:
            m.timeLeft = std::max(0.0f, s.phaseEnd_ - s.time());
            if (s.time() >= s.phaseEnd_) {
                for (auto& p : s.players_) p.alive = false;
                rulesReset(s);
            }
            break;
    }
}

void rulesOnDeath(Server& s, int victim, int killer) {
    ModeState& m = s.ms_;
    if (m.mode == MODE_BOMB && m.bombState == BOMB_CARRIED && m.bombCarrier == victim) {
        m.bombState = BOMB_DROPPED;
        m.bombPos = s.players_[victim].move.pos;
        m.bombCarrier = 255;
    }
    if (m.actionPlayer == victim) { m.actionPlayer = 255; m.actionProgress = 0; }
    if (m.mode == MODE_FFA && m.phase == PHASE_LIVE && (m.flags & MS_OVERTIME) && killer >= 0 && killer != victim)
        finishFFA(s);
}

void rulesOnLeave(Server& s, int id) { rulesOnDeath(s, id, -1); }

float rulesRespawnDelay(const Server& s) {
    if (s.ms_.phase == PHASE_WARMUP) return 1.5f;
    if (s.ms_.phase == PHASE_MATCH_END) return -1.0f;
    switch (s.ms_.mode) {
        case MODE_RACE: return 1.0f;
        case MODE_KOTH: return 4.0f;
        case MODE_PAYLOAD: return 5.0f;
        case MODE_FFA: return 2.0f;
        default: return -1.0f; // VS, BOMB: no respawn
    }
}

bool rulesCanDamage(const Server& s, int attacker, int victim) {
    if (s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END || s.ms_.phase == PHASE_FREEZE) return false;
    if (s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP) return false; // race: knockback only
    if (attacker < 0 || attacker == victim) return true;
    if (modeIsTeamBased(s.ms_.mode) && s.players_[attacker].team == s.players_[victim].team) return false;
    return true;
}

bool rulesFrozen(const Server& s, int id) {
    if (s.ms_.phase == PHASE_FREEZE) return true;
    if (s.ms_.mode == MODE_BOMB && s.ms_.actionPlayer == id && s.ms_.phase == PHASE_LIVE) return true;
    return false;
}

int rulesSpawnType(const Server& s, int id) {
    const int mode = s.ms_.mode;
    if (mode == MODE_RACE && s.map_.supportsMode(MODE_RACE)) return SPAWN_RACE;
    if (!modeIsTeamBased(mode)) return SPAWN_ANY;
    return s.players_[id].team == TEAM_B ? SPAWN_B : SPAWN_A;
}
