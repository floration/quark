"""Small helpers shared by the sources: time, tolerant field access, HTTP, subprocesses, versions."""
import datetime
import gzip
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

USER_AGENT = "quarkology"
IS_WINDOWS = os.name == "nt"
# no console window flashing up for git / ssh when the server runs under pythonw
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0) if IS_WINDOWS else 0


class SourceError(Exception):
    """A source could not be read; the message is shown to q (never put a secret in it)."""


class Retry(SourceError):
    """Not ready yet (for example the versions another source provides): try again after `seconds`."""

    def __init__(self, message, seconds=5):
        super().__init__(message)
        self.seconds = seconds


def iso(ts):
    """Unix time -> '2026-10-24T19:41:52Z' (None stays None)."""
    if ts is None:
        return None
    return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def now_iso():
    return iso(datetime.datetime.now(datetime.timezone.utc).timestamp())


def parse_time(s):
    """ISO 8601 text (with Z or an offset) -> unix time, or None when it is not one."""
    if isinstance(s, (int, float)):
        return float(s) / (1000.0 if s > 1e11 else 1.0)  # (milliseconds or seconds)
    if not isinstance(s, str) or not s.strip():
        return None
    t = s.strip().replace("Z", "+00:00")
    try:
        d = datetime.datetime.fromisoformat(t)
    except ValueError:
        return None
    if d.tzinfo is None:
        d = d.replace(tzinfo=datetime.timezone.utc)
    return d.timestamp()


def get(d, *paths, default=None):
    """The first present, non-None value among dotted paths: get(x, 'testing.version', 'testingVersion')."""
    for path in paths:
        cur = d
        for part in path.split("."):
            if isinstance(cur, dict) and part in cur:
                cur = cur[part]
            elif isinstance(cur, list) and part.isdigit() and int(part) < len(cur):
                cur = cur[int(part)]
            else:
                cur = None
                break
        if cur is not None:
            return cur
    return default


def as_list(v):
    """A list as is; a dict of name -> object as a list of objects with 'name' filled in; anything else empty."""
    if isinstance(v, list):
        return [x for x in v if x is not None]
    if isinstance(v, dict):
        out = []
        for k, x in v.items():
            if isinstance(x, dict):
                x = dict(x)
                x.setdefault("name", k)
                out.append(x)
        return out
    return []


def to_int(v, default=None):
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, (int, float)):
        return int(v)
    if isinstance(v, str):
        m = re.match(r"\s*(-?\d+)", v)
        if m:
            return int(m.group(1))
    return default


def to_float(v, default=None):
    if isinstance(v, bool):
        return default
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        try:
            return float(v.strip().rstrip("%"))
        except ValueError:
            return default
    return default


# (a suffix starts with a letter: "1.18.6-test.231"; never the "-9c2e1f0" commit of "quark-1.18.6-9c2e1f0"; testing
# builds from 1.19.0 on are "1.19.0.testing.341": the production VERSION plus .testing.<run>, above that release)
VERSION_RE = re.compile(r"(?<![\d.])(\d+)\.(\d+)\.(\d+)((?:\.testing\.\d+|-[A-Za-z]+(?:\.\d+)*)?)(?!\d|\.\d)")


def parse_version(s):
    """'v1.18.6', 'test-1.18.6-test.231', 'test-1.19.0.testing.5', '1.18.6' -> '1.18.6' / '1.18.6-test.231' /
    '1.19.0.testing.5' (None if there is none)."""
    if not isinstance(s, str):
        return None
    m = VERSION_RE.search(s)
    return m.group(0) if m else None


def base_version(s):
    """'1.18.6-test.231' / '1.19.0.testing.5' -> '1.18.6' / '1.19.0'."""
    v = parse_version(s)
    if not v:
        return None
    m = VERSION_RE.match(v)
    return "%s.%s.%s" % m.group(1, 2, 3)


def version_key(s):
    """Sort key: (major, minor, patch, kind, suffix number); kind 0: an old -test.N prerelease (below its release),
    1: the release, 2: a .testing.N build (above it, below the next release). Unknown sorts lowest."""
    v = parse_version(s)
    if not v:
        return (-1, -1, -1, -1, -1)
    m = VERSION_RE.match(v)
    suffix = m.group(4)
    n = re.findall(r"\d+", suffix)
    kind = 2 if suffix.startswith(".testing.") else 0 if suffix else 1
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), kind, int(n[-1]) if n else 0)


def short_sha(s):
    if not isinstance(s, str):
        return None
    m = re.search(r"\b[0-9a-f]{7,40}\b", s)
    return m.group(0)[:7] if m else None


_LOCAL_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # (never proxy 127.0.0.1)
_DEFAULT_OPENER = urllib.request.build_opener()


def http_get(url, timeout=3.0, local=None, headers=None, with_headers=False):
    """GET -> (status, bytes) (or (status, bytes, headers)). A 304 is returned, not raised. A gzip body (only if a
    server sends one unasked: no Accept-Encoding goes out) is decoded. Raises SourceError with a short message on
    network failure / other non-2xx."""
    if local is None:
        local = url.startswith("http://127.0.0.1") or url.startswith("http://localhost")
    h = {"User-Agent": USER_AGENT, "Accept": "application/json, text/plain, */*"}
    h.update(headers or {})
    req = urllib.request.Request(url, headers=h)
    opener = _LOCAL_OPENER if local else _DEFAULT_OPENER
    try:
        with opener.open(req, timeout=timeout) as r:
            body = r.read(16 * 1024 * 1024)
            if (r.headers.get("Content-Encoding") or "").lower() == "gzip":
                body = gzip.decompress(body)
            return (r.status, body, r.headers) if with_headers else (r.status, body)
    except urllib.error.HTTPError as e:
        if e.code == 304:
            return (304, b"", e.headers) if with_headers else (304, b"")
        raise SourceError("HTTP %d from %s" % (e.code, url.split("?")[0]))
    except (EOFError, gzip.BadGzipFile):
        raise SourceError("%s sent a broken gzip body" % url.split("?")[0])
    except (urllib.error.URLError, OSError, ValueError) as e:
        reason = getattr(e, "reason", e)
        raise SourceError("%s unreachable (%s)" % (url.split("?")[0], str(reason)[:120]))


def http_json(url, timeout=3.0):
    _, body = http_get(url, timeout)
    try:
        return json.loads(body.decode("utf-8-sig"))
    except (ValueError, UnicodeDecodeError):
        raise SourceError("%s did not return JSON" % url.split("?")[0])


class CachedJSON:
    """GET a sibling's JSON with If-None-Match: a 304 reuses the last body (no parse, no transfer)."""

    def __init__(self):
        self.etag = None
        self.data = None
        self.url = None

    def get(self, url, timeout=3.0):
        if url != self.url:
            self.etag, self.data, self.url = None, None, url
        hdr = {"If-None-Match": self.etag} if self.etag and self.data is not None else {}
        status, body, headers = http_get(url, timeout, headers=hdr, with_headers=True)
        if status == 304 and self.data is not None:
            return self.data
        try:
            data = json.loads(body.decode("utf-8-sig"))
        except (ValueError, UnicodeDecodeError):
            raise SourceError("%s did not return JSON" % url.split("?")[0])
        self.etag = headers.get("ETag") if headers is not None else None
        self.data = data
        return data


def read_text(path, limit=4 * 1024 * 1024):
    with open(path, "rb") as f:
        return f.read(limit).decode("utf-8-sig", errors="replace")


def read_json_file(path):
    return json.loads(read_text(path))


def run(args, cwd=None, timeout=10.0):
    """Run a fixed command (a list, never a shell string) -> (returncode, stdout, stderr) as text."""
    try:
        p = subprocess.run(args, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, creationflags=NO_WINDOW)
    except FileNotFoundError:
        raise SourceError("%s not found" % os.path.basename(args[0]))
    except subprocess.TimeoutExpired:
        raise SourceError("%s timed out after %d s" % (os.path.basename(args[0]), timeout))
    dec = lambda b: b.decode("utf-8", errors="replace")
    return p.returncode, dec(p.stdout), dec(p.stderr)


def write_json_atomic(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)
    os.replace(tmp, path)


def log(msg):
    """Diagnostics to stderr (nothing under pythonw). Never pass a secret here."""
    try:
        if sys.stderr:
            sys.stderr.write("[pm %s] %s\n" % (now_iso(), msg))
            sys.stderr.flush()
    except (OSError, ValueError):
        pass
