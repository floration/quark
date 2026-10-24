/* ===== UI: levelling groups =========================================================================== */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h, fmt = U.fmt;
  const G = U.groups = { selId: null, plan: null };

  const label = key => { const a = Q.assets[key]; return a ? a.name + ' / ' + Q.packName(a.kind, a.pack) : key; };
  const cur = () => Q.state.groups.find(g => g.id === G.selId) || null;

  G.render = function () {
    const list = U.$('#groupList'); list.innerHTML = '';
    if (!Q.state.groups.length) list.append(h('div.faint', { style: { padding: '10px' } }, 'No groups yet. Start from a preset, or an empty group.'));
    for (const g of Q.state.groups) list.append(h('div.list-item' + (g.id === G.selId ? '.on' : ''), { onclick: () => { G.selId = g.id; G.plan = null; G.render(); } },
      h('div.t', g.name), h('div.d', g.members.length + ' members, ' + Q.MEASURES[g.measure] + (g.applied ? ', applied at ' + fmt(g.applied.T, 1) : ''))));
    renderEditor();
  };
  U.views.groups = { render: G.render };

  async function presetDialog(p) {
    let argEl = null;
    if (p.arg === 'kits') argEl = h('div', Q.KITS.map(k => h('label', { style: { marginRight: '10px' } }, h('input', { type: 'checkbox', checked: true, value: k.dir }), k.name)));
    else if (p.arg === 'sound') { argEl = h('select', Q.SOUNDS.map(s => h('option', { value: s }, s))); }
    else if (p.arg === 'voice') { argEl = h('select', Q.VOICES.map(v => h('option', { value: v.dir }, v.name))); }
    else if (p.arg === 'line') { argEl = h('select', h('optgroup', { label: 'Announcer lines' }, Q.LINES.map(l => h('option', { value: l }, l))), h('optgroup', { label: 'Quips' }, Q.QUIPS.map(l => h('option', { value: l }, l)))); }
    let arg = null;
    if (argEl) {
      const ok = await U.dialog(p.name, [h('p', p.arg === 'kits' ? 'One group per kit you tick.' : 'Pick one:'), argEl], [{ label: 'Cancel', value: false }, { label: 'Create', primary: true, value: true }]);
      if (!ok) return;
      arg = p.arg === 'kits' ? [...argEl.querySelectorAll('input:checked')].map(i => i.value) : argEl.value;
    }
    addGroups(p.name, p.make(arg));
  }
  // add a preset's groups, leaving out any the list already has (same name and members): those are selected instead
  function addGroups(label, groups) {
    const have = new Map(Q.state.groups.map(g => [Q.groupSig(g), g]));
    const fresh = groups.filter(g => g.members.length && !have.has(Q.groupSig(g)));
    const dup = groups.map(g => have.get(Q.groupSig(g))).filter(Boolean);
    if (fresh.length) Q.commit('add ' + label, () => { Q.state.groups.push(...fresh); });
    if (dup.length) U.toast((fresh.length ? 'Added ' + fresh.length + '; ' : '') + 'already in the list: ' + dup.map(g => g.name).join(', '));
    else if (!fresh.length) U.toast('Nothing to add: those sounds are not loaded');
    const first = fresh[0] || dup[0];
    G.selId = first ? first.id : G.selId; G.plan = null; G.render();
    return fresh;
  }
  G.addPreset = (id, arg) => addGroups(Q.PRESETS.find(x => x.id === id).name, Q.PRESETS.find(x => x.id === id).make(arg));

  function setG(label, fn) { G.plan = null; Q.commit(label, () => fn(cur())); }

  function renderEditor() {
    const box = U.$('#groupEditor'); box.innerHTML = '';
    const g = cur();
    if (!g) { box.append(h('div.faint', 'Select or create a group. A group brings its members to one loudness: pick a target, preview the gains, then apply. Applying adds a gain step (and a limiter if a member would pass the true-peak ceiling) to each member and marks it "keep my level", so the game keeps it (sound-levels.txt).')); return; }
    const modeSel = h('select', { onchange: ev => setG('group target', x => { x.target.mode = ev.target.value; }) },
      h('option', { value: 'average' }, 'the group average'), h('option', { value: 'loudest' }, 'the loudest member'), h('option', { value: 'ref' }, 'a reference member'), h('option', { value: 'lufs' }, 'a number'));
    modeSel.value = g.target.mode;
    const measSel = h('select', { onchange: ev => setG('group measure', x => { x.measure = ev.target.value; }) }, Object.entries(Q.MEASURES).map(([k, v]) => h('option', { value: k }, v)));
    measSel.value = g.measure;
    const refSel = h('select', { onchange: ev => setG('group reference', x => { x.target.ref = ev.target.value; }) }, g.members.map(k => h('option', { value: k }, label(k))));
    refSel.value = g.target.ref || g.members[0] || '';
    box.append(h('h2', h('input', { value: g.name, onchange: ev => setG('rename group', x => { x.name = ev.target.value; }) }),
      h('button', { type: 'button', onclick: () => { Q.commit('delete group', () => { Q.state.groups = Q.state.groups.filter(x => x.id !== g.id); }); G.selId = null; G.render(); } }, 'Delete group')),
      h('div.gform',
        h('label', 'Measure', measSel),
        h('label', 'Match', modeSel),
        g.target.mode === 'lufs' ? h('label', h('input', { type: 'number', step: 0.1, value: g.target.value, onchange: ev => setG('group target', x => { x.target.value = +ev.target.value; }) }), g.measure === 'heard' ? 'dB' : 'LUFS') : null,
        g.target.mode === 'ref' ? h('label', 'Reference', refSel) : null,
        h('label', { title: 'No member goes over this true peak' }, 'True-peak ceiling', h('input', { type: 'number', step: 0.1, value: g.ceiling, onchange: ev => setG('group ceiling', x => { x.ceiling = +ev.target.value; }) }), 'dBTP'),
        h('label', { title: 'A member that would pass the ceiling gets a true-peak limiter after its gain (otherwise it is only flagged)' }, h('input', { type: 'checkbox', checked: g.limiter, onchange: ev => setG('group limiter', x => { x.limiter = ev.target.checked; }) }), 'soft limiter if needed')));
    // members
    const chips = h('div.chips', g.members.map(k => h('span.mchip', label(k), g.offsets[k] ? h('span.faint', (g.offsets[k] > 0 ? '+' : '') + fmt(g.offsets[k], 2) + ' dB') : null,
      h('button', { type: 'button', title: 'Remove', onclick: () => setG('remove member', x => { x.members = x.members.filter(m => m !== k); delete x.offsets[k]; }) }, '✕'))));
    box.append(h('div.side-h', 'Members (' + g.members.length + ')'), chips, adder(g));
    // plan
    const planBox = h('div');
    box.append(h('div', { style: { display: 'flex', gap: '6px', alignItems: 'center', margin: '8px 0' } },
      h('button', { type: 'button', onclick: () => G.preview(planBox) }, 'Preview gains'),
      h('button.primary', { type: 'button', onclick: () => G.apply(planBox) }, g.applied ? 'Re-apply' : 'Apply'),
      h('button', { type: 'button', onclick: () => U.library.playSequence(g.members.filter(k => Q.assets[k]), g.name) }, '▶ Audition members'),
      g.applied ? h('button', { type: 'button', onclick: () => { Q.unapplyGroup(g); G.plan = null; } }, 'Remove its gains') : null,
      h('span.faint', 'Re-applying replaces this group\'s own steps; undo takes the whole apply back.')), planBox);
    if (G.plan && G.plan.gid === g.id) drawPlan(planBox, g, G.plan);
  }

  function adder(g) {
    const kind = h('select', h('option', { value: 'sound' }, 'sounds'), h('option', { value: 'voice' }, 'voice lines + quips'));
    const packs = h('select', { multiple: true }), names = h('select', { multiple: true });
    const fill = () => {
      packs.innerHTML = ''; names.innerHTML = '';
      for (const p of (kind.value === 'voice' ? Q.VOICES : Q.KITS)) packs.append(h('option', { value: p.dir }, p.name));
      if (kind.value === 'voice') names.append(h('optgroup', { label: 'Announcer lines' }, Q.LINES.map(n => h('option', { value: n }, n))), h('optgroup', { label: 'Quips' }, Q.QUIPS.map(n => h('option', { value: n }, n))));
      else for (const n of Q.SOUNDS) names.append(h('option', { value: n }, n));
    };
    kind.onchange = fill; fill();
    return h('div.adder', 'Add', kind, 'from', packs, 'named', names,
      h('button', { type: 'button', onclick: () => {
        const ps = [...packs.selectedOptions].map(o => o.value), ns = [...names.selectedOptions].map(o => o.value);
        const add = [];
        for (const p of ps) for (const n of ns) { const k = Q.key(kind.value, p, n); if (Q.assets[k] && !g.members.includes(k)) add.push(k); }
        if (!add.length) { U.toast('Pick at least one pack and one name (Ctrl / Shift to pick several)'); return; }
        setG('add ' + add.length + ' members', x => { x.members.push(...add); if (!x.target.ref) x.target.ref = x.members[0]; });
      } }, '+ Add'), h('span.faint', 'every pack x name you pick (Ctrl / Shift for several)'));
  }

  // a member levelled by another group too: the later apply wins over the earlier one's level
  function others(g, key) {
    const o = Q.state.groups.filter(x => x.id !== g.id && x.applied && x.members.includes(key)).map(x => x.name);
    return o.length ? 'also levelled by ' + o.join(', ') : '';
  }
  function drawPlan(box, g, plan) {
    box.innerHTML = '';
    const unit = g.measure === 'heard' ? 'dB' : 'LUFS';
    const t = h('table.plan', h('tr', h('th', 'member'), h('th', 'now ' + unit), h('th', 'offset dB'), h('th', 'target'), h('th', 'gain dB'), h('th', 'true peak after'), h('th', 'limiter'), h('th', 'measured after'), h('th', 'note')));
    for (const r of plan.rows) {
      const offIn = h('input', { type: 'number', step: 0.1, value: +(r.off).toFixed(2), title: 'This member sits this many dB above (+) or below (-) the target', onchange: ev => { setG('member offset', x => { x.offsets[r.key] = +ev.target.value; }); } });
      t.append(h('tr', h('td', h('a', { href: '#', onclick: ev => { ev.preventDefault(); U.select(r.key, true); } }, label(r.key))),
        h('td', r.silent ? 'silent' : fmt(r.cur, 2)), h('td', offIn), h('td', fmt(r.target, 2)), h('td', (r.gain > 0 ? '+' : '') + fmt(r.gain, 2)),
        h('td' + (r.limit ? '.warn' : ''), fmt(r.tpAfter, 2) + ' dBTP'), h('td' + (r.limit ? '.warn' : ''), r.limit ? (g.limiter ? 'yes' : 'over!') : ''),
        h('td' + (r.after != null ? (Math.abs(r.after - r.target) <= 0.1 ? '.good' : '.warn') : ''), r.after != null ? fmt(r.after, 2) + (r.tpFinal != null ? '  (' + fmt(r.tpFinal, 1) + ' dBTP)' : '') : ''),
        h('td.warn', { style: { textAlign: 'left', fontFamily: 'var(--sans)' } }, others(g, r.key))));
    }
    const live = plan.rows.filter(r => r.after != null && !r.silent);
    box.append(h('div', { style: { margin: '4px 0' } }, 'Target ' + fmt(plan.T, 2) + ' ' + unit + (live.length ? '. After applying: spread ' + fmt(Math.max(...live.map(r => r.after - r.target)) - Math.min(...live.map(r => r.after - r.target)), 3) + ' dB around the target(s).' : '')), t);
  }

  // a group with nothing in it (or only names whose sounds are gone) has nothing to level
  function empty(g, box) {
    if (g.members.some(k => Q.assets[k])) return false;
    box.innerHTML = ''; box.append(h('div.faint', g.name + ' has no members yet: add sounds or lines above first.'));
    U.toast(g.name + ' has no members: nothing to level');
    return true;
  }
  G.preview = async function (box) {
    const g = cur(); if (empty(g, box)) return null;
    box.innerHTML = '<div class="faint">measuring...</div>';
    try { const plan = await Q.planGroup(g); plan.gid = g.id; G.plan = plan; drawPlan(box, g, plan); return plan; } catch (e) { U.fail('group preview', e); }
  };
  G.apply = async function (box) {
    const g = cur(); if (empty(g, box)) return null;
    box.innerHTML = '<div class="faint">applying...</div><div class="progress"><i></i></div>';
    try {
      const plan = await Q.applyGroup(g, (pass, i, n) => { const bar = box.querySelector('.progress i'); if (bar) bar.style.width = (100 * i / n) + '%'; });
      plan.gid = g.id; G.plan = plan;
      renderEditor();
      U.toast('Levelled ' + plan.rows.filter(r => !r.silent).length + ' sounds in ' + g.name);
      return plan;
    } catch (e) { U.fail('group apply', e); }
  };

  G.init = function () {
    const menu = U.$('#presetMenu');
    for (const p of Q.PRESETS) menu.append(h('button', { type: 'button', onclick: () => presetDialog(p) }, p.name));
    U.$('#grNew').onclick = () => { const g = Q.newGroup('New group', []); Q.commit('new group', () => Q.state.groups.push(g)); G.selId = g.id; G.render(); };
  };
})();
