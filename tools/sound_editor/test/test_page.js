/* Sound editor page checks in headless Chromium (Playwright).

     node tools/sound_editor/test/test_page.js <sound-editor folder>

   The folder is a built editor (build.py / install.py output: sound-editor.html + data/). Rebuild its page first
   (build.py - <folder> --page-only) so it runs this checkout's code. Exit code 0 = every check passed. The export checks
   serve the folder over http on 127.0.0.1 (port $QSE_TEST_PORT, default any free one). Each check is
   one regression the 2026-10 app audit found, or a feature added then. */
'use strict';
const path = require('path');
const fs = require('fs');
const http = require('http');
const { chromium } = require('playwright');

const dir = path.resolve(process.argv[2] || '');
if (!process.argv[2]) { console.error('usage: node test_page.js <sound-editor folder>'); process.exit(2); }
const URL = 'file://' + path.join(dir, 'sound-editor.html');
let failed = 0;
const ok = (cond, what, extra) => { console.log((cond ? 'ok   ' : 'FAIL ') + what + (cond || extra === undefined ? '' : '  ' + JSON.stringify(extra))); if (!cond) failed++; };

async function open(b, vp, url) {
  const ctx = await b.newContext({ viewport: vp, acceptDownloads: true });
  const p = await ctx.newPage(), logs = [];
  p.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') logs.push(m.type() + ': ' + m.text()); });
  p.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));
  await p.goto(url || URL);
  await p.waitForFunction(() => window.Q && Q.ready, null, { timeout: 60000 });
  return { ctx, p, logs };
}

(async () => {
  const b = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });

  // --- 1280x720: loads clean, the empty dock is small ---------------------------------------------------------------
  {
    const { ctx, p, logs } = await open(b, { width: 1280, height: 720 });
    const dockH = await p.$eval('#dock', e => e.offsetHeight);
    ok(dockH < 80, 'empty dock is a one-line hint at 1280x720', dockH);
    await p.click('td.cell[data-key="sounds/default/ROCKET"]');
    await p.waitForTimeout(400);
    const h2 = await p.$eval('#dock', e => e.offsetHeight);
    ok(h2 >= 240 && h2 <= 300, 'selected: the dock opens at about a third of the window', h2);
    const hscroll = await p.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
    ok(!hscroll, 'no page-wide horizontal scroll at 1280');
    ok(!logs.length, 'no console errors at 1280x720', logs);
    await ctx.close();
  }

  const { p, logs } = await open(b, { width: 2560, height: 1440 });

  // --- quips by name ---------------------------------------------------------------------------------------------------
  const m = await p.evaluate(() => Object.fromEntries(['male_quips_HEADSHOT.wav', 'voices/male/quips/HEADSHOT.wav', 'quips/HEADSHOT.wav',
    'female_quip_spree.wav', 'voices/british/HEADSHOT.wav', 'HEADSHOT.wav', 'heavy_metal/ROCKET.ogg'].map(f => { const r = Q.ui.files.match(f); return [f, r && r.pack + '/' + r.name]; })));
  ok(m['male_quips_HEADSHOT.wav'] === 'male/quips/HEADSHOT', 'male_quips_HEADSHOT.wav -> the quip', m);
  ok(m['voices/male/quips/HEADSHOT.wav'] === 'male/quips/HEADSHOT', 'voices/male/quips/HEADSHOT.wav -> the quip', m);
  ok(m['quips/HEADSHOT.wav'] === 'male/quips/HEADSHOT', 'quips/HEADSHOT.wav -> the quip', m);
  ok(m['female_quip_spree.wav'] === 'female/quips/SPREE', 'female_quip_spree.wav -> the quip', m);
  ok(m['voices/british/HEADSHOT.wav'] === 'british/HEADSHOT' && m['HEADSHOT.wav'] === 'male/HEADSHOT', 'a plain HEADSHOT is still the announcer line', m);
  ok(m['heavy_metal/ROCKET.ogg'] === 'heavy_metal/ROCKET', 'kit folders still match', m);
  ok(await p.evaluate(() => Q.ui.files.allNames('voice').filter(n => n.startsWith('quips/')).length === Q.QUIPS.length && Q.QUIPS.length > 0), 'the assign dialog lists the quips');

  // --- provenance --------------------------------------------------------------------------------------------------------
  const prov = await p.evaluate(() => {
    const k = 'sounds/default/ROCKET', P = Q.prov;
    const id = Q.addSource('ROCKET.wav', new Float32Array(4800).fill(0.1));
    Q.commit('t', () => { const e = Q.newEdit(); e.src = id; Q.state.edits[k] = e; });
    Q.commit('t', () => P.setOrigin(k, 'human', 'recorded by q', ''));
    Q.commit('t', () => P.setOrigin(k, 'third-party', null, 'freesound.org CC0'));   // (what the picker does)
    const after = P.entries([k])[k + '.wav'];
    Q.commit('t', () => P.setOrigin(k, 'third-party', 'trimmed', null));   // same origin: the source stays
    const same = Object.assign({}, Q.state.edits[k].origin);
    Q.commit('t', () => { delete Q.state.edits[k]; });
    const quip = P.defaultOrigin('voices/british/quips/HEADSHOT'), line = P.defaultOrigin('voices/british/HEADSHOT');
    return { after, same, quip, line };
  });
  ok(prov.after.origin === 'third-party' && !/recorded by q/.test(prov.after.note || '') && prov.after.source === 'freesound.org CC0', 'switching to third-party drops the old note', prov.after);
  ok(prov.same.source === 'freesound.org CC0' && prov.same.note === 'trimmed', 'the same origin keeps its other field', prov.same);
  ok(prov.quip.origin === prov.line.origin && prov.quip.source.includes(prov.line.source) && !/silence|Piper/.test(prov.quip.source.replace(prov.line.source, '')), 'a default quip has its line\'s origin', prov.quip);

  // --- origin chips, filter and counts -------------------------------------------------------------------------------
  await p.click('button[data-tab="sounds"]');
  const chips = await p.evaluate(() => ({ n: document.querySelectorAll('#soundMatrix td.cell .badge[class*="o-"]').length, cells: document.querySelectorAll('#soundMatrix td.cell').length,
    counts: document.querySelector('#sCounts').textContent, head: document.querySelector('#soundMatrix th.pack .pack-counts').textContent }));
  ok(chips.n === chips.cells && chips.cells === 165, 'every sound cell has an origin chip', chips);
  ok(/AI \d+/.test(chips.counts) && /AI \d+/.test(chips.head), 'origin counts in the toolbar and per kit', chips);
  await p.selectOption('#sOrigin', 'human');
  const hum = await p.evaluate(() => ({ rows: document.querySelectorAll('#soundMatrix tbody tr:not(.grp):not(.none)').length, none: !!document.querySelector('#soundMatrix tr.none') }));
  await p.selectOption('#sOrigin', 'ai');
  const ai = await p.evaluate(() => ({ rows: document.querySelectorAll('#soundMatrix tbody tr:not(.grp):not(.none)').length, dim: document.querySelectorAll('#soundMatrix td.cell.dimmed').length }));
  ok(ai.rows > 0 && (hum.rows > 0 || hum.none), '"still AI-made" filter keeps rows with an AI-made sound, dims the rest', { hum, ai });
  await p.selectOption('#sOrigin', '');
  await p.fill('#sFilter', 'zzzzz'); await p.waitForTimeout(300);
  ok(await p.$('#soundMatrix tr.none') !== null, 'a filter matching nothing says so');
  await p.click('#soundMatrix tr.none button'); await p.waitForTimeout(100);
  ok(await p.$eval('#sFilter', e => e.value) === '' && await p.$('#soundMatrix tr.none') === null, '"clear the filters" brings the rows back');
  const widths = await p.$$eval('#soundMatrix th.pack', ths => ths.map(t => Math.round(t.getBoundingClientRect().width)));
  ok(Math.max(...widths) - Math.min(...widths) <= 2, 'kit columns are even', widths);
  // re-rendering keeps unchanged rows: a metric switch rewrites only the value lines, "show" redraws only rows it changes
  const reuse = await p.evaluate(async () => {
    const rows = () => [...document.querySelectorAll('#soundMatrix tbody tr')], before = rows(), sel = id => document.getElementById(id);
    const td = document.querySelector('#soundMatrix td.cell[data-key="sounds/default/ROCKET"]'), v0 = td.querySelector('.vals .main').textContent;
    sel('sMetric').value = 'dur'; let t = performance.now(); sel('sMetric').dispatchEvent(new Event('change')); const tMetric = performance.now() - t;
    const v1 = td.querySelector('.vals .main').textContent, kept1 = rows().every((r, i) => r === before[i]);
    sel('sMetric').value = 'lufs'; sel('sMetric').dispatchEvent(new Event('change'));
    sel('sWhich').value = 'orig'; t = performance.now(); sel('sWhich').dispatchEvent(new Event('change')); const tWhich = performance.now() - t;
    const kept2 = rows().every((r, i) => r === before[i]);
    sel('sWhich').value = 'game'; sel('sWhich').dispatchEvent(new Event('change'));
    return { v0, v1, kept1, kept2, tMetric: Math.round(tMetric), tWhich: Math.round(tWhich) };
  });
  ok(reuse.kept1 && /s$/.test(reuse.v1) && reuse.v1 !== reuse.v0 && reuse.kept2, 'metric / show switches keep the rows and update the values', reuse);
  console.log('     (matrix: metric switch ' + reuse.tMetric + ' ms, show switch ' + reuse.tWhich + ' ms)');

  // --- voices: parallel load, the same filters ------------------------------------------------------------------------
  const t0 = Date.now();
  await p.click('button[data-tab="voices"]');
  await p.waitForFunction(() => Q.VOICES.every(v => Q.loaded['voice:' + v.dir]), null, { timeout: 60000 });
  console.log('     (voices loaded in ' + (Date.now() - t0) + ' ms)');
  ok(await p.evaluate(() => ['vOver', 'vFav', 'vOrigin'].every(id => document.getElementById(id))), 'Voices has the cut / favourites / origin filters');
  await p.evaluate(() => Q.ui.toggleFav('voices/male/GO'));
  await p.check('#vFav'); await p.waitForTimeout(200);
  const fav = await p.$$eval('#voiceMatrix .rh-name', e => e.map(x => x.textContent));
  ok(fav.length === 1 && fav[0] === 'GO', 'favourites filter on Voices', fav);
  await p.uncheck('#vFav');
  await p.evaluate(() => Q.ui.toggleFav('voices/male/GO'));
  ok(await p.evaluate(() => document.querySelectorAll('#voiceMatrix td.cell .badge[class*="o-"]').length) >= 240, 'voice cells have origin chips');

  // --- overview of voices draws without another change --------------------------------------------------------------------
  await p.click('button[data-tab="overview"]');
  await p.evaluate(() => { const t = document.querySelector('#ovTip'); t.hidden = false; t.textContent = 'stale'; });
  await p.selectOption('#ovSet', 'voice');
  await p.waitForFunction(() => document.querySelectorAll('#overview svg g[style]').length >= 245, null, { timeout: 30000 }).catch(() => {});
  ok(await p.evaluate(() => document.querySelectorAll('#overview svg g[style]').length) >= 245, 'Overview > Voices draws every line');
  ok(await p.$eval('#ovTip', e => e.hidden), 'the old chart\'s tooltip is gone');

  // --- chain: JS biquads match the Web Audio node, prefix cache ----------------------------------------------------------
  const dsp = await p.evaluate(async () => {
    const x = Q.assets['sounds/default/EXPLOSION'].orig, out = {};
    for (const [type, pr] of [['highpass', { freq: 200, q: 0.707 }], ['lowpass', { freq: 3000, q: 6 }], ['lowshelf', { freq: 200, db: 6 }], ['peaking', { freq: 1000, db: -9, q: 2 }], ['highshelf', { freq: 6000, db: 4 }]]) {
      const a = await Q.applyStep(x, { type, on: true, p: pr }), r = await Q.biquadOffline(x, type, pr);
      let d = 0; for (let i = 0; i < a.length; i++) d = Math.max(d, Math.abs(a[i] - r[i]));
      out[type] = d;
    }
    const e = { trim: [0, null], fadeIn: 0, fadeOut: 0, chain: ['highpass', 'compressor', 'drive', 'peaking', 'limiter'].map(t => Q.newStep(t)) };
    const real = Q.applyStep; let calls = 0;
    Q.applyStep = (y, st) => { calls++; return real(y, st); };
    let t = performance.now(); const first = await Q.renderEditData(x, e); const tFirst = performance.now() - t;
    calls = 0; e.chain[4].p.ceiling = -3;
    t = performance.now(); const second = await Q.renderEditData(x, e); const tLast = performance.now() - t;
    const lastCalls = calls;
    Q.applyStep = real;
    const e2 = JSON.parse(JSON.stringify(e)); e2.chain.forEach(s => { s.p = Object.assign({}, s.p, { _: 1 }); });   // (a cold render of the same chain)
    const cold = await (async () => { let y = Q.trimFade(x, e); for (const st of e.chain) y = await Q.applyStep(y, st); return y; })();
    let d = 0; for (let i = 0; i < cold.length; i++) d = Math.max(d, Math.abs(cold[i] - second[i]));
    return { out, lastCalls, tFirst, tLast, coldDiff: d, firstLen: first.length };
  });
  ok(Object.values(dsp.out).every(d => d < 2e-4), 'JS biquads match BiquadFilterNode (max diff < 2e-4)', dsp.out);
  ok(dsp.lastCalls === 1 && dsp.coldDiff === 0, 'changing the last step re-renders only that step, same output', dsp);
  console.log('     (5-step chain on EXPLOSION: first render ' + dsp.tFirst.toFixed(0) + ' ms, after a last-step change ' + dsp.tLast.toFixed(0) + ' ms)');

  // --- missing: removing your own name removes its sound from the export ----------------------------------------------------
  await p.click('button[data-tab="missing"]');
  await p.fill('#msName', 'MY_NEW_SOUND'); await p.click('#msAdd');
  const miss = await p.evaluate(async () => {
    const k = 'sounds/default/MY_NEW_SOUND';
    Q.commit('design', () => { const e = Q.newEdit(); e.src = Q.addSource('gen', new Float32Array(4800).fill(0.2)); e.made = 'gen'; Q.state.edits[k] = e; });
    const before = Q.exportList().includes(k);
    return { before };
  });
  await p.click('#missingTable button.mini:has-text("remove")');
  await p.waitForSelector('#dlg[open]');
  await p.click('#dlg .dlg-f button.primary');
  await p.waitForTimeout(100);
  const miss2 = await p.evaluate(() => { const k = 'sounds/default/MY_NEW_SOUND'; const r = { after: Q.exportList().includes(k), asset: !!Q.assets[k] }; Q.undo(); r.undone = Q.exportList().includes(k); Q.redo(); r.redone = Q.exportList().includes(k); return r; });
  ok(miss.before && !miss2.after && !miss2.asset, 'removed name: nothing of it exports', miss2);
  ok(miss2.undone && !miss2.redone, 'undo brings the removed sound back, redo removes it again', miss2);

  // --- levelling: empty group, duplicate presets, quips ------------------------------------------------------------------
  await p.click('button[data-tab="groups"]');
  await p.click('#grNew');
  await p.click('#groupEditor button.primary');   // Apply
  await p.waitForTimeout(200);
  const empty = await p.evaluate(() => ({ text: document.querySelector('#groupEditor').textContent, applied: !!Q.state.groups[Q.state.groups.length - 1].applied }));
  ok(/no members/.test(empty.text) && !empty.applied, 'an empty group says it has no members and is not applied', empty.text.slice(-120));
  const dup = await p.evaluate(() => { const n0 = Q.state.groups.length; Q.ui.groups.addPreset('weapons', ['default']); const n1 = Q.state.groups.length; Q.ui.groups.addPreset('weapons', ['default']); return [n0, n1, Q.state.groups.length]; });
  ok(dup[1] === dup[0] + 1 && dup[2] === dup[1], 'the same preset twice adds its group once', dup);
  const quips = await p.evaluate(() => {
    const g = Q.ui.groups.addPreset('oneVoiceQuips', 'male')[0];
    const adder = [...document.querySelectorAll('#groupEditor .adder select')];
    adder[0].value = 'voice'; adder[0].onchange();
    return { members: g.members.length, quips: Q.QUIPS.length, adderQuips: [...adder[2].options].filter(o => o.value.startsWith('quips/')).length };
  });
  ok(quips.members === quips.quips && quips.adderQuips === quips.quips, 'quips in the presets and the member adder', quips);

  // --- dock: a committed field keeps the focus (Tab moves on), origin in view at 2560 ----------------------------------------
  await p.click('button[data-tab="sounds"]');
  await p.click('td.cell[data-key="sounds/default/ROCKET"]');
  await p.waitForTimeout(300);
  const fi = await p.$$('#editor .ed-side input[type=number]');
  await fi[2].click(); await fi[2].fill('0.01'); await p.keyboard.press('Tab');
  await p.waitForTimeout(150);
  const focus = await p.evaluate(() => { const el = document.activeElement, all = [...document.querySelectorAll('#editor .ed-side input[type=number]')]; return { idx: all.indexOf(el), tag: el.tagName, fade: Q.state.edits['sounds/default/ROCKET'] && Q.state.edits['sounds/default/ROCKET'].fadeIn }; });
  ok(focus.idx === 3 && focus.fade === 0.01, 'Tab after a trim / fade field lands on the next field', focus);
  const side = await p.evaluate(() => { const s = document.querySelector('.ed-side'), o = [...s.querySelectorAll('.side-h')].find(x => x.textContent === 'Origin'); const r = o.getBoundingClientRect(), sr = s.getBoundingClientRect(); return { w: Math.round(sr.width), originVisible: r.top >= sr.top && r.bottom <= sr.bottom }; });
  ok(side.originVisible && side.w > 400, 'at 2560 the side column is two columns with Origin in view', side);
  // a chain field: clamped to the step's range, and Tab off the dock's last field keeps the focus on it
  await p.evaluate(() => { Q.commit('t', () => { Q.state.edits['sounds/default/ROCKET'].chain = [Q.newStep('highpass'), Q.newStep('gain')]; }); });
  await p.waitForTimeout(150);
  let cn = await p.$$('#editor .ed-chain input[type=number]');
  await cn[0].click(); await cn[0].fill('900'); await p.keyboard.press('Tab'); await p.waitForTimeout(150);
  const cf1 = await p.evaluate(() => ({ type: document.activeElement.type, f: Q.state.edits['sounds/default/ROCKET'].chain[0].p.freq }));
  cn = await p.$$('#editor .ed-chain input[type=number]');
  await cn[2].click(); await cn[2].fill('500'); await p.keyboard.press('Tab'); await p.waitForTimeout(150);
  const cf2 = await p.evaluate(() => { const el = document.activeElement, all = [...document.querySelectorAll('#editor .ed-chain input[type=number]')]; return { idx: all.indexOf(el), val: el.value, db: Q.state.edits['sounds/default/ROCKET'].chain[1].p.db }; });
  ok(cf1.type === 'range' && cf1.f === 900, 'Tab after a chain field lands on the next field', cf1);
  ok(cf2.idx === 2 && cf2.db === 24 && cf2.val === '24', 'a chain field is clamped to its range and keeps the focus at the end of the dock', cf2);
  await p.evaluate(() => { document.activeElement.blur(); Q.commit('t', () => { delete Q.state.edits['sounds/default/ROCKET']; }); });

  // --- scenes volume, upload folder ------------------------------------------------------------------------------------------
  await p.click('button[data-tab="scenes"]');
  await p.fill('#scMaster', '5'); await p.dispatchEvent('#scMaster', 'change');
  ok(await p.$eval('#scMaster', e => e.value) === '1', 'Scenes volume shows the clamped value');
  await p.fill('#scMaster', ''); await p.dispatchEvent('#scMaster', 'change');
  ok(await p.$eval('#scMaster', e => e.value) === '0.6' && await p.evaluate(() => Q.ui.scenes.opt().master) === 0.6, 'an empty Scenes volume shows and plays the default 0.6');
  ok(await p.$eval('#dirUpload', e => e.webkitdirectory), 'an Upload folder input (webkitdirectory)');

  ok(!logs.length, 'no console errors at 2560x1440', logs);

  // --- export over http (the folder writes go to the origin-private file system, standing in for the picker) ------------
  {
    const srv = http.createServer((q, r) => {
      const f = path.join(dir, decodeURIComponent(q.url.split('?')[0]));
      if (!f.startsWith(dir)) { r.writeHead(403); r.end(); return; }
      fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': f.endsWith('.html') ? 'text/html' : 'text/javascript' }); r.end(d); });
    });
    await new Promise(res => srv.listen(+(process.env.QSE_TEST_PORT || 0), '127.0.0.1', res));
    const { ctx, p: q, logs: hlogs } = await open(b, { width: 1280, height: 720 }, 'http://127.0.0.1:' + srv.address().port + '/sound-editor.html');
    const ex = await q.evaluate(async () => {
      const root = await navigator.storage.getDirectory();
      for await (const [n] of root.entries()) await root.removeEntry(n, { recursive: true });
      const ov = await root.getDirectoryHandle('overrides', { create: true });
      const put = async (d, path, text) => { const parts = path.split('/'), f = parts.pop(); for (const s of parts) d = await d.getDirectoryHandle(s, { create: true }); const w = await (await d.getFileHandle(f, { create: true })).createWritable(); await w.write(text); await w.close(); };
      const has = async (d, path) => { try { const parts = path.split('/'), f = parts.pop(); for (const s of parts) d = await d.getDirectoryHandle(s); await d.getFileHandle(f); return true; } catch (e) { return false; } };
      const read = async (d, path) => { const parts = path.split('/'), f = parts.pop(); for (const s of parts) d = await d.getDirectoryHandle(s); return (await (await d.getFileHandle(f)).getFile()).text(); };
      await put(ov, 'voices/male/quips/SPREE.wav', 'mine');
      await put(ov, 'sound-levels.txt', '# hand\nsounds/retro/HIT\n');
      await Q.loadPack('voice', 'male');
      Q.commit('t', () => {
        const e = Q.newEdit(); e.chain.push(Q.newStep('gain', { db: -3 })); Q.state.edits['voices/male/quips/HEADSHOT'] = e;
        const n = Q.newEdit(); n.chain.push(Q.newStep('gain')); n.chain[0].on = false; Q.state.edits['sounds/default/KILL'] = n;   // (no-op)
        const l = Q.newEdit(); l.chain.push(Q.newStep('gain', { db: 2 })); l.levelSet = true; Q.state.edits['sounds/default/ROCKET'] = l;
      });
      const out = { list: Q.exportList(), noop: Q.isNoop('sounds/default/KILL') };
      const keys = out.list, companions = Q.quipCompanions(keys);
      out.comp = companions.map(c => c.key.split('/').pop());
      // zip: never a sound-levels.txt of its own, unless merged with the picked one
      await Q.exportZip(null, { keys, companions });
      out.zip1 = Q.lastZipNames.slice();
      await Q.exportZip(null, { keys, companions, levelsText: '# hand\nsounds/retro/HIT\n' });
      out.zip2 = Q.lastZipNames.slice();
      // folder: a default quip already there stays, sound-levels.txt is merged
      const r = await Q.exportToFolder(ov, { keys, companions });
      Q.markExported(r.keys, r.companions);
      out.kept = r.keptQuips; out.spree = await read(ov, 'voices/male/quips/SPREE.wav'); out.godlike = await has(ov, 'voices/male/quips/GODLIKE.wav');
      out.kill = await has(ov, 'sounds/default/KILL.wav'); out.levels = await read(ov, 'sound-levels.txt');
      out.changed = Q.exportList().filter(Q.changedSinceExport);
      out.prov = Object.keys(Q.prov.entries(r.keys, r.companions)).filter(k => k.includes('quips')).length;
      // remove from the game: renamed to .bak, its level line goes
      Q.commit('t', () => { delete Q.state.edits['sounds/default/ROCKET']; });
      out.listed = (await Q.listOverrides(ov)).map(f => f.key).includes('sounds/default/ROCKET');
      await Q.removeOverrides(ov, ['sounds/default/ROCKET.wav']);
      out.gone = !(await has(ov, 'sounds/default/ROCKET.wav')) && await has(ov, 'sounds/default/ROCKET.wav.bak');
      out.levels2 = await read(ov, 'sound-levels.txt');
      // a folder with no overrides inside: asked first
      const desk = await root.getDirectoryHandle('Desktop', { create: true });
      window.showDirectoryPicker = async () => desk;
      return out;
    });
    ok(ex.noop && !ex.list.includes('sounds/default/KILL'), 'an edit that plays the shipped sound is left out of the export', ex.list);
    ok(ex.comp.length === 7 && !ex.comp.includes('HEADSHOT.wav'), 'one quip exported: the voice\'s other default quips go with it', ex.comp);
    ok(!ex.zip1.includes('sound-levels.txt') && ex.zip1.includes('sound-levels-add.txt') && ex.zip1.filter(n => n.includes('/quips/')).length === 8, 'the zip never overwrites sound-levels.txt (lines to add instead)', ex.zip1);
    ok(ex.zip2.includes('sound-levels.txt') && !ex.zip2.includes('sound-levels-add.txt'), 'the zip carries a merged sound-levels.txt when the old one is picked', ex.zip2);
    ok(ex.kept.includes('voices/male/quips/SPREE.wav') && ex.spree === 'mine' && ex.godlike && !ex.kill, 'folder export keeps a quip already there and writes the missing ones', ex);
    ok(/sounds\/retro\/HIT/.test(ex.levels) && /sounds\/default\/ROCKET/.test(ex.levels), 'folder export merges sound-levels.txt', ex.levels);
    ok(!ex.changed.length && ex.prov === 7, 'after an export nothing is "changed since"; companions get provenance entries', ex);
    ok(ex.listed && ex.gone && !/default\/ROCKET/.test(ex.levels2) && /retro\/HIT/.test(ex.levels2), 'Remove from the game: .bak, its sound-levels.txt line gone', ex.levels2);
    await q.click('#btnExport'); await q.waitForSelector('#dlg[open]');
    const rows = await q.$$eval('#dlg table.exp tr', t => t.length);
    await q.click('#dlg .dlg-f button:has-text("Choose folder and write")');
    await q.waitForSelector('#dlg2[open]');
    const ask = await q.$eval('#dlg2', d => d.innerText);
    await q.click('#dlg2 .dlg-f button:has-text("Cancel")');
    await q.waitForTimeout(200);
    const wrote = await q.evaluate(async () => { const d = await (await navigator.storage.getDirectory()).getDirectoryHandle('Desktop'); for await (const e of d.entries()) return true; return false; });
    ok(/no overrides folder/.test(ask) && !wrote && rows > 1, 'a folder with no overrides folder in it is asked about first; Cancel writes nothing', ask.slice(0, 120));
    await q.click('#dlg .dlg-f button:has-text("Close")');
    ok(!hlogs.length, 'no console errors in the export run', hlogs);
    await ctx.close(); srv.close();
  }
  await b.close();
  console.log(failed ? failed + ' check(s) FAILED' : 'all checks passed');
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
