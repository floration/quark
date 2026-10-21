// Community features backed by the master Worker (tools/master/worker.js), shared with the Discord bot
// (tools/discordbot): linking your Discord name, queues you can join from inside the game (the bot pings
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
std::string fromHex(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out += (char)std::stoi(h.substr(i, 2), nullptr, 16);
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

void Game::linkDiscord() {
    if (rankKey_.secret.empty() || cfg_.discordName == linkedDiscord_) return;
    linkedDiscord_ = cfg_.discordName;
    rank_.request("link", "/link", "secret=" + rankKey_.secret + "&discord=" + toHex(cfg_.discordName) + "&name=" + toHex(cfg_.name));
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
            auto it = crowns_.find(id);
            if (it == crowns_.end() || place < it->second) crowns_[id] = place;
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
            std::istringstream ls(line);
            long long n; std::string mode, members; long long created;
            if (!(ls >> n >> mode >> created >> members)) continue;
            lastFilled_ = std::max(lastFilled_, n);
            if (baseline) continue;
            std::vector<std::string> names;
            bool mine = false;
            std::istringstream ms(members);
            std::string m;
            while (std::getline(ms, m, ',')) {
                size_t a = m.find(':'), b = m.find(':', a + 1);
                if (a == std::string::npos || b == std::string::npos) continue;
                std::string who = m.substr(0, a);
                if (who == rankKey_.id) mine = true;
                names.push_back(fromHex(m.substr(a + 1, b - a - 1)));
            }
            if (!mine) continue;
            queued_.clear();   // (filled players leave every queue)
            std::string with;
            for (auto& nm : names) with += (with.empty() ? "" : ", ") + nm;
            pendingFilled_ = std::string("YOUR QUEUE FOR ") + queueTitle(mode) + " HAS BEEN FILLED (" + with + ") - FIND A SERVER AND BEGIN THE GAME";
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
    if (rank_.takeText("link", out) && !out.empty()) notify(out[0] == '!' ? "DISCORD LINK: COULDN'T REACH THE SERVER" : "DISCORD: " + out, 3);
}

int Game::crownOf(int playerId) const {
    if (!cfg_.crowns) return 0;
    const std::string& id = playerId == net_.clientId() ? rankKey_.id : net_.rankId(playerId);
    auto it = crowns_.find(id);
    return it == crowns_.end() ? 0 : it->second;
}

// A small crown (w wide): gold for #1, silver for #2-10. (x, y) is its top left.
void Game::drawCrown(float x, float y, float w, int place) {
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
}

// The queue list (pause menu > QUEUE): press a mode to join it, again to leave.
void Game::drawQueueMenu(float cx, float y) {
    float s = ui_.s(), w = 420 * s, h = 26 * s, gap = 4 * s, x = cx - w * 0.5f;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, (kQueueCount + 4) * (h + gap) + 24 * s);
    ui_.labelCentered(cx, y, "QUEUES - SHARED WITH DISCORD", kTextWhite);
    y += h + gap;
    ui_.labelCentered(cx, y, "WHEN ONE FILLS, EVERYONE IN IT IS TOLD (HERE AND ON DISCORD)", kTextGrey, 0.5f);
    y += h * 0.7f;
    for (int i = 0; i < kQueueCount; i++, y += h + gap) {
        const auto& q = kQueues[i];
        bool in = queued_.count(q.key) > 0;
        auto it = queueCounts_.find(q.key);
        int n = it == queueCounts_.end() ? 0 : it->second;
        std::string t = std::string(in ? "> " : "") + q.title + "   " + std::to_string(n) + "/" + std::to_string(q.size) + (in ? "  (LEAVE)" : "");
        if (ui_.button(x, y, w, h, t)) queueAction(q.key, in ? "leave" : "join");
    }
    y += gap;
    float hw = (w - gap) * 0.5f;
    if (ui_.button(x, y, hw, h, "LEAVE ALL")) queueAction("", "leaveall");
    if (ui_.button(x + hw + gap, y, hw, h, "BACK")) queueMenu_ = false;
}
