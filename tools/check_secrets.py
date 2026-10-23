#!/usr/bin/env python3
"""Looks for keys, tokens and secrets that must never be committed (docs/OPERATIONS.md "Keeping secrets secret").

    python3 tools/check_secrets.py              every tracked file (CI runs this on each push)
    python3 tools/check_secrets.py --staged     only what's staged for the next commit (the pre-commit hook)
    git config core.hooksPath tools/githooks    turn the pre-commit hook on (once per clone)

Exit 1 with each hit (file:line: what it looks like) when anything is found; the secret itself is never printed.
A line that is a deliberate example or test value carries the marker `secret-scan: allow` (say why next to it).
"""
import re
import subprocess
import sys

PATTERNS = [
    ("a private key block", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("a Discord bot token", re.compile(r"\b[MNO][A-Za-z\d_-]{23,27}\.[A-Za-z\d_-]{6}\.[A-Za-z\d_-]{27,40}\b")),
    ("a Discord webhook URL", re.compile(r"discord(?:app)?\.com/api/webhooks/\d+/[A-Za-z\d_-]{20,}")),
    ("a GitHub token", re.compile(r"\b(?:gh[pousr]_[A-Za-z\d]{36,}|github_pat_[A-Za-z\d_]{40,})\b")),
    ("a Cloudflare / AWS / Google key", re.compile(r"\b(?:AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{35})\b")),
    # a value assigned to one of quark's own secrets (QUARK_SIGNING_KEY, BOT_KEY, ADMIN_KEY, DISCORD_TOKEN,
    # CLOUDFLARE_API_TOKEN, GITHUB_TOKEN, QUARK_ALERT_WEBHOOK): a long literal, not a ${{ secrets.X }} / $X / <placeholder>
    ("a value for one of quark's secrets", re.compile(
        r"\b(?:QUARK_SIGNING_KEY|BOT_KEY|ADMIN_KEY|DISCORD_TOKEN|CLOUDFLARE_API_TOKEN|GITHUB_TOKEN|QUARK_ALERT_WEBHOOK)"
        r"\s*[:=]\s*[\"']?(?![\"'<$({%])(?=[A-Za-z\d_./:+-]{24,})[A-Za-z\d_./:+-]+")),
]
# a documented example, not a value: "...", "example", "xxxx", "your-..."
PLACEHOLDER = re.compile(r"\.\.\.|example|xxxx|your[-_]", re.I)
# files that must never be tracked at all, whatever is in them
BAD_FILES = re.compile(r"(^|/)(quark_signing_key\.txt|quark\.key|[^/]*\.pem|\.env|[^/]*\.env|config\.json|state\.json)$")
ALLOWED_FILES = re.compile(r"(^|/)config\.example\.json$")
SKIP = re.compile(r"\.(png|jpg|jpeg|gif|webp|ico|ogg|wav|mp3|flac|ttf|otf|glb|qpak|zip|gz)$|^third_party/|^assets/")


def files(staged):
    cmd = ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"] if staged else ["git", "ls-files"]
    return [f for f in subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines() if f]


def text_of(path, staged):
    if staged:
        r = subprocess.run(["git", "show", ":" + path], capture_output=True)
        data = r.stdout
    else:
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return None
    if b"\0" in data[:8192]:
        return None
    return data.decode("utf-8", "replace")


def main():
    staged = "--staged" in sys.argv[1:]
    hits = []
    for path in files(staged):
        if BAD_FILES.search(path) and not ALLOWED_FILES.search(path):
            hits.append(f"{path}: this file must never be committed (a key, an env file or a bot config)")
            continue
        if SKIP.search(path):
            continue
        text = text_of(path, staged)
        if text is None:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            if "secret-scan: allow" in line:
                continue
            for what, rx in PATTERNS:
                m = rx.search(line)
                if m and not PLACEHOLDER.search(m.group(0)):
                    hits.append(f"{path}:{n}: looks like {what}")
    if hits:
        print("check_secrets: possible secrets (not printed). Remove them, or mark a deliberate example with "
              "'secret-scan: allow':")
        for h in hits:
            print("  " + h)
        print("If a real secret was already pushed, removing it isn't enough: rotate it (docs/OPERATIONS.md).")
        return 1
    print(f"check_secrets: nothing found ({'staged files' if staged else 'all tracked files'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
