#!/usr/bin/env python3
"""Builds ui-dump.json + shots/ from the raw captures the patched UI test writes (raw/<n>.json + raw/<n>.png).

    python build_dump.py --out <dir> [--commit SHA] [--version V] [--gz data/ui-dump.json.gz] <raw dir> [<raw dir> ...]

(later raw dirs add pages / richer variants). Writes <out>/ui-dump.json, <out>/shots/*.png and <out>/shots/SOURCE.txt
(the commit the shots are of: build.py uses the screenshots only with a dump of that commit); --gz also writes the dump
gzipped (no timestamp in the header, so the same dump gives the same bytes) for committing with the tool. Python 3
standard library; Pillow, when installed, re-compresses the PNGs (about 30% smaller), else they are copied."""
import argparse, datetime, glob, gzip, io, json, os, re, shutil, sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))

SEG = {
    'main': 'Main menu', 'join': 'Join', 'host': 'Host', 'vsbots': 'Vs bots', 'settings': 'Settings', 'leaderboard': 'Leaderboard',
    'info': 'Info', 'demos': 'Demos', 'custom': 'Custom mode', 'connecting': 'Connecting', 'ingame': 'In game', 'pause': 'Pause menu',
    'controls': 'Controls', 'crosshair': 'Crosshair', 'hud': 'HUD', 'widgets': 'HUD widget editor', 'files': 'Files',
    'values': 'Widget values', 'elements': 'Elements', 'visuals': 'Visuals', 'vote': 'Vote', 'spec': 'Spectate vote', 'queue': 'Queue',
    'chat': 'Chat', 'console': 'Console', 'loading': 'Loading', 'editor': 'Map editor', 'editor-online': 'Map editor (online)',
    'groups': 'Groups', 'keep-edits': 'Keep edits?', 'export-ask': 'Export?', 'save-copy': 'Save a copy?', 'race-stats': 'Race stats',
    'general': 'General', 'weapons': 'Weapons', 'maps': 'Maps', 'movement': 'Movement', 'modes': 'Modes', 'hosting': 'Hosting',
    'dash': 'Dash + grapple', 'advanced': 'Advanced', 'gameplay': 'Gameplay', 'video': 'Video', 'fonts': 'Fonts', 'audio': 'Audio',
    'scores': 'Scoreboard', 'perf': 'Perf overlay (F3)', 'perfmini': 'Mouse to photon (F4)',
    'picker:maps': 'Map picker', 'picker:modes': 'Mode picker', 'picker:livemap': 'Change map', 'picker:livemode': 'Change mode',
    'picker:variants': 'Variants', 'picker:tmaps': 'Tourney maps', 'picker:tmodes': 'Tourney rulesets', 'picker:ladders': 'Ranked ladders',
}
INTERACTIVE = ('button', 'tab', 'toggle', 'cycle', 'slider', 'text', 'colorpicker', 'dropdown', 'key', 'handle', 'view', 'custom')


def title(path, ctx, tag):
    t = ' > '.join(SEG.get(s, s.replace('-', ' ').capitalize()) for s in path.split('/'))
    if ctx: t += ' (' + ctx + ')'
    if tag: t += ' [' + tag + ']'
    return t


def norm(e):
    k = e['kind']
    if k == 'drawtext': e['kind'] = 'label'; e['raw'] = True
    elif k == 'monotext': e['kind'] = 'label'; e['raw'] = True; e['mono'] = True
    if e['kind'] == 'label' and 'align' not in e: e['align'] = 'left'
    for f in ('x', 'y', 'w', 'h', 'cx', 'maxW'):
        if f in e: e[f] = round(e[f], 2)
    return e


def sig(d):   # what the page is made of: its interactive widgets
    s = set()
    for e in d['elements']:
        if e['kind'] in INTERACTIVE and not e.get('muted'):
            s.add(e['kind'] + ':' + (e.get('id') or ('' if e['kind'] == 'text' else e.get('label', ''))) + ':' + e.get('action', ''))
    return s


PRIO = ['menus', 'host-ffa', 'host-vs', 'host-tourney', 'host-ranked', 'client-spectator', 'demo', 'edit-host', 'edit-guest',
        'editor-builtin', 'hide-seeker', 'hide-rps']
def score(d): return (len(sig(d)), -PRIO.index(d['scenario']) if d['scenario'] in PRIO else -99, len(d['elements']))
HUD = ('ingame', 'ingame/scores')
def family(path, sc):   # the in-game HUD differs by situation: those scenarios get pages of their own
    return sc if path in HUD and sc in ('host-vs', 'hide-rps', 'hide-seeker', 'client-spectator', 'demo') else ''


def save_png(src, dst):
    try:
        from PIL import Image   # (optional)
        Image.open(src).save(dst, optimize=True)
    except ImportError:
        shutil.copyfile(src, dst)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('raw', nargs='+', help='raw capture folders (raw/<n>.json + .png)')
    ap.add_argument('--out', default=HERE, help='where ui-dump.json and shots/ go')
    ap.add_argument('--commit', default='?', help='the game commit the captures are of')
    ap.add_argument('--version', default='?', help="the game's VERSION")
    ap.add_argument('--repo', default='floration/quark (testing)')
    ap.add_argument('--gz', help='also write the dump here, gzipped (data/ui-dump.json.gz)')
    a = ap.parse_args(argv)

    raws = []
    for rd in a.raw:
        for f in sorted(glob.glob(os.path.join(rd, '*.json'))):
            with open(f, encoding='utf-8') as fh:
                d = json.load(fh)
            d['_png'] = f[:-5] + '.png'
            d['path'] = d['path'].replace('#', '/')
            raws.append(d)
    if not raws:
        sys.exit('build_dump: no captures in ' + ', '.join(a.raw))

    groups = {}
    for d in raws: groups.setdefault((d['path'], d['ctx']), []).append(d)

    shots = os.path.join(a.out, 'shots')
    if os.path.isdir(shots): shutil.rmtree(shots)
    os.makedirs(shots)
    pages = []
    for (path, ctx), vs in sorted(groups.items()):
        if path.startswith('ingame/perf') and any(d['scenario'] == 'host-ffa' for d in vs):   # (the overlay is the same everywhere)
            vs = [d for d in vs if d['scenario'] == 'host-ffa']
        vs.sort(key=score, reverse=True)
        vs.sort(key=lambda d: family(path, d['scenario']) != '')   # (the plain situation first: its page has no tag)
        clusters = []   # [rep, [scenarios]]
        for d in vs:
            s = sig(d)
            for c in clusters:
                r = sig(c[0])
                if family(path, d['scenario']) != family(path, c[0]['scenario']): continue
                if (len(s & r) / max(1, len(s | r))) >= 0.6 or (not s and not r): c[1].append(d['scenario']); break
            else: clusters.append([d, [d['scenario']]])
        for i, (rep, scs) in enumerate(clusters):
            tag = family(path, rep['scenario']) or ('' if i == 0 else rep['scenario'])
            pid = path + (' [' + ctx + ']' if ctx else '') + (' [' + tag + ']' if tag else '')
            fn = re.sub(r'[^A-Za-z0-9._-]+', '_', pid.replace('/', '__')).strip('_')
            shot = None
            if os.path.exists(rep['_png']):
                shot = 'shots/' + fn + '.png'
                save_png(rep['_png'], os.path.join(a.out, shot))
            els = [norm(dict(e)) for e in rep['elements']]
            pages.append({
                'id': pid, 'title': title(path, ctx, tag), 'path': path, 'ctx': ctx, 'scenarios': sorted(set(scs)),
                'inGame': rep['inGame'], 'editor': rep['editor'],
                'background': 'map editor 3D view' if rep['editor'] else '3D game view' if rep['inGame'] else
                              'menu backdrop (the real game flies over a baked map; the UI test skips it: full-screen dark rects)',
                'screenshot': shot, 'accent': rep['accent'], 'enemy': rep.get('enemy'), 'elements': els,
            })
    with open(os.path.join(shots, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(a.commit + '\nscreenshots of the pages of ui-dump.json, taken by extract.py from the game at that commit\n')
    total = sum(os.path.getsize(os.path.join(shots, f)) for f in os.listdir(shots))

    with open(os.path.join(HERE, 'style.json'), encoding='utf-8') as f:
        style = json.load(f)
    first = raws[0]
    for k in ('textScale', 'hintScale', 'scale'):
        if k in first: style[k] = first[k]
    if first.get('font'): style.setdefault('font', {})['id'] = first['font']
    now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()
    dump = {'source': {'repo': a.repo, 'commit': a.commit, 'version': a.version, 'extracted': now, 'width': 1280, 'height': 720,
                       'how': 'quark --uitest with the extract probe (tools/ui_customizer/extract/probe.patch); elements in draw order, '
                              'pixels at 1280x720'},
            'style': style, 'pages': pages}
    text = json.dumps(dump, ensure_ascii=False, separators=(',', ':'))
    with open(os.path.join(a.out, 'ui-dump.json'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    if a.gz:
        os.makedirs(os.path.dirname(os.path.abspath(a.gz)), exist_ok=True)
        buf = io.BytesIO()
        with gzip.GzipFile(filename='', mode='wb', fileobj=buf, compresslevel=9, mtime=0) as g:
            g.write(text.encode('utf-8'))
        with open(a.gz, 'wb') as f:
            f.write(buf.getvalue())
    c = Counter(e['kind'] for p in pages for e in p['elements'])
    print(f"{len(pages)} pages, {sum(c.values())} elements {dict(c)}, dump {len(text) // 1024} KB"
          + (f" ({os.path.getsize(a.gz) // 1024} KB gzipped)" if a.gz else '') + f", shots {total / 1e6:.1f} MB")
    return 0


if __name__ == '__main__':
    sys.exit(main())
