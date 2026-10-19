#include <array>
#include "game.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include "../game/gamemodes.h"

namespace {
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kRed = {1, 0.35f, 0.3f, 1};

struct Palette { const char* name; Vec3 friendly, enemy; };
const Palette kPalettes[] = {
    {"CYAN / RED", {0.25f, 0.85f, 1.0f}, {1.0f, 0.3f, 0.25f}},
    {"BLUE / ORANGE", {0.3f, 0.55f, 1.0f}, {1.0f, 0.6f, 0.15f}},
    {"GREEN / MAGENTA", {0.35f, 1.0f, 0.45f}, {1.0f, 0.3f, 0.85f}},
    {"WHITE / YELLOW", {0.95f, 0.95f, 0.95f}, {1.0f, 0.85f, 0.1f}},
};
constexpr int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

std::string fmtTime(float s) {
    int t = (int)std::ceil(std::max(0.0f, s));
    char b[16];
    snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}

const char* weaponName(int w) {
    if (w == 252) return "fell";
    if (w == 253) return "bomb";
    if (w == 254) return "drained";
    if (w == 255) return "suicide";
    return weaponDef(w).name;
}
}

bool Game::init(GLFWwindow* win, const LaunchOptions& opts, std::string* err) {
    win_ = win;
    opts_ = opts;
    cfg_.load();
    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    r_.resize(w, h);
    if (!r_.init(cfg_.shadowRes, err)) return false;
    maps_ = listMaps();
    for (auto& name : maps_) {
        Map m;
        MapInfo mi;
        if (m.load(name)) {
            mi.title = m.title.empty() ? name : m.title;
            mi.preferredMode = m.preferredMode;
            for (int k = 0; k < MODE_COUNT; k++) if (m.supportsMode(k)) mi.modes |= 1u << k;
        }
        mapInfo_.push_back(mi);
    }
    for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == cfg_.hostMap) hostMapIdx_ = (int)i;
    hostModeIdx_ = clampf((float)cfg_.hostMode, 0, MODE_COUNT - 1);
    cfg_.hostMaxPlayers = std::min(std::max(cfg_.hostMaxPlayers, 2), kMaxPlayers);
    hostPortStr_ = std::to_string(cfg_.hostPort);
    joinAddr_ = cfg_.lastAddress;
    ui_.accent = rgba(cfg_.colorFriendly);
    applyDisplayMode();
    applyPresentMode();
    for (auto& q : latQ_) glGenQueries(1, &q.q);
    if (!audio_.start()) printf("audio: no output device - running without sound\n");
    audio_.setMasterVolume(cfg_.volume);

    botMapIdx_ = bestMapFor(cfg_.botMode);
    if (opts_.vsBotsMode >= 0) {
        cfg_.botMode = opts_.vsBotsMode;
        botMapIdx_ = bestMapFor(cfg_.botMode);
        if (!opts_.hostMap.empty())
            for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == opts_.hostMap) botMapIdx_ = (int)i;
        startHost(true);
    } else if (opts_.hostMode >= 0) {
        hostModeIdx_ = opts_.hostMode;
        if (!opts_.hostMap.empty())
            for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == opts_.hostMap) hostMapIdx_ = (int)i;
        cfg_.hostUpnp = false;
        cfg_.hostPublic = false; // command-line test hosts stay off the internet list
        startHost();
    } else if (opts_.debugPicker) {
        setScreen(Screen::Host);
        picker_ = PICK_HOST_MAP;
    } else if (!opts_.connect.empty()) {
        connectTo(opts_.connect);
    }
    return true;
}

void Game::shutdown() {
    audio_.stop();
    leaveGame();
    cfg_.save();
}

// ---------------------------------------------------------------------------
// hosting / connection

void Game::startHost(bool vsBots) {
    stopHost();
    if (maps_.empty()) { error_ = "no maps found in maps/"; return; }
    ServerConfig sc;
    sc.name = cfg_.hostName;
    sc.map = maps_[hostMapIdx_ % maps_.size()];
    sc.mode = hostModeIdx_;
    sc.port = (uint16_t)atoi(hostPortStr_.c_str());
    if (sc.port == 0) sc.port = kDefaultPort;
    sc.maxPlayers = cfg_.hostMaxPlayers;
    sc.upnp = cfg_.hostUpnp;
    sc.balanceBots = cfg_.hostBalanceBots;
    sc.logLevel = cfg_.hostLog;
    sc.duelTeamSize = cfg_.hostDuelSize;
    sc.publicList = cfg_.hostPublic;
    if (vsBots) { // private local game: any free port, no port forwarding, fixed bots
        sc.name = "vs bots";
        sc.map = maps_[botMapIdx_ % maps_.size()];
        sc.mode = cfg_.botMode;
        sc.port = 0;
        sc.upnp = false;
        sc.lanOnly = true;
        sc.maxPlayers = kMaxPlayers;
        sc.vsBots = true;
        sc.botSkill = cfg_.botSkill;
        sc.botsEnemy = cfg_.botEnemies;
        sc.botsAlly = modeIsTeamBased(sc.mode) ? cfg_.botAllies : 0;
        sc.botsEnemy = std::min(sc.botsEnemy, kMaxPlayers - 1 - sc.botsAlly);
    }
    host_ = std::make_unique<Server>();
    host_->setLog([](const std::string& s) { printf("[host] %s\n", s.c_str()); });
    std::string e;
    if (!host_->start(sc, &e)) {
        error_ = "could not host: " + e;
        host_.reset();
        setScreen(vsBots ? Screen::VsBots : Screen::Host);
        return;
    }
    if (!vsBots) {
        cfg_.hostMap = sc.map;
        cfg_.hostMode = sc.mode;
        cfg_.hostPort = sc.port;
    }
    cfg_.save();
    hostQuit_ = false;
    Server* srv = host_.get();
    hostThread_ = std::thread([this, srv] { raiseThreadPriority(); srv->run(hostQuit_); });
    connectTo("127.0.0.1:" + std::to_string(host_->port()));
}

void Game::stopHost() {
    if (!host_) return;
    hostQuit_ = true;
    if (hostThread_.joinable()) hostThread_.join();
    host_->stop();
    host_.reset();
}

void Game::connectTo(const std::string& rawAddr) {
    error_.clear();
    std::string addr = rawAddr; // pasted addresses often carry spaces / newlines
    while (!addr.empty() && (unsigned char)addr.back() <= ' ') addr.pop_back();
    while (!addr.empty() && (unsigned char)addr[0] <= ' ') addr.erase(0, 1);
    if (!net_.connect(addr, cfg_.name)) { error_ = net_.error(); return; }
    if (!host_) { cfg_.lastAddress = addr; cfg_.save(); }
    setScreen(Screen::Connecting);
}

void Game::leaveGame(const std::string& error) {
    net_.disconnect();
    stopHost();
    error_ = error;
    paused_ = chatting_ = false;
    killFeed_.clear(); chatFeed_.clear(); notices_.clear();
    tracers_.clear(); explosions_.clear(); lastProjectiles_.clear();
    loadedMapGen_ = 0;
    setScreen(Screen::Main);
}

void Game::setScreen(Screen s) {
    screen_ = s;
    ui_.clearFocus();
    captureMouse(s == Screen::InGame && !paused_ && !chatting_);
    if (s == Screen::Join) browser_.refresh();
}

void Game::captureMouse(bool on) {
    if (on == mouseCaptured_) return;
    mouseCaptured_ = on;
    glfwSetInputMode(win_, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(win_, GLFW_RAW_MOUSE_MOTION, on ? GLFW_TRUE : GLFW_FALSE);
    firstMouse_ = true;
}

void Game::loadMapIfNeeded() {
    if (!net_.mapReady() || net_.mapGeneration() == loadedMapGen_) return;
    loadedMapGen_ = net_.mapGeneration();
    double lastKeepAlive = 0;
    r_.loadWorld(net_.map(), [&](float p) {
        // Keep the connection alive and show progress while lighting bakes.
        net_.poll();
        if (timeNow() - lastKeepAlive > 0.05) {
            UserCmd c; c.yaw = yaw_; c.pitch = pitch_;
            net_.tick(c);
            lastKeepAlive = timeNow();
        }
        r_.rect(0, 0, (float)r_.width(), (float)r_.height(), {0.05f, 0.06f, 0.08f, 1});
        float bw = r_.width() * 0.4f, bx = (r_.width() - bw) * 0.5f, by = r_.height() * 0.55f;
        r_.rect(bx, by, bw, 8, {0.2f, 0.2f, 0.25f, 1});
        r_.rect(bx, by, bw * p, 8, rgba(cfg_.colorFriendly));
        float ts = std::max(2.0f, std::round(r_.height() / 360.0f));
        std::string t = "BAKING LIGHT - " + net_.map().name;
        r_.text((r_.width() - r_.textWidth(t, ts)) * 0.5f, by - 30 * ts / 2, ts, t, kWhite);
        r_.flush2D();
        glfwSwapBuffers(win_);
    });
}

// ---------------------------------------------------------------------------
// input

void Game::onChar(unsigned cp) {
    if (cp < 128) uiIn_.chars += (char)cp;
}

// The map built for this mode if there is one, else the first map that supports it.
int Game::bestMapFor(int mode) const {
    int fallback = -1;
    for (size_t i = 0; i < maps_.size(); i++) {
        Map m;
        if (!m.load(maps_[i]) || !m.supportsMode(mode)) continue;
        if (m.preferredMode == mode) return (int)i;
        if (fallback < 0) fallback = (int)i;
    }
    return fallback < 0 ? 0 : fallback;
}

static std::string upper(std::string t) {
    for (auto& c : t) c = (char)toupper((unsigned char)c);
    return t;
}

bool Game::isDown(int action) const {
    for (int k = 0; k < 2; k++) {
        int c = cfg_.binds.code[action][k];
        if (c >= kMouseCode && c < kMouseCode + 16) { if (glfwGetMouseButton(win_, c - kMouseCode) == GLFW_PRESS) return true; }
        else if (c > 0 && c < kMouseCode && glfwGetKey(win_, c) == GLFW_PRESS) return true;
        else if ((c == kWheelUp || c == kWheelDown) && timeNow() < wheelHeldUntil_[c - kWheelUp]) return true;
    }
    return false;
}

// A key, mouse button or scroll step was pressed: rebinding capture, or the bound one-shot actions.
bool Game::pressInput(int code) {
    if (captureAction_ >= 0) {
        if (code == GLFW_KEY_ESCAPE) { captureAction_ = -1; return true; }
        int& slot = cfg_.binds.code[captureAction_][captureSlot_];
        slot = (code == GLFW_KEY_BACKSPACE || code == GLFW_KEY_DELETE) ? 0 : code;
        // An input does one thing: binding it here removes it from every other action (otherwise the
        // default wheel = next/prev weapon kept firing alongside e.g. wheel = jump).
        if (slot)
            for (int a = 0; a < A_COUNT; a++)
                for (int k = 0; k < 2; k++)
                    if ((a != captureAction_ || k != captureSlot_) && cfg_.binds.code[a][k] == slot) cfg_.binds.code[a][k] = 0;
        captureAction_ = -1;
        cfg_.save();
        return true;
    }
    const Binds& b = cfg_.binds;
    if (b.has(A_PERF, code)) { showPerf_ = !showPerf_; return true; }
    if (b.has(A_SCREENSHOT, code)) {
        static int n = 0;
        r_.screenshot("screenshot_" + std::to_string(n++) + ".png");
        notify("screenshot saved");
        return true;
    }
    if (screen_ != Screen::InGame || chatting_) return false;
    if (b.has(A_VOTEYES, code)) { net_.sendCommand("vote yes"); return true; }
    if (b.has(A_VOTENO, code)) { net_.sendCommand("vote no"); return true; }
    if (b.has(A_VOTESTART, code)) { net_.sendCommand("callvote start"); return true; }
    if (paused_) return false;
    if (b.has(A_CHAT, code)) {
        chatting_ = true;
        chatText_.clear();
        uiIn_.chars.clear();
        captureMouse(false);
        return true;
    }
    for (int w = 0; w < WPN_COUNT; w++) if (b.has(A_WEAPON1 + w, code)) { desiredWeapon_ = w; return true; }
    int cur = desiredWeapon_ >= 0 ? desiredWeapon_ : net_.selfWeapon();
    if (b.has(A_NEXTWEAPON, code)) { desiredWeapon_ = (cur + 1) % WPN_COUNT; return true; }
    if (b.has(A_PREVWEAPON, code)) { desiredWeapon_ = (cur + WPN_COUNT - 1) % WPN_COUNT; return true; }
    if (b.has(A_FIRE, code)) fireLatched_ = true; // a click shorter than a tick still fires
    return false;
}

void Game::onKey(int key, int action, int mods) {
    markInput();
    if (action == GLFW_RELEASE) return;
    if (key == GLFW_KEY_BACKSPACE) uiIn_.backspace = true;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) uiIn_.enter = true;
    if (key == GLFW_KEY_TAB) uiIn_.tab = true;
    // clipboard: Ctrl+V / Cmd+V pastes into the focused text field or chat, Ctrl+C copies the field
    bool cmd = (mods & GLFW_MOD_CONTROL) || (mods & GLFW_MOD_SUPER);
    if (cmd && key == GLFW_KEY_V && (ui_.typing() || chatting_)) {
        if (const char* clip = glfwGetClipboardString(win_))
            for (const char* c = clip; *c; c++) if (*c >= 32 && *c < 127) uiIn_.chars += *c;
        return;
    }
    if (cmd && key == GLFW_KEY_C && (ui_.typing() || chatting_)) {
        if (chatting_) glfwSetClipboardString(win_, chatText_.c_str());
        else uiIn_.copy = true;
        return;
    }
    if (action != GLFW_PRESS) return;
    bool typing = chatting_ || ui_.typing();
    if (captureAction_ >= 0 || !typing) {
        if (pressInput(key)) return;
    }
    if (screen_ == Screen::InGame) {
        if (chatting_) {
            if (key == GLFW_KEY_ESCAPE) { chatting_ = false; chatText_.clear(); captureMouse(!paused_); }
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                if (!chatText_.empty()) {
                    if (chatText_[0] == '/') net_.sendCommand(chatText_.substr(1));
                    else net_.sendCommand("say " + chatText_);
                }
                chatText_.clear();
                chatting_ = false;
                captureMouse(!paused_);
            }
            return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            if (controlsOpen_ || crosshairOpen_) { controlsOpen_ = crosshairOpen_ = false; cfg_.save(); return; }
            if (settingsInGame_) { settingsInGame_ = false; cfg_.save(); return; }
            if (voteMenu_ || picker_ != PICK_NONE) { voteMenu_ = false; picker_ = PICK_NONE; return; }
            paused_ = !paused_;
            captureMouse(!paused_);
            return;
        }
    } else if (key == GLFW_KEY_ESCAPE) {
        uiIn_.escape = true;
        if (controlsOpen_ || crosshairOpen_) { controlsOpen_ = crosshairOpen_ = false; cfg_.save(); return; }
        if (picker_ != PICK_NONE) { picker_ = PICK_NONE; return; }
        if (screen_ == Screen::Join || screen_ == Screen::Host || screen_ == Screen::Settings || screen_ == Screen::VsBots) {
            cfg_.save();
            setScreen(Screen::Main);
        } else if (screen_ == Screen::Connecting) {
            leaveGame();
        }
    }
}

void Game::onMouseButton(int button, int action) {
    markInput();
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) { uiIn_.pressed = captureAction_ < 0; uiIn_.down = true; }
        else if (action == GLFW_RELEASE) uiIn_.down = false;
    }
    if (action == GLFW_PRESS) {
        // in menus the left button belongs to the UI (except when rebinding)
        bool uiClick = button == GLFW_MOUSE_BUTTON_LEFT && (screen_ != Screen::InGame || paused_) && captureAction_ < 0;
        if (!uiClick) pressInput(kMouseCode + button);
    }
}

void Game::onCursor(double x, double y) {
    markInput();
    if (mouseCaptured_) {
        if (firstMouse_) { lastMx_ = x; lastMy_ = y; firstMouse_ = false; return; }
        double dx = x - lastMx_, dy = y - lastMy_;
        lastMx_ = x; lastMy_ = y;
        float k = deg2rad(cfg_.sensitivity) * 0.01f * 2.2f;
        if (scoped_) k *= std::tan(deg2rad(viewFov()) * 0.5f) / std::tan(deg2rad(cfg_.fov) * 0.5f); // same feel when zoomed
        yaw_ -= (float)dx * k;
        pitch_ = clampf(pitch_ - (float)dy * k, -1.55f, 1.55f);
        yaw_ = std::remainder(yaw_, 2 * PI);
    } else {
        // UI coordinates are framebuffer pixels (handles HiDPI).
        int ww, wh, fw, fh;
        glfwGetWindowSize(win_, &ww, &wh);
        glfwGetFramebufferSize(win_, &fw, &fh);
        uiIn_.mx = (float)(x * fw / std::max(1, ww));
        uiIn_.my = (float)(y * fh / std::max(1, wh));
    }
}

void Game::onResize(int w, int h) { r_.resize(w, h); }

void Game::onScroll(double dy) {
    markInput();
    if (dy == 0) return;
    // One action per whole notch: smooth-scrolling trackpads and high-res wheels send many small
    // steps, which used to skip through several weapons at once. Reversing direction starts over.
    if ((dy > 0) != (wheelAccum_ > 0)) wheelAccum_ = 0;
    wheelAccum_ += dy;
    while (std::fabs(wheelAccum_) >= 0.999) {
        int code = wheelAccum_ > 0 ? kWheelUp : kWheelDown;
        wheelAccum_ -= wheelAccum_ > 0 ? 1.0 : -1.0;
        wheelHeldUntil_[code - kWheelUp] = timeNow() + 0.03;
        pressInput(code);
        if (captureAction_ < 0 && screen_ != Screen::InGame) break;
    }
}

UserCmd Game::buildCmd() {
    UserCmd c;
    c.yaw = yaw_;
    c.pitch = pitch_;
    bool block = paused_ || chatting_ || screen_ != Screen::InGame;
    if (block) return c;
    int f = 0, s = 0;
    if (isDown(A_FORWARD)) f += 127;
    if (isDown(A_BACK)) f -= 127;
    if (isDown(A_RIGHT)) s += 127;
    if (isDown(A_LEFT)) s -= 127;
    c.forward = (int8_t)clampf((float)f, -127, 127);
    c.side = (int8_t)clampf((float)s, -127, 127);
    if (isDown(A_JUMP)) c.buttons |= BTN_JUMP;
    if (isDown(A_USE)) c.buttons |= BTN_USE;
    if (isDown(A_DASH)) c.buttons |= BTN_DASH;
    if (isDown(A_GRAPPLE)) c.buttons |= BTN_GRAPPLE;
    if (desiredWeapon_ >= 0) {
        if (desiredWeapon_ == net_.selfWeapon()) desiredWeapon_ = -1;
        else c.weapon = (uint8_t)desiredWeapon_;
    }
    if (isDown(A_FIRE) || fireLatched_) c.buttons |= BTN_FIRE;
    if (isDown(A_ALTFIRE)) c.buttons |= BTN_ALTFIRE;
    fireLatched_ = false;
    if (opts_.debugWeapon >= 1 && opts_.debugWeapon <= WPN_COUNT) c.weapon = (uint8_t)(opts_.debugWeapon - 1);
    if (opts_.debugScope) c.buttons |= BTN_ALTFIRE;
    if (opts_.debugFire) c.buttons |= BTN_FIRE;
    return c;
}

void Game::notify(const std::string& s, float seconds) {
    notices_.push_back({s, kWhite, time_ + seconds});
    while (notices_.size() > 4) notices_.pop_front();
}

bool Game::isFriendly(int id) const {
    if (id == net_.clientId()) return true;
    const PlayerSnap* me = net_.findPlayer(net_.clientId());
    const PlayerSnap* p = net_.findPlayer(id);
    if (!me || !p || !net_.hasSnapshot()) return false;
    if (!modeIsTeamBased(net_.mode().mode)) return false;
    int myTeam = me->team() == TEAM_SPECTATOR ? TEAM_A : me->team();
    return p->team() == myTeam;
}

Vec3 Game::teamColor(int id) const { return isFriendly(id) ? cfg_.colorFriendly : cfg_.colorEnemy; }

void Game::handleEvents() {
    for (auto& e : net_.takeEvents()) {
        switch (e.type) {
            case EV_KILL: {
                std::string killer = e.a == 255 ? "" : net_.playerName(e.a);
                std::string t = killer.empty() || e.a == e.b ? net_.playerName(e.b) + " [" + weaponName(e.c) + "]"
                                                             : killer + " [" + weaponName(e.c) + "] " + net_.playerName(e.b);
                Color c = e.a == net_.clientId() ? rgba(cfg_.colorFriendly) : e.b == net_.clientId() ? rgba(cfg_.colorEnemy) : kWhite;
                killFeed_.push_back({t, c, time_ + 6});
                while (killFeed_.size() > 6) killFeed_.pop_front();
                if (e.b == net_.clientId() && e.a != 255 && e.a != e.b) specTarget_ = e.a;
                if (e.a == net_.clientId() && e.b != e.a && cfg_.hitsounds) audio_.play(SND_KILL, 0.9f);
                break;
            }
            case EV_CHAT:
                chatFeed_.push_back({net_.playerName(e.a) + ": " + e.text, isFriendly(e.a) ? rgba(cfg_.colorFriendly) : rgba(cfg_.colorEnemy), time_ + 10});
                while (chatFeed_.size() > 8) chatFeed_.pop_front();
                break;
            case EV_MESSAGE:
                chatFeed_.push_back({e.text, kGrey, time_ + 8});
                while (chatFeed_.size() > 8) chatFeed_.pop_front();
                break;
            case EV_ROUND_WIN:
                centerText_ = e.text;
                centerUntil_ = time_ + (e.b ? 8.0 : 3.5);
                break;
            case EV_VOTE:
                voteText_ = e.text;
                break;
            case EV_HIT:
                if (e.a == net_.clientId()) {
                    hitUntil_ = time_ + 0.15;
                    if (cfg_.hitsounds) audio_.play(SND_HIT, 0.9f, 1.25f - std::min((int)e.c, 50) / 100.0f); // bigger hits sound deeper
                    // hits on the same target in quick succession add up into one number
                    bool merged = false;
                    for (auto& d : dmgNums_)
                        if (d.victim == e.b && time_ - d.start < 0.35) { d.amount += e.c; d.start = time_; merged = true; }
                    if (!merged) {
                        const PlayerSnap* v = net_.findPlayer(e.b);
                        dmgNums_.push_back({e.b, (float)e.c, time_, v ? v->pos : Vec3()});
                    }
                }
                break;
            case EV_SPAWN:
                if (e.a == net_.clientId()) {
                    yaw_ = e.c / 1000.0f;
                    pitch_ = 0;
                    if (opts_.spawnYaw > -999) { yaw_ = deg2rad(opts_.spawnYaw); pitch_ = deg2rad(opts_.spawnPitch); }
                }
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// main loop

void Game::frame(double dt) {
    time_ += dt;
    if (opts_.debugPerf) { showPerf_ = true; if (pendingInput_ < 0) pendingInput_ = timeNow(); }
    if (opts_.debugPause && screen_ == Screen::InGame && !paused_) { paused_ = true; voteMenu_ = opts_.debugVote; opts_.debugPause = false; }
    if (opts_.debugSettings && screen_ == Screen::InGame && !settingsInGame_) { paused_ = true; settingsInGame_ = true; controlsOpen_ = opts_.debugControls; crosshairOpen_ = opts_.debugCrosshair; opts_.debugSettings = false; }
    frameInput_ = pendingInput_; // the oldest input this frame will show
    pendingInput_ = -1;
    frameTimes_[frameIdx_] = (float)dt;
    frameIdx_ = (frameIdx_ + 1) % frameTimes_.size();
    frameCount_ = std::min(frameCount_ + 1, frameTimes_.size());
    net_.poll();
    if ((screen_ == Screen::InGame || opts_.debugPicker) && !opts_.screenshot.empty() && shotAt_ < 0) shotAt_ = time_ + opts_.shotDelay;

    if (screen_ == Screen::Join) browser_.poll();
    if (screen_ == Screen::Connecting || screen_ == Screen::InGame) {
        if (net_.state() == ConnState::Disconnected) {
            leaveGame(net_.error().empty() ? "disconnected" : net_.error());
        } else if (net_.state() == ConnState::Connected && net_.mapReady()) {
            loadMapIfNeeded();
            if (screen_ == Screen::Connecting) { setScreen(Screen::InGame); }
        }
    }

    if (screen_ == Screen::InGame) {
        handleEvents();
        // fixed-rate input + prediction
        tickAcc_ += dt;
        if (tickAcc_ > 0.25) tickAcc_ = 0.25;
        const double period = net_.tickPeriod();
        while (tickAcc_ >= period) {
            UserCmd c = buildCmd();
            // local fire feedback (server is authoritative for damage)
            bool wantFire = (c.buttons & BTN_FIRE) != 0;
            if (weaponDef(net_.selfWeapon()).semiAuto) { // mirror the server: one shot per click, short buffer
                if (wantFire && !localFireHeld_) localFireQueued_ = time_ + kFireBuffer;
                localFireHeld_ = wantFire;
                wantFire = time_ <= localFireQueued_;
            } else localFireHeld_ = wantFire;
            if (wantFire && net_.selfAlive() && time_ >= localNextFire_ && net_.mode().phase != PHASE_FREEZE) {
                localFireQueued_ = -1;
                int w = net_.selfWeapon();
                localNextFire_ = time_ + weaponDef(w).refire;
                audio_.play(SND_RIFLE + w, w == WPN_LASER ? 0.6f : 0.8f, 0.97f + (rand() % 7) * 0.01f);
                recoil_ = w == WPN_LASER ? 0.15 : w == WPN_SNIPER ? 1.6 : 1.0;
                if (w == WPN_LASER) laserUntil_ = time_ + 0.07;
                else if (!weaponDef(w).projectile && w != WPN_KNIFE) {
                    const MoveState& m = net_.predicted();
                    Vec3 eye = m.pos + Vec3(0, (m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight, 0);
                    Vec3 aim = angleForward(yaw_, pitch_);
                    Vec3 right = normalize(cross(aim, {0, 1, 0})), up = cross(right, aim);
                    const WeaponDef& wd = weaponDef(w);
                    float spread = (wd.scopedSpread >= 0 && (c.buttons & BTN_ALTFIRE)) ? wd.scopedSpread : wd.spread;
                    for (int k = 0; k < wd.pellets; k++) { // same seeded spread as the server, so tracers show the real shot
                        Vec3 d = aim;
                        if (spread > 0) {
                            float a, r;
                            spreadOffset(net_.nextSeq(), k, spread, a, r);
                            d = normalize(aim + right * (std::cos(a) * r) + up * (std::sin(a) * r));
                        }
                        float t = net_.map().raycast(eye, d, wd.range);
                        if (t < 0) t = wd.range;
                        tracers_.push_back({eye + aim * 0.5f + right * 0.15f - Vec3(0, 0.12f, 0), eye + d * t, time_ + (w == WPN_SNIPER ? 0.3 : 0.06)});
                    }
                }
            }
            net_.tick(c);
            tickAcc_ -= period;
        }
        scopeOverlay_ = net_.selfWeapon() == WPN_SNIPER; // crossbow: just a light zoom, normal crosshair
        scoped_ = net_.selfAlive() && !paused_ && !chatting_ && weaponDef(net_.selfWeapon()).scopedSpread >= 0 &&
                  (isDown(A_ALTFIRE) || opts_.debugScope);
        // explosions: projectiles that vanished
        auto projs = net_.interpolatedProjectiles();
        for (auto& old : lastProjectiles_) {
            bool still = false;
            for (auto& p : projs) still |= p.id == old.id;
            if (!still && weaponDef(old.type).splashRadius > 0) { explosions_.push_back({old.pos, time_}); audio_.play3D(SND_EXPLOSION, old.pos, 1.0f); }
            else if (!still && old.type == WPN_CROSSBOW) audio_.play3D(SND_BOLT_HIT, old.pos, 0.8f);
            if (still && old.type == WPN_STICKY && length(old.vel) > 0.01f)
                for (auto& p : projs) if (p.id == old.id && length(p.vel) < 0.01f) audio_.play3D(SND_STICKY_ATTACH, p.pos, 0.8f);
        }
        lastProjectiles_ = projs;

        int hp = net_.selfHealth();
        if (hp < lastHealth_ && net_.selfAlive()) { damageUntil_ = time_ + 0.25; audio_.play(SND_HURT, 0.7f); }
        lastHealth_ = hp;
        if (!net_.selfAlive() && uiIn_.pressed && !paused_) {
            // cycle spectate target
            auto ps = net_.interpolatedPlayers();
            for (int k = 1; k <= kMaxPlayers; k++) {
                int id = (specTarget_ + k + kMaxPlayers) % kMaxPlayers;
                if (ps[id].valid && (ps[id].snap.flags & PS_ALIVE) && id != net_.clientId()) { specTarget_ = id; break; }
            }
        }
        captureMouse(!paused_ && !chatting_);
    }

    // ---- render ----
    if (screen_ == Screen::InGame && net_.hasSnapshot() && r_.worldLoaded()) {
        drawWorld((float)clampf((float)(tickAcc_ / net_.tickPeriod()), 0, 1));
        drawHud();
    } else {
        r_.setCamera({0, 20, 30}, (float)time_ * 0.05f, -0.4f, 100);
        if (r_.worldLoaded()) r_.drawScene(false);
        else {
            r_.rect(0, 0, (float)r_.width(), (float)r_.height(), {0.05f, 0.06f, 0.08f, 1});
        }
    }
    drawMenu();
    std::string copied = ui_.takeCopy();
    if (!copied.empty()) glfwSetClipboardString(win_, copied.c_str());
    if (showPerf_) drawPerf();
    r_.flush2D();

    if (shotAt_ > 0 && time_ >= shotAt_ && (screen_ == Screen::InGame || opts_.debugPicker)) {
        r_.screenshot(opts_.screenshot);
        printf("screenshot written to %s\n", opts_.screenshot.c_str());
        quit_ = true;
    }
    uiIn_.pressed = false;
    uiIn_.chars.clear();
    uiIn_.backspace = uiIn_.enter = uiIn_.tab = uiIn_.escape = uiIn_.copy = false;
}

// ---------------------------------------------------------------------------
// world rendering

void Game::drawWorld(float alpha) {
    const Map& map = net_.map();
    const ModeState& ms = net_.mode();
    auto players = net_.interpolatedPlayers();
    int me = net_.clientId();
    bool alive = net_.selfAlive();

    // camera: own prediction when alive, else spectate someone
    Vec3 eye;
    float camYaw = yaw_, camPitch = pitch_;
    int viewing = me;
    if (alive) {
        Vec3 p = net_.smoothedPos(alpha);
        bool crouch = net_.predicted().flags & MF_CROUCHED;
        eye = p + Vec3(0, crouch ? kCrouchEyeHeight : kEyeHeight, 0);
    } else {
        if (specTarget_ < 0 || !players[specTarget_].valid || !(players[specTarget_].snap.flags & PS_ALIVE)) {
            specTarget_ = -1;
            for (int i = 0; i < kMaxPlayers; i++)
                if (i != me && players[i].valid && (players[i].snap.flags & PS_ALIVE) && (isFriendly(i) || !modeIsTeamBased(ms.mode))) { specTarget_ = i; break; }
        }
        if (specTarget_ >= 0) {
            const PlayerSnap& t = players[specTarget_].snap;
            viewing = specTarget_;
            camYaw = t.yaw;
            camPitch = t.pitch;
            eye = t.pos + Vec3(0, (t.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight, 0);
        } else {
            const PlayerSnap* mp = net_.findPlayer(me);
            Vec3 base = mp ? mp->pos : Vec3(0, 0, 0);
            eye = base + Vec3(0, 3.0f, 0);
        }
    }
    r_.setCamera(eye, camYaw, camPitch, alive ? viewFov() : cfg_.fov);
    audio_.setListener(eye, camYaw);
    updateSounds(players);

    // players (duel "double size" rounds draw everyone at twice the size, matching the server hitboxes)
    const ModeState& dms = net_.latest().ms;
    const float bs = dms.mode == MODE_DUEL && dms.phase != PHASE_WARMUP && dms.duelMod == DUEL_BIG ? kBigScale : 1.0f;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players[i].valid) continue;
        const PlayerSnap& p = players[i].snap;
        if (!(p.flags & PS_ALIVE)) continue;
        bool self = i == me;
        bool firstPerson = (self && alive) || i == viewing;
        Vec3 pos = self && alive ? net_.smoothedPos(alpha) : p.pos;
        float yaw = self && alive ? yaw_ : p.yaw;
        float pitch = self && alive ? pitch_ : p.pitch;
        bool crouch = self && alive ? (net_.predicted().flags & MF_CROUCHED) : (p.flags & PS_CROUCHED);
        float hs = crouch ? kPlayerCrouchHeight / kPlayerHeight : 1.0f;
        Vec3 col = teamColor(i);
        float sv = sunVisibility(map, pos + Vec3(0, 1.0f * hs * bs, 0));
        bool vis = !firstPerson;
        auto at = [&](float h) { return pos + Vec3(0, h * hs * bs, 0); };
        r_.box(at(0.45f), Vec3(0.28f, 0.45f * hs, 0.2f) * bs, yaw, col * 0.55f, sv, true, vis);
        r_.box(at(1.15f), Vec3(0.38f, 0.28f * hs, 0.24f) * bs, yaw, col, sv, true, vis);
        r_.box(pos + Vec3(0, 1.6f * hs * bs, 0), Vec3(0.2f, 0.18f, 0.2f) * bs, yaw, col * 1.15f, sv, true, vis);
        if (vis) {
            Vec3 fwd = {-std::sin(yaw), 0, -std::cos(yaw)};
            r_.box(at(1.62f) + fwd * (0.2f * bs), Vec3(0.16f, 0.05f, 0.02f) * bs, yaw, {0.05f, 0.05f, 0.06f}, sv, false);
            Vec3 shoulder = at(1.3f) + Vec3(std::cos(yaw), 0, -std::sin(yaw)) * (0.3f * bs);
            r_.boxPitched(shoulder, Vec3(0, 0, -0.35f) * bs, Vec3(0.06f, 0.07f, 0.35f) * bs, yaw, pitch, {0.12f, 0.12f, 0.14f}, sv);
            if (p.flags & PS_HASBOMB)
                r_.box(at(1.15f) + fwd * -0.32f, {0.2f, 0.22f, 0.1f}, yaw, {0.1f, 0.1f, 0.1f}, sv);
        }
        // name tags for teammates
        if (!self && isFriendly(i) && i != viewing) {
            float sx, sy;
            if (r_.project(pos + Vec3(0, 2.1f, 0), sx, sy)) {
                std::string n = net_.playerName(i);
                float ts = ui_.textScale() * 0.5f;
                ts = std::max(1.0f, std::round(ts));
                r_.text(sx - r_.textWidth(n, ts) * 0.5f, sy, ts, n, rgba(col));
            }
        }
    }

    // grapple ropes
    if (alive && (net_.predicted().flags & MF_HOOKED)) {
        Vec3 d = angleForward(yaw_, pitch_);
        Vec3 right = normalize(cross(d, {0, 1, 0}));
        r_.line(eye - Vec3(0, 0.25f, 0) - right * 0.15f, net_.predicted().hookPoint, 0.03f, {0.1f, 0.1f, 0.1f, 1});
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == me || !players[i].valid || !players[i].snap.hooked) continue;
        r_.line(players[i].snap.pos + Vec3(0, 1.2f, 0), players[i].snap.hookPoint, 0.04f, {0.1f, 0.1f, 0.1f, 1});
    }

    // other players' shots: laser beams while firing, a tracer when a sniper fires
    for (int i = 0; i < kMaxPlayers; i++) {
        bool firing = players[i].valid && (players[i].snap.flags & PS_FIRING) && (players[i].snap.flags & PS_ALIVE) && i != me;
        if (firing) {
            const PlayerSnap& p = players[i].snap;
            int w = p.weapon;
            if (w == WPN_LASER || (w == WPN_SNIPER && !remoteFiring_[i])) {
                Vec3 eyeP = p.pos + Vec3(0, (p.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight, 0);
                Vec3 d = angleForward(p.yaw, p.pitch);
                float range = weaponDef(w).range;
                float t = map.raycast(eyeP, d, range);
                Vec3 end = eyeP + d * (t < 0 ? range : t);
                Vec3 start = eyeP + d * 0.6f;
                if (w == WPN_LASER) r_.line(start, end, 0.06f, rgba(teamColor(i), 0.85f));
                else tracers_.push_back({start, end, time_ + 0.3});
            }
        }
        remoteFiring_[i] = firing;
    }
    // own laser beam follows the crosshair while held
    if (alive && time_ < laserUntil_ && net_.selfWeapon() == WPN_LASER) {
        Vec3 d = angleForward(yaw_, pitch_);
        float range = weaponDef(WPN_LASER).range;
        float t = map.raycast(eye, d, range);
        Vec3 right = normalize(cross(d, {0, 1, 0}));
        r_.line(eye + d * 0.5f + right * 0.15f - Vec3(0, 0.12f, 0), eye + d * (t < 0 ? range : t), 0.05f, rgba(cfg_.colorFriendly, 0.9f));
    }

    // projectiles
    for (auto& pr : lastProjectiles_) {
        Vec3 col = teamColor(pr.owner);
        float sp = length(pr.vel);
        Vec3 dir = sp > 0.01f ? pr.vel / sp : Vec3(0, 0, -1);
        float yaw = std::atan2(-dir.x, -dir.z);
        if (pr.type == WPN_STICKY) {
            bool resting = sp < 0.01f; // stuck: pulse so they're easy to spot
            float k = resting ? 0.75f + 0.25f * std::sin((float)time_ * 10) : 1.0f;
            r_.box(pr.pos, {0.14f, 0.14f, 0.14f}, (float)time_ * (resting ? 0 : 6), col * k, 1.0f);
        } else if (pr.type == WPN_CROSSBOW) {
            r_.boxPitched(pr.pos, {0, 0, 0}, {0.03f, 0.03f, 0.35f}, yaw, std::asin(clampf(dir.y, -1, 1)), {0.1f, 0.1f, 0.12f}, 1.0f);
            r_.line(pr.pos, pr.pos - dir * 1.5f, 0.04f, rgba(col, 0.5f));
        } else {
            r_.box(pr.pos, {0.12f, 0.12f, 0.12f}, 0, col * 1.3f, 1.0f);
            r_.line(pr.pos, pr.pos - dir * 1.2f, 0.12f, rgba(col, 0.5f));
        }
    }
    // explosions
    for (size_t i = 0; i < explosions_.size();) {
        float t = (float)(time_ - explosions_[i].start) / 0.35f;
        if (t >= 1) { explosions_.erase(explosions_.begin() + i); continue; }
        float r = 0.5f + t * 3.0f;
        Vec3 c = explosions_[i].at;
        r_.translucentBox({c - Vec3(r, r, r), c + Vec3(r, r, r)}, {1.0f, 0.85f, 0.5f, 0.55f * (1 - t)});
        i++;
    }
    for (size_t i = 0; i < tracers_.size();) {
        if (time_ > tracers_[i].until) { tracers_.erase(tracers_.begin() + i); continue; }
        r_.line(tracers_[i].a, tracers_[i].b, 0.03f, {1.0f, 0.95f, 0.7f, 0.8f});
        i++;
    }

    // objectives
    Vec3 friendly = cfg_.colorFriendly, enemy = cfg_.colorEnemy;
    const PlayerSnap* mep = net_.findPlayer(me);
    int myTeam = mep ? (mep->team() == TEAM_SPECTATOR ? TEAM_A : mep->team()) : TEAM_A;
    auto teamCol = [&](int t) { return t == myTeam ? friendly : enemy; };
    auto labelAt = [&](const Vec3& p, const std::string& s, const Color& c) {
        float sx, sy;
        if (r_.project(p, sx, sy)) {
            float ts = ui_.textScale();
            r_.text(sx - r_.textWidth(s, ts) * 0.5f, sy, ts, s, c);
        }
    };
    if (ms.mode == MODE_BOMB) {
        const char* names[] = {"A", "B", "C", "D"};
        for (size_t i = 0; i < map.bombSites.size(); i++) {
            r_.translucentBox(map.bombSites[i], rgba(teamCol(ms.attackers), 0.07f));
            Vec3 c = map.bombSites[i].center();
            labelAt({c.x, map.bombSites[i].mx.y + 0.5f, c.z}, std::string("SITE ") + names[i % 4], kWhite);
        }
        if (ms.bombState == BOMB_DROPPED || ms.bombState == BOMB_PLANTED) {
            bool blink = ms.bombState == BOMB_PLANTED && std::fmod(time_, std::max(0.15, ms.bombTimer / 20.0)) < 0.08;
            Vec3 c = blink ? Vec3(1, 0.2f, 0.2f) : Vec3(0.15f, 0.15f, 0.15f);
            r_.box(ms.bombPos + Vec3(0, 0.15f, 0), {0.25f, 0.15f, 0.18f}, 0, c, 1.0f);
            labelAt(ms.bombPos + Vec3(0, 0.8f, 0), ms.bombState == BOMB_PLANTED ? "BOMB" : "BOMB (DROPPED)", rgba(teamCol(ms.attackers)));
        }
    } else if (ms.mode == MODE_KOTH && map.hasHill) {
        Vec3 c = ms.hillOwner == 255 ? Vec3(1, 1, 1) : teamCol(ms.hillOwner);
        r_.translucentBox(map.hill, rgba(c, 0.12f + 0.05f * std::sin((float)time_ * 4)));
        labelAt({map.hill.center().x, map.hill.mx.y + 0.5f, map.hill.center().z}, (ms.flags & MS_CONTESTED) ? "CONTESTED" : "HILL", rgba(c));
    } else if (isPayloadMode(ms.mode) && map.payloadPath.size() >= 2) {
        bool neutral = ms.mode == MODE_NPAYLOAD;
        int cartTeam = neutral ? (ms.hillOwner == 255 ? -1 : ms.hillOwner) : ms.attackers;
        Vec3 cartCol = cartTeam < 0 ? Vec3(0.75f, 0.75f, 0.78f) : teamCol(cartTeam);
        for (size_t i = 1; i < map.payloadPath.size(); i++)
            r_.line(map.payloadPath[i - 1] + Vec3(0, 0.05f, 0), map.payloadPath[i] + Vec3(0, 0.05f, 0), 0.25f, rgba(neutral ? Vec3(1, 1, 1) : teamCol(ms.attackers), 0.35f));
        Vec3 cart = map.payloadPoint(ms.payloadDist);
        Vec3 ahead = map.payloadPoint(ms.payloadDist + 0.5f);
        Vec3 d = ahead - cart;
        float yaw = (length(d) > 1e-3f) ? std::atan2(-d.x, -d.z) : 0;
        float sv = sunVisibility(map, cart + Vec3(0, 1, 0));
        r_.box(cart + Vec3(0, 0.6f, 0), {0.9f, 0.6f, 1.2f}, yaw, cartCol * 0.8f, sv);
        r_.box(cart + Vec3(0, 1.35f, 0), {0.5f, 0.15f, 0.7f}, yaw, {0.2f, 0.2f, 0.22f}, sv);
        labelAt(cart + Vec3(0, 2.2f, 0), (ms.flags & MS_CONTESTED) ? "PAYLOAD - CONTESTED" : "PAYLOAD", rgba(cartCol));
    } else if (ms.mode == MODE_RACE) {
        int next = mep ? mep->checkpoint : 0;
        for (size_t i = 0; i < map.checkpoints.size(); i++) {
            bool isNext = (int)i == next;
            bool finish = i + 1 == map.checkpoints.size();
            Color c = isNext ? rgba(friendly, 0.2f) : rgba({1, 1, 1}, (int)i < next ? 0.03f : 0.07f);
            r_.translucentBox(map.checkpoints[i], c);
            if (isNext) {
                Vec3 cc = map.checkpoints[i].center();
                labelAt({cc.x, map.checkpoints[i].mx.y + 0.3f, cc.z}, finish ? "FINISH" : "CHECKPOINT " + std::to_string(i + 1), rgba(friendly));
            }
        }
    }

    // viewmodel
    if (alive && !(scoped_ && scopeOverlay_)) {
        int w = net_.selfWeapon();
        recoil_ = std::max(0.0, recoil_ - 0.016 * 8);
        float kick = (float)recoil_ * 0.06f;
        Vec3 gun = {0.025f, 0.03f, 0.16f};
        if (w == WPN_ROCKET) gun = {0.045f, 0.045f, 0.2f};
        if (w == WPN_KNIFE) gun = {0.01f, 0.02f, 0.09f};
        if (w == WPN_SNIPER) gun = {0.02f, 0.026f, 0.3f};
        if (w == WPN_LASER) gun = {0.035f, 0.04f, 0.17f};
        if (w == WPN_SHOTGUN) gun = {0.04f, 0.035f, 0.22f};
        if (w == WPN_STICKY) gun = {0.05f, 0.05f, 0.18f};
        if (w == WPN_CROSSBOW) gun = {0.02f, 0.025f, 0.22f};
        Vec3 at = {0.2f, -0.17f, -0.55f + kick};
        r_.viewBox(at, gun, {0.14f, 0.14f, 0.16f});
        r_.viewBox(at + Vec3(0, gun.y + 0.006f, 0.05f), {0.01f, 0.006f, 0.05f}, cfg_.colorFriendly);
        if (w == WPN_SNIPER) r_.viewBox(at + Vec3(0, gun.y + 0.03f, 0.02f), {0.018f, 0.018f, 0.09f}, {0.1f, 0.1f, 0.12f}); // scope
        if (w == WPN_LASER) r_.viewBox(at + Vec3(0, 0, -gun.z - 0.01f), {0.02f, 0.02f, 0.012f}, cfg_.colorFriendly * 1.4f);
        if (w == WPN_CROSSBOW) r_.viewBox(at + Vec3(0, 0.01f, -gun.z + 0.03f), {0.13f, 0.008f, 0.012f}, {0.12f, 0.12f, 0.14f}); // limbs
    }
    r_.drawScene(cfg_.shadows);
}

// ---------------------------------------------------------------------------
// HUD

void Game::drawHud() {
    const float W = (float)r_.width(), H = (float)r_.height();
    float s = H / 720.0f;
    ui_.begin(&r_, uiIn_, s);
    float ts = ui_.textScale();
    const ModeState& ms = net_.mode();
    int me = net_.clientId();
    const PlayerSnap* mep = net_.findPlayer(me);
    int myTeam = mep ? (mep->team() == TEAM_SPECTATOR ? TEAM_A : mep->team()) : TEAM_A;
    Color fc = rgba(cfg_.colorFriendly), ec = rgba(cfg_.colorEnemy);
    bool teamMode = modeIsTeamBased(ms.mode);

    float cx = W * 0.5f, cy = H * 0.5f;
    if (scoped_ && scopeOverlay_) {
        // circular scope mask built from horizontal strips (one batch, no texture)
        float R = H * 0.46f;
        const Color k = {0, 0, 0, 1};
        int strips = 96;
        for (int i = 0; i < strips; i++) {
            float y0 = cy - R + 2 * R * i / strips, y1 = cy - R + 2 * R * (i + 1) / strips;
            float yc = (y0 + y1) * 0.5f - cy;
            float half = std::sqrt(std::max(0.0f, R * R - yc * yc));
            r_.rect(0, y0, cx - half, y1 - y0 + 0.5f, k);
            r_.rect(cx + half, y0, W - cx - half, y1 - y0 + 0.5f, k);
        }
        r_.rect(0, 0, W, cy - R, k);
        r_.rect(0, cy + R, W, H - cy - R, k);
        r_.rect(cx - R, cy - 0.5f, 2 * R, 1, {0, 0, 0, 0.9f});
        r_.rect(cx - 0.5f, cy - R, 1, 2 * R, {0, 0, 0, 0.9f});
    }
    // crosshair (scoped: the scope's reticle replaces it)
    if (!(scoped_ && scopeOverlay_)) drawCrosshair(cx, cy, s, time_ < hitUntil_);
    {   // floating damage numbers above whoever you hit
        auto ip = net_.interpolatedPlayers();
        for (size_t i = 0; i < dmgNums_.size();) {
            DmgNum& d = dmgNums_[i];
            float t = (float)(time_ - d.start);
            if (t > 0.9f) { dmgNums_.erase(dmgNums_.begin() + i); continue; }
            if (d.victim >= 0 && d.victim < kMaxPlayers && ip[d.victim].valid) d.pos = ip[d.victim].snap.pos;
            float sx, sy;
            if (r_.project(d.pos + Vec3(0, 2.0f + t * 0.8f, 0), sx, sy)) {
                std::string n = std::to_string((int)std::lround(d.amount));
                float k = std::max(1.0f, std::round(ts * (d.amount >= 40 ? 1.5f : 1.0f)));
                Color c = d.amount >= 40 ? Color{1, 0.85f, 0.2f, 1 - t / 0.9f} : Color{1, 1, 1, 1 - t / 0.9f};
                r_.text(sx - r_.textWidth(n, k) * 0.5f, sy, k, n, c);
            }
            i++;
        }
    }
    if (time_ < damageUntil_) {
        float a = (float)(damageUntil_ - time_) / 0.25f * 0.35f;
        float b = 30 * s;
        r_.rect(0, 0, W, b, {ec.r, ec.g, ec.b, a}); r_.rect(0, H - b, W, b, {ec.r, ec.g, ec.b, a});
        r_.rect(0, 0, b, H, {ec.r, ec.g, ec.b, a}); r_.rect(W - b, 0, b, H, {ec.r, ec.g, ec.b, a});
    }

    // top: mode, timer, scores
    std::string title = modeTitle(ms.mode);
    std::string phase;
    switch (ms.phase) {
        case PHASE_WARMUP:
            phase = ms.timeLeft > 0 ? "MATCH STARTS IN " + std::to_string((int)std::ceil(ms.timeLeft))
                                    : "WARMUP - " + inputName(cfg_.binds.code[A_VOTESTART][0]) + " TO VOTE START";
            break;
        case PHASE_FREEZE: phase = "ROUND " + std::to_string(ms.round) + " - " + std::to_string((int)std::ceil(ms.timeLeft)); break;
        case PHASE_LIVE:
            if (ms.mode == MODE_VS || ms.mode == MODE_DUEL) phase = (ms.flags & MS_DRAIN) ? "STALEMATE - HEALTH DRAINING" : "DRAIN IN " + fmtTime(ms.timeLeft);
            else if (ms.mode == MODE_KOTH) phase = "FIRST TO " + std::to_string(ms.target);
            else phase = fmtTime(ms.timeLeft) + ((ms.flags & MS_OVERTIME) ? " OVERTIME" : "");
            break;
        case PHASE_ROUND_END: phase = "ROUND OVER"; break;
        case PHASE_MATCH_END: phase = "MATCH OVER"; break;
    }
    ui_.panel(cx - 220 * s, 8 * s, 440 * s, 64 * s, 0.55f);
    ui_.labelCentered(cx, 16 * s, title, kWhite);
    ui_.labelCentered(cx, 16 * s + 22 * s, phase, kGrey);
    if (teamMode) {
        int mine = myTeam == TEAM_A ? ms.scoreA : ms.scoreB;
        int theirs = myTeam == TEAM_A ? ms.scoreB : ms.scoreA;
        std::string a = std::to_string(mine), b = std::to_string(theirs);
        r_.text(cx - 200 * s, 22 * s, ts * 1.5f, a, fc);
        r_.text(cx + 200 * s - r_.textWidth(b, ts * 1.5f), 22 * s, ts * 1.5f, b, ec);
    } else if (mep) {
        std::string a = std::to_string(mep->score);
        int best = -9999;
        for (auto& p : net_.latest().players) if (p.id != me && !(p.flags & PS_SPECTATOR)) best = std::max<int>(best, p.score);
        std::string b = best == -9999 ? "-" : std::to_string(best);
        r_.text(cx - 200 * s, 22 * s, ts * 1.5f, a, fc);
        r_.text(cx + 200 * s - r_.textWidth(b, ts * 1.5f), 22 * s, ts * 1.5f, b, ec);
    }

    // objective line
    std::string obj;
    float progress = -1;
    if (ms.mode == MODE_BOMB && ms.phase == PHASE_LIVE) {
        bool att = myTeam == ms.attackers;
        if (ms.bombState == BOMB_PLANTED) obj = att ? "BOMB PLANTED - DEFEND IT" : "BOMB PLANTED - DEFUSE IT (HOLD E)";
        else if (ms.bombCarrier == me) obj = "YOU HAVE THE BOMB - PLANT AT A SITE (HOLD E)";
        else if (ms.bombState == BOMB_DROPPED) obj = att ? "BOMB DROPPED - PICK IT UP" : "BOMB DROPPED";
        else obj = att ? "ATTACK - PLANT THE BOMB" : "DEFEND THE SITES";
        if (ms.actionPlayer != 255) progress = ms.actionProgress;
    } else if (ms.mode == MODE_KOTH) {
        obj = ms.flags & MS_CONTESTED ? "HILL CONTESTED" : ms.hillOwner == 255 ? "CAPTURE THE HILL" : ms.hillOwner == myTeam ? "HOLDING THE HILL" : "ENEMY HOLDS THE HILL";
    } else if (ms.mode == MODE_PAYLOAD) {
        obj = myTeam == ms.attackers ? "PUSH THE PAYLOAD" : "STOP THE PAYLOAD";
        if (ms.payloadLen > 0) progress = ms.payloadDist / ms.payloadLen;
    } else if (ms.mode == MODE_NPAYLOAD) {
        obj = (ms.flags & MS_CONTESTED) ? "PAYLOAD CONTESTED" : ms.hillOwner == 255 ? "PUSH THE PAYLOAD TO THE ENEMY END"
              : ms.hillOwner == myTeam ? "YOUR TEAM IS PUSHING" : "ENEMY IS PUSHING";
    } else if (ms.mode == MODE_RACE && mep) {
        obj = (mep->flags & PS_FINISHED) ? "FINISHED" : "CHECKPOINT " + std::to_string(mep->checkpoint) + " / " + std::to_string(net_.map().checkpoints.size());
    } else if (ms.mode == MODE_VS && ms.phase == PHASE_LIVE) {
        obj = "LAST TEAM STANDING WINS";
    } else if (ms.mode == MODE_DUEL && ms.phase != PHASE_WARMUP && ms.duelMod < DUEL_MOD_COUNT) {
        obj = std::string("THIS LIFE: ") + duelModTitle(ms.duelMod);
    }
    if (!obj.empty()) ui_.labelCentered(cx, 80 * s, obj, kWhite);
    if (progress >= 0) {
        float bw = 300 * s;
        r_.rect(cx - bw * 0.5f, 104 * s, bw, 8 * s, {0, 0, 0, 0.5f});
        r_.rect(cx - bw * 0.5f, 104 * s, bw * clampf(progress, 0, 1), 8 * s, ms.mode == MODE_PAYLOAD ? rgba(myTeam == ms.attackers ? cfg_.colorFriendly : cfg_.colorEnemy) : kWhite);
    }
    if (ms.mode == MODE_NPAYLOAD && ms.payloadLen > 0) {
        // tug-of-war bar: your side's end on the left
        float bw = 300 * s, f = ms.payloadDist / ms.payloadLen;
        if (myTeam == TEAM_A) f = 1 - f; // team A pushes toward the end of the track
        r_.rect(cx - bw * 0.5f, 104 * s, bw, 8 * s, rgba(cfg_.colorEnemy, 0.8f));
        r_.rect(cx - bw * 0.5f, 104 * s, bw * clampf(1 - f, 0, 1), 8 * s, rgba(cfg_.colorFriendly, 0.9f));
        r_.rect(cx - 1, 100 * s, 2, 16 * s, kWhite);
    }
    if (ms.mode == MODE_BOMB && ms.bombState == BOMB_PLANTED && ms.phase == PHASE_LIVE)
        ui_.labelCentered(cx, 118 * s, fmtTime(ms.bombTimer), kRed, 1.5f);

    // health / weapon
    if (net_.selfAlive()) {
        int hp = net_.selfHealth();
        std::string h = std::to_string(hp);
        r_.text(24 * s, H - 70 * s, ts * 2, h, hp > 30 ? kWhite : kRed);
        r_.rect(24 * s, H - 28 * s, 200 * s, 8 * s, {0, 0, 0, 0.5f});
        r_.rect(24 * s, H - 28 * s, 200 * s * clampf(hp / 100.0f, 0, 1), 8 * s, hp > 30 ? fc : kRed);
        std::string wn = weaponDef(net_.selfWeapon()).name;
        r_.text(W - 24 * s - r_.textWidth(wn, ts * 1.5f), H - 50 * s, ts * 1.5f, wn, kWhite);
        // dash + grapple cooldown bars under the crosshair (full = ready)
        const MoveState& mv = net_.predicted();
        auto bar = [&](float y, const char* label, float remaining, float total) {
            float bw = 90 * s, bh = 5 * s, bx = cx - bw * 0.5f;
            float f = clampf(1.0f - remaining / total, 0, 1);
            r_.rect(bx - 1, y - 1, bw + 2, bh + 2, {0, 0, 0, 0.8f});
            r_.rect(bx, y, bw * f, bh, f >= 1 ? fc : Color{0.6f, 0.62f, 0.66f, 0.9f});
            float lts = std::max(1.0f, std::round(ts * 0.5f));
            r_.text(bx - r_.textWidth(label, lts) - 6 * s, y - 2 * s, lts, label, f >= 1 ? kWhite : kGrey);
        };
        int cw = net_.selfWeapon();
        bar(cy + 40 * s, "WEAPON", (float)std::max(0.0, localNextFire_ - time_), weaponDef(cw).refire);
        bar(cy + 52 * s, "DASH", mv.dashCooldown, kDashCooldown);
        bar(cy + 64 * s, "HOOK", mv.hookCooldown, kHookCooldown);
        // weapon slots: 1 rifle, 2 rocket, 3 knife, 4 sniper, 5 laser
        float bx = W - 24 * s;
        for (int wi = WPN_COUNT - 1; wi >= 0; wi--) {
            std::string t = std::to_string(wi + 1) + " " + weaponDef(wi).name;
            float sts = std::max(1.0f, std::round(ts * 0.75f));
            float tw = r_.textWidth(t, sts);
            bx -= tw + 14 * s;
            r_.text(bx, H - 74 * s, sts, t, wi == net_.selfWeapon() ? fc : Color{0.85f, 0.87f, 0.9f, 0.9f});
        }
    } else if (ms.phase != PHASE_ROUND_END && ms.phase != PHASE_MATCH_END) {
        std::string t = mep && (mep->flags & PS_SPECTATOR) ? "SPECTATING" : "DEAD";
        if (specTarget_ >= 0) t += " - WATCHING " + net_.playerName(specTarget_) + " (CLICK TO CYCLE)";
        ui_.labelCentered(cx, H - 60 * s, t, kGrey);
    }

    // kill feed (top right)
    float y = 12 * s;
    while (!killFeed_.empty() && killFeed_.front().until < time_) killFeed_.pop_front();
    for (auto& k : killFeed_) {
        float tw = r_.textWidth(k.text, ts);
        r_.rect(W - tw - 20 * s, y - 3 * s, tw + 12 * s, r_.textHeight(ts) + 6 * s, {0, 0, 0, 0.4f});
        r_.text(W - tw - 14 * s, y, ts, k.text, k.color);
        y += r_.textHeight(ts) + 8 * s;
    }
    // chat (bottom left)
    while (!chatFeed_.empty() && chatFeed_.front().until < time_ && !chatting_) chatFeed_.pop_front();
    y = H - 110 * s - chatFeed_.size() * (r_.textHeight(ts) + 4 * s);
    for (auto& c : chatFeed_) { r_.text(24 * s, y, ts, c.text, c.color); y += r_.textHeight(ts) + 4 * s; }
    if (chatting_) {
        ui_.label(24 * s, H - 100 * s, "SAY: " + chatText_ + (std::fmod(time_, 1.0) < 0.5 ? "_" : ""), kWhite);
        for (char c : uiIn_.chars) if (c >= 32 && c < 127 && chatText_.size() < 120) chatText_ += c;
        if (uiIn_.backspace && !chatText_.empty()) chatText_.pop_back();
    }
    // notices
    while (!notices_.empty() && notices_.front().until < time_) notices_.pop_front();
    y = H * 0.3f;
    for (auto& n : notices_) { ui_.labelCentered(cx, y, n.text, n.color); y += r_.textHeight(ts) + 6 * s; }
    if (time_ < centerUntil_) ui_.labelCentered(cx, H * 0.36f, centerText_, kWhite, 1.5f);
    if (ms.voteActive) {
        char vb[200];
        snprintf(vb, sizeof vb, "VOTE: %s   YES %d  NO %d  OF %d   F1 YES / F2 NO   %ds", voteText_.c_str(), ms.voteYes, ms.voteNo,
                 ms.voteVoters, (int)std::ceil(ms.voteTimeLeft));
        float vw = r_.textWidth(vb, ts) + 24 * s;
        r_.rect(cx - vw * 0.5f, 122 * s, vw, r_.textHeight(ts) + 12 * s, {0, 0, 0, 0.6f});
        ui_.labelCentered(cx, 128 * s, vb, kWhite);
    }

    // host info
    if (host_ && paused_) {
        std::string st = "HOSTING ON UDP PORT " + std::to_string(host_->port());
        ui_.labelCentered(cx, H - 120 * s, st, kGrey);
        if (host_->config().upnp && host_->upnpStatus().size()) ui_.labelCentered(cx, H - 100 * s, host_->upnpStatus(), kGrey);
        std::string ls = host_->listStatus();
        for (auto& ch : ls) ch = (char)toupper((unsigned char)ch);
        if (!ls.empty()) ui_.labelCentered(cx, H - 80 * s, ls, kGrey);
    }

    showScores_ = isDown(A_SCORES) || ms.phase == PHASE_MATCH_END;
    if (showScores_ && !paused_) drawScoreboard();
    if (paused_) drawPauseMenu(cx, H * 0.25f);
}

void Game::drawScoreboard() {
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    float ts = ui_.textScale();
    float w = 640 * s, x = (W - w) * 0.5f, y = 140 * s;
    const ModeState& ms = net_.mode();
    std::vector<PlayerSnap> ps = net_.latest().players;
    std::sort(ps.begin(), ps.end(), [](const PlayerSnap& a, const PlayerSnap& b) {
        if (a.team() != b.team()) return a.team() < b.team();
        return a.score > b.score;
    });
    float rowH = r_.textHeight(ts) + 10 * s;
    ui_.panel(x, y, w, (ps.size() + 2) * rowH + 10 * s, 0.8f);
    float cy = y + 10 * s;
    r_.text(x + 12 * s, cy, ts, "PLAYER", kGrey);
    r_.text(x + w * 0.5f, cy, ts, ms.mode == MODE_RACE ? "WINS" : "SCORE", kGrey);
    r_.text(x + w * 0.64f, cy, ts, "K", kGrey);
    r_.text(x + w * 0.74f, cy, ts, "D", kGrey);
    r_.text(x + w * 0.84f, cy, ts, "PING", kGrey);
    cy += rowH;
    for (auto& p : ps) {
        Color c = (p.flags & PS_SPECTATOR) ? kGrey : rgba(teamColor(p.id));
        std::string n = net_.playerName(p.id) + ((p.flags & PS_ALIVE) ? "" : " (dead)");
        if (p.id == net_.clientId()) r_.rect(x + 4 * s, cy - 4 * s, w - 8 * s, rowH - 2 * s, {1, 1, 1, 0.06f});
        r_.text(x + 12 * s, cy, ts, n, c);
        r_.text(x + w * 0.5f, cy, ts, std::to_string(p.score), kWhite);
        r_.text(x + w * 0.64f, cy, ts, std::to_string(p.kills), kWhite);
        r_.text(x + w * 0.74f, cy, ts, std::to_string(p.deaths), kWhite);
        bool bot = net_.playerName(p.id).rfind("BOT ", 0) == 0;
        r_.text(x + w * 0.84f, cy, ts, bot ? "BOT" : std::to_string(p.ping), kWhite);
        cy += rowH;
    }
}

// Everything players can call a vote on (majority of human players decides).
void Game::drawVoteMenu(float cx, float y) {
    float s = ui_.s();
    float w = 300 * s, h = 36 * s, gap = 8 * s, x = cx - w * 0.5f;
    bool warmup = net_.mode().phase == PHASE_WARMUP;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, (warmup ? 9 : 8) * (h + gap) + 40 * s);
    ui_.labelCentered(cx, y, "CALL A VOTE");
    y += 30 * s;
    auto vote = [&](const std::string& label, const std::string& cmd) {
        if (ui_.button(x, y, w, h, label)) { net_.sendCommand("callvote " + cmd); voteMenu_ = false; paused_ = false; }
        y += h + gap;
    };
    if (warmup) vote("START THE MATCH", "start");
    vote("BALANCE BOTS ON", "bots on");
    vote("BALANCE BOTS OFF", "bots off");
    vote("BOTS: BEGINNER", "botskill BEGINNER");
    vote("BOTS: INTERMEDIATE", "botskill INTERMEDIATE");
    vote("BOTS: ADVANCED", "botskill ADVANCED");
    vote("BOTS: EXPERT", "botskill EXPERT");
    if (ui_.button(x, y, w, h, "BACK")) voteMenu_ = false;
}

void Game::drawPauseMenu(float cx, float y) {
    float s = ui_.s();
    float w = 300 * s, h = 36 * s, gap = 8 * s, x = cx - w * 0.5f;
    if (settingsInGame_) {
        drawSettings(x - 60 * s, y, w + 120 * s);
        return;
    }
    if (picker_ == PICK_LIVE_MAP || picker_ == PICK_LIVE_MODE) { drawPicker(cx, y); return; }
    if (voteMenu_) { drawVoteMenu(cx, y); return; }
    bool teamMode = modeIsTeamBased(net_.mode().mode);
    int rows = 7 + (host_ ? 2 : 0);
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, rows * (h + gap) + 24 * s);
    if (ui_.button(x, y, w, h, "RESUME")) { paused_ = false; }
    y += h + gap;
    if (teamMode) {
        float hw = (w - gap) * 0.5f;
        if (ui_.button(x, y, hw, h, "JOIN TEAM A")) { net_.sendCommand("team a"); paused_ = false; }
        if (ui_.button(x + hw + gap, y, hw, h, "JOIN TEAM B")) { net_.sendCommand("team b"); paused_ = false; }
        y += h + gap;
    } else {
        if (ui_.button(x, y, w, h, "PLAY")) { net_.sendCommand("team a"); paused_ = false; }
        y += h + gap;
    }
    if (ui_.button(x, y, w, h, "SPECTATE")) { net_.sendCommand("team spec"); paused_ = false; }
    y += h + gap;
    if (ui_.button(x, y, w, h, "VOTE...")) voteMenu_ = true;
    y += h + gap;
    if (host_) {
        // Host controls: change mode/map live.
        const Map& map = net_.map();
        int cur = net_.mode().mode;
        float hw = (w - gap) * 0.5f;
        auto openLive = [&](int kind) {
            liveMode_ = cur;
            liveMap_ = 0;
            for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == map.name) liveMap_ = (int)i;
            picker_ = kind;
        };
        if (ui_.button(x, y, hw, h, "CHANGE MODE")) openLive(PICK_LIVE_MODE);
        if (ui_.button(x + hw + gap, y, hw, h, "CHANGE MAP") && !maps_.empty()) openLive(PICK_LIVE_MAP);
        y += h + gap;
        if (ui_.button(x, y, w, h, "FORCE START")) { net_.sendCommand("forcestart"); paused_ = false; }
        y += h + gap;
    }
    if (ui_.button(x, y, w, h, "SETTINGS")) settingsInGame_ = true;
    y += h + gap;
    if (ui_.button(x, y, w, h, host_ ? "STOP HOSTING" : "DISCONNECT")) { leaveGame(); return; }
    y += h + gap;
    if (ui_.button(x, y, w, h, "QUIT")) quit_ = true;
}

// ---------------------------------------------------------------------------
// menus

void Game::drawSettings(float x0, float y0, float wTotal) {
    if (controlsOpen_) { drawControls(x0, y0, wTotal); return; }
    if (crosshairOpen_) { drawCrosshairSettings(x0, y0, wTotal); return; }
    float s = ui_.s();
    float h = 32 * s, gap = 7 * s, colGap = 24 * s;
    float w = std::max(320 * s, wTotal * 0.5f);
    float x = x0 + wTotal * 0.5f - w - colGap * 0.5f; // two columns centred on the old area
    float xr = x + w + colGap;
    ui_.panel(x - 16 * s, y0 - 16 * s, w * 2 + colGap + 32 * s, 10 * (h + gap) + 150 * s);

    // --- left column: player, mouse, colors
    float y = y0;
    ui_.label(x, y, "NAME");
    y += 22 * s;
    ui_.textField(1, x, y, w, h, cfg_.name, 20);
    y += h + gap;
    {   // sensitivity, or the same thing as cm per 360 at your mouse DPI (drag either)
        ui_.slider(x, y, w, h, "SENSITIVITY", cfg_.sensitivity, 0.1f, 10.0f, "%.2f");
        y += h + gap;
        float cw = w * 0.6f, dw = w - cw - gap;
        // counts per 360 = 360 / (sens * 0.022); cm = counts / dpi * 2.54
        float cm = 360.0f / (cfg_.sensitivity * 0.022f) / cfg_.dpi * 2.54f;
        if (ui_.slider(x, y, cw, h, "CM/360", cm, 5, 120, "%.1f"))
            cfg_.sensitivity = clampf(360.0f / (cm / 2.54f * cfg_.dpi) / 0.022f, 0.01f, 50.0f);
        static const int dpis[] = {400, 800, 1000, 1200, 1600, 2400, 3200, 6400};
        static const std::vector<std::string> dpiNames = {"400 DPI", "800 DPI", "1000 DPI", "1200 DPI", "1600 DPI", "2400 DPI", "3200 DPI", "6400 DPI"};
        int di = 1;
        for (int i = 0; i < 8; i++) if (dpis[i] == cfg_.dpi) di = i;
        if (ui_.button(x + cw + gap, y, dw, h, dpiNames[di])) cfg_.dpi = dpis[(di + 1) % 8]; // click: next DPI
    }
    y += h + gap;
    ui_.slider(x, y, w, h, "FOV", cfg_.fov, 70, 130, "%.0f");
    y += h + gap;
    auto colorRow = [&](const char* label, Vec3& c) {
        ui_.label(x, y + 4 * s, label);
        r_.rect(x + w - 60 * s, y, 60 * s, 22 * s, rgba(c));
        y += 26 * s;
        float sw = (w - 2 * gap) / 3;
        const char* ch[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            float v = c[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f")) c[i] = std::round(v) / 255.0f;
        }
        y += h + gap;
    };
    colorRow("TEAM COLOR", cfg_.colorFriendly);
    colorRow("ENEMY COLOR", cfg_.colorEnemy);
    std::vector<std::string> pn;
    int pi = -1;
    for (int i = 0; i < kPaletteCount; i++) {
        pn.push_back(kPalettes[i].name);
        if (length(kPalettes[i].friendly - cfg_.colorFriendly) < 0.01f && length(kPalettes[i].enemy - cfg_.colorEnemy) < 0.01f) pi = i;
    }
    if (pi < 0) { pn.push_back("CUSTOM"); pi = kPaletteCount; }
    if (ui_.cycle(x, y, w, h, "PRESET", pi, kPaletteCount, pn)) {
        cfg_.colorFriendly = kPalettes[pi].friendly;
        cfg_.colorEnemy = kPalettes[pi].enemy;
    }
    ui_.accent = rgba(cfg_.colorFriendly);
    y += h + gap;
    if (ui_.slider(x, y, w * 0.6f, h, "VOLUME", cfg_.volume, 0, 1, "%.2f")) audio_.setMasterVolume(cfg_.volume);
    ui_.toggle(x + w * 0.6f + gap, y, w * 0.4f - gap, h, "HITSOUND", cfg_.hitsounds);

    // --- right column: display + latency
    y = y0;
    static const std::vector<std::string> dispNames = {"WINDOWED", "BORDERLESS FULLSCREEN", "EXCLUSIVE FULLSCREEN"};
    if (ui_.cycle(xr, y, w, h, "DISPLAY", cfg_.displayMode, 3, dispNames)) applyDisplayMode();
    y += h + gap;
    static const std::vector<std::string> presNames = {"NO VSYNC", "VSYNC", "ADAPTIVE VSYNC"};
    if (ui_.cycle(xr, y, w, h, "PRESENT", cfg_.presentMode, 3, presNames)) applyPresentMode();
    y += h + gap;
    static const std::vector<std::string> llNames = {"LOW LATENCY OFF", "LOW LATENCY ON", "LOW LATENCY ULTRA"};
    ui_.cycle(xr, y, w, h, "LOW LATENCY", cfg_.lowLatency, 3, llNames);
    y += h + gap;
    static const int caps[] = {0, 60, 120, 144, 165, 240, 360, 500, 1000};
    static const std::vector<std::string> capNames = {"UNLIMITED FPS", "60 FPS MAX", "120 FPS MAX", "144 FPS MAX", "165 FPS MAX",
                                                      "240 FPS MAX", "360 FPS MAX", "500 FPS MAX", "1000 FPS MAX"};
    int ci = 0;
    for (int i = 0; i < 9; i++) if (caps[i] == cfg_.maxFps) ci = i;
    if (ui_.cycle(xr, y, w, h, "MAX FPS", ci, 9, capNames)) cfg_.maxFps = caps[ci];
    y += h + gap;
    ui_.toggle(xr, y, w, h, "PLAYER SHADOWS", cfg_.shadows);
    y += h + gap;
    const char* help[] = {
        "NO VSYNC = LOWEST LATENCY, MAY TEAR",
        "EXCLUSIVE / BORDERLESS SKIP THE DESKTOP",
        "COMPOSITOR ON MOST DRIVERS",
        "LOW LATENCY ON: <= 1 FRAME QUEUED",
        "ULTRA: 0 QUEUED + LATE INPUT SAMPLING",
        "F3 SHOWS INPUT->FRAME LATENCY",
    };
    for (const char* t : help) { ui_.label(xr, y, t, {0.7f, 0.72f, 0.75f, 1}, 0.5f); y += 14 * s; }
    y += gap;
    if (ui_.button(xr, y, w * 0.5f - gap * 0.5f, h, "CONTROLS")) controlsOpen_ = true;
    if (ui_.button(xr + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "CROSSHAIR")) crosshairOpen_ = true;
    y += h + gap;
    if (ui_.button(xr, y, w, h, "BACK")) {
        cfg_.save();
        if (net_.state() == ConnState::Connected) net_.sendCommand("name " + cfg_.name);
        if (settingsInGame_) settingsInGame_ = false;
        else setScreen(Screen::Main);
    }
}

void Game::drawMenu() {
    if (screen_ == Screen::InGame) return;
    const float W = (float)r_.width(), H = (float)r_.height();
    float s = H / 720.0f;
    ui_.begin(&r_, uiIn_, s);
    r_.rect(0, 0, W, H, {0.03f, 0.035f, 0.05f, r_.worldLoaded() ? 0.6f : 1.0f});
    float cx = W * 0.5f;
    float bw = 340 * s, bh = 40 * s, gap = 10 * s;
    float x = cx - bw * 0.5f;
    float y = H * 0.12f;
    ui_.labelCentered(cx, y, "QUARK", rgba(cfg_.colorFriendly), 4.0f);
    r_.text(8 * s, H - 20 * s, std::max(1.0f, std::round(s)), "V" QUARK_VERSION, kGrey);
    y += 80 * s;

    if (picker_ != PICK_NONE && (screen_ == Screen::Host || screen_ == Screen::VsBots)) { drawPicker(cx, y); return; }
    switch (screen_) {
        case Screen::Main: {
            ui_.label(x, y, "YOUR NAME (SHOWN IN CHAT AND SCOREBOARD)", kGrey, 0.5f);
            y += 16 * s;
            if (ui_.textField(9, x, y, bw, bh, cfg_.name, 20)) cfg_.save();
            y += bh + gap * 2;
            if (ui_.button(x, y, bw, bh, "PLAY VS BOTS")) setScreen(Screen::VsBots);
            y += bh + gap;
            if (ui_.button(x, y, bw, bh, "JOIN GAME")) setScreen(Screen::Join);
            y += bh + gap;
            if (ui_.button(x, y, bw, bh, "HOST GAME")) setScreen(Screen::Host);
            y += bh + gap;
            if (ui_.button(x, y, bw, bh, "SETTINGS")) setScreen(Screen::Settings);
            y += bh + gap;
            if (ui_.button(x, y, bw, bh, "QUIT")) quit_ = true;
            y += bh + gap * 2;
            if (!error_.empty()) ui_.labelCentered(cx, y, error_, kRed);
            break;
        }
        case Screen::Join: {
            float lw = 560 * s, lx = cx - lw * 0.5f;
            ui_.label(lx, y, "ADDRESS (IP:PORT)");
            y += 22 * s;
            ui_.textField(2, lx, y, lw - 140 * s, bh, joinAddr_, 64);
            if (ui_.button(lx + lw - 130 * s, y, 130 * s, bh, "CONNECT") || (uiIn_.enter && !joinAddr_.empty())) connectTo(joinAddr_);
            y += bh + gap * 2;
            ui_.label(lx, y, "SERVERS (LAN + INTERNET)");
            ui_.label(lx, y + 22 * s, browser_.internetStatus(), kGrey, 0.5f);
            y += 12 * s;
            if (ui_.button(lx + lw - 130 * s, y - 6 * s, 130 * s, 30 * s, "REFRESH")) browser_.refresh();
            y += 30 * s;
            ui_.panel(lx, y, lw, 220 * s, 0.6f);
            float ry = y + 8 * s;
            for (auto& sv : browser_.servers()) {
                char row[160];
                if (sv.noReply) snprintf(row, sizeof row, "%s  - NO RESPONSE (PORT NOT OPEN?) - CLICK TO TRY", sv.name.c_str());
                else snprintf(row, sizeof row, "%s  %s/%s  %d/%d  %dms", sv.name.c_str(), sv.map.c_str(), modeName(sv.mode), sv.players,
                              sv.maxPlayers, (int)sv.ping);
                if (ui_.button(lx + 6 * s, ry, lw - 12 * s, 30 * s, row)) {
                    joinAddr_ = sv.addr.toString();
                    connectTo(joinAddr_);
                }
                ry += 34 * s;
                if (ry > y + 200 * s) break;
            }
            if (browser_.servers().empty()) ui_.label(lx + 10 * s, ry, "NO GAMES FOUND", kGrey);
            y += 230 * s;
            if (ui_.button(x, y, bw, bh, "BACK")) setScreen(Screen::Main);
            if (!error_.empty()) ui_.labelCentered(cx, y + bh + gap, error_, kRed);
            break;
        }
        case Screen::Host: {
            float lw = 460 * s, lx = cx - lw * 0.5f;
            ui_.panel(lx - 16 * s, y - 16 * s, lw + 32 * s, 528 * s);
            ui_.label(lx, y, "SERVER NAME");
            y += 22 * s;
            ui_.textField(3, lx, y, lw, bh, cfg_.hostName, 32);
            y += bh + gap;
            if (!maps_.empty()) {
                if (ui_.button(lx, y, lw, bh, "MAP: " + upper(maps_[hostMapIdx_ % maps_.size()]) + "  >")) picker_ = PICK_HOST_MAP;
                y += bh + gap;
                Map m;
                m.load(maps_[hostMapIdx_]);
                if (ui_.button(lx, y, lw, bh, std::string("MODE: ") + modeTitle(hostModeIdx_) + "  >")) picker_ = PICK_HOST_MODE;
                y += bh + gap;
                if (!m.supportsMode(hostModeIdx_)) ui_.label(lx, y, "THIS MAP DOES NOT SUPPORT THAT MODE", kRed);
                else if (hostModeIdx_ == MODE_DUEL && ui_.button(lx, y - 2 * s, lw, 24 * s, cfg_.hostDuelSize == 2 ? "TEAMS: 2V2 (CLICK FOR 1V1)" : "TEAMS: 1V1 (CLICK FOR 2V2)"))
                    cfg_.hostDuelSize = cfg_.hostDuelSize == 2 ? 1 : 2;
                y += 24 * s;
            }
            ui_.label(lx, y, "UDP PORT");
            y += 22 * s;
            ui_.textField(4, lx, y, lw * 0.4f, bh, hostPortStr_, 5);
            float mp = (float)cfg_.hostMaxPlayers;
            if (ui_.slider(lx + lw * 0.45f, y, lw * 0.55f, bh, "MAX PLAYERS", mp, 2, (float)kMaxPlayers, "%.0f"))
                cfg_.hostMaxPlayers = (int)std::round(mp);
            y += bh + gap;
            ui_.toggle(lx, y, lw, bh, "AUTO PORT FORWARD (UPNP)", cfg_.hostUpnp);
            y += bh + gap;
            ui_.toggle(lx, y, lw * 0.5f - 4 * s, bh, "BALANCE BOTS", cfg_.hostBalanceBots);
            ui_.toggle(lx + lw * 0.5f + 4 * s, y, lw * 0.5f - 4 * s, bh, "PUBLIC", cfg_.hostPublic);
            y += bh + gap;
            ui_.cycle(lx, y, lw, bh, "SERVER LOG", cfg_.hostLog, 4, {"SERVER LOG OFF", "SIMPLE LOG", "VERBOSE LOG (125 HZ)", "EXTREME LOG (EVERY TICK)"});
            y += bh + gap;
            ui_.label(lx, y, "FRIENDS JOIN WITH YOUR PUBLIC IP:" + hostPortStr_, kGrey, 0.5f);
            y += 14 * s;
            ui_.label(lx, y, "IF UPNP FAILS, FORWARD THAT UDP PORT ON YOUR ROUTER", kGrey, 0.5f);
            y += 26 * s;
            if (ui_.button(lx, y, lw * 0.5f - 5 * s, bh, "START")) startHost();
            if (ui_.button(lx + lw * 0.5f + 5 * s, y, lw * 0.5f - 5 * s, bh, "BACK")) { cfg_.save(); setScreen(Screen::Main); }
            y += bh + gap;
            if (!error_.empty()) ui_.labelCentered(cx, y + 20 * s, error_, kRed);
            break;
        }
        case Screen::Settings:
            drawSettings(cx - 230 * s, y, 460 * s);
            break;
        case Screen::VsBots: {
            float lw = 460 * s, lx = cx - lw * 0.5f;
            ui_.panel(lx - 16 * s, y - 16 * s, lw + 32 * s, 360 * s);
            if (ui_.button(lx, y, lw, bh, std::string("MODE: ") + modeTitle(cfg_.botMode) + "  >")) picker_ = PICK_BOT_MODE;
            y += bh + gap;
            if (!maps_.empty()) {
                // only offer maps that support the chosen mode
                Map m;
                for (size_t k = 0; k < maps_.size(); k++) {
                    if (m.load(maps_[botMapIdx_ % maps_.size()]) && m.supportsMode(cfg_.botMode)) break;
                    botMapIdx_ = (botMapIdx_ + 1) % (int)maps_.size();
                }
                if (ui_.button(lx, y, lw, bh, "MAP: " + upper(maps_[botMapIdx_ % maps_.size()]) + "  >")) picker_ = PICK_BOT_MAP;
                y += bh + gap;
            }
            static const std::vector<std::string> skills = {"BEGINNER BOTS", "INTERMEDIATE BOTS", "ADVANCED BOTS", "EXPERT BOTS"};
            ui_.cycle(lx, y, lw, bh, "DIFFICULTY", cfg_.botSkill, 4, skills);
            y += bh + gap;
            bool teams = modeIsTeamBased(cfg_.botMode);
            float ne = (float)cfg_.botEnemies, na = (float)cfg_.botAllies;
            if (ui_.slider(lx, y, lw, bh, teams ? "ENEMY BOTS" : "BOTS", ne, 1, 7, "%.0f")) cfg_.botEnemies = (int)std::round(ne);
            y += bh + gap;
            if (teams && ui_.slider(lx, y, lw, bh, "TEAMMATE BOTS", na, 0, 3, "%.0f")) cfg_.botAllies = (int)std::round(na);
            y += bh + gap * 2;
            if (ui_.button(lx, y, lw * 0.5f - 5 * s, bh, "START")) startHost(true);
            if (ui_.button(lx + lw * 0.5f + 5 * s, y, lw * 0.5f - 5 * s, bh, "BACK")) { cfg_.save(); setScreen(Screen::Main); }
            if (!error_.empty()) ui_.labelCentered(cx, y + bh + gap + 10 * s, error_, kRed);
            break;
        }
        case Screen::Connecting: {
            ui_.labelCentered(cx, y, "CONNECTING TO " + net_.address() + "...", kWhite);
            y += 60 * s;
            if (ui_.button(x, y, bw, bh, "CANCEL")) leaveGame();
            break;
        }
        default: break;
    }
}

void Game::drawPerf() {
    if (time_ >= latNextUpdate_) {
        latNextUpdate_ = time_ + 1.0;
        if (latSamples_.empty()) latText_ = "INPUT->FRAME: MOVE THE MOUSE";
        else {
            std::sort(latSamples_.begin(), latSamples_.end());
            double sum = 0;
            for (float v : latSamples_) sum += v;
            auto r = [](double ms) { return std::round(ms * 10.0) / 10.0; }; // 100 us steps
            char b[160];
            snprintf(b, sizeof b, "INPUT->FRAME AVG %.1fMS  MIN %.1fMS  99%% %.1fMS", r(sum / latSamples_.size()), r(latSamples_.front()),
                     r(latSamples_[std::min(latSamples_.size() - 1, (size_t)(latSamples_.size() * 0.99))]));
            latText_ = b;
        }
        latSamples_.clear();
    }
    if (time_ >= perfNextUpdate_ && frameCount_ > 10) {
        perfNextUpdate_ = time_ + 0.5;
        std::vector<float> t(frameTimes_.begin(), frameTimes_.begin() + frameCount_);
        std::sort(t.begin(), t.end());
        double sum = 0;
        for (float f : t) sum += f;
        auto pct = [&](double p) { return t[std::min(t.size() - 1, (size_t)(t.size() * p))]; };
        char b[200];
        snprintf(b, sizeof b, "AVG %.0f  1%% LOW %.0f  0.1%% LOW %.0f  WORST %.1fMS  PING %d",
                 t.size() / sum, 1.0 / pct(0.99), 1.0 / pct(0.999), t.back() * 1000.0,
                 net_.findPlayer(net_.clientId()) ? net_.findPlayer(net_.clientId())->ping : 0);
        perfText_ = b;
    }
    float ts = std::max(1.0f, std::round(r_.height() / 720.0f * 2.0f));
    std::string tick = net_.hasSnapshot() ? "SERVER " + std::to_string(net_.tickRate()) + " HZ  LOSS " + std::to_string(net_.snapshotLoss()) + "%" : "";
    float lh = r_.textHeight(ts) + 6;
    float w = std::max(std::max(r_.textWidth(perfText_, ts), r_.textWidth(latText_, ts)), r_.textWidth(tick, ts));
    r_.rect(4, 4, w + 8, lh * 3 + 4, {0, 0, 0, 0.6f});
    r_.text(8, 8, ts, perfText_, {1, 1, 0.4f, 1});
    r_.text(8, 8 + lh, ts, latText_, {1, 1, 0.4f, 1});
    r_.text(8, 8 + lh * 2, ts, tick, {1, 1, 0.4f, 1});
}

float Game::viewFov() const {
    return scoped_ ? weaponDef(net_.selfWeapon()).zoomFov : cfg_.fov;
}

// ---------------------------------------------------------------------------
// display modes, present modes, low latency, latency measurement

void Game::applyDisplayMode() {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    switch (cfg_.displayMode) {
        case 2: // exclusive fullscreen: owns the display, bypasses the desktop compositor
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(win_, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
            break;
        case 1: { // borderless fullscreen window covering the monitor
            int mx = 0, my = 0;
            glfwGetMonitorPos(mon, &mx, &my);
            glfwSetWindowMonitor(win_, nullptr, mx, my, vm->width, vm->height, 0);
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowPos(win_, mx, my);
            glfwSetWindowSize(win_, vm->width, vm->height);
            break;
        }
        default:
            glfwSetWindowMonitor(win_, nullptr, 100, 100, cfg_.width, cfg_.height, 0);
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_TRUE);
            break;
    }
    applyPresentMode(); // some drivers reset the swap interval on mode changes
    // Render at the new size right away (don't wait for a resize event that may arrive late).
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(win_, &fw, &fh);
    if (cfg_.displayMode != 0) { fw = std::max(fw, vm->width); fh = std::max(fh, vm->height); }
    if (fw > 0 && fh > 0) r_.resize(fw, fh);
}

void Game::applyPresentMode() {
    int interval = 0;
    if (cfg_.presentMode == 1) interval = 1;
    if (cfg_.presentMode == 2) {
        bool tear = glfwExtensionSupported("WGL_EXT_swap_control_tear") || glfwExtensionSupported("GLX_EXT_swap_control_tear");
        interval = tear ? -1 : 1; // adaptive: vsync, but tear instead of waiting when a frame is late
    }
    glfwSwapInterval(interval);
}

void Game::beforeInput() {
    // Low latency "on": allow at most one frame queued on the GPU. We wait for the previous
    // frame to finish before sampling input, so input is never stuck behind a deep queue.
    if (cfg_.lowLatency == 1) {
        int prev = fenceIdx_ ^ 1;
        if (fence_[prev]) {
            glClientWaitSync((GLsync)fence_[prev], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
            glDeleteSync((GLsync)fence_[prev]);
            fence_[prev] = nullptr;
        }
    }
}

void Game::afterSwap() {
    // Low latency "ultra": wait until this frame is fully rendered before starting the next
    // one (nothing queued), then the next frame samples input at the last possible moment.
    if (cfg_.lowLatency >= 1) {
        GLsync f = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (cfg_.lowLatency == 2) {
            glClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
            glDeleteSync(f);
        } else {
            glFlush();
            if (fence_[fenceIdx_]) glDeleteSync((GLsync)fence_[fenceIdx_]);
            fence_[fenceIdx_] = f;
            fenceIdx_ ^= 1;
        }
    }
    if (!showPerf_) return;
    // Input-to-frame latency: GPU timestamp written after this frame's commands, converted to
    // the CPU clock, minus when the game received the input event from the OS. Read back a few
    // frames later without stalling.
    double now = timeNow();
    if (now - gpuCalibAt_ > 1.0) {
        GLint64 g = 0;
        glGetInteger64v(GL_TIMESTAMP, &g);
        gpuClockOffset_ = timeNow() - g * 1e-9;
        gpuCalibAt_ = now;
    }
    if (frameInput_ >= 0) {
        LatQuery& q = latQ_[latNext_];
        if (!q.used) {
            glQueryCounter(q.q, GL_TIMESTAMP);
            q.input = frameInput_;
            q.used = true;
            latNext_ = (latNext_ + 1) % 16;
        }
    }
    for (auto& q : latQ_) {
        if (!q.used) continue;
        GLint avail = 0;
        glGetQueryObjectiv(q.q, GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) continue;
        GLuint64 t = 0;
        glGetQueryObjectui64v(q.q, GL_QUERY_RESULT, &t);
        double done = t * 1e-9 + gpuClockOffset_;
        double ms = (done - q.input) * 1000.0;
        if (ms >= 0 && ms < 1000) latSamples_.push_back((float)ms);
        q.used = false;
    }
}

// Every action, two bind slots each. Click a slot, then press a key / mouse button / scroll.
// Esc cancels, Backspace or Delete clears the slot.
void Game::drawControls(float x0, float y0, float wTotal) {
    float s = ui_.s();
    float rowH = 22 * s, gap = 3 * s, colGap = 24 * s;
    float colW = std::max(330 * s, wTotal * 0.5f);
    float x = x0 + wTotal * 0.5f - colW - colGap * 0.5f;
    std::vector<int> actions; // every rebindable action (crouch was removed from the game)
    for (int a = 0; a < A_COUNT; a++) if (a != A_CROUCH && a != A_WEAPON9) actions.push_back(a);
    int perCol = ((int)actions.size() + 1) / 2;
    float h = perCol * (rowH + gap) + 90 * s;
    float y = std::max(8 * s, y0 - 40 * s);
    ui_.panel(x - 16 * s, y - 16 * s, colW * 2 + colGap + 32 * s, h);
    float lts = std::max(1.0f, std::round(ui_.textScale() * 0.75f));
    for (int ai = 0; ai < (int)actions.size(); ai++) {
        int a = actions[ai];
        float cx = x + (ai / perCol) * (colW + colGap), cy = y + (ai % perCol) * (rowH + gap);
        r_.text(cx, cy + 5 * s, lts, actionName(a), {0.85f, 0.87f, 0.9f, 1});
        float bw = colW * 0.24f, xw = rowH;
        if (ui_.button(cx + colW - xw, cy, xw, rowH, "X")) { // unbind both slots
            cfg_.binds.code[a][0] = cfg_.binds.code[a][1] = 0;
            if (captureAction_ == a) captureAction_ = -1;
            cfg_.save();
        }
        for (int k = 0; k < 2; k++) {
            float bx = cx + colW - xw - 4 * s - (2 - k) * (bw + 4 * s);
            bool cap = captureAction_ == a && captureSlot_ == k;
            std::string label = cap ? "PRESS..." : inputName(cfg_.binds.code[a][k]);
            if (ui_.button(bx, cy, bw, rowH, label)) { captureAction_ = a; captureSlot_ = k; }
        }
    }
    y += perCol * (rowH + gap) + 10 * s;
    ui_.label(x, y, captureAction_ >= 0 ? "PRESS A KEY, MOUSE BUTTON OR SCROLL - ESC CANCELS, BACKSPACE CLEARS"
                                        : "CLICK A SLOT TO REBIND IT - X UNBINDS THE ACTION", {0.7f, 0.72f, 0.75f, 1}, 0.5f);
    y += 20 * s;
    float bw = colW * 0.6f;
    if (ui_.button(x, y, bw, 32 * s, "RESET TO DEFAULTS")) { cfg_.binds.setDefaults(); cfg_.save(); }
    if (ui_.button(x + colW * 2 + colGap - bw, y, bw, 32 * s, "BACK")) { controlsOpen_ = false; captureAction_ = -1; cfg_.save(); }
}

// Every map or mode as a grid of buttons. Maps that can't run the chosen mode are greyed out, and
// each map says which mode it was built for.
void Game::drawPicker(float cx, float y) {
    float s = ui_.s(), bh = 40 * s, gap = 8 * s;
    bool modes = picker_ == PICK_HOST_MODE || picker_ == PICK_BOT_MODE || picker_ == PICK_LIVE_MODE;
    bool host = picker_ == PICK_HOST_MAP || picker_ == PICK_HOST_MODE;
    bool live = picker_ == PICK_LIVE_MAP || picker_ == PICK_LIVE_MODE; // in game: changes the running server
    int& mode = live ? liveMode_ : host ? hostModeIdx_ : cfg_.botMode;
    int& mapIdx = live ? liveMap_ : host ? hostMapIdx_ : botMapIdx_;
    int n = modes ? MODE_COUNT : (int)maps_.size();
    int cols = 3;
    float bw = 250 * s, w = cols * bw + (cols - 1) * gap;
    float x = cx - w * 0.5f;
    int rows = (n + cols - 1) / cols;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 40 * s + rows * (bh + 18 * s + gap) + bh + 30 * s);
    ui_.labelCentered(cx, y, modes ? "CHOOSE A MODE" : std::string("CHOOSE A MAP FOR ") + modeTitle(mode));
    y += 34 * s;
    for (int i = 0; i < n; i++) {
        float bx = x + (i % cols) * (bw + gap), by = y + (i / cols) * (bh + 18 * s + gap);
        std::string label, sub;
        bool ok = true, cur;
        if (modes) {
            label = modeTitle(i);
            cur = i == mode;
            for (auto& mi : mapInfo_) if (mi.preferredMode == i) sub = "MAP: " + upper(mi.title);
        } else {
            const MapInfo& mi = mapInfo_[i];
            label = upper(maps_[i]);
            cur = i == mapIdx;
            ok = (mi.modes >> mode) & 1;
            sub = !ok ? "DOESN'T SUPPORT THIS MODE" : mi.preferredMode >= 0 ? std::string("BUILT FOR ") + modeTitle(mi.preferredMode) : "ALL MODES";
        }
        if (cur) r_.rect(bx - 2 * s, by - 2 * s, bw + 4 * s, bh + 4 * s, ui_.accent);
        if (ui_.button(bx, by, bw, bh, label, ok)) {
            if (modes) {
                mode = i;
                if (!live || !((mapInfo_[mapIdx].modes >> i) & 1)) mapIdx = bestMapFor(i); // live: keep the map if it can
            } else mapIdx = i;
            picker_ = PICK_NONE;
            if (live) {
                net_.sendCommand("map " + maps_[mapIdx] + " " + modeName(mode));
                paused_ = false;
            } else cfg_.save();
        }
        ui_.label(bx + 2 * s, by + bh + 3 * s, sub, ok ? Color{0.7f, 0.72f, 0.75f, 1} : Color{0.5f, 0.3f, 0.3f, 1}, 0.5f);
    }
    y += rows * (bh + 18 * s + gap) + 10 * s;
    if (ui_.button(cx - 100 * s, y, 200 * s, bh, "BACK")) picker_ = PICK_NONE;
}

// Crosshair from the settings: style, size, thickness, gap, color, outline. A hit flashes it white.
void Game::drawCrosshair(float cx, float cy, float scale, bool hit) {
    float u = std::max(1.0f, std::round(scale));
    float t = std::max(1.0f, std::round(cfg_.chThickness * u)), len = std::round(cfg_.chLength * u), g = std::round(cfg_.chGap * u);
    Color c = hit ? Color{1, 1, 1, 1} : rgba(cfg_.chTeamColor ? cfg_.colorFriendly : cfg_.chColor);
    cx = std::round(cx); cy = std::round(cy);
    std::vector<std::array<float, 4>> rs; // x, y, w, h
    int st = cfg_.chStyle;
    if (st == 0 || st == 1 || st == 4) {
        if (len > 0) {
            if (st != 4) rs.push_back({cx - t * 0.5f, cy - g - len, t, len});        // up (not on T)
            rs.push_back({cx - t * 0.5f, cy + g, t, len});                           // down
            rs.push_back({cx - g - len, cy - t * 0.5f, len, t});                     // left
            rs.push_back({cx + g, cy - t * 0.5f, len, t});                           // right
        }
    }
    if (st == 1 || st == 2) rs.push_back({cx - t * 0.5f, cy - t * 0.5f, t, t});
    if (st == 3) { // ring of small blocks
        float R = g + len * 0.5f + t;
        for (int i = 0; i < 24; i++) {
            float a = i * 2 * PI / 24;
            rs.push_back({cx + std::cos(a) * R - t * 0.5f, cy + std::sin(a) * R - t * 0.5f, t, t});
        }
        rs.push_back({cx - t * 0.5f, cy - t * 0.5f, t, t});
    }
    if (cfg_.chOutline)
        for (auto& r : rs) r_.rect(r[0] - 1, r[1] - 1, r[2] + 2, r[3] + 2, {0, 0, 0, c.a * 0.9f});
    for (auto& r : rs) r_.rect(r[0], r[1], r[2], r[3], c);
    if (hit && st != 1 && st != 2) r_.rect(cx - t, cy - t, 2 * t, 2 * t, {1, 1, 1, 1});
}

void Game::drawCrosshairSettings(float x0, float y0, float wTotal) {
    float s = ui_.s();
    float h = 32 * s, gap = 7 * s;
    float w = std::max(360 * s, wTotal * 0.5f);
    float x = x0 + wTotal * 0.5f - w * 0.5f;
    float y = y0;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 110 * s + 7 * (h + gap) + 6 * gap + 32 * s);
    // preview on a mid-grey square, like the world behind it
    float ph = 110 * s;
    r_.rect(x, y, w, ph, {0.42f, 0.43f, 0.45f, 1});
    drawCrosshair(x + w * 0.5f, y + ph * 0.5f, s, false);
    y += ph + gap;
    static const std::vector<std::string> styles = {"CROSS", "CROSS + DOT", "DOT", "CIRCLE", "T"};
    ui_.cycle(x, y, w, h, "STYLE", cfg_.chStyle, 5, styles);
    y += h + gap;
    ui_.slider(x, y, w, h, "LENGTH", cfg_.chLength, 0, 30, "%.0f");
    cfg_.chLength = std::round(cfg_.chLength);
    y += h + gap;
    ui_.slider(x, y, w, h, "THICKNESS", cfg_.chThickness, 1, 8, "%.0f");
    cfg_.chThickness = std::round(cfg_.chThickness);
    y += h + gap;
    ui_.slider(x, y, w, h, "GAP", cfg_.chGap, 0, 20, "%.0f");
    cfg_.chGap = std::round(cfg_.chGap);
    y += h + gap;
    ui_.toggle(x, y, w * 0.5f - gap * 0.5f, h, "OUTLINE", cfg_.chOutline);
    ui_.toggle(x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "TEAM COLOR", cfg_.chTeamColor);
    y += h + gap;
    if (!cfg_.chTeamColor) {
        float sw = (w - 2 * gap) / 3;
        const char* ch[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            float v = cfg_.chColor[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f")) cfg_.chColor[i] = std::round(v) / 255.0f;
        }
    }
    y += h + gap * 3;
    if (ui_.button(x, y, w * 0.5f - gap * 0.5f, h, "RESET")) {
        Settings d;
        cfg_.chStyle = d.chStyle; cfg_.chLength = d.chLength; cfg_.chThickness = d.chThickness; cfg_.chGap = d.chGap;
        cfg_.chOutline = d.chOutline; cfg_.chTeamColor = d.chTeamColor; cfg_.chColor = d.chColor;
    }
    if (ui_.button(x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "BACK")) { crosshairOpen_ = false; cfg_.save(); }
}

// Footsteps, jumps, landings, dash and grapple for you; footsteps, gunfire and grapples for
// everyone else (positioned in 3D).
void Game::updateSounds(const NetClient::PlayerArray& players) {
    const int me = net_.clientId();
    bool alive = net_.selfAlive();
    const MoveState& m = net_.predicted();
    if (alive && prevAlive_) {
        bool was = prevMove_.flags & MF_ONGROUND, now = m.flags & MF_ONGROUND;
        Vec3 d = m.pos - prevMove_.pos;
        float h = std::sqrt(d.x * d.x + d.z * d.z);
        if (now && h < 2.0f) {
            stepDist_ += h;
            if (stepDist_ > 2.4f) { stepDist_ = 0; audio_.play(SND_STEP, 0.55f, 0.9f + (rand() % 20) * 0.01f); }
        }
        if (was && !now && m.vel.y > 3) audio_.play(SND_JUMP, 0.5f);
        if (!was && now && prevMove_.vel.y < -6) audio_.play(SND_LAND, clampf(-prevMove_.vel.y / 20.0f, 0.3f, 1.0f));
        if (m.dashTimer > 0 && prevMove_.dashTimer <= 0) audio_.play(SND_DASH, 0.8f);
        if ((m.flags & MF_HOOKED) && !(prevMove_.flags & MF_HOOKED)) {
            audio_.play(SND_GRAPPLE_FIRE, 0.7f);
            audio_.play3D(SND_GRAPPLE_HIT, m.hookPoint, 0.9f);
        }
    }
    prevMove_ = m;
    prevAlive_ = alive;

    for (int i = 0; i < kMaxPlayers; i++) {
        RemoteAudio& ra = remoteAudio_[i];
        if (i == me || !players[i].valid || !(players[i].snap.flags & PS_ALIVE)) { ra.valid = false; continue; }
        const PlayerSnap& p = players[i].snap;
        if (ra.valid) {
            Vec3 d = p.pos - ra.pos;
            float h = std::sqrt(d.x * d.x + d.z * d.z);
            if (h < 2.0f && std::fabs(d.y) < 0.05f) { // walking on the ground
                ra.stepDist += h;
                if (ra.stepDist > 2.4f) { ra.stepDist = 0; audio_.play3D(SND_STEP, p.pos, 0.8f, 0.9f + (rand() % 20) * 0.01f); }
            }
            if (p.hooked && !ra.hooked) audio_.play3D(SND_GRAPPLE_HIT, p.hookPoint, 0.9f);
            bool firing = (p.flags & PS_FIRING) != 0;
            if (firing && p.weapon == WPN_LASER && time_ >= ra.nextLaser) {
                audio_.play3D(SND_LASER, p.pos + Vec3(0, 1.5f, 0), 0.7f);
                ra.nextLaser = time_ + 0.05;
            } else if (firing && !remoteFiring_[i] && p.weapon != WPN_LASER) {
                audio_.play3D(SND_RIFLE + p.weapon, p.pos + Vec3(0, 1.5f, 0), 1.0f, 0.97f + (rand() % 7) * 0.01f);
            }
        }
        ra.pos = p.pos;
        ra.hooked = p.hooked;
        ra.valid = true;
    }
}
