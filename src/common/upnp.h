// Minimal UPnP IGD client: asks the router to forward a UDP port to this machine.
// Runs on a background thread; never blocks the game.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class PortMapper {
public:
    ~PortMapper() { stop(); }
    void start(uint16_t port);  // begins discovery + mapping asynchronously
    void stop();                // removes the mapping (best effort)
    std::string status();       // human readable, e.g. "port 27500 open - public IP 1.2.3.4"
    bool done() const { return done_; }
    bool ok() const { return ok_; }
    std::string externalIp();

private:
    void worker();
    void setStatus(const std::string& s);
    std::thread thread_;
    std::mutex mu_;
    std::string status_ = "idle";
    std::string extIp_;
    std::string controlUrl_, serviceType_, localIp_;
    uint16_t port_ = 0;
    std::atomic<bool> done_{false}, ok_{false}, mapped_{false}, quit_{false};
    int lease_ = 0;             // the lease the router accepted (0 = permanent)
    bool addMapping();          // (re)adds the forward; true if the router accepted it
};
