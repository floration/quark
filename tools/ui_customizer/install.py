#!/usr/bin/env python3
"""Installs / updates the UI customizer from this game checkout (the Quark Project app runs it; safe to re-run).

    python tools/ui_customizer/install.py [--dest <Quark>/ui-customizer] [--extract | --extract-if-stale] [--open]

Run from anywhere (usually cwd = Quark\\repository, a checkout of testing). It builds <dest>/ui-customizer.html (the
app finds tools at Quark\\<folder>\\<folder>.html) from this checkout's tools/ui_customizer sources and the newest dump
(data/ui-dump.json.gz, or <dest>/extract/ui-dump.json when that one is newer). Non-interactive: no prompts, no pause;
it opens no browser unless --open is given (and never when QUARK_SHELL=1). It only writes the generated files
(ui-customizer.html, fonts/catalog.json, INSTALLED.txt) and creates provenance.json when there is none; it never
deletes or rewrites the owner's files there (provenance.json, saved / exported layouts, extract/, fonts/ofl/, uploads).

--extract first regenerates <dest>/extract (dump + screenshots) from this checkout's game source: it needs a game build
(git, cmake, MinGW-w64; about 8 minutes, a hidden window): extract/extract.py. --extract-if-stale (what the Quark Project
app runs, at low priority) does that only when the build finds the dump out of date (commits changed src/client since
its commit) or this PC has no reference screenshots of it (the Screenshot overlay; they are not committed), then builds
again; without a toolchain, or when the extract fails, the page is kept as built (with its
out-of-date banner) and the install still succeeds, so the app doesn't retry an 8-minute job on every start. Python 3
standard library only. Exit 0 = installed."""
import argparse, datetime, json, os, subprocess, sys, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
DEFAULT_DEST = os.path.normpath(os.path.join(REPO, '..', 'ui-customizer'))


def git(*args):
    try:
        p = subprocess.run(['git', '-C', REPO] + list(args), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                           creationflags=0x08000000 if os.name == 'nt' else 0)
        return p.stdout.strip() if p.returncode == 0 else ''
    except OSError:
        return ''


def ensure_provenance(dest):
    """provenance.json is the owner's (the page writes into it): created empty when missing, else left exactly as it is."""
    path = os.path.join(dest, 'provenance.json')
    if not os.path.exists(path):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            json.dump({'format': 1, 'tool': 'ui-customizer', 'items': {}}, f, indent=2)
            f.write('\n')
        return 'created provenance.json (empty)'
    try:
        with open(path, encoding='utf-8-sig') as f:
            data = json.load(f)
        n = len(data.get('items') or {}) if isinstance(data, dict) else 0
        return f'kept provenance.json ({n} items, not changed)'
    except (OSError, ValueError) as e:
        return f'kept provenance.json as it is (warning: not readable JSON: {e}; it is not embedded)'


def extract_dump(dest):
    """extract/extract.py into <dest>/extract; False (with the reason printed) when it can't run or fails."""
    sys.path.insert(0, os.path.join(HERE, 'extract'))
    import extract
    try:
        extract.main(['--out', os.path.join(dest, 'extract')])
        return True
    except SystemExit as e:   # (extract.py exits with the reason: no toolchain, a failed build or UI test)
        if e.code in (0, None):
            return True
        print(f'  WARNING: could not regenerate the dump ({e.code}); the page is built from the old one')
        return False


def main(argv=None):
    ap = argparse.ArgumentParser(description='Install / update the Quark UI customizer.')
    ap.add_argument('--dest', default=DEFAULT_DEST, help='the tool folder (default: Quark\\ui-customizer next to the checkout)')
    ap.add_argument('--extract', action='store_true', help='regenerate the dump + screenshots from the game source first (needs a game build)')
    ap.add_argument('--extract-if-stale', action='store_true',
                    help='regenerate the dump only when it is out of date (UI code commits since it), then build again')
    ap.add_argument('--open', action='store_true', help='open the page in the browser afterwards (never when QUARK_SHELL=1)')
    a = ap.parse_args(argv)
    dest = os.path.abspath(a.dest)
    os.makedirs(dest, exist_ok=True)
    sha = git('rev-parse', 'HEAD')
    ver = ''
    try:
        with open(os.path.join(REPO, 'VERSION'), encoding='utf-8') as f:
            ver = f.read().strip()
    except OSError:
        pass
    print(f'ui-customizer install: {REPO} ({ver or "?"} {sha[:12] or "?"}) -> {dest}')
    print('  ' + ensure_provenance(dest))
    if a.extract:
        sys.path.insert(0, os.path.join(HERE, 'extract'))
        import extract
        extract.main(['--out', os.path.join(dest, 'extract')])   # (asked for by hand: a failure fails the install)
    sys.path.insert(0, HERE)
    import build
    info = build.main(['--out-dir', dest]) or {}
    fresh = info.get('freshness') or {}
    no_shots = bool(info.get('shotsWanted')) and not info.get('shots')
    if a.extract_if_stale and not a.extract and (fresh.get('stale') or no_shots):
        why = f"the UI dump is out of date ({fresh['why']})" if fresh.get('stale') else "this PC has no reference screenshots of the dump"
        print(f"  {why}: regenerating it (about 8 minutes, a hidden window)")
        if extract_dump(dest):
            info = build.main(['--out-dir', dest]) or {}
            fresh = info.get('freshness') or {}
    dump_line = f"dump {info.get('dump') or 'none'}: {fresh.get('dumpVersion') or '?'} {(fresh.get('dumpCommit') or '?')[:12]}"
    if fresh.get('stale'):
        dump_line += f" - OUT OF DATE ({fresh['why']}); refresh it: {fresh['refresh']}"
    elif fresh.get('note'):
        dump_line += f" - up to date ({fresh['note']})"
    with open(os.path.join(dest, 'INSTALLED.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'ui-customizer.html built {datetime.datetime.now().isoformat(timespec="seconds")} by tools/ui_customizer/install.py\n'
                f'from {REPO} at {ver} {sha}\n'
                f'{dump_line}\n'
                'Generated here (rewritten by each install): ui-customizer.html, fonts/catalog.json, this file.\n'
                "Yours (never touched by the install): provenance.json, extract/, fonts/ofl/, and anything else.\n")
    print(f'  wrote ui-customizer.html, fonts/catalog.json, INSTALLED.txt')
    if fresh.get('stale'):
        print(f"  WARNING: the UI dump is out of date ({fresh['why']}); the page says so too. Refresh: {fresh['refresh']}")
    if a.open and os.environ.get('QUARK_SHELL') != '1':
        webbrowser.open('file:///' + os.path.join(dest, 'ui-customizer.html').replace('\\', '/'))
    print('ui-customizer install: done')
    return 0


if __name__ == '__main__':
    sys.exit(main())
