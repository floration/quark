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
// Ranked runs only on official maps marked for that ladder ("ranked ..." line).
bool isRankedMap(const Map& m, int ladder);

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
                            std::vector<std::string>* playersOut = nullptr, std::string* ladderOut = nullptr, const std::string& name = "");

// The result list of an EV_RANKED text with ranked ids in place of slots ("id:team:place,..."), as the Worker wants
// it; empty if malformed or a slot has no id. match / ladder: the text's match id and ladder.
std::string rankedResultIds(const std::string& eventText, const std::vector<std::string>& ids, std::string* match = nullptr,
                            std::string* ladder = nullptr, std::vector<std::string>* playersOut = nullptr);

// What this client saw of the match (from its own snapshots), to check the host's EV_RANKED result against
// before reporting it: a host that lies about who won gets no report. seen: kMaxPlayers entries.
struct SeenPlayer { bool playing = false; int team = 0, score = 0, kills = 0; };
bool rankedResultMatchesView(const std::string& eventText, const std::vector<SeenPlayer>& seen, bool teams, int scoreA, int scoreB,
                             std::string* why = nullptr);

// Race runs are reported by the game server that saw them (with its list token, under a race ticket), never by
// the racer's game. The fastest plausible time between checkpoints: the straight-line gap from the start (nearest
// spawn) to checkpoint 1, then between consecutive checkpoint boxes, at kRaceSpeedCeiling. One entry per checkpoint;
// the Worker keeps the same table (RACE_FLOORS in tools/master/worker.js; quark_tests checks they match).
constexpr float kRaceSpeedCeiling = 48.0f; // m/s: 1.5x the fastest dash exit (32); no run sustains this over a leg
std::vector<int> raceSplitFloorsMs(const Map& m);
bool raceSplitsPlausible(const std::vector<int>& splits, const std::vector<int>& floors);
// The /racerun form body a server posts for a finished run (port and token are added by MasterAnnouncer::post).
std::string buildRaceRunForm(const std::string& ticket, const std::string& map, const std::string& rankId, int ms,
                             const std::vector<int>& splits, const std::string& name);
std::string randomMatchId(); // 16 hex chars

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
    // Top players of a ladder ("rating games name" rows); fetched in the background, empty until it arrives.
    struct LeaderRow { int rating = 0, games = 0; std::string name; };
    void fetchLeaderboard(const std::string& ladder);
    bool leaderboard(const std::string& ladder, std::vector<LeaderRow>& out);
    // Race time trials: the fastest times per map ("rating" holds the time in ms; -1 = couldn't reach the server,
    // message in name). Runs themselves are posted by the game server (see buildRaceRunForm).
    void fetchRaceBoard(const std::string& map, double delay = 0);
    bool raceBoard(const std::string& map, std::vector<LeaderRow>& out) { return leaderboard("race:" + map, out); }
    // Plain requests to the Worker (Discord link, queues, crowns): GET when body is empty, else a form POST.
    // The reply (or "!" + the error) lands under key; takeText hands it over once.
    void request(const std::string& key, const std::string& pathAndQuery, const std::string& body = "");
    bool takeText(const std::string& key, std::string& out);
private:
    struct Job { double due; bool report; std::string body, ladder; std::vector<std::string> ids; bool board = false; std::string path = "/report";
                 std::string rawKey = {}; };   // rawKey: a plain request (see request())
    void run();
    void push(Job j);
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::deque<Job> jobs_;
    std::map<std::string, Rating> ratings_; // "ladder:id"
    std::vector<std::string> messages_;
    std::map<std::string, std::vector<LeaderRow>> boards_;
    std::map<std::string, std::string> texts_;
};
