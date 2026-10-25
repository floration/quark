"""Quark Project: stops the servers the shell started when the shell dies without closing (a crash, End task).

main.js starts this hidden at app start and writes to its stdin, one line each:
  add <pid>    a server the shell started (the cmd.exe, or the program itself, that it spawned)
  del <pid>    that server exited
  quit         the shell is closing normally (it stops its servers itself): exit without stopping anything
When stdin ends without `quit` (the shell's process is gone, so the pipe closed), every server still listed is stopped
with all its descendants, and this exits. It never polls: it sleeps in a blocking read until the shell writes or dies.

Why it is needed (Windows): Electron's libuv puts each process it spawns in a kill-on-close job, but with silent
breakaway, so only the direct child (cmd.exe / py.exe) dies with the shell and the real server (python.exe) lives on,
still serving old code that the next open would adopt. This script runs as python.exe under cmd.exe, so it leaves
that job the same way and outlives the shell. The direct child is often already gone when the shell dies, so the
descendants are found by parent pid in a process snapshot (taskkill /T needs a living root), and a process counts
only when it was created after its parent (a reused pid never matches). Elsewhere each server is a process group.
Standard library only (ctypes on Windows).
"""
import os
import signal
import sys

IS_WINDOWS = os.name == "nt"

if IS_WINDOWS:
    import ctypes
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    TH32CS_SNAPPROCESS = 0x2
    PROCESS_TERMINATE = 0x1
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    INVALID_HANDLE = ctypes.c_void_p(-1).value

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                    ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wintypes.DWORD),
                    ("cntThreads", wintypes.DWORD), ("th32ParentProcessID", wintypes.DWORD),
                    ("pcPriClassBase", ctypes.c_long), ("dwFlags", wintypes.DWORD),
                    ("szExeFile", ctypes.c_wchar * 260)]

    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    k32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    k32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    k32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]

    def created(pid):
        """The process's creation time (100 ns units), or None when it is gone / not ours to see."""
        h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not h:
            return None
        try:
            t = [wintypes.FILETIME() for _ in range(4)]
            if not k32.GetProcessTimes(h, *[ctypes.byref(x) for x in t]):
                return None
            return (t[0].dwHighDateTime << 32) | t[0].dwLowDateTime
        finally:
            k32.CloseHandle(h)

    def snapshot():
        """{pid: parent pid} of every process."""
        out = {}
        h = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
        if not h or h == INVALID_HANDLE:
            return out
        try:
            e = PROCESSENTRY32W()
            e.dwSize = ctypes.sizeof(e)
            ok = k32.Process32FirstW(h, ctypes.byref(e))
            while ok:
                out[e.th32ProcessID] = e.th32ParentProcessID
                ok = k32.Process32NextW(h, ctypes.byref(e))
        finally:
            k32.CloseHandle(h)
        return out

    def terminate(pid):
        h = k32.OpenProcess(PROCESS_TERMINATE, False, pid)
        if h:
            k32.TerminateProcess(h, 1)
            k32.CloseHandle(h)

    def stop_all(roots):
        """roots: {pid: creation time when it was added (None = unknown)}. Stops each root that is still the same
        process and every descendant created after its parent."""
        parents = snapshot()
        times = {}
        found = {}
        for pid, t0 in roots.items():
            now = created(pid) if pid in parents else None
            if pid in parents and t0 is not None and now is not None and now != t0:
                continue  # (the pid was reused by another program)
            found[pid] = t0 if t0 is not None else now
        grew = True
        while grew:
            grew = False
            for pid, ppid in parents.items():
                if pid in found or ppid not in found or pid == os.getpid():
                    continue
                t = times[pid] = times.get(pid) or created(pid)
                if t is None or (found[ppid] is not None and t < found[ppid]):
                    continue
                found[pid] = t
                grew = True
        for pid in sorted(found, key=lambda p: -(found[p] or 0)):  # (newest first: children before parents)
            if pid in parents:
                terminate(pid)
else:
    def created(pid):
        return None

    def stop_all(roots):
        # (main.js starts each server as its own process group: stop the group)
        for pid in roots:
            try:
                os.killpg(pid, signal.SIGTERM)
            except OSError:
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass


def main():
    roots = {}
    stdin = sys.stdin.buffer if sys.stdin else None
    if stdin is None:
        return 0
    while True:
        try:
            line = stdin.readline()
        except (OSError, ValueError):
            line = b""
        if not line:
            break
        cmd, _, arg = line.decode("ascii", "replace").strip().partition(" ")
        if cmd == "quit":
            return 0
        try:
            pid = int(arg)
        except ValueError:
            continue
        if cmd == "add" and pid > 0:
            roots[pid] = created(pid)
        elif cmd == "del":
            roots.pop(pid, None)
    if roots:
        stop_all(roots)
    return 0


if __name__ == "__main__":
    sys.exit(main())
