// Thread-local random numbers for game, server and client code (rand() shares one global state between the
// hosted-server thread and the client thread: a data race). A per-thread xorshift32: a few cycles, no locks.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

namespace qrng_detail {
inline uint32_t& state() {
    thread_local uint32_t s = [] {
        uint64_t t = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        uint64_t h = std::hash<std::thread::id>()(std::this_thread::get_id());
        uint32_t v = (uint32_t)(t ^ (t >> 32) ^ h ^ (h >> 32)) | 1u;
        return v;
    }();
    return s;
}
} // namespace qrng_detail

// 0 .. 2^32-1 (never 0 in practice; use % n for a range)
inline uint32_t qrand() {
    uint32_t& x = qrng_detail::state();
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}
// Seed this thread's generator (tests that want a repeatable sequence).
inline void qsrand(uint32_t seed) { qrng_detail::state() = seed ? seed : 0x9E3779B9u; }
