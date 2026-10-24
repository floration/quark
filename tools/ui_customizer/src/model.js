// Quark UI customizer - data model: the default layout (from the dump), the working layout, the diff between
// them and the Markdown spec. No DOM here, so node can test it (test/model.test.js).
'use strict';

const QModel = (() => {
  // Widget kinds the game has (src/client/ui.h; popup: an open colour picker / dropdown list, drawn by Ui::popups), plus
  // the ones the customizer could propose that the game doesn't have (none at the moment).
  const GAME_KINDS = ['button', 'tab', 'toggle', 'cycle', 'slider', 'text', 'colorpicker', 'dropdown', 'key', 'handle', 'view', 'custom',
    'label', 'panel', 'rect', 'popup'];
  const NEW_KINDS = [];
  const KINDS = GAME_KINDS.concat(NEW_KINDS);

  // Fields an element may carry (others from the dump are kept as they are and compared too).
  const GEOM = ['x', 'y', 'w', 'h'];
  // Compared for "edited" (label has its own "renamed" status, geometry its own "moved"/"resized").
  const PROP_FIELDS = ['kind', 'value', 'options', 'min', 'max', 'def', 'fmt', 'maxLen', 'enabled', 'color', 'textScale',
    'align', 'style', 'action', 'id', 'where', 'scope', 'dynamic', 'current', 'sub', 'hint', 'maxW', 'enemy', 'open', 'pick'];
  // Bookkeeping fields of the working layout that are never part of the widget itself.
  const META = new Set(['uid', 'origId', 'notes', 'copyOf']);
  const PAGE_FIELDS = ['id', 'title', 'background', 'scenarios', 'inGame', 'screenshot'];

  let uidN = 0;
  const uid = (p) => p + Date.now().toString(36).slice(-4) + (++uidN).toString(36) + Math.random().toString(36).slice(2, 5);
  const clone = (o) => (o === undefined ? undefined : JSON.parse(JSON.stringify(o)));
  const num = (v, d = 0) => { const n = Number(v); return Number.isFinite(n) ? n : d; };

  // ---- default layout -----------------------------------------------------------------------------------------
  // The dump as a clean baseline: every page and element gets a stable origId (page: its id; element:
  // "<page id>#<index in the dump's draw order>"). Tolerant of missing fields.
  function normalizeDump(dump) {
    dump = dump && typeof dump === 'object' ? dump : {};
    const source = Object.assign({ repo: '?', commit: '?', width: 1280, height: 720 }, dump.source || {});
    const style = dump.style && typeof dump.style === 'object' ? clone(dump.style) : {};
    const seen = new Set();
    const pages = (Array.isArray(dump.pages) ? dump.pages : []).map((p, pi) => {
      p = p && typeof p === 'object' ? p : {};
      let pid = String(p.id || p.title || 'page' + pi);
      let orig = pid, k = 2;
      while (seen.has(orig)) orig = pid + '~' + k++;
      seen.add(orig);
      const page = {
        origId: orig, id: pid, title: String(p.title || pid),
        scenarios: Array.isArray(p.scenarios) ? p.scenarios.slice() : [],
        inGame: !!p.inGame, background: p.background == null ? '' : String(p.background),
        screenshot: p.screenshot || null,
      };
      for (const key of Object.keys(p)) if (!(key in page) && key !== 'elements') page[key] = clone(p[key]);
      page.elements = (Array.isArray(p.elements) ? p.elements : []).map((e, ei) => {
        e = e && typeof e === 'object' ? e : {};
        const el = clone(e);
        el.origId = orig + '#' + ei;
        el.kind = String(e.kind || 'custom').toLowerCase();
        for (const g of GEOM) el[g] = num(e[g]);
        if (el.label == null) el.label = '';
        el.label = String(el.label);
        delete el.uid; delete el.notes;
        return el;
      });
      return page;
    });
    return { source, style, pages };
  }

  // A working layout made from the baseline: the same data plus uids, empty notes, no hidden pages.
  function makeWorking(baseline) {
    return {
      pages: baseline.pages.map((p) => {
        const page = clone(p);
        page.uid = uid('p');
        page.hidden = false;
        page.notes = '';
        page.elements = page.elements.map((e) => Object.assign(e, { uid: uid('e'), notes: '' }));
        return page;
      }),
    };
  }

  // The browser autosave stores a page the layout leaves exactly as the game has it as {$base: <its origId>, uid} (most
  // of the 120 pages: the saved layout is a few KB, not 1.7 MB of copies, which file:// pages share one small storage
  // quota for). Loading puts the dump's page back in its place, so after a dump refresh those pages are the new
  // game's; a page the dump no longer has is dropped (nothing of it was the owner's). Other pages pass through.
  const isBaseRef = (p) => !!p && typeof p === 'object' && typeof p.$base === 'string' && !Array.isArray(p.elements);
  function expandBaseRefs(baseline, doc) {
    if (!doc || !Array.isArray(doc.pages) || !doc.pages.some(isBaseRef)) return doc;
    const fresh = new Map(makeWorking(baseline).pages.map((p) => [p.origId, p]));
    const pages = doc.pages.map((p) => {
      if (!isBaseRef(p)) return p;
      const b = fresh.get(p.$base);
      if (!b) return null;
      fresh.delete(p.$base);
      if (p.uid) b.uid = String(p.uid);
      return b;
    }).filter(Boolean);
    return Object.assign({}, doc, { pages });
  }
  // A page's autosave text: the reference when the diff says it is the game's (no notes, no provenance mark), else json.
  const baseRefJson = (entry, json) => (entry && entry.base && entry.status === 'unchanged' && !entry.page.prov && !entry.page.notes
    ? JSON.stringify({ $base: entry.base.origId, uid: entry.page.uid }) : json);

  // Repairs a layout read from storage or a file: every page and element gets a uid, the expected arrays exist.
  function sanitizeWorking(doc) {
    if (!doc || !Array.isArray(doc.pages)) throw new Error('not a layout (no "pages" array)');
    const uids = new Set();
    const fresh = (u, p) => { if (!u || uids.has(u)) u = uid(p); uids.add(u); return u; };
    if (doc.font !== undefined) { const f = layoutFont(doc); if (f) doc.font = f; else delete doc.font; }
    for (const p of doc.pages) {
      p.uid = fresh(p.uid, 'p');
      p.id = String(p.id || 'page');
      p.title = String(p.title == null ? p.id : p.title);
      p.hidden = !!p.hidden;
      p.notes = p.notes ? String(p.notes) : '';
      if (p.prov !== undefined) { const m = sanitizeProv(p.prov); if (m) p.prov = m; else delete p.prov; }
      if (p.origId === undefined) p.origId = null;
      if (!Array.isArray(p.elements)) p.elements = [];
      p.elements = p.elements.filter((e) => e && typeof e === 'object');
      for (const e of p.elements) {
        e.uid = fresh(e.uid, 'e');
        if (e.origId === undefined) e.origId = null;
        e.kind = String(e.kind || 'custom');
        for (const g of GEOM) e[g] = num(e[g]);
        e.label = e.label == null ? '' : String(e.label);
        e.notes = e.notes ? String(e.notes) : '';
      }
    }
    return doc;
  }

  // ---- the layout's font -------------------------------------------------------------------------------------------
  // A layout may carry the font it is meant to be drawn in: doc.font = {id, weight} (id = a font trial id of build.py's
  // FONT_TRIALS, e.g. "rubik"; weight null = the font's own default). Absent = the customizer's current font.
  function layoutFont(doc) {
    const f = doc && doc.font;
    if (!f || typeof f !== 'object' || typeof f.id !== 'string' || !f.id) return null;
    const w = Number(f.weight);
    return { id: f.id, weight: f.weight != null && Number.isFinite(w) && w >= 1 && w <= 1000 ? w : null };
  }
  function setLayoutFont(doc, id, weight) {
    if (!id) { delete doc.font; return doc; }
    doc.font = { id: String(id), weight: weight == null ? null : Number(weight) };
    return doc;
  }
  // What the spec says about a layout font when the caller passes none (or only the default Work Sans): the fonts
  // known here are the ones a layout script would pick; others are named by id.
  const KNOWN_FONTS = {
    rubik: { name: 'Rubik', family: 'Rubik', fileName: 'Rubik.ttf', path: 'assets/fonts/Rubik.ttf (in the game repo)', weight: 700,
      licence: 'open', licenceSummary: 'SIL Open Font License 1.1 (assets/fonts/Rubik-OFL.txt)', licenceUrl: 'https://openfontlicense.org',
      axes: [{ tag: 'wght', min: 300, default: 300, max: 900 }], capRatio: 0.7, capFrom: 'OS/2 sCapHeight', sizeK: 1 },
  };
  function layoutFontDesc(f) {
    const k = KNOWN_FONTS[f.id] || { name: f.id, fileName: f.id, licence: 'unknown', weight: 700, sizeK: 1 };
    return Object.assign({ builtin: false, uploaded: false, stretch: '', mult: 1, minTs: 0, letterSpacing: 0, upper: false }, clone(k),
      { weight: f.weight != null ? f.weight : k.weight });
  }

  // A short signature of a baseline (which dump a saved layout belongs to).
  function signature(baseline) {
    const s = JSON.stringify(baseline.pages.map((p) => [p.origId, p.elements.length, p.elements.map((e) => e.kind + e.label)]));
    let h = 5381;
    for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
    return (baseline.source.commit || '?') + ':' + (h >>> 0).toString(36);
  }

  // ---- new elements --------------------------------------------------------------------------------------------
  function defaultsFor(kind) {
    const d = {
      button: { w: 240, h: 36, label: 'NEW BUTTON', action: '' },
      tab: { w: 140, h: 36, label: 'NEW TAB', current: false, action: 'nav:' },
      popup: { w: 300, h: 120, label: '', popup: 'dropdown', options: ['CHOICE 1', 'CHOICE 2'], value: '0', rowH: 32 },
      toggle: { w: 300, h: 32, label: 'NEW TOGGLE', value: '0', action: 'cfg:' },
      cycle: { w: 300, h: 32, label: 'NEW CYCLE', options: ['OPTION A', 'OPTION B', 'OPTION C'], value: '0', action: 'cfg:' },
      slider: { w: 300, h: 32, label: 'NEW SLIDER', min: 0, max: 100, def: 50, value: '50', fmt: '%.0f', action: 'cfg:' },
      text: { w: 300, h: 32, label: 'FIELD', value: 'TEXT', maxLen: 32, action: 'local' },
      key: { w: 300, h: 32, label: 'NEW ACTION', value: 'KEY', action: 'bind:' },
      label: { w: 160, h: 16, label: 'NEW LABEL', textScale: 2, align: 'left' },
      panel: { w: 320, h: 240, label: '', color: [0.05, 0.06, 0.08, 0.75] },
      rect: { w: 100, h: 60, label: '', color: [1, 1, 1, 0.15] },
      handle: { w: 24, h: 24, label: 'HANDLE', action: '' },
      view: { w: 320, h: 180, label: 'VIEW', action: '' },
      custom: { w: 120, h: 36, label: 'CUSTOM', action: '' },
      colorpicker: { w: 300, h: 32, label: 'NEW COLOUR', color: [0.25, 0.85, 1, 1], action: 'cfg:' },
      dropdown: { w: 300, h: 32, label: 'NEW DROPDOWN', options: ['CHOICE 1', 'CHOICE 2', 'CHOICE 3'], value: '0', action: 'cfg:' },
    };
    return clone(d[kind] || d.custom);
  }
  function newElement(kind, x, y) {
    const e = Object.assign({ kind, x: 0, y: 0 }, defaultsFor(kind));
    e.x = Math.round(x - e.w / 2);
    e.y = Math.round(y - e.h / 2);
    e.uid = uid('e');
    e.origId = null;
    e.notes = '';
    return e;
  }
  // A copy is a new widget: no origId (it remembers what it was copied from, for the spec).
  function copyElement(e, dx = 0, dy = 0) {
    const c = clone(e);
    c.uid = uid('e');
    c.copyOf = e.origId || e.copyOf || null;
    c.origId = null;
    c.x += dx; c.y += dy;
    return c;
  }
  function newPage(id, title) {
    return { uid: uid('p'), origId: null, id, title: title || id, scenarios: [], inGame: false, background: 'menu backdrop',
      screenshot: null, hidden: false, notes: '', elements: [] };
  }

  // ---- diff ---------------------------------------------------------------------------------------------------
  const same = (a, b) => {
    if (typeof a === 'number' && typeof b === 'number') return Math.abs(a - b) < 0.005;
    if (a === b) return true;
    if ((a === undefined || a === null || a === '') && (b === undefined || b === null || b === '')) return true;
    return JSON.stringify(a) === JSON.stringify(b);
  };
  const comparedFields = (a, b) => {
    const keys = new Set(PROP_FIELDS);
    for (const o of [a, b]) for (const k of Object.keys(o)) if (!META.has(k) && !GEOM.includes(k) && k !== 'label') keys.add(k);
    return [...keys];
  };

  // The same fields with the same values (=== or equal arrays of plain values): the untouched copy most widgets are.
  // Cheap, so the diff (run after every edit) only compares field by field the few that differ.
  function identical(a, b) {
    let n = 0;
    for (const k in a) {
      if (META.has(k)) continue;
      n++;
      const x = a[k], y = b[k];
      if (x !== y && !(Array.isArray(x) && Array.isArray(y) && x.length === y.length && x.every((v, i) => v === y[i]))) return false;
    }
    for (const k in b) if (!META.has(k)) n--;
    return n === 0;
  }
  // What changed between one baseline element and its working copy: {flags, changes:[{field, old, new}]}
  function elementChanges(base, el) {
    if (identical(base, el)) return { flags: [], changes: [] };
    const flags = [], changes = [];
    if (!same(base.x, el.x) || !same(base.y, el.y)) { flags.push('moved'); changes.push({ field: 'position', old: [base.x, base.y], new: [el.x, el.y] }); }
    if (!same(base.w, el.w) || !same(base.h, el.h)) { flags.push('resized'); changes.push({ field: 'size', old: [base.w, base.h], new: [el.w, el.h] }); }
    if (!same(base.label, el.label)) { flags.push('renamed'); changes.push({ field: 'label', old: base.label, new: el.label }); }
    let edited = false;
    for (const f of comparedFields(base, el)) {
      if (!same(base[f], el[f])) { edited = true; changes.push({ field: f, old: base[f], new: el[f] }); }
    }
    if (edited) flags.push('edited');
    return { flags, changes };
  }

  // The whole diff. Returns {pages:[entry], counts, byUid} where entry =
  //   {page (working page, or the baseline page when it no longer exists), base (baseline page or null),
  //    status: 'added'|'removed'|'changed'|'unchanged', pageChanges:[{field,old,new}],
  //    rows:[{el, base, flags, changes, fromPage}], removed:[baseline element], movedOut:[{base, el, toPage}]}
  // byUid: element uid -> its row (for the inspector's status line).
  function diff(baseline, doc) {
    const basePage = new Map(), baseEl = new Map();
    for (const p of baseline.pages) { basePage.set(p.origId, p); for (const e of p.elements) baseEl.set(e.origId, { e, page: p }); }
    const placed = new Map();   // origId -> {el, page} (the first one wins; later duplicates count as added)
    for (const p of doc.pages) for (const e of p.elements) if (e.origId && baseEl.has(e.origId) && !placed.has(e.origId)) placed.set(e.origId, { el: e, page: p });

    const counts = { pagesAdded: 0, pagesRemoved: 0, pagesChanged: 0, pagesRenamed: 0, moved: 0, resized: 0, added: 0, removed: 0,
      renamed: 0, movedBetweenPages: 0, edited: 0, notes: 0, reordered: 0 };
    const entries = [], byPageOrig = new Map(), byUid = new Map();

    for (const p of doc.pages) {
      const base = p.origId ? basePage.get(p.origId) || null : null;
      const entry = { page: p, base, status: 'unchanged', pageChanges: [], rows: [], removed: [], movedOut: [] };
      if (base) {
        byPageOrig.set(base.origId, entry);
        for (const f of PAGE_FIELDS) if (!same(base[f], p[f])) entry.pageChanges.push({ field: f, old: base[f], new: p[f] });
      }
      if (p.notes) entry.pageChanges.push({ field: 'notes', old: '', new: p.notes });
      for (const e of p.elements) {
        const hit = e.origId ? baseEl.get(e.origId) : null;
        let row;
        if (!hit || placed.get(e.origId).el !== e) {
          row = { el: e, base: null, flags: ['added'], changes: [], fromPage: null };
        } else {
          const { flags, changes } = elementChanges(hit.e, e);
          row = { el: e, base: hit.e, flags, changes, fromPage: null };
          if (hit.page.origId !== p.origId) { row.fromPage = hit.page; row.flags.unshift('moved from ' + hit.page.id); }
        }
        if (e.notes) row.note = e.notes;
        entry.rows.push(row);
        byUid.set(e.uid, row);
      }
      // draw order of the original widgets that are still on this page
      if (base) {
        const now = entry.rows.filter((r) => r.base && !r.fromPage).map((r) => r.base.origId), still = new Set(now);
        const was = base.elements.map((e) => e.origId).filter((o) => still.has(o));
        if (now.join() !== was.join()) { entry.pageChanges.push({ field: 'draw order', old: null, new: null }); counts.reordered++; }
      }
      entries.push(entry);
    }
    // baseline pages that are gone from the layout altogether (an imported layout can do that)
    for (const bp of baseline.pages) {
      if (byPageOrig.has(bp.origId)) continue;
      const entry = { page: bp, base: bp, status: 'removed', gone: true, pageChanges: [], rows: [], removed: [], movedOut: [] };
      byPageOrig.set(bp.origId, entry);
      entries.push(entry);
    }
    // removed widgets and widgets that left their page, listed under the page they were on
    for (const [orig, { e, page }] of baseEl) {
      const entry = byPageOrig.get(page.origId);
      const at = placed.get(orig);
      if (!at) entry.removed.push(e);
      else if (at.page.origId !== page.origId) entry.movedOut.push({ base: e, el: at.el, toPage: at.page });
    }

    for (const entry of entries) {
      const p = entry.page;
      if (entry.gone) { counts.pagesRemoved++; continue; }
      if (!entry.base) entry.status = 'added';
      else if (p.hidden) entry.status = 'removed';
      else if (entry.pageChanges.length || entry.removed.length || entry.movedOut.length || entry.rows.some((r) => r.flags.length || r.note)) entry.status = 'changed';
      if (entry.status === 'added') counts.pagesAdded++;
      if (entry.status === 'removed') counts.pagesRemoved++;
      if (entry.status === 'changed') counts.pagesChanged++;
      if (entry.pageChanges.some((c) => c.field === 'id' || c.field === 'title')) counts.pagesRenamed++;
      counts.removed += entry.removed.length;
      for (const r of entry.rows) {
        for (const f of r.flags) {
          if (f.startsWith('moved from')) counts.movedBetweenPages++;
          else counts[f]++;
        }
        if (r.note) counts.notes++;
      }
    }
    return { pages: entries, counts, byUid };
  }

  // ---- text helpers ----------------------------------------------------------------------------------------------
  function fmtNum(n) { return Number.isInteger(n) ? String(n) : String(Math.round(n * 100) / 100); }
  function fmtValue(v) {
    if (v === undefined || v === null || v === '') return '(none)';
    if (Array.isArray(v)) return '[' + v.map((x) => (typeof x === 'number' ? fmtNum(x) : JSON.stringify(x))).join(', ') + ']';
    if (typeof v === 'number') return fmtNum(v);
    if (typeof v === 'string') return JSON.stringify(v);
    return JSON.stringify(v);
  }
  const code = (s) => '`' + String(s).replace(/`/g, "'") + '`';
  const cell = (s) => String(s === undefined || s === null ? '' : s).replace(/\|/g, '\\|').replace(/\r?\n/g, ' ');
  const oneLine = (s) => String(s).replace(/\r?\n+/g, ' / ');

  // `button "HOST GAME"` plus how to find it in the code: orig id, widget id, action
  function describe(e, withOrig = true) {
    const bits = [];
    if (withOrig && e.origId) bits.push('orig ' + code(e.origId));
    if (e.id) bits.push('id ' + code(e.id));
    if (e.action) bits.push('action ' + code(e.action));
    if (e.copyOf) bits.push('copy of ' + code(e.copyOf));
    return e.kind + ' ' + JSON.stringify(e.label || '') + (bits.length ? ' (' + bits.join(', ') + ')' : '');
  }
  function changeText(c) {
    if (c.field === 'position') {
      const dx = c.new[0] - c.old[0], dy = c.new[1] - c.old[1], sg = (n) => (n >= 0 ? '+' : '') + fmtNum(n);
      return `moved: (${fmtNum(c.old[0])}, ${fmtNum(c.old[1])}) -> (${fmtNum(c.new[0])}, ${fmtNum(c.new[1])})  [dx ${sg(dx)}, dy ${sg(dy)}]`;
    }
    if (c.field === 'size') return `resized: ${fmtNum(c.old[0])}x${fmtNum(c.old[1])} -> ${fmtNum(c.new[0])}x${fmtNum(c.new[1])}`;
    if (c.field === 'label') return `label: ${fmtValue(c.old)} -> ${fmtValue(c.new)}`;
    if (c.field === 'draw order') return 'draw order of its widgets changed (see the full layout table, rows are in draw order)';
    if (c.field === 'notes') return 'note: ' + oneLine(c.new);
    return `${c.field}: ${fmtValue(c.old)} -> ${fmtValue(c.new)}`;
  }
  // Every property an added widget has (so it can be built from the line alone).
  function propsText(e) {
    const out = [`at (${fmtNum(e.x)}, ${fmtNum(e.y)}) size ${fmtNum(e.w)}x${fmtNum(e.h)}`];
    for (const k of Object.keys(e)) {
      if (META.has(k) || GEOM.includes(k) || k === 'kind' || k === 'label' || k === 'action' || k === 'id') continue;
      if (e[k] === undefined || e[k] === null || e[k] === '') continue;
      out.push(`${k} ${fmtValue(e[k])}`);
    }
    return out.join(', ');
  }
  const pageName = (p) => code(p.id) + (p.title && p.title !== p.id ? ' (' + p.title + ')' : '');
  function stamp(d) {
    const z = (n) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}-${z(d.getHours())}${z(d.getMinutes())}`;
  }

  // ---- the font trial ---------------------------------------------------------------------------------------------
  // opts.font: {name, family, fileName, path, uploaded, builtin, weight, stretch, sizeK, mult, minTs, letterSpacing, upper,
  //   licence ('open' | 'preview' | 'unknown'), licenceSummary, licenceText, licenceUrl, axes, capRatio, capFrom}
  const LICENCE_WORD = { open: 'open licence, can ship', preview: 'preview-only licence', unknown: 'licence unknown' };
  const isDefaultFont = (f) => !f || (f.builtin && Number(f.weight) === 700 && !f.stretch && !Number(f.letterSpacing) && !f.upper &&
    (Number(f.mult) || 1) === 1 && !(Number(f.minTs) > 0));
  function fontHeadline(f) {
    if (isDefaultFont(f)) return `Font: ${f && f.name ? f.name : 'Rubik'}, weight 700, size multiplier 1 (the game's current font: no font change)`;
    const bits = [`Font: ${f.name}`, `weight ${f.weight}`];
    if (f.stretch) bits.push(`width ${f.stretch}`);
    bits.push(`size multiplier ${fmtNum(Number(f.mult) || 1)}`);
    if (Number(f.minTs) > 0) bits.push(`minimum text scale ${fmtNum(f.minTs)}`);
    if (Number(f.letterSpacing)) bits.push(`letter spacing ${fmtNum(f.letterSpacing)} em`);
    if (f.upper) bits.push('all caps');
    return bits.join(', ') + '; ' + (LICENCE_WORD[f.licence] || LICENCE_WORD.unknown);
  }
  function fontSection(f, baseTs) {
    const L = ['## Font', ''];
    const same = f.builtin;
    L.push(`- ${fontHeadline(f)}.`);
    const fam = f.family ? ` (family ${JSON.stringify(f.family)})` : '';
    if (f.uploaded) L.push(`- File: uploaded by the owner: ${code(f.fileName || '?')}${fam}. Ask the owner for the file (it is not in the repo).`);
    else if (same) L.push(`- File: the game's own font, assets/fonts/${f.fileName || 'Rubik.ttf'}${fam}.`);
    else L.push(`- File: ${code(f.fileName || '?')}${fam}` + (f.path ? `, on the owner's PC at ${code(f.path)}` : '') + '.');
    if (f.axes && f.axes.length) L.push(`- Variable font: ${f.axes.map((a) => `${a.tag} ${fmtNum(a.min)}-${fmtNum(a.max)}`).join(', ')}.`);
    L.push(`- Licence: ${f.licenceSummary || LICENCE_WORD[f.licence] || 'unknown'}` + (f.licenceUrl ? ` (${f.licenceUrl})` : '') + '.');
    if (f.licence === 'preview') L.push('  - Do NOT ship this file. It was picked for its look: find an open-licence (OFL) font that looks the same, check it in the customizer, then ship that one.');
    if (f.licence === 'unknown') L.push('  - The font states no licence: confirm it may be redistributed before shipping it.');
    const k = Number(f.sizeK) || 1;
    L.push(`- Size correction used in the preview: font size x ${fmtNum(Math.round(k * 1000) / 1000)}, so its cap height matches the game's font (Rubik) at the same textScale` +
      (f.capRatio ? ` (cap height ${fmtNum(f.capRatio)} em from ${f.capFrom || 'the font'}, Rubik 0.70 em)` : '') + '.');
    const m = Number(f.mult) || 1;
    if (m !== 1) L.push(`- Text size: every text ${fmtNum(m)}x larger (default text scale ${baseTs} at 720p -> ${fmtNum(Math.round(baseTs * m * 100) / 100)}). ` +
      'Widgets keep their sizes: text that no longer fits is shrunk then clipped with "..." as Ui::fit does (check the long labels).');
    if (Number(f.minTs) > 0) L.push(`- Minimum text scale ${fmtNum(f.minTs)}: text the game draws smaller (scale 1, drawn at 1.3 by Renderer::kMinTextScale) is drawn at ${fmtNum(f.minTs)} instead.`);
    L.push('', 'How to apply it in the game:', '');
    if (!same || f.weight !== 700) {
      L.push('- UI fonts are baked by tools/make_font_atlas.py (src/client/font_ui.h: the default, Rubik; font_builtin.h: the others of Settings > FONT, ' +
        'each sized so its capitals are as tall as Rubik\'s). To offer this font: put the file and its licence in assets/fonts/, add a BUILTIN row there and ' +
        'a kBuiltinFonts row in src/client/uifonts.cpp, and re-run the script (an open licence only). A player can also try it as mods/fonts/<file> (never shipped).');
      if (f.axes && f.axes.length) L.push(`- It is a variable font: the game uses the default instance, so make a static instance at weight ${f.weight}` + (f.stretch ? ` and width ${f.stretch}` : '') + ' first (e.g. fontTools varLib.instancer).');
      else if (f.weight !== 700 && same) L.push(`- The game's font at weight ${f.weight}: change WEIGHT in tools/make_font_atlas.py (a variable font's wght).`);
      if (Math.abs(k - 1) > 0.005) L.push(`- Its cap height differs from Rubik's: make_font_atlas.py already sizes a BUILTIN font to Rubik's capitals (here x ${fmtNum(Math.round(k * 1000) / 1000)}).`);
    }
    if (Number(f.letterSpacing)) L.push(`- Letter spacing: add ${fmtNum(f.letterSpacing)} em (${fmtNum(Math.round(f.letterSpacing * 7.5 * 100) / 100)} font units of the 7.5-unit em) to every advance.`);
    if (f.upper) L.push('- All caps: the preview uppercases every text (the game already uppercases most labels; lowercase text such as chat and names would change too).');
    if (m !== 1) L.push(`- Text size: multiply the UI text scale by ${fmtNum(m)} (Ui text scale / Renderer::text callers).`);
    if (Number(f.minTs) > 0) L.push(`- Raise the smallest text scale to ${fmtNum(f.minTs)} (Renderer::kMinTextScale, or the callers that pass 1).`);
    L.push('');
    return L;
  }

  // ---- the title logo (label style "logo") ------------------------------------------------------------------------
  // Numbers from working-repo/tools/make_icon.py (render.js LOGO draws the same).
  // Only the logos the layout adds: since 1.18.13 the game draws its menu title this way (Renderer::logo), so a label
  // that already was a logo in the dump needs no instructions.
  function logoSection(doc, baseTs, baseline) {
    const was = new Set();
    for (const p of (baseline && baseline.pages) || []) for (const e of p.elements) if (e.style === 'logo') was.add(e.origId);
    const logos = [];
    for (const p of doc.pages) if (!p.hidden) for (const e of p.elements) if (e.style === 'logo' && !(e.origId && was.has(e.origId))) logos.push({ p, e });
    if (!logos.length) return [];
    const ts = Math.max(...logos.map((l) => Number(l.e.textScale) || baseTs * 4));   // (the title: the biggest)
    const em = 7.5 * ts * 0.66 / 0.7, r = em * 18 / 844, seam = em * 12 / 844;
    const f2 = (n) => String(Math.round(n * 100) / 100);
    const f4 = (n) => String(Math.round(n * 10000) / 10000);
    const ids = [...new Set(logos.map((l) => l.p.id))];
    const labels = [...new Set(logos.map((l) => JSON.stringify(l.e.label)))];
    const L = ['## Title logo (label style "logo")', ''];
    L.push(`- ${logos.length} label(s) on ${ids.length} page(s) have ${code('style: "logo"')} (${labels.join(', ')}): ` +
      'the title `ui_.labelCentered(cx, y, "QUARK", rgba(cfg_.colorFriendly), 4.0f)` (game.cpp, drawMenu) drawn like the app icon (assets/quark.png, made by tools/make_icon.py). ' +
      'Position and centring stay as they are (centred on cx, line top at y = H * 0.12).');
    L.push('- Font: **Rubik, wght 800 (ExtraBold)**, assets/fonts/Rubik.ttf (variable, wght 300-900; make_icon.py sets the axis to 800). The same weight for every letter.');
    L.push(`- Size: the em of the game's text at that scale, with Rubik's cap height (0.70 em) matched to Work Sans' (0.66 em): em px = 7.5 x textScale x s x 0.9429. ` +
      `The title (textScale ${f2(ts)} at 720p = labelCentered 4.0 x the UI text scale ${baseTs}): em ${f2(em)} px, cap height ${f2(em * 0.7)} px at 720p (s = H / 720).`);
    L.push('- First letter (the Q), exactly as the icon:',
      '  - Fill: cyan `#40D9FF` (64, 217, 255) left of the split line, red `#FF4D40` (255, 77, 64) right of it (make_icon.py CY / RD; fixed logo colours, not the team colour settings).',
      "  - Split line: through c = (pen x + advance / 2, centre of the Q's ink box vertically, tail included), tilted 9.09 deg from vertical, top leaning right: " +
      "x = cx - 0.16 (y - cy). (The icon's line from (0.58 S, 0) to (0.42 S, S) runs through the tile centre, where make_icon.py centres Pillow's textbbox of the Q: " +
      "the advance across, the ink down; for Rubik 800 that is 0.0024 em left of the ink box's centre.)",
      `  - Seam: a black line on the split line, ${f4(12 / 844)} em wide (${f2(seam)} px at 720p for the title), only inside the glyph (the icon's 12 px at S = 1024 with an 844 px em).`,
      "  - The tail is Rubik 800's own Q tail: nothing added or moved.");
    L.push('- Other letters ("UARK"): Rubik 800 filled with the friendly colour (cfg_.colorFriendly, default #40D9FF: the title\'s colour today), with the same outline.');
    L.push(`- Outline: black, behind every letter: the glyphs dilated by ${f4(18 / 844)} em = **${f2(r)} px at 720p** for the title, scaling with the em (x s: ${f2(r * 1.5)} px at 1080p, ` +
      `${f2(r * 2)} px at 1440p, ${f2(r * 3)} px at 2160p). A square dilation (Pillow MaxFilter(37) at em 844 px), so corners are a little fuller than a round SDF outline. ` +
      'The letters are pasted over it with their own antialiasing.');
    L.push("- Alpha: the label colour's alpha fades the whole logo, outline included.");
    L.push('- How to draw it in the game (text comes from one SDF atlas of the UI font, one colour per glyph):',
      '  - (a) Two clipped draws: a Rubik-800 SDF atlas for the 5 letters, the Q drawn twice (cyan, then red) with a half-plane clip in the text shader ' +
      '(discard where dot(p - c, n) < 0, n = (cos 9.09 deg, sin 9.09 deg) the line\'s normal), plus the seam as a third clipped pass. Needs a second atlas and a shader change, and the SDF outline is round, not square.',
      '  - (b) **Recommended: a baked logo texture on a textured quad.** A tools script (the same Pillow code as make_icon.py: mask, MaxFilter, split paste, seam) bakes "QUARK" at about 2x the largest ' +
      'on-screen size (em ~ 240 px, ~ 620 x 200 px) as two layers: A = outline + the split Q (fixed colours), B = the "UARK" fill in white. The menu draws A, then B tinted with cfg_.colorFriendly, ' +
      'as two quads over the same rectangle (mipmapped, linear filtering), centred on cx, height scaled by s. It matches the icon pixel for pixel (same rasteriser, same square outline), costs one ' +
      'small texture and two quads a frame, and needs only a textured-quad path in the 2D batch (Renderer has rect and text only today). Rebake it when the font or make_icon.py changes, like font_ui.h.');
    L.push('');
    return L;
  }

  // ---- the spec ----------------------------------------------------------------------------------------------------
  // opts: date, dumpName, font (fontDesc), freshness (build.py's: a stale dump is called out at the top), layout: which
  // pages get a full widget table after the changes: 'changed' (default: the changed pages, so an agent sees each one
  // whole), 'all' (every page: about 800 KB for the whole game) or 'none'.
  const SPEC_LAYOUTS = ['changed', 'all', 'none'];
  function specMarkdown(baseline, doc, opts = {}) {
    const date = opts.date || new Date();
    const lf = layoutFont(doc);   // (the layout's own font wins over none / the default Work Sans)
    if (lf && (!opts.font || (isDefaultFont(opts.font) && lf.id !== (opts.font.id || 'rubik')))) opts = Object.assign({}, opts, { font: layoutFontDesc(lf) });
    const D = diff(baseline, doc), c = D.counts, src = baseline.source;
    const W = src.width || 1280, H = src.height || 720;
    const L = [];
    const z = (n) => String(n).padStart(2, '0');
    L.push('# Quark UI change spec', '');
    if (opts.font) L.push('**' + fontHeadline(opts.font) + '**' + (isDefaultFont(opts.font) ? '' : ' (see "Font" below)'), '');
    L.push(`- Source: ${src.repo || '?'} @ commit ${code(src.commit || '?')}` + (opts.dumpName ? ` (layout dump: ${opts.dumpName})` : ''));
    L.push(`- Generated: ${date.getFullYear()}-${z(date.getMonth() + 1)}-${z(date.getDate())} ${z(date.getHours())}:${z(date.getMinutes())} (local time), by the Quark UI customizer`);
    L.push(`- Coordinates: pixels in a ${W}x${H} frame, origin top-left, x to the right, y down; w/h in the same pixels. ` +
      `The game lays menus out at 720p units times ui.s() (1.0 at 720p), mostly relative to the screen centre ` +
      `(cx = ${W / 2}): turn a new position into the same kind of expression (e.g. x = cx + (${'newX'} - ${W / 2}) * s) rather than an absolute pixel.`);
    L.push(`- Text: drawn at text scale ${baseline.style.textScale || 2} at 720p (a line is 8 * scale px tall); "textScale" values below are at 720p.`);
    L.push('- Widget identity: "orig" is `<page id>#<index>` in the dump\'s draw order; also given: the widget\'s UiExp (action) and id. ' +
      'Find a widget in the code by its label and UiExp, not by the index.', '');
    const fr = opts.freshness;
    if (fr && fr.stale) L.push(`**Warning: made against an out-of-date UI dump (${fr.why}).** The game may have changed these pages since: ` +
      'check each change against the current code before applying it.', '');
    if (opts.font && !isDefaultFont(opts.font)) L.push(...fontSection(opts.font, baseline.style.textScale || 2));

    L.push(...logoSection(doc, baseline.style.textScale || 2, baseline));
    L.push('## Summary', '');
    L.push('| what | count |', '|---|---|');
    const sum = [['pages added', c.pagesAdded], ['pages removed', c.pagesRemoved], ['pages changed', c.pagesChanged], ['pages renamed', c.pagesRenamed],
      ['widgets moved', c.moved], ['widgets resized', c.resized], ['widgets added', c.added], ['widgets removed', c.removed],
      ['widgets renamed', c.renamed], ['widgets moved between pages', c.movedBetweenPages], ['widgets with other property changes', c.edited],
      ['pages with a new draw order', c.reordered], ['notes from the owner', c.notes]];
    for (const [k, v] of sum) L.push(`| ${k} | ${v} |`);
    L.push('');

    L.push('## Implementation notes', '');
    L.push('- Every interactive widget call in the game takes a UiExp (src/client/ui.h, docs/UI_TEST.md). Added widgets give their intended action as `nav:<screen>`, `cfg:<key>`, `state:<obs>`, `cmd:<prefix>`, `url`, `quit`, `local`, `bind:<action>`... Where an added widget has no action, ask the owner what it should do.');
    L.push('- Kinds: `tab` is Ui::tab (current: filled), `colorpicker` Ui::colorPicker, `dropdown` Ui::dropdown, `popup` the open picker / list drawn by Ui::popups(); a label with `hint` is Ui::hint (0.75 of the text scale, 1.5 at 720p); `maxW` is the width a label is fitted to (closed up, then smaller, then cut).');
    L.push('- A "note" line is free text from the owner about the intent: it wins over the numbers when they disagree.');
    L.push('- Labels with align "center": x is the left edge of the text as drawn; the game centres it on cx = x + w/2 (given below when such a label moves).');
    L.push('- Kind `key` with a zero size is a hotkey of the screen (Ui::keyAction); a `key` with a size is a keybind slot (label + key box: in the game a BIND button next to a label).');
    L.push('- Page ids are the UI test screen paths (Game::uiScreenPath), with [context] / [scenario] tags when one screen has variants.');
    L.push('- Keep `quark --uitest` passing (every widget declares what it does, and the test clicks it).', '');

    // ---- changes
    L.push('## Changes', '');
    const changed = D.pages.filter((e) => e.status !== 'unchanged');
    if (!changed.length) L.push('No changes: the layout is the default.', '');
    for (const e of changed) {
      const p = e.page;
      const tag = e.status === 'added' ? 'NEW PAGE' : e.status === 'removed' ? 'REMOVE PAGE' : 'changed';
      L.push(`### ${pageName(p)} - ${tag}`, '');
      if (e.status === 'added') L.push(`- New page ${code(p.id)}, title ${fmtValue(p.title)}, background ${fmtValue(p.background)}, ${p.inGame ? 'in game' : 'menu'}. Something must navigate to it (see its widgets and the other pages' nav: actions).`);
      if (e.status === 'removed') L.push(`- Remove this page${e.gone ? ' (it is not in the layout at all)' : ' (hidden by the owner)'}; whatever navigates to it should go, or point elsewhere.`);
      for (const pc of e.pageChanges) L.push('- Page ' + changeText(pc));
      for (const r of e.rows) {
        if (!r.flags.length && !r.note) continue;
        if (!r.base) {
          L.push(`- **added** ${describe(r.el)}: ${propsText(r.el)}`);
        } else {
          const lead = r.fromPage ? `**moved here from page ${code(r.fromPage.id)}**` : '**' + r.flags.join(', ') + '**';
          L.push(`- ${lead} ${describe(r.base)}` + (r.flags.length ? '' : ' (unchanged)'));
          if (r.fromPage) L.push(`  - page: ${code(r.fromPage.id)} -> ${code(p.id)}` + (r.flags.length > 1 ? ' (' + r.flags.slice(1).join(', ') + ')' : ''));
          for (const ch of r.changes) L.push('  - ' + changeText(ch));
          if (r.el.kind === 'label' && r.el.align === 'center' && (r.flags.includes('moved') || r.flags.includes('resized') || r.flags.includes('renamed'))) {
            L.push(`  - centred on cx: ${fmtNum(r.base.x + r.base.w / 2)} -> ${fmtNum(r.el.x + r.el.w / 2)}`);
          }
        }
        if (r.note) L.push('  - note: ' + oneLine(r.note));
      }
      for (const m of e.movedOut) L.push(`- **moved to page ${code(m.toPage.id)}** ${describe(m.base)} (details under that page)`);
      for (const b of e.removed) L.push(`- **removed** ${describe(b)}: was at (${fmtNum(b.x)}, ${fmtNum(b.y)}) size ${fmtNum(b.w)}x${fmtNum(b.h)}`);
      L.push('');
    }

    // ---- full layout (of the changed pages, or of every page)
    const layout = SPEC_LAYOUTS.includes(opts.layout) ? opts.layout : 'changed';
    const tables = layout === 'all' ? D.pages : layout === 'changed' ? changed : [];
    if (layout === 'all') L.push('## Full layout', '', 'Every page of the edited layout. Widget rows are in draw order (later rows draw on top).', '');
    else if (tables.length) L.push('## Layout of the changed pages', '', 'Every widget of each changed page, as edited. Widget rows are in draw order (later rows draw on top). ' +
      'Unchanged pages are left out (export with "every page" to list them too).', '');
    for (const e of tables) {
      const p = e.page;
      const st = e.status === 'added' ? 'new' : e.status;
      L.push(`### ${pageName(p)} - ${st}`, '');
      const meta = [`title ${fmtValue(p.title)}`];
      if (p.background) meta.push(`background ${fmtValue(p.background)}`);
      if (p.inGame) meta.push('in game');
      if (p.scenarios && p.scenarios.length) meta.push('scenarios ' + p.scenarios.join(', '));
      if (p.screenshot) meta.push('screenshot ' + code(p.screenshot));
      L.push(meta.join('; '), '');
      if (e.rows.length) {
        L.push('| # | kind | label | x | y | w | h | action | status | orig |', '|---|---|---|---|---|---|---|---|---|---|');
        e.rows.forEach((r, i) => {
          const el = r.el;
          const status = r.flags.length ? r.flags.join(', ') : 'unchanged';
          L.push(`| ${i + 1} | ${cell(el.kind)} | ${cell(el.label)} | ${fmtNum(el.x)} | ${fmtNum(el.y)} | ${fmtNum(el.w)} | ${fmtNum(el.h)} | ${cell(el.action || '')} | ${cell(status + (r.note ? ' (note)' : ''))} | ${cell(el.origId || (el.copyOf ? 'copy of ' + el.copyOf : '-'))} |`);
        });
        L.push('');
      } else if (!e.gone) L.push('(no widgets)', '');
      const gone = e.removed.map((b) => ({ b, why: 'removed' })).concat(e.movedOut.map((m) => ({ b: m.base, why: 'moved to ' + m.toPage.id })));
      if (e.gone) for (const b of e.base.elements) if (!gone.some((g) => g.b === b)) gone.push({ b, why: 'removed with the page' });
      if (gone.length) {
        L.push('No longer on this page:', '');
        L.push('| kind | label | x | y | w | h | action | status | orig |', '|---|---|---|---|---|---|---|---|---|');
        for (const { b, why } of gone) L.push(`| ${cell(b.kind)} | ${cell(b.label)} | ${fmtNum(b.x)} | ${fmtNum(b.y)} | ${fmtNum(b.w)} | ${fmtNum(b.h)} | ${cell(b.action || '')} | ${cell(why)} | ${cell(b.origId)} |`);
        L.push('');
      }
    }
    return { markdown: L.join('\n'), diff: D, fileName: 'ui-spec-' + stamp(date) + (layout === 'all' ? '-full' : '') + '.md' };
  }

  // ---- provenance ----------------------------------------------------------------------------------------------------
  // Who made each page: page.prov = {origin: 'human' | 'ai', from: 'ai' (a human edit on top of the AI proposal),
  // date: 'YYYY-MM-DD' (last human edit)}, saved with the layout. The tool marks a page 'human' when an edit made in its
  // UI changes it, and a proposal build marks every page that differs from the game default 'ai' when it loads.
  // Exported to ui-customizer/provenance.json: {format: 1, tool: 'ui-customizer', items: {'ui/<page id>': {origin, by,
  // date, note}}}; pages identical to the game default are not listed.
  const PROV_BY = 'q';
  const AI_NOTE = 'AI redesign (ui-customizer proposal)';
  const ORIGINS = ['human', 'ai'];
  function ymd(d) {
    const z = (n) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}`;
  }
  const provKey = (p) => 'ui/' + p.id;
  // The editor dashboard's id for a provenance key (registry: assets/provenance/ui.json in the game repo): the
  // " [state]" suffix of a dump page (sel:, keep:, scroll, client-spectator, demo, host-*, hide-* ...) is dropped and
  // the dump's in-game paths become the registry's page ids. Ids the registry lacks (new pages) are kept as they are.
  function registryKey(key) {
    let k = String(key);
    while (/\s*\[[^\]]*\]\s*$/.test(k)) k = k.replace(/\s*\[[^\]]*\]\s*$/, '');
    if (!k.startsWith('ui/')) return k;
    let id = k.slice(3), m;
    if (/^editor(?:-online)?\/chat(?:\/|$)/.test(id)) return 'ui/hud/chat';   // the chat overlay of an edit session
    id = id.replace(/^editor-online(?=\/|$)/, 'editor');
    if ((m = /^(?:host|vsbots|ingame\/pause|ingame)\/picker[:/](.+)$/.exec(id))) id = 'picker/' + m[1];
    else if ((m = /^ingame\/pause\/settings(\/.*)?$/.exec(id))) id = 'settings' + (m[1] || '');
    else if ((m = /^ingame\/pause(\/.*)?$/.exec(id))) id = 'pause' + (m[1] || '');
    else if ((m = /^ingame\/console(\/.*)?$/.exec(id))) id = 'console' + (m[1] || '');
    else if ((m = /^ingame\/chat(\/.*)?$/.exec(id))) id = 'hud/chat' + (m[1] || '');
    else if ((m = /^ingame\/scores(\/.*)?$/.exec(id))) id = 'hud/scoreboard' + (m[1] || '');
    else if (/^ingame\/perf(mini)?(\/|$)/.test(id)) id = 'hud/perf-overlay';   // (F3's overlay and its mini latency line)
    else if (id === 'ingame') id = 'hud/widgets';
    return 'ui/' + id;
  }
  // Provenance items keyed by registry id: items that map to one id are merged (any 'human' -> 'human', else 'ai';
  // the latest date; distinct notes and makers joined). Input not modified; keys sorted.
  function mapProvenanceItems(items) {
    const out = {};
    for (const k of Object.keys(items || {}).sort()) {
      const v = items[k], key = registryKey(k), o = out[key];
      if (!v || typeof v !== 'object') continue;
      if (!o) { out[key] = clone(v); continue; }
      const join = (a, b) => (a == null || a === '' ? b : b == null || b === '' || String(a).split(' | ').includes(b) ? a : a + ' | ' + b);
      const m = Object.assign({}, o, v);
      m.origin = o.origin === 'human' || v.origin === 'human' ? 'human' : 'ai';
      if ('by' in o || 'by' in v) m.by = o.by === v.by ? o.by : join(o.by, v.by);
      if ('date' in o || 'date' in v) m.date = String(o.date || '') >= String(v.date || '') ? o.date : v.date;
      if ('note' in o || 'note' in v) m.note = join(o.note, v.note);
      out[key] = m;
    }
    const sorted = {};
    for (const k of Object.keys(out).sort()) sorted[k] = out[k];
    return sorted;
  }
  function sanitizeProv(m) {
    if (!m || typeof m !== 'object' || !ORIGINS.includes(m.origin)) return null;
    const out = { origin: m.origin };
    if (m.from === 'ai') out.from = 'ai';
    if (typeof m.date === 'string' && /^\d{4}-\d\d-\d\d$/.test(m.date)) out.date = m.date;
    return out;
  }
  // An edit made through the tool changed this page.
  function markHuman(page, date) {
    const was = sanitizeProv(page.prov);
    page.prov = { origin: 'human' };
    if (was && (was.from === 'ai' || was.origin === 'ai')) page.prov.from = 'ai';
    if (date) page.prov.date = date;
    return page;
  }
  // Every page that differs from the game default and has no mark yet becomes 'ai' (proposal builds, at load).
  function markAi(baseline, doc) {
    let n = 0;
    for (const e of diff(baseline, doc).pages) {
      if (e.gone || e.status === 'unchanged' || sanitizeProv(e.page.prov)) continue;
      e.page.prov = { origin: 'ai' };
      n++;
    }
    return n;
  }
  // The provenance items of a layout: one per page that differs from the game default (a page gone from the layout
  // altogether counts too). opts: date (YYYY-MM-DD, for items without a date of their own), defaultOrigin (pages that
  // differ but carry no mark: 'ai' in a proposal build, else 'human'), aiNote, by.
  function provenanceItems(baseline, doc, opts = {}) {
    const by = opts.by || PROV_BY, date = opts.date || ymd(new Date());
    const items = {};
    for (const e of diff(baseline, doc).pages) {
      if (e.status === 'unchanged') continue;
      const m = e.gone ? null : sanitizeProv(e.page.prov);
      const origin = m ? m.origin : opts.defaultOrigin === 'ai' ? 'ai' : 'human';
      let note;
      if (origin === 'ai') note = opts.aiNote || AI_NOTE;
      else if (m && m.from === 'ai') note = 'q edited the AI proposal in the ui-customizer (started from the AI redesign; q made the final design)';
      else if (m) note = 'designed by q in the ui-customizer';
      else note = 'changed in the ui-customizer; origin not recorded (edited before provenance tracking, or imported), assumed q';
      const key = provKey(e.page);
      if (items[key] && items[key].origin === 'human') continue;   // (two pages with one id: a human one wins)
      items[key] = { origin, by, date: (m && m.date) || date, note };
    }
    return mapProvenanceItems(items);   // keyed by the editor dashboard's registry ids
  }
  // Reads provenance.json text: null when empty; throws when it is not the format (so it is never overwritten).
  function parseProvenance(text) {
    if (text == null || !String(text).trim()) return null;
    const d = JSON.parse(text);
    if (!d || typeof d !== 'object' || Array.isArray(d) || !d.items || typeof d.items !== 'object' || Array.isArray(d.items)) {
      throw new Error('provenance.json is not in the expected format (no "items" object)');
    }
    return d;
  }
  // Merges items into an existing provenance (object or null): items of other pages are kept; an 'ai' item that is
  // already 'ai' keeps its own date and note (e.g. who approved it); everything else is replaced. Keys sorted.
  // Old ids that map to another registry id (dump paths, " [state]" variants) are folded into that id first.
  function mergeProvenance(existing, newItems) {
    const old = mapProvenanceItems(existing && existing.items && typeof existing.items === 'object' ? existing.items : {});
    const items = mapProvenanceItems(newItems);
    const all = Object.assign({}, clone(old));
    let added = 0, updated = 0, kept = 0;
    for (const [k, v] of Object.entries(items)) {
      const o = all[k];
      if (o && o.origin === 'ai' && v.origin === 'ai') { kept++; continue; }
      if (!o) added++;
      else if (JSON.stringify(o) === JSON.stringify(v)) kept++;
      else updated++;
      all[k] = clone(v);
    }
    const sorted = {};
    for (const k of Object.keys(all).sort()) sorted[k] = all[k];
    return { data: { format: 1, tool: 'ui-customizer', items: sorted }, added, updated, kept, other: Object.keys(old).filter((k) => !(k in items)).length };
  }

  return { KINDS, GAME_KINDS, NEW_KINDS, normalizeDump, makeWorking, sanitizeWorking, expandBaseRefs, baseRefJson, signature, newElement, copyElement, newPage,
    diff, elementChanges, specMarkdown, SPEC_LAYOUTS, fontHeadline, isDefaultFont, stamp, clone, uid, layoutFont, setLayoutFont, layoutFontDesc,
    ymd, provKey, registryKey, mapProvenanceItems, sanitizeProv, markHuman, markAi, provenanceItems, parseProvenance, mergeProvenance };
})();

if (typeof module !== 'undefined') module.exports = QModel;
