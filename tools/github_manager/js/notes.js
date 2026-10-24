// Notes, the Editor's first section: q's notes on a branch / PR / run / commit / version / local copy, or general
// ones. Read from /api/notes (ETag), written with POST / PUT / DELETE; the server keeps editor/notes.json (+ .bak),
// writes editor/notes.md for other tools and logs every change in the history. Also the pieces the other views
// show: the note badge on rows, the drawer's notes block, the note cards and forms, a safe little markdown.
import { html, raw, esc, relOnly, fmtDate, zoneName, enc, plural, prChip, branchChip, commitChip, versionChip, localChip } from './util.js';
import { store, BASE } from './store.js';

export const KINDS = [['general', 'General'], ['branch', 'Branch'], ['pr', 'Pull request'], ['run', 'CI run'], ['commit', 'Commit'], ['version', 'Version'], ['local', 'Local copy']];
export const KIND_LABEL = Object.fromEntries(KINDS);
const KIND_WORD = { general: 'note', branch: 'branch', pr: 'pull request', run: 'run', commit: 'commit', version: 'version', local: 'local copy' };
const TEXT_MAX = 20 * 1024;
const bytes = (s) => new TextEncoder().encode(s).length;
const size = (n) => `${n < 1024 ? `${n} B` : `${(n / 1024).toFixed(1)} KB`} / 20 KB`;

export const notes = {
  list: null,         // every note (newest change first), null until loaded
  available: null,    // false when the server has no /api/notes (an older server still running)
  loadError: null,    // the server could not read notes.json (it then loaded the .bak)
  etag: '',
  byTarget: new Map(),
};

// UI state that must survive the redraws every poll causes: open forms, their drafts, pending deletes, errors.
const ui = { open: new Set(), confirm: new Set(), busy: new Set(), errors: new Map() };
const drafts = new Map();
let hooks = { onChange() {}, toast() {} };

// ---- loading and writing ----
function accept(list) {
  notes.list = list;
  notes.byTarget = new Map();
  for (const n of list) {
    const k = `${n.target.kind}:${n.target.key}`;
    if (!notes.byTarget.has(k)) notes.byTarget.set(k, []);
    notes.byTarget.get(k).push(n);
  }
}

// -> true when the notes changed
export async function loadNotes() {
  try {
    const r = await fetch(`${BASE}/api/notes`, { cache: 'no-cache' });
    if (r.status === 404) { notes.available = false; return false; }
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const tag = r.headers.get('ETag') || '';
    if (tag && tag === notes.etag && notes.list) return false;
    const j = await r.json();
    accept(j.notes || []);
    notes.etag = tag;
    notes.available = true;
    notes.loadError = j.loadError || null;
    return true;
  } catch (e) {
    console.warn('notes:', e.message || e);
    return false;
  }
}

async function send(method, path, body) {
  const r = await fetch(BASE + path, { method, cache: 'no-store', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) });
  let j = null;
  try { j = await r.json(); } catch { /* not JSON */ }
  if (!r.ok) throw new Error((j && j.error) || `HTTP ${r.status}`);
  return j;
}
function replaceLocal(n, removeId) {
  const list = (notes.list || []).filter((x) => x.id !== (n ? n.id : removeId));
  if (n) list.unshift(n);
  notes.etag = '';  // the next poll fetches the server's copy
  accept(list);
}
export const createNote = async (body) => { const j = await send('POST', '/api/notes', body); replaceLocal(j.note); return j.note; };
export const updateNote = async (id, body) => { const j = await send('PUT', `/api/notes/${enc(id)}`, body); replaceLocal(j.note); return j.note; };
export const deleteNote = async (id) => { await send('DELETE', `/api/notes/${enc(id)}`); replaceLocal(null, id); };

// ---- lookups ----
const normKey = (kind, key) => {
  key = String(key ?? '');
  if (kind === 'branch') return key.replace(/^origin\//, '');
  if (kind === 'commit') return key.toLowerCase();
  if (kind === 'version') return key.replace(/^v/, '');
  return key;
};
export function notesFor(kind, key) {
  if (!notes.list) return [];
  key = normKey(kind, key);
  if (kind === 'commit') {
    if (key.length < 4) return [];
    return notes.list.filter((n) => n.target.kind === 'commit' && (n.target.key.startsWith(key) || key.startsWith(n.target.key)));
  }
  return notes.byTarget.get(`${kind}:${key}`) || [];
}
// pinned first, open before done, newest change first
export const sortNotes = (list) => [...list].sort((a, b) => (b.pinned - a.pinned) || ((a.status === 'done') - (b.status === 'done')) || String(b.updatedAt).localeCompare(String(a.updatedAt)));
export const openCount = () => (notes.list || []).filter((n) => n.status === 'open').length;

// ---- safe markdown: paragraphs, - / 1. lists, ``` code blocks, `code`, **bold**, [text](https://…), bare links ----
const SAFE_URL = /^(https?:\/\/[^\s]+|#[\w=&%.,/:+-]*)$/i;
function link(url, text) {
  const ext = !url.startsWith('#');
  return `<a href="${esc(url)}"${ext ? ' target="_blank" rel="noopener noreferrer"' : ''}>${text}</a>`;
}
function inline1(s) {
  const re = /`([^`]+)`|\[([^\]]+)\]\(([^)\s]+)\)|\*\*([^*]+)\*\*|(https?:\/\/[^\s<>"'`]+)/g;
  let out = '', last = 0, m;
  while ((m = re.exec(s))) {
    out += esc(s.slice(last, m.index));
    last = re.lastIndex;
    if (m[1] != null) out += `<code>${esc(m[1])}</code>`;
    else if (m[2] != null) out += SAFE_URL.test(m[3]) ? link(m[3], esc(m[2])) : esc(m[0]);
    else if (m[4] != null) out += `<b>${esc(m[4])}</b>`;
    else {
      // a bare link: trailing punctuation belongs to the sentence
      const url = m[5].replace(/[.,;:!?)\]]+$/, '');
      out += link(url, esc(url));
      last = m.index + url.length;
      re.lastIndex = last;
    }
  }
  return out + esc(s.slice(last));
}
const inline = (s) => s.split('\n').map(inline1).join('<br>');
const UL = /^\s*[-*+]\s+(.*)$/, OL = /^\s*\d+[.)]\s+(.*)$/, FENCE = /^\s*```/, HEAD = /^#{1,4}\s+(.*)$/;
export function md(text) {
  const lines = String(text || '').replace(/\r\n?/g, '\n').split('\n');
  const out = [];
  let i = 0;
  while (i < lines.length) {
    const l = lines[i];
    if (FENCE.test(l)) {
      const buf = [];
      for (i++; i < lines.length && !FENCE.test(lines[i]); i++) buf.push(lines[i]);
      i++;
      out.push(`<pre><code>${esc(buf.join('\n'))}</code></pre>`);
    } else if (!l.trim()) {
      i++;
    } else if (UL.test(l) || OL.test(l)) {
      const re = UL.test(l) ? UL : OL, items = [];
      while (i < lines.length && re.test(lines[i])) {
        items.push(re.exec(lines[i])[1]);
        // indented lines continue the item
        for (i++; i < lines.length && /^\s{2,}\S/.test(lines[i]) && !UL.test(lines[i]) && !OL.test(lines[i]); i++) items[items.length - 1] += '\n' + lines[i].trim();
      }
      const tag = re === UL ? 'ul' : 'ol';
      out.push(`<${tag}>${items.map((t) => `<li>${inline(t)}</li>`).join('')}</${tag}>`);
    } else if (HEAD.test(l)) {
      out.push(`<p class="md-h">${inline(HEAD.exec(l)[1])}</p>`);
      i++;
    } else {
      const buf = [];
      while (i < lines.length && lines[i].trim() && !FENCE.test(lines[i]) && !UL.test(lines[i]) && !OL.test(lines[i]) && !HEAD.test(lines[i])) buf.push(lines[i++]);
      out.push(`<p>${inline(buf.join('\n'))}</p>`);
    }
  }
  return raw(out.join(''));
}

// ---- pieces ----
const noteIcon = raw('<svg viewBox="0 0 16 16" width="11" height="11" aria-hidden="true"><path fill="currentColor" d="M3 1.5h7.4L13.5 4.6V14a.5.5 0 0 1-.5.5H3a.5.5 0 0 1-.5-.5V2a.5.5 0 0 1 .5-.5Zm1 1.5v10h8V5.2L9.8 3H4Zm1.5 3.2h5v1.2h-5V6.2Zm0 2.5h5v1.2h-5V8.7Z"/></svg>');

export function targetChip(t) {
  const k = t.kind, v = t.key;
  if (k === 'branch') return branchChip(v);
  if (k === 'pr') return prChip(v);
  if (k === 'run') return html`<span class="chip c-run"><a href="#run=${enc(v)}" title="CI run ${v}">run ${v}</a></span>`;
  if (k === 'commit') return commitChip(v);
  if (k === 'version') return versionChip(v);
  if (k === 'local') return localChip(v);
  return html`<span class="chip c-general"><a href="#tab=editor&kind=general">general</a></span>`;
}

// The count on a row: open notes (or, all done, how many), linking to the item's drawer where they are.
export function noteBadge(kind, key, href) {
  const ns = notesFor(kind, key);
  if (!ns.length) return '';
  const open = ns.filter((n) => n.status === 'open').length;
  const first = sortNotes(ns)[0].text.replace(/\s+/g, ' ').slice(0, 90);
  const tip = `${plural(ns.length, 'note')}${open !== ns.length ? ` (${open} open)` : ''}: ${first}`;
  return html`<a class="nbadge${open ? '' : ' all-done'}" href="${href}" title="${tip}" aria-label="${plural(ns.length, 'note')}">${noteIcon}${open || ns.length}</a>`;
}

const fieldDefaults = (o) => ({ text: '', author: 'q', pinned: false, kind: 'general', key: '', ...o });
const draftOf = (fk, defaults) => {
  if (!drafts.has(fk)) drafts.set(fk, fieldDefaults(defaults));
  return drafts.get(fk);
};

// The suggestions for the composer's target key, from what the server knows.
function keyOptions(kind) {
  const s = store.state || {};
  if (kind === 'branch') return (s.branches || []).map((b) => [b.name, b.kind]);
  if (kind === 'pr') return (s.pulls || []).map((p) => [String(p.number), `#${p.number} ${p.title}`]);
  if (kind === 'run') return (s.runs || []).slice(0, 80).map((r) => [String(r.id), `${r.workflow} on ${r.branch}: ${r.title}`]);
  if (kind === 'commit') return ((s.testing || {}).commitsSinceProduction || []).slice(0, 80).map((c) => [c.sha, c.subject]);
  if (kind === 'version') return [...new Set([(s.testing || {}).version, (s.production || {}).version, ...(s.local || []).map((l) => l.version)].filter(Boolean))].map((v) => [v, '']);
  if (kind === 'local') return (s.local || []).map((l) => [l.name, l.branch]);
  return [];
}
const KEY_HINT = { branch: 'branch name', pr: 'PR number', run: 'run id', commit: 'commit sha', version: 'e.g. 1.18.6', local: 'local copy name' };

// A note form. fk (form key) names it: "<scope>:add:<kind>:<key>", "<scope>:edit:<id>" or "e:new" (the composer,
// whose target is picked); the draft lives under it, so a redraw while typing loses nothing.
export function noteForm({ fk, note = null, kind = 'general', key = '', pickTarget = false, cancel = true }) {
  const d = draftOf(fk, note ? { text: note.text, author: note.author, pinned: note.pinned, kind: note.target.kind, key: note.target.key } : { kind, key });
  const err = ui.errors.get(fk), busy = ui.busy.has(fk);
  const n = bytes(d.text);
  const listId = `nf-keys-${fk.replace(/[^\w-]/g, '_')}`;
  return html`<form class="nform${pickTarget ? ' compose' : ''}" data-nform="${fk}" data-id="${note ? note.id : ''}" data-kind="${kind}" data-key="${key}" novalidate>
    ${pickTarget ? html`<div class="nf-target">
      <label class="f"><span class="f-l">About</span><select data-nf="kind">${KINDS.map(([v, t]) => html`<option value="${v}"${v === d.kind ? ' selected' : ''}>${t}</option>`)}</select></label>
      ${d.kind !== 'general' ? html`<label class="f nf-key"><span class="sr">${KIND_LABEL[d.kind]}</span><input type="text" data-nf="key" value="${d.key}" list="${listId}" placeholder="${KEY_HINT[d.kind]}" autocomplete="off" spellcheck="false"></label>
        <datalist id="${listId}">${keyOptions(d.kind).map(([v, t]) => html`<option value="${v}">${t}</option>`)}</datalist>` : html`<span class="muted small">not about one thing in particular</span>`}
    </div>` : ''}
    <textarea data-nf="text" rows="${pickTarget ? 4 : 3}" placeholder="Write a note. Markdown works: paragraphs, - lists, 1. lists, \`code\`, **bold**, [links](https://…)" spellcheck="true">${d.text}</textarea>
    <div class="nf-bar">
      <label class="chk"><input type="checkbox" data-nf="pinned"${d.pinned ? ' checked' : ''}> Pin to the Overview</label>
      <label class="f"><span class="f-l">By</span><input type="text" data-nf="author" value="${d.author}" size="8" maxlength="80" autocomplete="off"></label>
      <span class="nf-count small${n > TEXT_MAX ? ' err' : ' muted'}" data-nf-count>${size(n)}</span>
      <span class="grow"></span>
      <span class="muted small nf-key-hint">Ctrl+Enter saves</span>
      <span class="nf-btns">${cancel ? html`<button type="button" data-nact="cancel">Cancel</button>` : ''}<button type="submit" class="primary"${busy ? ' disabled' : ''}>${busy ? 'Saving…' : note ? 'Save' : 'Add note'}</button></span>
    </div>
    ${err ? html`<p class="msg bad small nf-err" role="alert">${err}</p>` : ''}
  </form>`;
}

// One note: target, status, who / when, the text, and its actions. scope: "d" in the drawer, "e" in the Editor tab,
// "o" on the Overview (read-only there: the card links to the Editor).
export function noteCard(n, { scope, showTarget = true, actions = true } = {}) {
  const fk = `${scope}:edit:${n.id}`;
  if (ui.open.has(fk)) return html`<li class="note editing" id="${scope}-note-${n.id}">${noteForm({ fk, note: n })}</li>`;
  const done = n.status === 'done', ck = `${scope}:${n.id}`;
  const edited = n.updatedAt && n.updatedAt !== n.createdAt;
  return html`<li class="note${n.pinned ? ' is-pinned' : ''}${done ? ' is-done' : ''}" id="${scope}-note-${n.id}" data-note="${n.id}">
    <div class="n-head">
      ${showTarget ? targetChip(n.target) : ''}
      ${done ? html`<span class="tag t-clean">✓ done</span>` : ''}
      ${n.pinned ? html`<span class="tag t-pin" title="Pinned: shown on the Overview">pinned</span>` : ''}
      <span class="n-by muted small" title="written ${fmtDate(n.createdAt)}${edited ? `, changed ${fmtDate(n.updatedAt)}` : ''} ${zoneName()} (Dallas)">${n.author} · ${relOnly(n.updatedAt || n.createdAt)}${edited ? ' · edited' : ''}</span>
      <span class="grow"></span>
      ${actions ? html`<span class="n-acts">${ui.confirm.has(ck)
        ? html`<span class="n-confirm" role="alert">Delete this note? It stays in the history. <button type="button" class="small danger" data-nact="delete-yes" data-id="${n.id}" data-ck="${ck}">Delete</button><button type="button" class="small" data-nact="delete-no" data-ck="${ck}">Keep</button></span>`
        : html`<button type="button" class="small" data-nact="edit" data-fk="${fk}">Edit</button><button type="button" class="small" data-nact="${done ? 'reopen' : 'done'}" data-id="${n.id}">${done ? 'Reopen' : 'Done'}</button><button type="button" class="small" data-nact="${n.pinned ? 'unpin' : 'pin'}" data-id="${n.id}">${n.pinned ? 'Unpin' : 'Pin'}</button><button type="button" class="small" data-nact="delete" data-ck="${ck}" title="Delete (asks first)">Delete</button>`}</span>` : ''}
    </div>
    <div class="n-text md">${md(n.text)}</div>
  </li>`;
}

// The drawer's notes: every note on this item, and "add note".
export function notesBlock(kind, key) {
  if (notes.available === false) return html`<h3>Notes</h3><p class="muted small">The server running now has no notes yet: restart the GitHub manager to use them.</p>`;
  key = normKey(kind, key);
  const ns = sortNotes(notesFor(kind, key));
  const fk = `d:add:${kind}:${key}`, adding = ui.open.has(fk);
  return html`<section class="notes-block">
    <h3>Notes ${ns.length ? html`<span class="count">${ns.length}</span>` : ''}<span class="grow"></span>${adding ? '' : html`<button type="button" class="small" data-nact="add" data-fk="${fk}">+ Add note</button>`}</h3>
    ${adding ? noteForm({ fk, kind, key }) : ''}
    ${ns.length ? html`<ul class="notes">${ns.map((n) => noteCard(n, { scope: 'd', showTarget: false }))}</ul>` : adding ? '' : html`<p class="muted small">No notes on this ${KIND_WORD[kind]}.</p>`}
  </section>`;
}

// ---- focus: a redraw replaces the forms; put the caret back where it was ----
export function captureFocus() {
  const a = document.activeElement;
  const f = a && a.closest && a.closest('[data-nform]');
  if (!f || !a.dataset.nf) return null;
  return { fk: f.dataset.nform, nf: a.dataset.nf, s: a.selectionStart, e: a.selectionEnd, top: a.scrollTop };
}
export function restoreFocus(c) {
  if (!c) return;
  const f = document.querySelector(`[data-nform="${CSS.escape(c.fk)}"] [data-nf="${c.nf}"]`);
  if (!f || f === document.activeElement) return;
  f.focus({ preventScroll: true });
  try { if (c.s != null) f.setSelectionRange(c.s, c.e); } catch { /* not a text field */ }
  f.scrollTop = c.top;
}
function focusForm(fk) {
  requestAnimationFrame(() => {
    const t = document.querySelector(`[data-nform="${CSS.escape(fk)}"] textarea`);
    if (t) { t.focus(); t.setSelectionRange(t.value.length, t.value.length); }
  });
}

// ---- behaviour (delegated, set up once) ----
function readForm(form) {
  const fk = form.dataset.nform, d = drafts.get(fk) || fieldDefaults({ kind: form.dataset.kind, key: form.dataset.key });
  for (const n of form.querySelectorAll('[data-nf]')) d[n.dataset.nf] = n.type === 'checkbox' ? n.checked : n.value;
  drafts.set(fk, d);
  return d;
}
async function act(fk, run, okText) {
  ui.busy.add(fk);
  ui.errors.delete(fk);
  hooks.onChange();
  try {
    await run();
    if (okText) hooks.toast(okText);
    return true;
  } catch (e) {
    ui.errors.set(fk, String(e.message || e));
    hooks.toast(`Not saved: ${e.message || e}`, true);
    return false;
  } finally {
    ui.busy.delete(fk);
    hooks.onChange();
  }
}
async function submit(form) {
  const fk = form.dataset.nform, id = form.dataset.id;
  const d = readForm(form);
  const kind = fk === 'e:new' ? d.kind : form.dataset.kind;
  const key = fk === 'e:new' ? d.key.trim() : form.dataset.key;
  let problem = '';
  if (!d.text.trim()) problem = 'Write something first.';
  else if (bytes(d.text) > TEXT_MAX) problem = 'A note can be at most 20 KB.';
  else if (!id && kind !== 'general' && !key) problem = `Say which ${KIND_WORD[kind]} it is about.`;
  if (problem) { ui.errors.set(fk, problem); hooks.onChange(); return; }
  const fields = { text: d.text, pinned: !!d.pinned, author: d.author.trim() || 'q' };
  let saved = null;
  const ok = await act(fk, async () => {
    saved = id ? await updateNote(id, { ...fields, rev: notes.list.find((n) => n.id === id)?.rev })
      : await createNote({ ...fields, target: { kind, key: kind === 'general' ? '' : key } });
  }, id ? 'Note saved' : 'Note added');
  if (!ok) return;
  if (fk === 'e:new') drafts.set(fk, fieldDefaults({ kind: d.kind, key: d.key, author: d.author }));
  else { drafts.delete(fk); ui.open.delete(fk); }
  hooks.onChange();
  if (saved && fk === 'e:new') focusForm(fk);
}

export function initNotes(h) {
  hooks = { ...hooks, ...h };
  document.addEventListener('click', async (e) => {
    const b = e.target.closest('[data-nact]');
    if (!b) return;
    e.preventDefault();
    const a = b.dataset.nact, id = b.dataset.id;
    const form = b.closest('[data-nform]');
    if (a === 'add' || a === 'edit') {
      ui.open.add(b.dataset.fk);
      hooks.onChange();
      focusForm(b.dataset.fk);
    } else if (a === 'cancel' && form) {
      const fk = form.dataset.nform;
      ui.open.delete(fk); drafts.delete(fk); ui.errors.delete(fk);
      hooks.onChange();
    } else if (a === 'delete') {
      ui.confirm.add(b.dataset.ck);
      hooks.onChange();
    } else if (a === 'delete-no') {
      ui.confirm.delete(b.dataset.ck);
      hooks.onChange();
    } else if (a === 'delete-yes') {
      ui.confirm.delete(b.dataset.ck);
      await act(b.dataset.ck, () => deleteNote(id), 'Note deleted (it stays in the History tab)');
    } else if (['done', 'reopen', 'pin', 'unpin'].includes(a)) {
      const n = (notes.list || []).find((x) => x.id === id);
      if (!n) return;
      const change = a === 'done' ? { status: 'done' } : a === 'reopen' ? { status: 'open' } : { pinned: a === 'pin' };
      await act(`x:${id}`, () => updateNote(id, { ...change, rev: n.rev }), { done: 'Marked done', reopen: 'Reopened', pin: 'Pinned to the Overview', unpin: 'Unpinned' }[a]);
    }
  });
  document.addEventListener('submit', (e) => {
    const form = e.target.closest('[data-nform]');
    if (!form) return;
    e.preventDefault();
    submit(form);
  });
  document.addEventListener('input', (e) => {
    const form = e.target.closest && e.target.closest('[data-nform]');
    if (!form) return;
    const d = readForm(form);
    const c = form.querySelector('[data-nf-count]');
    if (c) { const n = bytes(d.text); c.textContent = size(n); c.className = `nf-count small${n > TEXT_MAX ? ' err' : ' muted'}`; }
    if (e.target.dataset.nf === 'kind') { d.key = ''; hooks.onChange(); }
  });
  document.addEventListener('keydown', (e) => {
    const form = e.target.closest && e.target.closest('[data-nform]');
    if (form && e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); form.requestSubmit(); }
  });
}
