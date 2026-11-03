// TUTORIAL (1.19.9, "Tutorial update"; MAIN MENU > TUTORIAL, offered once on the first launch; `quark --tutorial
// [LESSON[:PHASE]]`): the movement tech, one lesson a tech, on the map `tutorial` (tools/maps/tutorial.py). q: "show a
// demo of a bot doing whatever movement tech, then puts you on a map that shows the bot doing it and gives you
// instructions on how to".
//
// A local game like TIME TRIAL (startHost with ServerConfig::tutorial). Its server runs the GHOST, a bot playing the
// lesson's input script in its lane beside yours, looping (src/game/tutorial_server.cpp). Here, each lesson:
// - DEMO: the camera follows the ghost doing the tech; your input is held; the keys it is pressing light up under
//   their names in your own binds (the script at the time the ghost is drawn at: the same tut::ghostCmd the server
//   runs), with where it aims; the steps are on the left. JUMP / ENTER / a click: your turn.
// - TRY: you at your lane's start, the ghost looping beside you; the lesson's check (tut::Check) fed your own predicted
//   movement each frame; its bar and number. RACE RESET (R): back to the start; ENTER: skip; Esc: the lessons.
// - DONE: a moment, then the next lesson; after the last, TUTORIAL COMPLETE (the pause menu's page).
// Cost when off: tut_.on tested in a few places. On: no allocation per frame (the text is built once a lesson and
// when the pause menu closes, as the binds may have changed; the readout is formatted into a string that keeps its
// capacity).
#include "game.h"
#include "binds.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kGold = {1.0f, 0.8f, 0.3f, 1};
const Color kGreen = {0.45f, 0.95f, 0.5f, 1};

// The keys shown: the ghost's (lit) under your binds' names
constexpr int kTutActs[9] = {A_FORWARD, A_LEFT, A_BACK, A_RIGHT, A_JUMP, A_DASH, A_GRAPPLE, A_FIRE, A_ALTFIRE};
enum { K_F, K_L, K_B, K_R, K_J, K_D, K_G, K_FI, K_AL, K_N = -1 };
uint16_t keyBit(int k) {
    static const uint16_t b[9] = {0, 0, 0, 0, BTN_JUMP, BTN_DASH, BTN_GRAPPLE, BTN_FIRE, BTN_ALTFIRE};
    return b[k];
}

// A step of a lesson: up to three of your keys (keys: their format, %s each, in order), then what to do. Drawn as
// "KEYS: TEXT", so the text is a key of the language files by itself (lang.cpp translates each side of ": ")
struct Step { const char* keys; int8_t k[3]; const char* text; };
struct Text {
    const char* title;
    const char* what;   // what it is and why
    Step steps[4];
    int nSteps;
    const char* goal;
};
// (in tut::Lesson order; every string a key of the language files: tools/lang/extract.py)
const Text kTexts[tut::L_COUNT] = {
    {"WALKING", "RUN, JUMP THE HURDLES, REACH THE GLOWING LINE.",
     {{"%s", {K_F, K_N, K_N}, "RUN FORWARD"}, {"%s", {K_J, K_N, K_N}, "JUMP THE HURDLES. YOU KEEP YOUR SPEED IN THE AIR"},
      {"%s %s %s", {K_L, K_B, K_R}, "STEP LEFT, BACK AND RIGHT"}}, 3, "REACH THE GLOWING LINE"},
    {"AD STRAFING", "BUNNY HOP WITH ONE STRAFE KEY AT A TIME, TURNING WITH IT: SPEED WITH NO DASH.",
     {{"%s", {K_J, K_N, K_N}, "HOLD IT. YOU HOP AGAIN EVERY TIME YOU LAND"},
      {"%s", {K_R, K_N, K_N}, "IN THE AIR, HOLD ONLY THIS AND TURN THE MOUSE RIGHT, SMOOTHLY"},
      {"%s", {K_L, K_N, K_N}, "NEXT HOP, HOLD ONLY THIS AND TURN LEFT"},
      {"%s", {K_F, K_N, K_N}, "DON'T HOLD IT WHILE YOU STRAFE"}}, 4, "REACH 12 M/S WITH NO DASH OR GRAPPLE"},
    {"DASH", "A BURST OF 24 M/S THE WAY YOU MOVE. TWO CHARGES, EACH BACK IN 1.2 S.",
     {{"%s", {K_F, K_N, K_N}, "RUN AT THE GAP"}, {"%s", {K_J, K_N, K_N}, "JUMP AT THE EDGE"},
      {"%s", {K_D, K_N, K_N}, "DASH IN THE AIR TO CROSS THE LAVA"}}, 3, "LAND ON THE FAR SIDE OF THE LAVA"},
    {"SIDEWAYS DASHES", "A DASH GOES THE WAY YOU MOVE, NOT THE WAY YOU LOOK.",
     {{"%s + %s", {K_R, K_D, K_N}, "DASH RIGHT"}, {"%s + %s", {K_L, K_D, K_N}, "DASH LEFT"},
      {"%s", {K_F, K_N, K_N}, "DON'T HOLD IT, OR THE DASH GOES FORWARD TOO"}}, 3, "DASH LEFT AND DASH RIGHT"},
    {"SUPER", "JUMP WHILE YOU DASH ALONG THE FLOOR: 24 M/S, UP AND AHEAD.",
     {{"%s", {K_F, K_N, K_N}, "HOLD IT"}, {"%s THEN %s", {K_D, K_J, K_N}, "DASH, AND JUMP A MOMENT LATER"},
      {nullptr, {K_N, K_N, K_N}, "THE JUMP MUST COME WHILE THE DASH STILL RUNS (0.12 S)"}}, 3, "2 SUPERS"},
    {"HYPER", "DASH DOWN INTO THE FLOOR WHILE YOU JUMP: YOUR SPEED + 5 M/S, UP AND AHEAD.",
     {{"%s", {K_F, K_N, K_N}, "RUN"}, {nullptr, {K_N, K_N, K_N}, "AIM AT THE FLOOR A FEW METRES AHEAD (15 DEGREES DOWN IS ENOUGH)"},
      {"%s + %s", {K_D, K_J, K_N}, "DASH AND JUMP TOGETHER"}}, 3, "2 HYPERS"},
    {"SUPER CHAINING", "HYPER AGAIN EVERY TIME YOU LAND: EACH ONE ADDS 5 M/S, UP TO 36.",
     {{"%s + %s + %s", {K_F, K_J, K_D}, "HOLD ALL THREE"}, {nullptr, {K_N, K_N, K_N}, "AIM A LITTLE DOWN THE WHOLE WAY"},
      {nullptr, {K_N, K_N, K_N}, "EVERY LANDING HYPERS AGAIN AS SOON AS A DASH CHARGE IS BACK"}}, 3, "REACH 27 M/S"},
    {"DASH SLIDE", "DASH DOWN INTO THE FLOOR WITHOUT A JUMP: YOU SLIDE AS ON ICE AND KEEP YOUR SPEED.",
     {{"%s THEN %s", {K_D, K_J, K_N}, "A SUPER FOR SPEED"}, {nullptr, {K_N, K_N, K_N}, "AIM A LITTLE DOWN"},
      {"%s", {K_D, K_N, K_N}, "DASH JUST BEFORE YOU LAND, AND DON'T JUMP"}}, 3, "SLIDE FOR 0.8 S"},
    {"WALL BOUNCE", "DASH HEAD ON INTO A WALL: YOU BOUNCE BACK OFF IT AT 16 M/S.",
     {{"%s", {K_F, K_N, K_N}, "WALK TO THE STRIPED WALL"}, {nullptr, {K_N, K_N, K_N}, "FACE IT (WITHIN 45 DEGREES)"},
      {"%s", {K_D, K_N, K_N}, "DASH INTO IT"}}, 3, "2 BOUNCES"},
    {"GRAPPLE", "THE HOOK PULLS YOU TO WHERE IT CATCHES. LET GO AND YOU KEEP THE SPEED.",
     {{nullptr, {K_N, K_N, K_N}, "AIM UP, OVER THE EDGE OF THE LEDGE"}, {"%s", {K_G, K_N, K_N}, "HOLD IT. IT PULLS YOU TO THE WALL AND UP"},
      {"%s", {K_F, K_N, K_N}, "ONCE ABOVE THE EDGE, LET GO OF THE GRAPPLE AND MOVE ONTO THE LEDGE"}}, 3, "STAND ON THE 10 M LEDGE"},
    {"GRAPPLE WALKING", "HOOK THE FLOOR FAR AHEAD: IT DRAGS YOU ALONG WITH NO FRICTION, UP TO 24 M/S.",
     {{nullptr, {K_N, K_N, K_N}, "AIM JUST UNDER LEVEL, AT THE FLOOR FAR AHEAD"}, {"%s + %s", {K_G, K_F, K_N}, "HOLD BOTH"},
      {nullptr, {K_N, K_N, K_N}, "LET GO BEFORE YOU REACH THE HOOK"}}, 3, "16 M/S ALONG THE FLOOR"},
    {"ROCKET JUMP", "YOUR OWN ROCKET THROWS YOU. HERE IT DOES NO DAMAGE.",
     {{nullptr, {K_N, K_N, K_N}, "AIM STRAIGHT DOWN"}, {"%s + %s", {K_J, K_FI, K_N}, "JUMP AND FIRE TOGETHER"},
      {"%s", {K_F, K_N, K_N}, "STEER ONTO THE LEDGE"}}, 3, "GET 7 M UP"},
    {"THE LAUNCH", "GRAPPLE TO 24 M/S, THEN A ROCKET AND A HYPER STRAIGHT DOWN: YOU FLY, CHEAP AND FAST.",
     {{"%s + %s", {K_G, K_F, K_N}, "GRAPPLE WALK UP TO 24 M/S"},
      {nullptr, {K_N, K_N, K_N}, "THEN ALL AT ONCE: LET GO, AIM STRAIGHT DOWN AND..."},
      {"%s + %s + %s", {K_J, K_D, K_FI}, "JUMP, DASH AND FIRE TOGETHER"},
      {nullptr, {K_N, K_N, K_N}, "THE HYPER LIFTS YOU, THE ROCKET ADDS 20 M/S MORE"}}, 4, "16 M UP, OUT OF A HYPER FROM 20 M/S"},
    {"STICKY JUMP", "LAY A STICKY BOMB, STAND ON IT, SET IT OFF AS YOU JUMP.",
     {{"%s", {K_FI, K_N, K_N}, "AIM STRAIGHT DOWN AND FIRE A STICKY"}, {"%s + %s", {K_J, K_AL, K_N}, "JUMP AND SET IT OFF TOGETHER"},
      {"%s", {K_F, K_N, K_N}, "STEER ONTO THE LEDGE"}}, 3, "GET 7 M UP"},
    {"ROCKET RIDING", "FIRE A ROCKET AND HOOK IT AS IT LEAVES: IT CARRIES YOU ALONG.",
     {{nullptr, {K_N, K_N, K_N}, "AIM STRAIGHT AHEAD"}, {"%s + %s", {K_FI, K_G, K_N}, "FIRE AND GRAPPLE TOGETHER"},
      {"%s", {K_G, K_N, K_N}, "KEEP HOLDING IT TO RIDE"}}, 3, "RIDE FOR 0.4 S"},
};
}  // namespace

static_assert(tut::L_COUNT <= 15);   // (Settings::tutorialDone: a bit a lesson, tutorial_done at most 0x7FFF)

const char* tutorialTitle(int l) { return kTexts[std::clamp(l, 0, tut::L_COUNT - 1)].title; }

void Game::startTutorial(int lesson) {
    tutOffer_ = false;
    if (!cfg_.tutorialOffered) cfg_.tutorialOffered = true;   // (startHost saves the config)
    tut_ = TutState();
    tut_.lesson = std::clamp(lesson, 0, tut::L_COUNT - 1);
    tutorialStart_ = true;
    startHost(true);
    tutorialStart_ = false;
    if (!host_) { setScreen(Screen::Main); return; }   // (error_ says why)
    tut_.on = true;
    net_.noPlayerHooks = true;
}

// To a lesson: the server puts you at its start and the ghost's loop starts half a second on (the view time + 0.5 s:
// it stands still till then), the demo for one loop of it
void Game::tutorialBegin(int lesson) {
    const int l = std::clamp(lesson, 0, tut::L_COUNT - 1);
    tut_.lesson = l;
    tut_.begun = true;
    tut_.phase = TutState::DEMO;
    tut_.phaseAt = time_;
    tut_.camSnap = true;
    tut_.check.start(l);
    tut_.ev = 0;
    tut_.ghostEv = 0;
    for (double& k : tut_.keyLit) k = 0;
    tut_.epochMs = (int64_t)std::llround(net_.renderTime() * 1000.0) + 500;
    tut_.demoEnd = time_ + 0.5 + tut::loopMs(l) * 0.001 + 0.4;
    char b[96];
    snprintf(b, sizeof b, "tutorial %s %lld", tut::lesson(l).id, (long long)tut_.epochMs);
    net_.sendCommand(b);
    if (tut::lesson(l).weapon < WPN_COUNT) wp_.want(tut::lesson(l).weapon);
    tutorialTexts();
}

// The lesson's text with your keys in it (and the hints): once a lesson, and when the pause menu closes
void Game::tutorialTexts() {
    const Text& t = kTexts[tut_.lesson];
    for (int k = 0; k < 9; k++) tut_.keyName[k] = keyOf(kTutActs[k]);
    tut_.title = t.title;
    tut_.what = t.what;
    tut_.goal = t.goal;
    tut_.nSteps = t.nSteps;
    char b[256];
    auto key = [&](int k) { return k >= 0 ? tut_.keyName[k].c_str() : ""; };
    for (int i = 0; i < t.nSteps; i++) {
        const Step& st = t.steps[i];
        if (!st.keys) { tut_.steps[i] = st.text; continue; }
        char k[96];
        snprintf(k, sizeof k, st.keys, key(st.k[0]), key(st.k[1]), key(st.k[2]));
        snprintf(b, sizeof b, "%s: %s", k, st.text);
        tut_.steps[i] = b;
    }
    const std::string jump = keyOf(A_JUMP), chat = keyOf(A_CHAT), reset = keyOf(A_RACERESET);
    static const char* const kTryNow = "TRY IT NOW", *const kStartOver = "START OVER", *const kSkip = "SKIP";
    snprintf(b, sizeof b, "%s: %s", jump.c_str(), kTryNow); tut_.demoHint = b;
    snprintf(b, sizeof b, "%s: %s", reset.c_str(), kStartOver); tut_.keysHint = b;
    snprintf(b, sizeof b, "%s: %s", chat.c_str(), kSkip); tut_.skipHint = b;
    snprintf(b, sizeof b, "LESSON %d OF %d", tut_.lesson + 1, (int)tut::L_COUNT); tut_.scoreLine = b;
}

void Game::tutorialFrame() {
    if (!net_.hasSnapshot()) return;
    const auto& players = framePlayers();
    const bool alive = net_.selfAlive();
    if (tut_.ghost < 0 || !players[tut_.ghost].valid) {   // (the ghost: the bot called GHOST; looked for till found)
        tut_.ghost = -1;
        for (int i = 0; i < kMaxPlayers; i++)
            if (i != net_.clientId() && players[i].valid && net_.playerName(i) == "GHOST") { tut_.ghost = i; break; }
    }
    if (!tut_.begun) {
        if (!alive) return;
        tutorialBegin(tut_.lesson);
        if (const size_t c = opts_.tutorialAt.find(':'); c != std::string::npos) {   // (--tutorial LESSON:PHASE: screenshots)
            const std::string ph = opts_.tutorialAt.substr(c + 1);
            if (ph == "try") tut_.demoEnd = time_;
            else if (ph == "done") { tut_.phase = TutState::DONE; tut_.phaseAt = time_ + 1e6; tut_.check.progress = 1; }
            else if (ph == "finish") { tut_.phase = TutState::FINISH; paused_ = true; }
            else if (ph == "lessons") { paused_ = true; tut_.menu = true; }
            else if (ph == "pause") paused_ = true;
            opts_.tutorialAt.clear();
        }
    }
    if (paused_ != tut_.wasPaused) {   // (back from the pause menu: the binds may have changed)
        tut_.wasPaused = paused_;
        if (!paused_) tutorialTexts();
    }
    const uint8_t ev = tut_.ev;
    tut_.ev = 0;
    // the ghost's keys at the time it's drawn at (its script, from the loop's start), each lit at least 0.3 s
    const int loop = std::max(1, tut::loopMs(tut_.lesson));
    const int64_t t = (int64_t)std::llround(net_.renderTime() * 1000.0) - tut_.epochMs;
    const UserCmd gc = tut::ghostCmd(tut_.lesson, t < 0 ? -1 : (int)(t % loop));
    for (int k = 0; k < 9; k++) {
        const bool on = k == K_F ? gc.forward > 0 : k == K_B ? gc.forward < 0 : k == K_L ? gc.side < 0 : k == K_R ? gc.side > 0 : (gc.buttons & keyBit(k)) != 0;
        if (on) tut_.keyLit[k] = time_ + 0.3;
    }
    {   // its turning (AD strafing): degrees a second, smoothed
        float d = gc.yaw - tut_.prevGhostYaw;
        if (d > PI) d -= 2 * PI;
        if (d < -PI) d += 2 * PI;
        const float rate = frameDt_ > 1e-5 ? d / (float)frameDt_ : 0.0f;
        tut_.turn += (rate - tut_.turn) * std::min(1.0f, (float)frameDt_ * 12.0f);
        tut_.prevGhostYaw = gc.yaw;
    }
    if (tut_.ghost >= 0) {   // its callouts: SUPER / HYPER over it as it does them
        const uint8_t gev = (uint8_t)(players[tut_.ghost].snap.keys >> kKeyEvShift);
        if (gev & (ME_SUPER | ME_HYPER)) { tut_.ghostEv = gev & ME_HYPER ? ME_HYPER : ME_SUPER; tut_.ghostEvAt = time_; }
    }
    switch (tut_.phase) {
        case TutState::DEMO:
            if (time_ >= tut_.demoEnd || (uiIn_.pressed && !paused_ && !chatting_)) {
                tut_.phase = TutState::TRY;
                tut_.phaseAt = time_;
                tut_.check.start(tut_.lesson);
                yaw_ = tut::kStartYaw;   // (looking down your lane, whatever the mouse did meanwhile)
                pitch_ = 0;
            }
            break;
        case TutState::TRY: {
            if (!alive) { tut_.wasAlive = false; break; }
            if (!tut_.wasAlive) { tut_.wasAlive = true; tut_.check.start(tut_.lesson); }   // (back from lava: a fresh try)
            const bool held = !paused_ && !chatting_;
            const int fwd = held ? (isDown(A_FORWARD) ? 127 : 0) - (isDown(A_BACK) ? 127 : 0) : 0;
            const int side = held ? (isDown(A_RIGHT) ? 127 : 0) - (isDown(A_LEFT) ? 127 : 0) : 0;
            tut_.check.frame(net_.predicted(), ev, fwd, side, heldWeapon_, (float)frameDt_);
            if (tut_.check.done) {
                tut_.phase = TutState::DONE;
                tut_.phaseAt = time_;
                cfg_.tutorialDone |= 1 << tut_.lesson;
                cfg_.save();
                audio_.play(SND_FINISH, 0.8f);
            }
            break;
        }
        case TutState::DONE:
            if (time_ - tut_.phaseAt >= 2.5) {
                if (tut_.lesson + 1 < tut::L_COUNT) tutorialBegin(tut_.lesson + 1);
                else { tut_.phase = TutState::FINISH; tut_.menu = false; paused_ = true; captureMouse(false); }
            }
            break;
        case TutState::FINISH: break;
    }
    // the readout under the bar (formatted into a string that keeps its capacity)
    const tut::LessonDef& d = tut::lesson(tut_.lesson);
    const tut::Check& c = tut_.check;
    char b[64];
    switch (d.goal) {
        case tut::G_REACH: case tut::G_GAP: snprintf(b, sizeof b, "%.0f / %.0f M", c.value, d.target - tut::kStartIn); break;
        case tut::G_SPEED: case tut::G_HOOKSPEED: snprintf(b, sizeof b, "%.1f / %.0f M/S", c.value, d.target); break;
        case tut::G_SIDEDASH: case tut::G_EVENTS: case tut::G_BOUNCES: snprintf(b, sizeof b, "%d / %.0f", c.count, d.target); break;
        case tut::G_SLIDE: case tut::G_RIDE: snprintf(b, sizeof b, "%.1f / %.1f S", std::max(c.value, c.best), d.target); break;
        default: snprintf(b, sizeof b, "%.1f / %.0f M", std::max(c.value, c.best), d.target); break;
    }
    tut_.readout = b;
}

// (onKey, before the chat key opens the chat: in the tutorial ENTER skips)
bool Game::tutorialKey(int key) {
    if (!tut_.on || !tut_.begun || paused_ || chatting_) return false;
    const Binds& b = binds();
    if (tut_.phase == TutState::DEMO && (b.has(A_JUMP, key) || b.has(A_CHAT, key))) {
        tut_.demoEnd = time_;   // (your turn: tutorialFrame hands over)
        return true;
    }
    if (b.has(A_CHAT, key) && (tut_.phase == TutState::TRY || tut_.phase == TutState::DONE)) {
        if (tut_.lesson + 1 < tut::L_COUNT) tutorialBegin(tut_.lesson + 1);
        else { tut_.phase = TutState::FINISH; paused_ = true; captureMouse(false); }
        return true;
    }
    if (b.has(A_RACERESET, key) && tut_.phase == TutState::TRY) {
        net_.sendCommand("tutorial reset");
        tut_.check.start(tut_.lesson);
        return true;
    }
    return false;
}

// The demo's camera: behind the ghost and over your lane, looking at it, following it (eased: 90% in 0.4 s), kept
// inside its bay
void Game::tutorialCamera(const NetClient::PlayerArray& players, Vec3& eye, float& yaw, float& pitch) {
    if (tut_.ghost < 0 || !players[tut_.ghost].valid || !(players[tut_.ghost].snap.flags & PS_ALIVE)) return;
    const tut::LessonDef& d = tut::lesson(tut_.lesson);
    const Vec3 g = players[tut_.ghost].snap.pos + Vec3(0, 1.2f, 0);
    const float x0 = tut::bayX0(tut_.lesson);
    Vec3 want = g + Vec3(-7.5f, 3.0f, 7.0f);
    want.x = clampf(want.x, x0 + 0.6f, x0 + d.len - 0.6f);
    want.y = clampf(want.y, 1.0f, d.height - 0.8f);
    want.z = clampf(want.z, tut::bayZ(tut_.lesson) - tut::kBayHalfZ + 0.6f, tut::bayZ(tut_.lesson) + tut::kBayHalfZ - 0.6f);
    if (tut_.camSnap) { tut_.camPos = want; tut_.camSnap = false; }
    else tut_.camPos = tut_.camPos + (want - tut_.camPos) * (1.0f - (float)std::exp(-frameDt_ * 5.5));
    eye = tut_.camPos;
    const Vec3 to = g - eye;
    yaw = std::atan2(-to.x, -to.z);
    pitch = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z));
}

// The tutorial's HUD: the lesson at the top, its steps and the goal on the left, the ghost's keys on the right
void Game::drawTutorialHud(float W, float H, float s) {
    if (!tut_.begun) return;
    const tut::LessonDef& d = tut::lesson(tut_.lesson);
    const float ts = ui_.textScale(), cx = W * 0.5f;
    // the lesson
    ui_.labelCentered(cx, 72 * s, tut_.title, d.key ? kGold : kWhite, 2.0f, 760 * s);
    if (d.key) {
        const float tw = r_.textWidth(tut_.title, std::max(1.0f, std::round(ts * 2)));
        const float kx = cx + tw * 0.5f + 12 * s, kw = r_.textWidth("KEY TECH", ts) + 12 * s;
        r_.roundRect(kx, 76 * s, kw, 20 * s, 4 * s, kGold);
        r_.text(kx + 6 * s, 76 * s + (20 * s - r_.textHeight(ts)) * 0.5f, ts, "KEY TECH", Color{0.1f, 0.08f, 0.02f, 1});
    }
    ui_.labelCentered(cx, 108 * s, tut_.what, kGrey, 1.0f, 900 * s);
    if (tut_.phase == TutState::DEMO) {
        ui_.labelCentered(cx, 140 * s, "WATCH THE GHOST", ui_.accent, 1.0f, 600 * s);
        ui_.hintCentered(cx, 162 * s, tut_.demoHint, kGrey, 600 * s);
    }
    // the steps and the goal
    const float px = 16 * s, pw = 380 * s, iw = pw - 24 * s;
    float y = 200 * s;
    const float lh = r_.textHeight(ts) + 6 * s;
    int lines = 0;
    for (int i = 0; i < tut_.nSteps; i++) lines += (int)ui_.wrap(tut_.steps[i], ts, iw - 22 * s).size();
    const float ph = 12 * s + 24 * s + lines * lh + tut_.nSteps * 4 * s + 16 * s + 24 * s + lh + 20 * s + 18 * s + 3 * 16 * s + 12 * s;
    ui_.panel(px, y, pw, ph, 0.72f);
    y += 12 * s;
    ui_.label(px + 12 * s, y, tut_.phase == TutState::DEMO ? "HOW" : "YOUR TURN", ui_.accent, 1.0f, iw);
    y += 24 * s;
    char num[4];
    for (int i = 0; i < tut_.nSteps; i++) {
        snprintf(num, sizeof num, "%d", i + 1);
        r_.text(px + 12 * s, y, ts, num, ui_.accent);
        for (const std::string& ln : ui_.wrap(tut_.steps[i], ts, iw - 22 * s)) { ui_.label(px + 34 * s, y, ln, kWhite, 1.0f, iw - 22 * s); y += lh; }
        y += 4 * s;
    }
    y += 16 * s;
    ui_.label(px + 12 * s, y, "GOAL", ui_.accent, 1.0f, iw);
    y += 24 * s;
    ui_.label(px + 12 * s, y, tut_.goal, kWhite, 1.0f, iw);
    y += lh + 4 * s;
    {   // the bar and the number
        const float p = tut_.phase == TutState::DONE || tut_.phase == TutState::FINISH ? 1.0f : tut_.check.progress;
        r_.rect(px + 12 * s, y, iw, 10 * s, {0.1f, 0.11f, 0.13f, 0.9f});
        r_.rect(px + 12 * s, y, iw * p, 10 * s, p >= 1 ? kGreen : ui_.accent);
        y += 16 * s;
        r_.text(px + 12 * s, y, ts, tut_.readout, kGrey);
        y += 22 * s;
    }
    ui_.hint(px + 12 * s, y, tut_.keysHint, kGrey, iw); y += 16 * s;
    ui_.hint(px + 12 * s, y, tut_.skipHint, kGrey, iw); y += 16 * s;
    ui_.hint(px + 12 * s, y, "ESC: LESSONS", kGrey, iw);
    // the ghost's keys: your binds' names, lit while it holds them
    {
        const float gw = 360 * s, gx = W - 16 * s - gw, kh = 30 * s, gap = 6 * s, kw = (gw - 24 * s - 3 * gap) / 4;
        float gy = 200 * s;
        ui_.panel(gx, gy, gw, 12 * s + 24 * s + 3 * (kh + 16 * s + gap) + 2 * lh + 12 * s, 0.72f);
        gy += 12 * s;
        ui_.label(gx + 12 * s, gy, "GHOST'S KEYS", ui_.accent, 1.0f, gw - 24 * s);
        gy += 24 * s;
        const float kts = std::max(1.0f, std::round(ts * 0.75f));
        auto keyBox = [&](int k, int col, int row, int span) {
            const float x = gx + 12 * s + col * (kw + gap), yy = gy + row * (kh + 16 * s + gap), w = span * kw + (span - 1) * gap;
            const bool on = tut_.keyLit[k] > time_;
            r_.roundRect(x, yy, w, kh, 4 * s, on ? ui_.accent : Color{0.06f, 0.07f, 0.09f, 0.85f});
            const std::string& nm = tut_.keyName[k];
            float nts = ts;
            while (nts > 1 && r_.textWidth(nm, nts) > w - 8 * s) nts -= 1;   // (a long key name: smaller)
            r_.text(x + (w - r_.textWidth(nm, nts)) * 0.5f, yy + (kh - r_.textHeight(nts)) * 0.5f, nts, nm, on ? Color{0, 0, 0, 1} : kWhite);
            const char* cap = actionName(kTutActs[k]);
            r_.text(x + (w - r_.textWidth(cap, kts)) * 0.5f, yy + kh + 3 * s, kts, cap, on ? kWhite : kGrey);
        };
        keyBox(K_F, 1, 0, 1);
        keyBox(K_FI, 3, 0, 1);
        keyBox(K_L, 0, 1, 1); keyBox(K_B, 1, 1, 1); keyBox(K_R, 2, 1, 1);
        keyBox(K_AL, 3, 1, 1);
        keyBox(K_J, 0, 2, 1); keyBox(K_D, 1, 2, 1); keyBox(K_G, 2, 2, 2);
        gy += 3 * (kh + 16 * s + gap) + 4 * s;
        // where it aims, and its turning
        const int64_t t = (int64_t)std::llround(net_.renderTime() * 1000.0) - tut_.epochMs;
        const float pitchDeg = tut::ghostCmd(tut_.lesson, t < 0 ? -1 : (int)(t % std::max(1, tut::loopMs(tut_.lesson)))).pitch * 180.0f / PI;
        const char* aim = pitchDeg <= -70 ? "AIM: STRAIGHT DOWN" : pitchDeg <= -8 ? "AIM: A LITTLE DOWN" : pitchDeg >= 8 ? "AIM: UP" : "AIM: LEVEL";
        ui_.label(gx + 12 * s, gy, aim, kWhite, 1.0f, gw - 24 * s);
        gy += lh;
        const float deg = tut_.turn * 180.0f / PI;
        if (std::fabs(deg) > 20) ui_.label(gx + 12 * s, gy, deg > 0 ? "TURNING LEFT" : "TURNING RIGHT", kWhite, 1.0f, gw - 24 * s);
    }
    // the ghost's SUPER / HYPER, over it
    if (tut_.ghost >= 0 && time_ - tut_.ghostEvAt < 0.7) {
        const auto& players = framePlayers();
        float sx, sy;
        if (players[tut_.ghost].valid && r_.project(players[tut_.ghost].snap.pos + Vec3(0, 2.4f, 0), sx, sy)) {
            const char* c = tut_.ghostEv == ME_HYPER ? "HYPER" : "SUPER";
            const float big = std::max(1.0f, std::round(ts * 1.5f));
            r_.text(sx - r_.textWidth(c, big) * 0.5f, sy - r_.textHeight(big), big, c, kGold);
        }
    }
    if (tut_.phase == TutState::DONE) {
        const float big = std::max(2.0f, std::round(ts * 3));
        const char* t = "LESSON DONE";
        r_.text(cx - r_.textWidth(t, big) * 0.5f, H * 0.34f, big, t, kGreen);
        if (tut_.lesson + 1 < tut::L_COUNT) {
            char b[96];
            snprintf(b, sizeof b, "NEXT: %s", kTexts[tut_.lesson + 1].title);
            ui_.labelCentered(cx, H * 0.34f + r_.textHeight(big) + 14 * s, b, kWhite, 1.0f, 600 * s);
        }
    }
}

// MAIN MENU on the first launch: what the tutorial is, START TUTORIAL or NOT NOW (Esc); either way never again
void Game::drawTutorialOffer() {
    const SetLayout L = menuLayout(560);
    const float ts = ui_.textScale(), bw = L.L(528);
    const char* body = "NEW HERE? THE TUTORIAL SHOWS EVERY MOVEMENT TECH: A GHOST DOES IT, THEN YOU DO.";
    const std::vector<std::string>& lines = ui_.wrap(body, ts, bw);
    const int nl = (int)lines.size();
    const float kH = 16 + 8 + nl * 20 - 4 + 12 + 40 + 12 + 14;
    float y = std::round(360 - (kH + 32) * 0.5f) + 16;
    const float x = L.X(376);
    ui_.panel(L.X(360), L.Y(y - 16), L.L(560), L.L(kH + 32), 0.94f);
    ui_.labelCentered(L.X(640), L.Y(y), "WELCOME TO QUARK", kWhite, 1.0f, bw);
    y += 24;
    for (int i = 0; i < nl; i++) ui_.label(x, L.Y(y + 20 * i), lines[i], kGrey, 1.0f, bw);
    y += nl * 20 - 4 + 12;
    const float hw = L.L(258);
    auto close = [&] { tutOffer_ = false; cfg_.tutorialOffered = true; cfg_.save(); };
    if (ui_.button(x, L.Y(y), hw, L.L(40), "NOT NOW", UiExp::nav("main").withId("NOT NOW")) || uiIn_.escape) close();
    else if (ui_.button(x + bw - hw, L.Y(y), hw, L.L(40), "START TUTORIAL", UiExp::state("hosting", "1").withId("START TUTORIAL"))) { close(); startTutorial(0); return; }
    y += 40 + 12;
    ui_.hintCentered(L.X(640), L.Y(y), "IT IS ALWAYS IN THE MAIN MENU: TUTORIAL", kGrey, bw);
}
