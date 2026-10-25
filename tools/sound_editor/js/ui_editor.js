/* ===== UI: editor (the dock) =========================================================================
   One sound: source waveform with trim / fade handles, the output as the game will play it, a spectrogram,
   metrics, the filter chain (each step with bypass), level / notes / where it plays, A/B, generator, copy chain. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h, fmt = U.fmt, R = 48000;
  const E = U.editor = { key: null, which: 'game', bMode: 'game', view: null, cursor: null, sel: null, spec: true };
  let els = {}, liveTok = null, renderT = 0;

  const edit = () => Q.state.edits[E.key];
  const src = () => Q.sourceOf(E.key);
  const srcDur = () => src().length / R;
  function trimOf() { const e = edit(); const d = srcDur(); return e ? [e.trim[0] || 0, e.trim[1] == null ? d : Math.min(d, e.trim[1])] : [0, d]; }
  function speedOf() { const e = edit(); let s = 1; if (e) for (const st of e.chain) if (st.on && st.type === 'pitch') s *= Math.pow(2, st.p.semis / 12); return s; }

  // --- live (dragged / typed) changes: one undo step from first move to release ------------------------------------
  function ensureEdit() { if (!Q.state.edits[E.key]) Q.state.edits[E.key] = Q.newEdit(); return Q.state.edits[E.key]; }
  E.live = function (label, fn) {
    if (!liveTok) liveTok = Q.beginStep(label);
    fn(ensureEdit());
    E.redraw(); scheduleRender();
  };
  E.liveEnd = function () { if (!liveTok) return; const t = liveTok; liveTok = null; Q.endStep(t); };
  E.change = (label, fn) => Q.commit(label, () => fn(ensureEdit()));
  function scheduleRender() {
    clearTimeout(renderT);
    renderT = setTimeout(() => { const k = E.key; Q.getOut(k).then(() => { if (E.key === k) { E.redraw(); fillMetrics(); } }).catch(e => U.fail('render', e)); }, 120);
  }

  // --- show ------------------------------------------------------------------------------------------------------------
  E.show = function (key) {
    const changed = key !== E.key;
    E.key = key;
    if (changed) { E.view = null; E.cursor = null; E.sel = null; }
    build();
  };
  let refreshT = 0;
  E.refresh = function () {
    if (E.key && !Q.assets[E.key]) { E.key = null; idle(); return; }   // (its sound is gone: your own name removed)
    if (!E.key) return;
    // a commit from a field in the dock (its change fires on Tab or a click elsewhere, while the focus is between
    // fields: on body): rebuild once the focus has landed, then build() puts it back on the same field of the new dock
    const ae = document.activeElement;
    if (!ae || ae === document.body || U.$('#editor').contains(ae)) { clearTimeout(refreshT); refreshT = setTimeout(() => { if (E.key && Q.assets[E.key]) build(); }, 0); return; }
    build();
  };
  let tabAt = -1e9;
  U.$('#editor').addEventListener('keydown', ev => { if (ev.key === 'Tab') tabAt = performance.now(); });
  U.$('#editor').addEventListener('focusout', ev => { if (!ev.relatedTarget && performance.now() - tabAt < 100 && ev.target.matches(FIELDS)) lastOut = { el: ev.target, t: performance.now() }; });
  function idle() {
    U.$('#dock').classList.add('idle');
    const root = U.$('#editor'); root.innerHTML = ''; root.append(h('div.empty', 'Select a sound (click a cell) to edit it here.'));
  }

  // what a rebuild must not lose: the focused field (by its place among the dock's fields) and the columns' scroll
  const FIELDS = 'input, select, textarea, button';
  // (a field left with Tab towards nothing focusable, the dock's last chain field, keeps the focus: lastOut)
  let lastOut = null;
  function uiState(root) {
    const f = [...root.querySelectorAll(FIELDS)], el = document.activeElement;
    const sc = sel => { const x = root.querySelector(sel); return x ? x.scrollTop : 0; };
    let focus = root.contains(el) ? f.indexOf(el) : -1;
    if (focus < 0 && (!el || el === document.body) && lastOut && performance.now() - lastOut.t < 1000) focus = f.indexOf(lastOut.el);
    lastOut = null;
    return { focus, side: sc('.ed-side'), chain: sc('.ed-chain') };
  }
  function restoreUI(root, st) {
    const set = (sel, v) => { const x = root.querySelector(sel); if (x) x.scrollTop = v; };
    set('.ed-side', st.side); set('.ed-chain', st.chain);
    if (st.focus >= 0) { const el = root.querySelectorAll(FIELDS)[st.focus]; if (el) el.focus({ preventScroll: true }); }
  }

  function build() {
    const a = Q.assets[E.key], e = edit(), root = U.$('#editor'), keep = uiState(root);
    clearTimeout(refreshT);
    U.$('#dock').classList.remove('idle');
    const loadedId = (a.kind === 'voice' ? 'voice:' : 'kit:') + a.pack;
    if (!Q.loaded[loadedId] && !a.custom) { Q.loadPack(a.kind === 'voice' ? 'voice' : 'kit', a.pack).then(() => E.key === a.key && build()); }
    const pack = Q.packName(a.kind, a.pack), meta = Q.state.meta[U.metaKey(E.key)] || {};
    const inh = Q.inheritsFrom(E.key);
    root.innerHTML = '';
    // header
    const head = h('div.ed-head',
      h('span.ed-title', a.name, h('small', pack + (a.custom ? ' (new: needs a game hook)' : ''))),
      e ? (Q.isNoop(E.key) ? h('span.chip', { title: 'Your edit plays exactly the shipped sound (no step on, no trim past the silence the game cuts): it is left out of the export' }, 'NO CHANGE: NOT EXPORTED') : h('span.chip.ed', 'EDITED')) : null,
      e && e.levelSet ? h('span.chip.lvl', { title: 'In sound-levels.txt: the game keeps this level' }, 'LEVEL YOURS') : null,
      inh ? h('span.chip.warn', { title: 'No edit of its own: in the game it uses your DEFAULT edit' + (inh.retro ? ', chiptuned' : '') }, inh.retro ? 'PLAYS DEFAULT EDIT, CHIPTUNED' : 'PLAYS DEFAULT EDIT') : null,
      h('span.ed-when', { title: a.desc || a.when || '' }, a.when || ''),
      h('span.spacer'),
      h('span.ab', h('button' + (E.which === 'orig' ? '.on' : ''), { type: 'button', title: 'A: the shipped original (key A)', onclick: () => E.setAB('orig', true) }, 'A original'),
        h('button' + (E.which !== 'orig' ? '.on' : ''), { type: 'button', title: 'B: your edit (key B)', onclick: () => E.setAB(E.bMode, true) }, 'B edit')),
      (() => { const s = h('select', { title: 'What B plays', onchange: ev => { E.bMode = ev.target.value; E.setAB(E.bMode, false); } }, h('option', { value: 'game' }, 'as the game plays it'), h('option', { value: 'out' }, 'the exported file')); s.value = E.bMode; return s; })(),
      h('button.primary', { type: 'button', title: 'Play (Space)', onclick: () => E.play() }, '▶ Play'),
      h('button', { type: 'button', title: 'Play the source between the trim handles (Shift+Space)', onclick: () => E.playSelection() }, '▶ Sel'),
      h('button', { type: 'button', onclick: () => Q.stopPreview() }, '■'),
      h('button', { type: 'button', title: 'Load a file into this sound', onclick: () => U.files.pickFor(E.key) }, 'Replace...'),
      h('button', { type: 'button', title: 'Make a sound from a generator (thump + noise)', onclick: () => E.generator() }, 'Design...'),
      h('button', { type: 'button', title: 'Copy this chain (and trim) to other sounds', onclick: () => E.copyChain(), disabled: !e }, 'Copy to...'),
      h('button', { type: 'button', title: 'Back to the shipped sound', disabled: !e, onclick: () => Q.commit('revert ' + a.name, () => { delete Q.state.edits[E.key]; }) }, 'Revert'),
      h('button.star' + (meta.fav ? '.on' : ''), { type: 'button', title: 'Favourite (F)', onclick: () => U.toggleFav(E.key) }, meta.fav ? '★' : '☆'));
    // waveform column
    const srcName = e && e.src && Q.sources[e.src] ? Q.sources[e.src].name : (a.custom ? '(empty: drop a file or Design...)' : 'shipped ' + a.key.split('/').slice(1).join('/'));
    els.srcCv = h('canvas'); els.outCv = h('canvas'); els.specCv = h('canvas');
    els.hStart = h('div.handle', { title: 'Trim start' }); els.hEnd = h('div.handle', { title: 'Trim end' });
    els.hFi = h('div.handle.fade', { title: 'Fade in' }); els.hFo = h('div.handle.fade', { title: 'Fade out' });
    els.srcLane = h('div.lane.src', h('span.lane-label', 'SOURCE  ' + srcName), els.srcCv, els.hStart, els.hEnd, els.hFi, els.hFo);
    els.outLane = h('div.lane', h('span.lane-label', 'OUTPUT (file)  markers: game start, guide, cap'), els.outCv);
    els.specLane = h('div.lane', { style: { display: E.spec ? '' : 'none' } }, els.specCv);
    els.scroll = h('input', { type: 'range', min: 0, max: 1000, value: 0, oninput: ev => { const v = E.view, span = v[1] - v[0], tot = totalDur(); const t0 = (+ev.target.value / 1000) * Math.max(0, tot - span); E.view = [t0, t0 + span]; E.redraw(); } });
    els.readout = h('span.num');
    els.metrics = h('div.metrics');
    const bar = h('div.wave-bar',
      h('button.mini', { type: 'button', title: 'Zoom out (-)', onclick: () => E.zoom(2) }, '−'),
      h('button.mini', { type: 'button', title: 'Zoom in (+)', onclick: () => E.zoom(0.5) }, '+'),
      h('button.mini', { type: 'button', title: 'Fit (0)', onclick: () => E.zoomFit() }, 'fit'),
      els.scroll,
      h('label', h('input', { type: 'checkbox', checked: E.spec, onchange: ev => { E.spec = ev.target.checked; els.specLane.style.display = E.spec ? '' : 'none'; E.redraw(); } }), 'spectrogram'),
      els.readout);
    const wave = h('div.ed-wave', els.srcLane, els.outLane, els.specLane, bar, els.metrics);
    // side: trim / level / notes / uses
    const [t0, t1] = trimOf();
    const num = (label, val, step, onv, title) => [h('span', { title }, label), h('input', { type: 'number', step, value: +val.toFixed(4), title,
      oninput: ev => { const v = parseFloat(ev.target.value); if (isFinite(v)) E.live(label, ed => onv(ed, v)); }, onchange: () => E.liveEnd() })];
    const side = h('div.ed-side', h('div.side-a',
      h('div.side-h', 'Trim and fades'),
      h('div.kv',
        num('start s', t0, 0.001, (ed, v) => { ed.trim[0] = Math.max(0, Math.min(v, trimOf()[1] - 0.001)); }),
        num('end s', t1, 0.001, (ed, v) => { ed.trim[1] = Math.min(srcDur(), Math.max(v, ed.trim[0] + 0.001)); }),
        num('fade in s', e ? e.fadeIn : 0, 0.001, (ed, v) => { ed.fadeIn = Math.max(0, v); }),
        num('fade out s', e ? e.fadeOut : 0, 0.001, (ed, v) => { ed.fadeOut = Math.max(0, v); }),
        h('span', 'length'), h('span.num', fmt(t1 - t0, 3) + ' s' + (a.guide ? '  (guide < ' + a.guide + ')' : ''))),
      h('div', { style: { display: 'flex', gap: '4px', marginTop: '5px', flexWrap: 'wrap' } },
        h('button', { type: 'button', title: 'Start where the game would (the first sample at 3% of the peak); end where it falls under -60 dB of the peak', onclick: () => E.autoTrim() }, 'Auto trim silence'),
        h('button', { type: 'button', onclick: () => E.change('reset trim', ed => { ed.trim = [0, null]; ed.fadeIn = 0; ed.fadeOut = 0; }) }, 'Reset')),
      h('div.side-h', 'Level'),
      h('label', { title: 'Listed in sound-levels.txt on export: the game keeps this file\'s level instead of matching it to the sound it replaces' },
        h('input', { type: 'checkbox', checked: !!(e && e.levelSet), onchange: ev => E.change(ev.target.checked ? 'keep my level' : 'game levels it', ed => { ed.levelSet = ev.target.checked; }) }), 'keep my level (sound-levels.txt)'),
      els.levelInfo = h('div.faint', { style: { marginTop: '3px' } })),
      h('div.side-b', originBlock(a, e),
      h('div.side-h', 'Notes'),
      h('textarea.note', { placeholder: 'notes for this sound (all packs)', onchange: ev => Q.commit('note', () => { const m = Q.state.meta[U.metaKey(E.key)] = Q.state.meta[U.metaKey(E.key)] || {}; m.note = ev.target.value; }) }, meta.note || ''),
      usesBlock(a)));
    // chain
    const chain = h('div.ed-chain', h('div.side-h', 'Filter chain', h('span.faint', { style: { textTransform: 'none', letterSpacing: 0, fontWeight: 400 } }, 'top to bottom, after trim')));
    const addSel = h('select', Object.keys(Q.STEPS).map(t => h('option', { value: t }, Q.STEPS[t].label)));
    chain.append(h('div', { style: { display: 'flex', gap: '4px', marginBottom: '6px' } }, addSel,
      h('button', { type: 'button', onclick: () => E.change('add ' + Q.STEPS[addSel.value].label, ed => ed.chain.push(Q.newStep(addSel.value))) }, '+ Add')));
    if (e) e.chain.forEach((st, i) => chain.append(stepUI(st, i, e.chain.length)));
    if (!e || !e.chain.length) chain.append(h('div.faint', 'No steps. Add one above; each renders offline and can be bypassed.'));
    root.append(head, h('div.ed-body', wave, side, chain));
    restoreUI(root, keep);
    wireHandles();
    wireCanvas();
    E.redraw();
    if (Q.assets[E.key].orig || e) { Q.getOut(E.key).then(() => { E.redraw(); fillMetrics(); }).catch(er => U.fail('render', er)); Q.getGame(E.key).then(() => fillMetrics()).catch(() => { }); }
    fillMetrics();
  }

  // who made this sound (provenance.json on export): a choice for a dropped file, fixed for an edit of a shipped sound
  function originBlock(a, e) {
    const P = Q.prov;
    if (!e) { const d = P.defaultOrigin(a.key); return [h('div.side-h', 'Origin'), h('div.faint', d ? 'Shipped: ' + P.LABEL[d.origin] + ' (' + d.source + ').' : 'Empty: drop a file or Design...')]; }
    const o = P.originOf(a.key);
    const chip = h('span.chip.origin.' + (o.origin || 'unset'), o.origin ? P.LABEL[o.origin] : 'ORIGIN NOT SET');
    if (!o.choose) return [h('div.side-h', 'Origin'), h('div', chip, ' ', h('span.faint', o.note)), h('div.faint', { style: { marginTop: '3px' } }, o.why)];
    return [h('div.side-h', 'Origin'), h('div', chip), U.originPicker(a.key, { compact: false }), h('div.faint', { style: { marginTop: '3px' } }, o.why)];
  }

  function usesBlock(a) {
    const uses = a.kind === 'sound' ? Q.USES[a.name] : null, weak = a.kind === 'sound' ? Q.WEAK[a.name] : Q.WEAK_LINES[a.name];
    if (!uses && !weak) return null;
    const pitch = p => p === 'j97' ? ', pitch 0.97-1.03' : p === 'j90' ? ', pitch 0.90-1.09' : p ? ', pitch ' + p : '';
    return [h('div.side-h', 'Where it plays'), uses ? h('ul.uses', { style: { paddingLeft: '14px', margin: 0 } }, uses.map(u => h('li' + (u.reuse ? '.reuse' : ''),
      u.what + ' (' + u.d + ', vol ' + u.vol + pitch(u.pitch) + (u.pack === 'theirs' ? ', their kit' : u.pack === 'owner' ? ', the owner\'s kit' : '') + ')' + (u.reuse ? ' [reuse]' : '')))) : null,
      weak ? h('div', { style: { color: 'var(--serious)', fontSize: '11px', marginTop: '3px' } }, weak) : null];
  }

  function stepUI(st, i, n) {
    const def = Q.STEPS[st.type];
    const box = h('div.step' + (st.on ? '' : '.off') + (st.group ? '.grp' : ''));
    box.append(h('div.step-h',
      h('input', { type: 'checkbox', checked: st.on, title: 'On / bypass', onchange: ev => E.change((ev.target.checked ? 'enable ' : 'bypass ') + def.label, ed => { ed.chain[i].on = ev.target.checked; }) }),
      h('span.nm', def.label), st.group ? h('span.tag', { title: 'Added by a levelling group (re-applying the group replaces it)' }, 'group ' + ((Q.state.groups.find(g => g.id === st.group) || {}).name || '')) : null,
      h('button.mini', { type: 'button', disabled: i === 0, title: 'Up', onclick: () => E.change('move step', ed => { const c = ed.chain; [c[i - 1], c[i]] = [c[i], c[i - 1]]; }) }, '▲'),
      h('button.mini', { type: 'button', disabled: i === n - 1, title: 'Down', onclick: () => E.change('move step', ed => { const c = ed.chain; [c[i + 1], c[i]] = [c[i], c[i + 1]]; }) }, '▼'),
      h('button.mini', { type: 'button', title: 'Remove', onclick: () => E.change('remove ' + def.label, ed => ed.chain.splice(i, 1)) }, '✕')));
    if (def.spec.length) {
      const grid = h('div.step-p');
      for (const [p, unit, min, max, step, scale] of def.spec) {
        const toR = v => scale === 'log' ? Math.log(v / min) / Math.log(max / min) * 1000 : v;
        const fromR = r => scale === 'log' ? min * Math.pow(max / min, r / 1000) : r;
        const numEl = h('input', { type: 'number', min, max, step, value: st.p[p] });
        const rng = h('input', { type: 'range', min: scale === 'log' ? 0 : min, max: scale === 'log' ? 1000 : max, step: scale === 'log' ? 1 : step, value: toR(st.p[p]) });
        const set = v => E.live(def.label + ' ' + p, ed => { ed.chain[i].p[p] = v; });
        rng.oninput = () => { const v = +(+fromR(+rng.value)).toFixed(step < 1 ? 3 : 0); numEl.value = v; set(v); };
        rng.onchange = () => E.liveEnd();
        // typed values are held to the step's range (a 500 dB gain would make a hard-clipped square); the field snaps on commit
        const clamp = v => Math.max(min, Math.min(max, v));
        numEl.oninput = () => { const v = parseFloat(numEl.value); if (isFinite(v)) { rng.value = toR(clamp(v)); set(clamp(v)); } };
        numEl.onchange = () => { const v = parseFloat(numEl.value), c = isFinite(v) ? clamp(v) : st.p[p]; if (+numEl.value !== c || !isFinite(v)) { numEl.value = c; set(c); } E.liveEnd(); };
        grid.append(h('span', unit), rng, numEl);
      }
      box.append(grid);
    }
    return box;
  }

  // --- drawing -------------------------------------------------------------------------------------------------------
  function totalDur() {
    const out = Q.outSync(E.key), [t0] = trimOf();
    return Math.max(srcDur(), out ? t0 + out.length / R : 0, 0.05) * 1.04;
  }
  E.zoomFit = () => { E.view = [0, totalDur()]; E.redraw(); };
  E.zoom = function (f, at) {
    if (!E.view) E.view = [0, totalDur()];
    const [a, b] = E.view, c = at != null ? at : (E.cursor != null && E.cursor >= a && E.cursor <= b ? E.cursor : (a + b) / 2);
    const span = Math.max(0.002, Math.min(totalDur(), (b - a) * f));
    let n0 = c - (c - a) / (b - a) * span; n0 = Math.max(0, Math.min(n0, totalDur() - span));
    E.view = [n0, n0 + span]; E.redraw();
  };
  E.redraw = function () {
    if (!E.key || !els.srcCv || !els.srcCv.isConnected) return;
    if (E.viewFor !== src()) { E.view = null; E.viewFor = src(); }   // (new audio: fit it)
    if (!E.view) E.view = [0, totalDur()];
    const a = Q.assets[E.key], e = edit(), [t0, t1] = trimOf(), v = E.view, x = src(), out = Q.outSync(E.key), sp = speedOf();
    const pv = Q.previewState(), mine = pv && pv.key === E.key;
    const g = (Q.gameSync(E.key) || {}).info || {};
    const accent = Q.css('--accent');
    const markers = [];
    if (a.guide) markers.push({ t: t0 + a.guide * sp, color: '#fab219', dash: [4, 3], label: 'guide ' + a.guide + 's', row: 1 });
    if (t0 + a.cap * sp < srcDur() + 1) markers.push({ t: t0 + a.cap * sp, color: '#d03b3b', dash: [6, 3], label: 'game cap ' + a.cap + 's' });
    Q.drawWave(els.srcCv, x, { t0: v[0], t1: v[1], dim: [t0, t1], regions: [{ a: t0, b: t1, fill: Q.css('--trim') }].concat(E.sel ? [{ a: E.sel[0], b: E.sel[1], fill: Q.css('--sel') }] : []),
      fades: e && (e.fadeIn || e.fadeOut) ? { a: t0, b: t1, fi: e.fadeIn, fo: e.fadeOut } : null, markers, cursor: E.cursor,
      playhead: mine && (pv.tag === 'src' || (pv.tag === 'orig' && !(e && e.src))) ? pv.pos : null });
    // output, from the trim start
    const om = [];
    if (out) {
      const cut = Q.leadingSilence(out) / R;
      if (cut > 0.0005) om.push({ t: t0 + cut, color: '#9cc3ef', label: 'game starts', dash: [2, 2], row: 2 });
      if (a.guide) om.push({ t: t0 + cut + a.guide, color: '#fab219', dash: [4, 3], label: 'guide', row: 1 });
      om.push({ t: t0 + cut + a.cap, color: '#d03b3b', dash: [6, 3], label: 'cap ' + a.cap + 's' });
    }
    let ph = null;
    if (mine && pv.tag === 'out') ph = t0 + pv.pos;
    if (mine && pv.tag === 'game') ph = t0 + (g.cutStart || 0) + pv.pos;
    Q.drawWave(els.outCv, out, { t0: v[0], t1: v[1], offset: t0, color: Q.css('--wave-out'), markers: om, playhead: ph, labels: false });
    if (E.spec) Q.drawSpectrogram(els.specCv, out, { t0: v[0], t1: v[1], offset: t0, playhead: ph });
    // handles
    const W = els.srcLane.clientWidth, X = t => (t - v[0]) / (v[1] - v[0]) * W;
    const place = (el, t) => { const px = X(t); el.style.left = px + 'px'; el.style.display = px < -6 || px > W + 6 ? 'none' : ''; };
    place(els.hStart, t0); place(els.hEnd, t1);
    place(els.hFi, t0 + (e ? e.fadeIn : 0)); place(els.hFo, t1 - (e ? e.fadeOut : 0));
    const span = v[1] - v[0], tot = totalDur();
    els.scroll.value = tot > span ? v[0] / (tot - span) * 1000 : 0;
    els.readout.textContent = 'view ' + fmt(v[0], 3) + '-' + fmt(v[1], 3) + 's' + (E.cursor != null ? '  cursor ' + fmt(E.cursor, 3) + 's' : '') + (E.sel ? '  sel ' + fmt(E.sel[1] - E.sel[0], 3) + 's' : '');
  };
  // animate the playhead while a preview plays
  (function anim() { if (Q.previewState() && E.key) E.redraw(); requestAnimationFrame(anim); })();
  Q.onPreviewChanged = () => {
    E.redraw();
    document.querySelectorAll('td.cell.playing').forEach(c => c.classList.remove('playing'));
    const p = Q.previewState(); if (p) document.querySelectorAll('td.cell[data-key="' + p.key + '"]').forEach(c => c.classList.add('playing'));
  };

  function tAt(ev) { const r = els.srcLane.getBoundingClientRect(), v = E.view; return v[0] + (ev.clientX - r.left) / r.width * (v[1] - v[0]); }
  function wireHandles() {
    const drag = (el, label, fn) => {
      el.onpointerdown = ev => {
        ev.preventDefault(); ev.stopPropagation(); el.setPointerCapture(ev.pointerId);
        el.onpointermove = mv => E.live(label, ed => fn(ed, Math.max(0, Math.min(srcDur(), tAt(mv)))));
        el.onpointerup = () => { el.onpointermove = null; E.liveEnd(); };
      };
    };
    drag(els.hStart, 'trim start', (ed, t) => { const end = trimOf()[1]; ed.trim[0] = Math.min(t, end - 0.001); });
    drag(els.hEnd, 'trim end', (ed, t) => { ed.trim[1] = Math.max(t, (ed.trim[0] || 0) + 0.001); });
    drag(els.hFi, 'fade in', (ed, t) => { const [a, b] = trimOf(); ed.fadeIn = Math.max(0, Math.min(t - a, b - a)); });
    drag(els.hFo, 'fade out', (ed, t) => { const [a, b] = trimOf(); ed.fadeOut = Math.max(0, Math.min(b - t, b - a)); });
  }
  function wireCanvas() {
    const lane = els.srcLane;
    lane.onpointerdown = ev => {
      if (ev.target.classList.contains('handle')) return;
      const t = tAt(ev);
      if (ev.shiftKey) {   // shift-drag: a play selection
        lane.setPointerCapture(ev.pointerId); E.sel = [t, t];
        lane.onpointermove = mv => { const u = tAt(mv); E.sel = [Math.min(t, u), Math.max(t, u)]; E.redraw(); };
        lane.onpointerup = () => { lane.onpointermove = null; if (E.sel[1] - E.sel[0] < 0.002) E.sel = null; E.redraw(); };
      } else { E.cursor = t; E.sel = null; E.redraw(); }
    };
    lane.ondblclick = ev => { E.cursor = tAt(ev); Q.playPreview(src(), { key: E.key, tag: 'src', from: E.cursor }); };
    els.outLane.onclick = ev => { const r = els.outLane.getBoundingClientRect(), v = E.view, t = v[0] + (ev.clientX - r.left) / r.width * (v[1] - v[0]); const out = Q.outSync(E.key); if (out) Q.playPreview(out, { key: E.key, tag: 'out', from: Math.max(0, t - trimOf()[0]) }); };
    const wheel = ev => {
      ev.preventDefault();
      if (ev.shiftKey || Math.abs(ev.deltaX) > Math.abs(ev.deltaY)) { const v = E.view, span = v[1] - v[0], d = (ev.deltaX || ev.deltaY) / 500 * span, tot = totalDur(); const n0 = Math.max(0, Math.min(tot - span, v[0] + d)); E.view = [n0, n0 + span]; E.redraw(); }
      else E.zoom(ev.deltaY > 0 ? 1.25 : 0.8, tAt(ev));
    };
    for (const l of [lane, els.outLane, els.specLane]) l.addEventListener('wheel', wheel, { passive: false });
  }

  // --- metrics ------------------------------------------------------------------------------------------------------
  function fillMetrics() {
    if (!els.metrics || !els.metrics.isConnected) return;
    const a = Q.assets[E.key], out = Q.outSync(E.key), gm = Q.gameSync(E.key), rows = [['File', out ? Q.measure(out) : null], ['In game', gm ? Q.measure(gm.v) : null], ['Original', a.orig ? Q.measure(a.orig) : null]];
    const m = els.metrics; m.innerHTML = '';
    for (const c of ['', 'length', 'LUFS (int)', 'max mom.', 'true peak', 'sample pk', 'heard (game)', 'game lvl 20ms']) m.append(h('span.hdr', c));
    for (const [n, x] of rows) {
      m.append(h('span.hdr', n));
      if (!x) { for (let i = 0; i < 7; i++) m.append(h('span', '...')); continue; }
      const over = a.guide && x.dur > a.guide + 1e-3;
      m.append(h('span', { style: { color: over ? 'var(--serious)' : '' } }, h('b', fmt(x.dur, 3)), ' s'), h('span', h('b', fmt(x.lufs)), ' LUFS'), h('span', fmt(x.mmax)), h('span', { style: { color: x.tp > -1 ? 'var(--warn)' : '' } }, fmt(x.tp) + ' dBTP'),
        h('span', fmt(x.peak) + ' dBFS'), h('span', fmt(x.heard) + ' dB'), h('span', fmt(x.game) + ' dB'));
    }
    const g = Q.gameSync(E.key), e = edit();
    if (els.levelInfo) {
      const i = g && g.info;
      els.levelInfo.textContent = !e ? (i && i.inherited ? 'In the game: your DEFAULT edit' + (i.retro ? ', chiptuned' : '') + '.' : 'Shipped sound, as the game has it.')
        : !i ? '' : e.levelSet ? 'The game keeps this level (listed in sound-levels.txt).' + (i.cutStart > 0.0005 ? ' Leading ' + fmt(i.cutStart * 1000, 0) + ' ms cut.' : '')
          : i.mode === 'peak 0.89' ? 'Nothing to match it to: the game normalises it to a 0.89 peak (' + (i.gainDb >= 0 ? '+' : '') + fmt(i.gainDb, 1) + ' dB). Tick "keep my level" to keep yours.'
          : 'The game rescales it ' + (i.gainDb >= 0 ? '+' : '') + fmt(i.gainDb, 1) + ' dB to match ' + (a.kind === 'voice' ? 'the line it replaces' : 'DEFAULT\'s ' + a.name) + (i.mode === 'heard' ? ' (heard loudness: the hitsound ladder)' : ' (loudest 20 ms)') + '. Tick "keep my level" to stop it.';
    }
  }

  // --- actions --------------------------------------------------------------------------------------------------------
  E.setAB = function (w, play) { E.which = w; if (w !== 'orig') E.bMode = w; build(); if (play) E.play(); };
  E.cycleWhich = function () { const order = ['game', 'out', 'orig'], n = order[(order.indexOf(E.which) + 1) % 3]; if (n !== 'orig') E.bMode = n; E.which = n; build(); U.toast('Plays: ' + ({ game: 'as the game plays it', out: 'the exported file', orig: 'the shipped original' })[n]); };
  E.play = function () { if (!E.key) return; U.playKey(E.key, E.which); };
  E.playSelection = function () {
    if (!E.key) return;
    const [a, b] = E.sel || trimOf();
    Q.playPreview(src(), { key: E.key, tag: 'src', from: a, to: b });
  };
  E.setTrimAtCursor = function (which) {
    const pv = Q.previewState();
    const t = pv && pv.key === E.key && pv.tag === 'src' ? pv.pos : E.cursor;
    if (t == null) { U.toast('Click the source waveform to place the cursor first'); return; }
    E.change(which ? 'trim end' : 'trim start', ed => { if (which) ed.trim[1] = Math.max(t, (ed.trim[0] || 0) + 0.001); else ed.trim[0] = Math.min(t, trimOf()[1] - 0.001); });
  };
  E.autoTrim = function () {
    const x = src(); if (!x.length) return;
    const on = Q.leadingSilence(x, 0.03);
    let pk = 0; for (let i = 0; i < x.length; i++) pk = Math.max(pk, Math.abs(x[i]));
    let end = x.length - 1; const thr = pk * 0.001;
    while (end > on && Math.abs(x[end]) < thr) end--;
    const a = on / R, b = Math.min(x.length, end + R * 0.005) / R;
    E.change('auto trim silence', ed => { ed.trim = [a, b >= srcDur() - 1e-6 ? null : b]; });
    U.toast('Trimmed ' + fmt(a * 1000, 1) + ' ms at the start (the game\'s 3% rule) and ' + fmt((srcDur() - b) * 1000, 0) + ' ms of tail under -60 dB');
  };

  // copy this sound's chain (and optionally trim / fades / level flag) to others
  E.copyChain = async function () {
    const a = Q.assets[E.key], e = edit(); if (!e) return;
    const kind = a.kind, packs = kind === 'voice' ? Q.VOICES : Q.KITS, names = kind === 'voice' ? Q.LINES.concat(Q.allQuips()) : Q.SOUNDS;
    const boxes = [];
    const optChain = h('input', { type: 'checkbox', checked: true }), optTrim = h('input', { type: 'checkbox' }), optLevel = h('input', { type: 'checkbox' }), optReplace = h('input', { type: 'checkbox', checked: true });
    const grid = h('table', h('tr', h('th', ''), packs.map(p => h('th', p.name))));
    for (const n of names) {
      const tr = h('tr', h('td.num', n));
      for (const p of packs) {
        const k = Q.key(kind, p.dir, n);
        const cb = h('input', { type: 'checkbox', disabled: k === E.key || !Q.assets[k] }); cb.dataset.key = k; boxes.push(cb);
        tr.append(h('td', cb));
      }
      grid.append(tr);
    }
    const quick = (fn) => () => boxes.forEach(b => { if (!b.disabled) b.checked = fn(Q.assets[b.dataset.key]); });
    const body = [h('p', 'Copy ' + a.name + ' / ' + Q.packName(kind, a.pack) + '\'s steps to the sounds you tick. Steps added by a levelling group are not copied.'),
      h('div', { style: { display: 'flex', gap: '12px', marginBottom: '6px' } }, h('label', optChain, 'filter chain'), h('label', optTrim, 'trim and fades (same seconds)'), h('label', optLevel, '"keep my level"'), h('label', optReplace, 'replace their chain (else append)')),
      h('div', { style: { display: 'flex', gap: '6px', marginBottom: '6px' } },
        h('button', { type: 'button', onclick: quick(x => x.name === a.name) }, 'same ' + (kind === 'voice' ? 'line, every voice' : 'sound, every kit')),
        h('button', { type: 'button', onclick: quick(x => x.pack === a.pack) }, 'all of ' + Q.packName(kind, a.pack)),
        h('button', { type: 'button', onclick: quick(() => false) }, 'none')),
      grid];
    const ok = await U.dialog('Copy chain to...', body, [{ label: 'Cancel', value: false }, { label: 'Copy', primary: true, value: true }]);
    if (!ok) return;
    const targets = boxes.filter(b => b.checked).map(b => b.dataset.key);
    const steps = e.chain.filter(s => !s.group);
    Q.commit('copy chain to ' + targets.length, () => {
      for (const k of targets) {
        const t = Q.state.edits[k] || (Q.state.edits[k] = Q.newEdit());
        if (optChain.checked) { const copies = steps.map(s => Object.assign(JSON.parse(JSON.stringify(s)), { id: Q.uid() })); t.chain = optReplace.checked ? t.chain.filter(s => s.group).concat(copies) : t.chain.concat(copies); }
        if (optTrim.checked) { t.trim = e.trim.slice(); t.fadeIn = e.fadeIn; t.fadeOut = e.fadeOut; }
        if (optLevel.checked) t.levelSet = e.levelSet;
      }
    });
    U.toast('Copied to ' + targets.length + ' sounds');
  };

  // generator: a thump (falling sine) + band noise, like the game's own synthesis
  E.generator = async function () {
    const a = Q.assets[E.key];
    const g = { len: 0.3, f0: 600, f1: 120, sweep: 0.03, tone: 1, noise: 0.5, noiseLo: 300, noiseHi: 5000, attack: 0.002, decay: 0.06, seed: 1 };
    const fields = [['len', 'length s', 0.01, 3, 0.01], ['f0', 'start Hz', 20, 8000, 1], ['f1', 'end Hz', 20, 8000, 1], ['sweep', 'sweep time s', 0.001, 1, 0.001],
      ['tone', 'tone level', 0, 1, 0.01], ['noise', 'noise level', 0, 1, 0.01], ['noiseLo', 'noise low Hz', 20, 15000, 1], ['noiseHi', 'noise high Hz', 50, 20000, 1],
      ['attack', 'attack s', 0.0005, 0.5, 0.0005], ['decay', 'decay s', 0.002, 2, 0.001], ['seed', 'noise seed', 0, 999, 1]];
    const cv = h('canvas', { style: { width: '100%', height: '70px', display: 'block' } });
    const draw = () => { const x = Q.generate(g); Q.drawWave(cv, x, { t0: 0, t1: g.len, labels: true }); return x; };
    const kv = h('div.kv', fields.map(([k, label, min, max, step]) => [h('span', label), h('input', { type: 'number', min, max, step, value: g[k], oninput: ev => { g[k] = +ev.target.value; draw(); } })]));
    const body = [h('p', 'A starting point for ' + a.name + ': a sine falling from start Hz to end Hz plus band-passed noise, with an attack and an exponential decay. Shape it further with the chain.'), kv, cv];
    setTimeout(draw, 30);
    const ok = await U.dialog('Design a sound', body, [{ label: 'Preview', onclick: () => { Q.playPreview(draw(), { key: E.key, tag: 'gen' }); return false; } }, { label: 'Cancel', value: false }, { label: 'Use it', primary: true, value: true }]);
    if (!ok) return;
    const id = Q.addSource('generated ' + g.f0 + '-' + g.f1 + ' Hz', Q.generate(g));
    E.change('design ' + a.name, ed => { ed.src = id; ed.made = 'gen'; delete ed.origin; ed.trim = [0, null]; ed.fadeIn = 0; ed.fadeOut = 0; });
  };
})();
