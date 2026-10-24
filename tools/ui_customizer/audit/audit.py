#!/usr/bin/env python3
"""Quark UI layout audit: balance, overlap/clipping, edges, font size, clarity and placement of every UI page.

Usage (Python 3, stdlib only; run from anywhere):
    python audit.py                         # audits the newest dump (Quark/ui-customizer/extract/ui-dump.json, else
                                            #   tools/ui_customizer/data/ui-dump.json.gz) -> <Quark>/ui-customizer/audit.json
    python audit.py layout.json             # audits a layout exported from the customizer (EXPORT LAYOUT button:
                                            #   {"format": "quark-ui-layout", "doc": {"pages": [...]}}) or any dump
    python audit.py layout.json -o out.json [--manual FILE] [--page <id substring>] [--quiet]

Text widths come from the game's baked UI font (src/client/font_ui.h: advances and kerning), or --font <metrics.json>.
The automatic rules are the same for the default dump and for a redesign, so scores can be compared. Hand-written
findings from the screenshot review (tools/manual.json, keyed by page group) are merged only for the default dump
(or with --manual): on a redesigned layout they would describe widgets that may no longer be there.

Coordinates are the dump's: pixels at 1280x720, layout scale s = 1.0. The game scales every length by
s = window height / 720 and draws text at textScale = max(1, round(2 s)), so text grows in steps; the text-fit
check is repeated for the window sizes in RESOLUTIONS with that rounding (a label that fits at 720p can be
shrunk or cut at 1600x900, where text is 1.2x bigger relative to the layout). The 4:3 check flags content outside
the 960 px wide centre band that a 1024x768 window shows of a centred 1280-wide layout.
"""
import argparse, collections, datetime, gzip, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))         # the game checkout
UIC = os.path.normpath(os.path.join(REPO, '..', 'ui-customizer'))     # Quark/ui-customizer (the installed tool)
_LOCAL, _REPO_DUMP = os.path.join(UIC, 'extract', 'ui-dump.json'), os.path.join(HERE, '..', 'data', 'ui-dump.json.gz')
DEF_DUMP = _LOCAL if os.path.isfile(_LOCAL) else os.path.normpath(_REPO_DUMP)
DEF_FONT = os.path.join(REPO, 'src', 'client', 'font_ui.h')
DEF_OUT = os.path.join(UIC, 'audit.json')
DEF_MANUAL = os.path.join(UIC, 'audit-manual.json')   # (hand-written findings, keyed by page group: the owner's, optional)

W0, H0 = 1280.0, 720.0
MIN_TS = 1.3                     # Renderer::kMinTextScale
EDGE_MIN = 24                    # px at 720p: minimum margin from a widget to the screen edge
RESOLUTIONS = [(1280, 720), (1920, 1080), (1024, 768), (1600, 900), (2560, 1440), (2560, 1080), (3440, 1440), (1366, 768)]
INTERACTIVE = ('button', 'toggle', 'cycle', 'slider', 'text', 'custom', 'colorpicker', 'dropdown')
NONLAYOUT = ('key', 'handle', 'view')
CATS = ('balance', 'overlap', 'edges', 'font', 'clarity', 'placement')
SEV_COST = {1: 0.5, 2: 1.5, 3: 3.0}
# Labels a first-time player may not understand (regex on the shown text) -> what it could say instead.
CRYPTIC = [
    (r'CM/360', 'CM/360 -> "CM PER 360" with a hint line (mouse distance for one full turn)'),
    (r'^INTERP\b|INTERP:', 'INTERP -> "NETWORK SMOOTHING" (move to an ADVANCED / NETWORK page)'),
    (r'^PRESENT$|NO VSYNC|^VSYNC', 'present mode cycle shows only "NO VSYNC" without its name: prefix it "VSYNC: OFF"'),
    (r'SOCD|OPPOSITE KEYS', 'SOCD -> "OPPOSITE KEYS (A+D)" with a one-line explanation'),
    (r'GPU MAX CLOCKS', 'GPU MAX CLOCKS -> needs a hint (keeps the GPU at full clock speed: lower latency, more power)'),
    (r'EXCLUSIVE AUDIO', 'EXCLUSIVE AUDIO -> needs a hint (takes the sound card for lower audio latency)'),
    (r'LOW LATENCY', 'LOW LATENCY cycle shows the value without saying what it is: "FRAME QUEUE: LOW LATENCY"'),
    (r'\bUPNP\b', 'UPNP is jargon: "AUTO PORT FORWARD" is enough, put UPNP in the hint'),
    (r'NEXT WEAP|PREV WEAP|WHEEL DN', 'abbreviated action name: spell it out (NEXT WEAPON, WHEEL DOWN)'),
    (r'^KP ', 'KP ENTER -> "NUMPAD ENTER"'),
    (r'^MODE: IN FFA$|^IN FFA$', 'cycle value "IN FFA" has no name: "APPLY IN: FFA"'),
    (r'^(R|G|B): ', 'single-letter R/G/B sliders: fine next to a swatch, but 3 sliders per colour is heavy; a colour picker would be clearer'),
    (r'%\.|\bFMT\b', 'format string visible'),
]

# ---------------------------------------------------------------------------------------------------------------
# font


class Font:
    def __init__(self, path):
        if path.endswith('.h'):   # the game's baked font: kUiAdv[95], kUiKern[95 * 95] (tools/make_font_atlas.py)
            src = open(path, encoding='utf-8').read()
            adv = [float(v.strip().rstrip('f')) for v in re.search(r'kUiAdv\[95\] = \{(.*?)\};', src, re.S).group(1).split(',') if v.strip()]
            kern = [int(v) for v in re.search(r'kUiKern\[95 \* 95\] = \{(.*?)\};', src, re.S).group(1).split(',') if v.strip()]
            m = {'advance': {chr(32 + i): a for i, a in enumerate(adv)},
                 'kernSixteenths': {chr(32 + a) + chr(32 + b): kern[a * 95 + b] for a in range(95) for b in range(95) if kern[a * 95 + b]}}
        else:
            m = json.load(open(path, encoding='utf-8'))
        self.adv = m['advance']
        self.kern = m.get('kernSixteenths', {})
        self.mono = 5.3
        self.default = self.adv.get('?', 4.5)

    def width(self, s, ts, mono=False):
        ts = max(ts, MIN_TS)
        if not s:
            return 0.0
        if mono:
            return len(s) * self.mono * ts
        total = 0.0
        for i, ch in enumerate(s):
            c = ch if ch in self.adv else '?'
            total += self.adv.get(c, self.default)
            if i + 1 < len(s):
                total += self.kern.get(c + s[i + 1], 0) / 16.0
        return total * ts

    def fit(self, s, ts, max_w):
        """Ui::fit: (scale drawn, clipped?)"""
        if not s or self.width(s, ts) <= max_w:
            return ts, False
        t = max(MIN_TS, max_w / (self.width(s, 10.0) * 0.1))
        return t, self.width(s, t) > max_w + 0.01


# ---------------------------------------------------------------------------------------------------------------
# input


def load_pages(path):
    d = json.load(gzip.open(path, 'rt', encoding='utf-8') if path.endswith('.gz') else open(path, encoding='utf-8'))
    kind = 'dump'
    if isinstance(d, dict) and d.get('format') == 'quark-ui-layout':
        kind = 'layout'
        pages = [p for p in d.get('doc', {}).get('pages', []) if not p.get('hidden')]
        source = d.get('source', {})
    elif isinstance(d, dict) and isinstance(d.get('doc'), dict) and 'pages' in d['doc']:
        kind = 'layout'
        pages = [p for p in d['doc']['pages'] if not p.get('hidden')]
        source = d.get('source', {})
    elif isinstance(d, dict) and 'pages' in d:
        pages = [p for p in d['pages'] if not p.get('hidden')]
        source = d.get('source', {})
        if any('uid' in p for p in pages):
            kind = 'layout'
    else:
        raise SystemExit('not a Quark UI dump or layout: ' + path)
    for p in pages:
        for e in p.get('elements', []):
            for g in ('x', 'y', 'w', 'h'):
                try:
                    e[g] = float(e.get(g, 0) or 0)
                except (TypeError, ValueError):
                    e[g] = 0.0
            e['kind'] = str(e.get('kind', 'custom')).lower()
            e['label'] = '' if e.get('label') is None else str(e['label'])
    return kind, source, pages


def group_of(pid):
    g = re.sub(r'\s*\[[^\]]*\]', '', pid).strip()
    g = g.replace('ingame/pause/settings', 'settings')
    g = re.sub(r'^editor-online', 'editor', g)
    g = re.sub(r'^(host|vsbots)/picker:(maps|modes|variants)$', r'picker:\2', g)
    return g


# ---------------------------------------------------------------------------------------------------------------
# geometry helpers


def cround(v):
    """std::round (halves away from zero), not Python's banker's rounding"""
    return math.floor(v + 0.5)


def rect(e):
    return (e['x'], e['y'], e['x'] + e['w'], e['y'] + e['h'])


def inter(a, b):
    w = min(a[2], b[2]) - max(a[0], b[0])
    h = min(a[3], b[3]) - max(a[1], b[1])
    return (w, h) if w > 0 and h > 0 else (0, 0)


def contains(outer, inner, tol=0.5):
    return inner[0] >= outer[0] - tol and inner[1] >= outer[1] - tol and inner[2] <= outer[2] + tol and inner[3] <= outer[3] + tol


def tag(i, e):
    lab = e.get('label') or ''
    if e['kind'] == 'cycle':
        lab = str(e.get('value', lab))
    elif e['kind'] == 'toggle':
        lab = e.get('shown') or lab
    elif e['kind'] == 'text':
        lab = 'field "%s"' % str(e.get('value', ''))[:20]
    return '%d:%s' % (i, (lab[:40] if lab else e['kind']))


def fmt_num(fmt, v):
    try:
        return fmt % float(v)
    except Exception:
        try:
            return str(round(float(v), 2))
        except Exception:
            return str(v)


def shown_text(e):
    """(text, max width at s=1) for a widget that draws fitted text, else None."""
    k = e['kind']
    if k == 'button':
        return e['label'], e['w'] - 8
    if k == 'toggle':
        t = e.get('shown') or (e['label'] + (': ON' if e.get('value') in (True, 1, '1', 'true') else ': OFF'))
        return t, e['w'] - 8
    if k in ('cycle', 'dropdown'):
        v = e.get('value', '')
        opts = e.get('options')
        if isinstance(v, (int, float)) and isinstance(opts, list) and 0 <= int(v) < len(opts):
            v = opts[int(v)]
        elif isinstance(v, str) and v.isdigit() and isinstance(opts, list) and int(v) < len(opts):
            v = opts[int(v)]
        return str(v), e['w'] - 2 * e['h'] - 8
    if k == 'slider':
        return e['label'] + ': ' + fmt_num(e.get('fmt', '%.2f'), e.get('value', 0)), e['w'] - 8
    return None


def is_chrome(e):
    if e['kind'] == 'label' and e['label'] == 'QUARK' and (e.get('textScale') or 0) >= 6:
        return 'title'
    if e['kind'] == 'label' and re.match(r'^V\d+\.\d+', e['label']) and e['y'] > H0 - 40 and e['x'] < 40:
        return 'version'
    if e['kind'] == 'rect' and e['w'] >= W0 * 0.95 and e['h'] >= H0 * 0.95:
        return 'backdrop'
    return None


def label_rect(font, e):
    ts = float(e.get('textScale') or 2)
    tw = font.width(e['label'], ts, bool(e.get('mono')))
    w = max(e['w'], tw) if not e.get('fit') else e['w']
    h = max(e['h'], 8 * max(ts, MIN_TS))
    return (e['x'], e['y'], e['x'] + w, e['y'] + h), tw


def intervals(items, gap):
    """merge [a,b] intervals closer than gap -> list of (a, b, [indices])"""
    out = []
    for a, b, i in sorted(items):
        if out and a <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], b)
            out[-1][2].append(i)
        else:
            out.append([a, b, [i]])
    return out


# ---------------------------------------------------------------------------------------------------------------
# audit of one page


class PageAudit:
    def __init__(self, font, page):
        self.f = font
        self.p = page
        self.els = page.get('elements', [])
        self.found = collections.OrderedDict()   # (type, rule) -> finding
        self.metrics = {}
        self.menu = not page.get('inGame') and not page.get('editor')

    def add(self, typ, rule, sev, elements, detail, suggestion):
        k = (typ, rule)
        if k in self.found:
            f = self.found[k]
            f['severity'] = max(f['severity'], sev)
            for x in elements:
                if x not in f['elements']:
                    f['elements'].append(x)
            f['_n'] += 1
            if detail and detail not in f['_details'] and len(f['_details']) < 6:
                f['_details'].append(detail)
        else:
            self.found[k] = {'type': typ, 'rule': rule, 'severity': sev, 'elements': list(elements), 'detail': detail,
                             'suggestion': suggestion, '_n': 1, '_details': [detail] if detail else []}

    # ---- classification
    def classify(self):
        self.live = []      # (i, e) interactive, not muted
        self.labels = []    # (i, e, rect, textW) not chrome, not muted
        self.panels = []
        self.rects = []
        self.chrome = {}
        # a full-screen rect drawn after other things is a veil (pause menu, dialogs): what is under it is the page
        # behind (the HUD, the editor), audited on its own page, so it is skipped here
        veil = -1
        for i, e in enumerate(self.els):
            if i > 2 and is_chrome(e) == 'backdrop':
                veil = i
        self.metrics['underVeil'] = veil if veil > 0 else 0
        self.under = []     # (i, e) visible things under the veil (HUD), for the show-through check
        for i, e in enumerate(self.els):
            k = e['kind']
            if i < veil:
                if k in ('label', 'panel') and not e.get('muted'):
                    self.under.append((i, e))
                continue
            c = is_chrome(e)
            if c:
                self.chrome.setdefault(c, []).append(i)
                continue
            if e.get('muted'):
                continue
            if k in INTERACTIVE and e['w'] > 0 and e['h'] > 0:
                self.live.append((i, e))
            elif k == 'label' and e['label'].strip():
                r, tw = label_rect(self.f, e)
                self.labels.append((i, e, r, tw))
            elif k == 'panel':
                self.panels.append((i, e))
            elif k == 'rect':
                self.rects.append((i, e))
        self.metrics['interactive'] = len(self.live)
        self.metrics['labels'] = len(self.labels)

    # ---- 1 balance
    def balance(self):
        live = [(i, e) for i, e in self.live if e['h'] < 300]
        if not live:
            return
        rs = [rect(e) for _, e in live] + [rect(e) for _, e in self.panels]
        x0, y0, x1, y1 = min(r[0] for r in rs), min(r[1] for r in rs), max(r[2] for r in rs), max(r[3] for r in rs)
        self.metrics['bbox'] = [round(x0), round(y0), round(x1), round(y1)]
        # a full-height panel docked to the right (map editor) leaves a 3D view on the left: dialogs centre on that
        dock = [pe for pe in self.els if pe['kind'] == 'panel' and pe['h'] >= H0 - 24 and pe['x'] + pe['w'] >= W0 - 1 and pe['x'] > W0 / 2]
        cxref = dock[0]['x'] / 2 if dock else W0 / 2
        if dock:
            rest = [(i, e) for i, e in live if e['x'] + e['w'] <= dock[0]['x']]
            if rest:
                rs2 = [rect(e) for _, e in rest] + [rect(pe) for _, pe in self.panels if pe['x'] + pe['w'] <= dock[0]['x']]
                x0, x1 = min(r[0] for r in rs2), max(r[2] for r in rs2)
        dx = (x0 + x1) / 2 - cxref
        self.metrics['hOffset'] = round(dx, 1)
        modal = self.menu or bool(self.panels)
        self.docked = abs(dx) > 200 or (bool(dock) and not (x1 <= dock[0]['x']))   # docked to a side on purpose
        if self.docked:
            self.metrics['docked'] = 'right' if dx > 0 else 'left'
        if modal and (x1 - x0) < W0 - 60 and not self.docked:
            if abs(dx) > 60:
                self.add('balance', 'h-offset', 3, [], 'content box %d..%d is %+d px off the horizontal centre' % (x0, x1, dx),
                         'centre the content block on x = 640 (move it %+d px)' % (-dx))
            elif abs(dx) > 16:
                self.add('balance', 'h-offset', 2, [], 'content box %d..%d is %+d px off the horizontal centre' % (x0, x1, dx),
                         'centre the content block on x = 640 (move it %+d px)' % (-dx))
        if self.menu:
            top_ref = 150.0 if y0 >= 150 else 0.0        # below the QUARK title when the content starts under it
            top, bot = y0 - top_ref, H0 - y1
            self.metrics['vMargins'] = [round(top), round(bot)]
            if bot >= 0 and top >= 0 and bot - top > 200 and not self.docked:
                self.add('balance', 'v-offset', 2 if bot - top > 350 else 1, [],
                         'content sits %s: %d px free above (below the title), %d px below' % ('high' if top < bot else 'low', top, bot),
                         'move the block %+d px to centre it in the space under the title, or grow it into a fuller panel' % ((bot - top) / 2))
        # panel padding: content inside each panel vs its edges (dead space at the bottom of dialogs)
        for pi, pe in self.panels:
            rp = rect(pe)
            inside = [rect(e) for _, e in live if contains(rp, rect(e), 1)] + [r for _, _, r, _ in self.labels if contains(rp, r, 1)]
            if len(inside) < 2 or pe['h'] >= H0 - 24:
                continue
            pt, pb = min(r[1] for r in inside) - rp[1], rp[3] - max(r[3] for r in inside)
            pl, pr = min(r[0] for r in inside) - rp[0], rp[2] - max(r[2] for r in inside)
            self.metrics.setdefault('panelPadding', []).append({'panel': pi, 'top': round(pt), 'bottom': round(pb), 'left': round(pl), 'right': round(pr)})
            if pb - pt > 24:
                self.add('balance', 'panel-dead-space', 2 if pb - pt > 60 else 1, ['%d:panel' % pi],
                         'panel %d has %d px empty at the bottom but %d px at the top' % (pi, pb, pt),
                         'shrink the panel to its content: %d px padding on every side (h -> %d)' % (max(16, round(pt)), round(pe['h'] - (pb - max(16, pt)))))
            if abs(pl - pr) > 24 and pl < 200 and pr < 200:
                self.add('balance', 'panel-lopsided', 1, ['%d:panel' % pi], 'panel %d padding left %d vs right %d' % (pi, pl, pr),
                         'equal side padding (16 px)')
        # columns
        cols = intervals([(e['x'], e['x'] + e['w'], i) for i, e in live], 10)
        colinfo = []
        byi = dict(live)
        for a, b, idx in cols:
            es = [(i, byi[i]) for i in idx]
            rows = []
            for i, e in sorted(es, key=lambda t: (t[1]['y'], t[1]['x'])):
                if rows and abs(rows[-1]['y'] - e['y']) <= 4:
                    rows[-1]['els'].append((i, e))
                    rows[-1]['x1'] = max(rows[-1]['x1'], e['x'] + e['w'])
                    rows[-1]['x0'] = min(rows[-1]['x0'], e['x'])
                    rows[-1]['h'] = max(rows[-1]['h'], e['h'])
                else:
                    rows.append({'y': e['y'], 'h': e['h'], 'x0': e['x'], 'x1': e['x'] + e['w'], 'els': [(i, e)]})
            colinfo.append({'x0': a, 'x1': b, 'rows': rows, 'top': min(r['y'] for r in rows), 'bottom': max(r['y'] + r['h'] for r in rows)})
        self.metrics['columns'] = [{'x': [round(c['x0']), round(c['x1'])], 'y': [round(c['top']), round(c['bottom'])], 'rows': len(c['rows'])}
                                   for c in colinfo]
        big = [c for c in colinfo if len(c['rows']) >= 4]
        if len(big) >= 2:
            bots = [c['bottom'] for c in big]
            tops = [c['top'] for c in big]
            if max(bots) - min(bots) > 40:
                self.add('balance', 'column-heights', 2, [],
                         'column bottoms differ by %d px (%s)' % (max(bots) - min(bots), ', '.join('%d' % b for b in bots)),
                         'even the columns out: move rows from the long column to the short one, or end both on the same baseline')
            if max(tops) - min(tops) > 24:
                self.add('balance', 'column-tops', 1, [], 'column tops differ by %d px' % (max(tops) - min(tops)),
                         'start every column on the same y')
            ws = [c['x1'] - c['x0'] for c in big]
            if max(ws) - min(ws) > 24:
                self.add('balance', 'column-widths', 1, [], 'column widths differ: %s px' % ', '.join('%d' % w for w in ws),
                         'use one column width for every column of the page')
        for c in colinfo:
            cw = c['x1'] - c['x0']
            if len(c['rows']) < 3 or cw < 120:
                continue
            ragged = []
            for r in c['rows']:
                short = (c['x1'] - r['x1']) + (r['x0'] - c['x0'])
                if short > 12:
                    ragged.append(r)
            if ragged and len(ragged) < len(c['rows']):
                tags = [tag(i, e) for r in ragged for i, e in r['els']]
                sev = 2 if any(len(r['els']) == 1 for r in ragged) else 1
                self.add('balance', 'ragged-rows', sev, tags[:12],
                         '%d of %d rows in the column at x %d..%d do not span its width (lone or short widgets)' % (len(ragged), len(c['rows']), c['x0'], c['x1']),
                         'stretch those rows to the full column width (%d px) or pair them with a neighbour' % cw)
            gaps = []
            for r0, r1 in zip(c['rows'], c['rows'][1:]):
                gaps.append(round(r1['y'] - (r0['y'] + r0['h'])))
            if len(gaps) >= 3:
                gs = sorted(gaps)
                med = gs[len(gs) // 2]
                odd = [g for g in gaps if abs(g - med) > 6 and g < max(3 * med, med + 40)]
                if odd:
                    self.add('balance', 'uneven-gaps', 1, [], 'row gaps in column x %d: %s (median %d)' % (c['x0'], sorted(set(gaps)), med),
                             'use one row gap (e.g. 8 px) and a larger fixed gap (e.g. 20 px + header) between sections')

    # ---- 2 overlap / clipping
    def overlap(self):
        f = self.f
        live = [(i, e) for i, e in self.live]
        for a in range(len(live)):
            ia, ea = live[a]
            ra = rect(ea)
            for b in range(a + 1, len(live)):
                ib, eb = live[b]
                rb = rect(eb)
                w, h = inter(ra, rb)
                if w > 1 and h > 1:
                    if contains(ra, rb) or contains(rb, ra):
                        if ea['kind'] == 'custom' or eb['kind'] == 'custom':
                            continue
                    self.add('overlap', 'widget-overlap', 3, [tag(ia, ea), tag(ib, eb)],
                             '%s and %s overlap by %dx%d px' % (tag(ia, ea), tag(ib, eb), w, h), 'separate them (at least 6 px gap)')
        # labels vs widgets and labels vs labels
        for i, e, r, tw in self.labels:
            for j, w_ in live:
                rw = rect(w_)
                iw, ih = inter(r, rw)
                if iw <= 1 or ih <= 1:
                    continue
                if contains(rw, r):
                    if w_['kind'] in ('custom', 'text'):
                        continue
                    if w_['kind'] == 'slider' and float(e.get('textScale') or 2) <= 1.5:
                        continue
                    self.add('overlap', 'label-on-widget', 2, [tag(i, e), tag(j, w_)],
                             'label "%s" is drawn on top of %s' % (e['label'][:30], tag(j, w_)), 'move the label outside the widget')
                else:
                    self.add('overlap', 'label-into-widget', 2, [tag(i, e), tag(j, w_)],
                             'label "%s" (%.0f px wide) runs into %s by %dx%d px' % (e['label'][:30], tw, tag(j, w_), iw, ih),
                             'shorten it, give it its own row, or leave %d px more room' % (iw + 6))
        # cramped: a label whose line box nearly touches a widget above / below it (0..3 px)
        for i, e, r, tw in self.labels:
            for j, w_ in self.live:
                rw = rect(w_)
                if min(r[2], rw[2]) - max(r[0], rw[0]) <= 0:
                    continue
                g1, g2 = rw[1] - r[3], r[1] - rw[3]
                if 0 <= g1 < 3 or 0 <= g2 < 3:
                    self.add('overlap', 'cramped', 1, [tag(i, e), tag(j, w_)],
                             'label "%s" is %d px from %s' % (e['label'][:30], min(g for g in (g1, g2) if g >= 0), tag(j, w_)),
                             'keep >= 4 px between a caption and its widget, >= 8 px to the next widget')
                    break
        labs = self.labels
        for a in range(len(labs)):
            ia, ea, ra, _ = labs[a]
            for b in range(a + 1, len(labs)):
                ib, eb, rb, _ = labs[b]
                w, h = inter(ra, rb)
                if w > 2 and h > 2:
                    if abs(ra[1] - rb[1]) < 1 and abs((ra[0] + ra[2]) - (rb[0] + rb[2])) < 6:
                        stacked = self.metrics.setdefault('stackedLabels', 0)
                        self.metrics['stackedLabels'] = stacked + 1   # same spot (world-space tags of bots standing together)
                        continue
                    self.add('overlap', 'label-overlap', 2, [tag(ia, ea), tag(ib, eb)],
                             'labels "%s" and "%s" overlap by %dx%d px' % (ea['label'][:24], eb['label'][:24], w, h), 'separate them')
        # HUD lines under a veil are still visible: they must not overlap each other either
        ul = [(i, e, label_rect(self.f, e)[0]) for i, e in getattr(self, 'under', []) if e['kind'] == 'label' and e['label'].strip()]
        for a in range(len(ul)):
            ia, ea, ra = ul[a]
            for b in range(a + 1, len(ul)):
                ib, eb, rb = ul[b]
                w, h = inter(ra, rb)
                if w > 2 and h > 2 and not (abs(ra[1] - rb[1]) < 1 and abs((ra[0] + ra[2]) - (rb[0] + rb[2])) < 6):
                    self.add('overlap', 'label-overlap', 2, [tag(ia, ea), tag(ib, eb)],
                             'HUD labels "%s" and "%s" overlap by %dx%d px' % (ea['label'][:24], eb['label'][:24], w, h), 'stack HUD notices in one list with a fixed line pitch')
        # surfaces (panels, solid boxes) that partly cover each other: two overlays fighting for the same spot
        surf = [(i, e) for i, e in self.panels] + [(i, e) for i, e in self.rects
                if e['w'] >= 40 and e['h'] >= 30 and (e.get('color') or [0, 0, 0, 1])[3] >= 0.3]
        for a in range(len(surf)):
            ia, ea = surf[a]
            ra = rect(ea)
            for b in range(a + 1, len(surf)):
                ib, eb = surf[b]
                rb = rect(eb)
                w, h = inter(ra, rb)
                if w > 2 and h > 2 and not contains(ra, rb, 1) and not contains(rb, ra, 1):
                    self.add('overlap', 'surface-overlap', 1 if ea['kind'] == eb['kind'] == 'panel' else 2, ['%d:%s' % (ia, ea['kind']), '%d:%s' % (ib, eb['kind'])],
                             '%s %d (%d,%d %dx%d) and %s %d (%d,%d %dx%d) partly cover each other (%dx%d px)' % (
                                 ea['kind'], ia, ea['x'], ea['y'], ea['w'], ea['h'], eb['kind'], ib, eb['x'], eb['y'], eb['w'], eb['h'], w, h),
                             'give each overlay its own screen zone, or hide the one underneath while the other is up')
        # the big title behind content drawn after it
        for ti in self.chrome.get('title', []):
            t = self.els[ti]
            rt = rect(t)
            hits = [j for j, e in self.panels + live if j > ti and inter(rt, rect(e))[0] > 1 and inter(rt, rect(e))[1] > 1]
            if hits:
                self.add('overlap', 'title-behind', 2, ['%d:QUARK' % ti] + [tag(j, self.els[j]) for j in hits[:4]],
                         'the QUARK title (y %d..%d) is drawn behind %d panel/widget(s); it shows through the 75%% panel' % (t['y'], t['y'] + t['h'], len(hits)),
                         'hide the title on this page, or start the content below y = %d' % (t['y'] + t['h'] + 16))
        # the HUD under a pause veil showing through a panel drawn over it
        hits = []
        for i, e in getattr(self, 'under', []):
            r = label_rect(self.f, e)[0] if e['kind'] == 'label' else rect(e)
            for j, pe in self.panels:
                w, h = inter(r, rect(pe))
                if w > 2 and h > 2 and pe.get('color', [0, 0, 0, 0.75])[3] < 0.95:
                    hits.append('%d:%s' % (i, e['label'][:20] or e['kind']))
                    break
        if hits:
            self.add('overlap', 'hud-behind-panel', 2, hits[:8],
                     '%d HUD element(s) under the pause veil sit behind the page panel (75%% alpha) and show through it' % len(hits),
                     'keep menu panels clear of the HUD strips (top score bar y 8..72, weapon bar y 646..700) or draw the panel opaque (alpha 0.92+)')
        # fitted text: shrunk / clipped (720p and other window sizes)
        for i, e in live:
            st = shown_text(e)
            if not st or not st[0]:
                continue
            txt, mw = st
            ts, clip = f.fit(txt, 2, mw)
            bad_res, small_res = [], []
            for (rw, rh) in RESOLUTIONS[1:]:
                s = rh / H0
                tsr = max(1, cround(2 * s))
                tsr2, clip2 = f.fit(txt, tsr, mw * s)
                if clip2:
                    bad_res.append('%dx%d' % (rw, rh))
                elif tsr2 / s < 1.75 and ts >= 1.75:      # relative to the layout: shrinks visibly only at that size
                    small_res.append('%dx%d (%.2f)' % (rw, rh, tsr2 / s))
            if small_res and not bad_res:
                self.add('overlap', 'shrunk-at-other-sizes', 1, [tag(i, e)],
                         '"%s" fits at 720p but is shrunk at %s (text grows in whole steps: x1.2 at 900p)' % (txt[:40], ', '.join(small_res)),
                         'leave ~20%% spare width in every text box (here %d px)' % math.ceil(f.width(txt, 2) * 1.2 + 8))
            if clip:
                self.add('overlap', 'text-clipped', 3, [tag(i, e)], '"%s" is cut with "..." (needs %.0f px, has %.0f)' % (txt[:40], f.width(txt, 2), mw),
                         'widen the widget to %d px or shorten the text' % math.ceil(f.width(txt, 2) + 8))
            elif ts < 1.75:
                self.add('overlap', 'text-shrunk', 2, [tag(i, e)], '"%s" shrunk to %.2f (normal 2) to fit %.0f px' % (txt[:40], ts, mw),
                         'widen to %d px or shorten the text' % math.ceil(f.width(txt, 2) + 8))
            elif ts < 2:
                self.add('overlap', 'text-shrunk-slightly', 1, [tag(i, e)], '"%s" shrunk to %.2f' % (txt[:40], ts),
                         'give it %d px more width' % math.ceil(f.width(txt, 2) - mw))
            if bad_res and not clip:
                self.add('overlap', 'clipped-at-other-sizes', 2, [tag(i, e)], '"%s" fits at 720p but is cut at %s (text grows in whole steps)' % (txt[:40], ', '.join(bad_res)),
                         'leave ~20%% spare width in every text box (here %d px)' % math.ceil(f.width(txt, 2) * 1.2 + 8))
        # plain labels are not fitted: text grows in whole steps (x1.2 at 900p, x1.33 for small text at 1080p), so a
        # label that just fits at 720p can run into its neighbour or out of its panel at other window sizes
        for i, e, r, tw in self.labels:
            if e.get('fit') or e.get('mono'):
                continue
            ts0 = float(e.get('textScale') or 2)
            if ts0 >= 6:
                continue
            w720 = f.width(e['label'], ts0)
            for (rw, rh) in RESOLUTIONS[1:]:
                s = rh / H0
                tsr = max(1, cround(2 * s))
                tl = max(MIN_TS, max(1, cround(tsr * ts0 / 2)))
                wr = f.width(e['label'], tl) / s           # width in 720p layout px
                hr = 8 * tl / s
                if wr <= w720 + 2:
                    continue
                if e.get('align') == 'center':
                    cx = e.get('cx', r[0] + w720 / 2)
                    rr = (cx - wr / 2, r[1], cx + wr / 2, r[1] + hr)
                else:
                    rr = (r[0], r[1], r[0] + wr, r[1] + hr)
                hit = None
                for j, w_ in self.live:
                    if inter(rr, rect(w_))[0] > 1 and inter(rr, rect(w_))[1] > 1 and not (inter(r, rect(w_))[0] > 0 and inter(r, rect(w_))[1] > 0):
                        hit = tag(j, w_)
                        break
                if not hit:
                    for j, e2, r2, _ in self.labels:
                        if j != i and inter(rr, r2)[0] > 1 and inter(rr, r2)[1] > 1 and not (inter(r, r2)[0] > 0 and inter(r, r2)[1] > 0):
                            if abs(r2[1] - r[1]) < 1 and 0 <= r2[0] - (r[0] + w720) <= 30:
                                continue    # the next word of a run laid out by measured widths (moves along with it)
                            hit = tag(j, e2)
                            break
                if not hit:
                    for pj, pe in self.panels:
                        rp = rect(pe)
                        if rp[0] <= r[0] <= rp[2] and rp[1] <= r[1] <= rp[3] and r[2] <= rp[2] and rr[2] > rp[2]:
                            hit = 'the panel edge'
                            break
                if hit:
                    self.add('overlap', 'label-grows-at-other-sizes', 2 if ts0 >= 1.5 else 1, [tag(i, e)],
                             'label "%s" fits at 720p but at %dx%d (text x%.2f) runs into %s' % (e['label'][:30], rw, rh, wr / w720, hit),
                             'leave 25%% spare room after every plain label, or draw it with labelFit')
                    break
        for i, e, r, tw in self.labels:
            if e.get('fit') and float(e.get('textScale') or 2) < 2:
                self.add('overlap', 'label-shrunk', 1 if e['textScale'] >= 1.7 else 2, [tag(i, e)],
                         'label "%s" shrunk to %.2f to fit %d px' % (e['label'][:40], e['textScale'], e['w']), 'shorten it or give it more room')
            if e['label'].rstrip().endswith('...') and not e['label'].rstrip().endswith('....'):
                self.add('overlap', 'label-ellipsis', 1, [tag(i, e)], 'label ends in "..." (cut by the code, or a progress ellipsis): "%s"' % e['label'][:50],
                         'check it is not clipping data (an address, a name)')
        # widgets / labels that stick out of the panel they sit in
        for pi, pe in self.panels:
            rp = rect(pe)
            for i, e in live:
                r = rect(e)
                cx, cy = (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
                if rp[0] <= cx <= rp[2] and rp[1] <= cy <= rp[3] and not contains(rp, r, 1):
                    self.add('overlap', 'out-of-panel', 2, [tag(i, e), '%d:panel' % pi], '%s sticks out of its panel' % tag(i, e),
                             'keep 16 px of panel padding around every widget')
            for i, e, r, tw in self.labels:
                cx, cy = (r[0] + min(r[2], r[0] + 40)) / 2, (r[1] + r[3]) / 2
                if rp[0] <= r[0] <= rp[2] and rp[1] <= cy <= rp[3] and r[2] > rp[2] - 4:
                    self.add('overlap', 'label-out-of-panel', 2, [tag(i, e)], 'label "%s" runs past its panel\'s right edge (%.0f > %.0f)' % (e['label'][:30], r[2], rp[2]),
                             'wrap the text or widen the panel')

    # ---- 3 edges
    def edges(self):
        items = [(i, e, rect(e)) for i, e in self.live] + [(i, e, rect(e)) for i, e in self.panels] + [(i, e, r) for i, e, r, _ in self.labels]
        if not items:
            return
        off, near = [], []
        for i, e, r in items:
            if r[0] < -0.5 or r[1] < -0.5 or r[2] > W0 + 0.5 or r[3] > H0 + 0.5:
                off.append((i, e, r))
                continue
            if e['kind'] == 'label' and not self.menu:
                continue
            for m, side in ((r[0], 'left'), (r[1], 'top'), (W0 - r[2], 'right'), (H0 - r[3], 'bottom')):
                if m < EDGE_MIN:
                    near.append((i, e, r, m, side))
        docked = getattr(self, 'docked', False)
        if off:
            self.add('edges', 'off-screen', 3, [tag(i, e) for i, e, r in off[:10]],
                     '%d element(s) extend past the screen: ' % len(off) + '; '.join('%s to (%d,%d)-(%d,%d)' % (tag(i, e), r[0], r[1], r[2], r[3]) for i, e, r in off[:3]),
                     'keep everything inside a 24 px safe margin (y + h <= 696 at 720p)')
        for i, e, r, m, side in near:
            sev = 2 if e['kind'] == 'panel' or m < 12 else 1
            if e['kind'] == 'panel' and (docked or not self.menu):
                sev = 1      # a docked side panel / HUD strip flush with the edge is a choice, not an accident
            self.add('edges', 'near-edge-' + side, sev, [tag(i, e)], '%s is %d px from the %s edge' % (tag(i, e), m, side),
                     'keep >= 24 px (panels >= 24 px, ideally 32 px) from every screen edge')
        rs = [r for _, _, r in items]
        self.metrics['edgeMargins'] = {'left': round(min(r[0] for r in rs)), 'top': round(min(r[1] for r in rs)),
                                       'right': round(W0 - max(r[2] for r in rs)), 'bottom': round(H0 - max(r[3] for r in rs))}
        if self.live:
            bot = max(e['y'] + e['h'] for _, e in self.live)
            self.metrics['bottomWidgetGap'] = round(H0 - bot)
        # 4:3 band (1024x768 shows x 160..1120 of a centred layout)
        if self.menu or self.panels:
            out = [(i, e) for i, e in self.live if e['x'] < 160 - 0.5 or e['x'] + e['w'] > 1120 + 0.5]
            outp = [(i, e) for i, e in self.panels if e['x'] < 160 - 0.5 or e['x'] + e['w'] > 1120 + 0.5]
            if out or outp:
                self.add('edges', 'outside-4:3', 1, [tag(i, e) for i, e in (outp + out)[:6]],
                         '%d widget(s)/panel(s) lie outside x 160..1120, the part of a centred 1280 layout a 4:3 window shows; fine only where the code derives that width from the window' % (len(out) + len(outp)),
                         'design the fixed-width content to 912 px (960 - 2 x 24) or derive the width from the window')

    # ---- 4 font
    def font(self):
        sizes = collections.Counter()
        small = []
        for i, e, r, tw in self.labels:
            ts = float(e.get('textScale') or 2)
            sizes[round(max(ts, MIN_TS), 2)] += 1
            if ts < 1.5:
                small.append((i, e))
        for i, e in self.live:
            if shown_text(e):
                sizes[2.0] += 1
        self.metrics['textScales'] = {str(k): v for k, v in sorted(sizes.items())}
        self.metrics['smallLabels'] = len(small)
        if small:
            # blocks of consecutive small lines
            blocks, cur = [], []
            for i, e in sorted(small, key=lambda t: (t[1]['x'], t[1]['y'])):
                if cur and abs(e['x'] - cur[-1][1]['x']) < 2 and 0 < e['y'] - cur[-1][1]['y'] <= 22:
                    cur.append((i, e))
                else:
                    if cur:
                        blocks.append(cur)
                    cur = [(i, e)]
            blocks.append(cur)
            big = max(len(b) for b in blocks)
            self.metrics['helpBlockLines'] = big
            sev = 3 if big >= 8 or len(small) >= 15 else 2 if big >= 4 or len(small) >= 6 else 1
            self.add('font', 'scale-1-text', sev, [tag(i, e) for i, e in small[:8]],
                     '%d label(s) at textScale 1 (drawn at the 1.3 minimum: ~9.75 px font at 720p, cap height 6.4 px); longest block %d lines' % (len(small), big),
                     'use textScale 2 (15 px) for anything a player must read; at most one 1.5-size hint line under a control; move long help to an INFO page or a hover hint')
        roles = collections.defaultdict(set)
        for i, e, r, tw in self.labels:
            ts = float(e.get('textScale') or 2)
            lab = e['label']
            if lab.isupper() and len(lab) < 30 and ts >= 1.5 and not any(inter(r, rect(w))[0] > 0 for _, w in self.live):
                roles['caption/header'].add(round(ts, 2))
        for role, ss in roles.items():
            if len(ss) > 2:
                self.add('font', 'mixed-header-sizes', 1, [], '%s labels use %d sizes: %s' % (role, len(ss), sorted(ss)),
                         'one size per role: page title 3, section header 2 (accent colour), caption 2 grey, hint 1.5')
        if len(sizes) > 3:
            self.add('font', 'many-sizes', 1, [], '%d distinct text sizes on one page: %s' % (len(sizes), sorted(sizes)),
                     'limit a page to 3 sizes')

    # ---- 5 clarity
    def clarity(self):
        n = len(self.live)
        if n > 50:
            self.add('clarity', 'too-many-controls', 3, [], '%d interactive widgets on one page' % n, 'split into sub-pages / tabs of <= 20 controls')
        elif n > 30:
            self.add('clarity', 'too-many-controls', 2, [], '%d interactive widgets on one page' % n, 'split into sub-pages / tabs of <= 20 controls')
        headers = []
        for i, e, r, tw in self.labels:
            ts = float(e.get('textScale') or 2)
            if ts >= 2 and e['label'].isupper():
                headers.append(i)
        self.metrics['headerLabels'] = len(headers)
        if n >= 14 and len(headers) < max(1, n // 14):
            self.add('clarity', 'no-sections', 2, [], '%d controls but only %d caption/header label(s): no visible grouping' % (n, len(headers)),
                     'add section headers (accent colour, textScale 2, 8 px above the group, 20 px gap before it)')
        seen = set()
        for i, e in self.live:
            st = shown_text(e)
            txt = (st[0] if st else e['label']) or ''
            for pat, why in CRYPTIC:
                if re.search(pat, txt) and pat not in seen:
                    seen.add(pat)
                    self.add('clarity', 'cryptic-label', 1, [tag(i, e)], why, 'rename and add a one-line hint next to the control')
        for i, e, r, tw in self.labels:
            for pat, why in CRYPTIC:
                if re.search(pat, e['label']) and pat not in seen:
                    seen.add(pat)
                    self.add('clarity', 'cryptic-label', 1, [tag(i, e)], why, 'rename and add a one-line hint next to the control')
        # help text far from the control it explains: small text blocks whose nearest widget is > 60 px away vertically
        far = []
        smalls = sorted([(r[1], i) for i, e, r, tw in self.labels if float(e.get('textScale') or 2) < 1.5])
        def in_block(y):     # part of a run of >= 4 small lines: a text page / help block, not a hint
            ys = [yy for yy, _ in smalls]
            return sum(1 for yy in ys if abs(yy - y) <= 3 * 16) >= 5
        for i, e, r, tw in self.labels:
            if float(e.get('textScale') or 2) >= 1.5 or len(e['label']) < 12 or in_block(r[1]):
                continue
            d = min([abs(r[1] - (w['y'] + w['h'])) for _, w in self.live] + [abs(w['y'] - r[3]) for _, w in self.live] + [999])
            if d > 60:
                far.append((i, e, d))
        if len(far) >= 1 and self.live:
            self.add('clarity', 'hint-far-from-control', 1, [tag(i, e) for i, e, d in far[:6]],
                     '%d small hint line(s) sit more than 60 px from any control' % len(far),
                     'put each hint directly under the control it explains (4 px gap), or make it a hover hint')
        dis = [(i, e) for i, e in self.live if e.get('enabled') is False]
        if dis:
            self.metrics['disabled'] = len(dis)

    # ---- 6 placement (automatic part; the settings tree is hand-made in manual.json / settings-tree.json)
    def placement(self, group):
        n = len(self.live)
        if group.startswith('settings') and n > 20:
            self.add('placement', 'settings-overloaded', 3 if n > 40 else 2, [], '%d controls on one settings page' % n,
                     'split by topic (see settings-tree.json): VIDEO / AUDIO / GAMEPLAY / NETWORK / PLAYER, advanced options behind ADVANCED')
        acts = collections.Counter(str(e.get('action', '')) for _, e in self.live)
        navs = [e for _, e in self.live if str(e.get('action', '')).startswith('nav:')]
        if len(navs) >= 4 and n - len(navs) >= 12:
            self.add('placement', 'mixed-nav-and-options', 1, [tag(i, e) for i, e in self.live if str(e.get('action', '')).startswith('nav:')][:6],
                     'navigation buttons mixed into a page of %d options' % (n - len(navs)),
                     'put sub-page navigation in a tab row at the top of the panel, BACK alone at the bottom')

    def run(self, group):
        self.classify()
        self.balance()
        self.overlap()
        self.edges()
        self.font()
        self.clarity()
        self.placement(group)


def finish(findings):
    out = []
    for f in findings:
        f = dict(f)
        n = f.pop('_n', 1)
        ds = f.pop('_details', [])
        if n > 1 and len(ds) > 1:
            f['detail'] = '%d cases, e.g. ' % n + ' | '.join(ds[:4])
        out.append(f)
    out.sort(key=lambda f: (-f['severity'], CATS.index(f['type']) if f['type'] in CATS else 9))
    return out


def scores(findings):
    sc = {}
    for c in CATS:
        cost = sum(SEV_COST.get(f['severity'], 1) for f in findings if f['type'] == c)
        sc[c] = max(0.0, round((10 - cost) * 2) / 2)
    return sc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('input', nargs='?', default=DEF_DUMP, help='ui-dump.json or a layout exported from the customizer')
    ap.add_argument('-o', '--out', default=None, help='output json (default: Quark/ui-customizer/audit.json for the dump, <input>.audit.json otherwise)')
    ap.add_argument('--font', default=DEF_FONT)
    ap.add_argument('--manual', dest='manual', action='store_true', default=None, help='merge Quark/ui-customizer/audit-manual.json findings')
    ap.add_argument('--no-manual', dest='manual', action='store_false')
    ap.add_argument('--page', default=None, help='only pages whose id contains this')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    font = Font(a.font)
    kind, source, pages = load_pages(a.input)
    is_default = kind == 'dump' and os.path.abspath(a.input) == os.path.abspath(DEF_DUMP)
    use_manual = a.manual if a.manual is not None else is_default
    out_path = a.out or (DEF_OUT if is_default else os.path.splitext(a.input)[0] + '.audit.json')
    manual = {}
    if use_manual and os.path.exists(DEF_MANUAL):
        manual = json.load(open(DEF_MANUAL, encoding='utf-8')).get('groups', {})
    res = []
    groups = collections.OrderedDict()
    for p in pages:
        pid = str(p.get('id') or p.get('title') or '?')
        if a.page and a.page not in pid:
            continue
        g = group_of(pid)
        groups.setdefault(g, []).append(pid)
        pa = PageAudit(font, p)
        pa.run(g)
        fs = finish(pa.found.values())
        m = manual.get(g)
        if m:
            for mf in m.get('findings', []):
                only = mf.get('pages')
                if only and not any(o == pid or (o.endswith('*') and pid.startswith(o[:-1])) for o in only):
                    continue
                x = {k: v for k, v in mf.items() if k != 'pages'}
                x.setdefault('rule', 'review')
                x['source'] = 'review'
                fs.append(x)
            fs.sort(key=lambda f: (-f['severity'], CATS.index(f['type']) if f['type'] in CATS else 9))
        res.append({'id': pid, 'group': g, 'title': p.get('title', pid), 'inGame': bool(p.get('inGame')), 'editor': bool(p.get('editor')),
                    'scores': scores(fs), 'metrics': pa.metrics, 'findings': fs})
    for r in res:
        r['scores']['overall'] = round(sum(r['scores'][c] for c in CATS) / len(CATS), 1)
    reps = {}
    for g, ids in groups.items():
        plain = [i for i in ids if '[' not in i and not i.startswith('ingame/pause/settings') and not i.startswith('editor-online')]
        reps[g] = (plain or ids)[0]
    for r in res:
        r['representative'] = reps[r['group']] == r['id']
    doc = {
        'tool': 'tools/ui_customizer/audit/audit.py',
        'generated': datetime.datetime.now().isoformat(timespec='seconds'),
        'input': os.path.abspath(a.input), 'inputKind': kind, 'source': source,
        'manualFindings': bool(manual),
        'rules': {'screen': [W0, H0], 'edgeMin': EDGE_MIN, 'minTextScale': MIN_TS, 'resolutions': ['%dx%d' % r for r in RESOLUTIONS],
                  'scoring': '10 minus 0.5 / 1.5 / 3 per finding of severity 1 / 2 / 3 in that category (similar findings are merged), floor 0'},
        'groups': {g: {'representative': reps[g], 'pages': ids} for g, ids in groups.items()},
        'pages': res,
    }
    with open(out_path, 'w', encoding='utf-8') as fo:
        json.dump(doc, fo, indent=1)
    if not a.quiet:
        worst = sorted(res, key=lambda r: r['scores']['overall'])
        print('%d pages, %d groups -> %s' % (len(res), len(groups), out_path))
        for r in worst[:15]:
            s = r['scores']
            print('  %-48s overall %4.1f  %s' % (r['id'][:48], s['overall'], ' '.join('%s %g' % (c[:3], s[c]) for c in CATS)))


if __name__ == '__main__':
    main()
