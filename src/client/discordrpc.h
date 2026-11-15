// Discord status (Rich Presence): the map, mode, score, server and time played on the player's Discord profile.
// No SDK: the local Discord app's IPC socket (Windows: the named pipe \\.\pipe\discord-ipc-N; Linux / macOS: the Unix
// socket discord-ipc-N in the runtime / temp folders, Flatpak's and Snap's included). A frame is a little-endian uint32
// opcode, a uint32 length and that much JSON: HANDSHAKE (0) {"v":1,"client_id":...}, then FRAME (1) SET_ACTIVITY.
//
// Never costs a frame: every byte of I/O is on the Client's own low-priority thread. The game builds a Presence (plain
// data, no allocation) at most once a second and hands it over only when it changed; the thread sends at most one
// SET_ACTIVITY every 15 s (the newest state wins; the first after connecting goes at once) and, with no Discord running
// or a broken pipe, tries again every 30 s, silently.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ModeState;
struct PlayerSnap;

namespace drpc {

// The Discord application's id (Discord Developer Portal > the application > Application ID). Empty: no Discord
// status at all (no thread is started). quark.cfg's discord_app_id= and the environment variable QUARK_DISCORD_APP_ID
// override it (digits only), so a new id can be tried before a build has it.
constexpr const char kDiscordAppId[] = "";
// The pictures (the website serves them: release.yml publishes `quark_tests mappreviews` to img/rpc/). Discord takes an
// https URL as an image. An official map shows its own; any other map and the menus show the game's icon.
constexpr const char kMapImageBase[] = "https://floration.github.io/quark/img/rpc/";   // + <map file name>.png
constexpr const char kIconUrl[] = "https://floration.github.io/quark/icon.png";
constexpr const char kServerIconUrl[] = "https://floration.github.io/quark/icon.png";   // (the small picture; its own icon later)
constexpr const char kSiteUrl[] = "https://floration.github.io/quark";                  // ("Get Quark")
constexpr size_t kMaxText = 128;   // Discord's limit on details / state / the images' texts

bool validAppId(const std::string& id);   // 1-32 digits
// The id in use: QUARK_DISCORD_APP_ID, else quark.cfg's (cfgValue), else kDiscordAppId; "" when none is valid.
std::string resolveAppId(const std::string& cfgValue);

enum Where : uint8_t {
    W_MENU = 0, W_BROWSING, W_HOSTING, W_BOTSETUP, W_SETTINGS, W_EDITOR, W_DEMO, W_TRAINER, W_CONNECTING,
    W_LEADERBOARD, W_CUSTOMMODE, W_INFO, W_DEMOS, W_BENCHMARK, W_TUTORIAL, W_GAME, W_COUNT
};
enum ScoreKind : uint8_t { SC_NONE = 0, SC_VERSUS, SC_PLACE, SC_WARMUP, SC_SPECTATING };
enum ServerKind : uint8_t { SV_ONLINE = 0, SV_LOCAL, SV_BOTS };

// What the profile shows, as plain data (the game fills one a second; == compares it to the last one sent).
struct Presence {
    uint8_t where = W_MENU;
    uint8_t mode = 0;              // MODE_* (W_GAME)
    uint8_t scoreKind = SC_NONE;
    uint8_t serverKind = SV_ONLINE;
    bool ranked = false, timeTrial = false;
    bool officialMap = false;      // the map has a picture on the website
    bool showServer = true;        // SHOW SERVER NAME (off: "Online")
    int16_t ours = 0, theirs = 0;  // SC_VERSUS: our side first
    uint8_t place = 0, of = 0;     // SC_PLACE
    uint8_t players = 0, maxPlayers = 0;   // the party (maxPlayers 0: none shown)
    int64_t start = 0;             // unix seconds (0: no timer)
    char map[40] = {};             // the map's file name ("" none)
    char mapTitle[48] = {};        // its name as the menus show it
    char server[64] = {};          // the server's name ("" unknown: "Online")
};
bool operator==(const Presence& a, const Presence& b);
inline bool operator!=(const Presence& a, const Presence& b) { return !(a == b); }

// The score part of a match's Presence from what the HUD has: spectating, warmup, our side's score first (team modes,
// 1v1, duel), own score vs the best other's (tourney), or the place (free for all, race, surf, hide and seek, tag).
void scoreFromMode(const ModeState& ms, const std::vector<PlayerSnap>& players, int me, Presence& p);
const char* modeLabel(int mode);   // "Bomb", "King of the Hill", ... (Discord's text, not the HUD's capitals)
const char* whereLabel(int where); // the menus' state line: "Browsing servers", "In settings", ...

struct Activity {
    std::string details, state, largeImage, largeText, smallImage, smallText;
    int partySize = 0, partyMax = 0;   // (partyMax 0: no party)
    int64_t start = 0;
};
Activity buildActivity(const Presence& p);
// Text for a JSON string: valid UTF-8 (a bad byte becomes '?'), at most max bytes cut on a character, and a 1-character
// text padded to Discord's 2 (with U+2800, a blank Discord doesn't trim).
std::string clampText(const std::string& s, size_t max = kMaxText);
std::string jsonEscape(const std::string& s);   // (with the quotes)
std::string activityJson(const Activity& a);    // the "activity" object
// The SET_ACTIVITY frame's JSON (activity null: clears it).
std::string setActivityJson(const Activity* a, long pid, uint64_t nonce);
std::string handshakeJson(const std::string& appId);
std::string frame(uint32_t opcode, const std::string& json);   // the 8-byte header and the JSON

// Test hooks: shorter intervals and the folder the socket is looked for in (quark_tests discordrpc).
struct Tuning {
    double sendGap = 15, retryGap = 30;   // s
    int pollMs = 250;
    std::string onlyDir;                  // non-empty: only <onlyDir>/discord-ipc-0..9
};
Tuning& tuning();

class Client {
public:
    Client() = default;
    ~Client() { stop(); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    bool running() const { return (bool)sh_; }
    void start(const std::string& appId);   // the thread (nothing if it runs or appId is empty)
    // Clears the activity if connected, closes, and joins the thread (waits at most waitMs; a thread stuck in a
    // write is left to finish on its own).
    void stop(int waitMs = 400);
    void set(const Presence& p);   // the newest state (copied under the lock; the thread sends it when it may)
    // (tests) connections made, frames sent, the last SET_ACTIVITY's JSON
    int connects() const;
    int sends() const;
    std::string lastJson() const;
    struct Shared;
private:
    std::shared_ptr<Shared> sh_;
};

}   // namespace drpc
