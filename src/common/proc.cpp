#include "proc.h"
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
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
} // namespace

int procRun(const std::vector<std::string>& argv, const std::string& input,
            const std::function<bool(const char*, size_t)>& sink) {
    if (argv.empty()) return -1;
    std::vector<char*> a = cargv(argv); // built before fork: no allocation in the child
    int in[2], out[2];
    if (pipe(in) != 0) return -1;
    if (pipe(out) != 0) { close(in[0]); close(in[1]); return -1; }
    pid_t pid = fork();
    if (pid < 0) { close(in[0]); close(in[1]); close(out[0]); close(out[1]); return -1; }
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        devnull(2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execvp(a[0], a.data());
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    // inputs here are small form bodies; MSG_NOSIGNAL isn't available for pipes, so ignore SIGPIPE briefly
    void (*old)(int) = signal(SIGPIPE, SIG_IGN);
    size_t off = 0;
    while (off < input.size()) {
        ssize_t w = write(in[1], input.data() + off, input.size() - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(in[1]);
    signal(SIGPIPE, old);
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
    int fds[2]; // CLOEXEC pipe: closes without data when exec succeeds, carries errno when it fails
    if (pipe(fds) != 0) return false;
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return false; }
    if (pid == 0) {
        close(fds[0]);
        if (fork() != 0) _exit(0); // double fork: the grandchild is reparented, nothing left for us to reap
        setsid();
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
