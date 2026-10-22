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

// Time trial: the form body for /racetime (best time + the time at every checkpoint, the last being the finish).
std::string buildRaceReport(const std::string& map, int ms, const std::vector<int>& splits, const RankKey& me, const std::string& name);

// Worker hex text ("-" or empty = none) -> printable text; empty if it isn't valid hex. Never throws.
std::string fromHexText(const std::string& hex);
// One member of a /filled row ("who:namehex:discordhex"). who is a ranked id, or "d:<discord name>" for a
// Discord user, so it may itself contain ':'.
struct FilledMember { std::string who, name, discord; };
// A /filled row's members field (comma separated). Malformed members are skipped.
std::vector<FilledMember> parseFilledMembers(const std::string& members);
// One /filled line, "n mode created members"; false if malformed.
struct FilledRow { long long n = 0, created = 0; std::string mode; std::vector<FilledMember> members; };
bool parseFilledRow(const std::string& line, FilledRow& out);

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
    // Race time trials: post a run (the Worker keeps it only if it's your best), and the fastest times per map
    // ("rating" holds the time in ms; -1 = couldn't reach the server, message in name).
    void submitRace(const std::string& body, const std::string& map);
    void fetchRaceBoard(const std::string& map);
    bool raceBoard(const std::string& map, std::vector<LeaderRow>& out) { return leaderboard("race:" + map, out); }
    // Plain requests to the Worker (Discord link, queues, crowns): GET when body is empty, else a form POST.
    // The reply (or "!" + the error) lands under key; takeText hands it over once.
    void request(const std::string& key, const std::string& pathAndQuery, const std::string& body = "");
    bool takeText(const std::string& key, std::string& out);
private:
    struct Job { double due; bool report; std::string body, ladder; std::vector<std::string> ids; bool board = false; std::string path = "/report";
                 std::string rawKey; };   // rawKey: a plain request (see request())
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
