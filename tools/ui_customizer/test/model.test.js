// node test/model.test.js - checks the diff and the spec on scripted edits of the sample dump.
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const M = require('../src/model.js');

const dump = JSON.parse(fs.readFileSync(path.join(__dirname, 'sample-dump.json'), 'utf8'));
const base = M.normalizeDump(dump);
const doc = M.makeWorking(base);
const page = (id) => doc.pages.find((p) => p.id === id);
const find = (p, label) => p.elements.find((e) => e.label === label);

// untouched: no changes at all
let D = M.diff(base, doc);
assert.deepStrictEqual(Object.values(D.counts).reduce((a, b) => a + b, 0), 0, 'fresh layout has no changes');
assert.ok(M.specMarkdown(base, doc).markdown.includes('No changes: the layout is the default.'));

// tolerant of junk
const junk = M.normalizeDump({ pages: [{ elements: [null, { kind: 'BUTTON', x: '5' }] }, null] });
assert.strictEqual(junk.pages.length, 2);
assert.strictEqual(junk.pages[0].elements[1].kind, 'button');
assert.strictEqual(junk.pages[0].elements[1].x, 5);
assert.strictEqual(M.normalizeDump(null).pages.length, 0);

// scripted edits
const main = page('main'), video = page('settings/video'), pause = page('ingame/pause');
const host = find(main, 'HOST GAME');
host.y += 44; host.w = 260;                          // moved + resized
find(main, 'QUIT').label = 'EXIT';                   // renamed
const demos = find(main, 'DEMOS');
main.elements = main.elements.filter((e) => e !== demos);   // removed
const fov = find(video, 'FIELD OF VIEW');
fov.max = 140; fov.notes = 'Allow up to 140,\nsome players want it';   // edited + note
const vsync = find(video, 'VSYNC');                  // moved between pages
video.elements = video.elements.filter((e) => e !== vsync);
pause.elements.push(vsync);
vsync.x = 100;
const res = find(video, 'RESOLUTION');
res.options = res.options.concat(['3840 X 2160']);    // options edited
const added = M.newElement('colorpicker', 640, 600); added.label = 'CROSSHAIR | COLOUR';
video.elements.push(added);                          // added (with a pipe to escape in tables)
const copy = M.copyElement(host, 0, 50);
main.elements.push(copy);                            // added copy
const np = M.newPage('settings/accessibility', 'Accessibility');
np.elements.push(M.newElement('toggle', 640, 200));
doc.pages.push(np);                                  // page added
page('settings/controls').hidden = true;             // page removed
video.title = 'Video settings';                      // page renamed

D = M.diff(base, doc);
const c = D.counts;
assert.strictEqual(c.pagesAdded, 1);
assert.strictEqual(c.pagesRemoved, 1);
assert.strictEqual(c.pagesRenamed, 1);
assert.strictEqual(c.moved, 2, 'HOST GAME and VSYNC moved');
assert.strictEqual(c.resized, 1);
assert.strictEqual(c.renamed, 1);
assert.strictEqual(c.removed, 1);
assert.strictEqual(c.movedBetweenPages, 1);
assert.strictEqual(c.edited, 2, 'FOV max, RESOLUTION options');
assert.strictEqual(c.added, 3, 'colour picker, copy, toggle on the new page');
assert.strictEqual(c.notes, 1);
assert.strictEqual(c.reordered, 0);

const row = D.byUid.get(vsync.uid);
assert.ok(row.flags[0] === 'moved from settings/video' && row.flags.includes('moved'));
assert.strictEqual(D.pages.find((e) => e.page === video).movedOut.length, 1);

const { markdown: md, fileName } = M.specMarkdown(base, doc, { date: new Date(2026, 9, 23, 14, 5), dumpName: 'sample-dump.json' });
assert.strictEqual(fileName, 'ui-spec-2026-10-23-1405.md');
fs.writeFileSync(path.join(require('os').tmpdir(), 'quark-ui-last-spec.md'), md);
const has = (s) => assert.ok(md.includes(s), 'spec should contain: ' + s);
has('commit `sample000`');
has('Generated: 2026-10-23 14:05');
has('| widgets moved | 2 |');
has('| widgets moved between pages | 1 |');
has('**moved, resized** button "HOST GAME" (orig `main#3`, action `nav:host`)');
has('moved: (520, 296) -> (520, 340)  [dx +0, dy +44]');
has('resized: 240x36 -> 260x36');
has('label: "QUIT" -> "EXIT"');
has('**removed** button "DEMOS" (orig `main#6`, action `nav:demos`): was at (520, 434) size 240x36');
has('max: 130 -> 140');
has('note: Allow up to 140, / some players want it');
has('**moved here from page `settings/video`** toggle "VSYNC"');
has('page: `settings/video` -> `ingame/pause` (moved)');
has('**moved to page `ingame/pause`** toggle "VSYNC"');
has('**added** colorpicker "CROSSHAIR | COLOUR"');
has('copy of `main#3`');
has('### `settings/accessibility` (Accessibility) - NEW PAGE');
has('### `settings/controls` (Settings: controls) - REMOVE PAGE');
has('Page title: "Settings: video" -> "Video settings"');
has('| colorpicker | CROSSHAIR \\| COLOUR |');
has('| toggle | VSYNC | 100 | 220 | 600 | 32 | cfg:vsync | moved from settings/video, moved |');
has('| button | DEMOS | 520 | 434 | 240 | 36 | nav:demos | removed | main#6 |');
// changes come before the layout tables (every page here is changed, so the default lists them all)
assert.ok(md.indexOf('## Changes') < md.indexOf('## Layout of the changed pages'));
assert.ok(!md.includes('## Full layout'), 'the default spec has no full layout');

// spec layout tables: changed pages (default), every page, none
{
  const d2 = M.makeWorking(base);
  d2.pages.find((p) => p.id === 'main').elements.find((e) => e.label === 'QUIT').x += 10;
  const changedOnly = M.specMarkdown(base, d2).markdown, all = M.specMarkdown(base, d2, { layout: 'all' }), none = M.specMarkdown(base, d2, { layout: 'none' }).markdown;
  const tableOf = (m, id) => m.includes('### `' + id + '`') && m.split('### `' + id + '`').length - 1;
  assert.strictEqual(tableOf(changedOnly, 'main'), 2, 'changed page: in the changes and in the tables');
  assert.ok(!tableOf(changedOnly, 'ingame/pause'), 'unchanged page left out by default');
  assert.ok(changedOnly.includes('## Layout of the changed pages') && !changedOnly.includes('## Full layout'));
  assert.ok(all.markdown.includes('## Full layout') && tableOf(all.markdown, 'ingame/pause') === 1, 'every page: unchanged ones too');
  assert.ok(/-full\.md$/.test(all.fileName), 'full spec file name: ' + all.fileName);
  assert.strictEqual(tableOf(none, 'main'), 1, 'none: only the changes');
  assert.ok(!none.includes('| # | kind |'), 'none: no widget tables');
  assert.ok(none.length < changedOnly.length && changedOnly.length < all.markdown.length);
  // a stale dump is called out at the top
  const stale = M.specMarkdown(base, d2, { freshness: { stale: true, why: 'the dump is 1.0.0, the game 1.0.1' } }).markdown;
  assert.ok(stale.indexOf('**Warning: made against an out-of-date UI dump (the dump is 1.0.0, the game 1.0.1).**') > 0 && stale.indexOf('Warning') < stale.indexOf('## Summary'));
  assert.ok(!changedOnly.includes('out-of-date'));
}

// autosave: unchanged pages stored as references to the dump, expanded on load (and dropped when the dump lost them)
{
  const d4 = M.makeWorking(base);
  d4.pages.find((p) => p.id === 'main').elements[0].x += 3;
  const ents = new Map(M.diff(base, d4).pages.map((e) => [e.page, e]));
  const saved = { pages: d4.pages.map((p) => JSON.parse(M.baseRefJson(ents.get(p), JSON.stringify(p)))) };
  assert.strictEqual(saved.pages.filter((p) => p.$base).length, d4.pages.length - 1, 'every unchanged page is a reference');
  assert.ok(Array.isArray(saved.pages.find((p) => p.id === 'main').elements), 'the changed page is stored whole');
  assert.ok(JSON.stringify(saved).length < JSON.stringify(d4).length / 2);
  const back = M.sanitizeWorking(M.expandBaseRefs(base, JSON.parse(JSON.stringify(saved))));
  assert.deepStrictEqual(back.pages.map((p) => p.uid), d4.pages.map((p) => p.uid), 'page uids kept');
  assert.strictEqual(M.diff(base, back).counts.moved, 1, 'the edit survives, nothing else changed');
  assert.strictEqual(M.diff(base, back).counts.pagesChanged, 1);
  // a page with a note or a provenance mark is stored whole even when its widgets are the game's
  const pause = d4.pages.find((p) => p.id === 'ingame/pause');
  pause.notes = 'keep this';
  assert.ok(!JSON.parse(M.baseRefJson(M.diff(base, d4).pages.find((e) => e.page === pause), '{"whole":1}')).$base);
  // a reference to a page the dump no longer has is dropped
  const gone = M.expandBaseRefs(base, { pages: [{ $base: 'nope#1', uid: 'p1' }, saved.pages[0]] });
  assert.strictEqual(gone.pages.length, 1);
}

// the diff's fast path (identical copies) agrees with the field by field compare
{
  const d3 = M.makeWorking(base), e = d3.pages[0].elements[0];
  const b = base.pages[0].elements[0];
  assert.deepStrictEqual(M.elementChanges(b, e), { flags: [], changes: [] });
  e.x += 0.001;   // (within the compare's tolerance: still unchanged)
  assert.deepStrictEqual(M.elementChanges(b, e).flags, []);
  e.color = [1, 0, 0, 1];
  assert.deepStrictEqual(M.elementChanges(b, e).flags, ['edited']);
}

// cut + paste to another page keeps identity; a second paste is a copy
const back = find(video, 'BACK');
video.elements = video.elements.filter((e) => e !== back);
pause.elements.push(M.clone(back));
D = M.diff(base, doc);
assert.strictEqual(D.counts.movedBetweenPages, 2);
pause.elements.push(M.clone(back));   // the same origId twice: the second one counts as added
D = M.diff(base, doc);
assert.strictEqual(D.counts.added, 4);

// draw order
const v2 = M.makeWorking(base), vp = v2.pages[1];
vp.elements.push(vp.elements.shift());
assert.strictEqual(M.diff(base, v2).counts.reordered, 1);

// a saved layout survives sanitizing, and a broken one is refused
const round = M.sanitizeWorking(JSON.parse(JSON.stringify(doc)));
assert.strictEqual(M.diff(base, round).counts.moved, D.counts.moved);
assert.throws(() => M.sanitizeWorking({}));
// an anchor set in the inspector is a change, and the spec gives the expression
{
  const d3 = M.makeWorking(base), b3 = d3.pages.find((p) => p.id === 'main').elements.find((e) => e.kind === 'button');
  b3.anchor = 'right';
  const md3 = M.specMarkdown(base, d3).markdown;
  assert.ok(md3.includes('anchor: right edge (x = screenW - (1280 - x720) * s)'), 'the spec states a set anchor');
}
assert.throws(() => M.sanitizeWorking({ pages: [] }), /no pages/, 'an empty layout is refused (an import would wipe every page)');
assert.throws(() => M.sanitizeWorking({ pages: [null, 3] }), /no pages/, 'a layout of non-pages is refused');

// the font trial: a headline at the top, a section with how to apply it
const fspec = M.specMarkdown(base, doc, { font: { name: 'Bahnschrift Condensed', family: 'Bahnschrift', fileName: 'bahnschrift.ttf', path: 'C:/Windows/Fonts/bahnschrift.ttf',
  weight: 600, stretch: 'condensed', sizeK: 0.93, mult: 1.1, minTs: 2, letterSpacing: 0.02, upper: false, licence: 'preview',
  licenceSummary: 'Microsoft-licensed Windows font: preview only', axes: [{ tag: 'wght', min: 300, max: 700 }], capRatio: 0.71, capFrom: 'OS/2 sCapHeight' } }).markdown;
assert.ok(fspec.split(String.fromCharCode(10))[2].startsWith('**Font: Bahnschrift Condensed, weight 600, width condensed, size multiplier 1.1, minimum text scale 2'), fspec.split(String.fromCharCode(10))[2]);
assert.ok(fspec.includes('preview-only licence') && fspec.includes('## Font') && fspec.includes('tools/make_font_atlas.py') && fspec.includes('Do NOT ship'));
assert.ok(M.specMarkdown(base, doc, { font: { builtin: true, weight: 700, mult: 1 } }).markdown.includes('no font change'));
assert.ok(!M.specMarkdown(base, doc, { font: { builtin: true, weight: 700, mult: 1 } }).markdown.includes('## Font'));

// the layout's own font (doc.font) and the logo style (test/apply-logo.js)
const applyLogo = require('./apply-logo.js');
const ld = M.makeWorking(base);
assert.strictEqual(M.layoutFont(ld), null, 'no layout font by default');
ld.pages[0].elements.push(Object.assign(M.newElement('label', 640, 100), { label: 'QUARK', textScale: 8, align: 'center' }));
applyLogo(M, ld);
assert.ok(applyLogo.lastCount >= 1);
assert.deepStrictEqual(M.layoutFont(M.sanitizeWorking(JSON.parse(JSON.stringify(ld)))), { id: 'rubik', weight: 700 }, 'layout font survives saving');
assert.strictEqual(M.sanitizeWorking({ font: 'x', pages: [{ id: 'p' }] }).font, undefined, 'a broken layout font is dropped');
const lspec = M.specMarkdown(base, ld).markdown;
assert.ok(lspec.includes('**Font: Rubik, weight 700') && lspec.includes('## Font'), 'the layout font is in the spec');
assert.ok(M.specMarkdown(base, ld, { font: { builtin: true, weight: 700, mult: 1 } }).markdown.includes('**Font: Rubik'), 'the layout font wins over the default');
assert.ok(lspec.includes('## Title logo (label style "logo")') && /\*\*1\.21 px at 720p\*\* for the title/.test(lspec) && lspec.includes('9.09 deg') && lspec.includes('textured quad'), 'logo section');
assert.ok(!M.specMarkdown(base, M.makeWorking(base)).markdown.includes('Title logo'), 'no logo section without a logo');

console.log('model tests passed (' + md.split('\n').length + ' spec lines, written to the temp folder as quark-ui-last-spec.md)');
