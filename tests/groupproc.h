// quark_tests runs its test groups several at once, each in its own process (main() in tests.cpp schedules them and
// prints their output in order). This is the process side: start one group with its output going to a file, and wait
// for one to end. Linux / macOS fork (the child runs the group's code, nothing re-initialised); Windows has no fork, so
// it runs this program again as `quark_tests group NAME --child`, its stdout and stderr on the file.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// How many groups can run at once: Windows waits on the running ones with one WaitForMultipleObjects (64 handles).
#ifdef _WIN32
constexpr unsigned kGroupMaxJobs = 64;
#else
constexpr unsigned kGroupMaxJobs = 1u << 30;
#endif

// Starts the group `name` in its own process, its stdout and stderr into the file `log`; `low` runs it at a lower CPU
// priority (nice 10 / BELOW_NORMAL_PRIORITY_CLASS). POSIX: the forked child runs `child` and exits with what it
// returns. Windows: `child` isn't used (the new process runs the group itself). Returns the process, or -1.
intptr_t groupStart(const char* name, const std::string& log, bool low, const std::function<int()>& child);
// Waits until one of `running` ends: its index in `running` (-1 if there's nothing to wait for), with its exit code in
// *code (a group exits with its failure count, at most 100) and, when it didn't exit by itself, what happened in *crash
// (a signal; Windows: an exception's NTSTATUS, any code over 100), else "".
int groupWait(const std::vector<intptr_t>& running, int* code, std::string* crash);
