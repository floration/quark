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
#include "master.h"

static bool isHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::string rankIdFromSecret(const std::string& secret) { return sha256Hex(secret).substr(0, 32); }

RankKey loadOrCreateRankKey(const std::string& path) {
    RankKey k;
    {
        std::ifstream f(path);
        std::string s;
        if (f >> s && isHex(s, 64)) k.secret = s;
    }
    if (k.secret.empty()) {
        std::random_device rd;
        char buf[72];
        for (int i = 0; i < 8; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)rd());
        std::ofstream f(path, std::ios::trunc);
        if (!(f << buf << "\n")) return RankKey();
        k.secret = buf;
    }
    k.id = rankIdFromSecret(k.secret);
    return k;
}

std::string buildRankReport(const std::string& text, const std::vector<std::string>& ids, const RankKey& me,
                            std::vector<std::string>* playersOut, std::string* ladderOut) {
    if (me.secret.empty()) return "";
    std::istringstream in(text);
    std::string match, mode, list;
    if (!(in >> match >> mode >> list) || !isHex(match, 16)) return "";
    for (char c : mode) if (!isalnum((unsigned char)c)) return "";
    std::string p;
    bool mine = false;
    std::vector<std::string> players;
    std::istringstream ls(list);
    std::string part;
    while (std::getline(ls, part, ',')) {
        int slot, team, place;
        if (sscanf(part.c_str(), "%d:%d:%d", &slot, &team, &place) != 3 || slot < 0 || slot >= (int)ids.size() || !isHex(ids[slot], 32)) return "";
        mine |= ids[slot] == me.id;
        players.push_back(ids[slot]);
        p += (p.empty() ? "" : ",") + ids[slot] + ":" + std::to_string(team) + ":" + std::to_string(place);
    }
    if (!mine) return "";
    if (playersOut) *playersOut = players;
    if (ladderOut) *ladderOut = mode;
    return "secret=" + me.secret + "&match=" + match + "&ladder=" + mode + "&p=" + p;
}

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
        std::map<std::string, Rating> got;
        if (base.empty()) msg = "ranked: no server configured";
        else if (j.report) {
            msg = httpPostForm(base + "/report", j.body, out, 8000, &err) ? "ranked result: " + out : "ranked report failed: " + err;
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
        for (auto& g : got) ratings_[g.first] = g.second;
        if (!msg.empty()) messages_.push_back(msg);
    }
}
