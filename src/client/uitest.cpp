// The UI test: `quark --uitest [report.json]` (docs/UI_TEST.md).
//
// Every interactive widget is drawn with a UiExp saying what activating it does (ui.h). While this test runs, the Ui
// reports every widget it draws each frame (Ui::probe); the test walks every screen it can reach from a few starting
// situations (scenarios: the menus offline, hosting a game in several modes, a client, a demo, edit sessions), clicks /
// types into / drags / presses every widget once in each situation, and checks the declared effect really happened:
//   NAV    the screen became the declared one              CFG   the setting changed, survives quark.cfg, and is
//   STATE  the declared observable changed                        saved when the screen is left (Esc / BACK)
//   CMD    the server command was sent (and kept from      EDIT  the editor's map changed, and undo restores it
//          the server, so the game stays as it was)        BIND  the slot took the next key and saved it
//   URL / QUIT  intercepted; LOCAL the widget's own value; INERT (and any greyed-out widget) nothing changed
// It also checks Esc from every screen goes back to a screen it came through, and the layout of every screen at
// 1280x720, 1920x1080 and 1024x768: nothing off screen, no two widgets overlapping, no label clipped.
// It fails if any widget has no expectation, fails its expectation, or was seen but never tested (the coverage gate),
// so a new widget can't go in untested. Nothing here runs in normal play (Game::uiTest_ is null).
#include "game.h"
#include "openurl.h"
#include "profiles.h"
#include "../common/u8file.h"
#include "../game/customrules.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------
// Game's side: where the UI is, what can be observed, and the interactive things that aren't widgets

std::string Game::uiScreenPath() const {
    auto settingsSub = [&]() -> std::string {
        if (controlsOpen_) return filePick_ == 2 ? "/controls/files" : "/controls";
        if (crosshairOpen_) return "/crosshair";
        if (widgetEdit_) return filePick_ == 1 ? "/hud/widgets/files" : valueDrop_ && widgetSel_ >= 0 ? "/hud/widgets/values" : "/hud/widgets";
        if (hudOpen_) return hudDrop_ ? "/hud/elements" : "/hud";
        if (visualsOpen_) return "/visuals";
        return "";
    };
    auto pickerName = [](int p) -> const char* {
        switch (p) {
            case PICK_HOST_MAP: case PICK_BOT_MAP: return "maps";
            case PICK_HOST_MODE: case PICK_BOT_MODE: return "modes";
            case PICK_LIVE_MAP: return "livemap";
            case PICK_LIVE_MODE: return "livemode";
            case PICK_VARIANT: return "variants";
            case PICK_TMAPS: return "tmaps";
            case PICK_TMODES: return "tmodes";
            case PICK_RANKED: return "ladders";
        }
        return "?";
    };
    std::string pick = picker_ != PICK_NONE ? std::string("/picker:") + pickerName(picker_) : "";
    static const char* info[] = {"modes", "controls", "hud", "hosting", "editor", "maps"};
    static const char* custom[] = {"general", "weapons", "maps", "movement"};
    switch (screen_) {
        case Screen::Main: return edPrompt_ ? "main/save-copy" : "main";   // (after someone else's session: keep a copy?)
        case Screen::Join: return "join";
        case Screen::Host: return "host" + pick;
        case Screen::VsBots: return "vsbots" + pick;
        case Screen::Settings: return "settings" + settingsSub();
        case Screen::Leaderboard: return raceStatsView_ ? "leaderboard/race-stats" : "leaderboard";
        case Screen::Info: return std::string("info/") + info[std::min(std::max(infoTab_, 0), 5)];
        case Screen::Demos: return "demos";
        case Screen::CustomMode: return std::string("custom/") + custom[std::min(std::max(customTab_, 0), 3)];
        case Screen::Connecting: return "connecting";
        case Screen::Editor: return std::string(edPath()) + (edPrompt_ ? "/keep-edits" : edGroupMenu_ ? "/groups" : edExportAsk_ ? "/export-ask" : "") + (chatting_ ? "/chat" : "");
        case Screen::InGame: {
            std::string p = "ingame";
            if (bake_) return p + "/loading";
            if (paused_) {
                p += "/pause";
                if (settingsInGame_) p += "/settings" + settingsSub();
                else if (picker_ != PICK_NONE) p += pick;
                else if (voteMenu_) p += specPick_ ? "/vote/spec" : "/vote";
                else if (queueMenu_) p += "/queue";
            }
            if (chatting_) p += console_ ? "/console" : "/chat";
            return p;
        }
    }
    return "?";
}

std::string Game::uiContext() const {
    if (screen_ == Screen::Editor && edPrompt_) return std::string("keep:") + std::to_string(edPromptThen_);
    if (screen_ != Screen::Editor || !edSelValid()) return "";
    static const char* kinds[] = {"box", "spawn", "light", "site", "hill", "checkpoint", "path", "ladder", "group"};
    std::string k = kinds[edSel_.kind];
    if (edSel_.kind == ED_GROUP) k = ed_.groups[edSel_.idx].type == Map::GROUP_ROOM ? "room" : ed_.groups[edSel_.idx].type == Map::GROUP_STAIRS ? "stairs" : "group";
    return "sel:" + k + (edMulti_.empty() ? "" : "+");
}

static std::string listDir(const std::string& dir) {   // names, sizes and times: any file written shows
    std::error_code ec;
    std::vector<std::string> v;
    for (auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        std::error_code e2;
        v.push_back(e.path().filename().u8string() + ":" + std::to_string(e.is_regular_file(e2) ? (long long)e.file_size(e2) : -1) + ":" +
                    std::to_string((long long)e.last_write_time(e2).time_since_epoch().count()));
    }
    std::sort(v.begin(), v.end());
    std::string o;
    for (auto& s : v) o += s + "\n";
    return o;
}

std::string Game::uiObserve(const std::string& w) {
    auto b = [](bool v) { return std::string(v ? "1" : "0"); };
    if (w == "hosting") return b(host_ != nullptr);
    if (w == "recording") return b(net_.recording());
    if (w == "demo") return b(net_.playback());
    if (w == "weapon") return std::to_string(desiredWeapon_ >= 0 ? desiredWeapon_ : net_.selfWeapon());
    if (w == "rps") return std::to_string(rpsMine_);
    if (w == "reach") return host_ ? std::to_string((int)host_->reachability()) + " " + host_->reachStatus() : "";
    if (w == "widgets") return widgets_.toText() + "|" + std::to_string(widgetSel_) + "|" + std::to_string(filePick_) + "|" + widgetMsg_ + "@" + std::to_string(widgetMsgUntil_);
    if (w == "ctlimport") return cfg_.toText() + "|" + std::to_string(filePick_) + "|" + widgetMsg_ + "@" + std::to_string(widgetMsgUntil_);
    if (w.rfind("files:", 0) == 0) return listDir(profileDir(w.substr(6).c_str()));
    if (w == "groups") return listDir(userDataPath("groups"));
    if (w == "usermaps") return listDir(userMapsDir());
    if (w == "queue") { std::string o; for (auto& q : queued_) o += q + " "; return o; }
    if (w == "custom") return editRules_.toText();
    if (w == "multisel") { std::string o; for (bool v : multiSel_) o += v ? '1' : '0'; return o; }
    if (w == "hudsel") return std::to_string(hudSel_);
    if (w == "edsel") {
        char c[128];
        snprintf(c, sizeof c, "%d/%d+%zu %.3f %.3f %.3f", edSel_.kind, edSel_.idx, edMulti_.size(), edCam_.x, edCam_.y, edCam_.z);
        return c;
    }
    if (w == "ednotice") return edMsg_ + "@" + std::to_string(edMsgUntil_);
    if (w == "edbaked") return b(edBaked_);
    if (w == "edopen") return std::to_string(edOpens_);
    if (w == "edgrid") return std::to_string(edGridIdx_);
    if (w == "edmap") return ed_.serialize();
    if (w == "edimport") return ed_.serialize() + b(edGroupMenu_);
    if (w == "browser") return std::to_string(browser_.refreshes());
    if (w == "scores") return b(showScores_);
    if (w == "perf") return b(showPerf_);
    if (w == "perfmini") return b(perfMini_);
    if (w == "spec") return std::to_string(specTarget_);
    if (w == "consolelog") return std::to_string(consoleLog_.size()) + (consoleLog_.empty() ? "" : consoleLog_.back());
    return "?" + w;
}

void Game::uiTestActions() {
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    auto bound = [&](int a) { int c = binds().code[a][0]; return c > 0 && c < kMouseCode ? c : 0; };
    auto say = [&] { ui_.keyAction("SAY", GLFW_KEY_ENTER, 0, UiExp::cmd("say hello").typing("hello")); };
    if (screen_ == Screen::InGame) {
        if (bake_) return;
        if (chatting_ && console_) {
            ui_.keyAction("CONSOLE: SETTING", GLFW_KEY_ENTER, 0, UiExp::cfg("fov").typing("fov 111"));
            ui_.keyAction("CONSOLE: SERVER COMMAND", GLFW_KEY_ENTER, 0, UiExp::cmd("team spec").typing("/team spec"));
            ui_.keyAction("CONSOLE: TAB", GLFW_KEY_TAB, 0, UiExp::state("consolelog"));
            return;
        }
        if (chatting_) { say(); return; }
        if (paused_) return;   // (the pause menu: its widgets; Esc is checked as the way back)
        ui_.keyAction("ESC (MENU)", GLFW_KEY_ESCAPE, 0, UiExp::nav("ingame/pause"));
        if (int k = bound(A_CHAT)) ui_.keyAction("CHAT", k, 0, UiExp::nav("ingame/chat"));
        ui_.keyAction("CONSOLE (`)", GLFW_KEY_GRAVE_ACCENT, 0, UiExp::nav("ingame/console"));
        ui_.keyAction("SCORES (HOLD)", 0, 0, UiExp::state("scores", "1"), A_SCORES);
        if (int k = bound(A_PERF)) ui_.keyAction("PERF OVERLAY", k, 0, UiExp::state("perf"));
        if (int k = bound(A_PERFMINI)) ui_.keyAction("LATENCY NUMBER", k, 0, UiExp::state("perfmini"));
        if (int k = bound(A_VOTEYES)) ui_.keyAction("VOTE YES", k, 0, UiExp::cmd("vote yes"));
        if (int k = bound(A_VOTENO)) ui_.keyAction("VOTE NO", k, 0, UiExp::cmd("vote no"));
        if (int k = bound(A_VOTESTART)) ui_.keyAction("VOTE START", k, 0, UiExp::cmd("callvote start"));
        if (int k = bound(A_QUIP)) ui_.keyAction("QUIP", k, 0, UiExp::cmd("quip"));
        if (int k = bound(A_TOGGLECHAT)) ui_.keyAction("TOGGLE CHAT", k, 0, UiExp::cfg("chat_off"));
        if (rpsPicking()) {   // the hide and seek tiebreak: slot keys 1 / 2 / 3 throw
            static const char* cmds[3] = {"rps ROCK", "rps PAPER", "rps SCISSORS"};
            static const char* names[3] = {"ROCK (SLOT 1)", "PAPER (SLOT 2)", "SCISSORS (SLOT 3)"};
            for (int k = 0; k < 3; k++) if (int key = bound(slotAction(k))) ui_.keyAction(names[k], key, 0, UiExp::cmd(cmds[k]));
        } else {   // the seeker / tagger's locator: its slot key (0)
            const PlayerSnap* me = net_.findPlayer(net_.clientId());
            int slot = weaponSlot(WPN_LOCATOR);
            static const std::string kLocatorWeapon = std::to_string(WPN_LOCATOR);
            if (isItMode(net_.mode().mode) && me && (me->flags & PS_HASBOMB) && net_.selfWeapon() != WPN_LOCATOR && desiredWeapon_ != WPN_LOCATOR)
                if (int key = bound(slotAction(slot))) ui_.keyAction("LOCATOR (SLOT KEY)", key, 0, UiExp::state("weapon", kLocatorWeapon.c_str()).withId("LOCATOR"));
        }
        if (!net_.selfAlive()) {   // dead / spectating: a click watches the next player
            const auto& ps = framePlayers();
            int alive = 0;
            for (int i = 0; i < kMaxPlayers; i++) alive += ps[i].valid && (ps[i].snap.flags & PS_ALIVE) && i != net_.clientId();
            if (alive >= 2) ui_.view(0, 0, W, H, "WATCH NEXT (CLICK)", UiExp::state("spec"));
        }
        return;
    }
    if (screen_ != Screen::Editor) return;
    static const char* kScopes[] = {"box", "spawn", "light", "site", "hill", "checkpoint", "path", "ladder", "group"};
    const float panelX = W - 320 * s;
    if (chatting_) { say(); return; }
    ui_.where = edPath();
    if (ui_.typing()) return;
    if (edOnline_) {
        if (int k = bound(A_CHAT)) ui_.keyAction("CHAT", k, 0, UiExp::nav("editor-online/chat"));
        if (int k = bound(A_VOTEYES)) ui_.keyAction("VOTE YES", k, 0, UiExp::cmd("vote yes"));
        if (int k = bound(A_VOTENO)) ui_.keyAction("VOTE NO", k, 0, UiExp::cmd("vote no"));
    }
    if (edGroupMenu_ || edExportAsk_) return;
    if (!edSelValid()) ui_.view(0, 0, panelX, H, "3D VIEW (CLICK TO SELECT)", UiExp::state("edsel"));
    else {
        int kind = edSel_.kind;
        ui_.scope = kScopes[kind];
        bool point = kind == ED_SPAWN || kind == ED_LIGHT || kind == ED_PATH;
        bool mat = kind == ED_BRUSH || kind == ED_SPAWN || (kind == ED_GROUP && ed_.groups[edSel_.idx].type != Map::GROUP_CUSTOM);
        if (mat) ui_.keyAction("M (MATERIAL / TEAM)", GLFW_KEY_M, 0, UiExp::edit());
        ui_.keyAction("ARROW (MOVE)", GLFW_KEY_UP, 0, UiExp::edit());
        ui_.keyAction("PAGE UP (MOVE UP)", GLFW_KEY_PAGE_UP, 0, UiExp::edit());
        if (!point || kind == ED_LIGHT) ui_.keyAction("SHIFT+ARROW (GROW)", GLFW_KEY_UP, GLFW_MOD_SHIFT, UiExp::edit());
        if (!point) {   // (a side one grid step from the other doesn't move in)
            Vec3 f = angleForward(yaw_, 0);
            int fa = std::fabs(f.x) > std::fabs(f.z) ? 0 : 2;
            AABB b = edBounds(ed_, edSel_);
            bool thin = b.mx[fa] - b.mn[fa] <= 0.125f * (1 << edGridIdx_) + 1e-4f;
            ui_.keyAction("CTRL+ARROW (SHRINK)", GLFW_KEY_UP, GLFW_MOD_CONTROL, thin ? UiExp::inert() : UiExp::edit());
        }
        bool last = (kind == ED_BRUSH && ed_.brushes.size() <= 1) || (kind == ED_SPAWN && ed_.spawns.size() <= 1);
        ui_.keyAction("DELETE", GLFW_KEY_DELETE, 0, last ? UiExp::state("ednotice") : UiExp::edit());
        if (kind != ED_HILL) ui_.keyAction("CTRL+D (DUPLICATE)", GLFW_KEY_D, GLFW_MOD_CONTROL, UiExp::edit());
        ui_.keyAction("R (ROTATE)", GLFW_KEY_R, 0, edTransformExp(ED_ROT_Y));
        ui_.keyAction("F (MIRROR)", GLFW_KEY_F, 0, edTransformExp(ED_MIR_X));
        ui_.scope = nullptr;
    }
    ui_.keyAction("CTRL+Z (UNDO)", GLFW_KEY_Z, GLFW_MOD_CONTROL, edUndo_.empty() ? UiExp::state("ednotice") : UiExp::state("edmap"));
    if (!edOnline_) ui_.keyAction("CTRL+S (SAVE)", GLFW_KEY_S, GLFW_MOD_CONTROL, UiExp::state("usermaps"));
    ui_.keyAction("] (BIGGER GRID)", GLFW_KEY_RIGHT_BRACKET, 0, edGridIdx_ < 5 ? UiExp::state("edgrid") : UiExp::inert());
    ui_.keyAction("[ (SMALLER GRID)", GLFW_KEY_LEFT_BRACKET, 0, edGridIdx_ > 0 ? UiExp::state("edgrid") : UiExp::inert());
    ui_.where = nullptr;
}

void Game::uiTestHandles() {
    if (!edSelValid() || edGroupMenu_ || edExportAsk_ || chatting_ || edLook_ || edPrompt_) return;
    const float s = r_.height() / 720.0f, vw = (float)r_.width(), vh = (float)r_.height();
    static const char* kScopes[] = {"box", "spawn", "light", "site", "hill", "checkpoint", "path", "ladder", "group"};
    ui_.where = edPath();
    ui_.scope = kScopes[edSel_.kind];   // (begin() clears both)
    for (auto& hd : edHandles()) {
        float sx, sy, ex, ey;
        Vec3 a;
        a[hd.axis] = (float)hd.sign;
        if (!r_.project(hd.pos, sx, sy) || !r_.project(hd.pos + a, ex, ey)) continue;
        float dx = ex - sx, dy = ey - sy, len = std::hypot(dx, dy);
        // (one along the view direction can't be dragged from here: a player turns the camera first)
        if (len < 10 * s || sx < 10 * s || sy < 10 * s || sx > vw - 10 * s || sy > vh - 10 * s) continue;
        std::string label = std::string(hd.move ? "MOVE " : "RESIZE ") + (hd.sign > 0 ? "+" : "-") + "XYZ"[hd.axis];
        ui_.handle(sx - 6 * s, sy - 6 * s, 12 * s, 12 * s, dx / len, dy / len, label, UiExp::edit().optional());
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The test

namespace {
std::vector<std::string>* g_urls = nullptr;
void urlHook(const std::string& u) { if (g_urls) g_urls->push_back(u); }

void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c < 32) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

// the "key=value" lines of a config text whose key matches (an exact key, or "prefix*")
std::string cfgLines(const std::string& text, const std::string& key) {
    bool prefix = !key.empty() && key.back() == '*';
    std::string k = prefix ? key.substr(0, key.size() - 1) : key, out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string lk = l.substr(0, eq);
        if (prefix ? lk.rfind(k, 0) == 0 : lk == k) out += l + "\n";
    }
    return out;
}

const char* kindName(UiWidget::Kind k) {
    static const char* n[] = {"button", "toggle", "cycle", "slider", "text", "key", "handle", "view"};
    return n[k];
}
const char* expName(UiExp::Kind k) {
    static const char* n[] = {"NONE", "NAV", "CFG", "STATE", "CMD", "URL", "QUIT", "EDIT", "LOCAL", "INERT", "BIND"};
    return n[k];
}
std::string expText(const UiExp& e) {
    std::string t = expName(e.kind);
    if (e.what) t += std::string(" ") + e.what;
    if (e.arg && e.kind == UiExp::STATE) t += std::string(" = ") + e.arg;
    return t;
}
bool hasRect(const UiWidget& w) { return w.kind != UiWidget::KEY && w.kind != UiWidget::HANDLE && w.kind != UiWidget::VIEW; }
bool hasValue(const UiWidget& w) { return w.kind == UiWidget::TOGGLE || w.kind == UiWidget::CYCLE || w.kind == UiWidget::SLIDER || w.kind == UiWidget::TEXT; }
}

class UiTest : public UiProbe {
public:
    UiTest(Game& g, GLFWwindow* win, const LaunchOptions& o) : g_(g), win_(win), o_(o) {}
    void widget(const UiWidget& w) override { cur_.push_back(w); }
    int run();

private:
    // ---- scenarios: where exploring starts, how to get there, which screens belong to it
    struct Scenario {
        std::string name, root;
        std::vector<std::string> explore, skip;   // path prefixes explored here (and, within them, not)
        std::function<bool()> setup;              // from anywhere: get to the root (false: couldn't)
        std::function<bool()> healthy;            // still the situation it's about (else: set up again)
        // (optional) only these widgets belong to this scenario (the rest are another's): the screen path, the widget
        std::function<bool(const std::string&, const UiWidget&)> only = nullptr;
    };
    struct Step { std::string id; };
    struct Node {
        std::string path, ctx, ident;
        std::vector<Step> steps;                 // the way here from the root
        std::vector<std::vector<Step>> alts;     // other ways here (tried when replaying the first fails: timing, a host starting...)
        int parent = -1;
    };
    struct Rec {
        std::string scenario, path, ctx, id, kind, exp;
        bool activated = false, ok = true, optional = false;
        std::vector<std::string> fails;
    };

    Game& g_;
    GLFWwindow* win_;
    LaunchOptions o_;
    std::vector<UiWidget> cur_;            // the last frame's widgets
    std::vector<std::string> ids_;         // their ids ("#2" for a repeat), parallel to cur_
    std::vector<std::function<void()>> pending_;   // input for the next frame
    double last_ = 0;
    long frames_ = 0;
    int resW_ = 1280, resH_ = 720;
    std::string resName_ = "1280x720";
    bool layoutPass_ = false;              // replaying for the layout checks only
    bool registering_ = false;             // record widgets as seen (exploring)
    Settings baseline_;
    // what activating a widget did
    bool window_ = false;                  // an activation is running: commands are recorded and kept from the server
    std::vector<std::string> cmds_, urls_;
    bool quitSeen_ = false;
    // results
    const Scenario* sc_ = nullptr;
    std::vector<Node> nodes_;
    std::set<std::string> idents_;
    std::map<std::string, Rec> recs_;
    std::map<std::string, std::string> layout_;   // issue key -> description
    std::vector<std::string> escFails_, setupFails_, notes_;
    int escChecks_ = 0;
    std::vector<std::pair<std::string, std::string>> pendingCfg_;   // CFG keys changed since the last reset (key, widget)
    std::map<std::string, std::vector<Node>> nodesBy_;   // per scenario (the layout passes replay them)
    std::unique_ptr<Server> srv_;          // a server of our own (client / edit session guest scenarios)
    std::thread srvThread_;
    std::atomic<bool> srvQuit_{false};
    std::string demoPath_;
    bool holdDemo_ = false;
    struct EdView { bool set = false; Vec3 cam; float yaw = 0, pitch = 0, speed = 12; int grid = 1, mat = 0; } edView_;
    void saveEdView() { edView_ = {true, g_.edCam_, g_.yaw_, g_.pitch_, g_.edSpeed_, g_.edGridIdx_, g_.edMat_}; }

    // ---- frames and input
    void step(int n = 1) {
        while (n-- > 0) {
            std::string before = path() + "|" + ctx();
            cur_.clear();
            g_.beforeInput();
            glfwPollEvents();
            for (auto& f : pending_) f();
            pending_.clear();
            if (holdDemo_ && g_.net_.playback()) g_.net_.holdPlaybackClock();   // (the demo stays where it is while tested)
            double now = timeNow(), dt = std::min(0.1, std::max(0.0005, now - last_));
            last_ = now;
            g_.frame(dt);
            glfwSwapBuffers(win_);
            g_.afterSwap();
            frames_++;
            if (g_.quit_) { quitSeen_ = true; g_.quit_ = false; }
            std::map<std::string, int> n2;
            ids_.clear();
            for (auto& w : cur_) {
                std::string id = baseId(w);
                int k = ++n2[id];
                ids_.push_back(k > 1 ? id + "#" + std::to_string(k) : id);
            }
            // a frame whose click changed the screen drew the old screen's widgets: they aren't the new screen's
            transitional_ = before != path() + "|" + ctx();
            if (registering_ && !settingUp_ && sc_ && !transitional_ && explorable(path()) && sc_->healthy()) seeAll();
        }
    }
    bool transitional_ = false;
    bool settingUp_ = false;   // (a scenario being set up: its frames aren't explored)
    bool setup() { settingUp_ = true; bool ok = sc_->setup(); settingUp_ = false; return ok; }
    bool verbose_ = getenv("QUARK_UITEST_VERBOSE") != nullptr;
    static std::string baseId(const UiWidget& w) {
        std::string id = w.exp.id ? w.exp.id : w.label;
        if (id.empty()) id = std::string("(unnamed ") + kindName(w.kind) + ")";
        if (!w.enabled) id += " [off]";
        return id;
    }
    std::string path() const { return g_.uiScreenPath(); }
    std::string ctx() const { return g_.uiContext(); }
    std::string ident() const {
        std::set<std::string> s;
        for (auto& w : cur_) if (w.kind != UiWidget::HANDLE) s.insert(baseId(w));   // (handles come and go with the camera)
        std::string o = path() + "|" + ctx() + "|";
        for (auto& i : s) o += i + ";";
        return o;
    }
    // a widget's key: the scenario, the screen it belongs to, what it acts on, its id
    std::string keyOf(int i) const {
        const UiWidget& w = cur_[i];
        std::string where = w.where ? w.where : path();
        return sc_->name + " :: " + where + (w.scope ? std::string(" [") + w.scope + "]" : "") + " > " + ids_[i];
    }
    int find(const std::string& id) const {
        for (size_t i = 0; i < ids_.size(); i++) if (ids_[i] == id) return (int)i;
        return -1;
    }
    void moveTo(float x, float y) { pending_.push_back([=] { g_.onCursor(x, y); }); }
    void press(float x, float y) { pending_.push_back([=] { g_.onCursor(x, y); g_.onMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS); }); }
    void release() { pending_.push_back([=] { g_.onMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE); }); }
    void rclick(float x, float y) {
        pending_.push_back([=] { g_.onCursor(x, y); g_.onMouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS); });
        step();
        pending_.push_back([=] { g_.onMouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE); });
    }
    void click(float x, float y) {
        press(x, y);
        step();
        release();
        // the widget being clicked must still be there in the frame that took the click (else the layout moved: retried)
        if (!clickId_.empty()) {
            int j = find(clickId_);
            if (j < 0 || x < cur_[j].x || y < cur_[j].y || x >= cur_[j].x + cur_[j].w || y >= cur_[j].y + cur_[j].h) clickMoved_ = true;
            clickId_.clear();
        }
    }
    std::string clickId_;      // the widget the next click is for
    bool clickMoved_ = false;  // ...and it wasn't under the click any more
    void keyDown(int k, int mods = 0) { pending_.push_back([=] { g_.onKey(k, GLFW_PRESS, mods); }); }
    void keyUp(int k, int mods = 0) { pending_.push_back([=] { g_.onKey(k, GLFW_RELEASE, mods); }); }
    void key(int k, int mods = 0) { keyDown(k, mods); step(); keyUp(k, mods); }
    void type(const std::string& t) { pending_.push_back([=] { for (char c : t) g_.onChar((unsigned char)c); }); step(); }
    bool waitFor(const std::function<bool()>& f, double seconds) {
        double until = timeNow() + seconds;
        while (timeNow() < until) { step(); if (f()) return true; }
        return f();
    }

    // ---- the scenario's screens
    bool explorable(const std::string& p) const {
        if (!o_.uiTestPath.empty() && p != sc_->root && p.rfind(o_.uiTestPath, 0) != 0) return false;   // (--uitest-path)
        bool in = false;
        for (auto& e : sc_->explore) in |= p.rfind(e, 0) == 0;
        for (auto& e : sc_->skip) in &= p.rfind(e, 0) != 0;
        return in;
    }
    void seeAll() {
        for (size_t i = 0; i < cur_.size(); i++) {
            if (sc_->only && !sc_->only(path(), cur_[i])) continue;
            std::string k = keyOf((int)i);
            auto it = recs_.find(k);
            if (it != recs_.end()) continue;
            Rec r;
            r.scenario = sc_->name; r.path = cur_[i].where ? cur_[i].where : path(); r.ctx = cur_[i].scope ? cur_[i].scope : ""; r.id = ids_[i];
            r.kind = kindName(cur_[i].kind); r.exp = expText(cur_[i].exp);
            r.optional = cur_[i].exp.flags & UiExp::OPTIONAL;
            recs_[k] = r;
        }
        layoutCheck();
    }

    // ---- layout: off screen, overlapping, clipped (every frame while exploring, and the other resolutions' passes)
    void layoutCheck() {
        const float W = (float)g_.r_.width(), H = (float)g_.r_.height();
        std::string p = path();
        auto issue = [&](const std::string& id, const std::string& what) {
            std::string k = resName_ + " " + sc_->name + " :: " + p + " > " + id + " : " + what;
            layout_.emplace(k, what);
        };
        for (size_t i = 0; i < cur_.size(); i++) {
            const UiWidget& a = cur_[i];
            if (!hasRect(a)) continue;
            if (a.x < -0.5f || a.y < -0.5f || a.x + a.w > W + 0.5f || a.y + a.h > H + 0.5f)
                issue(ids_[i], "off screen");
            if (a.clipped && !(a.exp.flags & UiExp::CLIP_OK)) issue(ids_[i], "label clipped (doesn't fit)");
            for (size_t j = i + 1; j < cur_.size(); j++) {
                const UiWidget& b = cur_[j];
                if (!hasRect(b)) continue;
                float ox = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x), oy = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
                if (ox > 1 && oy > 1) issue(ids_[i], "overlaps " + ids_[j]);
            }
        }
    }

    // ---- getting around
    void hardReset() {
        Game& g = g_;
        g.ui_.clearFocus();
        g.paused_ = g.chatting_ = g.console_ = false;
        g.chatText_.clear();
        g.settingsInGame_ = g.voteMenu_ = g.queueMenu_ = g.controlsOpen_ = g.crosshairOpen_ = g.hudOpen_ = g.visualsOpen_ = false;
        if (g.widgetEdit_) g.widgets_.save(g.activeHudPath());
        g.widgetEdit_ = g.hudDrop_ = g.valueDrop_ = false;
        g.widgetSel_ = -1;
        g.filePick_ = 0;
        g.captureAction_ = -1;
        g.picker_ = Game::PICK_NONE;
        g.multiSel_.clear();
        g.raceStatsView_ = false;
        g.customTab_ = g.infoTab_ = 0;
        g.infoScroll_ = 0;
        g.hudSel_ = 0;
        g.ruleMode_ = MODE_FFA;
        g.edPrompt_ = false;
        g.error_.clear();
        g.testHeld_ = 0;
        g.showPerf_ = g.perfMini_ = false;
        g.edGroupMenu_ = g.edExportAsk_ = false;
        g.edSel_ = Game::EdSel();
        g.edMulti_.clear();
        g.edResetArmed_ = g.edLeaveArmed_ = g.edMsgUntil_ = 0;
        g.widgetMsgUntil_ = 0;
        g.notices_.clear();
        g.editRules_ = CustomRules();
        g.editOrigName_.clear();
        g.demoScroll_ = g.joinScroll_ = 0;
        g.queued_.clear();
        g.rpsMine_ = RPS_NONE;
        g.desiredWeapon_ = -1;
        // (the seeker back on the knife: the locator's slot key is offered again)
        if (g.screen_ == Game::Screen::InGame && isItMode(g.net_.mode().mode) && g.net_.selfWeapon() == WPN_LOCATOR) g.desiredWeapon_ = WPN_KNIFE;
        g.specPick_ = false;
        if (g.screen_ == Game::Screen::Editor && edView_.set) {
            g.edCam_ = edView_.cam; g.yaw_ = edView_.yaw; g.pitch_ = edView_.pitch;
            g.edGridIdx_ = edView_.grid; g.edMat_ = edView_.mat; g.edSpeed_ = edView_.speed; g.edLook_ = false;
            g.edLastClickX_ = g.edLastClickY_ = -1e9f;
        }
        // the settings as the test started (memory and file), and what follows from them
        Settings& c = g.cfg_;
        bool display = c.displayMode != baseline_.displayMode || c.width != baseline_.width || c.height != baseline_.height;
        c = baseline_;
        c.save();
        g.joinAddr_ = c.lastAddress;
        g.hostPortStr_ = std::to_string(c.hostPort);
        g.ui_.accent = rgba(c.colorFriendly);
        g.audio_.setMasterVolume(c.volume);
        g.audio_.setAnnouncer(c.announcerVoice);
        g.appliedMode_ = -2;
        if (display) g.applyDisplayMode();
        g.applyPresentMode();
        glfwSetClipboardString(win_, "");
        pendingCfg_.clear();
    }
    // Back to the scenario's root: Esc the way a player would (it saves settings on the way, which is checked), then
    // everything back as it started. Set up again if the situation is gone.
    bool toRoot() {
        window_ = false;
        bool walked = false;
        if (sc_->healthy()) {
            for (int i = 0; i < 12; i++) {
                if (path() == sc_->root) { walked = true; break; }
                if (!explorable(path())) break;
                g_.ui_.clearFocus();
                g_.captureAction_ = -1;
                key(GLFW_KEY_ESCAPE);
                step(2);
            }
        }
        if (walked) checkSaved();
        hardReset();
        step(2);
        if (!sc_->healthy() || path() != sc_->root) {
            if (!setup()) return false;
            hardReset();
            step(2);
        }
        return path() == sc_->root;
    }
    // what changed in the settings must be on disk once the screen was left
    void checkSaved() {
        if (pendingCfg_.empty()) return;
        Settings disk;
        disk.load();
        std::string mem = g_.cfg_.toText(), dt = disk.toText();
        for (auto& [key, wkey] : pendingCfg_) {
            if (cfgLines(mem, key) == cfgLines(dt, key)) continue;
            fail(wkey, "changed " + key + " but leaving the screen didn't save it (memory: " + cfgLines(mem, key) + " file: " + cfgLines(dt, key) + ")");
        }
        pendingCfg_.clear();
    }
    bool toNode(int ni) {
        std::vector<const std::vector<Step>*> ways = {&nodes_[ni].steps};
        for (auto& a : nodes_[ni].alts) ways.push_back(&a);
        lastReplay_.clear();
        for (auto* way : ways) {
            if (!toRoot()) { lastReplay_ += " [couldn't set the scenario up again]"; return false; }
            bool ok = true;
            for (auto& st : *way) if (!replay(st)) { ok = false; lastReplay_ += " [" + st.id + " not there]"; break; }
            step(2);
            if (ok) waitFor([&] { return path() == nodes_[ni].path && ctx() == nodes_[ni].ctx; }, 3.0);   // (an edit session's echo)
            if (ok && path() == nodes_[ni].path && ctx() == nodes_[ni].ctx) return true;
            if (ok) lastReplay_ += " [ended at " + path() + (ctx().empty() ? "" : " " + ctx()) + "]";
        }
        return false;
    }
    std::string lastReplay_;   // why the last toNode failed (notes)
    bool replay(const Step& st) {
        int i = -1;
        for (int t = 0; t < 90 && (i = find(st.id)) < 0; t++) step();
        if (i < 0) return false;
        UiWidget w = cur_[i];
        replaying_ = true;   // (its commands are kept from the server too: the replayed way here mustn't change the game)
        act(w, false);
        step(2);
        replaying_ = false;
        return true;
    }
    bool replaying_ = false;

    // ---- doing what a widget is for (verify: the activation is being tested, not replayed)
    void act(const UiWidget& w, bool verify, std::string* why = nullptr, const std::string& id = "") {
        float cx = w.x + w.w * 0.5f, cy = w.y + w.h * 0.5f;
        if (w.kind == UiWidget::BUTTON || w.kind == UiWidget::TOGGLE || w.kind == UiWidget::CYCLE) clickId_ = id;
        switch (w.kind) {
            case UiWidget::BUTTON: case UiWidget::TOGGLE:
                if (w.exp.kind == UiExp::BIND) { click(cx, cy); step(); key(GLFW_KEY_F7); break; }
                click(cx, cy);
                break;
            case UiWidget::VIEW: click(w.x + w.w * 0.42f, w.y + w.h * 0.62f); break;   // (off centre: not where handles tend to be)
            case UiWidget::CYCLE: click(w.x + w.w - w.h * 0.5f, cy); break;   // ">"
            case UiWidget::SLIDER: {
                float lo = w.dx, hi = w.dy, v = (float)atof(w.value.c_str()), f = 0.1f;
                if (std::fabs(lo + (hi - lo) * f - v) < (hi - lo) * 0.05f) f = 0.35f;
                press(w.x + w.w * f, cy); step(); release();
                break;
            }
            case UiWidget::TEXT: {
                click(cx, cy);
                keyDown(GLFW_KEY_BACKSPACE); step(); keyUp(GLFW_KEY_BACKSPACE);
                std::string t = w.exp.arg ? w.exp.arg : "7";
                if (!w.value.empty() && w.value.size() >= 1 && w.value.substr(0, w.value.size() - 1) + t == w.value) t = "8";
                type(t);
                if (verify && why) {   // the field took it (checked before Enter: Enter may leave the screen)
                    int j = find(id);
                    if (j >= 0 && cur_[j].value == w.value) *why = "typing didn't change the field";
                }
                key(GLFW_KEY_ENTER);
                break;
            }
            case UiWidget::KEY:
                if (w.count > 0) { g_.testHeld_ |= 1u << (w.count - 1); step(3); break; }   // held (released by the caller)
                if (w.exp.arg) type(w.exp.arg);
                key(w.key, w.mods);
                break;
            case UiWidget::HANDLE: {
                // (a click right where the last selecting click was steps through what's under it instead of grabbing
                // a handle: a player moves the mouse to a handle first, so that memory is cleared here)
                g_.edLastClickX_ = g_.edLastClickY_ = -1e9f;
                float d = 140 * (float)g_.r_.height() / 720.0f;
                press(cx, cy); step();
                if (verbose_) printf("    handle %s at %.0f %.0f dir %.2f %.2f: grabbed axis %d sign %d move %d\n", w.label.c_str(), cx, cy, w.dx, w.dy, g_.edDragAxis_, g_.edDragSign_, (int)g_.edDragMove_);
                for (int k = 1; k <= 4; k++) { moveTo(cx + w.dx * d * k / 4, cy + w.dy * d * k / 4); step(); }
                release(); step();
                break;
            }
        }
    }

    void fail(const std::string& key, const std::string& why) {
        auto it = recs_.find(key);
        if (it == recs_.end()) { notes_.push_back(key + ": " + why); return; }
        it->second.ok = false;
        it->second.fails.push_back(why);
    }

    // Activate the widget at cur_[i] and check it did what it declares. (A click the layout moved away from under is tried
    // once more from the state it left.)
    void activate(int i, bool again = false) {
        const UiWidget w = cur_[i];
        clickMoved_ = false;
        const std::string id = ids_[i], wkey = keyOf(i), path0 = path(), ctx0 = ctx();
        Rec& rec = recs_[wkey];
        rec.activated = true;
        const UiExp e = w.exp;
        if (e.kind == UiExp::NONE) { fail(wkey, "no expectation declared (UiExp)"); return; }
        const bool inert = e.kind == UiExp::INERT || !w.enabled || (w.kind == UiWidget::CYCLE && w.count < 2);
        // before
        std::string cfg0 = g_.cfg_.toText(), obs0 = e.kind == UiExp::STATE ? g_.uiObserve(e.what) : "";
        std::string ed0 = g_.screen_ == Game::Screen::Editor ? g_.ed_.serialize() : "";
        size_t undo0 = g_.edUndo_.size();
        const std::string back0 = g_.edUndo_.empty() ? "" : g_.edUndo_.back();
        std::string fp0 = path0 + "|" + ctx0 + "|" + cfg0 + "|" + ed0 + "|" + obs0;
        cmds_.clear(); urls_.clear(); quitSeen_ = false;
        window_ = true;
        g_.edLastPush_ = -1e9;   // (the editor joins changes within 0.75 s into one undo step: each widget here is its own)
        std::string why;
        act(w, true, &why, id);
        if (clickMoved_ && !again) {
            window_ = false;
            step(2);
            int j = find(id);
            if (verbose_) printf("    (%s moved while it was clicked: again)\n", id.c_str());
            if (j >= 0) { activate(j, true); return; }
        }
        // after: wait for it (hosting / connecting / the edit session's echo take a while)
        auto cmdSent = [&] { for (auto& c : cmds_) if (c.rfind(e.what, 0) == 0) return true; return false; };
        auto navOk = [&] {
            std::string p = path(), t = e.what;
            if (t[0] == '~') return p.size() >= t.size() - 1 && p.compare(p.size() - (t.size() - 1), t.size() - 1, t, 1, std::string::npos) == 0;
            if (t.back() == '*') return p.rfind(t.substr(0, t.size() - 1), 0) == 0;
            return p == t;
        };
        auto done = [&]() -> bool {
            switch (e.kind) {
                case UiExp::NAV: return navOk();
                case UiExp::CFG: case UiExp::BIND: return cfgLines(g_.cfg_.toText(), e.what ? e.what : "bind_*") != cfgLines(cfg0, e.what ? e.what : "bind_*");
                case UiExp::STATE: { std::string v = g_.uiObserve(e.what); return e.arg ? v == e.arg : v != obs0; }
                case UiExp::CMD: return cmdSent();
                case UiExp::URL: return !urls_.empty();
                case UiExp::QUIT: return quitSeen_;
                case UiExp::EDIT: return g_.screen_ == Game::Screen::Editor && g_.ed_.serialize() != ed0;
                default: return true;
            }
        };
        if (inert) {
            step(3);
            capturePost();
            std::string what;
            if (path() != path0) what += " screen (" + path() + ")";
            if (ctx() != ctx0) what += " selection (" + ctx() + ")";
            if (g_.cfg_.toText() != cfg0) what += " settings";
            if (g_.screen_ == Game::Screen::Editor && g_.ed_.serialize() != ed0) what += " map";
            if (e.kind == UiExp::STATE && g_.uiObserve(e.what) != obs0) what += std::string(" ") + e.what;
            for (auto& c : cmds_) if (!background(c)) what += " command [" + c + "]";
            if (!urls_.empty()) what += " opened " + urls_[0];
            if (quitSeen_) what += " quit";
            if (!what.empty()) fail(wkey, std::string(w.enabled ? "declared INERT but it changed:" : "greyed out, but clicking it changed:") + what);
        } else if (e.kind == UiExp::LOCAL && w.kind == UiWidget::TEXT) {
            step(1);   // (typing into it was checked before Enter: Enter may leave the screen, the join address connects)
            capturePost();
        } else if (e.kind == UiExp::LOCAL) {
            step(1);
            capturePost();
            int j = find(id);
            if (!hasValue(w)) fail(wkey, "LOCAL on a widget with no value of its own");
            else if (j < 0) fail(wkey, "gone after activating it (LOCAL expects it to stay)");
            else if (cur_[j].value == w.value) fail(wkey, "its value didn't change");
        } else {
            double wait = (e.kind == UiExp::STATE && e.arg) || e.kind == UiExp::EDIT ? 8.0 : 2.0;
            bool happened = waitFor(done, wait);
            if (transitional_) step();
            capturePost();   // (where it led, before any undo / "<" puts things back)
            if (!happened) {
                std::string got;
                if (e.kind == UiExp::NAV) got = " (screen is " + path() + ")";
                if (e.kind == UiExp::CMD) { got = " (sent:"; for (auto& c : cmds_) got += " [" + c + "]"; got += ")"; }
                if (e.kind == UiExp::STATE) got = " (" + std::string(e.what) + " = " + g_.uiObserve(e.what).substr(0, 80) + ")";
                if (e.kind == UiExp::EDIT) got = " (the editor says \"" + g_.edMsg_ + "\")";
                fail(wkey, std::string("expected ") + expText(e) + " but it didn't happen" + got);
            } else if (e.kind == UiExp::CFG || e.kind == UiExp::BIND) {
                std::string k = e.what ? e.what : "bind_*";
                // round trip: what the file would hold reads back the same
                std::string text = g_.cfg_.toText();
                Settings t;
                std::istringstream in(text);
                std::string l;
                while (std::getline(in, l)) { size_t eq = l.find('='); if (eq != std::string::npos) t.set(l.substr(0, eq), l.substr(eq + 1)); }
                t.finishLoad();
                if (cfgLines(t.toText(), k) != cfgLines(text, k)) fail(wkey, "the setting doesn't survive quark.cfg: " + cfgLines(text, k) + " reads back as " + cfgLines(t.toText(), k));
                if (e.kind == UiExp::BIND) {   // saved at once
                    Settings disk;
                    disk.load();
                    if (cfgLines(disk.toText(), k) != cfgLines(text, k)) fail(wkey, "the new bind wasn't saved");
                } else pendingCfg_.push_back({k, wkey});
                // (these change the window / frame pacing: back at once, and no "<" round trip)
                static const char* live[] = {"display_mode", "present_mode", "maxfps", "low_latency"};
                bool isLive = false;
                for (const char* lk : live) isLive |= k == lk;
                if (isLive) {
                    Settings& c = g_.cfg_;
                    c.displayMode = baseline_.displayMode; c.presentMode = baseline_.presentMode; c.maxFps = baseline_.maxFps; c.lowLatency = baseline_.lowLatency;
                    g_.applyDisplayMode();
                    g_.applyPresentMode();
                    pendingCfg_.pop_back();
                    step(2);
                } else if (w.kind == UiWidget::CYCLE && path() == path0) {   // "<" goes back to where it was
                    int j = find(id);
                    if (j >= 0 && cur_[j].count == w.count) {   // (a list that changed length, e.g. CUSTOM gone from PRESET: no)
                        UiWidget c2 = cur_[j];
                        click(c2.x + c2.h * 0.5f, c2.y + c2.h * 0.5f);
                        step();
                        int k2 = find(id);
                        if (k2 >= 0 && cur_[k2].value != w.value) fail(wkey, "\"<\" didn't go back to the value \">\" left (" + w.value + " -> " + cur_[k2].value + ")");
                    }
                }
            } else if (e.kind == UiExp::EDIT) {
                // undo puts the map back (the edit session: through the server, so wait for its echo)
                // (a new step: the stack grew, or - full at its 500 - its top is another one)
                bool stepped = g_.edUndo_.size() > undo0 || (!g_.edUndo_.empty() && g_.edUndo_.back() != back0);
                if (!stepped && w.kind != UiWidget::HANDLE) fail(wkey, "changed the map without an undo step");
                if (verbose_) printf("    undo %zu -> %zu, top [%s] -> [%s], title [%s]\n", undo0, g_.edUndo_.size(), back0.c_str(), g_.edUndo_.empty() ? "" : g_.edUndo_.back().c_str(), g_.ed_.title.c_str());
                g_.ui_.clearFocus();
                // (a text field edits as it's typed in, joined into one step within 0.75 s: a slow frame between the
                // keys can make two - each undone)
                int tries = w.kind == UiWidget::TEXT ? 3 : 1;
                bool back = false;
                for (int t = 0; t < tries && !back; t++) {
                    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
                    back = waitFor([&] { return g_.ed_.serialize() == ed0; }, t + 1 < tries ? 3.0 : 8.0);
                }
                if (!back) fail(wkey, "undo didn't put the map back");
            }
            if (!why.empty()) fail(wkey, why);
            // a widget with a value of its own shows the change
            if (hasValue(w) && path() == path0 && e.kind != UiExp::NAV) {
                int j = find(id);
                if (j >= 0 && cur_[j].value == w.value && w.kind != UiWidget::TEXT && e.kind != UiExp::EDIT && e.kind != UiExp::CFG) fail(wkey, "its own value didn't change");
            }
        }
        if (w.kind == UiWidget::KEY && w.count > 0) { g_.testHeld_ = 0; step(); }
        if (verbose_) printf("  %s %s -> %s%s\n", rec.ok ? "ok  " : "FAIL", wkey.c_str(), path().c_str(), rec.ok ? "" : (" : " + rec.fails.back()).c_str());
        // a slider: a number typed into it is kept exactly (off any grid a drag snaps to), and saved as typed
        if (w.kind == UiWidget::SLIDER && w.enabled && path() == path0) {
            int j = find(id);
            if (j >= 0) {
                UiWidget s2 = cur_[j];
                const float lo = s2.dx, hi = s2.dy, unit = std::pow(10.0f, (float)-s2.count);
                // 37% of the way along, in the slider's own decimals, ending in a 7 (no 0.05 / quarter / 15 degree /
                // 30 s step lands there); a short whole-number range: any whole number in it
                long n = std::lround((lo + (hi - lo) * 0.37f) / unit);
                n = n - n % 10 + 7;
                if (n * unit > hi) n -= 10;
                if (n * unit < lo) n = std::lround((lo + (hi - lo) * 0.37f) / unit);
                if (std::fabs(n * unit - (float)atof(s2.value.c_str())) < unit * 0.5f) n += n * unit + unit <= hi ? 1 : -1;   // (not what it shows now)
                const float target = n * unit;
                char buf[32];
                snprintf(buf, sizeof buf, "%.*f", s2.count, target);
                std::string edBefore = e.kind == UiExp::EDIT ? g_.ed_.serialize() : "";
                g_.edLastPush_ = -1e9;
                click(s2.nx + s2.nw * 0.5f, s2.y + s2.h * 0.5f);
                step();
                for (int k = 0; k < 12; k++) { keyDown(GLFW_KEY_BACKSPACE); step(); keyUp(GLFW_KEY_BACKSPACE); }
                type(buf);
                key(GLFW_KEY_ENTER);
                step();
                if (e.kind == UiExp::EDIT) waitFor([&] { return g_.ed_.serialize() != edBefore; }, 4.0);   // (an edit session: the server's echo)
                if (e.kind != UiExp::EDIT) step(2);   // (any snapping the next frames would do)
                // it shows `want` (to its decimals), and so does quark.cfg / the custom mode file read back (as at the next start)
                auto shows = [&](float want, const std::string& did) {
                    auto is = [&](int k) { return std::fabs((float)atof(cur_[k].value.c_str()) - want) < unit * 0.5f; };
                    int k2 = find(id);
                    if (k2 < 0) { fail(wkey, "gone after it was " + did); return; }
                    if (!is(k2)) { fail(wkey, did + ", it shows " + cur_[k2].value); return; }
                    if (e.kind == UiExp::CFG && path() == path0) {
                        pendingCfg_.push_back({e.what ? e.what : "", wkey});   // (and on disk once the screen's left)
                        std::istringstream in(g_.cfg_.toText());
                        Settings t;
                        std::string l;
                        while (std::getline(in, l)) { size_t eq = l.find('='); if (eq != std::string::npos) t.set(l.substr(0, eq), l.substr(eq + 1)); }
                        t.finishLoad();
                        Settings keep = g_.cfg_;
                        g_.cfg_ = t;
                        step(2);
                        int k3 = find(id);
                        if (k3 >= 0 && !is(k3)) fail(wkey, did + ", but quark.cfg reads back as " + cur_[k3].value);
                        g_.cfg_ = keep;
                        step();
                    } else if (e.kind == UiExp::STATE && e.what && std::string(e.what) == "custom" && path() == path0) {
                        CustomRules keep = g_.editRules_;
                        g_.editRules_.fromText(keep.toText());
                        step(2);
                        int k3 = find(id);
                        if (k3 >= 0 && !is(k3)) fail(wkey, did + ", but the custom mode file reads back as " + cur_[k3].value);
                        g_.editRules_ = keep;
                        step();
                    }
                };
                shows(target, std::string("typed ") + buf + " (a typed value must be kept as typed)");
                typedChecks_++;
                if (e.kind == UiExp::EDIT && g_.ed_.serialize() != edBefore) {   // (and that's undone too)
                    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
                    if (!waitFor([&] { return g_.ed_.serialize() == edBefore; }, 8.0)) fail(wkey, "undo didn't put back the typed value");
                }
                // a right-click puts its default back (and saves it); it doesn't start typing
                int k4 = find(id);
                if (k4 >= 0 && path() == path0) {
                    UiWidget s4 = cur_[k4];
                    const bool same = std::fabs((float)atof(s4.value.c_str()) - s4.def) < unit * 0.5f;
                    const std::string ed1 = e.kind == UiExp::EDIT ? g_.ed_.serialize() : "";
                    g_.edLastPush_ = -1e9;
                    rclick(s4.x + s4.w * 0.25f, s4.y + s4.h * 0.5f);
                    step();
                    if (e.kind == UiExp::EDIT && !same) waitFor([&] { return g_.ed_.serialize() != ed1; }, 4.0);
                    if (e.kind != UiExp::EDIT) step(2);
                    char db[32];
                    snprintf(db, sizeof db, "%.*f", s4.count, s4.def);
                    if (g_.ui_.typing()) fail(wkey, "a right-click started typing into it");
                    shows(s4.def, std::string("right-clicked (default ") + db + ")");
                    resetChecks_++;
                    if (e.kind == UiExp::EDIT && g_.ed_.serialize() != ed1) {
                        key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
                        if (!waitFor([&] { return g_.ed_.serialize() == ed1; }, 8.0)) fail(wkey, "undo didn't put back the value a right-click reset");
                    }
                }
            } else fail(wkey, "gone after dragging it (so a typed value couldn't be checked)");
        } else if (w.kind == UiWidget::SLIDER && w.enabled) fail(wkey, "dragging it left the screen (so a typed value couldn't be checked)");
        window_ = false;
        step();
        if (transitional_) step();
    }

    int typedChecks_ = 0;   // sliders a value was typed into (and kept)
    int resetChecks_ = 0;   // sliders right-clicked back to their default

    // the state an activation led to (a new screen, a selection...), before the test undid it
    std::string postPath_, postCtx_, postIdent_;
    void capturePost() { postPath_ = path(); postCtx_ = ctx(); postIdent_ = ident(); }

    // Esc from a screen goes back to one of the screens on the way to it
    void escCheck(int ni) {
        if (!toNode(ni)) return;
        std::set<std::string> back;
        for (int p = nodes_[ni].parent; p >= 0; p = nodes_[p].parent) back.insert(nodes_[p].path);
        g_.ui_.clearFocus();
        key(GLFW_KEY_ESCAPE);
        step(2);
        escChecks_++;
        std::string p = path();
        if (!back.count(p) && nodes_[ni].path.rfind(p + "/", 0) != 0)   // (a screen it came through, or the one it's part of)
            escFails_.push_back(sc_->name + " :: " + nodes_[ni].path + " : Esc went to " + p + " (not a screen on the way here)");
    }

    bool explore(int ni) {
        if (!toNode(ni)) {
            if (retry_[ni]++ < 2) return false;   // (later: another way here may turn up meanwhile)
            std::string way;
            for (auto& st : nodes_[ni].steps) way += " > " + st.id;
            notes_.push_back(sc_->name + " :: couldn't get back to " + nodes_[ni].path + " (" + way.substr(std::min<size_t>(3, way.size())) + "):" + lastReplay_);
            return true;
        }
        Node node = nodes_[ni];
        std::set<std::string> tried;
        for (int guard = 0; guard < 400; guard++) {
            if (!explorable(path())) break;
            int pick = -1;
            for (size_t i = 0; i < cur_.size(); i++) {
                if (sc_->only && !sc_->only(path(), cur_[i])) continue;
                std::string k = keyOf((int)i);
                auto it = recs_.find(k);
                if ((it != recs_.end() && it->second.activated) || tried.count(k)) continue;
                pick = (int)i;
                break;
            }
            if (pick < 0) break;
            tried.insert(keyOf(pick));
            std::string id = ids_[pick];
            postPath_.clear();
            UiExp::Kind pk = cur_[pick].exp.kind;
            activate(pick);
            if (pk == UiExp::EDIT && postPath_ == node.path && postCtx_ == node.ctx) postPath_.clear();
            // somewhere new: explore it later (by replaying the way here)
            if (!postPath_.empty() && explorable(postPath_) && !idents_.count(postIdent_) && nodes_.size() < 600) {
                idents_.insert(postIdent_);
                Node n;
                n.path = postPath_; n.ctx = postCtx_; n.ident = postIdent_; n.steps = node.steps; n.steps.push_back({id}); n.parent = ni;
                nodes_.push_back(n);
            } else if (!postPath_.empty() && explorable(postPath_)) {   // another way to a known state
                for (size_t k = 0; k < nodes_.size(); k++)
                    if (nodes_[k].ident == postIdent_ && (int)k != ni && nodes_[k].alts.size() < 3 && nodes_[k].steps.size() >= node.steps.size()) {
                        std::vector<Step> way = node.steps;
                        way.push_back({id});
                        if (way.size() <= nodes_[k].steps.size() + 1) nodes_[k].alts.push_back(way);
                        break;
                    }
            }
            // anything else changed (a toggle that shows more controls, a selection...): back to the node as recorded, so
            // what's found next is reached the way it's recorded
            if (path() != node.path || ctx() != node.ctx || !sc_->healthy() || !explorable(path()) || ident() != node.ident) {
                if (!toNode(ni)) { notes_.push_back(sc_->name + " :: lost " + node.path + " after " + id + ":" + lastReplay_); break; }
            }
        }
        if (node.parent >= 0 && node.path != nodes_[node.parent].path) escCheck(ni);
        return true;
    }
    std::map<int, int> retry_;

    void runScenario(const Scenario& sc) {
        sc_ = &sc;
        holdDemo_ = sc.name == "demo";   // (the demo stays at one moment while it's tested: it would end and leave)
        edView_.set = false;
        printf("uitest: scenario %s\n", sc.name.c_str());
        fflush(stdout);
        double t0 = timeNow();
        nodes_.clear();
        idents_.clear();
        registering_ = true;
        if (!setup()) { setupFails_.push_back(sc.name + ": couldn't set it up"); registering_ = false; return; }
        hardReset();
        step(3);
        Node root;
        root.path = path(); root.ctx = ctx(); root.ident = ident();
        idents_.insert(root.ident);
        nodes_.push_back(root);
        retry_.clear();
        std::vector<int> later;
        for (size_t ni = 0; ni < nodes_.size(); ni++) if (!explore((int)ni)) later.push_back((int)ni);
        for (size_t k = 0; k < later.size(); k++) if (!explore(later[k])) later.push_back(later[k]);   // (each at most twice more)
        toRoot();
        checkSaved();
        registering_ = false;
        nodesBy_[sc.name] = nodes_;
        printf("uitest: %s: %zu states in %.0f s\n", sc.name.c_str(), nodes_.size(), timeNow() - t0);
        fflush(stdout);
    }

    // ---- resolution: the window and the settings' windowed size
    void setResolution(int w, int h) {
        resW_ = w; resH_ = h;
        resName_ = std::to_string(w) + "x" + std::to_string(h);
        baseline_.width = w; baseline_.height = h;
        g_.cfg_.width = w; g_.cfg_.height = h;
        g_.applyDisplayMode();
        for (int i = 0; i < 30; i++) {
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(win_, &fw, &fh);
            if (fw == w && fh == h) break;
            step();
        }
        g_.onResize(w, h);
        step(2);
    }

    // a layout pass: every distinct screen of a scenario again, at this resolution
    void layoutPass(const Scenario& sc) {
        sc_ = &sc;
        holdDemo_ = sc.name == "demo";   // (the demo stays at one moment while it's tested: it would end and leave)
        edView_.set = false;
        auto it = nodesBy_.find(sc.name);
        if (it == nodesBy_.end()) return;
        nodes_ = it->second;
        if (!setup()) { setupFails_.push_back(sc.name + " (" + resName_ + "): couldn't set it up"); return; }
        std::set<std::string> done;
        for (size_t ni = 0; ni < nodes_.size(); ni++) {
            std::string k = nodes_[ni].path + "|" + nodes_[ni].ctx;
            if (done.count(k)) continue;
            done.insert(k);
            if (toNode((int)ni)) layoutCheck();
        }
        toRoot();
    }

    // ---- the scenarios' situations
    void leaveAll() {
        g_.testMap_.clear();
        g_.testEdit_ = false;
        if (g_.screen_ == Game::Screen::Editor && !g_.edOnline_) { g_.edLook_ = false; g_.r_.clearStaticBoxes(); g_.edMeshDirty_ = true; }
        g_.leaveGame();
        g_.edPrompt_ = false;
        g_.setScreen(Game::Screen::Main);
        stopServer();
        step(2);
    }
    bool inGame() const { return g_.screen_ == Game::Screen::InGame && g_.net_.state() == ConnState::Connected && g_.net_.hasSnapshot() && !g_.bake_ && g_.r_.worldLoaded(); }
    int mapIndex(const std::string& name) const {
        for (size_t i = 0; i < g_.maps_.size(); i++) if (g_.maps_[i] == name) return (int)i;
        return 0;
    }
    bool hostGame(int mode, const std::string& map, bool ranked, bool spectate) {
        leaveAll();
        hardReset();
        g_.cfg_.hostRanked = ranked;
        g_.cfg_.hostArenaVariant = 0;
        g_.cfg_.hostCustom.clear();
        g_.hostModeIdx_ = mode;
        g_.hostMapIdx_ = mapIndex(map);
        g_.startHost(false);
        if (!waitFor([&] { return inGame(); }, 120)) return false;
        waitFor([&] { return g_.host_ && g_.host_->reachability() != Server::REACH_CHECKING; }, 6);
        if (spectate) {
            g_.net_.sendCommand("team spec");
            if (!waitFor([&] { const PlayerSnap* me = g_.net_.findPlayer(g_.net_.clientId()); return me && me->team() == TEAM_SPECTATOR; }, 10)) return false;
        }
        return true;
    }
    bool ownServer(bool edit, bool spectate, int mode = MODE_FFA, const char* map = nullptr) {
        leaveAll();
        hardReset();
        ServerConfig sc;
        sc.name = edit ? "uitest edit" : "uitest";
        sc.map = map ? map : edit ? "harbor" : "temple";   // (the guest may save a copy: not of the ranked map the host scenarios use)
        sc.mode = mode;
        sc.port = 0;
        sc.lanOnly = true;
        sc.vsBots = !edit;
        sc.botsEnemy = edit ? 0 : mode == MODE_HIDE ? 2 : 3;
        sc.editMode = edit;
        srv_ = std::make_unique<Server>();
        srv_->setLog([](const std::string&) {});
        std::string err;
        if (!srv_->start(sc, &err)) { notes_.push_back("own server: " + err); srv_.reset(); return false; }
        srvQuit_ = false;
        Server* s = srv_.get();
        srvThread_ = std::thread([this, s] { s->run(srvQuit_); });
        g_.connectTo("127.0.0.1:" + std::to_string(srv_->port()));
        if (edit) {
            bool ok = waitFor([&] { return g_.screen_ == Game::Screen::Editor && g_.edOnline_; }, 30);
            if (ok) saveEdView();
            return ok;
        }
        if (!waitFor([&] { return inGame(); }, 120)) return false;
        if (spectate) {
            g_.net_.sendCommand("team spec");
            if (!waitFor([&] { const PlayerSnap* me = g_.net_.findPlayer(g_.net_.clientId()); return me && me->team() == TEAM_SPECTATOR; }, 10)) return false;
        }
        return true;
    }
    // our own server's game, changed between its ticks: its thread stopped, ticked here, then started again
    void pauseServer() { srvQuit_ = true; if (srvThread_.joinable()) srvThread_.join(); }
    void resumeServer() { srvQuit_ = false; Server* s = srv_.get(); srvThread_ = std::thread([this, s] { s->run(srvQuit_); }); }
    void pumpServer(double seconds) { double until = timeNow() + seconds; while (timeNow() < until) { srv_->frame(); sleepMs(1); } }
    // hide and seek with us seeking (the seeker holds the knife and the locator, slot key 0)
    bool hideSeeker() {
        if (!ownServer(false, false, MODE_HIDE, "hamlet")) return false;
        pauseServer();
        Server& s = *srv_;
        s.command("forcestart");
        pumpServer(0.3);
        // seekers take turns in slot order, a round each: rounds are ended until it's our turn
        const int me = g_.net_.clientId();
        for (int k = 0; k < 24 && me >= 0 && me < kMaxPlayers && !s.players_[me].it; k++) { s.phaseEnd_ = s.time(); pumpServer(0.1); }
        resumeServer();
        return waitFor([&] { return iAmIt(); }, 15);
    }
    bool iAmIt() const { const PlayerSnap* me = g_.net_.findPlayer(g_.net_.clientId()); return me && (me->flags & PS_HASBOMB) && isItMode(g_.net_.mode().mode); }
    // the hide and seek tiebreak (rock paper scissors among those tied for first), held at its throw
    bool hideRps() {
        if (!ownServer(false, false, MODE_HIDE, "hamlet")) return false;
        pauseServer();
        Server& s = *srv_;
        s.command("forcestart");
        pumpServer(0.2);
        s.phaseEnd_ = s.time(); pumpServer(0.1);   // live
        s.phaseEnd_ = s.time(); pumpServer(0.1);   // time's up: the round's over
        for (auto& p : s.players_) if (p.connected) { p.sought = true; p.score = 3; }   // every turn played, all tied
        s.phaseEnd_ = s.time(); pumpServer(0.2);   // the last round over: tied, so the tiebreak
        bool ok = (s.ms_.flags & MS_RPS) && s.rps_.stage == RPS_PICK;
        if (ok) s.rps_.stageEnd = s.phaseEnd_ = s.time() + 100000;   // (the throw stays open while it's tested)
        resumeServer();
        return ok && waitFor([&] { return g_.rpsPicking() && inGame(); }, 15);
    }
    void stopServer() {
        if (!srv_) return;
        srvQuit_ = true;
        if (srvThread_.joinable()) srvThread_.join();
        srv_->stop();
        srv_.reset();
    }
    bool recordDemo() {
        if (!hostGame(MODE_FFA, "temple", false, false)) return false;
        g_.startRecording();
        waitFor([] { return false; }, 2.0);
        g_.net_.stopRecording();
        leaveAll();
        std::error_code ec;
        for (auto& e : fs::directory_iterator(fs::u8path(userDataPath("demos")), ec))
            if (e.path().extension() == ".qdemo") demoPath_ = e.path().u8string();
        return !demoPath_.empty();
    }

    std::vector<Scenario> scenarios() {
        std::vector<Scenario> v;
        auto hosted = [this](int mode, bool ranked, bool spec) {
            return [=] { return g_.screen_ == Game::Screen::InGame && g_.host_ && g_.net_.state() == ConnState::Connected && g_.net_.hasSnapshot() &&
                                g_.net_.mode().mode == (ranked ? MODE_ARENA : mode) && !(g_.net_.mode().flags & MS_EDITING) &&
                                ((g_.net_.mode().flags & MS_RANKED) != 0) == ranked &&
                                (spec == (g_.net_.findPlayer(g_.net_.clientId()) && g_.net_.findPlayer(g_.net_.clientId())->team() == TEAM_SPECTATOR)); };
        };
        // in game: every in-game screen once (host, free for all); the other situations: what differs there
        const std::vector<std::string> inGameAll = {"ingame"};
        const std::vector<std::string> noSettings = {"ingame/pause/settings", "ingame/pause/queue"};
        v.push_back({"host-ffa", "ingame", inGameAll, {}, [=] { return hostGame(MODE_FFA, "temple", false, false); }, hosted(MODE_FFA, false, false)});
        v.push_back({"host-vs", "ingame", inGameAll, noSettings, [=] { return hostGame(MODE_VS, "temple", false, false); }, hosted(MODE_VS, false, false)});
        v.push_back({"host-tourney", "ingame", inGameAll, noSettings, [=] { return hostGame(MODE_TOURNEY, "temple", false, false); }, hosted(MODE_TOURNEY, false, false)});
        v.push_back({"host-ranked", "ingame", inGameAll, noSettings, [=] { return hostGame(MODE_ARENA, "temple", true, false); }, hosted(MODE_ARENA, true, false)});
        v.push_back({"client-spectator", "ingame", inGameAll, noSettings, [=] { return ownServer(false, true); },
                     [=] { return g_.screen_ == Game::Screen::InGame && !g_.host_ && srv_ && g_.net_.state() == ConnState::Connected && g_.net_.hasSnapshot(); }});
        v.push_back({"demo", "ingame", inGameAll, noSettings,
                     [=] {
                         leaveAll();
                         if (demoPath_.empty() && !recordDemo()) return false;
                         if (!g_.playDemo(demoPath_)) return false;
                         return waitFor([&] { return inGame(); }, 60);
                     },
                     [=] { return g_.screen_ == Game::Screen::InGame && g_.net_.playback(); }});
        // hide and seek: the seeker's locator (slot key 0), and the tiebreak's rock / paper / scissors keys
        v.push_back({"hide-seeker", "ingame", inGameAll, {"ingame/"}, [=] { return hideSeeker(); },
                     [=] { return g_.screen_ == Game::Screen::InGame && srv_ && g_.net_.hasSnapshot() && iAmIt(); }});
        v.push_back({"hide-rps", "ingame", inGameAll, {"ingame/"}, [=] { return hideRps(); },
                     [=] { return g_.screen_ == Game::Screen::InGame && srv_ && g_.net_.hasSnapshot() && g_.rpsPicking(); }});
        // a built-in map with edits: leaving / saving / testing it asks "keep your edits as a new map?" (each way)
        Scenario builtin{"editor-builtin", "editor", {"editor"}, {"editor-online"},
                     [=] {
                         leaveAll();
                         g_.openEditor("temple");
                         step(2);
                         g_.edDo("t Temple (my edits)", "t " + g_.ed_.title);
                         step(2);
                         saveEdView();
                         return g_.screen_ == Game::Screen::Editor && g_.edDirty_ && g_.edOfficial();
                     },
                     [=] { return g_.screen_ == Game::Screen::Editor && !g_.edOnline_ && (g_.edPrompt_ || (g_.edDirty_ && g_.edOfficial())); }};
        builtin.only = [](const std::string& p, const UiWidget& w) {   // (the rest of the editor is the menus scenario's)
            return p != "editor" || (w.exp.kind == UiExp::NAV && w.exp.what && std::string(w.exp.what).find("keep-edits") != std::string::npos);
        };
        v.push_back(builtin);
        v.push_back({"edit-host", "editor-online", {"editor-online"}, {},
                     [=] {
                         leaveAll();
                         g_.openEditor("");
                         step(2);
                         g_.edHostSession();
                         bool ok = waitFor([&] { return g_.screen_ == Game::Screen::Editor && g_.edOnline_ && g_.net_.hasSnapshot(); }, 60);
                         if (ok) saveEdView();
                         return ok;
                     },
                     [=] { return g_.screen_ == Game::Screen::Editor && g_.edOnline_ && g_.host_ && g_.net_.state() == ConnState::Connected; }});
        v.push_back({"edit-guest", "editor-online", {"editor-online", "main/save-copy"}, {},
                     [=] {   // (the built-in map, changed: leaving always asks "save your copy?" - unchanged, it doesn't)
                         if (!ownServer(true, false)) return false;
                         const std::string t0 = g_.ed_.title, t1 = "Harbor (uitest copy)";
                         g_.edDo("t " + t1, "t " + t0, false, false);
                         bool ok = waitFor([&] { return g_.ed_.title == t1 && g_.net_.map().title == t1; }, 10);
                         step(2);
                         return ok;
                     },
                     [=] { return (g_.screen_ == Game::Screen::Editor && g_.edOnline_ && srv_ && g_.net_.state() == ConnState::Connected) || g_.edPrompt_; }});
        // the menus (and the offline editor) last: the editor's RESET MAPS drops the light caches the games use
        v.push_back({"menus", "main", {"main", "join", "host", "vsbots", "settings", "leaderboard", "info", "demos", "custom", "connecting", "editor"}, {"editor-online"},
                     [=] { leaveAll(); return path() == "main"; },
                     [=] { return g_.screen_ != Game::Screen::InGame && !(g_.screen_ == Game::Screen::Editor && g_.edOnline_) && !g_.host_ &&
                                  (g_.screen_ == Game::Screen::Connecting || g_.net_.state() == ConnState::Disconnected); }});
        return v;
    }

    // ---- the report
    int report(double seconds) {
        int widgets = 0, passed = 0, failed = 0, untested = 0, noExp = 0;
        std::map<std::string, std::array<int, 3>> perScreen;   // widgets, pass, fail
        std::vector<const Rec*> bad, unt;
        std::set<std::string> screens;
        for (auto& [k, r] : recs_) {
            widgets++;
            screens.insert(r.scenario + " :: " + r.path);
            auto& ps = perScreen[r.path];
            ps[0]++;
            if (!r.activated) {
                if (r.optional) { ps[1]++; passed++; continue; }
                untested++; ps[2]++; unt.push_back(&r);
                continue;
            }
            if (r.ok) { passed++; ps[1]++; }
            else { failed++; ps[2]++; bad.push_back(&r); for (auto& f : r.fails) noExp += f.rfind("no expectation", 0) == 0; }
        }
        bool ok = failed == 0 && untested == 0 && escFails_.empty() && layout_.empty() && setupFails_.empty();
        std::string out = o_.uiTestReport.empty() ? "uitest-report.json" : o_.uiTestReport;
        std::ofstream f(utf8Path(out));
        f << "{\n  \"result\": " << jsonStr(ok ? "pass" : "fail") << ",\n  \"seconds\": " << (int)seconds << ",\n  \"frames\": " << frames_
          << ",\n  \"widgets\": " << widgets << ",\n  \"screens\": " << screens.size() << ",\n  \"passed\": " << passed << ",\n  \"failed\": " << failed
          << ",\n  \"untested\": " << untested << ",\n  \"esc_checks\": " << escChecks_ << ",\n  \"typed_slider_checks\": " << typedChecks_ << ",\n  \"slider_reset_checks\": " << resetChecks_ << ",\n  \"per_screen\": {\n";
        size_t n = 0;
        for (auto& [p, c] : perScreen)
            f << "    " << jsonStr(p) << ": {\"widgets\": " << c[0] << ", \"pass\": " << c[1] << ", \"fail\": " << c[2] << "}" << (++n < perScreen.size() ? ",\n" : "\n");
        f << "  },\n  \"failures\": [\n";
        n = 0;
        for (auto* r : bad) {
            std::string why;
            for (auto& x : r->fails) why += (why.empty() ? "" : "; ") + x;
            f << "    {\"scenario\": " << jsonStr(r->scenario) << ", \"screen\": " << jsonStr(r->path + (r->ctx.empty() ? "" : " [" + r->ctx + "]"))
              << ", \"widget\": " << jsonStr(r->id) << ", \"kind\": " << jsonStr(r->kind) << ", \"expects\": " << jsonStr(r->exp) << ", \"why\": " << jsonStr(why) << "}"
              << (++n < bad.size() ? ",\n" : "\n");
        }
        f << "  ],\n  \"untested_widgets\": [\n";
        n = 0;
        for (auto* r : unt)
            f << "    {\"scenario\": " << jsonStr(r->scenario) << ", \"screen\": " << jsonStr(r->path + (r->ctx.empty() ? "" : " [" + r->ctx + "]"))
              << ", \"widget\": " << jsonStr(r->id) << ", \"expects\": " << jsonStr(r->exp) << "}" << (++n < unt.size() ? ",\n" : "\n");
        auto list = [&](const char* name, const std::vector<std::string>& v, bool last) {
            f << "  " << jsonStr(name) << ": [";
            for (size_t i = 0; i < v.size(); i++) f << (i ? ",\n    " : "\n    ") << jsonStr(v[i]);
            f << (v.empty() ? "]" : "\n  ]") << (last ? "\n" : ",\n");
        };
        f << "  ],\n";
        std::vector<std::string> lay;
        for (auto& [k, v] : layout_) lay.push_back(k);
        list("layout", lay, false);
        list("esc", escFails_, false);
        list("setup", setupFails_, false);
        list("notes", notes_, true);
        f << "}\n";
        f.close();
        // the summary
        printf("\n==== UI TEST: %s ====\n", ok ? "PASS" : "FAIL");
        printf("%d widgets on %zu screens (%zu scenarios), %d passed, %d failed, %d never tested; %d Esc checks; %d sliders typed into, %d right-clicked back to default; %zu layout issues; %.0f s, %ld frames\n",
               widgets, screens.size(), nodesBy_.size(), passed, failed, untested, escChecks_, typedChecks_, resetChecks_, layout_.size(), seconds, frames_);
        printf("per screen (widgets / pass / fail):\n");
        for (auto& [p, c] : perScreen) printf("  %-42s %4d %4d %4d\n", p.c_str(), c[0], c[1], c[2]);
        for (auto* r : bad) {
            printf("FAIL %s :: %s%s > %s [%s, %s]\n", r->scenario.c_str(), r->path.c_str(), r->ctx.empty() ? "" : (" [" + r->ctx + "]").c_str(), r->id.c_str(), r->kind.c_str(), r->exp.c_str());
            for (auto& x : r->fails) printf("       %s\n", x.c_str());
        }
        for (auto* r : unt) printf("UNTESTED %s :: %s%s > %s [%s]\n", r->scenario.c_str(), r->path.c_str(), r->ctx.empty() ? "" : (" [" + r->ctx + "]").c_str(), r->id.c_str(), r->exp.c_str());
        for (auto& [k, v] : layout_) printf("LAYOUT %s\n", k.c_str());
        for (auto& e : escFails_) printf("ESC %s\n", e.c_str());
        for (auto& e : setupFails_) printf("SETUP %s\n", e.c_str());
        for (auto& e : notes_) printf("NOTE %s\n", e.c_str());
        if (noExp) printf("(%d widgets have no expectation: give each one a UiExp, docs/UI_TEST.md)\n", noExp);
        printf("report: %s\n", out.c_str());
        fflush(stdout);
        return ok ? 0 : 1;
    }

    // what the game sends by itself (camera, looks, interp...), not because of a widget
    static bool background(const std::string& c) {
        static const char* bg[] = {"edcam ", "model ", "interp ", "typing", "ticketsig ", "name "};
        for (const char* p : bg) if (c.rfind(p, 0) == 0) return true;
        return false;
    }
    static bool cmdHook(void* ctx, const std::string& c) {
        UiTest* t = (UiTest*)ctx;
        if (!t->window_ && !t->replaying_) return true;
        if (t->window_) t->cmds_.push_back(c);
        // an activation's commands are checked, not run: the game (and the server) stay the scenario's. The edit
        // session's ops go through (EDIT checks the map that comes back), and the background ones.
        return c.rfind("ed ", 0) == 0 || background(c);
    }
};

int UiTest::run() {
    double t0 = timeNow();
    g_.uiTest_ = this;
    g_.ui_.probe = this;
    g_.ui_.steadyCaret = true;
    g_.net_.commandHook = &UiTest::cmdHook;
    g_.net_.commandHookCtx = this;
    g_urls = &urls_;
    g_openUrlHook = urlHook;
    baseline_ = g_.cfg_;
    last_ = timeNow();
    setResolution(1280, 720);
    std::vector<Scenario> all = scenarios();
    auto wanted = [&](const std::string& n) { return o_.uiTestOnly.empty() || ("," + o_.uiTestOnly + ",").find("," + n + ",") != std::string::npos; };
    for (auto& sc : all) if (wanted(sc.name)) runScenario(sc);
    // the other resolutions: the same screens, layout only (16:9 large, 4:3)
    for (auto res : {std::make_pair(1920, 1080), std::make_pair(1024, 768)}) {
        setResolution(res.first, res.second);
        printf("uitest: layout at %s\n", resName_.c_str());
        fflush(stdout);
        for (auto& sc : all) if (wanted(sc.name)) layoutPass(sc);
    }
    leaveAll();
    int rc = report(timeNow() - t0);
    g_.ui_.probe = nullptr;
    g_.uiTest_ = nullptr;
    g_.net_.commandHook = nullptr;
    g_openUrlHook = nullptr;
    g_urls = nullptr;
    return rc;
}

// Before Game::init: a data folder of its own (nothing of the player's is read or changed), nothing on the internet,
// and the settings the test starts every screen from.
void uiTestPrepare(LaunchOptions& o) {
    setEnv("QUARK_MASTER", "off");        // no server list / ranked / queues / Discord
    setEnv("QUARK_PUBLIC_IP", "off");
    setEnv("QUARK_NO_LAUNCHER_UPDATE", "1");
    std::random_device rd;
    unsigned tag = rd();
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / ("quark-uitest-" + std::to_string(tag % 1000000));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    setUserDataDir(dir.u8string(), false);
    Settings s;
    s.lastAddress = "127.0.0.1:9";        // JOIN connects nowhere
    s.hostPort = 27600 + (int)(tag % 300);   // (a port of its own: other games on this machine don't collide)
    s.hostProber = "127.0.0.1:9";         // the reachability check answers "not reachable" (and CHECK AGAIN shows)
    s.hostUpnp = false;
    s.hostPublic = false;
    s.width = 1280; s.height = 720;
    s.binds.code[A_TOGGLECHAT][0] = GLFW_KEY_F6;   // (unbound by default: bound here so the key is tested too)
    s.save();
    printf("uitest: data in %s\n", dir.u8string().c_str());
    (void)o;
}

int runUiTest(Game& game, GLFWwindow* win, const LaunchOptions& opts) {
    UiTest t(game, win, opts);
    return t.run();
}
