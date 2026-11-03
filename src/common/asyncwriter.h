// A file written on its own thread (PERF-11): the caller appends bytes to a memory buffer and hands them over with
// flush(); a writer thread does the fwrite / fflush, so a slow or busy disk never stalls the game thread (demo
// recording wrote and flushed every recorded snapshot, 62.5 times a second, from the frame loop).
#pragma once
#include <condition_variable>
#include <cstdarg>
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
    // (UTF-8 path) truncates / creates it. mode "w": a text file (Windows writes each \n as \r\n, as fprintf to a
    // "w" FILE did: the server's game log keeps its bytes)
    bool open(const std::string& path, const char* mode = "wb") {
        close();
        f_ = u8fopen(path, mode);
        if (!f_) return false;
        quit_ = false;
        th_ = std::thread([this] { run(); });
        return true;
    }
    bool isOpen() const { return f_ != nullptr; }
    void write(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; pending_.insert(pending_.end(), b, b + n); }
    // printf-style, formatted by vsnprintf into a reused buffer (no allocation per call once the buffers have grown):
    // the same bytes fprintf would write
    void vformat(const char* fmt, va_list ap) {
        if (fmt_.empty()) fmt_.resize(1024);
        va_list again;
        va_copy(again, ap);
        int n = vsnprintf(fmt_.data(), fmt_.size(), fmt, ap);
        if (n >= 0 && (size_t)n >= fmt_.size()) { fmt_.resize((size_t)n + 1); n = vsnprintf(fmt_.data(), fmt_.size(), fmt, again); }
        va_end(again);
        if (n > 0) write(fmt_.data(), (size_t)n);
    }
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    void format(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vformat(fmt, ap); va_end(ap); }
    size_t pendingBytes() const { return pending_.size(); }   // written since the last flush
    void flush() {   // hand what's been written so far to the writer thread (it writes and flushes it to the file)
        if (pending_.empty() || !f_) return;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (queue_.empty()) queue_.swap(pending_);   // (the usual case: no copy, and pending_ gets the writer's spent buffer back)
            else queue_.insert(queue_.end(), pending_.begin(), pending_.end());
        }
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
    std::vector<char> fmt_;                  // the caller's: vformat's line
    bool quit_ = false;
};
