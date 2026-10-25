/* ===== UI ============================================================================================
   The library (every model in the export, grouped by kind, with thumbnails and badges), the view bar, the viewport
   (orbit / pan / zoom, click to pick a box, drag the gizmo), the model panel (status, provenance, warnings, part list,
   part fields), import, export, project files, keys. */
(function () {
  'use strict';
  const Q = window.Q, S = Q.scene, R = Q.R;
  const U = Q.ui = {};
  const $ = (s, r) => (r || document).querySelector(s);
  U.$ = $;
  // h('div.cls#id', {attrs / on*}, children...)
  function h(tag, attrs, ...kids) {
    const m = tag.match(/^([a-z0-9]+)?((?:[.#][\w-]+)*)$/i), el = document.createElement(m[1] || 'div');
    for (const t of (m[2] || '').match(/[.#][\w-]+/g) || []) { if (t[0] === '.') el.classList.add(t.slice(1)); else el.id = t.slice(1); }
    if (attrs && (typeof attrs !== 'object' || attrs instanceof Node || Array.isArray(attrs))) { kids.unshift(attrs); attrs = null; }
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v == null || v === false) continue;
      if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k in el && k !== 'list' && k !== 'type') el[k] = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat(9)) if (c != null && c !== false) el.append(c instanceof Node ? c : String(c));
    return el;
  }
  U.h = h;
  let toastT = 0;
  U.toast = function (msg, ms) { const t = $('#toast'); t.textContent = msg; t.classList.add('on'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('on'), ms || 2600); };
  U.fail = function (what, e) { console.error(what, e); U.banner('Error (' + what + '): ' + (e && e.message || e), true); };
  // a modal: content nodes + buttons [{label, value, primary}] -> the value of the button pressed (Escape: null).
  // Every way out resolves before the dialog closes: a 'close' event is queued (it fires after d.close() returns), so
  // the close of one dialog would otherwise reach the next one opened from its result (and answer it with null).
  U.dialog = function (title, content, buttons) {
    const d = $('#dlg'), body = $('#dlgBody');
    body.innerHTML = '';
    body.append(h('h2', title), ...[].concat(content || []).flat(9).filter(c => c != null && c !== false));
    return new Promise(res => {
      const acts = h('div.dlg-actions');
      for (const b of buttons || [{ label: 'OK', value: true, primary: true }]) {
        acts.append(h('button' + (b.primary ? '.primary' : ''), { type: 'button', onclick: async () => {
          if (b.action) { try { const r = await b.action(); if (r === false) return; } catch (e) { U.fail(b.label, e); return; } }
          d.close(); res(b.value);
        } }, b.label));
      }
      body.append(acts);
      d.oncancel = e => { e.preventDefault(); d.close(); res(null); };   // (Escape)
      d.onclose = () => { if (!d.open) res(null); };   // (any other close; a stale event finds the next dialog open)
      d.showModal();
    });
  };
  U.banner = function (msg, err, actions) {
    const b = $('#banner'); b.hidden = false; b.className = err ? 'err' : ''; b.innerHTML = '';
    b.append(h('span', msg), ...(actions || []), h('span.spacer'), h('button.mini', { type: 'button', onclick: () => { b.hidden = true; } }, 'x'));
  };

  // --- the library --------------------------------------------------------------------------------------------------
  const thumbs = {}, inflight = {};   // key -> { url, sig } (shown), key -> sig (being drawn / read back)
  const thumbSig = key => (Q.ver[key] || 0) + '|' + S.view.palette + '|' + S.drawMode(key);
  let thumbQueue = [], thumbTimer = 0;
  function queueThumb(key) {
    if (inflight[key] === thumbSig(key) || thumbQueue.includes(key)) return;
    thumbQueue.push(key); if (!thumbTimer) thumbTimer = setTimeout(runThumbs, 30);
  }
  function runThumbs() {   // (a few per frame; each read back asynchronously, in order: no GPU stall)
    thumbTimer = 0;
    const t0 = performance.now();
    while (thumbQueue.length && performance.now() - t0 < 8) {
      const key = thumbQueue.shift(), sig = thumbSig(key);
      inflight[key] = sig;
      try {
        S.thumbnail(key, 96, url => {
          if (inflight[key] === sig) delete inflight[key];
          thumbs[key] = { url, sig };
          const img = document.querySelector('.card[data-key="' + CSS.escape(key) + '"] img');
          if (img) img.src = url;
        });
      } catch (e) { delete inflight[key]; console.warn(e); }
    }
    if (thumbQueue.length) thumbTimer = setTimeout(runThumbs, 16);
    S.dirty = true;
  }
  U.renderLibrary = function () {
    const list = $('#libList'), f = $('#libFilter').value.trim().toLowerCase(), onlyEdited = $('#libEdited').checked;
    list.innerHTML = '';
    for (const kind of Q.kinds) {
      const keys = Q.order.filter(k => Q.models[k].kind === kind && (!f || k.toLowerCase().includes(f)) && (!onlyEdited || Q.isEdited(k)));
      if (!keys.length) continue;
      list.append(h('div.kind', Q.kindTitle(kind) + ' (' + keys.length + ')'));
      const cards = h('div.cards');
      for (const key of keys) cards.append(card(key));
      list.append(cards);
    }
  };
  function card(key) {
    const m = Q.models[key], t = thumbs[key];
    if (!t || t.sig !== thumbSig(key)) queueThumb(key);
    const probs = Q.problems(key), pv = Q.provFor(key), origin = pv.origin, edited = Q.isEdited(key), cur = Q.current(key);
    const badges = h('div.badges');
    if (edited) badges.append(h('span.badge.edited', 'edited'));
    else if (cur.shipped) badges.append(h('span.badge.o-shipped', { title: 'the shipped override (assets/overrides): what the game draws' }, 'shipped'));
    badges.append(h('span.badge.o-' + origin, { title: 'origin: ' + origin + (pv.by ? ', by ' + pv.by : '') + (pv.source ? '\n' + pv.source : '') + (pv.note ? '\n' + pv.note : '') +
      (edited ? '' : '\n(' + (cur.shipped ? 'the shipped model' : 'the default') + ': assets/provenance)') }, origin === 'third-party' ? '3rd party' : origin));
    if (m.shipped && m.shipped.err) badges.append(h('span.badge.err', { title: 'The shipped override is refused by the game (it draws the default): ' + m.shipped.err }, 'refused'));
    if (m.err) badges.append(h('span.badge.err', 'bad file'));
    else if (probs.some(p => p.level === 'error')) badges.append(h('span.badge.err', '!'));
    else if (probs.some(p => p.level === 'warn')) badges.append(h('span.badge.warn', '!'));
    return h('div.card' + (S.sel === key ? '.sel' : ''), { 'data-key': key, title: key + '\n' + m.desc, onclick: () => U.select(key) },
      t ? h('img', { src: t.url, alt: '' }) : h('img', { alt: '' }), badges, h('div.nm', m.name));
  }
  U.refreshCard = function (key) {
    const old = document.querySelector('.card[data-key="' + CSS.escape(key) + '"]');
    if (old) old.replaceWith(card(key));
  };

  // --- selection ------------------------------------------------------------------------------------------------------
  U.select = function (key, keepMode) {
    if (!Q.models[key]) return;
    const prev = S.sel;
    S.sel = key; S.part = -1;
    try { localStorage.setItem('qme.sel', key); } catch (e) { }
    const m = Q.models[key], modes = S.modesFor(key);
    if (!keepMode || !modes.includes(S.view.mode)) S.view.mode = modes.includes(S.view.mode) && prev && Q.models[prev] && Q.models[prev].kind === m.kind ? S.view.mode : 'orbit';
    if (m.kind === 'weapon') S.view.fpWeapon = key;
    if (m.kind === 'player') S.view.tpPlayer = key;
    S.build(); S.frame(key); S.build(); S.updateOverlay();
    document.querySelectorAll('.card.sel').forEach(c => c.classList.remove('sel'));
    const c = document.querySelector('.card[data-key="' + CSS.escape(key) + '"]'); if (c) c.classList.add('sel');
    U.syncTabs(); U.syncAvail(); U.renderPanel(); U.renderHud();
  };
  U.selectPart = function (i) {
    S.part = i;
    if (i >= 0 && !S.editable()) S.part = -1;
    S.updateOverlay();
    U.renderPanel();
  };

  // --- view bar -------------------------------------------------------------------------------------------------------
  const binds = [
    ['#vTeam', 'team'], ['#vDrawAs', 'drawAs'], ['#vPlay', 'play', 'chk'], ['#vCrouch', 'crouch', 'chk'], ['#vHitbox', 'hitbox', 'chk'], ['#vGrid', 'grid', 'chk'],
    ['#vShadows', 'shadows', 'chk'], ['#vSun', 'sunVis', 'sun'], ['#vOutline', 'outline'], ['#vOutlineW', 'outlineW', 'num'], ['#vFov', 'fov', 'num'], ['#vBright', 'bright', 'num'],
    ['#vYaw', 'yaw', 'num'], ['#vPitch', 'pitch', 'num'], ['#vVmX', 'vmX', 'num'], ['#vVmY', 'vmY', 'num'], ['#vVmZ', 'vmZ', 'num'], ['#vVmScale', 'vmScale', 'num'],
    ['#vHoldIt', 'holdIt', 'chk'], ['#vCrown', 'crown', 'chk'], ['#vCrownPlace', 'crownPlace', 'int'], ['#vLineupTeams', 'lineupTeams', 'chk'], ['#vLanded', 'landed', 'chk'],
    ['#vSpeed', 'speed', 'num'], ['#vGround', 'ground', 'chk'], ['#vFpLineup', 'fpLineup', 'chk'], ['#vPalette', 'palette'],
    ['#vBombLineup', 'bombLineup', 'chk'], ['#vBombBlink', 'bombBlink', 'chk'], ['#vCartNeutral', 'cartNeutral', 'chk'], ['#vGateReached', 'gateReached', 'chk']];
  U.syncViewBar = function () {
    const v = S.view;
    for (const [sel, k, t] of binds) {
      const el = $(sel);
      if (t === 'chk') el.checked = !!v[k]; else if (t === 'sun') el.checked = v[k] > 0.5; else el.value = v[k];
    }
    $('#vVmCustom').checked = !!v.vmColor; $('#vVmColor').value = v.vmColor || Q.linToHex(Q.TEAM.friendly);
    U.syncAvail();
  };
  // an option that does nothing in this view is disabled, and says why
  U.syncAvail = function () {
    const m = S.sel && Q.models[S.sel], mode = S.view.mode;
    const avail = (sel, ok, why) => {
      const el = $(sel), lab = el.closest('label') || el;
      if (lab.dataset.title == null) lab.dataset.title = lab.title || '';
      el.disabled = !ok; lab.classList.toggle('off', !ok); lab.title = ok ? lab.dataset.title : why;
    };
    avail('#vHoldIt', !(m && m.kind === 'weapon' && mode === 'tp'), 'Third person with a weapon selected: the player always holds it');
    avail('#vBombLineup', !S.holdIt(), 'Off while players hold the weapon: "it" (hide and seek / tag) carries no bomb');
    avail('#vTeam', !(m && m.kind === 'weapon' && (mode === 'orbit' || (mode === 'fp' && !S.view.fpLineup))),
      'A weapon in your hands is drawn in the viewmodel colour (More > Viewmodel colour), not a team colour');
  };
  function viewChanged(rebuildThumbs) {
    S.saveView(); S.build(); S.updateOverlay(); U.renderHud(); U.syncTabs();
    if (rebuildThumbs) U.renderLibrary();
  }
  U.syncTabs = function () {
    const modes = S.sel ? S.modesFor(S.sel) : ['orbit'];
    document.querySelectorAll('#tabs button').forEach(b => { b.disabled = !modes.includes(b.dataset.mode); b.classList.toggle('on', b.dataset.mode === S.view.mode); });
  };
  U.setMode = function (mode) {
    if (!S.sel || !S.modesFor(S.sel).includes(mode)) return;
    S.view.mode = mode; S.saveView(); S.build(); S.frame(S.sel); S.build(); S.updateOverlay(); U.syncTabs(); U.syncAvail(); U.renderHud();
  };

  // --- the HUD (what is drawn, warnings) ------------------------------------------------------------------------------
  U.renderHud = function () {
    const hud = $('#hud'); hud.innerHTML = '';
    if (!S.sel) return;
    const key = S.sel, cur = Q.current(key), mode = S.drawMode(key), m = Q.models[key];
    const tris = cur.type === 'boxes' ? cur.parts.length * 12 : cur.type === 'mesh' ? cur.mesh.tris : 0;
    const what = mode === 'builtin' ? 'drawn as the game\'s own boxes (1 px outlines' + (S.hasPartAnim(key) ? ', part animation' : '') + ')'
      : mode === 'boxfile' ? 'drawn as a box model file (the game\'s box path: 1 px outlines, crouch per box' + (S.hasPartAnim(key) ? '; no flicker / blink / wobble' : '') + ')'
        : 'drawn as a mesh override (no outline: the game draws meshes without one)';
    hud.append(h('div', m.kind + ' / ' + m.name + ': ' + tris + ' triangles, ' + what));
    if (S.view.mode === 'lineup') hud.append(h('div', 'the game\'s lineup: quark --host ' + (S.holdIt() ? 'hide' : S.bombMode() ? 'bomb' : 'ffa') + ' --map <map> --models --yaw ' + S.view.yaw + ' --pitch ' + S.view.pitch));
    for (const p of Q.problems(key)) if (p.level !== 'info') hud.append(h('div.' + p.level, p.text));
  };

  // --- the panel ------------------------------------------------------------------------------------------------------
  // Rebuilt after a change; the field that has the focus (data-fid) keeps it, with its caret, and the scroll stays:
  // Tab moves on through the fields and arrow keys keep stepping the value. A change rebuilds it on the next frame
  // (U.schedulePanel): after Tab has moved the focus, and once for several changes.
  const fmt = Q.fmt;
  let panelRaf = 0;
  U.schedulePanel = function () { if (!panelRaf) panelRaf = requestAnimationFrame(() => { panelRaf = 0; U.renderPanel(); }); };
  U.renderPanel = function () {
    if (panelRaf) { cancelAnimationFrame(panelRaf); panelRaf = 0; }
    const panel = $('#panel'), key = S.sel, scroll = panel.scrollTop, act = document.activeElement;
    const fid = act && panel.contains(act) && act.dataset ? act.dataset.fid : null;
    let caret = null;
    try { if (fid && act.selectionStart != null) caret = [act.selectionStart, act.selectionEnd]; } catch (e) { }
    buildPanel(panel, key);
    panel.scrollTop = scroll;
    if (fid) {
      const el = panel.querySelector('[data-fid="' + CSS.escape(fid) + '"]');
      if (el) { el.focus({ preventScroll: true }); try { if (caret) el.setSelectionRange(caret[0], caret[1]); } catch (e) { } }
    }
  };
  function buildPanel(panel, key) {
    panel.innerHTML = '';
    if (!key) { panel.append(h('div.empty', 'Pick a model on the left.')); return; }
    const m = Q.models[key], cur = Q.current(key), edited = Q.isEdited(key);
    panel.append(h('div.ph', h('h2', m.name), h('span.path', key + '.glb')));
    panel.append(h('div.desc', m.desc));
    const pv = Q.provFor(key);
    let status = edited ? (cur.type === 'boxes' ? 'Edited boxes (' + cur.parts.length + ')' : 'Imported mesh: ' + (cur.name || '')) :
      cur.shipped ? 'The shipped override (assets/overrides): ' + (cur.type === 'boxes' ? 'a box model, ' + cur.parts.length + ' boxes' : 'a mesh') :
        cur.type === 'boxes' ? 'Default: ' + cur.parts.length + ' boxes' : 'Default mesh';
    if (m.err) status = 'The export file could not be read: ' + m.err;
    panel.append(h('div.row', h('b', status)));
    if (m.shipped && m.shipped.err) panel.append(h('div.probs', h('div.prob.error', 'The shipped override (assets/overrides/' + key.replace(/[^/]*$/, m.shipped.name) + ') is refused by the game: ' + m.shipped.err + '. The game logs it and draws the default (shown here).')));
    if (!edited) panel.append(h('div.row', h('span.badge.o-' + pv.origin, pv.origin), h('span.dim', [pv.by ? 'by ' + pv.by : '', pv.source, pv.note].filter(Boolean).join(' / ') || 'no details'),
      h('span.faint', '(assets/provenance, ' + (cur.shipped ? 'the shipped model' : 'the default') + ')')));
    const known = Q.knownProv(key);
    if (known && known.origin) panel.append(h('div.row.faint', 'Last exported from this editor: ' + known.origin + (known.date ? ' on ' + known.date : '') + (known.note ? ' (' + known.note + ')' : '') +
      '. Loading that file back (Import, Project > Load an overrides folder) keeps its origin.'));
    const acts = h('div.row');
    if (edited) acts.append(h('button.danger', { type: 'button', title: 'Throw the edits away (undo brings them back)', onclick: () => {
      Q.beginChange(key); delete Q.state.edits[key]; if (Q.state.prov) delete Q.state.prov[key];   // (its origin goes with it)
      Q.endChange(key, 'reset'); S.part = -1; U.toast('Back to ' + (cur.shipped || Q.baseOf(key).shipped ? 'the shipped model' : 'the default') + ' (Ctrl+Z undoes it)');
    } }, 'Reset to default'));
    if (cur.type === 'mesh' && m.defParts) acts.append(h('span.faint', 'A mesh: edit it in a 3D program, or reset to edit the boxes.'));
    acts.append(h('button', { type: 'button', title: 'Replace this model with a .glb / .gltf', onclick: () => $('#fileImport').click() }, 'Import replacement...'));
    acts.append(h('button', { type: 'button', title: 'Start from one box: a model you make yourself (origin: human)', onclick: () => {
      Q.beginChange(key); Q.state.edits[key] = { type: 'boxes', parts: [Object.assign(Q.newPart(), { name: 'box', c: [0, m.kind === 'player' ? 1 : 0, 0], h: m.kind === 'player' ? [0.3, 0.5, 0.2] : [0.03, 0.03, 0.1] })] };
      Q.state.prov = Q.state.prov || {}; Q.state.prov[key] = { origin: 'human', auto: true, scratch: true };   // (part of the undo step)
      Q.endChange(key, 'scratch'); U.selectPart(0);
    } }, 'Start from scratch'));
    acts.append(h('button', { type: 'button', title: 'Download this model as the .glb the game reads', onclick: () => {
      const b = Q.gltf.exportBytes(key); if (b) Q.download(new Blob([b], { type: 'model/gltf-binary' }), m.name + '.glb');
    } }, 'Download .glb'));
    panel.append(acts);

    const probs = Q.problems(key);
    if (probs.length) panel.append(h('div.probs', probs.map(p => h('div.prob.' + p.level, p.text))));

    if (edited) panel.append(provSection(key));
    if (cur.type === 'mesh') {
      const mats = {};
      for (const p of cur.mesh.prims) { const k = p.material + ' -> ' + Q.SLOT_NAMES[p.slot] + (p.alpha < 0.999 ? ' (translucent ' + fmt(p.alpha, 2) + ')' : ''); mats[k] = (mats[k] || 0) + p.pos.length / 9; }
      panel.append(h('div.sect', h('h3', 'Materials -> tint slot'), h('div.mats', Object.entries(mats).map(([k, n]) => h('div', k + ': ' + n + ' triangles'))),
        h('p.faint', 'The material name picks the colour that changes in game (prefix: team, glow, metal, base, accent; anything else: drawn as authored). A slot the game does not colour for this model is drawn as authored too.')));
      return;
    }
    if (cur.type !== 'boxes') return;
    panel.append(partsSection(key, cur.parts));
    if (S.part >= 0 && cur.parts[S.part]) panel.append(partFields(key, cur.parts[S.part]));
  }

  function provSection(key) {
    const p = Q.provFor(key), st = (Q.state.prov && Q.state.prov[key]) || {};
    const set = fields => { Q.beginChange(key, 'prov.' + Object.keys(fields)[0]); Q.setProv(key, fields); };   // (an undo step)
    const origin = h('select', { 'data-fid': 'prov.origin', onchange: e => set({ origin: e.target.value }) },
      ['ai', 'human', 'third-party'].map(o => h('option', { value: o, selected: p.origin === o }, o)));
    const txt = (field, ph) => h('input', { type: 'text', 'data-fid': 'prov.' + field, value: st[field] || '', placeholder: ph || '', onchange: e => set({ [field]: e.target.value, ...(field === 'note' ? { auto: !e.target.value } : {}) }) });
    const by = h('input', { type: 'text', 'data-fid': 'prov.by', value: st.by || Q.defaultBy(), onchange: e => { set({ by: e.target.value }); Q.setDefaultBy(e.target.value); } });
    return h('div.sect', h('h3', 'Provenance (provenance.json)'),
      h('div.kv', h('span', 'Origin'), origin, h('span', 'By'), by,
        h('span', 'Source'), txt('source', p.origin === 'third-party' ? 'pack name / URL (required)' : 'Quark model editor'),
        p.origin === 'third-party' ? [h('span', 'Licence'), txt('licence', 'e.g. CC0, CC-BY 4.0 (author)')] : null,
        h('span', 'Note'), txt('note', p.note || '')),
      h('p.faint', 'human = made by a person (Start from scratch, or a tweak of a human-made shipped model); ai = a tweak of an AI-made model (the note says what changed, against ' + Q.describeBase(key) +
        '); third-party = from a pack. Written to provenance.json and into the .glb on export, keyed ' + Q.provId(key) + '.'));
  }

  // the swatch: the colour as drawn now (slot colour * mul + team * teamMul + add, this view's team / map)
  const swCol = new Float32Array(18);
  function slotColorCss(p) {
    const m = Q.models[S.sel], kind = m ? m.kind : '', name = m ? m.name : '';
    Q.slotColors(kind, name, Q.runtimePalette({ kind, name, team: S.teamColor(), map: S.palette(), gateReached: S.view.gateReached, bombBlink: S.view.bombBlink, cartTeam: S.view.cartNeutral ? null : undefined }), swCol);
    const s = (p.slot | 0) * 3;
    if (!s) return Q.linToHex(p.color);
    const mu = p.mul || [1, 1, 1], ad = p.add || [0, 0, 0], tm = p.teamMul || [0, 0, 0];
    return Q.linToHex([0, 1, 2].map(k => swCol[s + k] * mu[k] + swCol[3 + k] * tm[k] + ad[k]));
  }
  function partsSection(key, parts) {
    const tbody = h('tbody');
    parts.forEach((p, i) => {
      tbody.append(h('tr' + (i === S.part ? '.sel' : ''), { onclick: () => U.selectPart(i === S.part ? -1 : i) },
        h('td.num.faint', i + 1), h('td', h('span.sw', { style: { background: slotColorCss(p) } })), h('td.nm', p.name || '-'),
        h('td.faint', Q.SLOT_NAMES[p.slot | 0] + ((p.alpha == null ? 1 : p.alpha) < 1 ? ' a' + fmt(p.alpha, 2) : '')),
        h('td.num.faint', fmt(p.h[0] * 200, 1) + 'x' + fmt(p.h[1] * 200, 1) + 'x' + fmt(p.h[2] * 200, 1) + ' cm')));
    });
    const sec = h('div.sect', h('h3', 'Boxes (' + parts.length + ')', h('span.spacer'),
      h('button.mini', { type: 'button', onclick: () => addPart(key) }, '+ Add'),
      h('button.mini', { type: 'button', disabled: S.part < 0, onclick: () => dupPart(key, false) }, 'Duplicate'),
      h('button.mini', { type: 'button', disabled: S.part < 0, title: 'A copy mirrored across X = 0 (left / right)', onclick: () => dupPart(key, true) }, 'Mirror'),
      h('button.mini.danger', { type: 'button', disabled: S.part < 0, onclick: () => delPart(key) }, 'Delete')),
    h('div.partlist', h('table.parts', tbody)));
    setTimeout(() => { const r = sec.querySelector('tr.sel'); if (r) r.scrollIntoView({ block: 'nearest' }); }, 0);
    return sec;
  }
  // a change to the boxes of the model being edited (one undo step; `group` merges a drag / typing)
  U.change = function (key, group, fn, why) {
    Q.beginChange(key, group);
    const e = Q.ensureBoxEdit(key);
    if (!e) return;
    fn(e.parts);
    Q.endChange(key, why || 'edit');
  };
  function addPart(key) {
    const m = Q.models[key];
    U.change(key, null, parts => {
      const at = S.part >= 0 && parts[S.part] ? parts[S.part].c.slice() : [0, m.kind === 'player' ? 1 : 0, 0];
      if (S.part >= 0) at[1] += parts[S.part].h[1] + 0.05;
      parts.push(Object.assign(Q.newPart(), { c: at, name: 'box ' + (parts.length + 1) }));
      S.part = parts.length - 1;
    }, 'add');
  }
  function dupPart(key, mirror) {
    U.change(key, null, parts => {
      const p = Q.clonePart(parts[S.part]);
      if (mirror) p.c[0] = -p.c[0]; else p.c[0] += p.h[0] * 2 + 0.01;
      if (p.name) p.name += mirror ? ' (mirror)' : ' copy';
      parts.splice(S.part + 1, 0, p); S.part++;
    }, 'dup');
  }
  function delPart(key) {
    U.change(key, null, parts => { parts.splice(S.part, 1); S.part = Math.min(S.part, parts.length - 1); }, 'delete');
  }
  function num(val, step, on, title, fid, min) {
    return h('input', { type: 'number', step, min, value: fmt(val, 5), title: title || '', 'data-fid': fid, oninput: e => { const v = parseFloat(e.target.value); if (Number.isFinite(v)) on(v, true); },
      onchange: e => { const v = parseFloat(e.target.value); if (Number.isFinite(v)) on(v, false); } });
  }
  const MIN_HALF = 0.0005;   // (the smallest half size: what the gizmo stops at; the game needs over 0)
  function partFields(key, p) {
    const i = S.part, ref = Q.refOf(key), m = Q.models[key], slot = p.slot | 0;
    const set = (group, fn) => (v, typing) => U.change(key, 'f' + i + group, parts => fn(parts[i], v), typing ? 'typing' : 'field');
    const n3 = (group, vals, step, fn, title, min) => [0, 1, 2].map(k => num(vals[k], step, set(group + k, (q, v) => fn(q, v, k)), title, group + k, min));
    const shown = Q.colorAtRef(p, ref);   // (the colour at the export's palette: what the fields edit)
    const intensity = Math.max(1, shown[0], shown[1], shown[2]);
    const recol = (q, D) => Q.recolor(q, D, ref);
    const picker = h('input', { type: 'color', 'data-fid': 'picker', value: Q.linToHex(shown), title: 'sRGB colour (the game stores linear colour; over 1 = brighter, glowing)',
      oninput: e => U.change(key, 'f' + i + 'col', parts => { const c = Q.hexToLin(e.target.value); recol(parts[i], c.map(x => x * intensity)); }, 'typing'),
      onchange: () => U.renderPanel() });
    const formula = slot ? [
      h('div.grid3', { style: { marginTop: '6px' } },
        h('span', { title: 'drawn = ' + Q.SLOT_NAMES[slot] + ' colour * mul + team colour * teamMul + add' }, 'mul'), n3('mul', p.mul || [1, 1, 1], 0.01, (q, v, k) => { q.mul = (q.mul || [1, 1, 1]).slice(); q.mul[k] = Math.max(0, v); q.color = Q.colorAtRef(q, ref); }, 'times the ' + Q.SLOT_NAMES[slot] + ' colour', 0),
        h('span', 'add'), n3('add', p.add || [0, 0, 0], 0.01, (q, v, k) => { q.add = (q.add || [0, 0, 0]).slice(); q.add[k] = Math.max(0, v); q.color = Q.colorAtRef(q, ref); }, 'added, whatever the slot colour (a grey that stays light in a dark team colour)', 0),
        slot !== 1 ? [h('span', 'teamMul'), n3('tm', p.teamMul || [0, 0, 0], 0.01, (q, v, k) => { q.teamMul = (q.teamMul || [0, 0, 0]).slice(); q.teamMul[k] = Math.max(0, v); q.color = Q.colorAtRef(q, ref); }, 'times the team colour, on top (the cart\'s core)', 0)] : null),
      h('p.faint', 'Drawn = ' + Q.SLOT_NAMES[slot] + ' colour x mul' + (slot !== 1 ? ' + team colour x teamMul' : '') + ' + add. Recolouring rewrites mul (and keeps add while it fits).')] : null;
    return h('div.sect', h('h3', 'Box ' + (i + 1)),
      h('div.kv', h('span', 'Name'), h('input', { type: 'text', 'data-fid': 'name', value: p.name || '', placeholder: 'unnamed', onchange: e => U.change(key, null, parts => { parts[i].name = e.target.value; }, 'field') })),
      h('div.grid3', { style: { marginTop: '6px' } },
        h('span'), h('span.faint', 'x (right)'), h('span.faint', 'y (up)'), h('span.faint', 'z (back)'),
        h('span', 'Centre'), n3('c', p.c, 0.005, (q, v, k) => { q.c[k] = v; }, 'metres'),
        h('span', 'Half size'), n3('h', p.h, 0.005, (q, v, k) => { q.h[k] = Math.max(MIN_HALF, v); }, 'half the box\'s size, metres (at least ' + MIN_HALF + ')', MIN_HALF),
        h('span', 'Colour'), [0, 1, 2].map(k => num(shown[k], 0.01, set('col' + k, (q, v) => { const D = Q.colorAtRef(q, ref); D[k] = Math.max(0, v); recol(q, D); }),
          'linear ' + 'rgb'[k] + (slot ? ' at the export\'s palette (team: white)' : '') + ' (over 1: brighter than the light)', 'col' + k, 0))),
      h('div.kv', { style: { marginTop: '6px' } },
        h('span', 'Picker'), h('div.row', { style: { margin: 0 } }, picker,
          h('span.faint', 'x'), num(intensity, 0.05, set('int', (q, v) => { const D = Q.colorAtRef(q, ref), cur = Math.max(1, D[0], D[1], D[2]); recol(q, D.map(x => x / cur * Math.max(0, v))); }), 'intensity: the colour times this (over 1 glows)', 'int', 0),
          h('span.faint.num', 'sRGB ' + Q.linToHex(shown))),
        h('span', 'Tint slot'), h('select', { 'data-fid': 'slot', onchange: e => U.change(key, null, parts => { Q.setSlot(parts[i], +e.target.value, ref); }, 'field') },
          Q.SLOT_TITLES.map((t, s2) => h('option', { value: s2, selected: slot === s2 }, Q.SLOT_NAMES[s2] + ': ' + t + (s2 && Q.drivenSlots(m.kind, m.name).indexOf(s2) < 0 ? ' (not set for this model)' : '')))),
        h('span', 'Alpha'), num(p.alpha == null ? 1 : p.alpha, 0.05, set('a', (q, v) => { q.alpha = Math.max(0.01, Math.min(1, v)); }), 'under 1: see-through (projectiles only, drawn unlit, like the bubble)', 'alpha', 0.01),
        h('span', 'Flags'), h('div', h('label.chk', { title: 'Casts a shadow' }, h('input', { type: 'checkbox', 'data-fid': 'shadow', checked: p.shadow !== false, onchange: e => U.change(key, null, parts => { parts[i].shadow = e.target.checked; }, 'field') }), ' shadow '),
          h('label.chk', { title: 'Only seen from outside (face plates: not drawn on your own body in first person)' }, h('input', { type: 'checkbox', 'data-fid': 'visOnly', checked: !!p.visOnly, onchange: e => U.change(key, null, parts => { parts[i].visOnly = e.target.checked; }, 'field') }), ' outside only '),
          m.kind === 'player' ? h('label.chk', { title: 'A crouch squashes this box\'s height too (every box\'s centre height follows a crouch anyway; the default legs and body have it, the head not)' }, h('input', { type: 'checkbox', 'data-fid': 'crouch', checked: !!p.crouch, onchange: e => U.change(key, null, parts => { parts[i].crouch = e.target.checked; }, 'field') }), ' crouch') : null)),
      formula,
      h('p.faint', 'Slot: team = the team / accent colour, glow / metal / base / accent = the map\'s colours; the colour here is at the export\'s palette (team white). Exported as a box model (extras.quark version 2), the game draws these boxes exactly like its own: outlines, shadows, crouch, outside-only.'));
  }

  // --- the viewport: camera, picking, gizmo -----------------------------------------------------------------------------
  let drag = null, gmode = 'move';
  U.gizmoMode = m => { gmode = m; document.querySelectorAll('#gizmoMode button').forEach(b => b.classList.toggle('on', b.dataset.g === m)); };
  function initViewport() {
    const cv = $('#gl');
    cv.addEventListener('contextmenu', e => e.preventDefault());
    cv.addEventListener('pointerdown', e => {
      cv.setPointerCapture(e.pointerId);
      const r = cv.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
      const v = S.view, orbiting = v.mode === 'orbit' || v.mode === 'tp' || v.mode === 'flight' || v.mode === 'track';
      const ax = e.button === 0 && !e.shiftKey ? S.pickGizmo(x, y, r.width, r.height) : null;
      if (ax) {
        const f = S.focus, col = [f.model[ax.axis * 4], f.model[ax.axis * 4 + 1], f.model[ax.axis * 4 + 2]], s = Math.hypot(col[0], col[1], col[2]) || 1;
        const dir = col.map(c => c / s), ray = S.ray(x, y, r.width, r.height), t0 = S.axisParam(ax.origin, dir, ray);
        const p = Q.current(S.sel).parts[S.part];
        drag = { kind: 'gizmo', axis: ax.axis, dir, origin: ax.origin, scale: s, t0, c0: p.c.slice(), h0: p.h.slice(), alt: e.altKey, group: 'g' + Date.now() };
        return;
      }
      drag = { kind: e.button === 0 && !e.shiftKey ? (orbiting ? 'orbit' : 'look') : 'pan', x: e.clientX, y: e.clientY, moved: 0, button: e.button };
    });
    cv.addEventListener('pointermove', e => {
      if (!drag) return;
      const r = cv.getBoundingClientRect();
      if (drag.kind === 'gizmo') {
        const ray = S.ray(e.clientX - r.left, e.clientY - r.top, r.width, r.height), t = S.axisParam(drag.origin, drag.dir, ray);
        if (t == null || drag.t0 == null) return;
        let d = (t - drag.t0) / drag.scale;
        if (drag.axis === 1) d /= S.yk(Q.current(S.sel).parts[S.part], S.focus, gmode !== 'move');   // (crouched: y on screen is squashed)
        const snap = e.shiftKey ? 0.01 : 0.001;
        d = Math.round(d / snap) * snap;
        const a = drag.axis, key = S.sel, i = S.part;
        U.change(key, drag.group, parts => {
          const p = parts[i];
          if (gmode === 'move') p.c[a] = +(drag.c0[a] + d).toFixed(6);
          else if (drag.alt || e.altKey) p.h[a] = +Math.max(0.0005, drag.h0[a] + d).toFixed(6);   // both sides
          else { const nh = Math.max(0.0005, drag.h0[a] + d / 2); p.h[a] = +nh.toFixed(6); p.c[a] = +(drag.c0[a] + (nh - drag.h0[a])).toFixed(6); }
        }, 'typing');
        return;
      }
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      drag.x = e.clientX; drag.y = e.clientY; drag.moved += Math.abs(dx) + Math.abs(dy);
      const o = S.orbit;
      if (drag.kind === 'orbit') { o.yaw -= dx * 0.008; o.pitch = Math.max(-1.55, Math.min(1.55, o.pitch - dy * 0.008)); S.build(); S.updateOverlay(); }
      else if (drag.kind === 'look') { S.view.yaw = +(S.view.yaw - dx * 0.25).toFixed(2); S.view.pitch = +Math.max(-89, Math.min(89, S.view.pitch - dy * 0.25)).toFixed(2); S.build(); S.updateOverlay(); U.syncViewBar(); }
      else if (drag.kind === 'pan' && (S.view.mode === 'orbit' || S.view.mode === 'tp' || S.view.mode === 'flight' || S.view.mode === 'track')) {
        const k = o.dist * 0.0015, cy = Math.cos(o.yaw), sy = Math.sin(o.yaw);
        o.target[0] -= cy * dx * k; o.target[2] -= -sy * dx * k; o.target[1] += dy * k;
        S.build(); S.updateOverlay();
      }
    });
    cv.addEventListener('pointerup', e => {
      const d = drag; drag = null;
      if (!d) return;
      if (d.kind === 'gizmo') { Q.closeGroup(); U.renderPanel(); U.refreshCard(S.sel); return; }
      if (d.kind === 'look') S.saveView();
      if (d.moved < 4 && d.button === 0 && (d.kind === 'orbit' || d.kind === 'look')) {
        const r = cv.getBoundingClientRect();
        const i = S.pick(S.ray(e.clientX - r.left, e.clientY - r.top, r.width, r.height));
        U.selectPart(i);
      }
    });
    cv.addEventListener('wheel', e => {
      e.preventDefault();
      const v = S.view;
      if (v.mode === 'orbit' || v.mode === 'tp' || v.mode === 'flight' || v.mode === 'track') { S.orbit.dist = Math.max(0.05, Math.min(60, S.orbit.dist * Math.exp(e.deltaY * 0.0012))); S.build(); S.updateOverlay(); }
      else { v.fov = Math.max(60, Math.min(140, Math.round(v.fov + Math.sign(e.deltaY) * 2))); S.saveView(); S.build(); U.syncViewBar(); }   // (the game's range)
    }, { passive: false });
  }

  // --- import ---------------------------------------------------------------------------------------------------------
  U.importFiles = async function (fileList) {
    const files = [...fileList];
    const main = files.find(f => /\.(glb|gltf)$/i.test(f.name));
    if (!main) { U.toast('Pick a .glb or .gltf file'); return; }
    let key = S.sel;
    const stem = main.name.replace(/\.(glb|gltf)$/i, '').toUpperCase();
    const byName = Q.order.filter(k => Q.models[k].name === stem);
    if ((!key || Q.models[key].name !== stem) && byName.length === 1) key = byName[0];
    if (!key) { U.toast('Select the model to replace first'); return; }
    const extra = {};
    for (const f of files) if (f !== main) extra[f.name] = new Uint8Array(await f.arrayBuffer());
    const bytes = new Uint8Array(await main.arrayBuffer());
    let mesh;
    try { mesh = Q.gltf.parse(bytes, main.name, extra); } catch (e) { U.dialog('Cannot read ' + main.name, h('p', String(e.message || e) + '. The game would not load it either (it keeps the default).')); return; }
    let boxes = Q.boxes.partsOf(mesh, key, false);
    if (boxes && boxes.error) { U.dialog('Cannot use ' + main.name, h('p', 'Its box list (extras.quark version 2) fails the game\'s checks: ' + boxes.error + '. The game would log it and keep the default.')); return; }
    // the origin it comes with: written into the file by this editor, else what this editor last exported for the
    // model, else a box model is taken as a tweak of what it replaces and a mesh as third-party
    const rec = Q.provState(mesh.provenance) || Q.provState(Q.knownProv(key));
    const fields = rec ? rec : { origin: boxes ? Q.baseOf(key).prov.origin : 'third-party' };
    const originSel = h('select', ['third-party', 'human', 'ai'].map(o => h('option', { value: o, selected: fields.origin === o }, o)));
    const src = h('input', { type: 'text', placeholder: 'pack name, URL', value: fields.source || '' }), lic = h('input', { type: 'text', placeholder: 'CC0, CC-BY 4.0 (author), ...', value: fields.licence || '' });
    const note = h('input', { type: 'text', value: fields.note || '' });
    const target = h('select', Q.order.map(k => h('option', { value: k, selected: k === key }, k)));
    const ok = await U.dialog('Import ' + main.name, [
      h('div.kv', h('span', 'Replaces'), target),
      h('p', mesh.tris + ' triangles in ' + mesh.prims.length + ' material(s): ' + mesh.prims.map(p => p.material + ' -> ' + Q.SLOT_NAMES[p.slot]).join(', ') + '.'),
      boxes ? h('p', (mesh.extras && mesh.extras.version === 2 ? 'A box model (' + boxes.length + ' boxes in its extras): the game draws it like its own boxes.' : 'It is made of ' + boxes.length + ' boxes') + ' It opens as boxes you can edit.')
        : h('p', 'A mesh: shown as the game draws it (no outline, tinted by material name).'),
      mesh.tris > Q.MAX_TRIS ? h('p', { style: { color: '#ff8b8b' } }, 'Over the game\'s ' + Q.MAX_TRIS + ' triangle limit: the game would refuse it.') : null,
      h('div.kv', h('span', 'Origin'), originSel, h('span', 'Source'), src, h('span', 'Licence'), lic, h('span', 'Note'), note),
      mesh.provenance ? h('p', 'The file says: ' + (mesh.provenance.origin || '?') + (mesh.provenance.by ? ', by ' + mesh.provenance.by : '') + (mesh.provenance.date ? ', ' + mesh.provenance.date : '') + (mesh.provenance.note ? ' (' + mesh.provenance.note + ')' : '') + '.') : null,
      h('p.faint', 'For provenance.json: a model from a pack is third-party (fill in where it is from and its licence); human = you made it yourself.')],
    [{ label: 'Cancel', value: false }, { label: 'Replace', value: true, primary: true, action: () => {
      if (originSel.value === 'third-party' && !src.value.trim()) { U.toast('Third-party: say where it is from (Source)'); src.focus(); return false; }
    } }]);
    if (!ok) return;
    key = target.value;
    const prov = { origin: originSel.value, auto: true };
    if (rec && rec.origin === prov.origin) { if (rec.by) prov.by = rec.by; if (rec.scratch) prov.scratch = true; }
    if (src.value.trim()) prov.source = src.value.trim();
    if (lic.value.trim()) prov.licence = lic.value.trim();
    if (note.value.trim()) { prov.note = note.value.trim(); prov.auto = false; }
    try {
      const how = Q.importInto(key, bytes, main.name, extra, { prov });
      if (key !== S.sel) U.select(key);
      U.toast(main.name + ' replaces ' + key + (how === 'boxes' ? ' (as boxes)' : ' (as a mesh)'));
    } catch (e) { U.fail('import', e); }
  };

  // --- export -----------------------------------------------------------------------------------------------------------
  U.exportDialog = async function () {
    const keys = Q.exportList();
    if (!keys.length) { U.dialog('Nothing to export', h('p', 'No model is edited yet. Edit a model\'s boxes or import a replacement first.')); return; }
    const checks = {};
    const list = h('ul', keys.map(k => {
      const p = Q.problems(k), err = p.find(x => x.level === 'error'), pv = Q.provFor(k);
      const cb = h('input', { type: 'checkbox', checked: !err, disabled: !!err });
      checks[k] = cb;
      return h('li', h('label.chk', cb, ' ', h('code', Q.exportPath(k))), ' ', h('span.faint', pv.origin + (pv.note ? ': ' + pv.note : '')),
        err ? h('div', { style: { color: '#ff8b8b' } }, err.text) : null);
    }));
    const chosen = () => keys.filter(k => checks[k].checked);
    let provDir = await Q.loadHandle('provDir');
    const provName = h('code', provDir ? provDir.name : 'none chosen');
    const provRow = h('div.row', h('span', 'provenance.json goes to:'), provName,
      h('button.mini', { type: 'button', disabled: !Q.canPickFolder(), onclick: async () => {
        try { const d = await window.showDirectoryPicker({ id: 'quark-model-editor', mode: 'readwrite' }); provDir = d; await Q.saveHandle('provDir', d); provName.textContent = d.name; } catch (e) { }
      } }, 'Choose the Quark\\model-editor folder...'),
      h('button.mini', { type: 'button', onclick: () => Q.downloadProvenance() }, 'Download provenance.json'));
    const content = [
      h('p', 'Each model is written as ', h('code', 'models/<kind>/<NAME>.glb'), ', what the game loads in place of its model. A box model carries its box list (extras.quark version 2): the game draws it like its own boxes (outlines, crouch, shadows, team colours), only the rocket\'s flame flicker, the sticky\'s blink and the bubble\'s wobble stay still. An imported mesh is drawn lit and shadowed but without the 1 px outline.'),
      list,
      h('p', h('b', 'Choose folder and write'), ' (Chrome / Edge): pick ', h('code', '%APPDATA%\\quark\\overrides'), ' to try them in the game (restart it), or the repository\'s ', h('code', 'assets\\overrides'), ' to ship them. Picking ', h('code', '%APPDATA%\\quark'), ' or the repository folder works too. A same-named .gltf there is moved to .bak.'),
      h('p', h('b', 'Download .zip'), ': the same layout plus provenance.json, to unzip into the overrides folder.'),
      provRow];
    const res = await U.dialog('Export to game', content, [
      { label: 'Cancel', value: null },
      { label: 'Download .zip', value: 'zip' },
      { label: 'Choose folder and write', value: 'folder', primary: true }]);
    if (res === 'zip') {
      const r = Q.exportZip(chosen());
      U.toast('quark-model-overrides.zip: ' + r.files.length + ' model(s) + provenance.json' + (r.skipped.length ? ', ' + r.skipped.length + ' skipped' : ''));
      U.renderLibrary();
    } else if (res === 'folder') {
      if (!Q.canPickFolder()) { U.dialog('Not in this browser', h('p', 'Writing to a folder needs Chrome or Edge. Use Download .zip.')); return; }
      let root;
      try { root = await window.showDirectoryPicker({ id: 'quark-overrides', mode: 'readwrite' }); } catch (e) { return; }
      try {
        const ov = await Q.resolveOverrides(root);
        const there = (await Q.scanModels(ov.dir)).filter(f => !Q.isEdited(f.key) && Q.models[f.key]);
        let removeKeys = [];
        if (there.length) {
          const cbs = there.map(f => h('input', { type: 'checkbox' }));
          const ok = await U.dialog('Model files for unedited models', [h('p', 'These are in ' + ov.where + ' but not edited here (the game draws them in place of the default). Move any to .bak?'),
            h('ul', there.map((f, i) => h('li', h('label.chk', cbs[i], ' ', 'models/' + f.key.split('/').slice(1).join('/') + '  (' + f.name + ')'))))],
          [{ label: 'Cancel', value: false }, { label: 'Continue', value: true, primary: true }]);
          if (!ok) { U.toast('Export cancelled: nothing written'); return; }
          removeKeys = there.filter((f, i) => cbs[i].checked).map(f => f.key);
        }
        const r = await Q.exportToFolder(root, { keys: chosen(), removeKeys, provDir });
        U.dialog('Written', [h('p', 'Into ' + r.where + (r.guess ? ' (no overrides folder found: written into the folder you picked)' : '') + ':'),
          h('ul', r.written.map(w => h('li', h('code', w)))),
          r.moved.length ? [h('p', 'Moved to .bak:'), h('ul', r.moved.map(w => h('li', h('code', w))))] : null,
          r.skipped.length ? [h('p', 'Skipped:'), h('ul', r.skipped.map(s => h('li', s.key + ': ' + s.why)))] : null,
          r.provenance ? h('p', 'provenance.json updated: ' + r.provenance) : h('p', 'provenance.json: no model-editor folder chosen' + (r.provenanceError ? ' (' + r.provenanceError + ')' : '') + '. ',
            h('button.mini', { type: 'button', onclick: () => Q.downloadProvenance() }, 'Download provenance.json')),
          h('p.faint', 'Restart the game to load them. A file the game cannot read is logged ("override models/...: ...") and the default is kept.')]);
        U.renderLibrary();
      } catch (e) { U.fail('export', e); }
    }
  };

  // --- keys -------------------------------------------------------------------------------------------------------------
  const KEYS = [['Ctrl+Z / Ctrl+Shift+Z, Ctrl+Y', 'undo / redo'], ['1 - 6', 'orbit, first person, third person, in flight, lineup, track'], ['F', 'frame the model'],
    ['W / E', 'gizmo: move / resize (Alt: both sides; Shift: 1 cm steps)'], ['click', 'pick a box (orbit, third person; first person: the viewmodel)'],
    ['drag / right drag / wheel', 'orbit / pan / zoom (first person, lineup: look around / fov)'], ['arrows, PgUp / PgDn', 'nudge the box 1 mm in x / z, y (Shift: 1 cm)'],
    ['Ctrl+D, Delete', 'duplicate, delete the box'], ['M', 'mirror the box across x = 0'], ['Space', 'animations on / off'], ['Esc', 'deselect'], ['?', 'this list']];
  U.help = () => U.dialog('Keys', h('table.keys', KEYS.map(([k, d]) => h('tr', h('td', k), h('td', d)))));
  U.closeMenus = function () {
    const open = document.querySelectorAll('.menu.open');
    open.forEach(x => x.classList.remove('open'));
    return open.length > 0;
  };
  function onKey(e) {
    const t = e.target;
    if (e.key === 'Escape' && U.closeMenus()) { e.preventDefault(); if (t && t.blur && t.closest && t.closest('.menu')) t.blur(); return; }
    if (t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA') && t.type !== 'checkbox') return;
    if ($('#dlg').open) return;
    const k = e.key, ctrl = e.ctrlKey || e.metaKey, key = S.sel;
    if (ctrl && (k === 'z' || k === 'Z')) { e.preventDefault(); const r = e.shiftKey ? Q.redo() : Q.undo(); if (r) { if (r !== S.sel) U.select(r, true); } return; }
    if (ctrl && (k === 'y' || k === 'Y')) { e.preventDefault(); const r = Q.redo(); if (r && r !== S.sel) U.select(r, true); return; }
    if (ctrl && (k === 'd' || k === 'D')) { e.preventDefault(); if (S.part >= 0) dupPart(key, false); return; }
    if (ctrl) return;
    if (k >= '1' && k <= '6') { U.setMode(['orbit', 'fp', 'tp', 'flight', 'lineup', 'track'][+k - 1]); return; }
    if (k === 'f' || k === 'F') { S.frame(key); S.build(); S.updateOverlay(); return; }
    if (k === 'w' || k === 'W') { U.gizmoMode('move'); return; }
    if (k === 'e' || k === 'E') { U.gizmoMode('size'); return; }
    if (k === ' ') { e.preventDefault(); S.view.play = !S.view.play; U.syncViewBar(); viewChanged(); return; }
    if (k === '?') { U.help(); return; }
    if (k === 'Escape') { U.selectPart(-1); return; }
    if (S.part < 0 || !key) return;
    if (k === 'Delete' || k === 'Backspace') { e.preventDefault(); delPart(key); return; }
    if (k === 'm' || k === 'M') { dupPart(key, true); return; }
    const st = e.shiftKey ? 0.01 : 0.001, ax = { ArrowLeft: [0, -1], ArrowRight: [0, 1], ArrowUp: [2, -1], ArrowDown: [2, 1], PageUp: [1, 1], PageDown: [1, -1] }[k];
    if (ax) { e.preventDefault(); const i = S.part; U.change(key, 'nudge' + i, parts => { parts[i].c[ax[0]] = +(parts[i].c[ax[0]] + ax[1] * st).toFixed(6); }, 'field'); }
  }

  // --- wiring ------------------------------------------------------------------------------------------------------------
  U.init = function () {
    initViewport();
    document.querySelectorAll('#tabs button').forEach(b => b.addEventListener('click', () => U.setMode(b.dataset.mode)));
    document.querySelectorAll('#gizmoMode button').forEach(b => b.addEventListener('click', () => U.gizmoMode(b.dataset.g)));
    document.querySelectorAll('.menu > button').forEach(b => b.addEventListener('click', e => {
      const m = b.parentElement, open = m.classList.contains('open');
      document.querySelectorAll('.menu.open').forEach(x => x.classList.remove('open'));
      if (!open) m.classList.add('open');
      e.stopPropagation();
    }));
    document.addEventListener('click', e => { if (!e.target.closest('.menu-list.wide')) document.querySelectorAll('.menu.open').forEach(x => x.classList.remove('open')); });
    const pal = $('#vPalette');
    for (const p of Q.palettes) pal.append(h('option', { value: p.id }, p.title || p.id));
    for (const [sel, k, t] of binds) {
      $(sel).addEventListener('change', e => {
        const el = e.target, v = S.view;
        if (t === 'chk') v[k] = el.checked; else if (t === 'sun') v[k] = el.checked ? 1 : 0;
        else if (t === 'num') {   // (within the field's min / max: a bad value would blank the view)
          const x = parseFloat(el.value);
          if (!Number.isFinite(x)) { el.value = v[k]; return; }
          v[k] = Math.min(el.max === '' ? Infinity : +el.max, Math.max(el.min === '' ? -Infinity : +el.min, x));
          if (v[k] !== x) el.value = v[k];
        }
        else if (t === 'int') v[k] = parseInt(el.value, 10); else v[k] = el.value;
        viewChanged(k === 'palette' || k === 'drawAs' || k === 'team');
        U.syncAvail();
        if (k === 'drawAs' || k === 'palette') U.renderPanel();
      });
    }
    const vmc = () => { S.view.vmColor = $('#vVmCustom').checked ? $('#vVmColor').value : ''; viewChanged(); };
    $('#vVmCustom').addEventListener('change', vmc); $('#vVmColor').addEventListener('input', () => { $('#vVmCustom').checked = true; vmc(); });
    $('#vReset').addEventListener('click', () => { S.resetView(); U.syncViewBar(); viewChanged(true); });
    $('#vShot').addEventListener('click', () => { S.dirty = true; U.drawNow(); $('#gl').toBlob(b => Q.download(b, 'quark-model-' + (S.sel || 'view').replace(/\//g, '_') + '.png')); });
    $('#btnFrame').addEventListener('click', () => { S.frame(S.sel); S.build(); S.updateOverlay(); });
    $('#libFilter').addEventListener('input', U.renderLibrary); $('#libEdited').addEventListener('change', U.renderLibrary);
    $('#btnUndo').addEventListener('click', () => { const r = Q.undo(); if (r && r !== S.sel) U.select(r, true); });
    $('#btnRedo').addEventListener('click', () => { const r = Q.redo(); if (r && r !== S.sel) U.select(r, true); });
    $('#btnImport').addEventListener('click', () => $('#fileImport').click());
    $('#fileImport').addEventListener('change', e => { const f = e.target.files; if (f && f.length) U.importFiles(f); e.target.value = ''; });
    $('#btnExport').addEventListener('click', U.exportDialog);
    $('#btnHelp').addEventListener('click', U.help);
    $('#btnSaveProject').addEventListener('click', () => { Q.download(new Blob([Q.projectJSON()], { type: 'application/json' }), 'quark-models-' + new Date().toISOString().slice(0, 10) + '.json'); });
    $('#btnLoadProject').addEventListener('click', () => $('#fileProject').click());
    $('#fileProject').addEventListener('change', async e => {
      const f = e.target.files[0]; e.target.value = '';
      if (!f) return;
      try { Q.importProject(await f.text()); U.afterLoad(); U.toast('Opened ' + f.name); } catch (err) { U.fail('open project', err); }
    });
    $('#btnLoadFolder').addEventListener('click', async () => {
      if (!Q.canPickFolder()) { U.dialog('Not in this browser', h('p', 'Reading a folder needs Chrome or Edge.')); return; }
      let root; try { root = await window.showDirectoryPicker({ id: 'quark-overrides', mode: 'read' }); } catch (e) { return; }
      try {
        const r = await Q.loadFromFolder(root, { provDir: await Q.loadHandle('provDir') });
        U.afterLoad();
        U.dialog('Loaded from ' + r.where, [h('p', r.loaded.length + ' model(s) loaded as edits' + (r.loaded.length ? ': ' + r.loaded.join(', ') : '') + '.'),
          r.failed.length ? h('ul', r.failed.map(f => h('li', f.name + ': ' + f.why))) : null]);
      } catch (e) { U.fail('load folder', e); }
    });
    $('#btnProvDownload').addEventListener('click', () => Q.downloadProvenance());
    $('#btnResetProject').addEventListener('click', async () => {
      const ok = await U.dialog('New project?', h('p', 'Every edit is discarded (save a project file first to keep them).'), [{ label: 'Cancel', value: false }, { label: 'Discard all edits', value: true, primary: true }]);
      if (!ok) return;
      Q.state = { edits: {}, prov: {} }; Q.sources = {}; Q.clearHistory(); for (const k of Q.order) Q.bump(k);
      await Q.clearAutosave(); U.afterLoad();
    });
    document.addEventListener('keydown', onKey);
    initLibResize();
    // drop files anywhere
    let dragDepth = 0;
    window.addEventListener('dragenter', e => { if (e.dataTransfer && [...e.dataTransfer.types].includes('Files')) { dragDepth++; $('#dropHint').hidden = false; e.preventDefault(); } });
    window.addEventListener('dragover', e => e.preventDefault());
    window.addEventListener('dragleave', () => { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) $('#dropHint').hidden = true; });
    window.addEventListener('drop', e => { e.preventDefault(); dragDepth = 0; $('#dropHint').hidden = true; if (e.dataTransfer.files.length) U.importFiles(e.dataTransfer.files); });
    U.syncViewBar();
  };
  // the library's width: drag its edge (remembered; double-click: back to the default, about a fifth of the window,
  // so a 2560 px screen shows five cards across and 1280 two)
  function initLibResize() {
    const lib = $('#lib'), grip = $('#libResize'), KEY = 'qme.libW';
    const clampW = w => Math.round(Math.max(160, Math.min(window.innerWidth * 0.6, w)));
    try { const w = +localStorage.getItem(KEY); if (w > 0) lib.style.width = clampW(w) + 'px'; } catch (e) { }
    let start = null;
    grip.addEventListener('pointerdown', e => { grip.setPointerCapture(e.pointerId); start = { x: e.clientX, w: lib.getBoundingClientRect().width }; grip.classList.add('on'); e.preventDefault(); });
    grip.addEventListener('pointermove', e => { if (start) lib.style.width = clampW(start.w + e.clientX - start.x) + 'px'; });
    grip.addEventListener('pointerup', () => {
      if (!start) return;
      start = null; grip.classList.remove('on');
      try { localStorage.setItem(KEY, String(parseInt(lib.style.width, 10))); } catch (e) { }
    });
    grip.addEventListener('dblclick', () => { lib.style.width = ''; try { localStorage.removeItem(KEY); } catch (e) { } });
  }
  U.afterLoad = function () {
    for (const k of Q.order) Q.bump(k);
    U.renderLibrary();
    if (S.sel) U.select(S.sel, true);
    U.syncUndo();
    Q.scheduleAutosave();
  };
  U.syncUndo = () => { $('#btnUndo').disabled = !Q.canUndo(); $('#btnRedo').disabled = !Q.canRedo(); };

  // --- reacting to changes -------------------------------------------------------------------------------------------
  let saveT = 0, cardT = 0;
  Q.scheduleAutosave = () => { clearTimeout(saveT); saveT = setTimeout(() => Q.autosave(), 800); };
  Q.onStateChanged = function (why, key) {
    Q.scheduleAutosave();
    if (why === 'prov') { U.syncUndo(); U.schedulePanel(); if (key) U.refreshCard(key); return; }
    if (key && key !== S.sel && (why === 'undo' || why === 'redo')) { U.select(key, true); return; }
    const cur = key ? Q.current(key) : null;
    if (cur && cur.type === 'boxes' && S.part >= cur.parts.length) S.part = cur.parts.length - 1;
    if (cur && cur.type !== 'boxes') S.part = -1;
    S.build(); S.updateOverlay(); U.renderHud();
    if (why !== 'typing') U.schedulePanel();
    else {   // typing / dragging: only the list row (the field being typed in stays)
      const row = document.querySelectorAll('.parts tr')[S.part], p = cur && cur.parts && cur.parts[S.part];
      if (row && p) { row.querySelector('.sw').style.background = slotColorCss(p); row.lastChild.textContent = fmt(p.h[0] * 200, 1) + 'x' + fmt(p.h[1] * 200, 1) + 'x' + fmt(p.h[2] * 200, 1) + ' cm'; }
    }
    U.syncUndo();
    clearTimeout(cardT); cardT = setTimeout(() => { if (key) U.refreshCard(key); }, why === 'typing' ? 400 : 0);
  };

  // --- the frame loop ----------------------------------------------------------------------------------------------------
  let last = 0, frames = 0, fpsT = 0, worst = 0;
  U.drawNow = function () {
    const cv = $('#gl'), dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.max(1, Math.round(cv.clientWidth * dpr)), hh = Math.max(1, Math.round(cv.clientHeight * dpr));
    if (Q.fixedSize) { if (cv.width !== Q.fixedSize[0] || cv.height !== Q.fixedSize[1]) { cv.width = Q.fixedSize[0]; cv.height = Q.fixedSize[1]; } }
    else if (cv.width !== w || cv.height !== hh) { cv.width = w; cv.height = hh; }
    R.draw(S.current);
    S.dirty = false;
  };
  U.loop = function (t) {
    requestAnimationFrame(U.loop);
    R.pollReads();
    const dt = last ? t - last : 16; last = t;
    if (S.view.play) { S.time += dt / 1000; S.animate(S.time); }
    const cv = $('#gl');
    if (cv.width !== Math.round(cv.clientWidth * Math.min(2, window.devicePixelRatio || 1)) && !Q.fixedSize) S.dirty = true;
    if (!S.dirty) return;
    const t0 = performance.now();
    U.drawNow();
    const ft = performance.now() - t0;
    frames++; worst = Math.max(worst, ft);
    if (t - fpsT > 1000) { $('#fps').textContent = 'cpu ' + worst.toFixed(1) + ' ms worst / frame'; fpsT = t; frames = 0; worst = 0; }
  };
})();
