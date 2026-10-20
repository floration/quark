// Profiling zones. Two optional backends, both compiled out by default:
//   -DQUARK_TRACY=ON      Tracy profiler zones (connect the Tracy profiler / tracy-capture)
//   -DQUARK_ZONESTATS=ON  per-thread totals per zone, printed by qzoneDump() (used by `quark --bench`)
#pragma once
#ifdef QUARK_TRACY
#include <tracy/Tracy.hpp>
#define QZONE(name) ZoneScopedN(name)
#define QFRAME() FrameMark
#define QPLOT(name, v) TracyPlot(name, (int64_t)(v))
inline void qzoneDump() {}
#elif defined(QUARK_ZONESTATS)
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
struct QZoneStat { const char* name; double total = 0, worst = 0; long count = 0; };
inline thread_local std::vector<QZoneStat*> g_qzones;
struct QZoneReg { QZoneStat s; explicit QZoneReg(const char* n) { s.name = n; g_qzones.push_back(&s); } };
struct QZoneScope {
    QZoneStat& s; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    explicit QZoneScope(QZoneStat& st) : s(st) {}
    ~QZoneScope() { double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); s.total += d; s.count++; if (d > s.worst) s.worst = d; }
};
#define QZ_CAT2(a, b) a##b
#define QZ_CAT(a, b) QZ_CAT2(a, b)
#define QZONE(name) static thread_local QZoneReg QZ_CAT(qzr_, __LINE__)(name); QZoneScope QZ_CAT(qzs_, __LINE__)(QZ_CAT(qzr_, __LINE__).s)
#define QFRAME()
#define QPLOT(name, v)
inline void qzoneDump() {
    std::vector<QZoneStat*> z = g_qzones;
    std::sort(z.begin(), z.end(), [](auto* a, auto* b) { return a->total > b->total; });
    for (auto* s : z) printf("  zone %-36s %9.3f ms total  %8ld calls  %7.3f us avg  %8.3f ms worst\n", s->name, s->total * 1000, s->count, s->count ? s->total / s->count * 1e6 : 0, s->worst * 1000);
}
#else
#define QZONE(name)
#define QFRAME()
#define QPLOT(name, v)
inline void qzoneDump() {}
#endif
