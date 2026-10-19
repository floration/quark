// Ranked play: a per-computer key, match reports and rating lookups against the Cloudflare Worker
// (tools/master/worker.js). The key's secret only ever goes to the Worker; servers see the public id
// (first 32 hex chars of sha256(secret)). A match is rated once every player reported the same result.
#pragma once
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct Map;
// Ranked only runs on the maps shipped with the game: same name and the same contents as the official file.
bool isOfficialMap(const Map& m);
bool isOfficialMapName(const std::string& name);

struct RankKey {
    std::string secret; // 64 hex chars
    std::string id;     // 32 hex chars
};
// Loads the key file, creating it (random) the first time. Empty key if it can't be read or written.
RankKey loadOrCreateRankKey(const std::string& path);
std::string rankIdFromSecret(const std::string& secret);

// From an EV_RANKED event text and the slot -> ranked id table: the form body this player posts to /report
// (empty if the text is malformed, an id is missing, or we didn't play in it). ids: kMaxPlayers entries.
std::string buildRankReport(const std::string& eventText, const std::vector<std::string>& ids, const RankKey& me,
                            std::vector<std::string>* playersOut = nullptr, std::string* ladderOut = nullptr);

struct Rating {
    double mu = 25, sigma = 25.0 / 3;
    int games = 0;
    int display() const; // conservative skill (mu - 3 sigma) x 40, from 0
};

// Background HTTP for ranked: reports and rating lookups. All calls are non-blocking.
class RankService {
public:
    ~RankService();
    // ladder: the ranked ruleset ("vanilla", "vampire", "instagib"); each has its own ratings
    void submit(const std::string& body, const std::string& ladder, const std::vector<std::string>& players); // then re-fetches their ratings
    void fetch(const std::string& ladder, const std::vector<std::string>& ids);
    bool rating(const std::string& ladder, const std::string& id, Rating& out);
    std::vector<std::string> takeMessages();
private:
    struct Job { double due; bool report; std::string body, ladder; std::vector<std::string> ids; };
    void run();
    void push(Job j);
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::deque<Job> jobs_;
    std::map<std::string, Rating> ratings_; // "ladder:id"
    std::vector<std::string> messages_;
};
