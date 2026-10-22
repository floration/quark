#include "ranked.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include "../common/http.h"
#include "../common/net.h"
#include "../common/sha256.h"
#include "../common/sign.h"
#include "master.h"
#include "map.h"
#include "official_maps.h"
#include "gamedefs.h"

static bool isHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool isOfficialMapName(const std::string& name) {
    for (const OfficialMap* o = kOfficialMaps; o->name; o++) if (name == o->name) return true;
    return false;
}

bool isOfficialMap(const Map& m) {
    std::string text;
    text.reserve(m.source.size());
    for (char c : m.source) if (c != '\r') text += c;
    std::string h = sha256Hex(text);
    for (const OfficialMap* o = kOfficialMaps; o->name; o++) if (m.name == o->name && h == o->sha256) return true;
    return false;
}

bool isRankedMap(const Map& m, int ladder) { return ladder >= 0 && ladder < 16 && (m.rankedLadders >> ladder & 1) && isOfficialMap(m); }

std::string legacyRankId(const std::string& secret) { return sha256Hex(secret).substr(0, 32); }
std::string rankSeedHex(const std::string& secret) { return sha256Hex("quark-rank-ed25519-v1:" + secret); }
std::string rankIdFromPublic(const std::string& pub) { return isHex(pub, 64) ? sha256Hex(pub).substr(0, 32) : std::string(); }

RankKey rankKeyFromSecret(const std::string& secret) {
    RankKey k;
    if (!isHex(secret, 64) || !sign::publicFromPrivate(rankSeedHex(secret), &k.pub)) return RankKey();
    k.secret = secret;
    k.id = rankIdFromPublic(k.pub);
    return k;
}

std::string rankIdFromSecret(const std::string& secret) { return rankKeyFromSecret(secret).id; }

std::string rankConnectMessage(uint32_t cookie, uint32_t nonce) {
    char b[48];
    snprintf(b, sizeof b, "quark-rankid-v1 %08x %08x", (unsigned)cookie, (unsigned)nonce);
    return b;
}
std::string rankTicketMessage(const std::string& match) { return "quark-ticket-v1 " + match; }

bool rankSign(const RankKey& key, const std::string& msg, std::string* sigHex) {
    return !key.secret.empty() && sign::signMessage(msg, rankSeedHex(key.secret), sigHex);
}
bool rankVerify(const std::string& pub, const std::string& msg, const std::string& sig) {
    return isHex(pub, 64) && isHex(sig, 128) && sign::verifyMessage(msg, sig, pub);
}

std::vector<int> seededBracket(const std::vector<int>& bestFirst) {
    size_t size = 2;
    while (size < bestFirst.size()) size *= 2;
    std::vector<int> order = {0};   // seed indexes in bracket order: {0,1} -> {0,3,1,2} -> {0,7,3,4,1,6,2,5} ...
    while (order.size() < size) {
        std::vector<int> next;
        int n = (int)order.size() * 2;
        for (int o : order) { next.push_back(o); next.push_back(n - 1 - o); }
        order = next;
    }
    std::vector<int> out(size, -1);
    for (size_t i = 0; i < size; i++) out[i] = order[i] < (int)bestFirst.size() ? bestFirst[order[i]] : -1;
    return out;
}

RankKey loadOrCreateRankKey(const std::string& path) {
    std::string secret;
    {
        std::ifstream f(path);
        std::string s;
        if (f >> s && isHex(s, 64)) secret = s;
    }
    if (secret.empty()) {
        std::random_device rd;
        char buf[72];
        for (int i = 0; i < 8; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)rd());
        if (!writeFileAtomic(path, std::string(buf) + "\n", true)) return RankKey();   // (the ranked identity: whole, and private)
        secret = buf;
    }
    return rankKeyFromSecret(secret);
}

std::string fromHexText(const std::string& h) {
    auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    if (h.size() % 2) return "";
    std::string out;
    for (size_t i = 0; i < h.size(); i += 2) {
        int a = nib(h[i]), b = nib(h[i + 1]);
        if (a < 0 || b < 0) return "";
        char c = (char)(a << 4 | b);
        if (c >= 32 && c < 127) out += c;
    }
    return out;
}

std::vector<FilledMember> parseFilledMembers(const std::string& members) {
    std::vector<FilledMember> out;
    std::istringstream ms(members);
    std::string m;
    while (std::getline(ms, m, ',')) {
        size_t b = m.rfind(':');   // split the two hex fields off the right: who may be "d:<name>"
        if (b == std::string::npos || b == 0) continue;
        size_t a = m.rfind(':', b - 1);
        if (a == std::string::npos || a == 0) continue;
        out.push_back({m.substr(0, a), fromHexText(m.substr(a + 1, b - a - 1)), fromHexText(m.substr(b + 1))});
    }
    return out;
}

bool parseFilledRow(const std::string& line, FilledRow& out) {
    std::istringstream ls(line);
    std::string members;
    if (!(ls >> out.n >> out.mode >> out.created)) return false;
    std::getline(ls >> std::ws, members);   // (the rest of the line: a Discord name may hold a space)
    while (!members.empty() && (members.back() == '\r' || members.back() == ' ')) members.pop_back();
    out.members = parseFilledMembers(members);
    return !members.empty();
}

static std::string hexName(const std::string& name) {
    std::string h;
    for (char c : name.substr(0, 20)) if (c >= 32 && c < 127) { char b[3]; snprintf(b, sizeof b, "%02x", (unsigned char)c); h += b; }
    return h;
}

std::string randomMatchId() {
    std::random_device rd;
    char id[24];
    snprintf(id, sizeof id, "%08x%08x", (unsigned)rd(), (unsigned)rd());
    return id;
}

// "match ladder slot:team:place,..." -> its entries (false if malformed)
namespace {
struct ResultEntry { int slot, team, place; };
// "match ladder slot:team:place,...[ left=slot,...]" -> its entries and leavers (first leaver first); false if malformed
bool parseRankedText(const std::string& text, std::string& match, std::string& mode, std::vector<ResultEntry>& out,
                     std::vector<int>* leftOut = nullptr) {
    std::istringstream in(text);
    std::string list, extra, more;
    if (!(in >> match >> mode >> list) || !isHex(match, 16)) return false;
    for (char c : mode) if (!isalnum((unsigned char)c)) return false;
    std::istringstream ls(list);
    std::string part;
    while (std::getline(ls, part, ',')) {
        ResultEntry e;
        char tail;
        if (sscanf(part.c_str(), "%d:%d:%d%c", &e.slot, &e.team, &e.place, &tail) != 3 || e.slot < 0 || e.slot >= kMaxPlayers) return false;
        for (auto& o : out) if (o.slot == e.slot) return false;
        out.push_back(e);
    }
    std::vector<int> left;
    if (in >> extra) {
        if (extra.rfind("left=", 0) != 0 || (in >> more)) return false;
        std::istringstream lls(extra.substr(5));
        while (std::getline(lls, part, ',')) {
            char* end = nullptr;
            long v = strtol(part.c_str(), &end, 10);
            if (part.empty() || *end || std::find(left.begin(), left.end(), (int)v) != left.end()) return false;
            if (std::none_of(out.begin(), out.end(), [&](const ResultEntry& e) { return e.slot == v; })) return false; // a leaver is in the result
            left.push_back((int)v);
        }
        if (left.empty()) return false;
    }
    if (leftOut) *leftOut = left;
    return !out.empty();
}
}

std::string rankedResultIds(const std::string& text, const std::vector<std::string>& ids, std::string* match, std::string* ladder,
                            std::vector<std::string>* playersOut, std::string* leftOut) {
    std::string m, mode;
    std::vector<ResultEntry> es;
    std::vector<int> left;
    if (!parseRankedText(text, m, mode, es, &left)) return "";
    std::string p;
    std::vector<std::string> players;
    for (auto& e : es) {
        if (e.slot >= (int)ids.size() || !isHex(ids[e.slot], 32)) return "";
        players.push_back(ids[e.slot]);
        p += (p.empty() ? "" : ",") + ids[e.slot] + ":" + std::to_string(e.team) + ":" + std::to_string(e.place);
    }
    if (match) *match = m;
    if (ladder) *ladder = mode;
    if (playersOut) *playersOut = players;
    if (leftOut) {
        leftOut->clear();
        for (int l : left) *leftOut += (leftOut->empty() ? "" : ",") + ids[l];
    }
    return p;
}

std::string buildRankReport(const std::string& text, const std::vector<std::string>& ids, const RankKey& me,
                            std::vector<std::string>* playersOut, std::string* ladderOut, const std::string& name) {
    if (me.secret.empty()) return "";
    std::string match, mode;
    std::vector<std::string> players;
    std::string p = rankedResultIds(text, ids, &match, &mode, &players);
    if (p.empty() || std::find(players.begin(), players.end(), me.id) == players.end()) return "";
    if (playersOut) *playersOut = players;
    if (ladderOut) *ladderOut = mode;
    std::string nameHex = hexName(name); // shown on the leaderboard; hex keeps the form body plain
    return "secret=" + me.secret + "&match=" + match + "&ladder=" + mode + "&p=" + p + (nameHex.empty() ? "" : "&name=" + nameHex);
}

bool rankedResultMatchesView(const std::string& text, const std::vector<SeenPlayer>& seen, bool teams, int scoreA, int scoreB, std::string* why) {
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    std::string match, mode;
    std::vector<ResultEntry> es;
    std::vector<int> left;
    if (!parseRankedText(text, match, mode, es, &left)) return fail("malformed result");
    auto isLeaver = [&](int slot) { return std::find(left.begin(), left.end(), slot) != left.end(); };
    // everyone we saw playing is in the result, and everyone in it played (leavers may be gone from our view)
    for (size_t i = 0; i < seen.size(); i++)
        if (seen[i].playing && std::none_of(es.begin(), es.end(), [&](const ResultEntry& e) { return e.slot == (int)i; }))
            return fail("the result lists different players than the match had");
    for (auto& e : es) {
        if (e.slot >= (int)seen.size()) return fail("malformed result");
        if (!seen[e.slot].playing && !isLeaver(e.slot)) return fail("the result lists someone who didn't play");
        if (seen[e.slot].playing && isLeaver(e.slot)) return fail("the result says someone left who is still here");
        if (!teams && e.team != e.slot) return fail("malformed result");
    }
    if (teams) {
        int winner = scoreA > scoreB ? 0 : scoreB > scoreA ? 1 : -1; // TEAM_A / TEAM_B / draw
        if (!left.empty()) {   // the first leaver's side lost, whatever the score was
            int lt = -1;
            for (auto& e : es) if (e.slot == left[0]) lt = e.team;
            if (lt != 0 && lt != 1) return fail("malformed result");
            winner = 1 - lt;
        }
        for (auto& e : es) {
            const SeenPlayer& p = seen[e.slot];
            if (p.playing && e.team != p.team) return fail("the result puts a player on the wrong side");
            int place = winner < 0 ? 0 : e.team == winner ? 0 : 1;
            if (e.place != place) return fail("the result doesn't match the score");
        }
        return true;
    }
    // free for all: places from score (then kills). Leavers' last scores aren't in our view: the first leaver must be
    // last, and the players still here must be in the order we saw.
    auto key = [&](const ResultEntry& e) { return (long long)seen[e.slot].score * 1000 + seen[e.slot].kills; };
    for (auto& e : es) {
        if (!left.empty() && e.slot == left[0]) {
            for (auto& o : es) if (o.slot != e.slot && o.place >= e.place) return fail("the first leaver isn't last");
            continue;
        }
        if (isLeaver(e.slot)) continue;
        int place = 0;
        for (auto& o : es) {
            if (isLeaver(o.slot)) { place += o.slot != left[0] && o.place < e.place; continue; } // (a later leaver: take the host's word)
            place += key(o) > key(e);
        }
        if (e.place != place) return fail("the result doesn't match the score");
    }
    return true;
}

std::vector<int> raceSplitFloorsMs(const Map& m) {
    std::vector<int> out;
    if (m.checkpoints.empty() || m.spawns.empty()) return out;
    auto gap = [](const Vec3& amn, const Vec3& amx, const Vec3& bmn, const Vec3& bmx) { // shortest distance between two boxes
        float lo[3] = {std::max(amn.x, bmn.x), std::max(amn.y, bmn.y), std::max(amn.z, bmn.z)};
        float hi[3] = {std::min(amx.x, bmx.x), std::min(amx.y, bmx.y), std::min(amx.z, bmx.z)};
        float d2 = 0;
        for (int k = 0; k < 3; k++) if (lo[k] > hi[k]) d2 += (lo[k] - hi[k]) * (lo[k] - hi[k]);
        return std::sqrt(d2);
    };
    const AABB& c0 = m.checkpoints[0];
    float first = 1e30f;
    for (auto& sp : m.spawns) first = std::min(first, gap(sp.pos, sp.pos, c0.mn, c0.mx));
    out.push_back((int)(first / kRaceSpeedCeiling * 1000.0f));
    for (size_t i = 1; i < m.checkpoints.size(); i++) {
        const AABB &a = m.checkpoints[i - 1], &b = m.checkpoints[i];
        out.push_back((int)(gap(a.mn, a.mx, b.mn, b.mx) / kRaceSpeedCeiling * 1000.0f));
    }
    return out;
}

bool raceSplitsPlausible(const std::vector<int>& splits, const std::vector<int>& floors) {
    if (splits.empty() || splits.size() != floors.size()) return false;
    for (size_t i = 0; i < splits.size(); i++) {
        int leg = splits[i] - (i ? splits[i - 1] : 0);
        if (leg <= 0 || leg < floors[i]) return false;
    }
    return true;
}

std::string buildRaceRunForm(const std::string& ticket, const std::string& map, const std::string& rankId, int ms,
                             const std::vector<int>& splits, const std::string& name) {
    if (!isHex(ticket, 16) || !isHex(rankId, 32) || splits.empty() || splits.back() != ms || map.empty()) return "";
    for (char c : map) if (!isalnum((unsigned char)c) && c != '_') return "";
    std::string sp;
    for (int x : splits) sp += (sp.empty() ? "" : ",") + std::to_string(x);
    std::string nh = hexName(name);
    return "ticket=" + ticket + "&map=" + map + "&id=" + rankId + "&ms=" + std::to_string(ms) + "&splits=" + sp + "&v=" QUARK_VERSION +
           (nh.empty() ? "" : "&name=" + nh);
}

void RankService::fetchRaceBoard(const std::string& map, double delay) { Job j{timeNow() + delay, false, "", "race:" + map, {}}; j.board = true; push(j); }

int Rating::display() const { return (int)std::lround(std::max(0.0, mu - 3 * sigma) * 40); }

RankService::~RankService() {
    { std::lock_guard<std::mutex> l(mu_); quit_ = true; }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void RankService::push(Job j) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
        jobs_.push_back(std::move(j));
    }
    cv_.notify_all();
}

void RankService::submit(const std::string& body, const std::string& ladder, const std::vector<std::string>& players) {
    double now = timeNow();
    push({now, true, body, ladder, players});
    for (double d : {4.0, 15.0, 45.0}) push({now + d, false, "", ladder, players}); // others' reports may finish it later
}

void RankService::fetch(const std::string& ladder, const std::vector<std::string>& ids) { if (!ids.empty()) push({timeNow(), false, "", ladder, ids}); }

void RankService::fetchLeaderboard(const std::string& ladder) { Job j{timeNow(), false, "", ladder, {}}; j.board = true; push(j); }

bool RankService::leaderboard(const std::string& ladder, std::vector<LeaderRow>& out) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = boards_.find(ladder);
    if (it == boards_.end()) return false;
    out = it->second;
    return true;
}

bool RankService::rating(const std::string& ladder, const std::string& id, Rating& out) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = ratings_.find(ladder + ":" + id);
    if (it == ratings_.end()) return false;
    out = it->second;
    return true;
}

std::vector<std::string> RankService::takeMessages() {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<std::string> m;
    m.swap(messages_);
    return m;
}

void RankService::request(const std::string& key, const std::string& pathAndQuery, const std::string& body) {
    Job j;
    j.due = timeNow();
    j.report = false;
    j.rawKey = key;
    j.path = pathAndQuery;
    j.body = body;
    push(j);
}

bool RankService::takeText(const std::string& key, std::string& out) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = texts_.find(key);
    if (it == texts_.end()) return false;
    out = it->second;
    texts_.erase(it);
    return true;
}

void RankService::run() {
    std::unique_lock<std::mutex> l(mu_);
    while (!quit_) {
        auto next = std::min_element(jobs_.begin(), jobs_.end(), [](const Job& a, const Job& b) { return a.due < b.due; });
        if (next == jobs_.end()) { cv_.wait(l); continue; }
        double wait = next->due - timeNow();
        if (wait > 0) { cv_.wait_for(l, std::chrono::milliseconds((int)(wait * 1000) + 1)); continue; }
        Job j = *next;
        jobs_.erase(next);
        l.unlock();
        std::string base = masterUrl(), out, err, msg;
        if (!j.rawKey.empty()) {
            bool ok = !base.empty() && (j.body.empty() ? httpGetText(base + j.path, out, 6000, &err) : httpPostForm(base + j.path, j.body, out, 8000, &err));
            l.lock();
            texts_[j.rawKey] = ok ? out : "!" + (base.empty() ? std::string("no server configured") : err);
            continue;
        }
        std::map<std::string, Rating> got;
        std::vector<LeaderRow> board;
        bool gotBoard = false;
        bool race = j.ladder.rfind("race:", 0) == 0; // "race:<map>": time trial board / submission
        std::string key = race ? j.ladder.substr(5) : j.ladder;
        bool okLadderName = !key.empty();
        for (char c : key) okLadderName &= (bool)isalnum((unsigned char)c);
        if (j.board) {
            std::string url = race ? base + "/raceboard?map=" + key + "&n=25" : base + "/leaderboard?ladder=" + key + "&n=25";
            if (!base.empty() && okLadderName && httpGetText(url, out, 6000, &err)) {
                std::istringstream in(out);
                std::string line;
                while (std::getline(in, line)) {
                    LeaderRow r;
                    std::istringstream ls(line);
                    if (!(ls >> r.rating)) continue;
                    if (!race && !(ls >> r.games)) continue;
                    std::getline(ls, r.name);
                    while (!r.name.empty() && r.name[0] == ' ') r.name.erase(0, 1);
                    board.push_back(r);
                }
                gotBoard = true;
            } else { // couldn't reach it: one row with rating -1 carries the message
                LeaderRow r; r.rating = -1; r.name = "couldn't reach the ranking server";
                board.push_back(r);
                gotBoard = true;
            }
        } else if (base.empty()) msg = "ranked: no server configured";
        else if (j.report) {
            msg = httpPostForm(base + j.path, j.body, out, 8000, &err) ? (race ? "race time: " : "ranked result: ") + out
                                                                      : (race ? "race time not sent: " : "ranked report failed: ") + err;
        } else {
            std::string ids;
            for (auto& id : j.ids) if (isHex(id, 32)) ids += (ids.empty() ? "" : ",") + id;
            bool okLadder = !j.ladder.empty();
            for (char c : j.ladder) okLadder &= (bool)isalnum((unsigned char)c);
            if (okLadder && !ids.empty() && httpGetText(base + "/ratings?ladder=" + j.ladder + "&ids=" + ids, out, 5000, &err)) {
                std::istringstream in(out);
                std::string id;
                Rating r;
                while (in >> id >> r.mu >> r.sigma >> r.games) if (isHex(id, 32)) got[j.ladder + ":" + id] = r;
            }
        }
        l.lock();
        if (gotBoard) boards_[j.ladder] = board;
        for (auto& g : got) ratings_[g.first] = g.second;
        if (!msg.empty()) messages_.push_back(msg);
    }
}
