// A file written on its own thread (PERF-11): the caller appends bytes to a memory buffer and hands them over with
// flush(); a writer thread does the fwrite / fflush, so a slow or busy disk never stalls the game thread (demo
// recording wrote and flushed every recorded snapshot, 62.5 times a second, from the frame loop).
#pragma once
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "u8file.h"

class AsyncFileWriter {
public:
    ~AsyncFileWriter() { close(); }
    bool open(const std::string& path) {   // (UTF-8 path) truncates / creates it
        close();
        f_ = u8fopen(path, "wb");
        if (!f_) return false;
        quit_ = false;
        th_ = std::thread([this] { run(); });
        return true;
    }
    bool isOpen() const { return f_ != nullptr; }
    void write(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; pending_.insert(pending_.end(), b, b + n); }
    void flush() {   // hand what's been written so far to the writer thread (it writes and flushes it to the file)
        if (pending_.empty() || !f_) return;
        { std::lock_guard<std::mutex> l(mu_); queue_.insert(queue_.end(), pending_.begin(), pending_.end()); }
        pending_.clear();
        cv_.notify_one();
    }
    void close() {   // everything written so far reaches the file, then it's closed
        if (!f_) return;
        flush();
        { std::lock_guard<std::mutex> l(mu_); quit_ = true; }
        cv_.notify_one();
        if (th_.joinable()) th_.join();
        fclose(f_);
        f_ = nullptr;
    }
private:
    void run() {
        std::vector<uint8_t> out;
        for (;;) {
            bool quit;
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [&] { return quit_ || !queue_.empty(); });
                out.swap(queue_);
                quit = quit_;
            }
            if (!out.empty()) { fwrite(out.data(), 1, out.size(), f_); fflush(f_); out.clear(); }
            if (quit) {
                std::lock_guard<std::mutex> l(mu_);
                if (queue_.empty()) return;
            }
        }
    }
    FILE* f_ = nullptr;
    std::thread th_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<uint8_t> pending_, queue_;   // pending_: the caller's (no lock); queue_: handed over, under mu_
    bool quit_ = false;
};
