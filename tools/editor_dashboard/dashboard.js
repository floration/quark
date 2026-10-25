/* Quark Editor Manager page: the editors only (GitHub, Claude Documents, game code and tooling are the Supervisor
   Board's). Talks only to dashboard.py on 127.0.0.1:
     GET  /api/status /api/changes /api/assets      the cached snapshot (dashboard.py builds it at start, on Refresh, and
                                                    by itself when a sidecar, the registry or a clone's HEAD changes)
     GET  /api/stamp                                polled every few s while the page is visible: did the snapshot change
     GET  /api/links                                the nav bar: every editor's link, up / down (checked live)
     GET  /api/preview?id=<asset id>                an asset's sound / picture (the list's play / view button)
     POST /api/refresh {fetch}                      rebuild it (fetch: git fetch in each clone first)
     POST /api/mark {ids, origin, by, note | clear} the dashboard's own sidecar (never a git clone)
     POST /api/merge {confirm: true}                tools/provenance.py merge, run hidden in the registry clone
   Views render with innerHTML from plain data; the asset list is virtualized (only the rows on screen are in the DOM),
   other long lists show their first LIST_MAX entries until "show all". Inside the Quark Project app (window.quarkShell)
   an editor link switches to that editor's tab instead of opening a window. */
"use strict";

const LIST_MAX = 60;
const ORIGINS = ["human", "third-party", "ai"];
const ORIGIN_LABEL = { human: "human", "third-party": "third-party", ai: "AI" };
const STATUS_LABEL = {
  local: ["only local", "the change is not in testing yet"],
  testing: ["in testing", "in testing, not in production yet"],
  both: ["in both", "already in testing and production"],
  none: ["no net change", "changed by a commit, then changed back"],
  unc: ["uncommitted", "not committed in this clone"],
};
// the version of an editor the Quark Project app installed, compared with testing's (installs-state.json)
const INSTALL_LABEL = {
  current: ["testing's", "st-both", "installed from testing's tip"],
  head: ["repository's", "st-testing", "installed from repository\\'s HEAD; testing is newer: the app updates it on its next pass"],
  older: ["older than testing", "st-local", "installed from an older testing: the app updates it on its next pass (or Update now)"],
  failed: ["update failed", "st-local", "the app's last try to install testing's failed: see its log in the Supervisor Board"],
  never: ["not by the app", "st-none", "the Quark Project app has no record of installing it"],
  waiting: ["not in testing yet", "st-none", "its source isn't in testing yet"],
  unknown: ["?", "st-none", "no git clone to compare with"],
};
const ROW_H = 30;
// a sidecar entry's place in the rule that decides the item (dashboard.py decide())
const PEND_ST = { win: "wins", same: "agrees", over: "overruled by your mark", conflict: "CONFLICT: disagrees and isn't decided (mark the item, or remove your mark)" };
const MARK_CONFIRM = 20;                                                // (marking more items at once asks first)

const S = {
  summary: null, changes: null, assets: null,
  tab: "changes", program: "overview", expanded: new Set(),
  filt: { assets: { q: "", origin: "", cat: "", ed: "", pending: false }, remaining: { q: "", origin: "ai", cat: "", ed: "", pending: false } },
  sort: { assets: { k: "id", d: 1 }, remaining: { k: "id", d: 1 } },
  sel: new Set(), lastIdx: -1, markOrigin: "human", rows: [], playing: null, mergeOut: null,
};
const SHELL = window.quarkShell && typeof window.quarkShell.open === "function" ? window.quarkShell : null;

// ---- small helpers --------------------------------------------------------------------------------------------------
const $ = (s, el = document) => el.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const store = {
  get(k, d) { try { const v = localStorage.getItem("qdash." + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem("qdash." + k, JSON.stringify(v)); } catch { /* private window: fine */ } },
};
function ago(t) {
  if (!t) return "";
  const s = Date.now() / 1000 - t;
  if (s < 90) return "just now";
  if (s < 5400) return Math.round(s / 60) + " min ago";
  if (s < 129600) return Math.round(s / 3600) + " h ago";
  if (s < 86400 * 60) return Math.round(s / 86400) + " d ago";
  return new Date(t * 1000).toISOString().slice(0, 10);
}
const when = (t) => t ? `<span title="${esc(new Date(t * 1000).toLocaleString())}">${esc(ago(t))}</span>` : "";
function stamp(t) { const d = new Date(t * 1000), p = (n) => String(n).padStart(2, "0"); return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`; }
const size = (n) => n < 1024 ? n + " B" : n < 1048576 ? (n / 1024).toFixed(1) + " KB" : (n / 1048576).toFixed(1) + " MB";
// human-made % rounded toward "not done yet", as tools/provenance.py summary does: 100 only when all are, 0 only when none
function pct(h, n) {
  if (!n) return "-";
  if (h === n) return "100";
  let v = Math.floor(h * 1000 / n) / 10;
  if (h && v === 0) v = 0.1;
  return v.toFixed(1);
}
const aiPct = (ai, n) => !n ? "-" : ai === 0 ? "0" : (100 - +pct(n - ai, n)).toFixed(1);
const plural = (n, w, many) => n + " " + (n === 1 ? w : many || w + "s");
function st(s) { const [l, t] = STATUS_LABEL[s] || [s, ""]; return `<span class="st st-${esc(s)}" title="${esc(t)}">${esc(l)}</span>`; }
function og(o) { return o ? `<span class="og og-${esc(o)}">${esc(ORIGIN_LABEL[o] || o)}</span>` : `<span class="og og-pending">new</span>`; }
function bar(h, t, a) {
  const n = h + t + a || 1;
  return `<div class="bar" title="human ${h}, third-party ${t}, AI ${a}"><i class="h" style="width:${100 * h / n}%"></i><i class="t" style="width:${100 * t / n}%"></i><i class="a" style="width:${100 * a / n}%"></i></div>`;
}
let toastTimer = 0;
function toast(msg, bad) {
  const t = $("#toast");
  t.textContent = msg; t.className = bad ? "toast bad" : "toast"; t.hidden = false;
  clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.hidden = true; }, bad ? 6000 : 2500);
}
async function api(path, body) {
  const r = await fetch(path, body === undefined ? {} : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const j = await r.json();
  if (!r.ok) { const e = new Error(j.error || r.statusText); e.body = j; throw e; }
  return j;
}

// ---- loading --------------------------------------------------------------------------------------------------------
async function loadAll() {
  const [summary, changes, assets] = await Promise.all([api("/api/status"), api("/api/changes"), api("/api/assets")]);
  Object.assign(S, { summary, changes, assets });
  indexAssets();
  renderHeader(); renderNav(summary.links); render();
  checkLinks();                                                         // (live: the snapshot's may be old)
}
let busy = false;                                                       // (a Refresh / mark / merge of this page runs)
async function refresh() {
  const b = $("#refresh"), fetchToo = $("#fetch").checked;
  b.disabled = true; b.textContent = fetchToo ? "Fetching..." : "Refreshing..."; busy = true;
  try {
    await api("/api/refresh", { fetch: fetchToo });
    await rerender(loadAll);
    shellStats();
    const fe = S.summary.fetchErrors || [];
    toast(fe.length ? "fetch failed in: " + fe.join(", ") : "Refreshed in " + (S.summary.ms / 1000).toFixed(2) + " s", fe.length > 0);
  } catch (e) { toast("Refresh failed: " + e.message, true); }
  b.disabled = false; b.textContent = "Refresh"; busy = false;
}
// Re-render after new data without losing the place: the scroll positions, the search box's focus and caret, the mark
// bar's note. (dashboard.py rebuilds the snapshot by itself when an editor writes its sidecar or q commits; the page
// polls /api/stamp and comes here.)
async function rerender(load) {
  const pane = $("#pane"), body = $("#vbody"), act = document.activeElement;
  const keep = { pane: pane && pane.scrollTop, body: body && body.scrollTop, focus: act && act.id, caret: act && act.selectionStart,
                 note: ($('#markbar input[name="note"]') || {}).value };
  await load();
  if (keep.pane != null && $("#pane")) $("#pane").scrollTop = keep.pane;
  if (keep.body != null && $("#vbody")) { $("#vbody").scrollTop = keep.body; drawRows(); }
  if (keep.note && $('#markbar input[name="note"]')) $('#markbar input[name="note"]').value = keep.note;
  const f = keep.focus && document.getElementById(keep.focus);
  if (f && f.matches("input[type=search], input[type=text]")) { f.focus(); try { f.setSelectionRange(keep.caret, keep.caret); } catch { /* (not a text box) */ } }
}
let polling = false;
async function poll() {
  if (polling || busy || document.hidden || !S.summary) return;
  polling = true;
  try {
    const s = await api("/api/stamp");
    $("#cost").classList.toggle("busy", !!s.refreshing);
    if (s.refreshing) $("#cost").textContent = "refreshing...";
    if (s.refreshed !== S.summary.refreshed) { await rerender(loadAll); shellStats(); }
    else if (s.assetsAt !== S.summary.assetsAt) {
      await rerender(async () => {
        const [summary, assets] = await Promise.all([api("/api/status"), api("/api/assets")]);
        Object.assign(S, { summary, assets }); indexAssets(); renderHeader(); render();
      });
      shellStats();
    } else if (!s.refreshing) renderCost();
  } catch { /* the server is gone: the next Refresh says so */ }
  polling = false;
}

// ---- the nav bar and the editor links ---------------------------------------------------------------------------------
// The Supervisor Board (8768) is a sibling local server: dashboard.py checks it answers and the bar dims it when it
// doesn't. A server editor (map, bot, gamemode, physics, weapons) runs its own serve.py on the port the Quark Project
// app's tabs.json gives it: linked there, dimmed while it doesn't answer. A page editor (sound, UI, model) is served by
// dashboard.py under /editors/<folder>/ (Chrome / Edge don't follow a file:// link from an http page). In the app, an
// editor link opens the app's tab for it (the app starts a server editor that isn't running).
let linksAt = 0;
const sib = (id) => ((S.links || {}).siblings || []).find((x) => x.id === id);
const edOf = (folder) => ((S.links || {}).editors || []).find((x) => x.folder === folder);
for (const a of document.querySelectorAll("#qtnav [data-self]")) a.href = location.origin + "/";   // (bind() may take another port)
function edTitle(e) {
  if (!e.kind) return e.note || "no link";
  if (e.kind === "page") return `${e.file} (served from here: its browser autosave belongs to this address)`;
  return e.up ? `${e.url} (its own server)` : downText(e);
}
function edLink(e, label, cls = "") {
  if (!e.kind) return `<span class="muted" title="${esc(edTitle(e))}">${esc(label)}</span>`;
  return `<a href="${esc(e.url)}" data-ed="${esc(e.folder)}" target="quark-${esc(e.folder)}" class="${cls}${e.up ? "" : " down"}" title="${esc(SHELL && e.tab ? "open its tab" : edTitle(e))}">${esc(label)}</a>`;
}
// up / down: a dot and a word (a page editor is "ready" when its page is installed)
function edUp(e) {
  if (!e || !e.installed) return `<span class="updot off"></span><span class="muted">not installed</span>`;
  if (!e.kind) return `<span class="updot off"></span><span class="muted" title="${esc(e.note || "")}">no link</span>`;
  if (e.kind === "page") return `<span class="updot ${e.up ? "on" : "off"}"></span>${e.up ? "page" : `<span class="warn">page missing</span>`}`;
  return `<span class="updot ${e.up ? "on" : "off"}"></span>${e.up ? `running <span class="muted mono small">:${e.port}</span>` : `<span class="muted" title="${esc(downText(e))}">not running</span>${SHELL && e.tab ? "" : ` <button class="mini" data-start="${esc(e.folder)}" ${starting.has(e.folder) ? "disabled" : ""} title="start ${esc(e.program)} hidden, on port ${e.port}">${starting.has(e.folder) ? "starting..." : "Start"}</button>`}`}`;
}
function instChip(inst) {
  if (!inst) return `<span class="muted small">-</span>`;
  const [l, cls, why] = INSTALL_LABEL[inst.state] || [inst.state, "st-none", ""];
  const tip = `${inst.name} (installs.json "${inst.entry}"): ${why}${inst.at ? "\nlast run " + new Date(inst.at).toLocaleString() : ""}`;
  return `<span class="st ${cls}" title="${esc(tip)}">${esc(l)}${inst.version ? " " + esc(inst.version) : ""}</span>`;
}
function renderNav(L) {
  if (!L) return;
  S.links = L;
  const notes = [];
  for (const x of L.siblings) {
    const a = document.querySelector(`#qtnav [data-tool="${x.id}"]`);
    if (!a) continue;
    a.classList.toggle("down", !x.up);
    a.title = x.up ? x.url : downText(x);
    if (!x.up) notes.push(`${x.label}: ${downText(x)}`);
  }
  $("#navNote").textContent = notes.join(" · ");
  $("#navEditors").innerHTML = L.editors.filter((e) => e.installed).map((e) => edLink(e, e.program)).join("");
}
async function checkLinks() {
  linksAt = Date.now();
  try {
    renderNav(await api("/api/links"));
    if (S.tab === "changes" && $("#pane")) { const sc = $("#pane").scrollTop; renderChanges(); $("#pane").scrollTop = sc; }
  } catch { /* the server is gone: loadAll says so */ }
}
// (no console command: the Quark Project app starts its servers, and a server editor has a Start button here)
const downText = (x) => x.folder ? "not running: Start starts it here (hidden), or open its tab in the Quark Project app"
  : "not running: the Quark Project app starts it";
function openEditor(ev, a) {
  const e = edOf(a.dataset.ed);
  if (!e) return;
  if (SHELL && e.tab) { ev.preventDefault(); try { SHELL.open(e.tab, ""); } catch { /* (the shell logs it) */ } return; }
  if (e.kind === "server" && !e.up) {                                   // (down when last checked: look again first)
    ev.preventDefault();
    // (a window opened now, before the await, so a pop-up blocker allows it; it goes to the editor once it answers)
    const win = window.open("about:blank", a.target || "_blank");
    try { win.document.title = e.program; win.document.body.textContent = `Starting ${e.program}...`; } catch { /* (blocked) */ }
    api("/api/links").then((L) => {
      renderNav(L);
      const now = edOf(e.folder);
      if (now && now.up) { if (win) win.location = now.url; else window.open(now.url, a.target || "_blank"); return; }
      startEditor(e.folder, win);
    }).catch(() => { if (win) win.close(); });
  }
}
// Start: dashboard.py starts a stopped server editor hidden, the way the Quark Project app does (POST /api/start)
const starting = new Set();
async function startEditor(folder, win) {
  const e = edOf(folder);
  if (!e || starting.has(folder)) return;
  starting.add(folder); toast(`Starting ${e.program}...`);
  if ($("#pane")) renderChanges();
  let r;
  try { r = await api("/api/start", { folder }); } catch (x) { r = x.body || { ok: false, message: x.message }; }
  starting.delete(folder);
  if (r.links) renderNav(r.links);
  const now = edOf(folder);
  if (r.ok && now && now.up) { if (win) win.location = now.url; toast(r.message); }
  else { if (win) win.close(); toast(r.message || r.error || "Start failed", true); }
  if (S.tab === "changes" && $("#pane")) { const sc = $("#pane").scrollTop; renderChanges(); $("#pane").scrollTop = sc; }
}

// ---- deep links: #view=changes&program=<id> (an editor's folder name), #view=assets&category=<cat>&origin=<o>&editor=<id>
// &q=<text>&asset=<id>, #view=remaining&category=, #asset=<id> (and the old #changes / #assets / #remaining). The hash
// follows what is on screen (replaceState); one selected asset is in it, so a reload or a copied link selects it again.
const VIEWS = ["changes", "assets", "remaining"];
const PROGRAM_ALIAS = { "ui-editor": "ui-customizer" };               // (the UI editor's id is its folder's name)
const progId = (s) => { const v = String(s || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, ""); return PROGRAM_ALIAS[v] || v; };
function applyHash() {
  const raw = location.hash.slice(1);
  if (!raw) return false;
  if (VIEWS.includes(raw)) { S.tab = raw; return true; }
  const p = new URLSearchParams(raw);
  const v = p.get("view");
  if (!v && p.has("asset")) {
    S.tab = "assets"; Object.assign(S.filt.assets, { q: p.get("asset"), origin: "", cat: "", ed: "", pending: false });
    S.focusAsset = p.get("asset"); return true;
  }
  if (!VIEWS.includes(v)) return false;
  S.tab = v;
  if (v === "changes") { if (p.has("program")) S.program = progId(p.get("program")) || "overview"; }
  else {
    const f = S.filt[v];
    f.cat = p.get("category") || ""; f.q = p.get("q") || ""; f.pending = p.get("pending") === "1"; f.ed = progId(p.get("editor") || "");
    if (v === "assets") f.origin = p.get("origin") || "";
    if (p.has("asset")) S.focusAsset = p.get("asset");
  }
  return true;
}
function writeHash() {
  const p = new URLSearchParams({ view: S.tab });
  if (S.tab === "changes") p.set("program", S.program);
  else {
    const f = S.filt[S.tab];
    if (f.cat) p.set("category", f.cat);
    if (S.tab === "assets" && f.origin) p.set("origin", f.origin);
    if (f.ed) p.set("editor", f.ed);
    if (f.q) p.set("q", f.q);
    if (f.pending) p.set("pending", "1");
    if (S.sel.size === 1) p.set("asset", [...S.sel][0]);
  }
  const h = "#" + p.toString();
  if (location.hash !== h) history.replaceState(null, "", h);
}

// ---- header ---------------------------------------------------------------------------------------------------------
// In the Quark Project app (?embed=1) its tab bar shows Testing / Production / Human-made / Last refresh (from
// /api/summary), so the page drops its header row: Refresh and fetch move to the right of the view tabs, and the row
// comes back only for the "no git clones" error. shellStats() asks the app to re-read the summary after a refresh / mark.
const EMBED = document.documentElement.classList.contains("embed");
if (EMBED) $("#tabs").append($("#top .actions"));
function shellStats() {
  try { if (window.quarkShell && typeof window.quarkShell.statsChanged === "function") window.quarkShell.statsChanged(); } catch (e) { /* (an older app) */ }
}
function renderHeader() {
  const s = S.summary, h = s.human, c = h.counts;
  $("#top").classList.toggle("alert", !!s.noClones);
  const ver = (x) => x.version ? `<b>${esc(x.version)}</b>` : `<b class="muted">none</b>`;
  const build = s.testing.build ? ` <span class="muted small" title="the testing build of this commit">${esc(s.testing.build.replace(/^.*?(-test\.)/, "$1"))}</span>` : "";
  $("#facts").innerHTML = `
    <div class="fact"><span class="k">Testing</span><span>${ver(s.testing)}${build}</span><span class="muted small mono" title="${esc(s.testing.ref || "no origin/testing")}">${esc(s.testing.sha || "no origin/testing")}</span></div>
    <div class="fact"><span class="k">Production</span>${ver(s.production)}<span class="muted small mono">${esc(s.production.tag || "no v<semver> tag")}</span></div>
    <div class="fact"><span class="k">Human-made</span><b>${h.humanPct == null ? "-" : h.humanPct + " %"}</b>${bar(c.human, c["third-party"], c.ai)}</div>
    <div class="fact"><span class="k">Last refresh</span><b>${esc(new Date(s.refreshed * 1000).toLocaleTimeString())}</b><span class="muted small">${s.fetched ? "with fetch" : s.auto ? "by itself, no fetch" : "no fetch"}</span></div>
    ${s.noClones ? `<div class="fact err">no git clones found in ${esc(s.quark)} (use --quark)</div>` : ""}`;
  renderCost();
  const own = S.assets ? S.assets.ownPending : 0;
  $('#tabs [data-tab="assets"]').innerHTML = "Assets: AI vs human" + (own ? `<span class="n" title="your marks not in the repo yet">${own}</span>` : "");
}
function renderCost() {
  const s = S.summary;
  $("#cost").classList.remove("busy");
  $("#cost").textContent = `${s.ms} ms, ${s.gitCalls} git calls${s.auto ? " (by itself)" : ""} · ${s.quark}`;
}

function setTab(t) {
  S.tab = t; store.set("tab", t);
  for (const b of document.querySelectorAll("#tabs [data-tab]")) b.classList.toggle("on", b.dataset.tab === t);
  render();
}
function render() {
  if (!S.changes) return;
  if (S.tab === "changes") renderChanges(); else renderAssets(S.tab);
}

// ---- per editor: counts, human-made % --------------------------------------------------------------------------------
const counted = (p) => p.clones.filter((c) => !c.stale);               // (a stale clone's counts can't be trusted)
const PC_OVERRIDES = "this PC: overrides";                            // (dashboard.py's name for the game's overrides folder)
function progCounts(p) {
  const local = p.files.filter(([, sts]) => Object.values(sts).includes("local")).length;
  // (a commit whose change testing already has, cherry-picked or reverted, isn't local work: net false)
  const commits = new Set();
  for (const x of counted(p)) for (const cm of x.commits) if (cm.net !== false) commits.add(cm.sha);
  const pc = p.exports.filter((e) => e.source === PC_OVERRIDES).length;
  return { local, commits: commits.size, unc: counted(p).reduce((n, x) => n + x.uncommitted.length, 0), testing: p.testing.length,
           exports: p.exports.length, pcExports: pc };
}
// the registry items in the categories this editor makes (CATEGORY_EDITOR in dashboard.py)
function edHuman(id) {
  const A = S.assets, r = { human: 0, "third-party": 0, ai: 0, n: 0 };
  for (const [c, s] of Object.entries(A.stats)) {
    if ((A.catEditor || {})[c] !== id) continue;
    for (const o of ORIGINS) { r[o] += s[o]; r.n += s[o]; }
  }
  return r;
}
function humanLine(id, short) {
  const h = edHuman(id);
  if (!h.n) return `<span class="muted small">no assets of its own</span>`;
  const p = pct(h.human + h["third-party"], h.n);
  return `<a class="hum" href="#view=assets&amp;editor=${esc(id)}" title="${h.human} human, ${h["third-party"]} third-party, ${h.ai} AI of ${h.n}: show them">${bar(h.human, h["third-party"], h.ai)}<b>${p} %</b>${short ? "" : ` <span class="muted small">human-made of ${h.n}</span>`}</a>`;
}

// ---- view 1: changes by editor ----------------------------------------------------------------------------------------
function renderChanges() {
  const ch = S.changes, progs = ch.programs;
  let side = `<button data-prog="overview" class="${S.program === "overview" ? "on" : ""}"><span class="nm">Overview</span></button><div class="sep"></div>`;
  for (const p of progs) {
    const c = progCounts(p), h = edHuman(p.id), e = edOf(p.id);
    side += `<button data-prog="${esc(p.id)}" class="${S.program === p.id ? "on" : ""}"><span class="updot ${e && e.up ? "on" : "off"}" title="${esc(e ? (e.up ? "running / ready" : e.installed ? "not running" : "not installed") : "")}"></span><span class="nm">${esc(p.name)}</span>
      ${c.local ? `<span class="cnt l" title="files only local">${c.local}</span>` : ""}
      ${c.testing ? `<span class="cnt t" title="commits in testing, not production">${c.testing}</span>` : ""}
      ${c.exports ? `<span class="cnt e" title="exported files">${c.exports}</span>` : ""}
      ${h.n ? `<span class="hp" title="human-made: ${h.human + h["third-party"]} of ${h.n}">${pct(h.human + h["third-party"], h.n)}%</span>` : ""}</button>`;
  }
  side += `<div class="sep"></div><div class="legend small">
    <div><span class="cnt l">n</span> files only local</div>
    <div><span class="cnt t">n</span> testing, not production</div>
    <div><span class="cnt e">n</span> editor exports</div>
    <div><span class="hp">n%</span> its assets human-made</div></div>`;
  const p = progs.find((x) => x.id === S.program);
  if (!p) S.program = "overview";
  writeHash();
  $("#view").innerHTML = `<aside class="side" id="side">${side}</aside><section class="pane" id="pane">${p ? programPane(p) : overviewPane()}</section>`;
}

// one card per editor (they fit eight across at 2560 px): its link and up / down, the version installed, its counts
function editorCard(p) {
  const c = progCounts(p), e = edOf(p.id) || { folder: p.id, installed: false }, inst = (S.changes.editors.find((x) => x.folder === p.id) || {}).install;
  // (label: [singular, plural])
  const num = (n, [one, many], cls, tip, extra = "") => `<span class="${n ? cls : "muted"}" title="${esc(tip)}"><b>${n || 0}</b> ${n === 1 ? one : many}${extra}</span>`;
  const newest = p.exports.length ? p.exports[0].mtime : 0;
  return `<div class="card ed">
    <div class="hd"><a href="#view=changes&amp;program=${esc(p.id)}" data-prog="${esc(p.id)}"><b>${esc(p.name)}</b></a><span class="grow"></span>${e.kind && e.installed ? edLink(e, "Open", "btn") : ""}</div>
    <div class="line small">${edUp(e)}<span class="grow"></span>${instChip(inst)}</div>
    <div class="nums small">${num(c.local, ["only local", "only local"], "warn", "editor files whose change is only local (every working clone)")}${num(c.commits, ["local commit", "local commits"], "", "commits not in testing that touch its files and bring a change testing doesn't have")}${num(c.unc, ["uncommitted", "uncommitted"], "warn", "uncommitted files")}${num(c.testing, ["testing, not prod.", "testing, not prod."], "", "commits in testing, not in production")}${num(c.exports, ["export", "exports"], "", `exported files: ${c.exports - c.pcExports} in its folder, ${c.pcExports} in this PC's overrides folder`, c.pcExports ? ` <span class="muted">(${c.pcExports} this PC)</span>` : "")}</div>
    <div class="line">${humanLine(p.id)}</div>
    ${newest ? `<div class="small muted">newest export ${when(newest)}</div>` : ""}</div>`;
}

// what needs q's decision or action, in one place (shown red, only when something does): sidecar conflicts, sidecar ids
// Merge can't take, exports not in testing whose asset is still AI, failed installs, clones that need a fetch, a merged
// registry not committed yet
function attention() {
  const ch = S.changes, A = S.assets, out = [];
  const conf = A.rows.filter((r) => r.c), bad = A.rows.filter((r) => r.bad);
  if (conf.length) out.push(`<a href="#view=assets&amp;origin=conflict">${plural(conf.length, "asset")}</a> where an editor's mark disagrees with yours or another editor's: mark ${conf.length === 1 ? "it" : "them"} (or remove your mark) to decide <span class="muted small">${esc(conf.slice(0, 3).map((r) => r.id).join(", "))}${conf.length > 3 ? " ..." : ""}</span>`);
  if (bad.length) out.push(`<a href="#view=assets&amp;origin=pending">${plural(bad.length, "sidecar entry", "sidecar entries")}</a> Merge can't take (no registry category): <span class="mono small">${esc(bad.slice(0, 3).map((r) => r.id).join(", "))}${bad.length > 3 ? " ..." : ""}</span>`);
  for (const p of ch.programs) {
    const ai = p.exports.filter((e) => (e.repo === "missing" || e.repo === "differs") && e.asset && (() => { const r = assetIdx.get(assetKey(e.asset)); return r && !isHuman(effOrigin(r)); })());
    if (ai.length) out.push(`<a href="#view=changes&amp;program=${esc(p.id)}" data-prog="${esc(p.id)}">${esc(p.name)}</a>: ${plural(ai.length, "export")} not in testing whose asset is still AI <span class="muted small">(mark ${ai.length === 1 ? "it" : "them"} human if a person made ${ai.length === 1 ? "it" : "them"})</span>`);
  }
  for (const e of ch.editors) if (e.install && e.install.state === "failed") out.push(`${esc(e.program)}: the app's last update failed <span class="muted small">(its log is in the Supervisor Board)</span>`);
  const stale = ch.clones.filter((c) => c.stale);
  if (stale.length) out.push(`${esc(stale.map((c) => c.name).join(", "))}: not counted until a Refresh with fetch`);
  if (A.regDirty && A.regDirty.length) out.push(`<a href="#view=assets">${esc((A.mergeTarget || {}).name || "the registry clone")}: assets/provenance/ has ${plural(A.regDirty.length, "changed file")}</a> (a merge): commit ${A.regDirty.length === 1 ? "it" : "them"} from the Assets view`);
  return out.length ? `<div class="card attn"><b>Needs attention</b><ul>${out.map((x) => `<li>${x}</li>`).join("")}</ul></div>` : "";
}

function overviewPane() {
  const ch = S.changes, s = S.summary;
  let h = `<h2>Overview</h2>${attention()}`;
  h += `<h3>Editors <span class="muted">compared with ${esc(s.testing.ref || "testing")} ${esc(s.testing.sha)} and ${esc(s.production.tag || "production")}</span></h3>`;
  h += `<div class="edgrid">${ch.programs.map(editorCard).join("")}</div>`;
  h += `<div class="ovgrid"><div>`;
  // the clones, only as far as the editors' files go
  h += `<h3>Working clones <span class="muted">the editors' files only</span></h3>`;
  h += `<div class="card"><table class="t"><tr><th>Clone</th><th class="num">Local commits</th><th class="num">Uncommitted</th><th class="num">Only local files</th><th>Notes</th></tr>`;
  for (const c of ch.clones) {
    const notes = [];
    if (c.role === "mirror") notes.push("mirror of testing (only pulled)");
    if (c.outside) notes.push(`<span class="muted mono small">${esc(c.path)}</span>`);
    if (c.stale) notes.push(`<span class="warn" title="its origin/testing is ${esc(c.testingSha)} and testing's tip (${esc(s.testing.sha)}) isn't in this clone, so its counts would include testing's own commits: Refresh with fetch">not counted: origin/testing here is ${esc(c.testingSha)} (needs fetch)</span>`);
    else if (c.base === "reference") notes.push(`<span class="muted" title="its own origin/testing is older; testing's commits aren't counted as its work">compared with testing's tip (its origin/testing is ${esc(c.testingSha)})</span>`);
    for (const e of c.errors) notes.push(`<span class="err">${esc(e)}</span>`);
    h += `<tr class="${c.stale ? "stale" : ""}"><td class="nowrap"><b>${esc(c.name)}</b></td><td class="num" title="commits not in testing that touch an editor's files and bring a change testing doesn't have${c.allCommits > c.commits ? ` (${c.allCommits - c.commits} more are in testing already: cherry-picked, or undone by a revert)` : ""}">${c.commits || ""}${c.allCommits > c.commits ? ` <span class="muted small">+${c.allCommits - c.commits}</span>` : ""}</td>
      <td class="num">${c.uncommitted ? `<span class="st st-unc">${c.uncommitted}</span>` : ""}</td><td class="num">${c.local || ""}</td><td>${notes.join(" · ")}</td></tr>`;
  }
  if (!ch.clones.length) h += `<tr><td colspan="5" class="empty">no clones found</td></tr>`;
  h += `</table></div></div><div>`;
  h += `<h3>Editor folders <span class="muted">${S.links && S.links.tabsFrom ? `links from <span class="mono" title="${esc(S.links.tabsFrom)}">${esc(S.links.tabsFrom.split(/[\\/]/).slice(-2).join("/"))}</span>` : "no tabs.json found: page editors only"}</span></h3><div class="card"><table class="t"><tr><th>Folder</th><th>Editor</th><th>Page</th><th>Installed</th><th class="num" title="files in the editor's own folder; its card also counts this PC's overrides folder (the last rows)">Exports in its folder</th></tr>`;
  for (const e of ch.editors) {
    const ex = ch.exports.find((x) => x.name === e.folder), L = edOf(e.folder);
    let page = `<span class="muted">-</span>`;
    if (L && L.kind === "server") page = `${edLink(L, L.url.replace(/^https?:\/\//, ""))} ${edUp(L)}`;
    else if (L && L.kind === "page") page = L.pages.map((n) => `<a href="/editors/${esc(e.folder)}/${esc(encodeURIComponent(n))}" ${n === L.url.split("/").pop() ? `data-ed="${esc(e.folder)}"` : ""} target="_blank" rel="noopener">${esc(n)}</a>`).join(" · ");
    else if (L && L.note) page = `<span class="muted">${esc(L.note)}</span>`;
    h += `<tr><td class="mono">${esc(e.folder)}</td><td>${esc(e.program)}</td><td>${page}</td><td>${e.present ? instChip(e.install) : `<span class="muted">not installed</span>`}</td><td class="num">${e.present ? (ex ? ex.files + (ex.more ? ` (+${ex.more} not listed)` : "") : 0) : ""}</td></tr>`;
  }
  for (const ex of ch.exports.filter((x) => !ch.editors.some((e) => e.folder === x.name))) {
    const un = ex.unowned || [];
    h += `<tr><td class="mono">${esc(ex.name)}</td><td class="muted">by path</td><td colspan="2"><span class="muted small mono">${esc(ex.path)}</span>${un.length ? `<div class="warn small" title="${esc(un.join("\n"))}">${plural(un.length, "file")} of no editor (not listed): ${esc(un.slice(0, 3).join(", "))}${un.length > 3 ? " ..." : ""}</div>` : ""}</td><td class="num">${ex.files}${un.length ? ` + ${un.length}` : ""}</td></tr>`;
  }
  h += `</table></div></div></div>`;
  h += `<p class="muted small">Files go to editors by the PATH_RULES table at the top of dashboard.py; files of no editor are not shown
    (game code, tooling and GitHub are in the Supervisor Board).</p>`;
  return h;
}

function showMore(key, list, fn) {
  const all = S.expanded.has(key), shown = all ? list : list.slice(0, LIST_MAX);
  let h = shown.map(fn).join("");
  if (list.length > LIST_MAX) h += `<div class="more"><button data-more="${esc(key)}">${all ? "show fewer" : `show all ${list.length}`}</button></div>`;
  return h;
}
function commitRow(cm, fileFn) {
  return `<div class="cm"><span class="mono muted" title="${esc(cm.sha)}">${esc(cm.h.slice(0, 8))}</span><span class="s" title="${esc(cm.s)}">${esc(cm.s)}</span><span class="muted small">${esc(cm.a)} · ${when(cm.t)}</span>
    <div class="files">${cm.files.map(fileFn).join("")}</div></div>`;
}

// an exported override's asset row (the registry id is the override path; the extension may differ)
let assetIdx = new Map();
const assetKey = (id) => String(id).toLowerCase().replace(/\.[^./]*$/, "");
function indexAssets() {
  // (and each row's editor name once: the list sorts and draws by it, a find per row each time was slow)
  const names = new Map(S.changes ? S.changes.programs.map((p) => [p.id, p.name]) : []), ce = S.assets.catEditor || {};
  for (const r of S.assets.rows) r.ed = names.get(ce[r.cat]) || "";
  assetIdx = new Map(S.assets.rows.map((r) => [assetKey(r.id), r]));
}
const effOrigin = (r) => (r.p && r.p[0].origin) || r.o;
const isHuman = (o) => o === "human" || o === "third-party";

function programPane(p) {
  const s = S.summary, e = edOf(p.id) || { folder: p.id, installed: false }, inst = (S.changes.editors.find((x) => x.folder === p.id) || {}).install;
  let h = `<div class="phd"><h2>${esc(p.name)}</h2>${e.kind && e.installed ? edLink(e, "Open " + p.name, "btn") : ""}<span class="line">${edUp(e)}</span><span>installed: ${instChip(inst)}</span><span class="grow"></span>${humanLine(p.id)}</div>`;
  // local work
  h += `<h3>Local work <span class="muted">commits ahead of testing and uncommitted files, in every working clone</span></h3>`;
  if (!p.clones.length) h += `<div class="empty">nothing local</div>`;
  for (const c of p.clones) {
    const inT = c.commits.filter((cm) => cm.net === false).length;
    h += `<div class="card ${c.stale ? "stale" : ""}"><div class="hd"><b>${esc(c.clone)}</b><span class="muted">${plural(c.commits.length, "commit")} not in testing${inT ? ` (${inT} of them already in testing by content: cherry-picked or reverted)` : ""} · ${c.uncommitted.length} uncommitted</span>
      ${c.stale ? `<span class="warn">not counted: this clone's origin/testing is old and testing's tip isn't in it (Refresh with fetch)</span>` : ""}</div>`;
    if (c.commits.length) h += showMore("c:" + p.name + c.clone, c.commits, (cm) => commitRow(cm, ([l, f, x]) => `<span><span class="letter">${esc(l)}</span>${esc(f)} ${st(x)}</span>`));
    if (c.uncommitted.length) {
      h += `<details ${c.uncommitted.length <= 12 ? "open" : ""}><summary>${plural(c.uncommitted.length, "uncommitted file")}</summary><div class="flist">`;
      h += showMore("u:" + p.name + c.clone, c.uncommitted, ([xy, f, x]) => `<div><span class="letter" title="git status ${esc(xy)}">${esc(xy.trim() || "M")}</span><span class="p">${esc(f)}</span>${st(x)}</div>`);
      h += `</div></details>`;
    }
    h += `</div>`;
  }
  if (p.files.length) {
    h += `<details class="card"><summary>Every local file (${p.files.length}), with its state in each clone</summary><table class="t"><tr><th>File</th><th>State per clone</th></tr>`;
    h += showMore("f:" + p.name, p.files, ([f, sts]) => `<tr><td class="mono">${esc(f)}</td><td>${Object.entries(sts).map(([cl, x]) => `<span class="muted small">${esc(cl)}</span> ${st(x)}`).join(" &nbsp; ")}</td></tr>`);
    h += `</table></details>`;
  }
  // testing vs production
  h += `<h3>In testing, not in production <span class="muted">${esc(s.production.tag || "no production tag")} → ${esc(s.testing.ref || "testing")} (${esc(s.production.version || "?")} → ${esc(s.testing.version || "?")})</span></h3>`;
  if (!p.testing.length && !p.testingFiles.length) h += `<div class="empty">nothing for ${esc(p.name)} since production</div>`;
  else {
    h += `<div class="card"><details ${p.testing.length <= 8 ? "open" : ""}><summary>${plural(p.testing.length, "commit")}</summary>`;
    h += showMore("t:" + p.name, p.testing, (cm) => commitRow(cm, ([l, f]) => `<span><span class="letter">${esc(l)}</span>${esc(f)}</span>`));
    h += `</details><details><summary>${plural(p.testingFiles.length, "file")} differ between production and testing</summary><div class="flist">`;
    h += showMore("tf:" + p.name, p.testingFiles, ([l, f]) => `<div><span class="letter">${esc(l)}</span><span class="p">${esc(f)}</span></div>`);
    h += `</div></details></div>`;
  }
  // exports, each override linked to its asset row (and marked human from here)
  // (every exported override whose asset isn't human-made yet: one "mark them all human" button)
  const toMark = [...new Set(p.exports.map((e) => e.asset && assetIdx.get(assetKey(e.asset))).filter((r) => r && !isHuman(effOrigin(r))).map((r) => r.id))];
  h += `<h3>Exported files <span class="muted">editor folders and this PC's overrides folder (not in git), compared with testing's assets/overrides</span>${toMark.length ? `<span class="grow"></span><button class="mini" data-markall="${esc(p.id)}" title="mark every exported asset of ${esc(p.name)} that isn't human-made yet as human (your marks; only when a person made them)">Mark all ${toMark.length} human</button>` : ""}</h3>`;
  if (!p.exports.length) h += `<div class="empty">no exports</div>`;
  else {
    const repo = (S.assets.mergeTarget || {}).name || "the registry clone";
    const REPO = { same: ["in testing", "st-both", "testing's assets/overrides has this same file"],
                   local: [`in ${repo} only`, "st-testing", `${repo} has this file, testing doesn't yet (an unpushed commit or an uncommitted file)`],
                   differs: ["differs from testing", "st-local", "testing's assets/overrides has a different file"],
                   missing: ["not in testing", "st-local", "not in testing's assets/overrides yet"] };
    h += `<div class="card scrollx"><table class="t"><tr><th>Where</th><th>File</th><th>Override</th><th>Asset</th><th>Date</th><th class="num">Size</th></tr>`;
    h += showMore("e:" + p.name, p.exports, (e) => {
      const r = REPO[e.repo], row = e.asset ? assetIdx.get(assetKey(e.asset)) : null, eo = row && effOrigin(row);
      const asset = !e.asset ? "" : !row ? `<span class="muted small" title="${esc(e.asset)}">no registry item</span>`
        : `<a href="#asset=${esc(encodeURIComponent(row.id))}" class="mono small" title="show it in Assets">${esc(row.id)}</a> ${og(eo)}${row.p ? ` <span class="muted small">(pending)</span>` : ""}${isHuman(eo) ? "" : ` <button class="mini" data-markhuman="${esc(row.id)}" title="mark it human (your mark, saved to the Editor Manager's provenance.json)">mark human</button>`}`;
      return `<tr><td class="mono small nowrap">${esc(e.source)}</td><td class="mono small brk">${esc(e.path)}</td><td>${r ? `<span class="st ${r[1]}" title="${esc(r[2])}">${esc(r[0])}</span>` : ""}</td><td class="brk">${asset}</td><td class="nowrap" title="${esc(stamp(e.mtime))}">${when(e.mtime)}</td><td class="num">${size(e.size)}</td></tr>`;
    });
    h += `</table></div>`;
  }
  return h;
}

// ---- views 2 + 3: assets (all, or the remaining AI-made ones) --------------------------------------------------------------
const SORTS = {
  id: (r) => r.id.toLowerCase(), cat: (r) => r.cat, ed: (r) => r.ed || "~", o: (r) => r.o ? ORIGINS.indexOf(r.o) : 9,
  p: (r) => r.p ? (r.c ? -1 : ORIGINS.indexOf(r.p[0].origin)) : 9, date: (r) => (r.p && r.p[0].date) || r.date || "", src: (r) => (r.source || r.note || "").toLowerCase(),
};
function filteredRows(mode) {
  const f = S.filt[mode], q = f.q.trim().toLowerCase(), ce = S.assets.catEditor || {};
  const rows = S.assets.rows.filter((r) => {
    if (f.cat && r.cat !== f.cat) return false;
    if (f.ed && ce[r.cat] !== f.ed) return false;
    if (f.pending && !r.p) return false;
    if (f.origin) {
      if (f.origin === "pending") { if (!r.p) return false; }
      else if (f.origin === "conflict") { if (!r.c) return false; }
      else if (r.o !== f.origin) return false;
    }
    if (q && !(r.id.toLowerCase().includes(q) || (r.source || "").toLowerCase().includes(q) || (r.note || "").toLowerCase().includes(q) || (r.by || "").toLowerCase().includes(q))) return false;
    return true;
  });
  const { k, d } = S.sort[mode], key = SORTS[k] || SORTS.id;
  if (k !== "id" || d !== 1) {                                          // (the server sends them sorted by id)
    const keyed = rows.map((r) => [key(r), r]);
    keyed.sort((a, b) => (a[0] < b[0] ? -d : a[0] > b[0] ? d : a[1].id < b[1].id ? -1 : 1));
    return keyed.map((x) => x[1]);
  }
  return rows;
}

// The card that takes the marks into the registry. One rule decides each item, here and in Merge (dashboard.py decide()):
// your mark wins over the editors' entries, else the newest editor entry; pending = Merge would change the registry; a
// conflict = an editor disagrees with your mark (or with another editor) and you haven't decided it. Merge writes the
// merge file (exactly those choices) and runs tools/provenance.py merge on it; then "Review and commit" makes a local
// commit of assets/provenance/ (never a push).
function mergeCard() {
  const A = S.assets, tgt = A.mergeTarget || {}, edsPend = A.sidecars.filter((x) => !x.own).reduce((a, x) => a + x.pending, 0);
  const nConf = A.rows.filter((r) => r.c).length, bad = A.rows.filter((r) => r.bad);
  const sc = (x) => [`${x.pending} to merge`, x.over ? `${x.over} overruled by you` : "", x.conflicts ? `<span class="err">${plural(x.conflicts, "conflict")}</span>` : ""].filter(Boolean).join(" · ");
  let h = `<div class="card"><div><b>${A.ownPending}</b> ${A.ownPending === 1 ? "mark" : "marks"} of yours to merge${edsPend ? `, <b>${edsPend}</b> from editors` : ""}${nConf ? ` · <a class="err" href="#view=assets&amp;origin=conflict" title="an editor disagrees with your mark (or with another editor): mark the item, or remove your mark, to decide">${plural(nConf, "conflict")}</a>` : ""}</div>
    <div class="sidecars">${A.sidecars.map((x) => `<div title="${esc(x.path)}\n${x.items} entries"><span class="og ${x.pending ? "og-pending" : "og-human"}">${esc(x.tool)}</span><span>${sc(x)}</span><span class="muted">${when(x.mtime)}</span><span class="muted mono small ell">${esc(x.short)}</span></div>`).join("") || `<span class="muted">no sidecars yet</span>`}</div>`;
  if (A.toMerge) h += `<div class="cmd"><button class="primary" id="mergeGo" ${tgt.ok ? "" : "disabled"} title="${esc(tgt.ok ? `writes each pending item's winning entry to the merge file and runs tools/provenance.py merge on it in ${tgt.path} (hidden)` : tgt.why || "")}">Merge ${A.toMerge} into ${esc(tgt.name || "the registry clone")}</button>
      <details class="grow"><summary class="small">or run it yourself in <span class="mono">${esc(A.mergeCwd.split(/[\\/]/).pop())}</span></summary><div class="cmd"><code id="mergecmd">${esc(A.mergeCmd)}</code><button data-plan="mergecmd" title="writes the merge file, then copies the command">Copy</button></div></details></div>
      ${tgt.ok ? "" : `<div class="small warn" style="margin-top:4px">${esc(tgt.why || "")}</div>`}`;
  else h += `<div class="small muted" style="margin-top:6px">nothing to merge</div>`;
  if (bad.length) h += `<div class="small warn" style="margin-top:4px" title="${esc(bad.map((r) => r.id + ": " + r.bad).join("\n"))}">${plural(bad.length, "entry", "entries")} Merge skips (no registry category): <span class="mono">${esc(bad.slice(0, 3).map((r) => r.id).join(", "))}${bad.length > 3 ? " ..." : ""}</span></div>`;
  if (S.mergeOut) h += `<details class="small" style="margin-top:4px" ${S.mergeOut.ok ? "" : "open"}><summary class="${S.mergeOut.ok ? "" : "err"}">last ${esc(S.mergeOut.what || "merge")}: ${esc(S.mergeOut.line)}</summary><pre class="out">${esc(S.mergeOut.lines.join("\n"))}</pre></details>`;
  // the merged registry, not committed yet: review the diff, commit it locally
  const dirty = A.regDirty || [];
  if (dirty.length) {
    const d = S.regDiff;
    h += `<div class="cmd"><span class="warn grow">${esc(tgt.name)}: assets/provenance/ has ${plural(dirty.length, "uncommitted file")}</span><button id="commitOpen">${d ? "Hide" : "Review and commit"}</button></div>`;
    if (d) h += `<div class="commitbox">${d.loading ? `<span class="muted small">reading the diff...</span>` : `<div class="small muted">${esc(d.stat || "")}${d.lines > d.diff.split("\n").length ? ` · the first ${d.diff.split("\n").length} of ${d.lines} lines` : ""}</div><pre class="out diff">${diffHtml(d.diff)}</pre>
      <div class="cmd"><input type="text" id="commitMsg" class="grow" value="${esc(S.commitMsg || "Provenance: merge the editors' and the owner's marks")}"><button class="primary" id="commitGo" title="git add + commit of assets/provenance/ only, in ${esc(tgt.path || "")} (a local commit: never pushed)">Commit locally</button></div>`}</div>`;
  }
  if (A.errors.length) h += `<div class="err small" style="margin-top:6px">${A.errors.map(esc).join("<br>")}</div>`;
  if (!A.sidecars.some((x) => x.own)) h += `<div class="muted small" style="margin-top:4px">your marks go to <span class="mono">${esc(A.ownSidecar)}</span></div>`;
  return h + `</div>`;
}
const diffHtml = (d) => d.split("\n").map((l) => `<span class="${/^\+(?!\+\+)/.test(l) ? "add" : /^-(?!--)/.test(l) ? "del" : /^(@@|diff |new file)/.test(l) ? "hunk" : ""}">${esc(l)}</span>`).join("\n");

function renderAssets(mode) {
  const A = S.assets, f = S.filt[mode], stats = A.stats, cats = Object.keys(stats).sort();
  const tot = { human: 0, "third-party": 0, ai: 0, pending: 0, eff: { human: 0, "third-party": 0, ai: 0 } };
  for (const c of cats) { for (const o of ORIGINS) { tot[o] += stats[c][o]; tot.eff[o] += stats[c].eff[o]; } tot.pending += stats[c].pending; }
  const n = tot.human + tot["third-party"] + tot.ai;
  // (with the pending marks, the total includes the new items they add, so the % can't pass 100)
  const effH = tot.eff.human + tot.eff["third-party"], effN = effH + tot.eff.ai, regH = tot.human + tot["third-party"];
  let h = `<div class="statrow">`;
  // overall
  if (mode === "remaining") {
    h += `<div class="card"><div class="k muted small">REMAINING (AI-made, in the registry)</div><div class="big">${tot.ai} <small>of ${n} · ${aiPct(tot.ai, n)} %</small></div>
      ${bar(tot.human, tot["third-party"], tot.ai)}<div class="legend small" style="margin-top:6px">
      ${tot.ai - tot.eff.ai > 0 ? `<span class="warn">${tot.ai - tot.eff.ai} of them have a pending human / third-party mark</span>` : `<span class="muted">none has a pending mark</span>`}</div></div>`;
  } else {
    h += `<div class="card"><div class="k muted small">HUMAN-MADE (REGISTRY)</div><div class="big">${pct(regH, n)} % <small>${regH} of ${n}</small></div>
      ${bar(tot.human, tot["third-party"], tot.ai)}<div class="legend small" style="margin-top:6px">
      <span><i class="dot" style="background:var(--qt-human)"></i>human ${tot.human}</span><span><i class="dot" style="background:var(--qt-third)"></i>third-party ${tot["third-party"]}</span><span><i class="dot" style="background:var(--qt-ai)"></i>AI ${tot.ai}</span></div>
      ${tot.pending ? `<div class="small" style="margin-top:4px" title="${effH} of ${effN} items, counting the pending marks and the new items they add">with the pending marks: <b>${pct(effH, effN)} %</b> (${plural(tot.pending, "pending item")})</div>` : ""}</div>`;
  }
  // per category: a tile each (as many across as fit: one row at 2560 px)
  h += `<div class="card"><div class="cats">`;
  for (const c of cats) {
    const s = stats[c], cn = s.human + s["third-party"] + s.ai;
    const right = mode === "remaining" ? `${s.ai} left of ${cn}` : `${s.human} h · ${s["third-party"]} 3p · ${s.ai} AI${s.pending ? ` · <span class="warn">${s.pending} pending</span>` : ""}${s.conflicts ? ` · <span class="err">${s.conflicts} conflict${s.conflicts === 1 ? "" : "s"}</span>` : ""}`;
    h += `<div class="cat ${f.cat === c ? "on" : ""}" data-cat="${esc(c)}" title="${esc(f.cat === c ? "show every category" : "show only " + c)}"><span class="c">${esc(c)}</span>${bar(s.human, s["third-party"], s.ai)}<span class="pct">${mode === "remaining" ? aiPct(s.ai, cn) : pct(s.human + s["third-party"], cn)}%</span><span class="n">${right}</span></div>`;
  }
  if (!cats.length) h += `<span class="empty">no registry items</span>`;
  h += `</div></div>`;
  // pending marks, conflicts, merge, commit
  h += mergeCard();
  h += `</div>`;
  // toolbar
  const opt = (v, l, cur) => `<option value="${esc(v)}" ${v === cur ? "selected" : ""}>${esc(l)}</option>`;
  const eds = S.changes.programs.filter((p) => Object.values(A.catEditor || {}).includes(p.id));
  h += `<div class="toolbar">
    <input type="search" id="q" placeholder="search id, source, note, by" value="${esc(f.q)}">
    ${mode === "remaining" ? "" : `<select id="origin">${opt("", "every origin", f.origin)}${opt("human", "human", f.origin)}${opt("third-party", "third-party", f.origin)}${opt("ai", "AI", f.origin)}${opt("pending", "pending", f.origin)}${opt("conflict", "conflict", f.origin)}</select>`}
    <select id="cat">${opt("", "every category", f.cat)}${cats.map((c) => opt(c, c, f.cat)).join("")}</select>
    <select id="ed">${opt("", "every editor", f.ed)}${eds.map((p) => opt(p.id, p.name, f.ed)).join("")}</select>
    <label class="chk"><input type="checkbox" id="pendingOnly" ${f.pending ? "checked" : ""}> pending only</label>
    <span class="grow"></span><span id="count" class="muted"></span>
    <button id="selAll">Select shown</button><button id="selNone">Clear selection</button></div>`;
  // mark bar
  h += `<div class="markbar" id="markbar"><b id="selN"></b>
    <span class="seg">${ORIGINS.map((o) => `<button data-origin="${o}" class="${o} ${S.markOrigin === o ? "on" : ""}">${ORIGIN_LABEL[o]}</button>`).join("")}</span>
    <label>by <input type="text" name="by" value="${esc(store.get("by", "q"))}"></label>
    <label>note <input type="text" name="note" placeholder="optional"></label>
    <button class="primary" id="markGo">Mark</button><button id="markClear" title="remove the Editor Manager's own mark for the selected items">Remove my mark</button>
    <span class="muted small">saved to the Editor Manager's provenance.json, never into a clone</span></div>`;
  // the list (click a header to sort)
  const so = S.sort[mode], hd = (k, l) => `<span class="sort ${so.k === k ? (so.d > 0 ? "up" : "down") : ""}" data-sort="${k}" title="sort by ${esc(l.toLowerCase())}">${esc(l)}</span>`;
  h += `<div class="vt"><div class="vrow vhead"><span><input type="checkbox" id="selHead" title="select every shown item"></span><span></span>${hd("id", "Asset")}${hd("cat", "Category")}${hd("ed", "Editor")}${hd("o", "Origin")}${hd("p", "Pending")}${hd("date", "By / date")}${hd("src", "Source / note")}</div>
    <div class="vbody" id="vbody"><div id="vspace"></div></div><div class="vfoot" id="vfoot"></div></div>`;
  $("#view").innerHTML = `<section class="assets">${h}</section>`;
  S.base = mode === "remaining" ? A.rows.filter((r) => r.o === "ai").length : A.rows.length;   // (the count's "of N")
  S.missing = S.focusAsset && !A.rows.some((r) => r.id === S.focusAsset) ? S.focusAsset : null;
  S.rows = filteredRows(mode); S.lastIdx = -1;
  updateList(true);
  if (S.focusAsset) {                                                   // (#asset=<id>: select it and bring it into view)
    const i = S.rows.findIndex((r) => r.id === S.focusAsset);
    if (i >= 0) { S.sel.add(S.rows[i].id); S.lastIdx = i; $("#vbody").scrollTop = Math.max(0, (i - 3) * ROW_H); drawRows(); updateSel(); }
    else toast(S.missing ? `No asset ${S.focusAsset} in the registry or a sidecar` : `${S.focusAsset} is hidden by the filters`, true);
    S.focusAsset = null;
  }
  writeHash();
}

// after the filters change: the list, and the selection kept to the items still shown (Mark never changes a hidden item)
function updateList(resetScroll) {
  const body = $("#vbody");
  if (!body) return;
  if (resetScroll) body.scrollTop = 0;
  if (S.sel.size) { const shown = new Set(S.rows.map((r) => r.id)); for (const id of [...S.sel]) if (!shown.has(id)) S.sel.delete(id); }
  $("#vspace").style.height = S.rows.length * ROW_H + "px";
  $("#count").textContent = `${S.rows.length} of ${S.base ?? S.assets.rows.length}${S.tab === "remaining" ? " AI-made" : ""} shown`;
  drawRows();
  updateSel();
}
const ICON_PLAY = `<svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true"><path d="M4 2.5v11l9-5.5z" fill="currentColor"/></svg>`;
const ICON_STOP = `<svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true"><rect x="3.5" y="3.5" width="9" height="9" fill="currentColor"/></svg>`;
const ICON_IMG = `<svg viewBox="0 0 16 16" width="13" height="13" aria-hidden="true"><rect x="1.5" y="2.5" width="13" height="11" rx="1.5" fill="none" stroke="currentColor"/><path d="M3 12l3.5-4 2.5 3 2-2 2 3z" fill="currentColor"/></svg>`;
function drawRows() {
  const body = $("#vbody");
  if (!body) return;
  const first = Math.max(0, Math.floor(body.scrollTop / ROW_H) - 8), last = Math.min(S.rows.length, Math.ceil((body.scrollTop + body.clientHeight) / ROW_H) + 8);
  let h = "";
  for (let i = first; i < last; i++) {
    const r = S.rows[i], p = r.p && r.p[0];
    let pend = "";
    if (p) {
      const tip = r.p.map((x) => `${x.tool}: ${ORIGIN_LABEL[x.origin] || x.origin}${x.by ? " by " + x.by : ""}${x.date ? " " + x.date : ""}${x.note ? " (" + x.note + ")" : ""}: ${PEND_ST[x.st] || x.st}`).join("\n")
        + "\n" + (r.bad || (r.pm ? "Merge writes the first one" : "the registry already has the first one"));
      pend = `${r.c ? `<span class="og og-conflict">conflict</span> ` : ""}<span class="og ${r.pm || r.bad ? "og-pending" : "og-" + esc(p.origin)}" title="${esc(tip)}">${esc(ORIGIN_LABEL[p.origin] || p.origin || "?")}</span> <span class="muted small" title="${esc(tip)}">${esc(p.tool)}${r.p.length > 1 ? " +" + (r.p.length - 1) : ""}</span>${r.bad ? ` <span class="warn small" title="${esc(r.bad)}">can't merge</span>` : ""}`;
    }
    const src = [r.source, r.note && "note: " + r.note, r.wip && "wip: " + r.wip].filter(Boolean).join(" · ");
    const pv = r.pv === "a" ? `<button class="pv ${S.playing === r.id ? "on" : ""}" data-pv="a" title="${S.playing === r.id ? "stop" : "play it"}">${S.playing === r.id ? ICON_STOP : ICON_PLAY}</button>`
      : r.pv === "i" ? `<button class="pv" data-pv="i" title="view it">${ICON_IMG}</button>` : "";
    h += `<div class="vrow ${S.sel.has(r.id) ? "sel" : ""}" data-i="${i}" style="top:${i * ROW_H}px"><span><input type="checkbox" ${S.sel.has(r.id) ? "checked" : ""}></span><span>${pv}</span>
      <span class="id" title="${esc(r.id)}">${esc(r.id)}</span><span class="muted">${esc(r.cat)}</span><span class="small">${esc(r.ed)}</span><span>${og(r.o)}</span><span>${pend}</span>
      <span class="small">${esc(r.by || "")}${r.date ? ` <span class="muted">${esc(r.date)}</span>` : ""}</span><span class="muted small" title="${esc(src)}">${esc(src)}</span></div>`;
  }
  if (!S.rows.length) h = `<div class="vempty">${S.missing ? `No asset <span class="mono">${esc(S.missing)}</span> in the registry or in a sidecar.` : "No item matches the filters."}</div>`;
  // keep the spacer, replace the rows
  const space = $("#vspace");
  body.innerHTML = ""; body.appendChild(space); body.insertAdjacentHTML("beforeend", h);
}
function updateSel() {
  const n = S.sel.size, shownSel = S.rows.reduce((a, r) => a + (S.sel.has(r.id) ? 1 : 0), 0);
  const mb = $("#markbar");
  if (!mb) return;
  $("#selN").textContent = n ? `${n} selected` : "select items to mark";
  mb.classList.toggle("off", !n);
  $("#markGo").disabled = $("#markClear").disabled = !n;
  $("#markGo").textContent = n ? `Mark ${n} as ${ORIGIN_LABEL[S.markOrigin]}` : "Mark";
  const head = $("#selHead");
  head.checked = S.rows.length > 0 && shownSel === S.rows.length; head.indeterminate = shownSel > 0 && shownSel < S.rows.length;
  $("#vfoot").innerHTML = `<span>${plural(S.rows.length, "item")}</span><span>${shownSel} of them selected</span><span>click a row to select, Shift+click for a range; click a column title to sort</span>`;
  writeHash();
}

// ---- previews: one sound at a time, a picture in a small pop-up ------------------------------------------------------------
const audio = new Audio();
audio.addEventListener("ended", () => { S.playing = null; drawRows(); });
audio.addEventListener("error", () => { if (S.playing) { toast("Can't play " + S.playing, true); S.playing = null; drawRows(); } });
function preview(r, btn) {
  const url = "/api/preview?id=" + encodeURIComponent(r.id);
  if (r.pv === "a") {
    if (S.playing === r.id) { audio.pause(); S.playing = null; }
    else { S.playing = r.id; audio.src = url; audio.play().catch(() => { /* (the error event says so) */ }); }
    drawRows(); return;
  }
  const pop = $("#pvpop"), b = btn.getBoundingClientRect();
  pop.innerHTML = `<img alt="${esc(r.id)}" src="${esc(url)}"><div class="small mono">${esc(r.id)}</div>`;
  pop.hidden = false;
  pop.style.left = Math.min(b.right + 8, innerWidth - 340) + "px";
  pop.style.top = Math.max(8, Math.min(b.top - 20, innerHeight - 300)) + "px";
}

async function postMark(ids, origin, clear, note) {
  const by = ($('#markbar input[name="by"]') || {}).value;
  const who = (by == null ? store.get("by", "q") : by).trim();
  store.set("by", who);
  busy = true;
  try {
    const r = await api("/api/mark", { ids, origin, by: who, note: note || "", clear: !!clear });
    S.assets = r.assets; S.summary = r.status; indexAssets();
    renderHeader(); shellStats();
    return r.result || {};
  } finally { busy = false; }
}
async function mark(clear) {
  const ids = [...S.sel];
  if (!ids.length) return;
  if (!clear && ids.length > MARK_CONFIRM && !confirm(`Mark ${ids.length} items as ${ORIGIN_LABEL[S.markOrigin]}?`)) return;
  const note = $('#markbar input[name="note"]').value.trim();
  try {
    const res = await postMark(ids, S.markOrigin, clear, note);
    S.sel.clear();
    const keep = $("#vbody").scrollTop;
    renderAssets(S.tab); $("#vbody").scrollTop = keep; drawRows();
    if (clear) toast(res.removed ? `Removed your mark from ${plural(res.removed, "item")}` : `None of ${ids.length === 1 ? "it" : "these"} had your mark`);
    else toast(markText(res, ORIGIN_LABEL[S.markOrigin]));
  } catch (e) { toast("Mark failed: " + e.message, true); }
}
// the toast after a mark: what was written, what overruled an editor, what the registry already says
function markText(res, label) {
  return [res.marked ? `Marked ${plural(res.marked, "item")} ${label}` : "Nothing to mark",
          res.overruled ? `overruling ${plural(res.overruled, "editor entry", "editor entries")}` : "",
          res.same ? `${res.same} already ${res.same === 1 ? "is" : "are"} ${label} in the registry` : "",
          res.bad ? `${res.bad} skipped (no registry category)` : ""].filter(Boolean).join("; ");
}
async function markHuman(ids) {
  try {
    const res = await postMark(ids, "human", false, "");
    const sc = $("#pane") ? $("#pane").scrollTop : 0;
    render(); if ($("#pane")) $("#pane").scrollTop = sc;
    toast(markText(res, "human") + (res.marked ? " (pending until merged)" : ""));
  } catch (e) { toast("Mark failed: " + e.message, true); }
}
// "Mark all N human" on an editor's exports: every exported override whose asset isn't human-made yet
function markAllHuman(pid) {
  const p = S.changes.programs.find((x) => x.id === pid);
  if (!p) return;
  const ids = [...new Set(p.exports.map((e) => e.asset && assetIdx.get(assetKey(e.asset))).filter((r) => r && !isHuman(effOrigin(r))).map((r) => r.id))];
  if (!ids.length) return;
  if (!confirm(`Mark ${plural(ids.length, "exported asset")} of ${p.name} as human?\n\nOnly when a person made ${ids.length === 1 ? "it" : "them"} (a trim of an AI sound stays AI).\n\n${ids.slice(0, 12).join("\n")}${ids.length > 12 ? "\n..." : ""}`)) return;
  markHuman(ids);
}
async function merge() {
  const A = S.assets, tgt = A.mergeTarget || {}, pend = A.sidecars.filter((x) => x.pending);
  const n = A.toMerge, nConf = A.rows.filter((r) => r.c).length;
  if (!confirm(`Merge ${n} pending ${n === 1 ? "item" : "items"} (from ${pend.map((x) => x.tool).join(", ")}) into ${tgt.name}'s assets/provenance/?\n\nEach takes the origin the page shows: your mark, else the newest editor entry.${nConf ? `\n${plural(nConf, "conflict")} keep${nConf === 1 ? "s" : ""} your mark until you decide.` : ""}\nRuns tools/provenance.py merge in ${tgt.path}; then commit assets/provenance/ (Review and commit).`)) return;
  const b = $("#mergeGo"); b.disabled = true; b.textContent = "Merging..."; busy = true;
  let r;
  try { r = await api("/api/merge", { confirm: true }); }
  catch (e) { r = e.body || { ok: false, output: [e.message] }; }
  busy = false;
  const lines = r.output || [];
  S.mergeOut = { what: "merge", ok: !!r.ok, lines, line: lines.filter((l) => l.trim()).pop() || (r.ok ? "done" : "failed") };
  if (r.assets) { S.assets = r.assets; S.summary = r.status; indexAssets(); renderHeader(); shellStats(); }
  S.regDiff = null;
  renderAssets(S.tab);
  toast(r.ok ? `Merged into ${tgt.name}: ${S.mergeOut.line}. Review and commit assets/provenance/ next.` : "Merge failed: " + S.mergeOut.line, !r.ok);
}
// Review and commit: the diff of assets/provenance/ in the registry clone, then a local commit of that folder only
async function toggleCommit() {
  if (S.regDiff) { S.regDiff = null; return rerenderAssets(); }
  S.regDiff = { loading: true }; rerenderAssets();
  try { S.regDiff = await api("/api/regdiff"); } catch (e) { S.regDiff = null; toast("Can't read the diff: " + e.message, true); }
  if (S.regDiff && !S.regDiff.files) { toast(S.regDiff.why || "no registry clone to commit in", true); S.regDiff = null; }
  rerenderAssets();
}
async function commitRegistry() {
  const msg = $("#commitMsg").value.trim(), tgt = S.assets.mergeTarget || {};
  S.commitMsg = msg;
  const b = $("#commitGo"); b.disabled = true; b.textContent = "Committing..."; busy = true;
  let r;
  try { r = await api("/api/commit", { confirm: true, message: msg }); } catch (e) { r = e.body || { ok: false, output: [e.message] }; }
  busy = false;
  const lines = r.output || [];
  S.mergeOut = { what: "commit", ok: !!r.ok, lines, line: lines[0] || (r.ok ? "done" : "failed") };
  if (r.assets) { S.assets = r.assets; S.summary = r.status; indexAssets(); renderHeader(); shellStats(); }
  if (r.ok) { S.regDiff = null; S.commitMsg = ""; }
  rerenderAssets();
  toast(r.ok ? `Committed in ${tgt.name}: ${S.mergeOut.line} (local only: push it when you push)` : "Commit failed: " + (lines.slice(-1)[0] || "?"), !r.ok);
}
function rerenderAssets() { const keep = $("#vbody") ? $("#vbody").scrollTop : 0; renderAssets(S.tab); $("#vbody").scrollTop = keep; drawRows(); }
// "run it yourself": write the merge file first, then copy the command
async function copyPlan(id) {
  try { const r = await api("/api/plan", {}); toast(`Wrote ${r.path} (${plural(r.items, "item")})`); }
  catch (e) { return toast("Can't write the merge file: " + e.message, true); }
  copyText(id);
}
function copyText(id) {
  const txt = $("#" + id).textContent;
  (navigator.clipboard ? navigator.clipboard.writeText(txt) : Promise.reject()).then(() => toast("Copied"), () => {
    const r = document.createRange(); r.selectNodeContents($("#" + id)); getSelection().removeAllRanges(); getSelection().addRange(r); toast("Selected: press Ctrl+C");
  });
}

// ---- events (delegated: the views re-render often) --------------------------------------------------------------------
document.addEventListener("click", (ev) => {
  const t = ev.target;
  if (!t.closest("#pvpop") && !t.closest("[data-pv]")) $("#pvpop").hidden = true;
  const tab = t.closest("#tabs [data-tab]");
  if (tab) return setTab(tab.dataset.tab);
  const ed = t.closest("a[data-ed]");
  if (ed) return openEditor(ev, ed);
  const stb = t.closest("[data-start]");
  if (stb) return startEditor(stb.dataset.start);
  const prog = t.closest("[data-prog]");
  if (prog) { ev.preventDefault(); S.program = prog.dataset.prog; store.set("program", S.program); renderChanges(); $("#pane").scrollTop = 0; return; }
  const more = t.closest("[data-more]");
  if (more) { const k = more.dataset.more; S.expanded.has(k) ? S.expanded.delete(k) : S.expanded.add(k); const sc = $("#pane").scrollTop; renderChanges(); $("#pane").scrollTop = sc; return; }
  const mh = t.closest("[data-markhuman]");
  if (mh) return markHuman([mh.dataset.markhuman]);
  const ma = t.closest("[data-markall]");
  if (ma) return markAllHuman(ma.dataset.markall);
  const cp = t.closest("[data-copy]");
  if (cp) return copyText(cp.dataset.copy);
  const pl = t.closest("[data-plan]");
  if (pl) return copyPlan(pl.dataset.plan);
  if (t.id === "commitOpen") return toggleCommit();
  if (t.id === "commitGo") return commitRegistry();
  const cat = t.closest(".cats [data-cat]");
  if (cat) { const f = S.filt[S.tab]; f.cat = f.cat === cat.dataset.cat ? "" : cat.dataset.cat; renderAssets(S.tab); return; }
  const so = t.closest(".vhead [data-sort]");
  if (so) { const s = S.sort[S.tab]; s.d = s.k === so.dataset.sort ? -s.d : 1; s.k = so.dataset.sort; renderAssets(S.tab); return; }
  const o = t.closest("#markbar [data-origin]");
  if (o) { S.markOrigin = o.dataset.origin; for (const b of document.querySelectorAll("#markbar [data-origin]")) b.classList.toggle("on", b === o); updateSel(); return; }
  if (t.id === "markGo") return mark(false);
  if (t.id === "markClear") return mark(true);
  if (t.closest("#mergeGo")) return merge();
  if (t.id === "refresh") return refresh();
  const tool = t.closest("a[data-tool]"), sb = tool && sib(tool.dataset.tool);
  if (sb && !sb.up) {                                                   // (down when last checked: look again first)
    ev.preventDefault();
    const href = tool.href, target = tool.target || "_blank";
    api("/api/links").then((L) => {
      renderNav(L);
      const now = sib(sb.id);
      now && now.up ? window.open(href, target) : toast(`${sb.label}: ${downText(sb)}`, true);
    });
    return;
  }
  if (t.id === "selAll" || t.id === "selHead") {
    const on = t.id === "selAll" || t.checked;
    for (const r of S.rows) on ? S.sel.add(r.id) : S.sel.delete(r.id);
    drawRows(); updateSel(); return;
  }
  if (t.id === "selNone") { S.sel.clear(); drawRows(); updateSel(); return; }
  const row = t.closest("#vbody .vrow");
  if (row) {
    const i = +row.dataset.i, id = S.rows[i].id;
    const pv = t.closest("[data-pv]");
    if (pv) return preview(S.rows[i], pv);
    if (ev.shiftKey && S.lastIdx >= 0) {
      const on = !S.sel.has(id), [a, b] = [Math.min(i, S.lastIdx), Math.max(i, S.lastIdx)];
      for (let k = a; k <= b; k++) on ? S.sel.add(S.rows[k].id) : S.sel.delete(S.rows[k].id);
      getSelection().removeAllRanges();
    } else S.sel.has(id) ? S.sel.delete(id) : S.sel.add(id);
    S.lastIdx = i; drawRows(); updateSel();
  }
});
document.addEventListener("keydown", (ev) => { if (ev.key === "Escape") $("#pvpop").hidden = true; });
document.addEventListener("input", (ev) => {
  const t = ev.target, f = S.filt[S.tab];
  if (!f) return;
  if (t.id === "q") { f.q = t.value; S.rows = filteredRows(S.tab); updateList(true); writeHash(); }
});
document.addEventListener("change", (ev) => {
  const t = ev.target, f = S.filt[S.tab];
  if (!f) return;
  if (t.id === "origin") f.origin = t.value;
  else if (t.id === "cat") f.cat = t.value;
  else if (t.id === "ed") f.ed = t.value;
  else if (t.id === "pendingOnly") f.pending = t.checked;
  else return;
  renderAssets(S.tab);
});
let rafPending = false;
document.addEventListener("scroll", (ev) => {
  if (ev.target.id !== "vbody" || rafPending) return;
  rafPending = true; requestAnimationFrame(() => { rafPending = false; drawRows(); });
}, true);
window.addEventListener("focus", () => { if (S.links && Date.now() - linksAt > 5000) checkLinks(); });
window.addEventListener("resize", () => { if (S.tab !== "changes") drawRows(); });
window.addEventListener("hashchange", () => { if (applyHash()) setTab(S.tab); });
document.addEventListener("visibilitychange", () => { if (!document.hidden) poll(); });
setInterval(poll, 4000);                                                // (a stat check on the server: cheap)

// ---- start ----------------------------------------------------------------------------------------------------------
(function start() {
  S.tab = store.get("tab", "changes");
  if (!VIEWS.includes(S.tab)) S.tab = "changes";
  S.program = progId(store.get("program", "overview"));
  applyHash();                                                          // (a deep link wins over the remembered view)
  for (const b of document.querySelectorAll("#tabs [data-tab]")) b.classList.toggle("on", b.dataset.tab === S.tab);
  loadAll().catch((e) => { $("#view").innerHTML = `<div class="pane"><p class="err">Couldn't load: ${esc(e.message)}. Is dashboard.py still running?</p></div>`; });
})();
