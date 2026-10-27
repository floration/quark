// The walk grid built on every core (walkfast.cpp: a threaded copy of tests/walkgrid.h's WalkGrid::build).
#pragma once
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include "json.h"
#include "walkgrid.h"
int maptoolThreads();                                          // worker threads (the machine's cores, 1..16)
// f(i) for i in [0, n), spread over the threads in chunks (dynamic: rows differ a lot in cost)
template <class F> inline void parallelFor(int n, F f, int chunk = 4) {
    int T = std::min(maptoolThreads(), std::max(1, n / chunk));
    if (T <= 1) { for (int i = 0; i < n; i++) f(i); return; }
    std::atomic<int> next{0};
    auto work = [&] { for (;;) { int a = next.fetch_add(chunk); if (a >= n) break; for (int i = a; i < std::min(n, a + chunk); i++) f(i); } };
    std::vector<std::thread> th;
    for (int t = 1; t < T; t++) th.emplace_back(work);
    work();
    for (auto& t : th) t.join();
}

// the same with the worker's index (0 .. maptoolThreads() - 1) for per-worker scratch: f(i, worker)
template <class F> inline void parallelForWorker(int n, F f, int chunk = 4) {
    int T = std::min(maptoolThreads(), std::max(1, n / chunk));
    if (T <= 1) { for (int i = 0; i < n; i++) f(i, 0); return; }
    std::atomic<int> next{0};
    auto work = [&](int wk) { for (;;) { int a = next.fetch_add(chunk); if (a >= n) break; for (int i = a; i < std::min(n, a + chunk); i++) f(i, wk); } };
    std::vector<std::thread> th;
    for (int t = 1; t < T; t++) th.emplace_back(work, t);
    work(0);
    for (auto& t : th) t.join();
}

void buildWalkGrid(WalkGrid& w, const Map& m, Json* laps = nullptr);   // == w.build(m), spot for spot and link for link
