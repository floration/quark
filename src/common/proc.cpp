#include "proc.h"
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {
// A write to a pipe whose reader is gone (curl that exited early) must fail with EPIPE, not kill the game. Ignored
// once, at start-up before main() and any thread, process-wide; children get the default back (POSIX_SPAWN_SETSIGDEF
// below). It used to be swapped around every write, which raced between threads (FINAL_REVIEW 2.7).
const bool g_sigpipeIgnored = [] { signal(SIGPIPE, SIG_IGN); return true; }();

std::vector<char*> cargv(const std::vector<std::string>& argv) {
    std::vector<char*> a;
    for (const std::string& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    return a;
}
void devnull(int fd) {
    int n = open("/dev/null", O_RDWR);
    if (n >= 0) { dup2(n, fd); if (n != fd) close(n); }
}
// Both ends close on exec, so a child started on another thread never inherits this pipe (it did: a curl then waited
// for a stdin EOF that another child was holding back, and a browser opened meanwhile kept an HTTP read open).
bool cloexecPipe(int fds[2]) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return pipe2(fds, O_CLOEXEC) == 0;
#else
    // macOS has no pipe2: a fork + exec on another thread right between these lines could still inherit the pipe.
    // None of ours can: procRun spawns with POSIX_SPAWN_CLOEXEC_DEFAULT (the child gets stdin, stdout and stderr
    // only) and procSpawnDetached closes everything above stderr before its exec.
    if (pipe(fds) != 0) return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}
} // namespace

int procRun(const std::vector<std::string>& argv, const std::string& input,
            const std::function<bool(const char*, size_t)>& sink) {
    if (argv.empty()) return -1;
    (void)g_sigpipeIgnored;
    std::vector<char*> a = cargv(argv);
    int in[2], out[2];
    if (!cloexecPipe(in)) return -1;
    if (!cloexecPipe(out)) { close(in[0]); close(in[1]); return -1; }
    // posix_spawn: the pipe ends become the child's stdin / stdout (a dup2 target doesn't keep close-on-exec), stderr
    // goes to /dev/null, SIGPIPE back to its default. (Also no fork of the whole game: glibc spawns with vfork.)
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&at);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(&at, &def);
    short flags = POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;   // (macOS: nothing but the three above reaches the child)
#endif
    posix_spawnattr_setflags(&at, flags);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, a[0], &fa, &at, a.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(in[0]);
    close(out[1]);
    if (rc != 0) { close(in[1]); close(out[0]); return -1; }
    size_t off = 0;   // (inputs here are small form bodies: they fit in the pipe, so this can't wait on our own read)
    while (off < input.size()) {
        ssize_t w = write(in[1], input.data() + off, input.size() - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;   // EPIPE: it stopped reading
        off += (size_t)w;
    }
    close(in[1]);
    char buf[65536];
    bool stopped = false;
    for (;;) {
        ssize_t n = read(out[0], buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (!sink(buf, (size_t)n)) { stopped = true; kill(pid, SIGTERM); break; }
    }
    close(out[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (stopped) return 0;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

bool procSpawnDetached(const std::vector<std::string>& argv, const std::string& cwd) {
    if (argv.empty()) return false;
    std::vector<char*> a = cargv(argv);
    long lim = sysconf(_SC_OPEN_MAX);   // (before the fork: only async-signal-safe calls in the child)
    int maxFd = lim > 0 && lim < 65536 ? (int)lim : 65536;
    int fds[2]; // CLOEXEC pipe: closes without data when exec succeeds, carries errno when it fails
    if (!cloexecPipe(fds)) return false;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return false; }
    if (pid == 0) {
        close(fds[0]);
        if (fork() != 0) _exit(0); // double fork: the grandchild is reparented, nothing left for us to reap
        setsid();
        signal(SIGPIPE, SIG_DFL);   // (ignored in this process; a browser expects the default)
        for (int fd = 3; fd < maxFd; fd++) if (fd != fds[1]) close(fd);   // nothing of ours past stderr reaches it
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) { int e = errno; if (write(fds[1], &e, sizeof e)) {} _exit(127); }
        devnull(0); devnull(1); devnull(2);
        execvp(a[0], a.data());
        int e = errno;
        if (write(fds[1], &e, sizeof e)) {}
        _exit(127);
    }
    close(fds[1]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    int e = 0;
    ssize_t n;
    while ((n = read(fds[0], &e, sizeof e)) < 0 && errno == EINTR) {}
    close(fds[0]);
    return n == 0;
}
#else
int procRun(const std::vector<std::string>&, const std::string&, const std::function<bool(const char*, size_t)>&) { return -1; }
bool procSpawnDetached(const std::vector<std::string>&, const std::string&) { return false; }
#endif
