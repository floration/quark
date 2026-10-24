// node test/provenance.test.js - page origin marks (human / ai) and the merge into provenance.json.
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const M = require('../src/model.js');

const dump = JSON.parse(fs.readFileSync(path.join(__dirname, 'sample-dump.json'), 'utf8'));
const base = M.normalizeDump(dump);
const page = (doc, id) => doc.pages.find((p) => p.id === id);

// ---- a plain layout: nothing changed, nothing to record
let doc = M.makeWorking(base);
assert.deepStrictEqual(M.provenanceItems(base, doc), {}, 'unchanged layout: no items');

// ---- the proposal: pages that differ become 'ai'; unchanged ones get no mark
const main = page(doc, 'main'), video = page(doc, 'settings/video');
main.elements[0].y += 20;
video.title = 'Video settings';
const np = M.newPage('settings/accessibility', 'Accessibility');
doc.pages.push(np);
assert.strictEqual(M.markAi(base, doc), 3, 'three pages differ');
assert.deepStrictEqual(main.prov, { origin: 'ai' });
assert.deepStrictEqual(np.prov, { origin: 'ai' });
assert.strictEqual(page(doc, 'settings/controls').prov, undefined, 'unchanged page: no mark');
assert.strictEqual(M.markAi(base, doc), 0, 'marks once');

// ---- the owner edits two pages: one on top of the AI proposal, one that was the game default
const controls = page(doc, 'settings/controls');
M.markHuman(video, '2026-10-24');
controls.elements[0].x += 5;
M.markHuman(controls, '2026-10-24');
assert.deepStrictEqual(video.prov, { origin: 'human', from: 'ai', date: '2026-10-24' }, 'human edit of the AI proposal');
assert.deepStrictEqual(controls.prov, { origin: 'human', date: '2026-10-24' }, 'human edit of a default page');
M.markHuman(video, '2026-10-25');
assert.deepStrictEqual(video.prov, { origin: 'human', from: 'ai', date: '2026-10-25' }, 'a later edit keeps "from ai", new date');

// marks survive saving (sanitizeWorking); junk marks are dropped
const saved = M.sanitizeWorking(JSON.parse(JSON.stringify(doc)));
assert.deepStrictEqual(page(saved, 'settings/video').prov, video.prov, 'mark kept through save / load');
const junk = M.sanitizeWorking({ pages: [{ id: 'a', prov: { origin: 'robot' } }, { id: 'b', prov: { origin: 'ai', date: 'soon', x: 1 } }] });
assert.strictEqual(junk.pages[0].prov, undefined, 'unknown origin dropped');
assert.deepStrictEqual(junk.pages[1].prov, { origin: 'ai' }, 'bad date and extra fields dropped');

// ---- items: changed pages only, origin from the mark
const items = M.provenanceItems(base, doc, { date: '2026-10-26', aiNote: 'AI redesign (test)' });
assert.deepStrictEqual(Object.keys(items).sort(), ['ui/main', 'ui/settings/accessibility', 'ui/settings/controls', 'ui/settings/video']);
assert.deepStrictEqual(items['ui/main'], { origin: 'ai', by: 'q', date: '2026-10-26', note: 'AI redesign (test)' });
assert.strictEqual(items['ui/settings/video'].origin, 'human');
assert.strictEqual(items['ui/settings/video'].date, '2026-10-25', 'date of the last human edit');
assert.ok(items['ui/settings/video'].note.includes('q edited the AI proposal'), 'note says it started from the AI proposal');
assert.strictEqual(items['ui/settings/controls'].note, 'designed by q in the ui-customizer');
for (const v of Object.values(items)) assert.deepStrictEqual(Object.keys(v), ['origin', 'by', 'date', 'note'], 'item fields');

// a changed page without a mark: the build's default origin
delete main.prov;
assert.strictEqual(M.provenanceItems(base, doc, {})['ui/main'].origin, 'human', 'plain build: unmarked = human (origin not recorded)');
assert.ok(M.provenanceItems(base, doc, {})['ui/main'].note.includes('origin not recorded'));
assert.strictEqual(M.provenanceItems(base, doc, { defaultOrigin: 'ai' })['ui/main'].origin, 'ai', 'proposal build: unmarked = ai');
main.prov = { origin: 'ai' };

// a page gone from the layout altogether is listed too
const gone = M.sanitizeWorking(JSON.parse(JSON.stringify(doc)));
gone.pages = gone.pages.filter((p) => p.id !== 'ingame/pause');
assert.ok(M.provenanceItems(base, gone, { defaultOrigin: 'ai' })['ui/pause'], 'removed page listed (under its registry id)');

// ---- merge: other items are kept, 'ai' over 'ai' keeps its approval note, everything else is replaced
const existing = {
  format: 1, tool: 'ui-customizer', items: {
    'ui/other-page': { origin: 'human', by: 'q', date: '2026-01-01', note: 'from another layout' },
    'ui/main': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'AI redesign, approved by q 2026-10-23' },
    'ui/settings/video': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'AI redesign, approved by q 2026-10-23' },
  },
};
const r = M.mergeProvenance(existing, items);
assert.deepStrictEqual(Object.keys(r.data), ['format', 'tool', 'items'], 'top-level format');
assert.strictEqual(r.data.format, 1);
assert.strictEqual(r.data.tool, 'ui-customizer');
assert.deepStrictEqual(r.data.items['ui/other-page'], existing.items['ui/other-page'], 'other item kept');
assert.deepStrictEqual(r.data.items['ui/main'], existing.items['ui/main'], 'ai over ai: approval note kept');
assert.strictEqual(r.data.items['ui/settings/video'].origin, 'human', 'ai -> human replaced');
assert.ok(r.data.items['ui/settings/accessibility'] && r.data.items['ui/settings/controls'], 'new items added');
assert.deepStrictEqual([r.added, r.updated, r.kept, r.other], [2, 1, 1, 1], 'counts');
assert.deepStrictEqual(Object.keys(r.data.items), Object.keys(r.data.items).slice().sort(), 'keys sorted');
assert.strictEqual(existing.items['ui/settings/video'].origin, 'ai', 'input not modified');
// human over human: replaced (the newer edit date); same content: kept
const r2 = M.mergeProvenance(r.data, items);
assert.deepStrictEqual([r2.added, r2.updated, r2.kept], [0, 0, 4], 'merging again changes nothing');
// from nothing
const r3 = M.mergeProvenance(null, items);
assert.strictEqual(Object.keys(r3.data.items).length, 4);

// ---- registry ids (the editor dashboard's assets/provenance/ui.json): dump paths and " [state]" variants mapped
const RK = {
  'ui/main': 'ui/main', 'ui/editor [sel:box]': 'ui/editor', 'ui/editor-online': 'ui/editor',
  'ui/editor-online [sel:box]': 'ui/editor', 'ui/editor-online/groups': 'ui/editor/groups',
  'ui/editor-online/keep-edits [keep:1]': 'ui/editor/keep-edits', 'ui/editor/export-ask [sel:box]': 'ui/editor/export-ask',
  'ui/info/controls [scroll 2]': 'ui/info/controls', 'ui/ingame/pause/settings': 'ui/settings',
  'ui/ingame/pause/settings/hud/widgets/files': 'ui/settings/hud/widgets/files', 'ui/ingame/pause': 'ui/pause',
  'ui/ingame/pause [demo]': 'ui/pause', 'ui/ingame/pause/queue': 'ui/pause/queue',
  'ui/ingame/pause/vote [host-ranked]': 'ui/pause/vote', 'ui/host/picker:maps': 'ui/picker/maps',
  'ui/vsbots/picker:variants': 'ui/picker/variants', 'ui/ingame/pause/picker:livemap [client-spectator]': 'ui/picker/livemap',
  'ui/host/picker/modes': 'ui/picker/modes', 'ui/ingame/console': 'ui/console',
  'ui/editor-online/chat': 'ui/hud/chat', 'ui/editor/chat': 'ui/hud/chat', 'ui/editor-online/chat [x]': 'ui/hud/chat','ui/ingame/chat': 'ui/hud/chat',
  'ui/ingame/scores [hide-rps]': 'ui/hud/scoreboard', 'ui/ingame/perf': 'ui/hud/perf-overlay',
  'ui/ingame/perf/perfmini': 'ui/hud/perf-overlay', 'ui/ingame': 'ui/hud/widgets', 'ui/ingame [host-vs]': 'ui/hud/widgets',
  // pages the registry lacks keep their ids
  'ui/custom/dash': 'ui/custom/dash', 'ui/host/advanced': 'ui/host/advanced', 'ui/host': 'ui/host',
  'ui/settings/controls/movement': 'ui/settings/controls/movement', 'ui/ingame/pause/settings/gameplay': 'ui/settings/gameplay',
  'ui/settings/video': 'ui/settings/video', 'ui/ingamex': 'ui/ingamex', 'ui/editor-onlinex': 'ui/editor-onlinex',
};
for (const [k, v] of Object.entries(RK)) assert.strictEqual(M.registryKey(k), v, k);
assert.strictEqual(M.registryKey(M.registryKey('ui/ingame/pause/settings/video [x]')), 'ui/settings/video', 'idempotent');

// merging variants: any human -> human, latest date, distinct notes joined; input untouched
const variants = {
  'ui/ingame/scores': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'AI redesign; approved' },
  'ui/ingame/scores [demo]': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'AI redesign; approved' },
  'ui/ingame/scores [hide-rps]': { origin: 'human', by: 'q', date: '2026-10-25', note: 'designed by q in the ui-customizer' },
  'ui/ingame/scores [host-vs]': { origin: 'ai', by: 'q', date: '2026-10-24', note: 'AI redesign; approved' },
  'ui/ingame/perf': { origin: 'ai', by: 'q', date: '2026-10-20', note: 'a' },
  'ui/ingame/perf/perfmini': { origin: 'ai', by: 'q', date: '2026-10-22', note: 'b' },
  'ui/settings/visuals': { origin: 'ai', by: 'q', date: '2026-10-22', note: 'c' },
};
const variantsCopy = JSON.parse(JSON.stringify(variants));
const mv = M.mapProvenanceItems(variants);
assert.deepStrictEqual(Object.keys(mv), ['ui/hud/perf-overlay', 'ui/hud/scoreboard', 'ui/settings/visuals'], 'merged, sorted');
assert.deepStrictEqual(mv['ui/hud/scoreboard'], { origin: 'human', by: 'q', date: '2026-10-25', note: 'AI redesign; approved | designed by q in the ui-customizer' });
assert.deepStrictEqual(mv['ui/hud/perf-overlay'], { origin: 'ai', by: 'q', date: '2026-10-22', note: 'a | b' }, 'all ai: ai, latest date');
assert.deepStrictEqual(Object.keys(mv['ui/hud/scoreboard']), ['origin', 'by', 'date', 'note'], 'field order kept');
assert.deepStrictEqual(variants, variantsCopy, 'input not modified');
assert.deepStrictEqual(M.mapProvenanceItems(mv), mv, 'mapping mapped items changes nothing');

// items of a layout come out under registry ids: a dump page like ingame/pause/settings/video -> ui/settings/video
let d2 = M.makeWorking(base);
const pausePage = page(d2, 'ingame/pause');
if (pausePage) {
  pausePage.title = (pausePage.title || '') + ' x';
  const it = M.provenanceItems(base, d2, { date: '2026-10-26', defaultOrigin: 'ai' });
  assert.ok(it['ui/pause'] && !it['ui/ingame/pause'], 'ingame/pause exported as ui/pause');
}

// merge drops old ids that map to another registry id (folding them in), keeps ids that are already registry ids
const oldFile = { format: 1, tool: 'ui-customizer', items: {
  'ui/ingame/console': { origin: 'human', by: 'q', date: '2026-10-01', note: 'designed by q in the ui-customizer' },
  'ui/editor-online [sel:box]': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'AI old' },
  'ui/custom/dash': { origin: 'ai', by: 'q', date: '2026-10-23', note: 'AI old' },
} };
const rm = M.mergeProvenance(oldFile, { 'ui/editor': { origin: 'ai', by: 'q', date: '2026-10-26', note: 'AI new' } });
assert.deepStrictEqual(Object.keys(rm.data.items), ['ui/console', 'ui/custom/dash', 'ui/editor'], 'old dump ids gone, folded into registry ids');
assert.strictEqual(rm.data.items['ui/console'].origin, 'human', 'a human mark survives the rename');
assert.strictEqual(rm.data.items['ui/editor'].note, 'AI old', 'ai over ai: the old approval note kept');
assert.deepStrictEqual(M.mergeProvenance(null, { 'ui/ingame/chat [x]': { origin: 'ai', by: 'q', date: '2026-10-26', note: 'n' } }).data.items,
  { 'ui/hud/chat': { origin: 'ai', by: 'q', date: '2026-10-26', note: 'n' } }, 'new items mapped too');

// ---- reading provenance.json
assert.strictEqual(M.parseProvenance(''), null, 'empty file');
assert.strictEqual(M.parseProvenance('  \n'), null, 'blank file');
assert.deepStrictEqual(M.parseProvenance(JSON.stringify(existing)), existing);
assert.throws(() => M.parseProvenance('{oops'), 'bad JSON is not overwritten');
assert.throws(() => M.parseProvenance('{"items":[]}'), /expected format/);
assert.throws(() => M.parseProvenance('[]'), /expected format/);

// ---- the shipped ui-customizer/provenance.json is valid and in the exact format
const shipped = path.join(__dirname, '..', 'provenance.json');
if (fs.existsSync(shipped)) {
  const d = M.parseProvenance(fs.readFileSync(shipped, 'utf8'));
  assert.deepStrictEqual(Object.keys(d), ['format', 'tool', 'items']);
  assert.strictEqual(d.format, 1);
  assert.strictEqual(d.tool, 'ui-customizer');
  for (const [k, v] of Object.entries(d.items)) {
    assert.ok(k.startsWith('ui/'), k);
    assert.deepStrictEqual(Object.keys(v), ['origin', 'by', 'date', 'note'], k);
    assert.strictEqual(M.registryKey(k), k, 'shipped id is a registry id: ' + k);
    assert.ok(['human', 'ai'].includes(v.origin) && /^\d{4}-\d\d-\d\d$/.test(v.date) && typeof v.by === 'string' && typeof v.note === 'string', k);
  }
}

console.log('provenance tests passed');
