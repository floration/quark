/* ===== UI: shell ===================================================================================
   Element helper, tabs, selection, toast, dialogs, menus, keyboard shortcuts, the output meter, autosave. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui = {};

  // h('div.cls#id', {attrs / on*: handlers / style}, ...children)
  U.h = function (sel, attrs, ...kids) {
    const m = sel.match(/^([a-z0-9]+)?((?:[.#][\w-]+)*)$/i);
    const el = document.createElement(m && m[1] || 'div');
    if (m && m[2]) for (const p of m[2].match(/[.#][\w-]+/g)) p[0] === '.' ? el.classList.add(p.slice(1)) : (el.id = p.slice(1));
    if (attrs && (typeof attrs !== 'object' || attrs instanceof Node || Array.isArray(attrs))) { kids.unshift(attrs); attrs = null; }
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v == null || v === false) continue;
      if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k === 'html') el.innerHTML = v;
      else if (k in el && k !== 'list' && typeof v !== 'string') el[k] = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
    const add = c => { if (c == null || c === false) return; if (Array.isArray(c)) c.forEach(add); else el.appendChild(c instanceof Node ? c : document.createTextNode(String(c))); };
    kids.forEach(add);
    return el;
  };
  const h = U.h;
  U.$ = s => document.querySelector(s);
  U.fmt = (v, d) => (v == null || !isFinite(v)) ? (v === -Infinity ? '-inf' : '--') : v.toFixed(d == null ? 1 : d);
  U.esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  // --- toast ---------------------------------------------------------------------------------------------------
  let toastT = 0;
  U.toast = function (msg, err) {
    const t = U.$('#toast'); t.textContent = msg; t.className = 'show' + (err ? ' err' : '');
    clearTimeout(toastT); toastT = setTimeout(() => { t.className = ''; }, err ? 6000 : 2600);
  };
  Q.log = []; // (tests read errors from here)
  U.fail = function (where, e) { console.warn(where, e); Q.log.push(where + ': ' + (e && e.message || e)); U.toast(where + ': ' + (e && e.message || e), true); };

  // --- dialog: U.dialog(title, body nodes, buttons [{label, primary, value, onclick}]) -> Promise(value) ---------
  // (opts.second: the second dialog, shown over the first one: #dlg2)
  U.dialog = function (title, body, buttons, opts) {
    const dlg = U.$(opts && opts.second ? '#dlg2' : '#dlg'), form = dlg.querySelector('form');
    form.innerHTML = '';
    form.append(h('div.dlg-h', title), h('div.dlg-b', body));
    const foot = h('div.dlg-f');
    return new Promise(resolve => {
      for (const b of buttons || [{ label: 'Close', value: null }]) {
        if (b.node) { foot.append(b.node); continue; }
        foot.append(h('button' + (b.primary ? '.primary' : ''), { type: 'button', onclick: async ev => {
          if (b.onclick) { const keep = await b.onclick(ev); if (keep === false) return; }
          dlg.close(); resolve(b.value);
        } }, b.label));
      }
      form.append(foot);
      dlg.onclose = () => resolve(null);
      dlg.showModal();
    });
  };
  U.closeDialog = () => { const d = U.$('#dlg'); if (d.open) d.close(); };
  U.closeDialog2 = () => { const d = U.$('#dlg2'); if (d.open) d.close(); };

  // --- tabs ------------------------------------------------------------------------------------------------------
  Q.tab = 'sounds';
  U.showTab = function (tab) {
    Q.tab = tab;
    document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.tab === tab));
    document.querySelectorAll('.view').forEach(v => v.classList.toggle('on', v.id === 'view-' + tab));
    try { localStorage.setItem('qse.tab', tab); } catch (e) { }
    U.refreshView();
  };
  U.views = {};   // tab -> {render()}
  U.refreshView = function () { const v = U.views[Q.tab]; if (v) { try { v.render(); } catch (e) { U.fail('view', e); } } };

  // --- selection ---------------------------------------------------------------------------------------------------
  Q.sel = null;
  U.select = function (key, play) {
    if (!Q.assets[key]) return;
    Q.sel = key;
    document.querySelectorAll('td.cell.sel').forEach(c => c.classList.remove('sel'));
    document.querySelectorAll('td.cell[data-key="' + key + '"]').forEach(c => c.classList.add('sel'));
    U.editor && U.editor.show(key);
    if (play) U.playKey(key);
  };
  // play an asset as the game would (or the file / the shipped one)
  U.playKey = async function (key, which, opts) {
    const a = Q.assets[key]; if (!a) return;
    try {
      const id = a.kind === 'voice' ? 'voice:' + a.pack : 'kit:' + a.pack;
      if (!Q.loaded[id]) await Q.loadPack(a.kind === 'voice' ? 'voice' : 'kit', a.pack);
      which = which || 'game';
      const x = which === 'orig' ? a.orig : which === 'out' ? await Q.getOut(key) : (await Q.getGame(key)).v;
      if (!x || !x.length) { U.toast(a.name + ': no sound yet'); return; }
      const pitch = opts && opts.pitch || (U.randomPitch ? 0.9 + Math.random() * 0.19 : 1);
      Q.playPreview(x, Object.assign({ key, tag: which, pitch }, opts || {}));
    } catch (e) { U.fail('play', e); }
  };
  U.randomPitch = false;

  // --- state changes: refresh what shows, autosave a moment later ---------------------------------------------------
  let saveT = 0;
  Q.onStateChanged = function (label) {
    U.missing && U.missing.ensureAssets();   // (your own names' slots follow state.customs, after undo too)
    U.updateUndo();
    U.refreshView();
    U.editor && U.editor.refresh();
    clearTimeout(saveT);
    saveT = setTimeout(() => Q.autosave(), 1200);
  };
  Q.autosaveSoon = () => { clearTimeout(saveT); saveT = setTimeout(() => Q.autosave(), 1200); };
  Q.onMixChanged = function () { U.refreshView(); U.scenes && U.scenes.applyMix(); saveMixSoon(); };
  function saveMixSoon() { clearTimeout(saveT); saveT = setTimeout(() => Q.autosave(), 1200); }
  U.updateUndo = function () {
    const u = U.$('#btnUndo'), r = U.$('#btnRedo');
    u.disabled = !Q.canUndo(); r.disabled = !Q.canRedo();
    u.title = Q.canUndo() ? 'Undo ' + Q.undoLabel() + ' (Ctrl+Z)' : 'Nothing to undo';
    r.title = Q.canRedo() ? 'Redo ' + Q.redoLabel() + ' (Ctrl+Shift+Z)' : 'Nothing to redo';
  };
  U.doUndo = () => { const l = Q.undo(); U.toast(l ? 'Undid: ' + l : 'Nothing to undo'); };
  U.doRedo = () => { const l = Q.redo(); U.toast(l ? 'Redid: ' + l : 'Nothing to redo'); };

  // --- solo / mute buttons -----------------------------------------------------------------------------------------
  U.smButtons = function (kind, idSolo, idMute) {   // kind: 'Sound' | 'Pack'
    const s = h('button.solo' + (Q.mix['solo' + kind].has(idSolo) ? '.on' : ''), { type: 'button', title: 'Solo (in the scenes and row / column play)', onclick: e => { e.stopPropagation(); Q.toggleMix('solo' + kind, idSolo); } }, 'S');
    const m = h('button.mute' + (Q.mix['mute' + kind].has(idMute) ? '.on' : ''), { type: 'button', title: 'Mute (in the scenes)', onclick: e => { e.stopPropagation(); Q.toggleMix('mute' + kind, idMute); } }, 'M');
    return h('span.sm', s, m);
  };

  // --- the output meter (peak, falls back) ------------------------------------------------------------------------
  function meterLoop() {
    const cv = U.$('#meter'), g = cv.getContext('2d');
    let held = 0;
    const buf = new Float32Array(2048);
    const tick = () => {
      let pk = 0;
      if (Q.analyser) { Q.analyser.getFloatTimeDomainData(buf); for (let i = 0; i < buf.length; i++) pk = Math.max(pk, Math.abs(buf[i])); }
      held = Math.max(pk, held * 0.92);
      const db = 20 * Math.log10(held + 1e-9), f = Math.max(0, Math.min(1, (db + 48) / 48));
      g.fillStyle = '#0b0d10'; g.fillRect(0, 0, cv.width, cv.height);
      g.fillStyle = db > -1 ? '#d03b3b' : db > -6 ? '#fab219' : '#0ca30c'; g.fillRect(0, 2, f * cv.width, cv.height - 4);
      requestAnimationFrame(tick);
    };
    tick();
  }

  // --- keyboard ------------------------------------------------------------------------------------------------------
  U.keys = [
    ['Space', 'play / stop the selected sound (in Scenes: the scene)'], ['Shift+Space', 'play the selection (between the trim handles)'],
    ['A / B', 'A: hear the shipped original, B: hear your edit'], ['G', 'cycle what plays: game / file / original'],
    ['[  ]', 'set trim start / end at the cursor (click the wave to place it)'], ['Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y', 'undo / redo'],
    ['Arrow keys', 'move the selection in the matrix (left / right: packs)'], ['Enter', 'play the selected cell'],
    ['S / M', 'solo / mute the selected sound'], ['F', 'favourite the selected sound'], ['R', 'random pitch preview on / off'],
    ['+ / - / 0', 'zoom the waveform in / out / fit'], ['1 ... 6', 'switch tabs'], ['?', 'this list'],
  ];
  U.showHelp = () => U.dialog('Keyboard shortcuts', h('div.keys', U.keys.map(([k, d]) => [h('kbd', k), h('span', d)])));
  function onKey(e) {
    const t = e.target, typing = t && (t.tagName === 'INPUT' && !['checkbox', 'range', 'button'].includes(t.type) || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT');
    if (U.$('#dlg').open || U.$('#dlg2').open) return;
    const k = e.key, ctrl = e.ctrlKey || e.metaKey;
    if (ctrl && (k === 'z' || k === 'Z')) { if (typing) return; e.preventDefault(); e.shiftKey ? U.doRedo() : U.doUndo(); return; }
    if (ctrl && (k === 'y' || k === 'Y')) { if (typing) return; e.preventDefault(); U.doRedo(); return; }
    if (typing || ctrl || e.altKey) return;
    if (k === ' ') {
      e.preventDefault();
      if (Q.tab === 'scenes') { U.scenes.toggle(); return; }
      if (Q.previewState()) { Q.stopPreview(); return; }
      if (Q.sel) e.shiftKey ? U.editor.playSelection() : U.editor.play();
      return;
    }
    const tabs = ['sounds', 'voices', 'scenes', 'groups', 'missing', 'overview'];
    if (k >= '1' && k <= '6') { U.showTab(tabs[+k - 1]); return; }
    if (k === '?') { U.showHelp(); return; }
    if (k === 'r' || k === 'R') { U.randomPitch = !U.randomPitch; U.toast('Random pitch preview ' + (U.randomPitch ? 'on (0.90 to 1.09, like footsteps)' : 'off')); return; }
    if (k.startsWith('Arrow') && (Q.tab === 'sounds' || Q.tab === 'voices')) { e.preventDefault(); U.library.move(k); return; }
    if (!Q.sel) return;
    const a = Q.assets[Q.sel];
    if (k === 'Enter') U.playKey(Q.sel);
    else if (k === 'a' || k === 'A') U.editor.setAB('orig', true);
    else if (k === 'b' || k === 'B') U.editor.setAB(U.editor.which === 'orig' ? 'game' : U.editor.which, true);
    else if (k === 'g' || k === 'G') U.editor.cycleWhich();
    else if (k === '[') U.editor.setTrimAtCursor(0);
    else if (k === ']') U.editor.setTrimAtCursor(1);
    else if (k === 's' || k === 'S') Q.toggleMix('soloSound', Q.soundId(a.kind, a.name));
    else if (k === 'm' || k === 'M') Q.toggleMix('muteSound', Q.soundId(a.kind, a.name));
    else if (k === 'f' || k === 'F') U.toggleFav(Q.sel);
    else if (k === '+' || k === '=') U.editor.zoom(0.5);
    else if (k === '-') U.editor.zoom(2);
    else if (k === '0') U.editor.zoomFit();
  }

  // favourites / notes live per sound name (all packs share them) in state.meta
  U.metaKey = key => { const a = Q.assets[key]; return a.kind + ':' + a.name; };
  U.isFav = key => !!(Q.state.meta[U.metaKey(key)] || {}).fav;
  U.toggleFav = key => Q.commit('favourite', () => { const m = Q.state.meta[U.metaKey(key)] = Q.state.meta[U.metaKey(key)] || {}; m.fav = !m.fav; });

  U.initShell = function () {
    document.querySelectorAll('#tabs button').forEach(b => b.onclick = () => U.showTab(b.dataset.tab));
    U.$('#btnUndo').onclick = U.doUndo; U.$('#btnRedo').onclick = U.doRedo;
    U.$('#btnHelp').onclick = U.showHelp;
    U.$('#monitor').oninput = e => { Q.monitorLevel = +e.target.value; if (Q.monitor) Q.monitor.gain.value = Q.monitorLevel; try { localStorage.setItem('qse.mon', e.target.value); } catch (er) { } };
    try { const m = localStorage.getItem('qse.mon'); if (m) { U.$('#monitor').value = m; Q.monitorLevel = +m; } } catch (e) { }
    document.querySelectorAll('.menu > button').forEach(b => b.onclick = e => { e.stopPropagation(); const m = b.parentNode, open = !m.classList.contains('open'); document.querySelectorAll('.menu.open').forEach(x => x.classList.remove('open')); m.classList.toggle('open', open); });
    document.addEventListener('click', () => document.querySelectorAll('.menu.open').forEach(x => x.classList.remove('open')));
    document.addEventListener('keydown', onKey);
    // dock resize
    const grip = U.$('#dockGrip');
    grip.onpointerdown = e => {
      grip.setPointerCapture(e.pointerId);
      const y0 = e.clientY, h0 = U.$('#dock').offsetHeight;
      grip.onpointermove = ev => { const nh = Math.max(60, Math.min(window.innerHeight - 160, h0 - (ev.clientY - y0))); document.documentElement.style.setProperty('--dock-h', nh + 'px'); U.editor && U.editor.redraw(); };
      grip.onpointerup = () => { grip.onpointermove = null; try { localStorage.setItem('qse.dock', U.$('#dock').offsetHeight); } catch (er) { } };
    };
    try { const d = localStorage.getItem('qse.dock'); if (d) document.documentElement.style.setProperty('--dock-h', d + 'px'); } catch (e) { }
    window.addEventListener('resize', () => { U.editor && U.editor.redraw(); if (Q.tab === 'scenes' || Q.tab === 'overview') U.refreshView(); });
    meterLoop();
    U.updateUndo();
  };
})();
