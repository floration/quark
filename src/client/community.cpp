// Community features backed by the master Worker (tools/master/worker.js), shared with the Discord bot
// (the discord-bot branch): linking your Discord name, queues you can join from inside the game (the bot pings
// Discord users, the game tells you), and crowns for the ranked leaders (gold #1, silver #2-10).
#include "game.h"
#include <algorithm>
#include <sstream>

namespace {
const Color kTextWhite = {1, 1, 1, 1};
const Color kTextGrey = {0.7f, 0.72f, 0.75f, 1};

std::string toHex(const std::string& t) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : t) if (c >= 32 && c < 127) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}
} // namespace

// Same keys and sizes as the Worker's QUEUE_SIZES and the bot's QUEUES.
const Game::QueueDef Game::kQueues[] = {
    {"ranked-vanilla", "RANKED VANILLA", 2}, {"ranked-vampire", "RANKED LASER VAMPIRE", 2}, {"ranked-instagib", "RANKED INSTAGIB", 4},
    {"vs", "VS", 6}, {"bomb", "BOMB", 6}, {"koth", "KING OF THE HILL", 6}, {"payload", "PAYLOAD", 6}, {"npayload", "NEUTRAL PAYLOAD", 6},
    {"points", "MULTI POINT", 6}, {"duel", "DUEL", 2}, {"1v1", "1V1", 2}, {"tourney", "TOURNEY", 4}, {"ffa", "FREE FOR ALL", 4},
    {"race", "RACE", 2}, {"hide", "HIDE AND SEEK", 4}, {"tag", "TAG", 4},
};
const int Game::kQueueCount = (int)(sizeof(kQueues) / sizeof(kQueues[0]));

const char* Game::queueTitle(const std::string& key) {
    for (auto& q : kQueues) if (key == q.key) return q.title;
    return "?";
}

// Linking proves you own the Discord account: /link in Discord gives a 6-digit code (10 minutes), typed here.
// Your ranked id and Discord name are then public on /crowns (shown as leaderboard roles).
void Game::linkDiscord() {
    std::string code = discordCode_;
    discordCode_.clear();
    code.erase(std::remove(code.begin(), code.end(), ' '), code.end());
    if (code.empty()) return;
    if (rankKey_.secret.empty()) { notify("DISCORD LINK NEEDS A RANKED KEY (quark.key)", 4); return; }
    bool unlink = code == "unlink";
    bool digits = code.size() == 6 && std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (!unlink && !digits) { notify("DISCORD: TYPE THE 6-DIGIT CODE FROM /link IN DISCORD", 4); return; }
    rank_.request("link", "/link", "secret=" + rankKey_.secret + (unlink ? std::string("&unlink=1") : "&code=" + code) + "&name=" + toHex(cfg_.name));
}

void Game::queueAction(const std::string& mode, const char* action) {
    if (rankKey_.secret.empty()) { notify("QUEUES NEED A RANKED KEY (quark.key) - COULDN'T MAKE ONE", 4); return; }
    std::string body = "secret=" + rankKey_.secret + "&mode=" + mode + "&action=" + action + "&name=" + toHex(cfg_.name);
    rank_.request("queue", "/queue", body);
    if (std::string(action) == "join") queued_.insert(mode);
    else if (std::string(action) == "leave") queued_.erase(mode);
    else queued_.clear();
    nextQueuePoll_ = time_ + 0.5;   // fresh counts soon
}

// Every frame (cheap: only timers until something is due; the requests run on the RankService thread).
void Game::updateCommunity() {
    std::string out;
    // crowns: who's top 10 on any board, by ranked id (best place wins)
    if (time_ >= nextCrownFetch_) { nextCrownFetch_ = time_ + 120; rank_.request("crowns", "/crowns"); }
    if (rank_.takeText("crowns", out) && (out.empty() || out[0] != '!')) {
        crowns_.clear();
        std::istringstream in(out);
        std::string board, id, name, discord;
        int place;
        while (in >> board >> place >> id >> name >> discord) {
            const char* letter = board == "vanilla" ? "Q" : board == "vampire" ? "LG" : board == "instagib" ? "I" : board.rfind("race", 0) == 0 ? "R" : "";
            auto it = crowns_.find(id);
            if (it == crowns_.end() || place < it->second.place) crowns_[id] = {place, letter};
        }
    }
    // queue replies, counts, and filled queues
    if (rank_.takeText("queue", out)) {
        if (!out.empty() && out[0] == '!') notify("QUEUE: COULDN'T REACH THE SERVER", 3);
        else if (out.rfind("queued ", 0) == 0) { std::istringstream ls(out.substr(7)); std::string m, n; ls >> m >> n; notify("QUEUED FOR " + std::string(queueTitle(m)) + " (" + n + ")", 3); }
        else if (out == "not a queue" || out == "bad request") notify("QUEUE: " + out, 3);
        nextFilledPoll_ = time_;   // ("filled": we'll see it in /filled right away)
    }
    bool watching = !queued_.empty() || queueMenu_;
    if (watching && time_ >= nextQueuePoll_) { nextQueuePoll_ = time_ + (queueMenu_ ? 5 : 20); rank_.request("queuecounts", "/queue"); }
    if (rank_.takeText("queuecounts", out) && (out.empty() || out[0] != '!')) {
        queueCounts_.clear();
        std::istringstream in(out);
        std::string m;
        int n, size;
        while (in >> m >> n >> size) queueCounts_[m] = n;
    }
    if ((watching || lastFilled_ < 0) && time_ >= nextFilledPoll_) {
        nextFilledPoll_ = time_ + 10;
        rank_.request("filled", "/filled?since=" + std::to_string(std::max<long long>(0, lastFilled_)));
    }
    if (rank_.takeText("filled", out) && (out.empty() || out[0] != '!')) {
        std::istringstream in(out);
        std::string line;
        bool baseline = lastFilled_ < 0;   // the first look only learns where the list is
        if (baseline) lastFilled_ = 0;
        while (std::getline(in, line)) {
            FilledRow row;
            if (!parseFilledRow(line, row)) continue;
            lastFilled_ = std::max(lastFilled_, row.n);
            if (baseline) continue;
            bool mine = false;
            std::string with;
            for (auto& m : row.members) {
                if (m.who == rankKey_.id) mine = true;
                with += (with.empty() ? "" : ", ") + m.name;
            }
            if (!mine) continue;
            queued_.clear();   // (filled players leave every queue)
            pendingFilled_ = std::string("YOUR QUEUE FOR ") + queueTitle(row.mode) + " HAS BEEN FILLED (" + with + ") - FIND A SERVER AND BEGIN THE GAME";
        }
    }
    // tell them, unless they're in a ranked match right now (then once it's over)
    bool inRanked = screen_ == Screen::InGame && net_.hasSnapshot() && (net_.mode().flags & MS_RANKED) && net_.mode().phase != PHASE_MATCH_END;
    if (!pendingFilled_.empty() && !inRanked) {
        notify(pendingFilled_, 12);
        if (screen_ == Screen::InGame) addChat({pendingFilled_, rgba(cfg_.colorFriendly), time_ + 15});
        else error_ = pendingFilled_;
        audio_.play(SND_STREAK, 1.0f);
        pendingFilled_.clear();
    }
    updateLifetime();
    if (rank_.takeText("link", out) && !out.empty()) {
        if (out[0] == '!') notify("DISCORD LINK: COULDN'T REACH THE SERVER", 3);
        else {
            if (out.rfind("linked ", 0) == 0) { cfg_.discordName = out.substr(7, 40); cfg_.save(); }
            else if (out == "unlinked") { cfg_.discordName.clear(); cfg_.save(); }
            notify("DISCORD: " + out, 4);
            // privacy (REVIEW WEB-12): what linking makes public, said where it's done
            if (out.rfind("linked ", 0) == 0)
                notify("DISCORD LINKED - YOUR DISCORD NAME IS NOW PUBLIC WITH YOUR GAME NAME AND RANKED ID (CROWNS, QUEUE FILLS). TYPE UNLINK TO REMOVE IT", 10);
        }
    }
}

// Lifetime kills / deaths. The scoreboard shows each player's stored total plus this game's kills / deaths, so it
// counts up live; your own game reports this game's numbers once, when the match ends (or when you leave). Totals
// are fetched when you join, when someone new appears and when a new match starts - never during the end screen,
// where they'd already include this game.
void Game::updateLifetime() {
    const bool inGame = screen_ == Screen::InGame && net_.hasSnapshot() && !net_.playback();
    const PlayerSnap* me = inGame ? net_.findPlayer(net_.clientId()) : nullptr;
    const int phase = inGame ? net_.mode().phase : -1;
    auto report = [&] {
        if (lifePendK_ <= 0 && lifePendD_ <= 0) return;
        if (rankKey_.secret.empty()) { lifePendK_ = lifePendD_ = 0; return; }
        int k = std::min(lifePendK_, 300), d = std::min(lifePendD_, 300);
        rank_.request("lifetime", "/lifetime", "secret=" + rankKey_.secret + "&kills=" + std::to_string(k) + "&deaths=" + std::to_string(d) + "&name=" + toHex(cfg_.name));
        lifePendK_ = lifePendD_ = 0;
    };
    if (me) {
        if (lifeSeenK_ < 0 || me->kills < lifeSeenK_) lifeSeenK_ = 0;    // just joined, or a new match reset the scores:
        if (lifeSeenD_ < 0 || me->deaths < lifeSeenD_) lifeSeenD_ = 0;   // count from zero
        lifePendK_ += me->kills - lifeSeenK_; lifePendD_ += me->deaths - lifeSeenD_;
        lifeSeenK_ = me->kills; lifeSeenD_ = me->deaths;
    } else if (lifeSeenK_ >= 0) { report(); lifeSeenK_ = lifeSeenD_ = -1; }   // left the game: it ended for you
    if (phase == PHASE_MATCH_END && lifePhase_ != PHASE_MATCH_END) report();   // the match is over: send it
    bool newMatch = lifePhase_ == PHASE_MATCH_END && phase != PHASE_MATCH_END && phase >= 0;
    lifePhase_ = phase;
    std::string out;
    rank_.takeText("lifetime", out);
    // (the list of ids is only built when it's going to be sent: a new match, or someone unknown at most every 10 s;
    // building it every frame was a dozen allocations and a quadratic search)
    if (me && phase != PHASE_MATCH_END && (newMatch || time_ >= nextLifeFetch_)) {
        bool unknown = false;
        for (int i = 0; i < kMaxPlayers && !newMatch && !unknown; i++) {
            const std::string& id = i == net_.clientId() ? rankKey_.id : net_.rankId(i);
            unknown = id.size() == 32 && !lifetime_.count(id);
        }
        if (newMatch || unknown) {
            std::string ids;
            for (int i = 0; i < kMaxPlayers; i++) {
                const std::string& id = i == net_.clientId() ? rankKey_.id : net_.rankId(i);
                if (id.size() != 32 || ids.find(id) != std::string::npos) continue;
                if (!ids.empty()) ids += ',';
                ids += id;
            }
            if (!ids.empty()) { nextLifeFetch_ = time_ + 10; lifeAsked_ = ids; rank_.request("lifetimes", "/lifetime?ids=" + ids); }
        } else nextLifeFetch_ = time_ + 1;   // (everyone known: look again in a second, not every frame)
    }
    if (rank_.takeText("lifetimes", out) && (out.empty() || out[0] != '!')) {
        std::istringstream in(out);
        std::string id; int k, d;
        while (in >> id >> k >> d) lifetime_[id] = {k, d};
        std::istringstream asked(lifeAsked_);   // asked about but not on the master yet: zero so far
        for (std::string a; std::getline(asked, a, ',');) if (!lifetime_.count(a)) lifetime_[a] = {0, 0};
    }
}

int Game::crownOf(int playerId) const {
    if (!cfg_.crowns) return 0;
    const std::string& id = playerId == net_.clientId() ? rankKey_.id : net_.rankId(playerId);
    auto it = crowns_.find(id);
    return it == crowns_.end() ? 0 : it->second.place;
}

const char* Game::crownLetter(int playerId) const {
    const std::string& id = playerId == net_.clientId() ? rankKey_.id : net_.rankId(playerId);
    auto it = crowns_.find(id);
    return it == crowns_.end() ? "" : it->second.letter;
}

// A small crown (w wide): gold for #1, silver for #2-10. (x, y) is its top left.
void Game::drawCrown(float x, float y, float w, int place, const char* letter) {
    if (place <= 0) return;
    Color c = place == 1 ? Color{1.0f, 0.78f, 0.15f, 1} : Color{0.82f, 0.85f, 0.9f, 1};
    Color edge = {0.1f, 0.08f, 0.02f, 0.9f};
    float h = w * 0.7f, u = w / 7;
    r_.rect(x - 1, y + h * 0.45f - 1, w + 2, h * 0.55f + 2, edge);
    r_.rect(x, y + h * 0.45f, w, h * 0.55f, c);                        // band
    for (int k = 0; k < 3; k++) {                                      // three points
        float px = x + k * 3 * u;
        r_.rect(px - 1, y - 1, u + 2, h * 0.5f + 2, edge);
        r_.rect(px, y, u, h * 0.5f, c);
    }
    if (*letter) {   // the board's letter on the band
        float ts = std::max(1.0f, std::floor(h * 0.5f / r_.textHeight(1)));
        r_.text(x + (w - r_.textWidth(letter, ts)) * 0.5f, y + h * 0.5f, ts, letter, edge);
    }
}

// The queue list (pause menu > QUEUE): press a mode to join it, again to leave.
void Game::drawQueueMenu(float cx, float y) {
    // two columns of modes (16 full-width rows ran off the bottom at 720p), the actions under them
    float s = ui_.s(), w = 640 * s, h = 26 * s, gap = 4 * s, x = cx - w * 0.5f, cw = (w - gap) * 0.5f;
    int rows = (kQueueCount + 1) / 2;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, (rows + 3) * (h + gap) + h * 0.7f + 24 * s);
    ui_.labelCentered(cx, y, "QUEUES - SHARED WITH DISCORD", kTextWhite);
    y += h + gap;
    ui_.labelCentered(cx, y, "WHEN ONE FILLS, EVERYONE IN IT IS TOLD (HERE AND ON DISCORD)", kTextGrey, 0.5f);
    y += h * 0.7f;
    for (int i = 0; i < kQueueCount; i++) {
        const auto& q = kQueues[i];
        bool in = queued_.count(q.key) > 0;
        auto it = queueCounts_.find(q.key);
        int n = it == queueCounts_.end() ? 0 : it->second;
        std::string t = std::string(in ? "> " : "") + q.title + "  " + std::to_string(n) + "/" + std::to_string(q.size) + (in ? " (LEAVE)" : "");
        if (ui_.button(x + (i % 2) * (cw + gap), y + (i / 2) * (h + gap), cw, h, t, UiExp::state("queue").withId(q.title))) queueAction(q.key, in ? "leave" : "join");
    }
    y += rows * (h + gap) + gap;
    float tw = (w - 2 * gap) / 3;
    // (greyed out when there's nothing left to join / leave)
    if (ui_.button(x, y, tw, h, "QUEUE FOR ALL", UiExp::state("queue"), (int)queued_.size() < kQueueCount)) for (int i = 0; i < kQueueCount; i++) if (!queued_.count(kQueues[i].key)) queueAction(kQueues[i].key, "join");
    if (ui_.button(x + tw + gap, y, tw, h, "LEAVE ALL", UiExp::state("queue"), !queued_.empty())) queueAction("", "leaveall");
    if (ui_.button(x + 2 * (tw + gap), y, tw, h, "BACK", UiExp::nav("ingame/pause"))) queueMenu_ = false;
}
