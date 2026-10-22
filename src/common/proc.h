// Run external programs (curl, xdg-open, gio, ...) on Linux/macOS without a shell: argv arrays via
// fork+execvp, so no path, URL or name can be interpreted as shell syntax. Stubs on Windows.
#pragma once
#include <functional>
#include <string>
#include <vector>

// Runs argv[0] (PATH lookup), writes `input` to its stdin, and hands each chunk of its stdout to `sink`
// (return false from sink to stop reading). stderr goes to /dev/null. Returns the exit status (0 = ok),
// or -1 if it could not be started / was killed.
int procRun(const std::vector<std::string>& argv, const std::string& input,
            const std::function<bool(const char*, size_t)>& sink);
// Starts argv in the background (optionally in `cwd`), detached, stdio on /dev/null. True if exec succeeded.
bool procSpawnDetached(const std::vector<std::string>& argv, const std::string& cwd = "");
