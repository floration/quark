#!/usr/bin/env node
/* Headless checks of the model editor (Chromium through Playwright, software WebGL):
     node tools/model_editor/tests/editor_test.js <asset export folder> [work folder]
   The export is what `build/quark_tests exportassets <folder>` writes. The page is built into the work folder (default:
   a temporary one) twice: from this checkout, and from a copy with fake shipped overrides and repository provenance
   (a human-made KNIGHT, a broken HEAVY). Checks provenance (undo, reset, reload, re-import, shipped models), the
   panel's fields (Tab, arrow keys), view options, the library, the toolbar without data, and no console errors.
   Exits 1 on a failure. */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs'), os = require('os'), path = require('path'), { execFileSync } = require('child_process');

const HERE = path.resolve(__dirname, '..');
const EXPORT = process.argv[2];
if (!EXPORT || !fs.existsSync(path.join(EXPORT, 'manifest.json'))) { console.error('usage: editor_test.js <asset export folder> [work folder]'); process.exit(2); }
const WORK = process.argv[3] || fs.mkdtempSync(path.join(os.tmpdir(), 'qme-test-'));
let failures = 0;
const check = (ok, what, detail) => { console.log((ok ? 'ok   ' : 'FAIL ') + what + (ok || detail === undefined ? '' : ': ' + JSON.stringify(detail))); if (!ok) failures++; };

function build(editorDir, out) {
  execFileSync('python3', [path.join(editorDir, 'build.py'), EXPORT, out], { stdio: 'pipe' });
  return 'file://' + path.join(out, 'model-editor.html');
}
// an in-memory folder with the File System Access API's handle methods the editor uses
const FS_MOCK = `
  window.mkDir = function (name) {
    const d = { kind: 'directory', name, children: new Map(),
      async getDirectoryHandle(n, o) { let c = d.children.get(n); if (!c) { if (!o || !o.create) throw new DOMException(n, 'NotFoundError'); c = mkDir(n); d.children.set(n, c); } if (c.kind !== 'directory') throw new TypeError(n); return c; },
      async getFileHandle(n, o) { let c = d.children.get(n); if (!c) { if (!o || !o.create) throw new DOMException(n, 'NotFoundError'); c = mkFile(n); d.children.set(n, c); } return c; },
      async removeEntry(n) { d.children.delete(n); },
      async *entries() { for (const e of [...d.children]) yield e; } };
    return d;
  };
  window.mkFile = function (name) {
    const f = { kind: 'file', name, data: new Uint8Array(0),
      async getFile() { return new Blob([f.data]); },
      async createWritable() { return { async write(x) { f.data = new Uint8Array(x instanceof Uint8Array ? x : await new Blob([x]).arrayBuffer()); }, async close() { } }; } };
    return f;
  };
  window.readText = async (dir, parts) => { let d = dir; for (const p of parts.slice(0, -1)) d = await d.getDirectoryHandle(p); return (await (await d.getFileHandle(parts[parts.length - 1])).getFile()).text(); };`;

async function open(browser, url, viewport) {
  const ctx = await browser.newContext({ viewport: viewport || { width: 2560, height: 1440 } });
  const page = await ctx.newPage(), log = [];
  page.on('console', m => { if (m.type() === 'error' || (m.type() === 'warning' && !/reference palette/.test(m.text()))) log.push(m.type() + ': ' + m.text()); });
  page.on('pageerror', e => log.push('pageerror: ' + e.message));
  await page.addInitScript(FS_MOCK);
  await page.goto(url);
  await page.waitForFunction(() => window.Q && (Q.ready || Q.bootError), null, { timeout: 90000 });
  return { ctx, page, log };
}
const frames = (page, n) => page.evaluate(n => new Promise(r => { let k = 0; const f = () => (++k >= n ? r() : requestAnimationFrame(f)); requestAnimationFrame(f); }), n || 3);

(async () => {
  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const url = build(HERE, path.join(WORK, 'Quark', 'model-editor'));
  const { ctx, page, log } = await open(browser, url);
  const ev = (fn, arg) => page.evaluate(fn, arg);

  // --- the library, default names, thumbnails --------------------------------------------------------------------------
  check(await ev(() => document.querySelectorAll('.card').length) === 30, 'library: 30 models');
  const cols = vw => ev(() => { const c = [...document.querySelectorAll('.cards')[0].children]; return c.filter(x => x.offsetTop === c[0].offsetTop).length; });
  check(await cols() >= 5, 'library: five or more cards across at 2560 px', await cols());
  await page.waitForFunction(() => [...document.querySelectorAll('.card img')].every(i => i.src.startsWith('data:image/png')), null, { timeout: 90000 });
  check(true, 'library: every thumbnail drawn');
  await page.click('.card[data-key="models/player/SCOUT"]');
  const names = await ev(() => [...document.querySelectorAll('.parts td.nm')].map(td => td.textContent));
  check(names.join('|') === 'legs|body|head|visor', 'default box names (SCOUT)', names);
  check(await ev(() => Q.order.every(k => (Q.models[k].defParts || []).every(p => p.name))), 'every default box is named');

  // --- provenance: undo / reset ----------------------------------------------------------------------------------------
  await page.click('#panel button:has-text("Start from scratch")');
  check(await ev(() => Q.provFor('models/player/SCOUT').origin) === 'human', 'start from scratch: human');
  await page.keyboard.press('Escape');
  await page.keyboard.press('Control+z');
  await frames(page);
  let st = await ev(() => ({ edited: Q.isEdited('models/player/SCOUT'), prov: Q.state.prov['models/player/SCOUT'] || null, origin: Q.provFor('models/player/SCOUT').origin }));
  check(!st.edited && !st.prov && st.origin === 'ai', 'undo start from scratch: its origin goes too', st);
  await page.click('.parts tr:nth-child(1)');
  await page.keyboard.press('ArrowRight');
  st = await ev(() => Q.provFor('models/player/SCOUT'));
  check(st.origin === 'ai' && /^tweak of the AI default/.test(st.note), 'a tweak after the undo is ai', st);
  await page.keyboard.press('Control+Shift+z');   // (nothing to redo: the tweak cleared it)
  await ev(() => { Q.undo(); });
  await page.click('#panel button:has-text("Start from scratch")');
  await frames(page);
  await page.click('#panel button:has-text("Reset to default")');
  await frames(page);
  await page.click('.parts tr:nth-child(2)');
  await page.keyboard.press('ArrowLeft');
  st = await ev(() => Q.provFor('models/player/SCOUT'));
  check(st.origin === 'ai' && /^tweak of the AI default/.test(st.note), 'reset clears the origin (a later tweak is ai)', st);
  await ev(() => Q.undo());   // (the tweak)
  await ev(() => Q.undo());   // (the reset: back to the scratch model, human)
  check(await ev(() => Q.provFor('models/player/SCOUT').origin) === 'human', 'undoing the reset brings the origin back');

  // --- provenance: export, reload, re-import ---------------------------------------------------------------------------
  const r1 = await ev(async () => {
    window.ov = mkDir('overrides'); window.pd = mkDir('model-editor');
    const r = await Q.exportToFolder(ov, { keys: ['models/player/SCOUT'], provDir: pd });
    const side = JSON.parse(await readText(pd, ['provenance.json']));
    const glb = new Uint8Array(await (await (await (await (await ov.getDirectoryHandle('models')).getDirectoryHandle('player')).getFileHandle('SCOUT.glb')).getFile()).arrayBuffer());
    return { written: r.written, side: side.items['models/player/SCOUT.gltf'], embedded: Q.gltf.parse(glb, 'SCOUT.glb').provenance };
  });
  check(r1.side && r1.side.origin === 'human' && r1.embedded && r1.embedded.origin === 'human', 'folder export: provenance.json and the .glb say human', r1);
  st = await ev(async () => {
    Q.state = { edits: {}, prov: {} }; Q.sources = {}; Q.clearHistory(); Q.ui.afterLoad();
    const r = await Q.loadFromFolder(ov, { provDir: pd });
    return { loaded: r.loaded, prov: Q.provFor('models/player/SCOUT') };
  });
  check(st.prov.origin === 'human' && st.prov.note === 'made from scratch in the model editor', 'load overrides folder: keeps human (from the file)', st);
  // a file without embedded provenance: the folder's provenance.json decides
  st = await ev(async () => {
    const key = 'models/player/STANDARD', m = Q.models[key], mesh = Q.boxes.toMesh(m.defParts.map(Q.clonePart).map(p => (p.c[1] += 0.01, p)), Q.refOf(key));
    const bytes = Q.gltf.writeGlb(mesh, 'STANDARD', mesh.extras, null);
    const dir = await (await ov.getDirectoryHandle('models')).getDirectoryHandle('player');
    const f = await dir.getFileHandle('STANDARD.glb', { create: true }); const w = await f.createWritable(); await w.write(bytes); await w.close();
    const pj = await ov.getFileHandle('provenance.json', { create: true }), w2 = await pj.createWritable();
    await w2.write(new TextEncoder().encode(JSON.stringify({ format: 1, items: { 'models/player/STANDARD.gltf': { origin: 'third-party', source: 'a pack', licence: 'CC0' } } }))); await w2.close();
    Q.state = { edits: {}, prov: {} }; Q.clearHistory();
    await Q.loadFromFolder(ov, { provDir: pd });
    return Q.provFor(key);
  });
  check(st.origin === 'third-party' && st.source === 'a pack' && st.licence === 'CC0', 'load overrides folder: provenance.json entry for a file without one', st);
  // Import (the dialog) of an exported .glb starts from the file's origin
  st = await ev(async () => {
    const glb = Q.gltf.exportBytes('models/player/SCOUT');
    Q.state = { edits: {}, prov: {} }; Q.clearHistory(); Q.ui.afterLoad(); Q.ui.select('models/player/SCOUT');
    const f = new File([glb], 'SCOUT.glb');
    const p = Q.ui.importFiles([f]);
    await new Promise(r => setTimeout(r, 300));
    const sel = document.querySelector('#dlg select:nth-of-type(1)'), origin = [...document.querySelectorAll('#dlg select')][1].value;
    [...document.querySelectorAll('#dlg button')].find(b => b.textContent === 'Replace').click();
    await p;
    return { dialog: origin, prov: Q.provFor('models/player/SCOUT'), sel: !!sel };
  });
  check(st.dialog === 'human' && st.prov.origin === 'human', 'import of an exported .glb keeps human', st);
  // a model whose file is moved to .bak loses its provenance entries
  st = await ev(async () => {
    Q.state = { edits: {}, prov: {} }; Q.clearHistory();
    await Q.exportToFolder(ov, { keys: [], removeKeys: ['models/player/SCOUT'], provDir: pd });
    const side = JSON.parse(await readText(pd, ['provenance.json']));
    return { side: !!side.items['models/player/SCOUT.gltf'], log: !!Q.provLog['models/player/SCOUT.gltf'] };
  });
  check(!st.side && !st.log, 'a model moved to .bak leaves provenance.json and the log', st);
  // Download provenance.json: not the models an error blocks
  st = await ev(() => {
    Q.state = { edits: {}, prov: {} }; Q.clearHistory();
    Q.ensureBoxEdit('models/weapon/RIFLE').parts[0].h[0] = 0; Q.ensureBoxEdit('models/weapon/KNIFE').parts[0].c[0] += 0.01;
    return { list: Q.exportableList(), text: JSON.parse(Q.mergeProvenance('', Q.exportableList())).items };
  });
  check(st.list.join() === 'models/weapon/KNIFE' && !st.text['models/weapon/RIFLE.gltf'], 'provenance.json leaves out a model with export errors', st.list);
  // Export to game, clicked through: the .bak dialog for unedited models' files stays up (a dialog's close event used
  // to answer the next dialog), Escape there cancels, Continue moves what is ticked
  await ev(async () => {
    Q.state = { edits: {}, prov: {} }; Q.clearHistory(); Q.ui.afterLoad();
    Q.ensureBoxEdit('models/player/SCOUT').parts[0].c[0] += 0.01; Q.bump('models/player/SCOUT');
    window.ov2 = mkDir('overrides');
    const kd = await (await ov2.getDirectoryHandle('models', { create: true })).getDirectoryHandle('player', { create: true });
    const f = await kd.getFileHandle('KNIGHT.glb', { create: true }), w = await f.createWritable(); await w.write(Q.gltf.exportBytes('models/player/KNIGHT')); await w.close();
    window.showDirectoryPicker = async o => (o && o.id === 'quark-model-editor' ? pd : ov2);
  });
  const files2 = () => ev(async () => [...(await (await ov2.getDirectoryHandle('models')).getDirectoryHandle('player')).children.keys()].sort().join());
  const bakDialog = async () => {
    await page.click('#btnExport');
    await page.click('#dlg button:has-text("Choose folder and write")');
    await page.waitForSelector('#dlg h2:has-text("Model files for unedited models")');
    await page.waitForTimeout(150);
    return ev(() => document.querySelector('#dlg').open && document.querySelector('#dlg h2').textContent);
  };
  st = await bakDialog();
  check(st === 'Model files for unedited models', 'export: the .bak dialog stays open', st);
  await page.keyboard.press('Escape');
  await frames(page);
  check(await files2() === 'KNIGHT.glb' && !(await ev(() => document.querySelector('#dlg').open)), 'export: Escape at the .bak dialog writes nothing', await files2());
  await bakDialog();
  await page.check('#dlg li input[type=checkbox]');
  await page.click('#dlg button:has-text("Continue")');
  await page.waitForSelector('#dlg h2:has-text("Written")');
  check(await files2() === 'KNIGHT.glb.bak,SCOUT.glb', 'export: a ticked unedited model\'s file is moved to .bak', await files2());
  await page.keyboard.press('Escape');
  await ev(() => { Q.state = { edits: {}, prov: {} }; Q.clearHistory(); Q.ui.afterLoad(); });

  // --- New project: undo off -----------------------------------------------------------------------------------------
  await page.click('.card[data-key="models/player/STANDARD"]');
  await page.click('.parts tr:nth-child(1)');
  await page.keyboard.press('ArrowRight');
  await frames(page);
  check(!(await ev(() => document.querySelector('#btnUndo').disabled)), 'undo enabled after an edit');
  await page.click('#top .menu > button:has-text("Project")');
  await page.click('#btnResetProject');
  await page.click('#dlg button:has-text("Discard all edits")');
  await frames(page);
  check(await ev(() => document.querySelector('#btnUndo').disabled), 'undo disabled after New project');

  // --- the panel's fields: Tab, arrow keys --------------------------------------------------------------------------------
  await page.click('.parts tr:nth-child(2)');
  await page.click('#panel input[data-fid="c0"]');
  await page.keyboard.press('Control+a'); await page.keyboard.type('0.01');
  await page.keyboard.press('Tab');
  await frames(page);
  st = await ev(() => ({ fid: document.activeElement.dataset.fid, x: Q.current('models/player/STANDARD').parts[1].c[0] }));
  check(st.fid === 'c1' && Math.abs(st.x - 0.01) < 1e-9, 'Tab after typing: the change is kept and the next field has the focus', st);
  const y0 = await ev(() => Q.current('models/player/STANDARD').parts[1].c.slice());
  await page.keyboard.press('ArrowUp'); await frames(page); await page.keyboard.press('ArrowUp'); await frames(page);
  const y1 = await ev(() => Q.current('models/player/STANDARD').parts[1].c.slice());
  check(Math.abs(y1[1] - y0[1] - 0.01) < 1e-6 && y1[2] === y0[2] && y1[0] === y0[0], 'arrow keys step the focused field only', { y0, y1 });
  await page.fill('#panel input[data-fid="h0"]', '-1'); await page.press('#panel input[data-fid="h0"]', 'Enter');
  check(await ev(() => Q.current('models/player/STANDARD').parts[1].h[0]) === 0.0005, 'a negative half size stops at 0.5 mm');
  await page.keyboard.press('Escape');

  // --- view options -----------------------------------------------------------------------------------------------------
  await page.click('#viewBar .menu > button');
  await page.fill('#vFov', '500'); await page.press('#vFov', 'Enter');
  await page.fill('#vBright', '-3'); await page.press('#vBright', 'Enter');
  st = await ev(() => ({ fov: Q.scene.view.fov, bright: Q.scene.view.bright, field: document.querySelector('#vFov').value }));
  check(st.fov === 140 && st.bright === 0.6 && st.field === '140', 'view settings are clamped to their range', st);
  await page.keyboard.press('Escape');
  check(await ev(() => !document.querySelector('.menu.open')), 'Escape closes the More menu');
  await page.click('#top .menu > button:has-text("Project")');
  await page.keyboard.press('Escape');
  check(await ev(() => !document.querySelector('.menu.open')), 'Escape closes the Project menu');
  await ev(() => { Q.scene.resetView(); Q.ui.syncViewBar(); });
  st = await ev(() => { Q.scene.view.hitbox = true; Q.scene.build(); Q.scene.updateOverlay(); const n = Q.scene.current.overlay.count; Q.scene.view.hitbox = false; Q.scene.updateOverlay(); return n; });
  check(st > 0, 'hitbox shows in orbit', st);
  await page.click('.card[data-key="models/projectile/ROCKET"]');
  await page.click('#tabs button[data-mode="flight"]');
  const zs = [];
  for (let i = 0; i < 3; i++) { await frames(page, 6); zs.push(await ev(() => [Q.scene.focus.model[14], Q.scene.current.ground.z])); }
  check(zs.every(z => z[0] === 0) && new Set(zs.map(z => z[1])).size > 1 && (await ev(() => Q.scene.orbit.dist)) < 3, 'in flight: the projectile stays in view, the floor streams', zs);
  await page.click('.card[data-key="models/prop/BOMB"]');
  check(!(await ev(() => Q.scene.modesFor('models/prop/BOMB').includes('track'))), 'BOMB has no Track view');
  await page.click('.card[data-key="models/weapon/RIFLE"]');
  await page.click('#tabs button[data-mode="tp"]');
  check(await ev(() => document.querySelector('#vHoldIt').disabled), '"hold the weapon" is off where it does nothing');
  check(log.length === 0, 'no console errors or warnings', log);
  await ctx.close();

  // --- 1280 x 720 -----------------------------------------------------------------------------------------------------------
  {
    const o = await open(browser, url, { width: 1280, height: 720 });
    const c = await o.page.evaluate(() => { const c = [...document.querySelectorAll('.cards')[0].children]; return c.filter(x => x.offsetTop === c[0].offsetTop).length; });
    const sw = await o.page.evaluate(() => document.documentElement.scrollWidth <= innerWidth);
    check(c >= 2 && sw, '1280 px: two cards across, no horizontal scroll', c);
    check(o.log.length === 0, '1280 px: no console errors', o.log);
    await o.ctx.close();
  }

  // --- shipped overrides with repository provenance -----------------------------------------------------------------------------
  {
    const repo = path.join(WORK, 'repo'), ed = path.join(repo, 'tools', 'model_editor');
    fs.rmSync(repo, { recursive: true, force: true });
    fs.mkdirSync(path.join(repo, 'tools'), { recursive: true });
    fs.cpSync(HERE, ed, { recursive: true, filter: s => !s.includes(path.sep + 'tests') });
    const o0 = await open(browser, url);   // (a KNIGHT box model from the page itself)
    const knight = await o0.page.evaluate(() => { const k = 'models/player/KNIGHT'; Q.ensureBoxEdit(k).parts[3].h[1] += 0.05; return Q.bytesToB64(Q.gltf.exportBytes(k)); });
    await o0.ctx.close();
    fs.mkdirSync(path.join(repo, 'assets', 'overrides', 'models', 'player'), { recursive: true });
    fs.writeFileSync(path.join(repo, 'assets', 'overrides', 'models', 'player', 'KNIGHT.glb'), Buffer.from(knight, 'base64'));
    fs.writeFileSync(path.join(repo, 'assets', 'overrides', 'models', 'player', 'HEAVY.glb'), Buffer.from('glTF broken'));
    fs.mkdirSync(path.join(repo, 'assets', 'provenance'), { recursive: true });
    fs.writeFileSync(path.join(repo, 'assets', 'provenance', 'models.json'), JSON.stringify({ format: 1, items: {
      'models/player/KNIGHT.gltf': { origin: 'human', by: 'alex', source: 'Quark model editor', note: 'a knight' },
      'models/player/SCOUT.gltf': { origin: 'ai', source: 'box model in src/client/models.cpp' } } }));
    const o = await open(browser, build(ed, path.join(WORK, 'Quark2', 'model-editor')));
    const p = o.page;
    st = await p.evaluate(() => ({ badges: document.querySelector('.card[data-key="models/player/KNIGHT"] .badges').innerText, heavy: document.querySelector('.card[data-key="models/player/HEAVY"] .badges').innerText,
      banner: document.querySelector('#banner').innerText }));
    check(/shipped/i.test(st.badges) && /human/i.test(st.badges), 'shipped KNIGHT shows shipped + human', st.badges);
    check(/refused/i.test(st.heavy) && /refuses the shipped override/.test(st.banner), 'a broken shipped override is flagged (badge, banner)', st);
    await p.click('.card[data-key="models/player/HEAVY"]');
    st = await p.evaluate(() => document.querySelector('#panel .prob.error').textContent);
    check(st.includes('(assets/overrides/models/player/HEAVY.glb)'), 'the panel names the refused file\'s path', st);
    await p.click('.card[data-key="models/player/KNIGHT"]');
    await p.click('.parts tr:nth-child(1)');
    await p.keyboard.press('ArrowRight');
    st = await p.evaluate(() => Q.provFor('models/player/KNIGHT'));
    check(st.origin === 'human' && /^tweak of the shipped model \(assets\/overrides, human, by alex\) in the model editor: 1 box moved/.test(st.note), 'editing a shipped human model stays human, diffed against it', st);
    check(o.log.length === 0, 'shipped build: no console errors', o.log);
    await o.ctx.close();
  }

  // --- no data: the toolbar is disabled ---------------------------------------------------------------------------------
  {
    const nd = path.join(WORK, 'nodata'); fs.mkdirSync(nd, { recursive: true });
    execFileSync('python3', [path.join(HERE, 'build.py'), EXPORT, nd, '--page-only']);
    const ctx2 = await browser.newContext(), p = await ctx2.newPage();
    await p.goto('file://' + path.join(nd, 'model-editor.html'));
    await p.waitForFunction(() => window.Q && Q.bootError, null, { timeout: 30000 });
    st = await p.evaluate(() => [...document.querySelectorAll('#top button, #viewBar button')].filter(b => !b.disabled).map(b => b.textContent));
    check(st.length === 0, 'no data: the toolbars are disabled', st);
    await ctx2.close();
  }

  await browser.close();
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
