// Quark map editor: the decoration panel (loaded after deco.js and deco_rules.js). MapDeco.mountPanel(el, api) builds
// it in the right dock's Decoration tab (tools/map_editor/README.md, "Decoration hook"): the rules (a form per
// generator), their targets, constraints and seed, presets, tags (saved selections), preview / apply / remove /
// regenerate all, the last report. Previews are drawn into api.layer (a THREE.Group the page owns). Plain DOM.
(function (G) {
'use strict';
const D = G.MapDeco = G.MapDeco || {};
const MATS = D.MATS;
const SYMS = [['auto', 'auto (the map\'s own)'], ['none', 'none'], ['point', 'point (x,z) -> (-x,-z)'], ['mirrorX', 'mirror x'], ['mirrorZ', 'mirror z']];
const TARGETS = [['all', 'whole map'], ['filter', 'filter'], ['set', 'captured selection'], ['tag', 'tag'], ['group', 'group']];
const CSS = `
.dk { display: flex; flex-direction: column; gap: 6px; padding: 6px 8px; overflow: auto; height: 100%; box-sizing: border-box; }
.dk h4 { margin: 6px 0 2px; font-size: 10px; text-transform: uppercase; letter-spacing: .08em; color: var(--text-faint, #888); font-weight: 600; }
.dk .bar { display: flex; flex-wrap: wrap; gap: 4px; align-items: center; }
.dk .rules { border: 1px solid var(--line, #333); border-radius: 4px; max-height: 220px; overflow: auto; }
.dk .rrow { display: flex; gap: 5px; align-items: center; padding: 3px 6px; cursor: pointer; border-left: 2px solid transparent; }
.dk .rrow:hover { background: var(--panel2, #222); }
.dk .rrow.on { background: var(--raise, #2a2a2a); border-left-color: var(--accent, #4f9cf5); }
.dk .rrow .n { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.dk .rrow .c { color: var(--text-faint, #888); font-size: 10px; white-space: nowrap; }
.dk .form { display: grid; grid-template-columns: 1fr auto; gap: 3px 6px; align-items: center; }
.dk .form label { color: var(--text-dim, #aaa); }
.dk .form input[type=number] { width: 72px; }
.dk .form input[type=text], .dk .form select { max-width: 150px; }
.dk .form .wide { grid-column: 1 / 3; }
.dk .help { color: var(--text-faint, #888); font-size: 11px; }
.dk .rep { font: 11px var(--mono, monospace); white-space: pre-wrap; color: var(--text-dim, #aaa); background: var(--panel2, #1d2128); border-radius: 4px; padding: 4px 6px; max-height: 200px; overflow: auto; }
.dk .mats { display: flex; flex-wrap: wrap; gap: 2px 8px; }
.dk .mats label { color: var(--text-dim, #aaa); font-size: 11px; }
`;
function el(tag, attrs, ...kids) {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
        if (v === undefined || v === null || v === false) continue;
        if (k === 'class') e.className = v;
        else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
        else if (k === 'value') e.value = v;
        else if (k === 'checked') e.checked = !!v;
        else e.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : String(c));
    return e;
}
// a deep copy of JSON data that keeps -0 (JSON.stringify writes it as 0, which would change a map's text on save)
const NEG0 = '\u0001-0\u0001';
const clone = (o) => o === undefined ? undefined : JSON.parse(JSON.stringify(o, (k, v) => (v === 0 && Object.is(v, -0) ? NEG0 : v)), (k, v) => (v === NEG0 ? -0 : v));
const opt = (v, label, cur) => el('option', { value: String(v), selected: String(v) === String(cur) }, label);
const fmt = (n) => (Math.round(n * 100) / 100).toString();

let mounted = null;
D.unmount = function () { if (mounted) { mounted.destroy(); mounted = null; } };
D.mountPanel = function (root, api) {
    D.unmount();
    const sc = api.sidecar || { get: api.sidecarGet, set: api.sidecarSet };
    const S = { deco: D.normDeco(sc.get('deco')), cur: null, report: null, preview: null, presets: [], userPresets: [], busy: false };
    if (!document.getElementById('dk-style')) document.head.append(el('style', { id: 'dk-style' }, CSS));
    const box = el('div', { class: 'dk' });
    root.replaceChildren(box);

    // ---- persistence and the map
    const save = () => {
        const applied = S.deco.rules.filter((r) => r.generated.length).map((r) => r.name);
        S.deco.rulesApplied = applied;   // (the page's save note reads it)
        sc.set('deco', S.deco);
    };
    const rule = () => S.deco.rules.find((r) => r.id === S.cur) || null;
    const selection = () => (api.getSelection ? api.getSelection() : { brushes: [], groups: [] });

    // ---- preview drawing (into api.layer, the page's 'deco' THREE group; README "Decoration hook")
    let pvGroup = null;
    function clearPreview(redraw = true) {
        S.preview = null;
        if (pvGroup && pvGroup.parent) pvGroup.parent.remove(pvGroup);
        if (pvGroup) pvGroup.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
        pvGroup = null;
        if (typeof api.setPreview === 'function') api.setPreview(null);
        if (redraw && api.redraw) api.redraw();
    }
    function drawPreview(add, remove) {
        clearPreview(false);
        S.preview = { add, remove };
        if (typeof api.setPreview === 'function') { api.setPreview({ add, remove }); return; }
        const T = api.THREE || G.THREE, layer = api.layer || api.scene;
        if (!T || !layer) { if (api.redraw) api.redraw({ preview: S.preview }); return; }
        pvGroup = new T.Group(); pvGroup.name = 'deco-preview';
        const part = (list, color) => {
            if (!list.length) return;
            const geo = new T.BoxGeometry(1, 1, 1), mat = new T.MeshBasicMaterial({ color, transparent: true, opacity: 0.35, depthWrite: false });
            const mesh = new T.InstancedMesh(geo, mat, list.length), m4 = new T.Matrix4(), q = new T.Quaternion();
            list.forEach((b, i) => {
                const s = [0, 1, 2].map((k) => Math.max(0.01, b.max[k] - b.min[k]));
                m4.compose(new T.Vector3((b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2), q, new T.Vector3(s[0] * 1.002, s[1] * 1.002, s[2] * 1.002));
                mesh.setMatrixAt(i, m4);
            });
            mesh.renderOrder = 5; pvGroup.add(mesh);
            const pos = new Float32Array(list.length * 72); let o = 0;   // 12 edges per box
            for (const b of list) {
                const [x0, y0, z0] = b.min, [x1, y1, z1] = b.max;
                const c = [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]];
                for (const [a, d] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) { pos.set(c[a], o); pos.set(c[d], o + 3); o += 6; }
            }
            const lg = new T.BufferGeometry(); lg.setAttribute('position', new T.BufferAttribute(pos, 3));
            const lines = new T.LineSegments(lg, new T.LineBasicMaterial({ color, transparent: true, opacity: 0.9, depthTest: false }));
            lines.renderOrder = 6; pvGroup.add(lines);
        };
        part(add, 0x2fb344); part(remove, 0xe0524a);
        layer.add(pvGroup);
        if (api.redraw) api.redraw();
    }

    // ---- actions
    // apply / remove / preview run in a Web Worker (js/worker_deco.js) when the page can make one: the page stays
    // responsive while Midtown regenerates. Without one (node tests, an old browser) they run here.
    let worker = null, wseq = 0, mapGen = 0;
    const pending = new Map();
    function engine(op, ...args) {
        if (worker === null && typeof Worker !== 'undefined') {
            try {
                worker = new Worker('js/worker_deco.js');
                worker.onmessage = (e) => { const p = pending.get(e.data.id); if (!p) return; pending.delete(e.data.id); if (e.data.error) p.reject(new Error(e.data.error)); else p.resolve(e.data.result); };
                worker.onerror = (e) => { console.warn('decoration worker failed, running on the page:', e.message); worker = false; for (const [, p] of pending) p.retry(); pending.clear(); };
            } catch (e) { worker = false; }
        }
        if (!worker) return Promise.resolve().then(() => D[op](...args));
        return new Promise((resolve, reject) => {
            const id = ++wseq;
            pending.set(id, { resolve, reject, retry: () => { try { resolve(D[op](...args)); } catch (e) { reject(e); } } });
            worker.postMessage({ id, op, args });
        });
    }
    // runs fn (may be async) once; a map change while it ran (the result was made from the old map) drops its result
    function run(label, fn) {
        if (S.busy) return;
        S.busy = true; status.textContent = label + '... (the editor stays usable)';
        const g0 = mapGen;
        setTimeout(async () => {
            try { await fn(() => mapGen === g0); } catch (e) { S.report = 'error: ' + (e && e.stack || e); console.error(e); }
            S.busy = false; status.textContent = ''; render();
        }, 10);
    }
    const STALE = 'the map changed while this ran: nothing was applied, run it again';
    function reportText(rep, extra) {
        if (!rep) return '';
        const lines = [];
        if (extra) lines.push(extra);
        for (const r of rep.rules || []) {
            const rj = Object.entries(r.rejected || {}).map(([k, v]) => k + ' ' + v).join(', ');
            lines.push(r.name + ': ' + r.units + ' units, ' + r.boxes + ' boxes from ' + r.targets + ' target boxes (' + r.ms + ' ms, symmetry ' + (r.symmetry || '-') + ')' + (rj ? '\n  not placed: ' + rj : ''));
            for (const w of r.warnings || []) lines.push('  ! ' + w);
            for (const s of (r.samples || []).slice(0, 5)) lines.push('  - ' + s.why + ' at ' + s.at.join(' '));
        }
        for (const w of rep.warnings || []) lines.push('! ' + w);
        return lines.join('\n');
    }
    const previewRules = (ids) => run('Preview', async (fresh) => {
        const p = await engine('preview', api.getMap(), S.deco, ids);
        if (!fresh()) { S.report = STALE; return; }
        drawPreview(p.add, p.remove);
        S.report = reportText(p.report, 'preview: +' + p.add.length + ' boxes, -' + p.remove.length + ' (green: added, red: taken out)');
    });
    const applyRules = (ids, label) => run(label, async (fresh) => {
        clearPreview(false);
        const a = await engine('apply', api.getMap(), S.deco, ids);
        if (!fresh()) { S.report = STALE; return; }
        S.deco = a.deco; save();
        api.setMap(a.map, label);
        S.report = reportText(a.report, label + ': +' + a.added.length + ' boxes, -' + a.removed.length + (a.missing.length ? ', ' + a.missing.length + ' recorded boxes were no longer in the map (moved or deleted: left alone)' : ''));
    });
    const removeRules = (ids, label) => run(label, async (fresh) => {
        clearPreview(false);
        const a = await engine('remove', api.getMap(), S.deco, ids);
        if (!fresh()) { S.report = STALE; return; }
        S.deco = a.deco; save();
        api.setMap(a.map, label);
        S.report = label + ': -' + a.removed.length + ' boxes' + (a.missing.length ? ', ' + a.missing.length + ' no longer in the map' : '');
    });
    async function verify() {
        if (!api.maptool) { S.report = 'no helper (quark_maptool) on this page'; render(); return; }
        status.textContent = 'Checking...';
        try {
            const m = api.getMap(); for (const k of ['generated', 'materials', 'player']) delete m[k];
            const a = await api.maptool('check', { map: m });
            const failed = (a.checks || []).filter((c) => !c.pass);
            S.report = (a.error ? 'check failed: ' + a.error : failed.length ? 'checks failing:\n' + failed.map((c) => '  ' + c.name + ': ' + c.detail).join('\n') : 'every check passes (' + (a.checks || []).length + ')');
        } catch (e) { S.report = 'check failed: ' + e.message; }
        status.textContent = ''; render();
    }
    function addRule(r) { S.deco.rules.push(r); S.cur = r.id; save(); render(); }
    function move(id, d) {
        const i = S.deco.rules.findIndex((r) => r.id === id), j = i + d;
        if (i < 0 || j < 0 || j >= S.deco.rules.length) return;
        const [r] = S.deco.rules.splice(i, 1); S.deco.rules.splice(j, 0, r); save(); render();
    }
    // your own presets: on disk through the server (deco_presets/user/<name>.json: GET / PUT /api/deco-presets), so they
    // survive a fresh browser profile (the Electron shell); the browser's storage only when the server can't be reached
    const fileName = (n) => (String(n).replace(/[^A-Za-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '') || 'preset').slice(0, 40);
    function storedPresets() { try { return JSON.parse(localStorage.getItem('quark.deco.presets') || '[]'); } catch (e) { return []; } }
    function localPresets() {
        const out = S.userPresets.slice();
        for (const p of storedPresets()) if (!out.some((x) => x.name === p.name)) out.push(Object.assign({}, p, { browserOnly: true }));
        return out;
    }
    async function saveUserPreset(p) {
        try {
            const r = await fetch('/api/deco-presets/' + encodeURIComponent(fileName(p.name)), { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(p) });
            const a = await r.json();
            if (!r.ok || a.error) throw new Error(a.error || r.status);
            S.report = 'preset saved: deco_presets/user/' + a.file;
            await loadUserPresets();
        } catch (e) {
            try { const l = storedPresets().filter((x) => x.name !== p.name); l.push(p); localStorage.setItem('quark.deco.presets', JSON.stringify(l)); S.report = 'the server could not save the preset (' + e.message + '): kept in this browser only'; }
            catch (e2) { S.report = 'could not save the preset: ' + e.message; }
        }
        render();
    }
    async function loadUserPresets() {
        try { const a = await (await fetch('/api/deco-presets')).json(); S.userPresets = Array.isArray(a.presets) ? a.presets : []; }
        catch (e) { S.userPresets = []; }
    }
    function download(name, obj) {
        const a = el('a', { href: URL.createObjectURL(new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' })), download: name });
        document.body.append(a); a.click(); a.remove();
    }
    async function loadPresets() {
        try {
            const idx = await (await fetch('deco_presets/index.json')).json();
            S.presets = (await Promise.all(idx.map(async (f) => { try { return Object.assign(await (await fetch('deco_presets/' + f)).json(), { file: f }); } catch (e) { return null; } }))).filter(Boolean);
        } catch (e) { S.presets = []; }
        await loadUserPresets();
        render();
    }

    // ---- forms
    function field(label, input, help) { return [el('label', { title: help || '' }, label), input]; }
    function numIn(v, on, step = 0.05, min, max) {
        return el('input', { type: 'number', value: v == null ? '' : v, step, min, max, onchange: (e) => on(e.target.value === '' ? null : Number(e.target.value)) });
    }
    function paramForm(r) {
        const g = D.generators[r.gen], f = el('div', { class: 'form' });
        for (const p of g.params) {
            const v = r.params[p.key] !== undefined ? r.params[p.key] : p.default;
            const set = (x) => { r.params[p.key] = x; save(); };
            let input;
            if (p.type === 'number') input = numIn(v, set, p.step, p.min, p.max);
            else if (p.type === 'bool') input = el('input', { type: 'checkbox', checked: v, onchange: (e) => set(e.target.checked) });
            else if (p.type === 'mat') input = el('select', { onchange: (e) => set(Number(e.target.value)) }, MATS.map((n, i) => opt(i, i + ' ' + n + (D.isSolid(i) ? '' : ' (not solid)'), v)));
            else if (p.type === 'select') input = el('select', { onchange: (e) => set(e.target.value) }, p.options.map((o) => typeof o === 'object' ? opt(o.value, o.label, v) : opt(o, o, v)));
            else if (p.type === 'shapes') {
                input = el('textarea', { rows: 3, class: 'wide', onchange: (e) => set(e.target.value.split(/\n/).map((l) => l.trim().split(/[ ,]+/).map(Number)).filter((a) => a.length >= 3 && a.slice(0, 3).every((x) => x > 0)).map((a) => ({ w: a[0], d: a[1], h: a[2], mat: a[3] | 0 }))) });
                input.value = (v || []).map((s) => [s.w, s.d, s.h, s.mat | 0].join(' ')).join('\n');
                f.append(el('label', { class: 'wide', title: p.help || '' }, p.label), input); continue;
            } else input = el('input', { type: 'text', value: v, onchange: (e) => set(e.target.value) });
            f.append(...field(p.label, input, p.help));
        }
        return f;
    }
    function targetForm(r) {
        const t = r.target || (r.target = { kind: 'all' }), f = el('div', { class: 'form' });
        const rerender = () => { save(); render(); };
        f.append(...field('Target', el('select', { onchange: (e) => { const k = e.target.value; r.target = { kind: k, filter: t.filter }; if (k === 'set') capture(); rerender(); } }, TARGETS.map(([k, l]) => opt(k, l, t.kind)))));
        const capture = () => { const m = api.getMap(), s = selection(), tag = D.tagFromSelection(m, s); r.target.brushes = tag.brushes; r.target.groups = tag.groups; };
        if (t.kind === 'set') {
            f.append(el('span', { class: 'help wide' }, (t.brushes || []).length + ' boxes, ' + (t.groups || []).length + ' groups'),
                el('div', { class: 'bar wide' }, el('button', { class: 'mini', onclick: () => { capture(); rerender(); } }, 'Use the current selection'),
                    el('button', { class: 'mini', onclick: () => api.select(D.selectionOf(api.getMap(), S.deco, r.target)) }, 'Select them')));
        } else if (t.kind === 'tag') {
            const names = Object.keys(S.deco.tags);
            f.append(...field('Tag', el('select', { onchange: (e) => { t.name = e.target.value; save(); } }, [opt('', '(choose)', t.name)].concat(names.map((n) => opt(n, n, t.name))))));
        } else if (t.kind === 'group') {
            f.append(...field('Group index', numIn(t.index != null ? t.index : 0, (v) => { t.index = v | 0; save(); }, 1, 0)));
        }
        const useF = t.kind === 'filter' || !!t.filter;
        if (t.kind !== 'filter' && t.kind !== 'all') f.append(el('label', { class: 'wide' }, el('input', { type: 'checkbox', checked: useF, onchange: (e) => { if (e.target.checked) t.filter = {}; else delete t.filter; rerender(); } }), 'narrow it with a filter'));
        if (t.kind === 'filter' || t.kind === 'all' || t.filter) {
            const F = t.filter || (t.filter = {});
            const setF = (k, v) => { if (v == null || v === '' || (Array.isArray(v) && !v.length)) delete F[k]; else F[k] = v; save(); };
            f.append(...field('Role', el('select', { onchange: (e) => setF('role', e.target.value === 'any' ? null : e.target.value) }, D.ROLES.map((x) => opt(x, x, F.role || 'any'))),
                'wall: 2 m or taller; floor: a top with standing room; ceiling: headroom under it; block: low cover; step: inside a staircase'));
            const mats = el('div', { class: 'mats wide' }, el('span', { class: 'help' }, 'materials (none ticked: every visible solid)'));
            MATS.forEach((n, i) => mats.append(el('label', {}, el('input', { type: 'checkbox', checked: (F.mats || []).includes(i), onchange: (e) => { const s = new Set(F.mats || []); if (e.target.checked) s.add(i); else s.delete(i); setF('mats', [...s].sort()); } }), n)));
            f.append(mats);
            for (const [k, l] of [['minHeight', 'Taller than (m)'], ['maxHeight', 'Lower than (m)'], ['minLength', 'Longer than (m)'], ['yMin', 'Reaching above y'], ['yMax', 'Starting below y']]) f.append(...field(l, numIn(F[k], (v) => setF(k, v), 0.1)));
            f.append(el('div', { class: 'bar wide' },
                el('button', { class: 'mini', title: 'only boxes touching the box round the current selection', onclick: () => {
                    const m = api.getMap(), s = selection(), bs = (s.brushes || []).map((i) => m.brushes[i]).filter(Boolean);
                    if (!bs.length) return;
                    F.region = { min: [0, 1, 2].map((k) => Math.min(...bs.map((b) => b.min[k]))), max: [0, 1, 2].map((k) => Math.max(...bs.map((b) => b.max[k]))) }; rerender();
                } }, F.region ? 'Region: ' + F.region.min.map(fmt).join(' ') + ' .. ' + F.region.max.map(fmt).join(' ') : 'Limit to the selection\'s bounds'),
                F.region ? el('button', { class: 'mini', onclick: () => { delete F.region; rerender(); } }, 'no region') : null));
        }
        return f;
    }
    function constraintForm(r) {
        const C = r.constraints || (r.constraints = clone(D.DEFAULT_CONSTRAINTS)), f = el('div', { class: 'form' });
        const set = (k, v) => { C[k] = v; save(); };
        f.append(...field('Symmetry', el('select', { onchange: (e) => set('symmetry', e.target.value) }, SYMS.map(([k, l]) => opt(k, l, C.symmetry || 'auto'))), 'each unit is placed with its mirror or not at all; auto: point for ranked maps and maps that are mirrored already'));
        f.append(...field('Max units (0: no limit)', numIn(C.maxCount || 0, (v) => set('maxCount', Math.max(0, v | 0)), 1, 0)));
        f.append(...field('Spawn clearance (m)', numIn(C.spawnClear, (v) => set('spawnClear', v), 0.05, 0)));
        f.append(...field('Objectives keep out', el('select', { onchange: (e) => set('objectives', e.target.value) }, [opt('all', 'everything', C.objectives), opt('solid', 'solid boxes only', C.objectives)])));
        f.append(...field('Passage margin (m)', numIn(C.passMargin, (v) => set('passMargin', v), 0.05, 0), 'solid boxes must leave a passage the player\'s width (0.8 m) and height plus this'));
        f.append(el('label', { class: 'wide' }, el('input', { type: 'checkbox', checked: C.allowBlockDrops, onchange: (e) => set('allowBlockDrops', e.target.checked) }), 'solid railings may block drops'));
        f.append(...field('Seed', el('span', {}, numIn(r.seed, (v) => { r.seed = v | 0; save(); }, 1), ' ', el('button', { class: 'mini', onclick: () => { r.seed = (Math.random() * 1e6) | 0; save(); render(); previewRules([r]); } }, 'new'))));
        return f;
    }

    // ---- the panel
    const status = el('span', { class: 'help' });
    function render() {
        const r = rule();
        const genSel = el('select', {}, Object.entries(D.generators).map(([k, g]) => opt(k, g.label, '')));
        const presetSel = el('select', {}, [opt('', '(preset)', '')].concat(S.presets.map((p, i) => opt('s' + i, p.name, ''))).concat(localPresets().map((p, i) => opt('l' + i, p.name + (p.browserOnly ? ' (mine, this browser)' : ' (mine)'), ''))));
        const presetOf = (v) => v[0] === 's' ? S.presets[+v.slice(1)] : v[0] === 'l' ? localPresets()[+v.slice(1)] : null;
        const kids = [
            el('div', { class: 'bar' }, genSel, el('button', { onclick: () => addRule(D.newRule(genSel.value)) }, 'Add rule')),
            el('div', { class: 'bar' }, presetSel,
                el('button', { title: 'add the preset\'s rules', onclick: () => { const p = presetOf(presetSel.value); if (!p) return; for (const x of p.rules) S.deco.rules.push(D.ruleFromPreset(x)); S.cur = S.deco.rules[S.deco.rules.length - 1].id; save(); render(); } }, 'Add preset'),
                el('button', { class: 'mini', title: 'import a preset .json', onclick: () => {
                    const inp = el('input', { type: 'file', accept: '.json,application/json', onchange: async () => { try { const p = JSON.parse(await inp.files[0].text()); for (const x of p.rules || []) S.deco.rules.push(D.ruleFromPreset(x)); save(); render(); } catch (e) { S.report = 'import failed: ' + e.message; render(); } } });
                    inp.click(); } }, 'Import')),
            el('h4', {}, 'Rules (run top to bottom)'),
        ];
        const list = el('div', { class: 'rules' });
        if (!S.deco.rules.length) list.append(el('div', { class: 'help', style: 'padding:6px' }, 'No rules yet: add one, or a preset.'));
        for (const x of S.deco.rules) {
            list.append(el('div', { class: 'rrow' + (x.id === S.cur ? ' on' : ''), onclick: () => { S.cur = x.id; render(); } },
                el('input', { type: 'checkbox', checked: x.enabled !== false, title: 'enabled (Regenerate all runs the enabled rules)', onclick: (e) => e.stopPropagation(), onchange: (e) => { x.enabled = e.target.checked; save(); } }),
                el('span', { class: 'n', title: (D.generators[x.gen] || {}).label }, x.name),
                el('span', { class: 'c' }, x.generated.length ? x.generated.length + ' boxes' : '-'),
                el('button', { class: 'mini', title: 'up', onclick: (e) => { e.stopPropagation(); move(x.id, -1); } }, '↑'),
                el('button', { class: 'mini', title: 'down', onclick: (e) => { e.stopPropagation(); move(x.id, 1); } }, '↓')));
        }
        kids.push(list);
        kids.push(el('div', { class: 'bar' },
            el('button', { onclick: () => previewRules(null) }, 'Preview all'),
            el('button', { class: 'primary', onclick: () => applyRules(null, 'Regenerate decorations') }, 'Regenerate all'),
            el('button', { class: 'danger', onclick: () => removeRules(null, 'Remove decorations') }, 'Remove all'),
            S.preview ? el('button', { class: 'mini', onclick: () => { clearPreview(); render(); } }, 'Clear preview') : null,
            el('button', { class: 'mini', title: 'the game\'s checks on the map as it is now (quark_maptool check)', onclick: verify }, 'Check map'), status));
        if (r) {
            const g = D.generators[r.gen] || { label: r.gen, help: 'unknown generator', params: [] };
            kids.push(el('h4', {}, g.label));
            kids.push(el('div', { class: 'form' }, ...field('Name', el('input', { type: 'text', value: r.name, onchange: (e) => { r.name = e.target.value; save(); render(); } }))));
            kids.push(el('div', { class: 'help' }, g.help));
            kids.push(el('div', { class: 'bar' },
                el('button', { onclick: () => previewRules([r]) }, 'Preview'),
                el('button', { class: 'primary', onclick: () => applyRules([r.id], 'Decorate: ' + r.name) }, r.generated.length ? 'Regenerate' : 'Apply'),
                el('button', { disabled: !r.generated.length, onclick: () => removeRules([r.id], 'Remove decoration: ' + r.name) }, 'Remove'),
                el('button', { class: 'mini', title: 'select the boxes this rule made', disabled: !r.generated.length, onclick: () => { const ix = D.decoIndices(api.getMap(), S.deco)[r.id] || []; api.select({ brushes: ix, groups: [] }); } }, 'Select made'),
                el('button', { class: 'mini', title: 'apply this rule to the current selection only (once; the rule keeps its target)', onclick: () => run('Apply to selection', async (fresh) => {
                    const one = Object.assign(clone(r), { target: Object.assign({ kind: 'set' }, D.tagFromSelection(api.getMap(), selection())) });
                    one.id = r.id; S.deco.rules[S.deco.rules.indexOf(r)] = one;
                    const a = await engine('apply', api.getMap(), S.deco, [r.id]);
                    if (!fresh()) { S.report = STALE; return; }
                    S.deco = a.deco; save(); api.setMap(a.map, 'Decorate selection: ' + r.name);
                    S.report = reportText(a.report, 'applied to the selection; the rule\'s target is now that selection'); }) }, 'Apply to selection')));
            kids.push(el('h4', {}, 'Target'), targetForm(r));
            kids.push(el('h4', {}, 'Parameters'), paramForm(r));
            kids.push(el('h4', {}, 'Constraints'), constraintForm(r));
            kids.push(el('div', { class: 'bar' },
                el('button', { class: 'mini', onclick: () => { const c = D.ruleFromPreset(D.ruleToPreset(r)); c.name = r.name + ' copy'; addRule(c); } }, 'Duplicate'),
                el('input', { type: 'text', id: 'dk-pname', placeholder: 'preset name', value: r.name, style: 'width:110px' }),
                el('button', { class: 'mini', title: 'save this rule as a preset (deco_presets/user/<name>.json)', onclick: () => { const name = (document.getElementById('dk-pname').value || '').trim() || r.name; saveUserPreset({ name, description: '', rules: [D.ruleToPreset(r)] }); } }, 'Save as preset'),
                el('button', { class: 'mini', title: 'save every rule as one preset under that name', onclick: () => { const name = (document.getElementById('dk-pname').value || '').trim() || 'my preset'; saveUserPreset({ name, description: '', rules: S.deco.rules.map(D.ruleToPreset) }); } }, 'Save all as preset'),
                el('button', { class: 'mini', title: 'download every rule as a preset .json', onclick: () => download('decoration_preset.json', { name: 'exported', description: '', rules: S.deco.rules.map(D.ruleToPreset) }) }, 'Export'),
                el('button', { class: 'mini danger', onclick: () => {
                    if (r.generated.length && !(typeof G.confirm === 'function' ? G.confirm('Delete the rule and remove the ' + r.generated.length + ' boxes it made?') : true)) return;
                    if (r.generated.length) { const a = D.remove(api.getMap(), S.deco, [r.id]); S.deco = a.deco; api.setMap(a.map, 'Remove decoration: ' + r.name); }
                    S.deco.rules = S.deco.rules.filter((x) => x.id !== r.id); S.cur = null; save(); render(); } }, 'Delete rule')));
        }
        // tags
        const tagName = el('input', { type: 'text', placeholder: 'tag name' });
        kids.push(el('h4', {}, 'Tags (saved selections)'));
        kids.push(el('div', { class: 'bar' }, tagName, el('button', { onclick: () => {
            const n = tagName.value.trim(); if (!n) return;
            S.deco.tags[n] = D.tagFromSelection(api.getMap(), selection()); save(); render(); } }, 'Save selection')));
        for (const [n, t] of Object.entries(S.deco.tags)) kids.push(el('div', { class: 'bar' }, el('span', { class: 'n' }, n + ' '), el('span', { class: 'help' }, t.brushes.length + ' boxes, ' + (t.groups || []).length + ' groups'),
            el('button', { class: 'mini', onclick: () => api.select(D.selectionOf(api.getMap(), S.deco, { kind: 'tag', name: n })) }, 'Select'),
            el('button', { class: 'mini', title: 'add the selection to the tag', onclick: () => { const x = D.tagFromSelection(api.getMap(), selection()); t.brushes.push(...x.brushes); t.groups = [...new Set((t.groups || []).concat(x.groups))]; save(); render(); } }, '+ sel'),
            el('button', { class: 'mini danger', onclick: () => { delete S.deco.tags[n]; save(); render(); } }, 'x')));
        if (S.report) kids.push(el('h4', {}, 'Report'), el('div', { class: 'rep' }, S.report));
        box.replaceChildren(...kids);
    }
    const offs = [];
    if (api.on) {
        // a map change: the sidecar entry is read again only when it changed (it holds every generated box: a copy
        // per nudge would cost Midtown ms each), the panel redraws when it is in sight
        offs.push(api.on('map', (keys) => {
            mapGen++;
            if (!keys || keys.includes('sc.deco')) { S.deco = D.normDeco(sc.get('deco')); if (!rule()) S.cur = null; }
            if (S.preview) clearPreview();
            if (root.offsetParent !== null) render(); else S.stale = true;
        }));
        offs.push(api.on('rtab', (t) => { if (t === 'deco' && S.stale) { S.stale = false; render(); } }));
    }
    mounted = { destroy() { if (worker) worker.terminate(); clearPreview(); for (const f of offs) if (typeof f === 'function') f(); root.replaceChildren(); } };
    render();
    loadPresets();
};
})(typeof globalThis !== 'undefined' ? globalThis : window);
