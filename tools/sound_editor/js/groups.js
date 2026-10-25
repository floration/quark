/* ===== LOUDNESS GROUPS ==============================================================================
   A group = any set of sounds / voice lines brought to one loudness (each member may carry an offset in dB: the
   hitsound ladder keeps its 0.7 / 0.85 / 1.0 / 1.15 ratios). Applying adds a gain step (and a true-peak limiter
   when a member would pass the ceiling) to each member's chain, tagged with the group, then refines the gains
   until every member measures on target. Re-applying replaces the group's own steps (never stacks them). */
(function () {
  'use strict';
  const Q = window.Q, L = window.QLoud;

  Q.MEASURES = { lufs: 'Integrated LUFS', mmax: 'Max momentary LUFS', heard: 'Heard loudness (game, dB)' };
  Q.newGroup = (name, members, extra) => Object.assign({ id: 'g' + Q.uid(), name, members: members.slice(), target: { mode: 'average', value: -20, ref: members[0] || null },
    measure: 'lufs', ceiling: -1, limiter: true, offsets: {} }, extra || {});

  const ladderOffsets = kit => { const o = {}; Q.HIT_TIERS.forEach((n, i) => { o[Q.key('sound', kit, n)] = 20 * Math.log10(Q.HIT_TIER_GAIN[i]); }); return o; };
  // presets: each returns a list of groups (arg: the kit / voice / sound / line chosen in the dialog)
  Q.PRESETS = [
    { id: 'weapons', name: "Each kit's weapons", arg: 'kits', make: kits => kits.map(k => Q.newGroup(Q.kitByDir(k).name + ' weapons', Q.WEAPONS.map(n => Q.key('sound', k, n)))) },
    { id: 'ladder', name: 'Hitsound ladder (keeps 0.7 / 0.85 / 1.0 / 1.15)', arg: 'kits', make: kits => kits.map(k => Q.newGroup(Q.kitByDir(k).name + ' hitsound ladder',
      Q.HIT_TIERS.map(n => Q.key('sound', k, n)), { measure: 'heard', offsets: ladderOffsets(k), target: { mode: 'ref', value: -20, ref: Q.key('sound', k, 'HIT_HEAVY') } })) },
    { id: 'ladderAll', name: 'Hitsound ladder across all kits (one level per tier)', arg: null, make: () => {
      const m = [], off = {}; for (const k of Q.KITS) { Q.HIT_TIERS.forEach(n => m.push(Q.key('sound', k.dir, n))); Object.assign(off, ladderOffsets(k.dir)); }
      return [Q.newGroup('Hitsound ladder, all kits', m, { measure: 'heard', offsets: off, target: { mode: 'ref', value: -20, ref: Q.key('sound', 'default', 'HIT_HEAVY') } })];
    } },
    { id: 'movement', name: 'Movement', arg: 'kits', make: kits => kits.map(k => Q.newGroup(Q.kitByDir(k).name + ' movement', Q.MOVEMENT.map(n => Q.key('sound', k, n)))) },
    { id: 'sameSound', name: 'Same sound across all kits', arg: 'sound', make: n => [Q.newGroup(n + ' across kits', Q.KITS.map(k => Q.key('sound', k.dir, n)), { target: { mode: 'ref', value: -20, ref: Q.key('sound', 'default', n) } })] },
    { id: 'oneVoice', name: "One voice's lines", arg: 'voice', make: v => [Q.newGroup(Q.voiceByDir(v).name + ' lines', have(Q.LINES.map(l => Q.key('voice', v, l))))] },
    { id: 'oneVoiceQuips', name: "One voice's quips", arg: 'voice', make: v => [Q.newGroup(Q.voiceByDir(v).name + ' quips', have(Q.allQuips().map(l => Q.key('voice', v, l))))] },
    { id: 'sameLine', name: 'Same line or quip across all voices', arg: 'line', make: l => [Q.newGroup(l + ' across voices', have(Q.VOICES.map(v => Q.key('voice', v.dir, l))))] },
    { id: 'allVoices', name: 'All voices (every recorded line)', arg: null, make: () => [Q.newGroup('All voices', have([].concat(...Q.VOICES.map(v => Q.LINES.map(l => Q.key('voice', v.dir, l))))), { target: { mode: 'lufs', value: -16, ref: null } })] },
    { id: 'allQuips', name: "All voices' quips", arg: null, make: () => [Q.newGroup('All quips', have([].concat(...Q.VOICES.map(v => Q.allQuips().map(l => Q.key('voice', v.dir, l))))), { target: { mode: 'lufs', value: -16, ref: null } })] },
  ];
  function have(keys) { return keys.filter(k => Q.assets[k]); }
  // a group made again from the same preset (same name, same members) is the same group: not added twice
  Q.groupSig = g => g.name + '|' + g.members.slice().sort().join(',');

  Q.measureValue = (m, measure) => m ? m[measure] : -Infinity;

  /* What a member is measured on: what the game will play once its level is kept (sound-levels.txt): the file with
     the game's leading-silence trim and cap. (Integrated LUFS of a 1 s clip moves by ~0.5 LU when its 400 ms blocks
     shift by the 37 ms the game trims, so measuring the raw file would leave the group visibly uneven in the game.) */
  const asPlayed = (key, x) => Q.prepareGame(x, Q.assets[key].cap, null, false, true).v;
  Q.levelView = asPlayed;
  // a member's output without this group's own steps (so re-applying measures from the same start)
  async function baseOut(key, gid) {
    const e = Q.state.edits[key];
    if (!e) return asPlayed(key, Q.assets[key].orig || new Float32Array(0));
    if (!e.chain.some(s => s.group === gid)) return asPlayed(key, await Q.getOut(key));
    const e2 = Object.assign({}, e, { chain: e.chain.filter(s => s.group !== gid) });
    return asPlayed(key, await Q.renderEditData(Q.sourceOf(key), e2));
  }
  async function packsFor(keys) {
    const need = new Set(keys.map(k => { const a = Q.assets[k]; return a ? (a.kind === 'voice' ? 'voice' : 'kit') + ':' + a.pack : null; }).filter(Boolean));
    await Promise.all([...need].filter(id => !Q.loaded[id]).map(id => { const [t, d] = id.split(':'); return Q.loadPack(t, d); }));
  }

  // the plan: each member's current level, target and gain
  Q.planGroup = async function (g) {
    const members = g.members.filter(k => Q.assets[k]);
    await packsFor(members);
    const rows = [];
    for (const key of members) {
      const x = await baseOut(key, g.id), m = Q.measure(x);
      rows.push({ key, cur: Q.measureValue(m, g.measure), tp: m.tp, off: g.offsets[key] || 0, silent: !isFinite(Q.measureValue(m, g.measure)) });
    }
    const live = rows.filter(r => !r.silent);
    let T;
    if (g.target.mode === 'lufs') T = g.target.value;
    else if (g.target.mode === 'loudest') T = Math.max(...live.map(r => r.cur - r.off));
    else if (g.target.mode === 'ref') { const r = live.find(r => r.key === g.target.ref) || live[0]; T = r ? r.cur - r.off : -20; }
    else T = live.reduce((s, r) => s + r.cur - r.off, 0) / Math.max(1, live.length);
    for (const r of rows) {
      r.target = T + r.off;
      r.gain = r.silent ? 0 : r.target - r.cur;
      r.tpAfter = r.tp + r.gain;
      r.limit = !r.silent && r.tpAfter > g.ceiling;
    }
    return { T, rows };
  };

  // apply: one undoable step; refines until every member is within 0.02 dB of its target (or 8 passes)
  Q.applyGroup = async function (g, onProgress) {
    const plan = await Q.planGroup(g), tok = Q.beginStep('level group ' + g.name);
    for (const r of plan.rows) {
      if (r.silent) continue;
      let e = Q.state.edits[r.key];
      if (!e) e = Q.state.edits[r.key] = Q.newEdit();
      e.chain = e.chain.filter(s => s.group !== g.id);
      e.chain.push(Object.assign(Q.newStep('gain', { db: +r.gain.toFixed(3) }), { group: g.id }));
      if (r.limit && g.limiter) e.chain.push(Object.assign(Q.newStep('limiter', { ceiling: g.ceiling }), { group: g.id }));
      e.levelSet = true;
    }
    for (let pass = 0; pass < 8; pass++) {
      let worst = 0;
      for (let i = 0; i < plan.rows.length; i++) {
        const r = plan.rows[i]; if (r.silent) continue;
        const m = Q.measure(asPlayed(r.key, await Q.getOut(r.key))), got = Q.measureValue(m, g.measure), res = r.target - got;
        r.after = got; r.tpFinal = m.tp;
        worst = Math.max(worst, Math.abs(res));
        if (Math.abs(res) > 0.02) { const st = Q.state.edits[r.key].chain.find(s => s.group === g.id && s.type === 'gain'); st.p.db = +(st.p.db + res).toFixed(3); }
        onProgress && onProgress(pass, i + 1, plan.rows.length);
      }
      if (worst <= 0.02) break;
    }
    for (const r of plan.rows) if (!r.silent) { const m = Q.measure(asPlayed(r.key, await Q.getOut(r.key))); r.after = Q.measureValue(m, g.measure); r.tpFinal = m.tp; }
    g.applied = { at: Date.now(), T: plan.T };
    const sg = Q.state.groups.find(x => x.id === g.id); if (sg) sg.applied = g.applied;
    Q.endStep(tok);
    return plan;
  };
  // take a group's steps out of every member again
  Q.unapplyGroup = function (g) {
    return Q.commit('remove group levels ' + g.name, () => {
      for (const k of g.members) {
        const e = Q.state.edits[k]; if (!e) continue;
        e.chain = e.chain.filter(s => s.group !== g.id);
        if (!e.chain.length && !e.src && !e.trim[0] && e.trim[1] == null && !e.fadeIn && !e.fadeOut) delete Q.state.edits[k];
      }
      const sg = Q.state.groups.find(x => x.id === g.id); if (sg) delete sg.applied;
    });
  };
})();
