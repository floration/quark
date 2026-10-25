/* ===== UI: library ==================================================================================
   The sound matrix (rows = sounds, columns = kits), the voice matrix (rows = lines, columns = voices), sequence
   play (a row across packs, a column through a pack), and the loudness overview chart. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h, fmt = U.fmt, lib = U.library = {};

  const unit = { lufs: 'LUFS', mmax: 'LUFS', tp: 'dBTP', heard: 'dB', dur: 's' };
  lib.metricVal = (m, metric) => m ? m[metric] : null;
  lib.fmtMetric = (m, metric) => m ? (metric === 'dur' ? fmt(m.dur, 2) + 's' : fmt(m[metric], 1)) : '...';

  // --- sequence play ----------------------------------------------------------------------------------------------
  let seqToken = 0;
  lib.playSequence = async function (keys, label) {
    const tok = ++seqToken;
    U.toast('Playing ' + label + ' (' + keys.length + ')');
    for (const k of keys) {
      if (tok !== seqToken) return;
      const a = Q.assets[k];
      if (!Q.audible(a.kind, a.pack, a.name)) continue;
      U.select(k);
      await U.playKey(k);
      const p = Q.previewState();
      const dur = p ? p.dur : 0.1;
      await new Promise(r => setTimeout(r, Math.min(5200, dur * 1000) + 180));
    }
  };
  lib.stopSequence = () => { seqToken++; };

  // --- one matrix ---------------------------------------------------------------------------------------------------
  function packsOf(kind) { return kind === 'voice' ? Q.VOICES : Q.KITS; }
  function rowGroups(kind) {
    if (kind === 'voice') return [['Announcer lines', Q.LINES], ['QUIPS', Q.QUIPS]];
    const seen = new Set(Q.SOUND_GROUPS.flatMap(g => g[1]));
    const extra = Q.SOUNDS.filter(s => !seen.has(s));
    return extra.length ? Q.SOUND_GROUPS.concat([['Other', extra]]) : Q.SOUND_GROUPS;
  }
  function opts(kind) {
    const p = kind === 'voice' ? 'v' : 's';
    return { metric: U.$('#' + p + 'Metric').value, which: U.$('#' + p + 'Which').value, filter: U.$('#' + p + 'Filter').value.trim().toLowerCase(),
      edited: U.$('#' + p + 'Edited').checked, over: U.$('#' + p + 'Over').checked, fav: U.$('#' + p + 'Fav').checked, origin: U.$('#' + p + 'Origin').value };
  }
  // the time a row's mini waveforms span: its longest clip (so the longest fills its cell, the others compare to it)
  function rowSpan(kind, name, which) {
    let n = 0;
    for (const p of packsOf(kind)) {
      const k = Q.key(kind, p.dir, name), a = Q.assets[k]; if (!a) continue;
      const x = which === 'out' ? Q.outSync(k) : which === 'game' ? (Q.gameSync(k) || {}).v : null;
      n = Math.max(n, (x || a.orig || []).length);
    }
    return Math.max(0.1, n / 48000 * 1.04);
  }

  /* Mini waveforms are drawn in one batch per frame: every queued canvas's size is read first (one layout), then each is
     sized and drawn (reading clientWidth between draws forced a layout of the whole table per cell). */
  const miniQ = new Map(); let miniRaf = 0;
  function queueMini(cv, draw) {
    miniQ.set(cv, draw);
    if (!miniRaf) miniRaf = requestAnimationFrame(() => {
      miniRaf = 0;
      const jobs = [...miniQ].filter(([c]) => c.isConnected); miniQ.clear();
      const sizes = jobs.map(([c]) => ({ w: c.clientWidth, h: c.clientHeight }));
      jobs.forEach(([c, d], i) => d(c, sizes[i]));
    });
  }
  // the value line of a cell (the metric shown first): a metric change rewrites only this
  function valsFor(m, metric) {
    const isDur = metric === 'dur';
    return h('div.vals', h('span.main', lib.fmtMetric(m, metric)), h('span', isDur ? '' : unit[metric]), h('span', isDur ? fmt(m.lufs, 1) + ' LUFS' : fmt(m.dur, 2) + 's'), h('span', 'pk ' + fmt(m.peak, 1)));
  }
  const curMetric = { sound: 'lufs', voice: 'lufs' };

  function fillCell(td, key, o) {
    const a = Q.assets[key], loadedId = (a.kind === 'voice' ? 'voice:' : 'kit:') + a.pack;
    td.innerHTML = '';
    if (!Q.loaded[loadedId]) { td.append(h('div.loadcol', 'loading...')); return; }
    const x = o.which === 'orig' ? a.orig : o.which === 'out' ? Q.outSync(key) : (Q.gameSync(key) || {}).v;
    const m = Q.metricsSync(key, o.which);
    if (!x || !m) {
      td.append(h('div.loadcol', 'measuring...'));
      Q.getMetrics(key, o.which).then(() => { if (td.isConnected) fillCell(td, key, o); }).catch(e => U.fail('measure ' + key, e));
      return;
    }
    const cv = h('canvas'), pack = (a.kind === 'voice' ? Q.voiceByDir(a.pack) : Q.kitByDir(a.pack));
    td.append(cv);
    td._m = m;
    td.append(valsFor(m, curMetric[a.kind]));
    const badges = h('div.badges'), e = Q.state.edits[key], inh = Q.inheritsFrom(key);
    if (e) badges.append(Q.isNoop(key) ? h('span.badge.noop', { title: 'Edited, but it plays the same as the shipped sound: not exported' }, 'ED=') : h('span.badge.ed', { title: 'Edited: exported with your changes' }, 'ED'));
    if (e && e.levelSet) badges.append(h('span.badge.lvl', { title: 'Level set by you: listed in sound-levels.txt, the game keeps it' }, 'LVL'));
    if (inh) badges.append(h('span.badge.inh', { title: inh.retro ? 'RETRO is DEFAULT chiptuned: your DEFAULT edit plays here (chiptuned) in the game' : 'This kit uses DEFAULT\'s sound: your DEFAULT edit plays here in the game' }, inh.retro ? '=D~' : '=D'));
    else if (!e && a.sameAsDefault) badges.append(h('span.badge', { title: 'Same as DEFAULT (a DEFAULT override also replaces it)' }, '=D'));
    else if (!e && a.derived === 'retro') badges.append(h('span.badge', { title: 'RETRO = DEFAULT chiptuned (x1.1 pitch, 4-bit)' }, '~D'));
    if (a.shippedOverride) badges.append(h('span.badge.ship', { title: 'Already replaced by a file in assets/overrides (shipped)' }, 'OWN'));
    const og = Q.prov.currentOrigin(key);
    if (og.origin !== 'empty') badges.append(h('span.badge.o-' + og.origin, { title: (og.origin === 'unset' ? 'Origin not set: say who made this file (editor panel or export)' : Q.prov.LABEL[og.origin]) + '\n' + og.why }, Q.prov.SHORT[og.origin]));
    td.classList.toggle('dimmed', !!o.origin && og.origin !== o.origin);
    if (a.guide && m.dur > a.guide + 1e-3) badges.append(h('span.badge.over', { title: 'Longer than the length guide (' + a.guide + ' s)' }, '>' + a.guide + 's'));
    const outM = Q.metricsSync(key, 'out');
    if (outM && outM.dur > a.cap) badges.append(h('span.badge.cap', { title: 'The game cuts it at ' + a.cap + ' s' }, 'CUT'));
    td.append(badges);
    td.classList.toggle('silent', !x.length || m.peak < -90);
    td.title = a.name + ' / ' + Q.packName(a.kind, a.pack) + '\n' + (a.when || '') + '\nLUFS ' + fmt(m.lufs) + '  max momentary ' + fmt(m.mmax) + '  true peak ' + fmt(m.tp) + ' dBTP  heard ' + fmt(m.heard) + ' dB  ' + fmt(m.dur, 3) + ' s';
    queueMini(cv, (c, size) => Q.drawMini(c, x, rowSpan(a.kind, a.name, o.which), pack ? pack.color : '#6fb1ff', size));
  }
  lib.fillCell = fillCell;

  function rowVisible(kind, name, o) {
    const meta = Q.state.meta[kind + ':' + name] || {};
    const keys = packsOf(kind).map(p => Q.key(kind, p.dir, name));
    if (o.filter) {
      const a = Q.assets[keys[0]] || {};
      if (!(name.toLowerCase().includes(o.filter) || (a.when || '').toLowerCase().includes(o.filter) || (meta.note || '').toLowerCase().includes(o.filter))) return false;
    }
    if (o.edited && !keys.some(k => Q.state.edits[k] || Q.inheritsFrom(k))) return false;
    if (o.fav && !meta.fav) return false;
    if (o.over && kind === 'sound') {   // over the length guide
      const g = (Q.assets[keys[0]] || {}).guide;
      if (!g || !keys.some(k => { const m = Q.metricsSync(k, o.which); return m && m.dur > g + 1e-3; })) return false;
    }
    if (o.over && kind === 'voice' && !keys.some(k => { const a = Q.assets[k], x = a && (Q.state.edits[k] ? Q.outSync(k) : a.orig); return x && x.length / 48000 > a.cap; })) return false;   // cut by the game
    if (o.origin && !keys.some(k => Q.assets[k] && Q.prov.currentOrigin(k).origin === o.origin)) return false;
    return true;
  }

  // who made what the game plays, counted per pack (header) and for the whole matrix (toolbar)
  const ORDER = ['ai', 'third-party', 'human', 'unset'];
  function originCounts(kind) {
    const per = {}, all = {};
    const names = rowGroups(kind).flatMap(g => g[1]);
    for (const p of packsOf(kind)) {
      const c = per[p.dir] = {};
      for (const n of names) {
        const k = Q.key(kind, p.dir, n); if (!Q.assets[k]) continue;
        const og = Q.prov.currentOrigin(k).origin; if (og === 'empty') continue;
        c[og] = (c[og] || 0) + 1; all[og] = (all[og] || 0) + 1;
      }
    }
    return { per, all };
  }
  const countSpans = c => ORDER.filter(o => c[o]).map(o => h('span.oc.o-' + o, { title: c[o] + ' ' + (o === 'unset' ? 'with no origin set yet' : Q.prov.LABEL[o]) }, Q.prov.SHORT[o] + ' ' + c[o]));

  /* The table is reconciled, not rebuilt: rows whose signature didn't change stay where they are (moving or re-adding
     165 / 245 rows restyled and laid out the whole table), a changed row is swapped in place, and a metric change only
     rewrites each cell's value line. */
  const grpRows = { sound: new Map(), voice: new Map() };
  lib.renderMatrix = function (kind) {
    const table = U.$(kind === 'voice' ? '#voiceMatrix' : '#soundMatrix'), o = opts(kind), packs = packsOf(kind);
    curMetric[kind] = o.metric;
    table.style.minWidth = (300 + 150 * packs.length) + 'px';
    const counts = originCounts(kind), tot = Object.values(counts.all).reduce((s, n) => s + n, 0);
    const cs = U.$(kind === 'voice' ? '#vCounts' : '#sCounts'); cs.innerHTML = '';
    cs.append(h('span.faint', 'of ' + tot + ':'), ...countSpans(counts.all));
    const head = h('tr', h('th.rowhead-h', kind === 'voice' ? 'Line' : 'Sound'));
    for (const p of packs) {
      const loaded = Q.loaded[(kind === 'voice' ? 'voice:' : 'kit:') + p.dir];
      head.append(h('th.pack', h('div.pack-head', h('span.sw', { style: { background: p.color } }),
        h('span.nm', p.name, p.sub ? h('small', ' ' + p.sub) : null),
        U.smButtons('Pack', Q.packId(kind, p.dir), Q.packId(kind, p.dir)),
        h('button.mini', { type: 'button', title: 'Play every ' + (kind === 'voice' ? 'line' : 'sound') + ' of ' + p.name + ' in turn', disabled: !loaded,
          onclick: () => lib.playSequence(visibleNames(kind).map(n => Q.key(kind, p.dir, n)).filter(k => Q.assets[k]), p.name) }, '▶')),
        h('div.pack-counts', countSpans(counts.per[p.dir] || {}))));
    }
    const thead = h('thead', head), oldHead = table.querySelector('thead');
    if (oldHead) oldHead.replaceWith(thead); else table.prepend(thead);
    let body = table.querySelector('tbody');
    if (!body) { body = h('tbody'); table.append(body); }
    const want = [], fresh = new Map();
    for (const [gname, names] of rowGroups(kind)) {
      const rows = names.filter(n => rowVisible(kind, n, o));
      if (!rows.length) continue;
      let g = grpRows[kind].get(gname);
      if (!g) grpRows[kind].set(gname, g = h('tr.grp', h('td', { colSpan: packs.length + 1 }, gname)));
      want.push(g);
      for (const name of rows) want.push(cachedRow(kind, name, packs, o, fresh));
    }
    rowCache[kind] = fresh;
    if (!want.length) want.push(h('tr.none', h('td', { colSpan: packs.length + 1 }, 'No ' + (kind === 'voice' ? 'lines' : 'sounds') + ' match these filters. ',
      h('button.mini', { type: 'button', onclick: () => lib.clearFilters(kind) }, 'clear the filters'))));
    want.forEach((tr, i) => { if (body.children[i] !== tr) body.insertBefore(tr, body.children[i] || null); });
    while (body.children.length > want.length) body.lastChild.remove();
  };
  function visibleNames(kind) { const o = opts(kind); return rowGroups(kind).flatMap(g => g[1]).filter(n => rowVisible(kind, n, o)); }
  lib.visibleNames = visibleNames;

  /* A commit re-renders the matrix; a row whose inputs didn't change is reused as it is (its canvases drawn, its
     metrics filled), so a commit redraws only the rows it touched instead of all 165 / 245 cells (about 300 ms -> a few
     ms at 2560). The signature is everything rowFor reads except the metric (only the value line shows it): the options,
     the row's note / favourite / solo / mute, and per cell its pack being loaded, its edit, what the game plays
     (inherited DEFAULT edits too) and its origin. */
  const rowCache = { sound: new Map(), voice: new Map() };
  function rowSig(kind, name, packs, o) {
    const parts = [o.origin, JSON.stringify(Q.state.meta[kind + ':' + name] || {}), Q.mix.soloSound.has(Q.soundId(kind, name)), Q.mix.muteSound.has(Q.soundId(kind, name))];
    for (const p of packs) {
      const k = Q.key(kind, p.dir, name);
      if (!Q.assets[k]) { parts.push('-'); continue; }
      // (the clip the cell shows, not the "show" option: an unedited sound is the same clip as shipped, file and in game,
      // so switching the option redraws only the rows it changes)
      const g = Q.gameSig(k), e = Q.editSig(k), shown = o.which === 'orig' || (o.which === 'out' ? e : g) === 'orig' ? 'orig' : o.which + ':' + (o.which === 'out' ? e : g);
      parts.push(!!Q.loaded[(kind === 'voice' ? 'voice:' : 'kit:') + p.dir], Q.audible(kind, p.dir, name), g, e, shown, Q.prov.currentOrigin(k).origin);
    }
    return parts.join('\u0001');
  }
  function cachedRow(kind, name, packs, o, fresh) {
    const sig = rowSig(kind, name, packs, o), c = rowCache[kind].get(name);
    const tr = c && c.sig === sig ? c.tr : rowFor(kind, name, packs, o);
    if (c && c.tr === tr && c.metric !== o.metric) for (const td of tr.querySelectorAll('td.cell')) { const v = td._m && td.querySelector('.vals'); if (v) v.replaceWith(valsFor(td._m, o.metric)); }
    fresh.set(name, { sig, tr, metric: o.metric });
    return tr;
  }

  function rowFor(kind, name, packs, o) {
    const a0 = Q.assets[Q.key(kind, packs[0].dir, name)] || {}, meta = Q.state.meta[kind + ':' + name] || {};
    const uses = kind === 'sound' ? Q.USES[name] : null, weak = kind === 'sound' ? Q.WEAK[name] : Q.WEAK_LINES[name];
    const reuses = uses ? uses.filter(u => u.reuse) : [];
    const keys = packs.map(p => Q.key(kind, p.dir, name)).filter(k => Q.assets[k]);
    const rh = h('td.rowhead',
      h('div.rh-top',
        h('button.star' + (meta.fav ? '.on' : ''), { type: 'button', title: 'Favourite (F)', onclick: () => U.toggleFav(keys[0]) }, meta.fav ? '★' : '☆'),
        h('span.rh-name', { title: 'Select', onclick: () => U.select(Q.sel && Q.assets[Q.sel] && Q.assets[Q.sel].name === name && Q.assets[Q.sel].kind === kind ? Q.sel : keys[0]) }, name),
        U.smButtons('Sound', Q.soundId(kind, name), Q.soundId(kind, name)),
        h('button.mini', { type: 'button', title: 'Play this ' + (kind === 'voice' ? 'line in every voice' : 'sound in every kit') + ' in turn (compare)', onclick: () => lib.playSequence(keys, name + ' across ' + (kind === 'voice' ? 'voices' : 'kits')) }, '▶▶')),
      h('div.rh-when', a0.when || '', a0.guide ? h('span.guide', '  guide < ' + a0.guide + ' s') : null),
      reuses.length ? h('div.rh-uses', 'also: ' + reuses.map(u => u.what + (typeof u.pitch === 'number' ? ' (pitch ' + u.pitch + ')' : '')).join('; ')) : null,
      weak ? h('div.rh-weak', weak) : null,
      meta.note ? h('div.rh-uses', 'note: ' + meta.note) : null);
    const tr = h('tr', rh);
    for (const p of packs) {
      const key = Q.key(kind, p.dir, name);
      if (!Q.assets[key]) { tr.append(h('td.cell', h('div.loadcol', '-'))); continue; }
      const td = h('td.cell' + (Q.sel === key ? '.sel' : '') + (Q.audible(kind, p.dir, name) ? '' : '.muted'), { 'data-key': key,
        onclick: () => { const pv = Q.previewState(); if (Q.sel === key && pv && pv.key === key) { Q.stopPreview(); return; } lib.stopSequence(); U.select(key, true); } });
      U.files.dropTarget(td, key);
      fillCell(td, key, o);
      tr.append(td);
    }
    return tr;
  }

  // keyboard: move the selected cell
  lib.move = function (k) {
    const table = U.$(Q.tab === 'voices' ? '#voiceMatrix' : '#soundMatrix');
    const rows = [...table.querySelectorAll('tbody tr:not(.grp)')];
    let r = rows.findIndex(tr => tr.querySelector('td.cell.sel')), c = 0;
    if (r < 0) { r = 0; c = 0; } else c = [...rows[r].querySelectorAll('td.cell')].findIndex(td => td.classList.contains('sel'));
    if (k === 'ArrowUp') r = Math.max(0, r - 1); if (k === 'ArrowDown') r = Math.min(rows.length - 1, r + 1);
    const cells = rows[r] ? [...rows[r].querySelectorAll('td.cell')] : [];
    if (k === 'ArrowLeft') c = Math.max(0, c - 1); if (k === 'ArrowRight') c = Math.min(cells.length - 1, c + 1);
    const td = cells[Math.max(0, c)];
    if (td) { U.select(td.dataset.key); td.scrollIntoView({ block: 'nearest', inline: 'nearest' }); }
  };

  // voices: load every voice when the tab (or the overview of voices) opens, all at once: the scripts download and
  // the clips decode in parallel (decodeAudioData runs off the main thread); each voice shows as soon as it is in
  let voicesP = null;
  lib.loadVoices = function () {
    if (voicesP) return voicesP;
    let done = 0;
    const label = () => { U.$('#voiceLoad').textContent = done < Q.VOICES.length ? 'loading voices: ' + done + ' of ' + Q.VOICES.length + '...' : ''; };
    label();
    voicesP = Promise.all(Q.VOICES.map(v => Q.loadPack('voice', v.dir).catch(e => U.fail('voice ' + v.dir, e)).then(() => { done++; label(); })));
    return voicesP;
  };
  // a pack finished loading: redraw the view that shows it (coalesced: seven voices landing together draw once or twice)
  const landed = new Set(); let landT = 0;
  lib.packLoaded = function (type) {
    landed.add(type);
    if (landT) return;
    landT = setTimeout(() => {
      landT = 0;
      const t = new Set(landed); landed.clear();
      if (Q.tab === 'sounds' && t.has('kit')) lib.renderMatrix('sound');
      else if (Q.tab === 'voices' && t.has('voice')) lib.renderMatrix('voice');
      else if (Q.tab === 'overview' && t.has(U.$('#ovSet').value === 'voice' ? 'voice' : 'kit')) U.views.overview.render();
    }, 60);
  };

  lib.clearFilters = function (kind) {
    const p = kind === 'voice' ? 'v' : 's';
    U.$('#' + p + 'Filter').value = ''; U.$('#' + p + 'Origin').value = '';
    for (const id of ['Edited', 'Over', 'Fav']) U.$('#' + p + id).checked = false;
    lib.renderMatrix(kind);
  };

  U.views.sounds = { render: () => lib.renderMatrix('sound') };
  U.views.voices = { render: () => { lib.renderMatrix('voice'); lib.loadVoices(); } };
  for (const id of ['sMetric', 'sWhich', 'sEdited', 'sOver', 'sFav', 'sOrigin']) document.getElementById(id).addEventListener('change', () => lib.renderMatrix('sound'));
  for (const id of ['vMetric', 'vWhich', 'vEdited', 'vOver', 'vFav', 'vOrigin']) document.getElementById(id).addEventListener('change', () => lib.renderMatrix('voice'));
  let ft = 0;
  U.$('#sFilter').addEventListener('input', () => { clearTimeout(ft); ft = setTimeout(() => lib.renderMatrix('sound'), 150); });
  U.$('#vFilter').addEventListener('input', () => { clearTimeout(ft); ft = setTimeout(() => lib.renderMatrix('voice'), 150); });

  // --- overview: a dot plot per sound, one mark per pack (shape + colour), spread on the right ---------------------
  const SVGNS = 'http://www.w3.org/2000/svg';
  const s = (tag, attrs) => { const el = document.createElementNS(SVGNS, tag); for (const [k, v] of Object.entries(attrs || {})) el.setAttribute(k, v); return el; };
  function shape(kind, x, y, color) {
    const r = 5, ring = '#111317';
    if (kind === 'square') return s('rect', { x: x - r + 0.5, y: y - r + 0.5, width: 2 * r - 1, height: 2 * r - 1, rx: 1.5, fill: color, stroke: ring, 'stroke-width': 1.5 });
    if (kind === 'diamond') return s('path', { d: `M${x} ${y - r - 1}L${x + r + 1} ${y}L${x} ${y + r + 1}L${x - r - 1} ${y}Z`, fill: color, stroke: ring, 'stroke-width': 1.5 });
    if (kind === 'triangle') return s('path', { d: `M${x} ${y - r - 1}L${x + r + 1} ${y + r}L${x - r - 1} ${y + r}Z`, fill: color, stroke: ring, 'stroke-width': 1.5 });
    if (kind === 'cross') return s('path', { d: `M${x - r} ${y - r}L${x + r} ${y + r}M${x + r} ${y - r}L${x - r} ${y + r}`, stroke: color, 'stroke-width': 2.5, 'stroke-linecap': 'round' });
    return s('circle', { cx: x, cy: y, r, fill: color, stroke: ring, 'stroke-width': 1.5 });
  }
  let ovTok = 0;
  lib.renderOverview = async function () {
    const kind = U.$('#ovSet').value, metric = U.$('#ovMetric').value, which = U.$('#ovWhich').value, packs = packsOf(kind);
    const box = U.$('#overview'), tok = ++ovTok;
    U.$('#ovTip').hidden = true;   // (the chart it belonged to is going)
    const legend = U.$('#ovLegend'); legend.innerHTML = '';
    for (const p of packs) { const sv = s('svg', { width: 14, height: 14 }); sv.append(shape(p.shape, 7, 7, p.color)); legend.append(h('span', sv, p.name)); }
    if (kind === 'voice') lib.loadVoices();
    const names = rowGroups(kind).flatMap(g => g[1]);
    const rows = [];
    for (const n of names) {
      const pts = [];
      for (const p of packs) {
        const k = Q.key(kind, p.dir, n);
        if (!Q.assets[k] || !Q.loaded[(kind === 'voice' ? 'voice:' : 'kit:') + p.dir]) continue;
        const m = await Q.getMetrics(k, which), v = m ? m[metric] : null;
        if (v != null && isFinite(v)) pts.push({ p, k, v, m });
      }
      rows.push({ n, pts });
    }
    if (tok !== ovTok) return;   // (a newer render started while this one measured)
    const all = rows.flatMap(r => r.pts.map(p => p.v));
    const waiting = packs.filter(p => !Q.loaded[(kind === 'voice' ? 'voice:' : 'kit:') + p.dir]).length;
    if (!all.length) { box.innerHTML = '<div class="faint" style="padding:20px">Loading' + (waiting ? ' (' + (packs.length - waiting) + ' of ' + packs.length + ' ' + (kind === 'voice' ? 'voices' : 'kits') + ' in)' : '') + '...</div>'; return; }
    const lo = Math.floor(Math.min(...all) - 1), hi = Math.ceil(Math.max(...all) + 1);
    const W = Math.max(600, box.clientWidth - 30), left = 150, right = 90, rowH = 20, top = 26, H = top + rows.length * rowH + 10;
    const X = v => left + (v - lo) / (hi - lo) * (W - left - right);
    const svg = s('svg', { width: W, height: H });
    const axis = s('g', { class: 'axis' });
    const step = hi - lo > 30 ? 5 : hi - lo > 12 ? 2 : 1;
    for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) {
      axis.append(s('line', { x1: X(v), x2: X(v), y1: top - 6, y2: H - 6, stroke: '#1f242c' }));
      const t = s('text', { x: X(v), y: 14, 'text-anchor': 'middle' }); t.textContent = v; axis.append(t);
    }
    const ut = s('text', { x: W - right + 22, y: 14 }); ut.textContent = 'spread'; axis.append(ut);
    svg.append(axis);
    const tip = U.$('#ovTip');
    rows.forEach((r, i) => {
      const y = top + i * rowH + rowH / 2;
      const lab = s('text', { x: left - 10, y: y + 4, 'text-anchor': 'end' }); lab.textContent = r.n; svg.append(lab);
      if (!r.pts.length) return;
      const vs = r.pts.map(p => p.v), mn = Math.min(...vs), mx = Math.max(...vs);
      svg.append(s('line', { x1: X(mn), x2: X(mx), y1: y, y2: y, stroke: '#3a4250', 'stroke-width': 2 }));
      for (const pt of r.pts) {
        const g = s('g', { style: 'cursor:pointer' }); g.append(shape(pt.p.shape, X(pt.v), y, pt.p.color));
        g.append(s('circle', { cx: X(pt.v), cy: y, r: 9, fill: 'transparent' }));
        g.addEventListener('mouseenter', ev => { tip.hidden = false; tip.textContent = r.n + ' / ' + pt.p.name + ': ' + fmt(pt.v, 2) + ' ' + unit[metric] + '  (' + fmt(pt.m.dur, 2) + ' s)'; });
        g.addEventListener('mousemove', ev => { tip.style.left = ev.clientX + 12 + 'px'; tip.style.top = ev.clientY + 12 + 'px'; });
        g.addEventListener('mouseleave', () => { tip.hidden = true; });
        g.addEventListener('click', () => U.select(pt.k, true));
        svg.append(g);
      }
      const sp = s('text', { x: W - right + 22, y: y + 4, fill: mx - mn > 6 ? '#ec835a' : '#a5abb6' }); sp.textContent = fmt(mx - mn, 1); svg.append(sp);
    });
    box.innerHTML = '';
    if (waiting) box.append(h('div.faint', { style: { padding: '0 0 6px' } }, 'Loading ' + waiting + ' more ' + (kind === 'voice' ? 'voice' : 'kit') + (waiting > 1 ? 's' : '') + ': the chart fills in as they arrive.'));
    box.append(svg);
  };
  U.views.overview = { render: () => lib.renderOverview().catch(e => U.fail('overview', e)) };
  for (const id of ['ovSet', 'ovMetric', 'ovWhich']) document.getElementById(id).addEventListener('change', () => U.views.overview.render());
})();
