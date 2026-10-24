#!/usr/bin/env python3
"""Makes the customizer's dump (every UI page's elements) and screenshots from the game's CURRENT source.

    python tools/ui_customizer/extract/extract.py [--out <Quark>/ui-customizer/extract] [--commit HEAD] [--gz]

1. A scratch clone of this checkout at --commit (<out>/../.extract-src: reused, so later runs build incrementally;
   never this checkout itself), with probe.patch applied there only (it is never committed to the game):
   the renderer logs every 2D draw and every Ui widget as JSON, the UI test writes the richest frame of each screen
   (raw/<n>.json + .png), and under --uitest the window is created hidden, unfocused and off screen, every display
   mode change keeps it so, and no audio device is opened.
2. cmake + build the `quark` target there (Release, no tests; MinGW Makefiles on Windows).
3. `quark --uitest` (about 4 minutes) with a private TMP (the UI test makes its own data folder there: the player's
   settings, maps and demos are never touched), no console window. Killed by its PID after --timeout seconds.
4. build_dump.py: <out>/ui-dump.json + <out>/shots/ (+ data/ui-dump.json.gz in this checkout with --gz, for committing).
   An older dump of another commit in <out> is kept as <out>-<its version> first (--no-keep: replaced).

Needs git, cmake, a C++ compiler and make (Windows: MinGW-w64, as tools/build_windows.ps1 -Setup installs), Python 3.
On Linux the hidden window still needs a display: run it under Xvfb (DISPLAY=:99). Prints what it did; exit 0 = done."""
import argparse, json, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
REPO = os.path.normpath(os.path.join(TOOL, '..', '..'))
DEFAULT_OUT = os.path.normpath(os.path.join(REPO, '..', 'ui-customizer', 'extract'))
WIN = os.name == 'nt'


def run(cmd, cwd=None, env=None, quiet=False, check=True):
    if not quiet:
        print('  $ ' + ' '.join(cmd), flush=True)
    kw = {'creationflags': 0x08000000} if WIN else {}   # (CREATE_NO_WINDOW: no console pops up)
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace', **kw)
    if check and p.returncode != 0:
        sys.exit(f'extract: {cmd[0]} failed ({p.returncode}):\n' + p.stdout[-4000:])
    return p


def toolchain_env():
    """PATH with a MinGW-w64 found in the usual places when g++ isn't on it already (Windows)."""
    env = dict(os.environ)
    if WIN and not shutil.which('g++'):
        la = os.environ.get('LOCALAPPDATA', '')
        for d in [os.path.join(la, 'Programs', 'qac-tools', 'gcc', 'mingw64', 'bin'), r'C:\mingw64\bin', r'C:\msys64\ucrt64\bin',
                  r'C:\msys64\mingw64\bin', os.path.join(la, 'Microsoft', 'WinGet', 'Links')]:
            if os.path.isfile(os.path.join(d, 'g++.exe')):
                env['PATH'] = d + os.pathsep + env.get('PATH', '')
                break
    for t in ('git', 'cmake') + (('g++', 'mingw32-make') if WIN else ('c++',)):
        if not shutil.which(t, path=env.get('PATH')):
            sys.exit(f'extract: {t} not found (Windows: tools/build_windows.ps1 -Setup installs Git, CMake and MinGW-w64)')
    return env


def kill(pid):
    if WIN:
        subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       creationflags=0x08000000)
    else:
        try:
            os.kill(pid, 9)
        except OSError:
            pass


def main(argv=None):
    ap = argparse.ArgumentParser(description='Regenerate the UI customizer dump + screenshots from the game source.')
    ap.add_argument('--out', default=DEFAULT_OUT, help='where ui-dump.json and shots/ go (default: Quark/ui-customizer/extract)')
    ap.add_argument('--scratch', help='the scratch clone (default: <out>/../.extract-src)')
    ap.add_argument('--commit', default='HEAD', help='the commit of this checkout to extract (default HEAD)')
    ap.add_argument('--gz', action='store_true', help="also write the tool's data/ui-dump.json.gz in this checkout")
    ap.add_argument('--no-keep', action='store_true', help="don't keep an older dump as <out>-<version>")
    ap.add_argument('--timeout', type=int, default=1500, help='seconds the UI test may take (default 1500)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args(argv)
    out = os.path.abspath(a.out)
    scratch = os.path.abspath(a.scratch or os.path.join(os.path.dirname(out), '.extract-src'))
    env = toolchain_env()
    t0 = time.time()

    sha = run(['git', '-C', REPO, 'rev-parse', a.commit], quiet=True).stdout.strip()
    version = run(['git', '-C', REPO, 'show', sha + ':VERSION'], quiet=True).stdout.strip() or '?'
    print(f'extract: {version} {sha[:12]} -> {out}')

    # 1. the scratch clone at that commit, the probe applied
    if not os.path.isdir(os.path.join(scratch, '.git')):
        run(['git', 'clone', '--no-checkout', '-q', REPO, scratch], env=env)
    run(['git', '-C', scratch, 'fetch', '-q', '--no-tags', REPO, '+HEAD:refs/extract/head', '+refs/heads/*:refs/extract/heads/*'], env=env)
    run(['git', '-C', scratch, 'checkout', '-q', '-f', '--detach', sha], env=env)
    run(['git', '-C', scratch, 'clean', '-fdq', '-e', 'build-x', '-e', '.tmp', '-e', 'run'], env=env)
    patch = os.path.join(HERE, 'probe.patch')
    if run(['git', '-C', scratch, 'apply', '--check', patch], env=env, check=False).returncode != 0:
        sys.exit('extract: probe.patch no longer applies to this commit: update it (tools/ui_customizer/extract/README.md, '
                 '"Updating the probe")')
    run(['git', '-C', scratch, 'apply', patch], env=env)

    # 2. build
    bld = os.path.join(scratch, 'build-x')
    gen = ['-G', 'MinGW Makefiles'] if WIN else []
    run(['cmake', '-S', scratch, '-B', bld] + gen + ['-DCMAKE_BUILD_TYPE=Release', '-DQUARK_BUILD_TESTS=OFF'], env=env)
    run(['cmake', '--build', bld, '-j', str(a.jobs), '--target', 'quark'], env=env)
    exe = os.path.join(bld, 'quark.exe' if WIN else 'quark')
    if not os.path.isfile(exe):
        sys.exit('extract: the build made no ' + exe)

    # 3. the UI test, hidden, with a private TMP and its own data folder
    work = os.path.join(scratch, 'run')
    shutil.rmtree(work, ignore_errors=True)
    tmp = os.path.join(scratch, '.tmp')
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(work, 'raw'))
    os.makedirs(tmp)
    renv = dict(env, TMP=tmp, TEMP=tmp, TMPDIR=tmp, QUARK_EXTRACT_DIR=work, QUARK_EXTRACT_SHOTS='1')
    log = os.path.join(work, 'run.log')
    print(f'  $ {exe} --uitest (hidden window, no audio; log {log})', flush=True)
    kw = {'creationflags': 0x08000000} if WIN else {}
    with open(log, 'w', encoding='utf-8', errors='replace') as lf:
        p = subprocess.Popen([exe, '--uitest', os.path.join(work, 'report.json')], cwd=scratch, env=renv, stdout=lf,
                             stderr=subprocess.STDOUT, **kw)
        try:
            rc = p.wait(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            kill(p.pid)
            p.wait()
            sys.exit(f'extract: the UI test took over {a.timeout} s: killed (PID {p.pid}); see {log}')
    with open(log, encoding='utf-8', errors='replace') as lf:
        summary = [l.strip() for l in lf if ' widgets on ' in l]
    if summary:
        print('  ' + summary[-1])
    if rc != 0:
        print(f'  note: the UI test exited {rc} (failures are the game\'s, see {log}); the captures are used anyway')
    n = len([f for f in os.listdir(os.path.join(work, 'raw')) if f.endswith('.json')])
    if not n:
        sys.exit('extract: the UI test wrote no captures; see ' + log)

    # 4. the dump (an older one of another commit kept beside it)
    old = os.path.join(out, 'ui-dump.json')
    if os.path.isfile(old) and not a.no_keep:
        try:
            with open(old, encoding='utf-8-sig') as f:
                osrc = json.load(f).get('source') or {}
        except (OSError, ValueError):
            osrc = {}
        if str(osrc.get('commit', '')) not in (sha, sha[:7]):
            keep = out + '-' + str(osrc.get('version') or str(osrc.get('commit', 'old'))[:7])
            if not os.path.exists(keep):
                shutil.copytree(out, keep)
                print(f'  kept the previous dump ({osrc.get("version", "?")} {str(osrc.get("commit", "?"))[:7]}) as {keep}')
    os.makedirs(out, exist_ok=True)
    sys.path.insert(0, HERE)
    import build_dump
    args = ['--out', out, '--commit', sha, '--version', version, os.path.join(work, 'raw')]
    if a.gz:
        args[0:0] = ['--gz', os.path.join(TOOL, 'data', 'ui-dump.json.gz')]
    build_dump.main(args)
    shutil.rmtree(tmp, ignore_errors=True)
    print(f'extract: done in {time.time() - t0:.0f} s ({n} captures)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
