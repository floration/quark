/* Board > Flow graph (SVG): threads -> entries -> branches / PRs -> versions, with state colours.
   Readable at ~20 threads / ~150 entries: unfinished only by default, entries without GitHub links folded into
   one row per thread, and every thread can be collapsed to one row. */
'use strict';
(function (DM) {
  const {h, s} = DM;
  const st = DM.state;
  const ROW = 22, GAP_G = 10, TOP = 30;
  const GH_KINDS = new Set(['pr', 'branch', 'version', 'release', 'run']);

  function trunc(t, px, fs) {
    const max = Math.max(4, Math.floor(px / ((fs || 11.5) * 0.56)));
    t = String(t || '');
    return t.length > max ? t.slice(0, max - 1) + '…' : t;
  }
  function wrap(t, px, maxLines) {
    const per = Math.max(4, Math.floor(px / (12 * 0.56)));
    const words = String(t).split(/\s+/), lines = [];
    let cur = '';
    for (const w of words) {
      if ((cur + ' ' + w).trim().length <= per) cur = (cur + ' ' + w).trim();
      else { if (cur) lines.push(cur); cur = w; }
    }
    if (cur) lines.push(cur);
    if (lines.length > maxLines) { const keep = lines.slice(0, maxLines); keep[maxLines - 1] = trunc(lines.slice(maxLines - 1).join(' '), px, 12); return keep; }
    return lines.map(l => trunc(l, px, 12));
  }
  function verOf(l) { const m = /(\d+\.\d+\.\d+)/.exec(l.label || '') || /(\d+\.\d+\.\d+)/.exec(l.url || ''); return m ? m[1] : null; }

  function build() {
    const p = st.prefs.flow, d = st.data, gh = d.github;
    const prByNum = new Map(gh.prs.map(x => [+x.number, x]));
    const prByBranch = new Map();
    for (const x of gh.prs) if (x.headRefName) { const o = prByBranch.get(x.headRefName); if (!o || +x.number > +o.number) prByBranch.set(x.headRefName, x); }
    const runById = new Map(gh.runs.map(r => [String(r.id), r]));
    const lastRun = new Map();
    for (const r of gh.runs) { const o = lastRun.get(r.branch); if (!o || Date.parse(r.createdAt) > Date.parse(o.createdAt)) lastRun.set(r.branch, r); }
    const runState = r => !r ? null : r.status && r.status !== 'completed' ? 'in_progress' : r.conclusion === 'success' ? 'success' : r.conclusion ? 'failure' : null;

    const mids = new Map(), vers = new Map();
    function midFor(l) {
      let key, node;
      if (l.kind === 'pr') {
        const n = +((/\/pull\/(\d+)/.exec(l.url || '') || /#(\d+)/.exec(l.label || '') || [])[1]);
        if (!n) return null;
        key = 'pr:' + n;
        if (!mids.has(key)) {
          const pr = prByNum.get(n);
          const state = pr ? (pr.merged ? 'merged' : String(pr.state || '').toLowerCase()) : (l.state || 'open');
          const ci = pr && pr.checks ? ({FAILURE: 'failure', ERROR: 'failure', PENDING: 'in_progress', SUCCESS: 'success'}[String(pr.checks).toUpperCase()] || null) :
            pr && pr.headRefName ? runState(lastRun.get(pr.headRefName)) : null;
          mids.set(key, {key, kind: 'pr', label: 'PR #' + n, sub: pr ? pr.title : '', state, ci, url: (pr && pr.url) || l.url, branch: pr && pr.headRefName});
        }
        return mids.get(key);
      }
      let br = null;
      if (l.kind === 'branch') br = String(l.label || (/\/tree\/(.+)$/.exec(l.url || '') || [])[1] || '').replace(/^origin\//, '');
      if (l.kind === 'run') { const id = (/\/runs\/(\d+)/.exec(l.url || '') || [])[1]; const r = id && runById.get(id); br = r && r.branch; if (!br) return null; }
      if (!br) return null;
      const pr = prByBranch.get(br);
      if (pr) return midFor({kind: 'pr', url: pr.url, label: 'PR #' + pr.number});
      key = 'br:' + br;
      if (!mids.has(key)) mids.set(key, {key, kind: 'branch', label: br, sub: 'branch', state: 'branch', ci: runState(lastRun.get(br)) || (l.kind === 'branch' ? l.state : null),
        url: l.kind === 'branch' ? l.url : 'https://github.com/floration/quark/tree/' + br, branch: br});
      return mids.get(key);
    }
    function verFor(l) {
      const v = verOf(l);
      if (!v) return null;
      if (!vers.has(v)) {
        const prod = gh.releases.find(r => r.tag === 'v' + v);
        const test = gh.releases.filter(r => String(r.tag || '').startsWith('test-' + v + '-')).sort((a, b) => Date.parse(b.publishedAt || 0) - Date.parse(a.publishedAt || 0))[0];
        const state = prod ? 'prod' : test ? 'testing' : 'local';
        vers.set(v, {key: 'v:' + v, kind: 'version', label: v, state, url: (prod || test || {}).url || l.url,
          sub: prod ? 'production' : test ? 'testing build ' + String(test.tag).replace(/^test-/, '') : 'not released yet'});
      }
      return vers.get(v);
    }

    const entries = d.entries.filter(e => p.showDone || !DM.isDone(e));
    const groups = new Map();
    for (const e of entries) {
      const t = e.thread || '(no thread)';
      if (!groups.has(t)) groups.set(t, {name: t, entries: [], linked: [], unlinked: []});
      const g = groups.get(t);
      g.entries.push(e);
      (e.links.some(l => GH_KINDS.has(l.kind)) ? g.linked : g.unlinked).push(e);
    }
    const last = e => Date.parse((e.lastChanged || {}).time || '') || 0;
    const gl = [...groups.values()].sort((a, b) => (b.linked.length > 0) - (a.linked.length > 0) || Math.max(0, ...b.entries.map(last)) - Math.max(0, ...a.entries.map(last)));

    const rows = []; // {g, kind: entry|more|summary, entries[], y}
    const tnodes = [];
    let y = TOP;
    for (const g of gl) {
      const shown = p.linkedOnly ? g.linked : g.entries;
      const collapsed = shown.length > 0 && (p.collapsed[g.name] != null ? p.collapsed[g.name] : shown.length > 8);
      const top = y;
      const sorted = shown.slice().sort((a, b) => (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9) || last(b) - last(a));
      if (collapsed) rows.push({g, kind: 'summary', entries: shown, y: y + ROW / 2}), y += ROW;
      else {
        for (const e of sorted) rows.push({g, kind: 'entry', entries: [e], y: y + ROW / 2}), y += ROW;
        const hidden = p.linkedOnly ? g.unlinked.length : 0;
        if (hidden) rows.push({g, kind: 'more', entries: g.unlinked, y: y + ROW / 2}), y += ROW;
      }
      if (y === top) { rows.push({g, kind: 'more', entries: g.unlinked, y: y + ROW / 2}); y += ROW; }
      tnodes.push({g, top, bottom: y, collapsed});
      y += GAP_G;
    }
    // edges
    const edges = []; // {a, b, rows:Set}
    const edgeMap = new Map();
    const addEdge = (a, b, row) => {
      const k = a.key + '>' + b.key;
      let ed = edgeMap.get(k);
      if (!ed) edgeMap.set(k, ed = {a, b, rows: new Set()}), edges.push(ed);
      ed.rows.add(row);
    };
    rows.forEach((r, i) => {
      r.key = 'row:' + i;
      r.mids = new Set(); r.vers = new Set();
      if (r.kind === 'more') return;
      for (const e of r.entries) {
        const ms = [], vs = [];
        for (const l of e.links) {
          if (l.kind === 'pr' || l.kind === 'branch' || l.kind === 'run') { const m = midFor(l); if (m && !ms.includes(m)) ms.push(m); }
          if (l.kind === 'version' || l.kind === 'release') { const v = verFor(l); if (v && !vs.includes(v)) vs.push(v); }
        }
        for (const m of ms) { addEdge(r, m, r); r.mids.add(m); }
        for (const v of vs) {
          r.vers.add(v);
          if (ms.length) for (const m of ms) addEdge(m, v, r); else addEdge(r, v, r);
        }
      }
    });
    // place middle and version columns by barycenter
    const place = (nodes, baryOf) => {
      const list = [...nodes].map(n => ({n, b: baryOf(n)})).sort((a, b) => a.b - b.b);
      let prev = TOP - 6;
      for (const it of list) { it.n.y = Math.max(prev + ROW + 6, it.b); prev = it.n.y; }
    };
    const mean = a => a.length ? a.reduce((x, y) => x + y, 0) / a.length : TOP;
    place(mids.values(), m => mean(rows.filter(r => r.mids.has(m)).map(r => r.y)));
    place(vers.values(), v => mean(edges.filter(e => e.b === v).map(e => e.a.y)));
    const height = Math.max(y, ...[...mids.values(), ...vers.values()].map(n => n.y + ROW)) + 10;
    return {rows, tnodes, mids: [...mids.values()], vers: [...vers.values()], edges, height, groups: gl};
  }

  function render(body, tools) {
    const p = st.prefs.flow;
    const G = build();
    tools.append(
      DM.board.check('include finished', p.showDone, v => { p.showDone = v; DM.app.save(); DM.board.redraw(); }),
      DM.board.check('only entries linked to GitHub', p.linkedOnly, v => { p.linkedOnly = v; DM.app.save(); DM.board.redraw(); },
        'Entries with no branch, PR or version are folded into one row per thread'),
      h('button', {class: 'mini', onclick: () => { for (const g of G.groups) p.collapsed[g.name] = false; DM.app.save(); DM.board.redraw(); }}, 'Expand all'),
      h('button', {class: 'mini', onclick: () => { for (const g of G.groups) p.collapsed[g.name] = true; DM.app.save(); DM.board.redraw(); }}, 'Collapse all'),
      h('span', {class: 'legend'},
        h('span', null, h('i', {class: 'sw f-open'}), 'PR open'), h('span', null, h('i', {class: 'sw f-merged'}), 'merged'),
        h('span', null, h('i', {class: 'sw f-closed'}), 'closed'), h('span', null, h('i', {class: 'sw f-fail'}), 'CI failing'),
        h('span', null, h('i', {class: 'sw f-run'}), 'CI running'), h('span', null, h('i', {class: 'sw f-testing'}), 'testing build'),
        h('span', null, h('i', {class: 'sw f-prod'}), 'production'), h('span', null, h('i', {class: 'sw f-local'}), 'not released'),
        h('span', {title: 'The coloured bar on each entry is its status, same colours as the Overview'}, h('i', {class: 'sw stripe-key'}), 'entry bar = its status')));
    if (!G.rows.length) { body.appendChild(h('div', {class: 'empty'}, 'No entries to show with these options.')); return; }

    const box = h('div', {class: 'flow'});
    body.appendChild(box);
    const W = Math.max(940, box.clientWidth || 1200);
    const cw = {t: 190, e: Math.min(360, Math.max(260, W * 0.27)), m: 220, v: 150};
    const gap = Math.max(50, (W - 16 - cw.t - cw.e - cw.m - cw.v) / 3);
    const X = {t: 8, e: 8 + cw.t + Math.min(gap, 40)};
    X.m = X.e + cw.e + gap; X.v = X.m + cw.m + gap;
    const Wt = X.v + cw.v + 8;
    const svg = s('svg', {class: 'flow-svg', width: Wt, height: G.height, viewBox: '0 0 ' + Wt + ' ' + G.height, role: 'img',
      'aria-label': 'Flow from threads to entries to branches and PRs to versions'});
    box.appendChild(svg);
    // column headers
    [['Threads', X.t], ['Entries' + (p.showDone ? '' : ' (unfinished)'), X.e], ['Branches and PRs', X.m], ['Versions', X.v]]
      .forEach(([t, x]) => svg.appendChild(s('text', {x, y: 14, class: 'fl-col', text: t})));

    const edgeLayer = s('g', {class: 'fl-edges'}), nodeLayer = s('g');
    svg.append(edgeLayer, nodeLayer);
    const xr = n => n.kind === 'version' ? X.v : n.kind === 'pr' || n.kind === 'branch' ? X.m + cw.m : X.e + cw.e;
    const xl = n => n.kind === 'version' ? X.v : n.kind === 'pr' || n.kind === 'branch' ? X.m : X.e;
    const curve = (x1, y1, x2, y2) => { const mx = (x1 + x2) / 2; return `M${x1},${y1} C${mx},${y1} ${mx},${y2} ${x2},${y2}`; };
    const edgeEls = [];
    for (const ed of G.edges) {
      const x1 = xr(ed.a), x2 = xl(ed.b);
      const cls = ed.b.kind === 'version' ? 'fl-e v' : 'fl-e';
      const el = s('path', {d: curve(x1, ed.a.y, x2, ed.b.y), class: cls});
      el._rows = ed.rows; edgeEls.push(el);
      edgeLayer.appendChild(el);
    }
    // thread bands + connectors
    const rowEls = [];
    for (const tn of G.tnodes) {
      const hgt = tn.bottom - tn.top - 2;
      const g = s('g', {class: 'fl-thread' + (tn.collapsed ? ' collapsed' : ''), tabindex: 0, role: 'button'});
      g.appendChild(s('rect', {x: X.t, y: tn.top, width: cw.t, height: hgt, rx: 4}));
      const name = tn.g.name === '(no thread)' ? 'no thread' : tn.g.name;
      const maxLines = Math.max(1, Math.min(3, Math.floor((hgt - 6) / 15) - (hgt > ROW * 2.5 ? 1 : 0)));
      const lines = wrap(name, cw.t - 26, maxLines);
      lines.forEach((ln, i) => g.appendChild(s('text', {x: X.t + 18, y: tn.top + 15 + i * 15, class: 'fl-tt', text: ln})));
      g.appendChild(s('text', {x: X.t + 6, y: tn.top + 15, class: 'fl-tog', text: tn.collapsed ? '▸' : '▾'}));
      if (hgt > ROW * 2.5) g.appendChild(s('text', {x: X.t + 18, y: tn.top + 15 + lines.length * 15, class: 'fl-ts', text: tn.g.entries.length + ' entries'}));
      g.appendChild(s('title', {text: name + '\n' + tn.g.entries.length + ' entries (' + tn.g.linked.length + ' linked to GitHub)\nClick to ' + (tn.collapsed ? 'expand' : 'collapse')}));
      const toggle = () => { st.prefs.flow.collapsed[tn.g.name] = !tn.collapsed; DM.app.save(); DM.board.redraw(); };
      g.addEventListener('click', toggle);
      g.addEventListener('keydown', ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); toggle(); } });
      g._rows = new Set(G.rows.filter(r => r.g === tn.g));
      hover(g, g._rows);
      nodeLayer.appendChild(g);
    }
    for (const r of G.rows) {
      edgeLayer.appendChild(Object.assign(s('line', {x1: X.t + cw.t, x2: X.e, y1: r.y, y2: r.y, class: 'fl-c'}), {_rows: new Set([r])}));
      edgeEls.push(edgeLayer.lastChild);
      const g = s('g', {class: 'fl-row ' + r.kind, tabindex: 0, role: 'button'});
      let label, cls = '', title;
      if (r.kind === 'entry') {
        const e = r.entries[0], pl = DM.pill(e);
        cls = pl.cls; label = DM.trimTitle(e.title);
        title = DM.plain(e.title) + '\n' + pl.label + (e.waitingWhy ? ': ' + e.waitingWhy : '') + '\n' + DM.fileTitle(e.file);
        g.addEventListener('click', () => DM.app.openEntry(e.id));
      } else if (r.kind === 'summary') {
        const n = r.entries.length;
        label = n + (n === 1 ? ' entry' : ' entries') + ' (collapsed)'; cls = 'sum'; title = 'Click to expand';
        g.addEventListener('click', () => { st.prefs.flow.collapsed[r.g.name] = false; DM.app.save(); DM.board.redraw(); });
      } else {
        const n = r.entries.length;
        label = n ? '+ ' + n + ' without GitHub links' : 'only finished entries'; cls = 'more';
        title = n ? 'Show these in the Entries tab' : '';
        if (n) g.addEventListener('click', () => DM.entries.filterTo({thread: r.g.name === '(no thread)' ? '__none' : r.g.name, status: p.showDone ? '' : 'notdone'}));
      }
      g.appendChild(s('rect', {x: X.e, y: r.y - ROW / 2 + 2, width: cw.e, height: ROW - 4, rx: 3, class: 'bg'}));
      g.appendChild(s('rect', {x: X.e, y: r.y - ROW / 2 + 2, width: 4, height: ROW - 4, rx: 1.5, class: 'stripe p-' + cls}));
      g.appendChild(s('text', {x: X.e + 10, y: r.y + 4, text: trunc(label, cw.e - 16)}));
      g.appendChild(s('title', {text: title}));
      g.addEventListener('keydown', ev => { if (ev.key === 'Enter') g.dispatchEvent(new MouseEvent('click')); });
      hover(g, new Set([r]));
      nodeLayer.appendChild(g);
      rowEls.push(g);
    }
    // middle + version nodes
    for (const n of G.mids.concat(G.vers)) {
      const x = xl(n), w = n.kind === 'version' ? cw.v : cw.m;
      const rowsOf = new Set(G.rows.filter(r => r.mids.has(n) || r.vers.has(n)));
      const stCls = n.kind === 'version' ? 'f-' + n.state : 'f-' + (n.kind === 'branch' ? 'branch' : n.state);
      const g = s('g', {class: 'fl-node ' + stCls + (n.ci === 'failure' ? ' ci-fail' : n.ci === 'in_progress' ? ' ci-run' : ''), tabindex: 0, role: 'button'});
      g.appendChild(s('rect', {x, y: n.y - ROW / 2 + 1, width: w, height: ROW - 2, rx: 4}));
      const badge = n.ci === 'failure' ? 'CI failing' : n.ci === 'in_progress' ? 'CI running' : '';
      const main = n.kind === 'pr' ? n.label + ' ' + (n.state || '') : n.kind === 'branch' ? DM.shortBranch(n.label) : n.kind === 'version' ? n.label + ' · ' + (n.state === 'prod' ? 'production' : n.state === 'testing' ? 'testing' : 'not released') : n.label;
      g.appendChild(s('text', {x: x + 7, y: n.y + 4, text: trunc(main, w - (badge ? 70 : 12))}));
      if (badge) g.appendChild(s('text', {x: x + w - 6, y: n.y + 4, 'text-anchor': 'end', class: 'fl-badge', text: badge}));
      g.appendChild(s('title', {text: [n.label, n.sub, n.branch && n.kind === 'pr' ? 'branch ' + n.branch : '', badge, rowsOf.size + ' linked row(s)'].filter(Boolean).join('\n')}));
      g.addEventListener('click', () => nodeDialog(n, rowsOf));
      g.addEventListener('keydown', ev => { if (ev.key === 'Enter') nodeDialog(n, rowsOf); });
      hover(g, rowsOf);
      nodeLayer.appendChild(g);
    }
    function hover(g, rset) {
      g.addEventListener('pointerenter', () => {
        svg.classList.add('hl');
        for (const el of edgeEls) el.classList.toggle('on', [...el._rows].some(r => rset.has(r)));
        for (const el of rowEls) el.classList.remove('on');
        g.classList.add('on');
      });
      g.addEventListener('pointerleave', () => { svg.classList.remove('hl'); g.classList.remove('on'); for (const el of edgeEls) el.classList.remove('on'); });
    }
  }

  function nodeDialog(n, rowsOf) {
    const es = [...new Set([...rowsOf].flatMap(r => r.entries))];
    DM.app.openDialog(h('span', null, n.kind === 'version' ? 'Version ' + n.label : n.label),
      h('div', null,
        h('p', null, n.sub || '', n.ci ? ' · CI ' + n.ci.replace('_', ' ') : '', ' ',
          DM.ext(n.url, 'open on GitHub', 'btnlink'), ' ',
          DM.ghManagerUrl({kind: n.kind === 'version' ? 'version' : n.kind, url: n.url, label: n.label}) ?
            h('a', {href: DM.ghManagerUrl({kind: n.kind === 'version' ? 'version' : n.kind, url: n.url, label: n.label}), target: 'quark-github', rel: 'noopener', class: 'btnlink'}, 'open in the GitHub manager') : null),
        h('h4', {class: 'side-h'}, 'Linked entries (' + es.length + ')'),
        h('div', {class: 'cards one'}, es.map(e => DM.entries.card(e, {inDialog: true})))));
  }

  DM.board.SUBS.flow = {render};
})(window.DM);
