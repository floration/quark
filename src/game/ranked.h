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
    std::string secret; // 64 hex chars: stays on this computer and goes only to the Worker
    std::string pub;    // 64 hex chars: the Ed25519 public key derived from the secret (rankSeedHex)
    std::string id;     // 32 hex chars: rankIdFromPublic(pub), what servers and other players see
};
// Loads the key file, creating it (random) the first time. Empty key if it can't be read or written.
RankKey loadOrCreateRankKey(const std::string& path);
RankKey rankKeyFromSecret(const std::string& secret); // empty if the secret isn't 64 hex chars
// Ranked ids are the first 32 hex chars of sha256(public key hex), so a server can check that a player holds the
// key behind the id they claim (they sign its connect challenge). Ids before 1.17 were sha256(secret): the Worker
// moves an old id's rows to the new one the first time that secret reaches it (POST /hello, sent at start-up).
std::string rankIdFromPublic(const std::string& pubHex);
std::string rankIdFromSecret(const std::string& secret);
std::string legacyRankId(const std::string& secret);
std::string rankSeedHex(const std::string& secret); // Ed25519 seed: sha256("quark-rank-ed25519-v1:" + secret)
// What a player signs: the server's connect challenge (cookie + the client's session nonce), and a ranked match id
// (the server forwards those signatures with its ticket: the Worker only tickets players who really joined).
// Both name the server: the address the player's game dialled, as "a.b.c.d:port" (NetAddr::toString, after DNS).
// Without it a server the player joined could relay its own connect challenge and match ids from a real ranked
// server, have the player sign them, and play there under their id (REVIEW 2.2). Whatever the real server puts in
// its challenge, a relay can forward too; only the address the player chose can't be forwarded. The real server
// takes a signature only for one of its own addresses (Server::ownAddress), and the Worker takes a ticket signature
// only for the ticketing server's ip:port (or, a LAN / same-machine player, a private address on its port).
std::string rankConnectMessage(uint32_t cookie, uint32_t nonce, const std::string& server);
std::string rankTicketMessage(const std::string& match, const std::string& server);
bool rankSign(const RankKey& key, const std::string& msg, std::string* sigHex);
bool rankVerify(const std::string& pubHex, const std::string& msg, const std::string& sigHex);
// Games on a ladder before a rating counts as placed ("?" in game; off the leaderboard and crowns). The Worker's
// PLACEMENT_GAMES is the same number (quark_tests checks).
constexpr int kPlacementGames = 5;
// Tourney seeding: the bracket's first round for entrants sorted best seed first (-1 = bye). Standard order:
// seed 1 meets the last seed, the top seeds get the byes, and seeds 1 and 2 can only meet in the final.
std::vector<int> seededBracket(const std::vector<int>& bestFirst);
constexpr int kUnratedSeed = 200;   // a ladder the player has no games on (or no ranked id) counts this much

// From an EV_RANKED event text and the slot -> ranked id table: the form body this player posts to /report
// (empty if the text is malformed, an id is missing, or we didn't play in it). ids: kMaxPlayers entries.
std::string buildRankReport(const std::string& eventText, const std::vector<std::string>& ids, const RankKey& me,
                            std::vector<std::string>* playersOut = nullptr, std::string* ladderOut = nullptr, const std::string& name = "");

// The result list of an EV_RANKED text with ranked ids in place of slots ("id:team:place,..."), as the Worker wants
// it; empty if malformed or a slot has no id. match / ladder: the text's match id and ladder. leftOut: the ids of
// players who left during the match (first leaver first), comma separated.
// EV_RANKED text: "<match> <ladder> slot:team:place,...[ left=slot,slot]". The first leaver (gone past the reconnect
// grace while the match was live) takes the loss; anyone who leaves after them isn't punished for it.
std::string rankedResultIds(const std::string& eventText, const std::vector<std::string>& ids, std::string* match = nullptr,
                            std::string* ladder = nullptr, std::vector<std::string>* playersOut = nullptr, std::string* leftOut = nullptr);

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
