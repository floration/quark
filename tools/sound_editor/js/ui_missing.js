/* ===== UI: missing sounds =============================================================================
   The audit's events with no sound (each needs a game hook), the owner's own new names, and slots per kit / voice
   to design or drop a sound into. Designed slots export like any other sound. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h;
  const M = U.missing = {};

  // the slots follow the audit's list and state.customs (also after an undo / redo / open): a removed name's assets go,
  // so nothing exports or lists them
  M.ensureAssets = function () {
    const want = new Set();
    for (const m of Q.MISSING) if (m.name && !m.existing) { Q.addCustomAssets(m.name, m.kind || 'sound', m.when); want.add((m.kind || 'sound') + ':' + m.name); }
    for (const c of Q.state.customs) { Q.addCustomAssets(c.name, c.kind, c.when || 'your own sound'); want.add(c.kind + ':' + c.name); }
    for (const [k, a] of Object.entries(Q.assets)) if (a.custom && !want.has(a.kind + ':' + a.name)) { delete Q.assets[k]; if (Q.sel === k) Q.sel = null; }
  };
  // remove one of your own names: its slots' audio, edits and group memberships go with it (one undo step)
  M.removeOwn = async function (name, kind) {
    const keys = (kind === 'voice' ? Q.VOICES : Q.KITS).map(p => Q.key(kind, p.dir, name));
    const used = keys.filter(k => Q.state.edits[k]);
    if (used.length) {
      const ok = await U.dialog('Remove ' + name, h('p', name + ' has a sound in ' + used.length + ' slot' + (used.length > 1 ? 's' : '') + ' (' + used.map(k => Q.packName(kind, Q.parseKey(k).pack)).join(', ') + '). Removing the name removes them too, so they are not exported. Undo brings them back.'),
        [{ label: 'Cancel', value: false }, { label: 'Remove', primary: true, value: true }]);
      if (!ok) return false;
    }
    return Q.commit('remove ' + name, () => {
      Q.state.customs = Q.state.customs.filter(c => !(c.name === name && c.kind === kind));
      for (const k of keys) delete Q.state.edits[k];
      for (const g of Q.state.groups) { g.members = g.members.filter(k => !keys.includes(k)); for (const k of keys) delete g.offsets[k]; }
    });
  };

  function slots(name, kind) {
    const packs = kind === 'voice' ? Q.VOICES : Q.KITS, box = h('div.slots');
    for (const p of packs) {
      const key = Q.key(kind, p.dir, name), a = Q.assets[key];
      if (!a) continue;
      const e = Q.state.edits[key], has = e || (!a.custom && a.orig && a.orig.length && Q.measure(a.orig).peak > -80);
      const el = h('div.slot' + (has ? '.has' : '') + (Q.sel === key ? '.sel' : ''), { 'data-key': key, title: key + (has ? '' : ': empty, drop a file or click and Design...'),
        onclick: () => U.select(key, !!has) }, h('span.sw', { style: { background: p.color } }), p.name.split(' ')[0].slice(0, 7), e ? h('span.badge.ed', 'ED') : null);
      U.files.dropTarget(el, key);
      box.append(el);
    }
    return box;
  }

  M.render = function () {
    M.ensureAssets();
    const t = U.$('#missingTable'); t.innerHTML = '';
    t.append(h('thead', h('tr', h('th', 'Name'), h('th', 'Pri'), h('th', 'Plays'), h('th', 'When it would play / design note'), h('th', 'Your sound (drop a file on a slot, or click it and use Design...)'))));
    const body = h('tbody');
    const areas = [...new Set(Q.MISSING.map(m => m.area))];
    const own = Q.state.customs.map(c => ({ area: 'Yours', name: c.name, kind: c.kind, pri: '', d: '', when: c.when || 'your own sound', note: '', own: true }));
    for (const area of areas.concat(own.length ? ['Yours'] : [])) {
      body.append(h('tr.grp', h('td', { colSpan: 5 }, area)));
      for (const m of Q.MISSING.filter(x => x.area === area).concat(area === 'Yours' ? own : [])) {
        if (!m.name) {
          body.append(h('tr.reuse-row', h('td', h('i', 'game fix')), h('td', h('span.pri.' + m.pri, m.pri)), h('td', m.d), h('td', m.when, h('div.faint', m.note)), h('td', 'reuses ' + m.reuse + ' (no new sound needed)')));
          continue;
        }
        const kind = m.kind || 'sound';
        body.append(h('tr', h('td', h('b.num', m.name), h('div.hook', m.existing ? 'line exists, never played' : 'needs a game hook'), m.own ? h('button.mini', { type: 'button', onclick: () => M.removeOwn(m.name, kind) }, 'remove') : null),
          h('td', m.pri ? h('span.pri.' + m.pri, m.pri) : ''), h('td', m.d + (kind === 'voice' ? ' (voice)' : '')), h('td', m.when, m.note ? h('div.faint', m.note) : null), h('td', slots(m.name, kind))));
      }
    }
    t.append(body);
  };
  U.views.missing = { render: M.render };

  /* A name typed in "Add your own": upper case, runs of other characters become one _, no _ at either end, starting
     with a letter. "quips/LAUGH" (or the quip kind) is a new quip for every voice, never a line named QUIPS_LAUGH.
     Returns {name, kind, quip} or {error}. */
  M.parseName = function (text, kindSel) {
    let t = String(text || '').trim(), quip = kindSel === 'quip';
    const qm = t.match(/^quips?[\/\\_ ](.+)$/i);
    if (qm && kindSel !== 'sound') { quip = true; t = qm[1]; }
    const base = t.toUpperCase().replace(/[^A-Z0-9]+/g, '_').replace(/^_+|_+$/g, '');
    if (!base) return { error: 'Type a name first (letters, digits, _)' };
    if (!/^[A-Z]/.test(base)) return { error: base + ': a name starts with a letter' };
    if (base.length > 48) return { error: 'Keep the name under 48 characters' };
    const kind = quip ? 'voice' : kindSel, name = quip ? 'quips/' + base : base;
    const taken = quip ? Q.allQuips().includes(name) : (kind === 'voice' ? Q.LINES : Q.SOUNDS).includes(name);
    if (taken || Q.state.customs.some(c => c.name === name && c.kind === kind) || Q.MISSING.some(m => m.name === name && (m.kind || 'sound') === kind)) return { error: name + ' already exists' };
    return { name, kind, quip };
  };

  M.init = function () {
    U.$('#msAdd').onclick = () => {
      const r = M.parseName(U.$('#msName').value, U.$('#msKind').value);
      if (r.error) { U.toast(r.error, true); return; }
      const { name, kind, quip } = r;
      Q.commit('add ' + name, () => Q.state.customs.push({ name, kind, when: 'your own ' + (quip ? 'quip' : kind === 'voice' ? 'line' : 'sound') }));
      U.$('#msName').value = '';
      U.toast('Added ' + name + ': a slot per ' + (kind === 'voice' ? 'voice' : 'kit') + ' under Yours' + (quip ? ' (and a row in Voices > QUIPS); exports as voices/<voice>/' + name + '.wav, the game plays it as soon as it is there' : '; it needs a game hook before it plays'));
    };
  };
})();
