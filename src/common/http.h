// Minimal blocking HTTPS GET for small text responses (server list). Call it from a worker
// thread, never from the game or tick loop. WinHTTP on Windows, the system curl elsewhere.
#pragma once
#include <string>

bool httpGetText(const std::string& url, std::string& out, int timeoutMs = 5000, std::string* err = nullptr);
// POST an application/x-www-form-urlencoded body (callers pass only URL-safe characters: [A-Za-z0-9=&:,._-]).
bool httpPostForm(const std::string& url, const std::string& body, std::string& out, int timeoutMs = 8000, std::string* err = nullptr);
