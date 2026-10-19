// Minimal blocking HTTPS GET for small text responses (server list). Call it from a worker
// thread, never from the game or tick loop. WinHTTP on Windows, the system curl elsewhere.
#pragma once
#include <string>

bool httpGetText(const std::string& url, std::string& out, int timeoutMs = 5000, std::string* err = nullptr);
