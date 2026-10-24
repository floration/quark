// node test/e2e.js [screenshot dir] - drives ui-customizer.html (UIC_HTML, default the installed one: Quark/ui-customizer)
// in a headless browser (test/browser.js: playwright-core + an installed Edge/Chrome), does a few edits with the mouse and
// keyboard, and checks the exported spec. Font previews go to <screenshot dir>/fonts when a screenshot dir is given.
'use strict';
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { findPlaywright, findBrowser, defaultHtml } = require('./browser.js');

(async () => {
  const { chromium } = findPlaywright();
  let vwidths = '';
  const shots = process.argv[2] || null;
  if (shots) fs.mkdirSync(shots, { recursive: true });
  const browser = await chromium.launch({ executablePath: findBrowser(), headless: true });
  const ctx = await browser.newContext({ viewport: { width: 1600, height: 900 }, acceptDownloads: true });
  // a fake File System Access folder (the real picker can't be driven headless): provenance.json is written into it,
  // kept in localStorage across reloads; it starts with an item of another page that the merge must keep
  await ctx.addInitScript(() => {
    let files = null;
    try { files = JSON.parse(localStorage.getItem('__fakeFs') || 'null'); } catch (e) { /* none */ }
    files = window.__fakeFiles = files || { 'build.py': '', 'provenance.json': JSON.stringify({ format: 1, tool: 'ui-customizer',
      items: { 'ui/zz-other': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'kept by the merge' } } }) };
    const save = () => localStorage.setItem('__fakeFs', JSON.stringify(files));
    const fileHandle = (name) => ({ kind: 'file', name, getFile: async () => ({ text: async () => files[name] }),
      createWritable: async () => { let buf = ''; return { write: async (t) => { buf += t; }, close: async () => { files[name] = buf; save(); } }; } });
    const dir = { kind: 'directory', name: 'ui-customizer', queryPermission: async () => 'granted', requestPermission: async () => 'granted',
      getFileHandle: async (name, o) => {
        if (!(name in files)) { if (!o || !o.create) throw new DOMException('not found', 'NotFoundError'); files[name] = ''; }
        return fileHandle(name);
      } };
    window.__pickerCalls = 0;
    window.showDirectoryPicker = async () => { window.__pickerCalls++; return dir; };
  });
  const page = await ctx.newPage();
  const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push(String(e)));
  const html = path.resolve(defaultHtml());
  const url = 'file:///' + html.replace(/\\/g, '/');
  await page.goto(url);
  await page.waitForTimeout(300);
  const ev = (fn, arg) => page.evaluate(fn, arg);

  const nPages = await ev(() => QApp.S.doc.pages.length);
  assert.ok(nPages > 0, 'pages loaded');
  const listed = await page.locator('#pageList .pg').count();
  assert.strictEqual(listed, nPages, 'every page listed');

  // screen position of a game point
  async function at(gx, gy) {
    const r = await page.locator('#ov').boundingBox();
    const z = await ev(() => QApp.zoom());
    return [r.x + gx * z, r.y + gy * z];
  }
  // pick a page and a button on it to drag
  const target = await ev(() => {
    for (const p of QApp.S.doc.pages) {
      const e = p.elements.find((x) => x.kind === 'button' && x.w > 60 && x.y > 150 && x.y < 450 && Number.isInteger(x.x) && Number.isInteger(x.y));
      if (e) return { page: p.uid, uid: e.uid, x: e.x, y: e.y, w: e.w, h: e.h, label: e.label };
    }
    return null;
  });
  assert.ok(target, 'found a button to drag');
  await ev((u) => QApp.selectPage(u), target.page);
  // drag it 100 px down
  const [sx, sy] = await at(target.x + target.w / 2 + 7, target.y + target.h / 2);
  const [, ey] = await at(0, target.y + target.h / 2 + 100);
  await page.mouse.move(sx, sy);
  await page.mouse.down();
  await page.mouse.move(sx, sy + 10, { steps: 2 });
  await page.mouse.move(sx, ey, { steps: 6 });
  await page.mouse.up();
  const moved = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.ok(Math.abs(moved.y - target.y - 100) <= 1, 'dragged 100 px down (got ' + (moved.y - target.y) + ')');   // (+-1: mouse pixels at a fractional zoom)
  assert.strictEqual(moved.x, target.x, 'x unchanged');

  // arrow keys: 1 px and 10 px
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('Shift+ArrowRight');
  let cur = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.strictEqual(cur.x - target.x, 11, 'nudged 11 px');

  // undo the nudges (merged into one step) and the drag, then redo
  await page.keyboard.press('Control+z');
  cur = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.strictEqual(cur.x, target.x, 'undo: nudges');
  await page.keyboard.press('Control+z');
  cur = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.strictEqual(cur.y, target.y, 'undo: drag');
  const provOf = (u) => ev((x) => { const p = QApp.S.doc.pages.find((q) => q.uid === x); return p.prov || null; }, u);
  assert.strictEqual(await provOf(target.page), null, 'undoing every edit of the page clears its mark');
  await page.keyboard.press('Control+y');
  cur = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.strictEqual(cur.y, moved.y, 'redo: drag');
  assert.strictEqual((await provOf(target.page)).origin, 'human', 'an edit marks the page human');

  // resize with the bottom-right handle (it is selected)
  const [hx, hy] = await at(cur.x + cur.w, cur.y + cur.h);
  const [hx2, hy2] = await at(cur.x + cur.w + 40, cur.y + cur.h + 8);
  await page.mouse.move(hx, hy);
  await page.mouse.down();
  await page.mouse.move(hx2, hy2, { steps: 5 });
  await page.mouse.up();
  cur = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.ok(Math.abs(cur.w - (target.w + 40)) <= 1 && Math.abs(cur.h - (target.h + 8)) <= 1, `resized to ${cur.w}x${cur.h}`);

  // rename in the inspector
  const lab = page.locator('#inspector .row:has(> label:text-is("label")) input');
  await lab.fill('RENAMED BY TEST');
  await lab.press('Tab');
  cur = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.strictEqual(cur.label, 'RENAMED BY TEST');
  await page.locator('#inspector textarea[placeholder^="What should"]').fill('Test note: keep it near the bottom');
  await page.locator('#ov').click({ position: { x: 2, y: 2 } });   // (blur; selects nothing or the backdrop is skipped)

  // add a widget from the palette
  const before = await ev(() => QApp.S.doc.pages.find((p) => p.uid === QApp.S.pageUid).elements.length);
  await page.locator('#palette button', { hasText: 'Dropdown' }).click();
  const after = await ev(() => QApp.S.doc.pages.find((p) => p.uid === QApp.S.pageUid).elements.length);
  assert.strictEqual(after, before + 1, 'palette adds a widget');
  // delete it again with Del, then delete an original widget
  await page.locator('#ov').focus().catch(() => {});
  await page.keyboard.press('Delete');
  assert.strictEqual(await ev(() => QApp.S.doc.pages.find((p) => p.uid === QApp.S.pageUid).elements.length), before);

  // cut the dragged button and paste it on another page
  await ev((u) => { QApp.S.sel = new Set([u]); }, target.uid);
  await page.keyboard.press('Control+x');
  const other = await ev((u) => QApp.S.doc.pages.find((p) => p.uid !== u).uid, target.page);
  await ev((u) => QApp.selectPage(u), other);
  await page.keyboard.press('Control+v');
  const onOther = await ev((a) => QApp.S.doc.pages.find((p) => p.uid === a.o).elements.some((e) => e.uid === a.u), { o: other, u: target.uid });
  assert.ok(onOther, 'cut + paste moved it to the other page');

  // export the spec
  const [dl] = await Promise.all([page.waitForEvent('download'), page.locator('#bSpec').click()]);
  assert.ok(/^ui-spec-\d{4}-\d\d-\d\d-\d{4}\.md$/.test(dl.suggestedFilename()), 'spec file name ' + dl.suggestedFilename());
  const md = fs.readFileSync(await dl.path(), 'utf8');
  const has = (s) => assert.ok(md.includes(s), 'spec should contain: ' + s);
  has('**moved here from page');
  has('label: ' + JSON.stringify(target.label) + ' -> "RENAMED BY TEST"');
  has(`moved: (${target.x}, ${target.y}) -> (${target.x}, ${moved.y})`);
  has(`resized: ${target.w}x${target.h} -> `);
  has('note: Test note: keep it near the bottom');
  has('| widgets moved between pages | 1 |');
  assert.ok(await page.locator('#modal').isVisible(), 'spec modal shown');
  if (shots) fs.writeFileSync(path.join(shots, 'e2e-spec.md'), md);
  await page.locator('#mClose').click();
  // Export spec also merged the page origins into provenance.json (the picked folder)
  await page.waitForFunction(() => (window.__fakeFiles['provenance.json'] || '').includes('"human"'), null, { timeout: 5000 });
  const prov1 = JSON.parse(await ev(() => window.__fakeFiles['provenance.json']));
  const tId = await ev((u) => QApp.S.doc.pages.find((p) => p.uid === u).id, target.page);
  const oId = await ev((u) => QApp.S.doc.pages.find((p) => p.uid === u).id, other);
  assert.deepStrictEqual(Object.keys(prov1), ['format', 'tool', 'items'], 'provenance format');
  assert.strictEqual(prov1.items['ui/' + tId].origin, 'human', 'edited page recorded as human');
  assert.strictEqual(prov1.items['ui/' + oId].origin, 'human', 'paste target recorded as human');
  assert.strictEqual(prov1.items['ui/zz-other'].note, 'kept by the merge', 'other items kept');
  assert.strictEqual(Object.keys(prov1.items).length, 3, 'only changed pages listed: ' + Object.keys(prov1.items).join(', '));
  assert.strictEqual(await ev(() => window.__pickerCalls), 1, 'folder picked once');
  assert.ok((await page.locator('#stInfo').textContent()).includes('provenance.json updated'), 'status bar says provenance.json was updated');

  // autosave survives a reload
  await page.waitForTimeout(600);
  await page.reload();
  await page.waitForTimeout(300);
  const kept = await ev((u) => QApp.S.doc.pages.flatMap((p) => p.elements).find((e) => e.uid === u), target.uid);
  assert.ok(kept && kept.label === 'RENAMED BY TEST', 'autosaved layout reloaded');
  assert.strictEqual((await provOf(target.page)).origin, 'human', 'mark autosaved');

  // ---- in-page dialogs (the Quark Project app's Electron has no window.prompt): + Page, Esc cancels, ids stay unique ----
  page.on('dialog', (d) => { errors.push('native dialog: ' + d.type() + ' ' + d.message()); d.dismiss(); });
  const nBefore = await ev(() => QApp.S.doc.pages.length);
  await page.locator('#bAddPage').click();
  assert.ok(await page.locator('#dlg').isVisible(), '+ Page opens the in-page dialog');
  await page.keyboard.press('Escape');
  assert.ok(!(await page.locator('#dlg').isVisible()) && (await ev(() => QApp.S.doc.pages.length)) === nBefore, 'Esc cancels');
  await page.locator('#bAddPage').click();
  await page.locator('#dlgInput').fill('  ');
  await page.keyboard.press('Enter');
  assert.ok(await page.locator('#dlg').isVisible(), 'an empty id is refused');
  await page.locator('#dlgInput').fill('main');
  await page.keyboard.press('Enter');
  const addedId = await ev(() => QApp.S.doc.pages.find((p) => p.uid === QApp.S.pageUid).id);
  assert.strictEqual(addedId, 'main-2', 'a taken id gets -2');
  assert.strictEqual(await ev(() => QApp.S.doc.pages.length), nBefore + 1, '+ Page added a page');
  const idInput = page.locator('#inspector .row:has(> label:text-is("id")) input');
  await idInput.fill('main');
  assert.ok(await idInput.evaluate((i) => i.classList.contains('bad')), 'a duplicate page id is flagged');
  assert.strictEqual(await ev(() => QApp.S.doc.pages.find((p) => p.uid === QApp.S.pageUid).id), 'main-2', 'and not applied');
  await idInput.press('Tab');
  assert.strictEqual(await idInput.inputValue(), 'main-2', 'leaving the field puts the id back');
  await page.locator('#palette button', { hasText: 'Button' }).click();   // (an empty page goes without asking)
  await ev(() => { QApp.S.sel = new Set(); });
  await ev(() => QApp.commit(() => {}));
  await page.locator('#inspector button', { hasText: 'Delete page' }).click();
  assert.ok(await page.locator('#dlg').isVisible(), 'Delete page asks in the page');
  await page.locator('#dlgOk').click();
  assert.strictEqual(await ev(() => QApp.S.doc.pages.length), nBefore, 'Delete page (confirmed in the page)');

  // ---- origins in the page list, preview at other resolutions (read only), spec layout tables ----
  const nOrg = await page.locator('#pageList .pg .org').count();
  assert.strictEqual(nOrg, nBefore, 'every page shows who made it');
  await page.locator('.ochip.o-human').click();
  const humanRows = await page.locator('#pageList .pg').count();
  assert.ok(humanRows >= 1 && humanRows < nBefore, 'origin filter lists the human pages (' + humanRows + ')');
  await page.locator('.ochip.o-human').click();
  const mainId = await ev(() => QApp.S.doc.pages.find((p) => p.id === 'main').uid);
  await ev((u) => QApp.selectPage(u), mainId);
  const quit = await ev(() => QApp.S.doc.pages.find((p) => p.id === 'main').elements.find((e) => e.kind === 'button' && /QUIT/.test(e.label || '')));
  for (const [v, fw] of [['2560x1440', 1280], ['1920x1200', 1152], ['3440x1440', 1720]]) {
    await page.locator('#sView').selectOption(v);
    const pv = await ev(() => { const r = QApp.preview(); const q = r.page.elements.find((e) => e.kind === 'button' && /QUIT/.test(e.label || '')); return { fw: r.view.fw, x: q.x, k: r.view.k }; });
    assert.strictEqual(pv.fw, fw, v + ' frame width in 720p units');
    assert.strictEqual(pv.x, quit.x + (fw - 1280) / 2, v + ': a centred button keeps its offset from the centre');
  }
  assert.ok(Math.abs((await ev(() => QApp.preview().view.k)) - 1) < 1e-9, '1440p text is exact (scale 4)');
  const ovBox = await page.locator('#ov').boundingBox();
  await page.mouse.click(ovBox.x + ovBox.width / 2, ovBox.y + ovBox.height / 2);
  assert.strictEqual(await ev(() => QApp.S.sel.size), 0, 'the preview is read only');
  if (shots) await page.screenshot({ path: path.join(shots, 'e2e-preview-3440.png') });
  await page.locator('#sView').selectOption('');
  assert.strictEqual(await ev(() => QApp.preview()), null, 'back to editing');
  const full = await ev(() => QApp.spec('all')), part = await ev(() => QApp.spec('changed'));
  assert.ok(full.includes('## Full layout') && !part.includes('## Full layout') && part.length < full.length / 2, `changes-only spec is small (${part.length} vs ${full.length} chars)`);
  const stale = await ev(() => JSON.parse(document.getElementById('buildInfo').textContent).freshness);
  assert.strictEqual(await page.locator('#staleBar').isVisible(), !!(stale && stale.stale), 'the out-of-date banner shows exactly when the dump is stale');

  // the help popover and a screenshot of every page group's first page
  await page.keyboard.press('?');
  assert.ok(await page.locator('#help').isVisible(), 'help opens with ?');
  await page.keyboard.press('Escape');
  if (shots) {
    await ev((u) => QApp.selectPage(u), target.page);
    await page.waitForTimeout(100);
    await page.screenshot({ path: path.join(shots, 'e2e-1.png') });
    const ids = await ev(() => QApp.S.doc.pages.map((p) => p.uid));
    for (const [i, u] of ids.slice(1, 4).entries()) {
      await ev((x) => QApp.selectPage(x), u);
      await page.waitForTimeout(100);
      await page.screenshot({ path: path.join(shots, `e2e-${i + 2}.png`) });
    }
  }

  // ---- font trials ----
  page.on('dialog', (d) => d.accept());
  const nFonts = await page.locator('#sFont option').count();
  assert.ok(nFonts >= 8, 'font menu lists the trials (' + nFonts + ')');
  const settingsUid = await ev(() => (QApp.S.doc.pages.find((p) => p.id === 'settings') || QApp.S.doc.pages[0]).uid);
  await ev((u) => QApp.selectPage(u), settingsUid);
  const previews = path.join(shots || path.join(os.tmpdir(), 'quark-ui-e2e'), 'fonts');
  fs.mkdirSync(previews, { recursive: true });
  const shotFont = async (id, file) => {
    await page.locator('#sFont').selectOption(id);
    await page.waitForFunction((x) => QApp.S.font.id === x && !!QApp.fonts.get(x).family, id);
    await page.waitForTimeout(250);
    const fam = await ev((x) => QApp.fonts.get(x).family, id);
    assert.ok(await ev((f) => document.fonts.check('700 15px ' + f), fam), id + ' loaded');
    await page.locator('#stage').screenshot({ path: path.join(previews, file) });
  };
  // every font of the menu (what this PC has: the repo's, Windows', the downloaded open ones) loads and draws
  const fontIds = await ev(() => QFonts.list().filter((e) => !e.uploaded).map((e) => e.id));
  assert.strictEqual(fontIds[0], 'rubik', "the game's font first");
  for (const id of fontIds) await shotFont(id, `settings-${id}.png`);
  // the game's other fonts (Settings > FONT): their own menu group, all can ship
  const ig = await page.locator('#sFont optgroup[label="In the game: Settings > FONT"] option').allTextContents();
  assert.ok(ig.length >= 5, 'in-game group: ' + ig.join(', '));
  assert.ok(ig.every((t) => t.endsWith('[can ship]')), 'the in-game fonts can ship');
  const hasFont = (id) => fontIds.includes(id);
  // variable width / weight really change the drawing: a label's width as the page draws it
  const tw = (id, weight, text) => ev(async (a) => {
    await QApp.fonts.ensure(a.id);
    QApp.setFont(a.id);
    if (a.weight) QApp.setFontOpt('weight', a.weight);
    const c = document.createElement('canvas').getContext('2d');
    return QRender.textWidth(c, a.text || 'SETTINGS AND CONTROLS', 2);
  }, { id, weight, text });
  const wR5 = await tw('rubik', 500), wR9 = await tw('rubik', 900);
  assert.ok(wR9 > wR5 * 1.02, `Rubik 900 is wider than 500 (${wR9.toFixed(1)} vs ${wR5.toFixed(1)})`);
  vwidths = `Rubik 500/900 ${wR5.toFixed(0)}/${wR9.toFixed(0)}`;
  if (hasFont('fredoka') && hasFont('fredokasc')) {
    const wF = await tw('fredoka'), wF85 = await tw('fredokasc');
    assert.ok(wF85 < wF * 0.93, `Fredoka 85% is narrower (${wF85.toFixed(1)} vs ${wF.toFixed(1)})`);
    vwidths += `, Fredoka ${wF.toFixed(0)}->85% ${wF85.toFixed(0)}`;
  }
  if (hasFont('bahn') && hasFont('bahnc')) {
    const wB = await tw('bahn'), wBc = await tw('bahnc');
    assert.ok(wBc < wB * 0.93, `Bahnschrift Condensed is narrower (${wBc.toFixed(1)} vs ${wB.toFixed(1)})`);
    vwidths += `, Bahnschrift ${wB.toFixed(0)}->condensed ${wBc.toFixed(0)}`;
  }
  // weight, size multiplier, minimum scale
  const trial = hasFont('bahnc') ? 'bahnc' : 'jost';   // (Bahnschrift: a Windows font, preview only)
  await page.locator('#sFont').selectOption(trial);
  await page.waitForFunction((x) => QApp.S.font.id === x, trial);
  await page.locator('#sWeight').selectOption(trial === 'bahnc' ? '700' : '600');
  await page.locator('#sSize').selectOption('1.2');
  await page.locator('#sMinTs').selectOption('2');
  let fstate = await ev(() => QApp.S.font);
  assert.deepStrictEqual([fstate.weight, fstate.mult, fstate.minTs], [trial === 'bahnc' ? 700 : 600, 1.2, 2], 'weight / size / min scale set');
  // compare: F flips, side by side shows a second canvas
  await page.locator('#bFonts').click();
  assert.ok(await page.locator('#fontPanel').isVisible(), 'font panel opens');
  assert.ok((await page.locator('#fpInfo').textContent()).includes(trial === 'bahnc' ? 'preview only' : 'Open Font License'), 'licence shown');
  await page.locator('#sCmp').selectOption('rubik');
  await page.locator('#cSide').check();
  await page.waitForTimeout(200);
  assert.ok(await page.locator('#stage2').isVisible(), 'side by side');
  await page.locator('#fpClose').click();
  await page.locator('#stageWrap').screenshot({ path: path.join(previews, `compare-${trial}-vs-rubik-text-1.2x.png`) });
  await page.locator('#bFonts').click();
  await page.locator('#cSide').uncheck();
  await page.locator('#fpClose').click();
  await page.keyboard.press('f');
  assert.ok(await ev(() => QApp.S.flip), 'F flips to the compare font');
  await page.keyboard.press('f');
  assert.ok(!(await ev(() => QApp.S.flip)), 'F flips back');
  // upload a font file (one that is not embedded) through the file input
  const upFile = ['C:/Windows/Fonts/calibrib.ttf', 'C:/Windows/Fonts/arial.ttf'].find((p) => fs.existsSync(p));
  let upId = null;
  if (upFile) {
    await page.locator('#fFont').setInputFiles(upFile);
    await page.waitForFunction(() => QApp.S.font.id.startsWith('u-'), null, { timeout: 10000 });
    upId = await ev(() => QApp.S.font.id);
    const up = await ev((x) => { const e = QApp.fonts.get(x); return { name: e.name, lic: e.info.licence, k: e.sizeK }; }, upId);
    assert.ok(up.lic === 'preview' && up.k > 0.5 && up.k < 2, 'uploaded font parsed: ' + JSON.stringify(up));
  }
  // the spec names the font
  const fspec = await ev(() => QApp.spec());
  assert.ok(fspec.includes('**Font: ') && fspec.includes('## Font') && fspec.includes('tools/make_font_atlas.py'), 'spec has the font section');
  if (!upId && trial === 'bahnc') assert.ok(fspec.includes('preview-only licence'), 'spec says preview only');
  // autosave keeps the font and the upload across a reload
  await page.waitForTimeout(600);
  await page.reload();
  await page.waitForTimeout(300);
  if (upId) await page.waitForFunction((x) => !!QApp.fonts.get(x), upId, { timeout: 10000 });
  fstate = await ev(() => QApp.S.font);
  assert.strictEqual(fstate.id, upId || trial, 'font kept after reload');
  assert.strictEqual(fstate.mult, 1.2, 'size kept after reload');
  if (upId) {   // remove it again (confirm dialog accepted above)
    await page.locator('#bFonts').click();
    await page.locator('#upList button', { hasText: 'Remove' }).first().click();
    await page.locator('#dlgOk').click();   // (the in-page confirm)
    await page.waitForFunction((x) => !QApp.fonts.get(x), upId);
    assert.strictEqual(await ev(() => QApp.S.font.id), 'rubik', "back to the game's font after removing the upload");
    await page.locator('#fpClose').click();
  }
  // font sheet
  await page.locator('#sSize').selectOption('1');
  await page.locator('#sMinTs').selectOption('0');
  await page.locator('#pgSheet').click();
  await page.waitForTimeout(800);
  assert.ok(await page.locator('#sheetWrap').isVisible(), 'font sheet shown');
  const sheetH = await page.locator('#sheet').evaluate((c) => c.height);
  assert.ok(sheetH > 1000, 'font sheet drawn (' + sheetH + ' px)');
  const sheetPng = await page.locator('#sheet').evaluate((c) => c.toDataURL('image/png'));   // (the whole sheet, not just what is scrolled into view)
  fs.writeFileSync(path.join(previews, 'font-sheet.png'), Buffer.from(sheetPng.split(',')[1], 'base64'));
  await ev((u) => QApp.selectPage(u), settingsUid);
  assert.ok(await page.locator('#stage').isVisible(), 'back to the page');
  // tour flips pages by itself; a key stops it
  const tp = await ev(() => QApp.S.pageUid);
  await page.keyboard.press('t');
  await page.waitForTimeout(1700);
  assert.notStrictEqual(await ev(() => QApp.S.pageUid), tp, 'tour moved on');
  await page.keyboard.press('t');
  assert.strictEqual(await ev(() => QApp.S.tour), 0, 'tour stopped');

  // ---- title logo (test/apply-logo.js): import a layout carrying style "logo" and the Rubik font ----
  const QModel = require(path.join(__dirname, '..', 'src', 'model.js'));
  const applyLogo = require('./apply-logo.js');
  const lbase = QModel.normalizeDump(await ev(() => JSON.parse(document.getElementById('dumpData').textContent)));
  const ldoc = applyLogo(QModel, QModel.makeWorking(lbase));
  assert.ok(applyLogo.lastCount > 0, 'apply-logo found QUARK titles');
  const lfile = path.join(os.tmpdir(), 'quark-e2e-logo-layout.json');
  fs.writeFileSync(lfile, JSON.stringify({ format: 'quark-ui-layout', version: 1, doc: ldoc }));   // (no top-level font: doc.font carries it)
  await page.locator('#fImport').setInputFiles(lfile);
  await page.waitForFunction(() => QApp.S.font.id === 'rubik' && !!QApp.fonts.get('rubik').family, null, { timeout: 10000 });
  assert.ok([null, 700].includes(await ev(() => QApp.S.font.weight)), 'layout font: Rubik 700 (its own default weight)');
  await page.waitForTimeout(300);
  // count cyan / red / black pixels in the main menu's title, drawn 1:1
  const logoPixels = () => ev(() => {
    const main = QApp.S.doc.pages.find((p) => p.id === 'main');
    const t = main.elements.find((e) => e.kind === 'label' && e.label === 'QUARK');
    const c = document.createElement('canvas'); c.width = 1280; c.height = 720;
    const g = c.getContext('2d');
    QRender.drawPage(g, main, 1280, 720, null);
    const d = g.getImageData(Math.floor(t.x) - 10, Math.floor(t.y) - 6, Math.ceil(t.w) + 20, Math.ceil(t.h) + 12).data;
    const n = { cyan: 0, red: 0, black: 0 };
    for (let i = 0; i < d.length; i += 4) {
      const [r, gg, b] = [d[i], d[i + 1], d[i + 2]];
      if (r < 90 && gg > 190 && b > 230) n.cyan++;
      else if (r > 230 && gg < 100 && b < 90) n.red++;
      else if (r < 8 && gg < 8 && b < 8) n.black++;
    }
    return { n, style: t.style || '' };
  });
  let lp = await logoPixels();
  assert.strictEqual(lp.style, 'logo', 'main title has style logo');
  assert.ok(lp.n.red > 150 && lp.n.cyan > 600 && lp.n.black > 300, 'logo drawn: split Q + cyan word + black outline ' + JSON.stringify(lp.n));
  // the inspector: style dropdown normal / logo
  const mainUid = await ev(() => QApp.S.doc.pages.find((p) => p.id === 'main').uid);
  await ev((u) => QApp.selectPage(u), mainUid);
  await ev(() => { const p = QApp.S.doc.pages.find((x) => x.uid === QApp.S.pageUid); QApp.S.sel = new Set([p.elements.find((e) => e.label === 'QUARK').uid]); QApp.commit(() => {}); });
  const styleSel = page.locator('#inspector .row:has(> label:text-is("style")) select');
  assert.strictEqual(await styleSel.inputValue(), 'logo', 'inspector shows style logo');
  assert.deepStrictEqual(await styleSel.locator('option').allTextContents(), ['normal', 'logo'], 'style options');
  const baseLogo = await ev(() => QApp.baseline.pages.some((p) => p.elements.some((e) => e.style === 'logo')));   // (the game draws it since 1.18.13)
  let lspec = await ev(() => QApp.spec());
  assert.ok(baseLogo || lspec.includes('## Title logo (label style "logo")') && lspec.includes('#40D9FF') && lspec.includes('#FF4D40') && lspec.includes('9.09 deg') &&
    lspec.includes('Rubik, wght 800') || baseLogo, 'spec describes the logo');
  assert.ok(lspec.includes('**Font: Rubik, weight 700'), 'spec names the layout font');
  assert.ok(lspec.includes('style: (none) -> "logo"') || baseLogo, 'spec lists the style change');
  await styleSel.selectOption('');
  if (baseLogo) assert.ok((await ev(() => QApp.spec())).includes('style: "logo" -> (none)'), 'spec lists the style taken off the game\'s logo');
  lp = await logoPixels();
  assert.strictEqual(lp.style, '', 'style back to normal');
  assert.strictEqual(lp.n.red, 0, 'normal title has no red');
  await page.keyboard.press('Control+z');
  assert.strictEqual((await logoPixels()).style, 'logo', 'undo brings the logo back');
  assert.strictEqual(await ev(() => QApp.S.doc.font && QApp.S.doc.font.id), 'rubik', 'undo keeps the layout font');
  if (shots) await page.locator('#stage').screenshot({ path: path.join(shots, 'e2e-logo.png') });
  // the export carries the font in the layout too
  assert.ok(!(await ev(() => QApp.S.doc.pages.some((p) => p.prov))), 'import and undo leave no marks');
  await ev(() => { const p = QApp.S.doc.pages.find((x) => x.id === 'main'); QApp.commit(() => { p.notes = 'provenance test'; }); });
  await ev(() => { window.showDirectoryPicker = undefined; });   // (a browser without the File System Access API)
  const provDl = page.waitForEvent('download', { predicate: (d) => d.suggestedFilename() === 'provenance.json' });
  const [ldl] = await Promise.all([page.waitForEvent('download'), page.locator('#bExport').click()]);
  const lexp = JSON.parse(fs.readFileSync(await ldl.path(), 'utf8'));
  assert.deepStrictEqual(lexp.doc.font, { id: 'rubik', weight: 700 }, 'exported layout carries its font');
  assert.ok(lexp.doc.pages.some((p) => p.prov && p.prov.origin === 'human'), 'exported layout carries the marks');
  const prov2 = JSON.parse(fs.readFileSync(await (await provDl).path(), 'utf8'));
  const embedded = await ev(() => JSON.parse(document.getElementById('provenanceData').textContent || '{"items":{}}'));   // (merged with the build-time copy)
  assert.ok(Object.keys(embedded.items).every((k) => k in prov2.items), 'downloaded provenance keeps the build-time items');
  assert.deepStrictEqual([prov2.items['ui/main'].origin, prov2.items['ui/main'].note], ['human', 'designed by q in the ui-customizer'], 'edited page: human');
  const unmarked = Object.entries(prov2.items).find(([k, v]) => k !== 'ui/main' && v.note.includes('origin not recorded'));
  assert.ok(unmarked || baseLogo, 'imported unmarked change: human, origin not recorded');   // (with the game's own logo the import changes no other page)
  assert.ok((await page.locator('#stInfo').textContent()).includes('provenance.json downloaded'), 'status bar says where to put it');
  fs.unlinkSync(lfile);

  // ---- the proposal build: every page that differs from the game default is the AI's; an edit makes it human ----
  const redesignHtml = process.env.UIC_PROPOSAL_HTML || '';   // (a --layout build to check too, when given)
  let propInfo = 'no proposal build';
  if (fs.existsSync(redesignHtml)) {
    const pp = await ctx.newPage();
    pp.on('pageerror', (e) => errors.push(String(e)));
    await pp.goto('file:///' + redesignHtml.replace(/\\/g, '/'));
    await pp.waitForTimeout(300);
    const pr = await pp.evaluate(() => {
      const D = QModel.diff(QApp.baseline, QApp.S.doc);
      const changedPages = D.pages.filter((e) => !e.gone && e.status !== 'unchanged').map((e) => e.page);
      return { changed: changedPages.length, ai: QApp.S.doc.pages.filter((p) => p.prov && p.prov.origin === 'ai').length,
        unmarkedChanged: changedPages.filter((p) => !p.prov).length };
    });
    assert.ok(pr.changed > 0 && pr.ai === pr.changed && pr.unmarkedChanged === 0, 'proposal pages marked ai: ' + JSON.stringify(pr));
    const edited = await pp.evaluate(() => {
      const p = QApp.S.doc.pages.find((x) => x.prov && x.prov.origin === 'ai' && x.elements.length);
      QApp.selectPage(p.uid);
      QApp.commit(() => { p.elements[0].x += 3; });
      const before = JSON.stringify(p.prov);
      const items = QApp.provItems();
      QApp.undo();
      const after = QApp.S.doc.pages.find((x) => x.uid === p.uid).prov;
      return { before, item: items['ui/' + p.id], after };
    });
    assert.deepStrictEqual(JSON.parse(edited.before).origin, 'human', 'edit on the proposal: human');
    assert.strictEqual(JSON.parse(edited.before).from, 'ai', 'from the AI proposal');
    assert.ok(edited.item.note.includes('q edited the AI proposal'), 'item note: ' + edited.item.note);
    assert.deepStrictEqual(edited.after, { origin: 'ai' }, 'undo: back to ai');
    propInfo = `proposal: ${pr.ai} pages ai`;
    await pp.close();
  }

  await browser.close();
  if (errors.length) { console.error('console errors:\n' + errors.join('\n')); process.exit(1); }
  console.log(`e2e passed: ${nPages} pages, dragged/nudged/resized/renamed "${target.label}", cut+paste across pages, fonts (${nFonts}: switch, compare, upload, sheet, tour; widths px: ${vwidths}), logo style (import, pixels, inspector, spec, undo, export), provenance (marks, undo, folder write, download; ${propInfo}), spec ${md.split('\n').length} lines, no console errors`);
})().catch((e) => { console.error(e); process.exit(1); });
