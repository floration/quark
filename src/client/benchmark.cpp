// BENCHMARK (main menu; `quark --benchmark [seconds]`): a fixed bot match on Hamlet watched from a fixed camera path,
// its frame times measured, then the result page (a frame-time graph, avg / 1% / 0.1% / min FPS, stdev) and a file
// in the data folder's benchmarks/ to compare runs. The match and the maths: src/game/benchmark.h.
#include "game.h"
#include "../common/u8file.h"
#include "../common/version.h"
#include "../game/benchmark.h"
#include "../game/demo.h"
#include "gl.h"
#include "isa.h"
#include "openurl.h"
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kRed = {1, 0.35f, 0.3f, 1};
constexpr float kBandTop = 166, kBandH = 522, kMRow = 40;   // (the menus' band and row height, as game.cpp's)
float bandTop(float kH) { return kBandTop + std::max(0.0f, (kBandH - (kH + 32)) * 0.5f) + 16; }
constexpr size_t kGraphCols = 464;   // (the graph is 928 wide at 720p: a column every 2 px)

std::string upperStr(std::string t) { for (auto& c : t) c = (char)toupper((unsigned char)c); return t; }
std::string fmt(const char* f, double v) { char b[48]; snprintf(b, sizeof b, f, v); return b; }
std::string glString(unsigned e) { const char* s = (const char*)glGetString(e); return s ? s : "?"; }
std::string buildLabel();
// The CPU's brand string: cpuid on x86 (every OS), else /proc/cpuinfo (Linux) or sysctl (macOS)
std::string cpuBrand() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned r[12] = {};
    if (__get_cpuid_max(0x80000000, nullptr) >= 0x80000004) {
        for (unsigned k = 0; k < 3; k++) __get_cpuid(0x80000002 + k, &r[k * 4], &r[k * 4 + 1], &r[k * 4 + 2], &r[k * 4 + 3]);
        std::string b((const char*)r, 48);
        b = b.c_str();
        const size_t a = b.find_first_not_of(' ');
        if (a != std::string::npos) return b.substr(a);
    }
#endif
#ifdef __APPLE__
    char buf[256] = {};
    size_t len = sizeof buf - 1;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0 && buf[0]) return buf;
#endif
    std::ifstream in("/proc/cpuinfo");
    for (std::string line; std::getline(in, line);)
        if (line.rfind("model name", 0) == 0 || line.rfind("Model", 0) == 0) {
            const size_t c = line.find(':');
            if (c != std::string::npos && c + 2 <= line.size()) return line.substr(c + 2);
        }
    return "unknown";
}

// The build a run came from (the upload's "build"): FLAGS.txt's label, else "ci" for a CI build (a testing version's
// .testing.N, or a launcher install: installed-files.txt beside it), else "local" (built on this machine)
// (+ "-avx2" from the AVX2 build, src/client/isa.h)
std::string buildKind() {
    const std::string flags = buildLabel();
    if (flags != std::string("V") + QUARK_VERSION) return flags;
#if defined(QUARK_ISA_AVX2)
    const char* isaTag = "-avx2";
#else
    const char* isaTag = "";
#endif
    std::error_code ec;
    if (std::string(QUARK_VERSION).find(".testing.") != std::string::npos ||
        (!executableDir().empty() && std::filesystem::exists(std::filesystem::u8path(executableDir() + "/installed-files.txt"), ec))) return std::string("ci") + isaTag;
    return std::string("local") + isaTag;
}

std::string toHex(const std::string& t) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : t) if (c >= 32 && c < 127) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}

std::string benchDir() { return userDataPath("benchmarks"); }
std::string exportDir() { return userDataPath("benchmarks/exports"); }
std::string stemOf(const std::string& path) { return std::filesystem::u8path(path).stem().u8string(); }

// The build's label for a run's default name: the first non-empty line of FLAGS.txt next to the executable (builds
// made with other compiler flags carry one, so their runs name themselves), at most 24 characters; else the version.
std::string buildLabel() {
    if (!executableDir().empty()) {
        std::ifstream in(std::filesystem::u8path(executableDir() + "/FLAGS.txt"));
        std::string line;
        while (std::getline(in, line)) {
            std::string t;
            for (char c : line) if (c >= 32 && c < 127 && c != ',') t += c;
            while (!t.empty() && t.back() == ' ') t.pop_back();
            size_t a = t.find_first_not_of(' ');
            if (a == std::string::npos) continue;
            t = t.substr(a, 24);
            while (t.back() == ' ') t.pop_back();
            return t;
        }
    }
    return std::string("V") + QUARK_VERSION;
}

}

// A saved run: "# key,value" lines (the numbers, the setup as "# setup:NAME,value"), then "frame_time_us" and a frame
// time a line in whole microseconds. `frames`: read them too (the result page), else just the header (the list).
bool Game::benchLoad(const std::string& path, BenchFile& f, bool frames) {
    std::ifstream in(std::filesystem::u8path(path));
    if (!in) return false;
    f = BenchFile();
    f.path = path;
    std::string line;
    bool magic = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "frame_time_us") break;
        if (line.rfind("# quark benchmark", 0) == 0) { magic = true; continue; }
        if (line.size() < 2 || line[0] != '#') continue;
        const size_t c = line.find(',');
        if (c == std::string::npos) continue;
        const std::string k = line.substr(2, c - 2), v = line.substr(c + 1);
        const double d = atof(v.c_str());
        if (k == "name") f.name = v;
        else if (k == "label") f.label = v;
        else if (k == "date") f.when = v;
        else if (k == "map") f.map = v;
        else if (k == "uploaded") f.uploaded = (int)d;
        else if (k == "demo") f.demo = v;
        else if (k == "frames") f.frames = (size_t)d;
        else if (k == "seconds") f.seconds = d;
        else if (k == "avg_fps") f.avgFps = d;
        else if (k == "low_1pct_fps") f.low1 = d;
        else if (k == "low_0.1pct_fps") f.low01 = d;
        else if (k == "min_fps") f.minFps = d;
        else if (k == "avg_ms") f.avgMs = d;
        else if (k == "stdev_ms") f.stdevMs = d;
        else if (k == "worst_ms") f.worstMs = d;
        else if (k.rfind("setup:", 0) == 0) f.setup.push_back({k.substr(6), v});
    }
    if (!magic) return false;
    if (f.name.empty()) f.name = stemOf(path);
    if (frames) {
        f.times.reserve(f.frames);
        while (std::getline(in, line)) if (!line.empty()) f.times.push_back((float)(atof(line.c_str()) * 1e-6));
    }
    return true;
}

// The demo a run plays: --benchmark's, else the shipped one (maps/benchmark/hamlet.qdemo). One recorded with an older
// protocol is converted to this one (demo::convert, as the demos folder's are) into benchmarks/cache/ once.
std::string Game::benchDemoPath() const {
    const std::string p = !opts_.benchmarkDemo.empty() ? opts_.benchmarkDemo : findDataPath(bench::kDemo);
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(p), ec)) return "";
    uint16_t proto = 0;
    if (!demo::readProtocol(p, &proto) || proto == kProtocolVersion || !demo::convertible(proto)) return p;
    const std::string out = userDataPath("benchmarks/cache") + "/" + stemOf(p) + "-p" + std::to_string(kProtocolVersion) + ".qdemo";
    if (std::filesystem::exists(std::filesystem::u8path(out), ec)) return out;
    std::ifstream in(std::filesystem::u8path(p), std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), conv, err;
    if (!demo::convert(data, &conv, &err)) return p;   // (playback then says why it won't play)
    std::filesystem::create_directories(std::filesystem::u8path(userDataPath("benchmarks/cache")), ec);
    if (FILE* f = u8fopen(out, "wb")) { fwrite(conv.data(), 1, conv.size(), f); fclose(f); return out; }
    return p;
}

// Play the demo; the frame loop takes it from there (benchFrameTime). The frame-time buffer is allocated (and its pages
// touched) here, before anything is measured, so recording a frame is one store.
void Game::benchStart() {
    const std::string demoPath = benchDemoPath();
    if (demoPath.empty()) {
        error_ = "BENCHMARK DEMO MISSING";
        if (opts_.benchmark) { printf("BENCHMARK: no demo (%s)\n", opts_.benchmarkDemo.empty() ? bench::kDemo : opts_.benchmarkDemo.c_str()); quit_ = true; }
        return;
    }
    if (bench_.times.size() < bench::kMaxFrames) bench_.times.assign(bench::kMaxFrames, 0.0f);
    if (!playDemo(demoPath)) {   // (leaves any game first)
        if (opts_.benchmark) { printf("BENCHMARK: %s\n", error_.c_str()); quit_ = true; }
        return;
    }
    bench_.demo = demoPath;
    bench_.n = 0;
    bench_.seconds = opts_.benchmarkSeconds;
    bench_.startedAt = bench_.measureAt = 0;
    bench_.phase = BenchRun::STARTING;
}

void Game::benchFrameTime(double dt) {
    switch (bench_.phase) {
        case BenchRun::OFF: return;
        case BenchRun::STARTING:   // the demo's world drawn: the warm-up
            if (screen_ != Screen::InGame || !net_.hasSnapshot() || !r_.worldLoaded() || bake_) return;
            bench_.phase = BenchRun::WARMUP;
            bench_.startedAt = time_;
            return;
        case BenchRun::WARMUP:
            if (time_ - bench_.startedAt >= bench::kWarmup) { bench_.phase = BenchRun::MEASURING; bench_.measureAt = time_; bench_.n = 0; }
            return;
        case BenchRun::MEASURING:
            bench_.times[bench_.n++] = (float)dt;
            if (net_.playbackDone() || (bench_.seconds > 0 && time_ - bench_.measureAt >= bench_.seconds) || bench_.n >= bench_.times.size()) benchFinish();
            return;
    }
}

void Game::benchCancel() {
    bench_.phase = BenchRun::OFF;
    leaveGame();
}

// The run is over: out of the match, the run saved, then its result page (--benchmark: printed, then quit)
void Game::benchFinish() {
    bench_.phase = BenchRun::OFF;
    const std::string path = benchSave(bench_.times.data(), bench_.n);   // (in the match still: the setup's 3D size is its)
    leaveGame();
    if (!path.empty()) { BenchFile f; if (benchLoad(path, f, false)) benchUpload(f); }   // (after the run: off the measured frames)
    if (path.empty() || !benchOpen(path)) {
        error_ = "BENCHMARK: COULD NOT SAVE THE RUN IN " + upperStr(benchDir());
        if (opts_.benchmark) { printf("BENCHMARK: could not save the run in %s\n", benchDir().c_str()); quit_ = true; }
        return;
    }
    if (opts_.benchmark) {
        const BenchFile& f = benchRes_.run;
        printf("BENCHMARK %s: demo %s: %zu frames in %.1f s  avg %.1f fps  1%% low %.1f  0.1%% low %.1f  min %.1f fps (worst %.2f ms)  stdev %.3f ms  avg %.3f ms\n",
               f.name.c_str(), f.demo.c_str(), f.frames, f.seconds, f.avgFps, f.low1, f.low01, f.minFps, f.worstMs, f.stdevMs, f.avgMs);
        for (auto& kv : f.setup) printf("  %s: %s\n", kv.first.c_str(), kv.second.c_str());
        const BenchFile& p = benchRes_.prev;
        if (benchRes_.havePrev) printf("  previous run (%s): avg %.1f  1%% low %.1f  0.1%% low %.1f  min %.1f  stdev %.3f ms\n", p.name.c_str(), p.avgFps, p.low1, p.low01, p.minFps, p.stdevMs);
        printf("  saved %s\n", path.c_str());
        fflush(stdout);
        quit_ = true;
    }
}

std::string Game::benchSave(const float* t, size_t n) {
    BenchFile f;
    std::vector<float> scratch;
    const bench::Stats st = bench::compute(t, n, scratch);
    f.frames = st.frames; f.seconds = st.seconds;
    f.avgFps = st.avgFps; f.low1 = st.low1; f.low01 = st.low01; f.minFps = st.minFps;
    f.stdevMs = st.stdevMs; f.avgMs = st.avgMs; f.worstMs = st.worstMs;
    f.times.assign(t, t + n);
    // the setup: what decides the numbers
    static const char* disp[] = {"WINDOWED", "BORDERLESS FULLSCREEN", "EXCLUSIVE FULLSCREEN"};
    static const char* pres[] = {"OFF", "ON", "ADAPTIVE", "LATENT SYNC"};
    static const char* ll[] = {"OFF", "ON", "ULTRA"};
    const std::string aa = cfg_.msaa == 0 ? "OFF" : cfg_.msaa == 1 ? "SMOOTH EDGES" : std::to_string(cfg_.msaa) + "X MSAA";
    auto clean = [](std::string v) { std::string o; for (char c : v) if (c >= 32 && c < 127) o += c; return upperStr(o).substr(0, 64); };
    f.setup.push_back({"RESOLUTION", std::to_string(r_.width()) + " X " + std::to_string(r_.height())});
    f.setup.push_back({"RENDER SCALE", std::to_string(cfg_.renderScale) + "% (3D " + std::to_string(r_.sceneWidth()) + " X " + std::to_string(r_.sceneHeight()) + ")"});
    f.setup.push_back({"DISPLAY", disp[std::min(std::max(cfg_.displayMode, 0), 2)]});
    f.setup.push_back({"VSYNC / LOW LATENCY", std::string(pres[std::min(std::max(cfg_.presentMode, 0), 3)]) + " / " + ll[std::min(std::max(cfg_.lowLatency, 0), 2)]});
    f.setup.push_back({"FPS LIMIT", cfg_.maxFps > 0 ? std::to_string(cfg_.maxFps) : "UNLIMITED"});
    f.setup.push_back({"AA / SHADOWS", aa + " / " + (cfg_.shadows ? std::to_string(cfg_.shadowRes) : std::string("OFF"))});
    f.setup.push_back({"CPU", clean(cpuBrand())});
    f.setup.push_back({"GPU", clean(glString(GL_RENDERER))});
    f.setup.push_back({"DRIVER", clean(glString(GL_VERSION)).substr(0, 36)});
    f.setup.push_back({"VERSION", std::string(QUARK_VERSION) + " " + isa::buildName()});
    int players = 0;
    if (net_.hasSnapshot()) for (auto& p : net_.latest().players) if ((p.flags & PS_CONNECTED) && p.team() != TEAM_SPECTATOR) players++;
    f.map = net_.mapReady() ? net_.map().name : "";
    f.demo = stemOf(bench_.demo);
    f.setup.push_back({"DEMO", upperStr(f.demo + (f.map.empty() ? "" : " (" + f.map + ", " + std::to_string(players) + " PLAYING)"))});
    // the file: named by the date (they sort by it), the run named "<label> HAMLET <date>" (renamed on its page)
    char stamp[40], when[40], shortWhen[40];
    const time_t now = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &lt);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &lt);
    strftime(shortWhen, sizeof shortWhen, "%Y-%m-%d %H:%M", &lt);
    f.when = when;
    f.label = buildLabel();
    f.name = upperStr(f.label + " " + (f.map.empty() ? std::string("BENCHMARK") : f.map) + " " + shortWhen);
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::u8path(benchDir()), ec);
    f.path = benchDir() + "/" + stamp + ".qbench";
    for (int k = 2; fs::exists(fs::u8path(f.path), ec); k++) f.path = benchDir() + "/" + stamp + "_" + std::to_string(k) + ".qbench";   // (two in one second)
    return benchWrite(f) ? f.path : "";
}

bool Game::benchWrite(const BenchFile& f) {
    const std::string tmp = f.path + ".tmp";   // (written whole, then moved over: a rename never leaves half a file)
    FILE* o = u8fopen(tmp, "wb");
    if (!o) return false;
    fprintf(o, "# quark benchmark 1\n# name,%s\n# label,%s\n# date,%s\n# version,%s\n# map,%s\n# demo,%s\n# uploaded,%d\n",
            f.name.c_str(), f.label.c_str(), f.when.c_str(), QUARK_VERSION, f.map.c_str(), f.demo.c_str(), f.uploaded);
    fprintf(o, "# frames,%zu\n# seconds,%.3f\n# avg_fps,%.2f\n# low_1pct_fps,%.2f\n# low_0.1pct_fps,%.2f\n# min_fps,%.2f\n# avg_ms,%.4f\n# stdev_ms,%.4f\n# worst_ms,%.4f\n",
            f.frames, f.seconds, f.avgFps, f.low1, f.low01, f.minFps, f.avgMs, f.stdevMs, f.worstMs);
    for (auto& kv : f.setup) fprintf(o, "# setup:%s,%s\n", kv.first.c_str(), kv.second.c_str());
    fprintf(o, "frame_time_us\n");
    for (float t : f.times) fprintf(o, "%lld\n", (long long)std::llround(t * 1e6));
    const bool ok = fclose(o) == 0;
    std::error_code ec;
    if (ok) std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(f.path), ec);
    if (!ok || ec) { std::filesystem::remove(std::filesystem::u8path(tmp), ec); return false; }
    return true;
}

// The result page for a saved run: its numbers as saved, the graph from its frames, and the run saved before it
bool Game::benchOpen(const std::string& path) {
    BenchResult& R = benchRes_;
    if (!benchLoad(path, R.run, true)) return false;
    std::vector<float> scratch;
    bench::Stats st;
    st.p999Ms = 0;
    if (!R.run.times.empty()) {
        st = bench::compute(R.run.times.data(), R.run.times.size(), scratch);
        bench::graphColumns(R.run.times.data(), R.run.times.size(), kGraphCols, R.colMax, R.colMean);
    } else { R.colMax.clear(); R.colMean.clear(); }
    R.topMs = bench::graphTopMs(st);
    R.nameDirty = false;
    R.havePrev = false;
    benchDemoFound_ = benchDemoPath();   // (RUN AGAIN: only with the demo there)
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string me = fs::u8path(path).filename().u8string();
    std::string prev;
    for (auto& e : fs::directory_iterator(fs::u8path(benchDir()), ec)) {
        const std::string n = e.path().filename().u8string();
        if (e.path().extension() == ".qbench" && n < me && n > prev) prev = n;
    }
    if (!prev.empty()) R.havePrev = benchLoad(benchDir() + "/" + prev, R.prev, false);
    if (!opts_.benchmark) setScreen(Screen::BenchmarkResult);
    return true;
}

void Game::benchDelete() {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(benchRes_.run.path), ec);
    notify(ec ? "COULD NOT DELETE " + upperStr(benchRes_.run.name) : "DELETED " + upperStr(benchRes_.run.name));
    benchListAt_ = 0;
    setScreen(Screen::Benchmarks);
}

// EXPORT CSV: benchmarks/exports/<run>.csv, a header line then every frame's time in whole microseconds
void Game::benchExportCsv() {
    const BenchFile& f = benchRes_.run;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(exportDir()), ec);
    const std::string path = exportDir() + "/" + stemOf(f.path) + ".csv";
    FILE* o = u8fopen(path, "wb");
    if (!o) { notify("COULD NOT WRITE " + upperStr(path), 6); return; }
    auto plain = [](std::string v) { for (auto& c : v) if (c == ',' || c == '"') c = ' '; return v; };
    fprintf(o, "frame_time_us (label %s; run %s; version %s; demo %s)\n", plain(f.label).c_str(), plain(f.name).c_str(), QUARK_VERSION, plain(f.demo).c_str());
    for (float t : f.times) fprintf(o, "%lld\n", (long long)std::llround(t * 1e6));
    fclose(o);
    notify("SAVED " + upperStr(path), 6);
}

// In the run: BENCHMARK and the time left, top centre, small (the player's HUD is off: it's a camera, not a player)
void Game::drawBenchOverlay(float W, float H, float s) {
    ui_.begin(&r_, uiIn_, s);
    std::string state;
    if (bench_.phase == BenchRun::MEASURING) {
        double l = net_.playbackLeft();
        if (bench_.seconds > 0) l = std::min(l, bench_.seconds - (time_ - bench_.measureAt));
        const int left = (int)std::ceil(std::max(0.0, l));
        char b[32];
        snprintf(b, sizeof b, "%d:%02d LEFT", left / 60, left % 60);
        state = b;
    } else state = "WARMING UP";
    const float cx = W * 0.5f, bw = 300 * s;
    r_.rect(cx - bw * 0.5f, 12 * s, bw, 58 * s, {0.03f, 0.035f, 0.05f, 0.7f});
    ui_.labelCentered(cx, 18 * s, "BENCHMARK", kWhite, 1.0f, bw - 16 * s);
    ui_.hintCentered(cx, 40 * s, state, ui_.accent, bw - 16 * s);
    ui_.hintCentered(cx, 54 * s, "ESC TO CANCEL", kGrey, bw - 16 * s);
    (void)H;
}

// BENCHMARKS: RUN BENCHMARK, then the saved runs (newest first; each opens its result page), BACK / OPEN FOLDER
void Game::drawBenchList(const SetLayout& L) {
    if (time_ >= benchListAt_) {   // (rescanned now and then, headers only: a run saved elsewhere shows up)
        benchListAt_ = time_ + 2;
        benchDemoFound_ = benchDemoPath();
        benchList_.clear();
        std::error_code ec;
        std::vector<std::string> paths;
        for (auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(benchDir()), ec))
            if (e.path().extension() == ".qbench") paths.push_back(e.path().u8string());
        std::sort(paths.begin(), paths.end(), std::greater<std::string>());
        for (auto& p : paths) { BenchFile f; if (benchLoad(p, f, false)) benchList_.push_back(std::move(f)); }
    }
    const int kRows = 8, nb = (int)benchList_.size();
    benchScroll_ = std::min(benchScroll_, std::max(0, nb - kRows));
    const int rows = std::min(nb - benchScroll_, kRows);
    static const std::string about = "PLAYS THE BENCHMARK DEMO (HAMLET, FREE FOR ALL, FULL OF BOTS) AND MEASURES EVERY FRAME";
    const float lx = L.X(360), lw = L.L(560);
    const float listH = std::max(rows, 1) * 36.0f, scrollH = nb > rows ? 28 : 12;
    const float kH = 24 + 16 + 40 + 22 + 30 + listH + scrollH + 40;
    float y = bandTop(kH);
    ui_.panel(L.X(344), L.Y(y - 16), L.L(592), L.L(kH + 32));
    ui_.labelCentered(L.X(640), L.Y(y), "BENCHMARKS", kWhite, 1.5f, lw);
    y += 40;
    const bool have = !benchDemoFound_.empty();
    if (ui_.button(lx, L.Y(y), lw, L.L(kMRow), have ? "RUN BENCHMARK" : "BENCHMARK DEMO MISSING", have ? UiExp::state("demo", "1") : UiExp::inert(), have)) { benchStart(); return; }
    y += 46;
    ui_.hintCentered(L.X(640), L.Y(y), about, kGrey, lw);
    y += 30;
    if (benchList_.empty()) ui_.labelCentered(L.X(640), L.Y(y + 8), "NO RUNS SAVED YET", kGrey, 1.0f, lw);
    for (int k = 0; k < rows; k++) {
        const BenchFile& f = benchList_[benchScroll_ + k];
        char b[64];
        snprintf(b, sizeof b, "  -  %.0f AVG  %.0f 1%%%s", f.avgFps, f.low1, f.uploaded == 1 ? "  - UPLOADED" : "");
        if (ui_.button(lx, L.Y(y + 36 * k), lw, L.L(32), upperStr(f.name) + b, UiExp::nav("benchmarks/result").withId("RUN").optional().clipOk())) { benchOpen(f.path); return; }
    }
    y += listH;
    if (nb > rows) {
        char more[64];
        snprintf(more, sizeof more, "%d-%d OF %d - WHEEL TO SCROLL", benchScroll_ + 1, benchScroll_ + rows, nb);
        ui_.hintCentered(L.X(640), L.Y(y), more, kGrey, lw);
    }
    y += scrollH;
    if (ui_.button(lx, L.Y(y), L.L(276), L.L(kMRow), "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
    if (ui_.button(L.X(644), L.Y(y), L.L(276), L.L(kMRow), "OPEN FOLDER", UiExp::url())) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(benchDir()), ec);
        openUrl(std::filesystem::absolute(std::filesystem::u8path(benchDir()), ec).lexically_normal().u8string());
    }
}

// The result page: the frame-time graph over the whole run (each column the mean frame of its share, its slowest frame
// a tick above), lines at 60 / 144 / 240 fps; the numbers beside the previous run's; the setup; the run's name;
// EXPORT CSV / PNG; BACK, DELETE, OPEN FOLDER (the exports), RUN AGAIN.
void Game::drawBenchResults(const SetLayout& L) {
    BenchResult& R = benchRes_;
    const BenchFile& f = R.run;
    ui_.panel(L.X(160), L.Y(166), L.L(960), L.L(522), 0.85f);
    ui_.labelCentered(L.X(640), L.Y(178), "BENCHMARK RESULTS", kWhite, 1.5f, L.L(928));
    const float hs = ui_.hintScale();
    // the graph: x 176..1104, y 206..370 at 720p
    const float gx = 176, gw = 928, gy = 206, gh = 164;
    r_.rect(L.X(gx), L.Y(gy), L.L(gw), L.L(gh), {0, 0, 0, 0.35f});
    const float top = R.topMs;
    auto yOf = [&](float ms) { return gy + gh * (1 - std::min(ms, top) / top); };
    const size_t cols = R.colMean.size();
    const float cw = cols ? gw / cols : gw;
    for (size_t c = 0; c < cols; c++) {
        const float x = gx + c * cw, ym = yOf(R.colMean[c]), yx = yOf(R.colMax[c]);
        r_.rect(L.X(x), L.Y(ym), std::max(1.0f, L.L(cw)), L.L(gy + gh - ym), {ui_.accent.r, ui_.accent.g, ui_.accent.b, 0.55f});
        if (R.colMax[c] > top) r_.rect(L.X(x), L.Y(gy), std::max(1.0f, L.L(cw)), L.L(3), kRed);   // (off the top)
        else if (yx < ym - 0.5f) r_.rect(L.X(x), L.Y(yx), std::max(1.0f, L.L(cw)), std::max(1.0f, L.L(1.5f)), {1.0f, 0.8f, 0.3f, 0.9f});
    }
    static const struct { int fps; const char* name; } refs[] = {{60, "60 FPS"}, {144, "144 FPS"}, {240, "240 FPS"}};
    float lastLabel = -100;
    for (auto& rf : refs) {
        const float ms = 1000.0f / rf.fps;
        if (ms > top) continue;
        const float y = yOf(ms);
        r_.rect(L.X(gx), L.Y(y), L.L(gw), std::max(1.0f, L.L(1)), {1, 1, 1, 0.35f});
        const float ly = y - lastLabel < 28 ? y + 3 : y - 14;   // (a line close under the last: its label under it)
        ui_.hint(L.X(gx + gw) - r_.textWidth(rf.name, hs) - L.L(4), L.Y(ly), rf.name, kWhite, L.L(80));
        lastLabel = y;
    }
    ui_.hint(L.X(gx + 4), L.Y(gy + 2), fmt("%.0f MS", top), kGrey, L.L(80));
    ui_.hint(L.X(gx + 4), L.Y(gy + gh - 14), "FRAME TIME (MS) OVER THE RUN", kGrey, L.L(300));
    if (cols == 0) ui_.labelCentered(L.X(640), L.Y(gy + gh * 0.5f - 8), "NO FRAMES MEASURED", kRed, 1.0f, L.L(600));
    // the numbers (x 176..576) and the previous run's (right-aligned at 676); the setup (x 704..1104)
    const float rowH = 18;
    float y = 380;
    ui_.hint(L.X(176), L.Y(y), "THIS RUN", kGrey, L.L(200));
    if (R.havePrev) { const std::string p = "PREVIOUS"; ui_.hint(L.X(676) - r_.textWidth(p, hs), L.Y(y), p, kGrey, L.L(100)); }
    y += rowH;
    auto row = [&](const std::string& a, const std::string& v, const std::string& pv, Color c) {
        const float bw = r_.textWidth(v, hs);
        ui_.hint(L.X(176), L.Y(y), a, kGrey, L.L(400) - bw - L.L(12));
        ui_.hint(L.X(576) - bw, L.Y(y), v, c, bw + 1);
        if (R.havePrev && !pv.empty()) { const float pw = r_.textWidth(pv, hs); ui_.hint(L.X(676) - pw, L.Y(y), pv, kGrey, pw + 1); }
        y += rowH;
    };
    const BenchFile& p = R.prev;
    row("AVG FPS", fmt("%.1f", f.avgFps), fmt("%.1f", p.avgFps), {1.0f, 0.8f, 0.3f, 1});
    row("1% LOW", fmt("%.1f", f.low1), fmt("%.1f", p.low1), kWhite);
    row("0.1% LOW", fmt("%.1f", f.low01), fmt("%.1f", p.low01), kWhite);
    row("MIN FPS (WORST FRAME)", fmt("%.1f", f.minFps) + fmt(" (%.2f MS)", f.worstMs), fmt("%.1f", p.minFps), kWhite);
    row("STDEV", fmt("%.3f MS", f.stdevMs), fmt("%.3f MS", p.stdevMs), kWhite);
    row("AVG FRAME", fmt("%.3f MS", f.avgMs), fmt("%.3f MS", p.avgMs), kWhite);
    row("FRAMES", std::to_string(f.frames) + fmt(" IN %.1f S", f.seconds), std::to_string(p.frames), kWhite);
    if (R.havePrev) ui_.hint(L.X(176), L.Y(y), "PREVIOUS: " + upperStr(p.name), kGrey, L.L(500));
    y = 380;
    for (auto& kv : f.setup) {
        ui_.hint(L.X(704), L.Y(y), kv.first, kGrey, L.L(150));
        ui_.hint(L.X(856), L.Y(y), kv.second, kWhite, L.L(248));
        y += rowH;
    }
    // the name (saved when the field is left), EXPORT CSV / PNG
    const float ny = L.Y(578), nh = L.L(36);
    ui_.hint(L.X(176), ny + (nh - r_.textHeight(hs)) * 0.5f, "NAME", kGrey, L.L(56));
    if (ui_.textField(61, L.X(236), ny, L.L(492), nh, R.run.name, 48, UiExp::state("bench").withId("RUN NAME"))) R.nameDirty = true;
    if (R.nameDirty && !ui_.typing()) {
        R.nameDirty = false;
        if (R.run.name.empty()) R.run.name = stemOf(R.run.path);
        if (!benchWrite(R.run)) notify("COULD NOT RENAME THE RUN", 5);
        benchListAt_ = 0;
    }
    if (ui_.button(L.X(744), ny, L.L(176), nh, "EXPORT CSV", UiExp::state("bench"))) benchExportCsv();
    if (ui_.button(L.X(928), ny, L.L(176), nh, "EXPORT PNG", UiExp::state("bench"))) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(exportDir()), ec);
        benchShot_ = exportDir() + "/" + stemOf(f.path) + ".png";   // (taken once this frame is drawn: Game::frame)
    }
    const float fy = L.Y(632), fw = L.L(176), fh = L.L(40);
    if (ui_.button(L.X(176), fy, fw, fh, "BACK", UiExp::nav("benchmarks"))) { benchListAt_ = 0; setScreen(Screen::Benchmarks); }
    if (ui_.button(L.X(368), fy, fw, fh, "DELETE", UiExp::state("bench"))) { benchDelete(); return; }
    if (ui_.button(L.X(744), fy, fw, fh, "OPEN FOLDER", UiExp::url())) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(exportDir()), ec);
        openUrl(std::filesystem::absolute(std::filesystem::u8path(exportDir()), ec).lexically_normal().u8string());
    }
    if (ui_.button(L.X(928), fy, fw, fh, "RUN AGAIN", benchDemoFound_.empty() ? UiExp::inert() : UiExp::state("demo", "1"), !benchDemoFound_.empty())) benchStart();
}

// ---- uploads: each finished run's numbers to the ranking server (tools/master/worker.js POST /bench), on RankService's
// thread like every other request to it. No ranked key: nothing is sent. Not answered "ok" (no network, or a Worker
// without /bench yet: testing builds come out before the Worker that takes them is deployed with a production
// release): the run file keeps "uploaded,0" and it's sent again at the next start or when BENCHMARKS opens.
void Game::benchUpload(const BenchFile& f) {
    if (rankKey_.secret.empty() || f.uploaded != 0 || f.frames == 0 || benchSending_.count(f.path)) return;
    auto setup = [&](const char* k) { for (auto& kv : f.setup) if (kv.first == k) return kv.second; return std::string(); };
    auto num = [](double v) { char b[32]; snprintf(b, sizeof b, "%.3f", v); return std::string(b); };
    const std::string version = std::string(QUARK_VERSION);
    std::string body = "secret=" + rankKey_.secret + "&name=" + toHex(cfg_.name) + "&version=" + toHex(version) +
                       "&channel=" + (version.find(".testing.") != std::string::npos ? "testing" : "production") + "&build=" + toHex(buildKind()) +
                       "&cpu=" + toHex(setup("CPU")) + "&gpu=" + toHex(setup("GPU")) + "&driver=" + toHex(setup("DRIVER")) +
                       "&avg=" + num(f.avgFps) + "&low1=" + num(f.low1) + "&low01=" + num(f.low01) + "&min=" + num(f.minFps) +
                       "&stdev=" + num(f.stdevMs) + "&frames=" + std::to_string(f.frames) + "&seconds=" + num(f.seconds) +
                       "&res=" + toHex(setup("RESOLUTION")) + "&scale=" + toHex(setup("RENDER SCALE")) + "&aa=" + toHex(setup("AA / SHADOWS")) +
                       "&vsync=" + toHex(setup("VSYNC / LOW LATENCY")) + "&fpscap=" + toHex(setup("FPS LIMIT")) + "&demo=" + toHex(f.demo);
    benchSending_.insert(f.path);
    rank_.request("bench:" + f.path, "/bench", body);
}

void Game::benchUploads() {
    for (auto it = benchSending_.begin(); it != benchSending_.end();) {
        std::string out;
        if (!rank_.takeText("bench:" + *it, out)) { ++it; continue; }
        while (!out.empty() && (unsigned char)out.back() <= ' ') out.pop_back();
        // "!...": not reached (retried later); "ok": stored; "rate limited" / "daily limit": later; anything else: refused
        const int state = out == "ok" ? 1 : out.empty() || out[0] == '!' || out == "rate limited" || out == "daily limit" ? 0 : 2;
        if (state != 0) {
            BenchFile f;
            if (benchLoad(*it, f, true)) {
                f.uploaded = state;
                benchWrite(f);
                if (benchRes_.run.path == *it) benchRes_.run.uploaded = state;
                benchListAt_ = 0;
            }
        }
        it = benchSending_.erase(it);
    }
}

void Game::benchRetryUploads() {
    if (rankKey_.secret.empty()) return;
    std::error_code ec;
    int n = 0;
    for (auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(benchDir()), ec)) {
        if (e.path().extension() != ".qbench") continue;
        BenchFile f;
        if (benchLoad(e.path().u8string(), f, false) && f.uploaded == 0 && ++n <= 10) benchUpload(f);   // (10 at a time)
    }
}
