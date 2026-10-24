// node test/fonts.test.js - the font parser (name table licence, OS/2 cap height, fvar axes) on the game's fonts, and
// on this PC's Windows fonts and downloaded open fonts (<Quark>/ui-customizer/fonts/ofl) when they are there; then the
// catalogue build.py wrote (UIC_DIR, default <Quark>/ui-customizer), when it exists.
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const F = require('../src/fonts.js');

const repoFonts = path.join(__dirname, '..', '..', '..', 'assets', 'fonts');
const uic = process.env.UIC_DIR || path.join(__dirname, '..', '..', '..', '..', 'ui-customizer');
const parse = (p) => F.parseSync(fs.readFileSync(p));

const rubik = parse(path.join(repoFonts, 'Rubik.ttf'));
assert.strictEqual(rubik.kind, 'ttf');
assert.strictEqual(rubik.family, 'Rubik');
assert.strictEqual(rubik.licence, 'open', 'Rubik is OFL');
assert.strictEqual(rubik.capRatio, 0.7, "Rubik's cap height (the reference every trial font is sized to)");
assert.ok(rubik.axes.some((a) => a.tag === 'wght' && a.min === 300 && a.max === 900), 'Rubik wght axis');
const ws = parse(path.join(repoFonts, 'WorkSans.ttf'));
assert.strictEqual(ws.family, 'Work Sans');
assert.strictEqual(ws.capRatio, 0.66);
assert.ok(ws.axes.some((a) => a.tag === 'wght' && a.min === 100 && a.max === 900), 'Work Sans wght axis');
let n = 2;
for (const f of ['Jost.ttf', 'BarlowCondensed-Bold.ttf', 'BebasNeue-Regular.ttf', 'Decalotype-Bold.ttf', 'LilitaOne-Regular.ttf']) {
  const fi = parse(path.join(repoFonts, f));
  assert.strictEqual(fi.licence, 'open', f + ' is OFL');
  n++;
}
assert.strictEqual(F.licenceOf('Microsoft supplied font. You may use this font...', '').status, 'preview');
assert.strictEqual(F.licenceOf('', '').status, 'unknown');
assert.strictEqual(F.kindOf(Buffer.from('wOF2xxxxxxxxxxxx')), 'woff2');
assert.strictEqual(F.kindOf(Buffer.from('not a font at all')), null);
const win = 'C:/Windows/Fonts/bahnschrift.ttf';
if (fs.existsSync(win)) {
  const bi = parse(win);
  assert.strictEqual(bi.licence, 'preview');
  assert.ok(bi.axes.some((a) => a.tag === 'wdth' && a.min === 75), 'Bahnschrift wdth axis');
  n++;
}
// the open fonts downloaded from google/fonts (<Quark>/ui-customizer/fonts/ofl/<family>/): OFL in the name table, OFL.txt next to them
const ofl = path.join(uic, 'fonts', 'ofl');
const want = { 'fredoka/Fredoka[wdth,wght].ttf': ['wght', 'wdth'], 'sairasemicondensed/SairaSemiCondensed-Bold.ttf': [],
  'signika/Signika[GRAD,wght].ttf': ['wght'], 'asap/Asap[wdth,wght].ttf': ['wght', 'wdth'], 'nunito/Nunito[wght].ttf': ['wght'],
  'mplusrounded1c/MPLUSRounded1c-Bold.ttf': [], 'kanit/Kanit-Bold.ttf': [], 'archivonarrow/ArchivoNarrow[wght].ttf': ['wght'] };
for (const [f, axes] of Object.entries(want)) {
  const p = path.join(ofl, f);
  if (!fs.existsSync(p)) continue;
  assert.ok(fs.existsSync(path.join(path.dirname(p), 'OFL.txt')), f + ': OFL.txt next to it');
  const fi = parse(p);
  assert.strictEqual(fi.kind, 'ttf', f);
  assert.strictEqual(fi.licence, 'open', f + ' is OFL');
  if (fi.capRatio !== undefined) assert.ok(fi.capRatio > 0.6 && fi.capRatio < 0.8, f + ' cap height ' + fi.capRatio);   // (M+ has an old OS/2: build.py reads its H glyph)
  for (const t of axes) assert.ok(fi.axes.some((a) => a.tag === t), f + ' has a ' + t + ' axis');
  n++;
}
// canvas fontStretch takes keywords only: a percentage width is drawn from a pinned FontFace instead
assert.strictEqual(F.canvasStretch('condensed'), 'condensed');
assert.strictEqual(F.canvasStretch('85%'), '');
assert.strictEqual(F.canvasStretch(''), '');
// the catalogue build.py wrote: the game's font first (built in, the reference), the game's other fonts, sizes matched
const catFile = path.join(uic, 'fonts', 'catalog.json');
let catInfo = 'no catalogue (build first)';
if (fs.existsSync(catFile)) {
  const cat = JSON.parse(fs.readFileSync(catFile, 'utf8'));
  assert.strictEqual(cat.fonts[0].id, 'rubik', "the game's font first");
  assert.ok(cat.files[cat.fonts[0].file].builtin, 'Rubik is the embedded UI font');
  assert.strictEqual(cat.refCapRatio, 0.7, "sizes are matched to Rubik's capitals");
  for (const id of ['worksans', 'jost', 'barlowcondensed', 'bebasneue', 'decalotype', 'lilitaone']) {
    const e = cat.fonts.find((x) => x.id === id);
    assert.ok(e, id + ' in the catalogue');
    for (const k of e.files || [e.file]) assert.strictEqual(cat.files[k].licence, 'open', id + ' can ship');
    assert.ok(e.sizeK > 0.7 && e.sizeK < 1.4, id + ' size fix ' + e.sizeK);
  }
  assert.ok(Math.abs(cat.fonts.find((x) => x.id === 'worksans').sizeK - 0.7 / 0.66) < 0.001, 'Work Sans sized to Rubik');
  for (const e of cat.fonts) for (const k of e.files || [e.file]) {
    const p = String(cat.files[k].path || '');
    if (cat.files[k].licence === 'preview') assert.ok(!/assets\/fonts/.test(p), e.id + ': a preview-only font is never in the repo');
  }
  catInfo = cat.fonts.length + ' fonts in the catalogue';
}
console.log(`font tests passed (${n} fonts parsed, ${catInfo})`);
