/* Quark Editor Manager page: the editors only (GitHub, Claude Documents, game code and tooling are the Supervisor
   Board's). Talks only to dashboard.py on 127.0.0.1:
     GET  /api/status /api/changes /api/assets      the cached snapshot (dashboard.py builds it at start and on Refresh)
     POST /api/refresh {fetch}                      rebuild it (fetch: git fetch in each clone first)
     POST /api/mark {ids, origin, by, note | clear} the dashboard's own sidecar (never a git clone)
   Views render with innerHTML from plain data; the asset list is virtualized (only the rows on screen are in the DOM),
   other long lists show their first LIST_MAX entries until "show all". */
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
const ROW_H = 30;

const S = {
  summary: null, changes: null, assets: null,
  tab: "changes", program: "overview", expanded: new Set(),
  filt: { assets: { q: "", origin: "", cat: "", pending: false }, remaining: { q: "", origin: "ai", cat: "", pending: false } },
  sel: new Set(), lastIdx: -1, markOrigin: "human", rows: [],
};

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
const plural = (n, w) => n + " " + w + (n === 1 ? "" : "s");
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
  if (!r.ok) throw new Error(j.error || r.statusText);
  return j;
}

// ---- loading --------------------------------------------------------------------------------------------------------
async function loadAll() {
  const [summary, changes, assets] = await Promise.all([api("/api/status"), api("/api/changes"), api("/api/assets")]);
  Object.assign(S, { summary, changes, assets });
  renderHeader(); renderNav(summary.links); render();
  checkLinks();                                                         // (live: the snapshot's may be old)
}
async function refresh() {
  const b = $("#refresh"), fetchToo = $("#fetch").checked;
  b.disabled = true; b.textContent = fetchToo ? "Fetching..." : "Refreshing...";
  try {
    await api("/api/refresh", { fetch: fetchToo });
    await loadAll();
    const fe = S.summary.fetchErrors || [];
    toast(fe.length ? "fetch failed in: " + fe.join(", ") : "Refreshed in " + (S.summary.ms / 1000).toFixed(2) + " s", fe.length > 0);
  } catch (e) { toast("Refresh failed: " + e.message, true); }
  b.disabled = false; b.textContent = "Refresh";
}

// ---- the nav bar ---------------------------------------------------------------------------------------------------------
// The Supervisor Board (8768) is a sibling local server: dashboard.py checks it answers and the bar dims it when it
// doesn't. Each editor's page is served by dashboard.py under /editors/<folder>/ (Chrome / Edge don't follow a file://
// link from an http page).
let linksAt = 0;
const sib = (id) => ((S.links || {}).siblings || []).find((x) => x.id === id);
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
  $("#navEditors").innerHTML = L.editors.map((e) => `<a href="${esc(e.url)}" target="quark-${esc(e.folder)}" title="${esc(e.file)} (served from here: its browser autosave belongs to this address)">${esc(e.program)}</a>`).join("");
}
async function checkLinks() {
  linksAt = Date.now();
  try { renderNav(await api("/api/links")); } catch { /* the server is gone: loadAll says so */ }
}
const downText = (x) => "not running" + (x.start ? ": start " + x.start : "");

// ---- deep links: #view=changes&program=<id> (an editor's folder name), #view=assets&category=<cat>&origin=<o>&q=<text>, #view=remaining&category=,
// #asset=<id> (and the old #changes / #assets / #remaining). The hash follows what is on screen (replaceState).
const VIEWS = ["changes", "assets", "remaining"];
const PROGRAM_ALIAS = { "ui-editor": "ui-customizer" };               // (the UI editor's id is its folder's name)
const progId = (s) => { const v = String(s || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, ""); return PROGRAM_ALIAS[v] || v; };
function applyHash() {
  const raw = location.hash.slice(1);
  if (!raw) return false;
  if (VIEWS.includes(raw)) { S.tab = raw; return true; }
  const p = new URLSearchParams(raw);
  if (p.has("asset")) {
    S.tab = "assets"; Object.assign(S.filt.assets, { q: p.get("asset"), origin: "", cat: "", pending: false });
    S.focusAsset = p.get("asset"); return true;
  }
  const v = p.get("view");
  if (!VIEWS.includes(v)) return false;
  S.tab = v;
  if (v === "changes") { if (p.has("program")) S.program = progId(p.get("program")) || "overview"; }
  else {
    const f = S.filt[v];
    f.cat = p.get("category") || ""; f.q = p.get("q") || ""; f.pending = p.get("pending") === "1";
    if (v === "assets") f.origin = p.get("origin") || "";
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
    if (f.q) p.set("q", f.q);
    if (f.pending) p.set("pending", "1");
  }
  const h = "#" + p.toString();
  if (location.hash !== h) history.replaceState(null, "", h);
}

// ---- header ---------------------------------------------------------------------------------------------------------
function renderHeader() {
  const s = S.summary, h = s.human, c = h.counts;
  const ver = (x) => x.version ? `<b>${esc(x.version)}</b>` : `<b class="muted">none</b>`;
  $("#facts").innerHTML = `
    <div class="fact"><span class="k">Testing</span>${ver(s.testing)}<span class="muted small mono" title="${esc(s.testing.ref || "no origin/testing")}">${esc(s.testing.sha || "no origin/testing")}</span></div>
    <div class="fact"><span class="k">Production</span>${ver(s.production)}<span class="muted small mono">${esc(s.production.tag || "no v<semver> tag")}</span></div>
    <div class="fact"><span class="k">Human-made</span><b>${h.humanPct == null ? "-" : h.humanPct + " %"}</b>${bar(c.human, c["third-party"], c.ai)}</div>
    <div class="fact"><span class="k">Last refresh</span><b>${esc(new Date(s.refreshed * 1000).toLocaleTimeString())}</b><span class="muted small">${s.fetched ? "with fetch" : "no fetch"}</span></div>
    ${s.noClones ? `<div class="fact err">no git clones found in ${esc(s.quark)} (use --quark)</div>` : ""}`;
  $("#cost").textContent = `${s.ms} ms, ${s.gitCalls} git calls · ${s.quark}`;
  const own = S.assets ? S.assets.ownPending : 0;
  $('#tabs [data-tab="assets"]').innerHTML = "Assets: AI vs human" + (own ? `<span class="n" title="your marks not in the repo yet">${own}</span>` : "");
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

// ---- view 1: changes by editor ----------------------------------------------------------------------------------------
function progCounts(p) {
  const local = p.files.filter(([, sts]) => Object.values(sts).includes("local")).length;
  return { local, testing: p.testing.length, exports: p.exports.length };
}
function renderChanges() {
  const ch = S.changes, progs = ch.programs;
  let side = `<button data-prog="overview" class="${S.program === "overview" ? "on" : ""}"><span class="nm">Overview</span></button><div class="sep"></div>`;
  for (const p of progs) {
    const c = progCounts(p);
    side += `<button data-prog="${esc(p.id)}" class="${S.program === p.id ? "on" : ""}"><span class="nm">${esc(p.name)}</span>
      ${c.local ? `<span class="cnt l" title="files only local">${c.local}</span>` : ""}
      ${c.testing ? `<span class="cnt t" title="commits in testing, not production">${c.testing}</span>` : ""}
      ${c.exports ? `<span class="cnt e" title="exported files">${c.exports}</span>` : ""}</button>`;
  }
  side += `<div class="sep"></div><div class="legend small">
    <div><span class="cnt l">n</span> files only local</div>
    <div><span class="cnt t">n</span> testing, not production</div>
    <div><span class="cnt e">n</span> editor exports</div></div>`;
  const p = progs.find((x) => x.id === S.program);
  if (!p) S.program = "overview";
  writeHash();
  $("#view").innerHTML = `<aside class="side" id="side">${side}</aside><section class="pane" id="pane">${p ? programPane(p) : overviewPane()}</section>`;
}

function overviewPane() {
  const ch = S.changes, s = S.summary;
  let h = `<h2>Overview</h2>`;
  h += `<h3>By editor <span class="muted">compared with ${esc(s.testing.ref || "testing")} ${esc(s.testing.sha)} and ${esc(s.production.tag || "production")}</span></h3>`;
  h += `<div class="card"><table class="t"><tr><th>Editor</th><th>Only local files</th><th>Local commits</th><th>Uncommitted</th><th>Testing, not production</th><th>Exports</th><th>Newest export</th></tr>`;
  for (const p of ch.programs) {
    const c = progCounts(p);
    const commits = new Set(), unc = p.clones.reduce((n, x) => n + x.uncommitted.length, 0);
    for (const x of p.clones) for (const cm of x.commits) commits.add(cm.sha);
    const newest = p.exports.length ? p.exports[0].mtime : 0;
    h += `<tr><td><a href="#view=changes&amp;program=${esc(p.id)}" data-prog="${esc(p.id)}">${esc(p.name)}</a></td><td class="num">${c.local || ""}</td><td class="num">${commits.size || ""}</td>
      <td class="num">${unc || ""}</td><td class="num">${c.testing || ""}</td><td class="num">${c.exports || ""}</td><td>${when(newest)}</td></tr>`;
  }
  h += `</table></div>`;
  // the clones, only as far as the editors' files go
  h += `<h3>Working clones <span class="muted">the editors' files only</span></h3>`;
  h += `<div class="card"><table class="t"><tr><th>Clone</th><th>Local commits</th><th>Uncommitted</th><th>Only local files</th><th>Notes</th></tr>`;
  for (const c of ch.clones) {
    const notes = [];
    if (c.role === "mirror") notes.push("mirror of testing (only pulled)");
    if (c.outside) notes.push(`<span class="muted mono small">${esc(c.path)}</span>`);
    if (c.stale) notes.push(`<span class="warn" title="this clone's origin/testing is ${esc(c.testingSha)}: Refresh with fetch">origin/testing here is ${esc(c.testingSha)}</span>`);
    for (const e of c.errors) notes.push(`<span class="err">${esc(e)}</span>`);
    h += `<tr><td class="nowrap"><b>${esc(c.name)}</b></td><td class="num" title="commits not in testing that touch an editor's files">${c.commits || ""}</td>
      <td class="num">${c.uncommitted ? `<span class="st st-unc">${c.uncommitted}</span>` : ""}</td><td class="num">${c.local || ""}</td><td>${notes.join(" · ")}</td></tr>`;
  }
  if (!ch.clones.length) h += `<tr><td colspan="5" class="empty">no clones found</td></tr>`;
  h += `</table></div>`;
  h += `<h3>Editor folders</h3><div class="card"><table class="t"><tr><th>Folder</th><th>Editor</th><th>Page</th><th>Exported files</th></tr>`;
  for (const e of ch.editors) {
    const ex = ch.exports.find((x) => x.name === e.folder), pg = ((S.links || {}).editors || []).find((x) => x.folder === e.folder);
    const page = pg ? pg.pages.map((n) => `<a href="/editors/${esc(e.folder)}/${esc(encodeURIComponent(n))}" target="_blank" rel="noopener">${esc(n)}</a>`).join(" · ") : `<span class="muted">-</span>`;
    h += `<tr><td class="mono">${esc(e.folder)}</td><td>${esc(e.program)}</td><td>${page}</td><td>${e.present ? (ex ? ex.files + (ex.more ? ` (+${ex.more} not listed)` : "") : 0) : `<span class="muted">not installed</span>`}</td></tr>`;
  }
  for (const ex of ch.exports.filter((x) => !ch.editors.some((e) => e.folder === x.name)))
    h += `<tr><td class="mono">${esc(ex.name)}</td><td class="muted">by path</td><td></td><td>${ex.files} <span class="muted small mono">${esc(ex.path)}</span></td></tr>`;
  h += `</table></div>`;
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

function programPane(p) {
  const s = S.summary;
  let h = `<h2>${esc(p.name)}</h2>`;
  // local work
  h += `<h3>Local work <span class="muted">commits ahead of testing and uncommitted files, in every working clone</span></h3>`;
  if (!p.clones.length) h += `<div class="empty">nothing local</div>`;
  for (const c of p.clones) {
    h += `<div class="card"><div class="hd"><b>${esc(c.clone)}</b><span class="muted">${plural(c.commits.length, "commit")} not in testing · ${c.uncommitted.length} uncommitted</span></div>`;
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
  // exports
  h += `<h3>Exported files <span class="muted">editor folders and this PC's overrides folder (not in git)</span></h3>`;
  if (!p.exports.length) h += `<div class="empty">no exports</div>`;
  else {
    const REPO = { same: ["in repo", "st-both", "the same file is in the registry clone's assets/overrides"], differs: ["differs from repo", "st-local", "assets/overrides has a different file"], missing: ["not in repo", "st-local", "not in the registry clone's assets/overrides yet"] };
    h += `<div class="card"><table class="t"><tr><th>Where</th><th>File</th><th>Override</th><th>Date</th><th class="num">Size</th></tr>`;
    h += showMore("e:" + p.name, p.exports, (e) => {
      const r = REPO[e.repo];
      return `<tr><td class="mono">${esc(e.source)}</td><td class="mono">${esc(e.path)}</td><td>${r ? `<span class="st ${r[1]}" title="${esc(r[2])}">${r[0]}</span>` : ""}</td><td class="nowrap">${stamp(e.mtime)} <span class="muted">· ${when(e.mtime)}</span></td><td class="num">${size(e.size)}</td></tr>`;
    });
    h += `</table></div>`;
  }
  return h;
}

// ---- views 2 + 3: assets (all, or the remaining AI-made ones) --------------------------------------------------------------
const effOrigin = (r) => (r.p && r.p[0].origin) || r.o;
function filteredRows(mode) {
  const f = S.filt[mode], q = f.q.trim().toLowerCase();
  return S.assets.rows.filter((r) => {
    if (f.cat && r.cat !== f.cat) return false;
    if (f.pending && !r.p) return false;
    if (f.origin) {
      if (f.origin === "pending") { if (!r.p) return false; }
      else if (r.o !== f.origin) return false;
    }
    if (q && !(r.id.toLowerCase().includes(q) || (r.source || "").toLowerCase().includes(q) || (r.note || "").toLowerCase().includes(q) || (r.by || "").toLowerCase().includes(q))) return false;
    return true;
  });
}

function renderAssets(mode) {
  const A = S.assets, f = S.filt[mode], stats = A.stats, cats = Object.keys(stats).sort();
  const tot = { human: 0, "third-party": 0, ai: 0, pending: 0, eff: { human: 0, "third-party": 0, ai: 0 } };
  for (const c of cats) { for (const o of ORIGINS) { tot[o] += stats[c][o]; tot.eff[o] += stats[c].eff[o]; } tot.pending += stats[c].pending; }
  const n = tot.human + tot["third-party"] + tot.ai;
  const effH = tot.eff.human + tot.eff["third-party"], regH = tot.human + tot["third-party"];
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
      ${tot.pending ? `<div class="small" style="margin-top:4px">with the pending marks: <b>${pct(effH, n)} %</b> (${plural(tot.pending, "pending item")})</div>` : ""}</div>`;
  }
  // per category
  h += `<div class="card"><div class="cats">`;
  for (const c of cats) {
    const s = stats[c], cn = s.human + s["third-party"] + s.ai;
    const right = mode === "remaining" ? `${s.ai} left of ${cn}` : `${s.human} h · ${s["third-party"]} 3p · ${s.ai} AI${s.pending ? ` · <span class="warn">${s.pending} pending</span>` : ""}`;
    h += `<span class="c ${f.cat === c ? "on" : ""}" data-cat="${esc(c)}">${esc(c)}</span>${bar(s.human, s["third-party"], s.ai)}<span class="pct">${mode === "remaining" ? aiPct(s.ai, cn) : pct(s.human + s["third-party"], cn)}%</span><span class="n">${right}</span>`;
  }
  if (!cats.length) h += `<span class="empty" style="grid-column:1/5">no registry items</span>`;
  h += `</div></div>`;
  // pending marks + merge command
  const own = A.sidecars.find((x) => x.own), others = A.sidecars.filter((x) => !x.own && x.pending);
  h += `<div class="card"><div><b>${A.ownPending}</b> ${A.ownPending === 1 ? "mark" : "marks"} not in the repo yet${others.length ? `, <b>${others.reduce((a, x) => a + x.pending, 0)}</b> pending from editors` : ""}</div>
    <div class="sidecars">${A.sidecars.map((x) => `<div title="${esc(x.path)}"><span class="og ${x.pending ? "og-pending" : "og-human"}">${esc(x.tool)}</span><span>${x.pending} pending / ${x.items}</span><span class="muted">${when(x.mtime)}</span><span class="muted mono small ell">${esc(x.short)}</span></div>`).join("") || `<span class="muted">no sidecars yet</span>`}</div>
    ${A.mergeCmd ? `<div class="small muted" style="margin-top:6px" title="${esc(A.mergeCwd)}">fold them into the registry: run this in <span class="mono">${esc(A.mergeCwd.split(/[\\/]/).pop())}</span></div><div class="cmd"><code id="mergecmd">${esc(A.mergeCmd)}</code><button data-copy="mergecmd">Copy</button></div>` : `<div class="small muted" style="margin-top:6px">nothing to merge</div>`}
    ${A.errors.length ? `<div class="err small" style="margin-top:6px">${A.errors.map(esc).join("<br>")}</div>` : ""}
    ${own ? "" : `<div class="muted small" style="margin-top:4px">your marks go to <span class="mono">${esc(A.ownSidecar)}</span></div>`}</div>`;
  h += `</div>`;
  // toolbar
  const opt = (v, l, cur) => `<option value="${esc(v)}" ${v === cur ? "selected" : ""}>${esc(l)}</option>`;
  h += `<div class="toolbar">
    <input type="search" id="q" placeholder="search id, source, note, by" value="${esc(f.q)}">
    ${mode === "remaining" ? "" : `<select id="origin">${opt("", "every origin", f.origin)}${opt("human", "human", f.origin)}${opt("third-party", "third-party", f.origin)}${opt("ai", "AI", f.origin)}${opt("pending", "pending", f.origin)}</select>`}
    <select id="cat">${opt("", "every category", f.cat)}${cats.map((c) => opt(c, c, f.cat)).join("")}</select>
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
  // the list
  h += `<div class="vt"><div class="vrow vhead"><span><input type="checkbox" id="selHead" title="select every shown item"></span><span>Asset</span><span>Category</span><span>Origin</span><span>Pending</span><span>By / date</span><span>Source / note</span></div>
    <div class="vbody" id="vbody"><div id="vspace"></div></div><div class="vfoot" id="vfoot"></div></div>`;
  $("#view").innerHTML = `<section class="assets">${h}</section>`;
  S.rows = filteredRows(mode); S.lastIdx = -1;
  updateList(true);
  writeHash();
  if (S.focusAsset) {                                                   // (#asset=<id>: select it and bring it into view)
    const i = S.rows.findIndex((r) => r.id === S.focusAsset);
    S.focusAsset = null;
    if (i >= 0) { S.sel.add(S.rows[i].id); S.lastIdx = i; $("#vbody").scrollTop = Math.max(0, (i - 3) * ROW_H); drawRows(); updateSel(); }
  }
}

function updateList(resetScroll) {
  const body = $("#vbody");
  if (!body) return;
  if (resetScroll) body.scrollTop = 0;
  $("#vspace").style.height = S.rows.length * ROW_H + "px";
  $("#count").textContent = `${S.rows.length} of ${S.assets.rows.length} shown`;
  drawRows();
  updateSel();
}
function drawRows() {
  const body = $("#vbody");
  if (!body) return;
  const first = Math.max(0, Math.floor(body.scrollTop / ROW_H) - 8), last = Math.min(S.rows.length, Math.ceil((body.scrollTop + body.clientHeight) / ROW_H) + 8);
  let h = "";
  for (let i = first; i < last; i++) {
    const r = S.rows[i], p = r.p && r.p[0];
    const pend = p ? `<span class="og og-pending" title="${esc(r.p.map((x) => `${x.tool}: ${x.origin}${x.by ? " by " + x.by : ""}${x.date ? " " + x.date : ""}${x.note ? " (" + x.note + ")" : ""}`).join("\n"))}">${esc(ORIGIN_LABEL[p.origin] || p.origin || "?")}</span> <span class="muted small">${esc(p.tool)}${r.p.length > 1 ? " +" + (r.p.length - 1) : ""}</span>` : "";
    const src = [r.source, r.note && "note: " + r.note, r.wip && "wip: " + r.wip].filter(Boolean).join(" · ");
    h += `<div class="vrow ${S.sel.has(r.id) ? "sel" : ""}" data-i="${i}" style="top:${i * ROW_H}px"><span><input type="checkbox" ${S.sel.has(r.id) ? "checked" : ""}></span>
      <span class="id" title="${esc(r.id)}">${esc(r.id)}</span><span class="muted">${esc(r.cat)}</span><span>${og(r.o)}</span><span>${pend}</span>
      <span class="small">${esc(r.by || "")}${r.date ? ` <span class="muted">${esc(r.date)}</span>` : ""}</span><span class="muted small" title="${esc(src)}">${esc(src)}</span></div>`;
  }
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
  $("#vfoot").innerHTML = `<span>${plural(S.rows.length, "item")}</span><span>${shownSel} of them selected</span><span>click a row to select, Shift+click for a range</span>`;
}

async function mark(clear) {
  const ids = [...S.sel];
  if (!ids.length) return;
  const by = $('#markbar input[name="by"]').value.trim(), note = $('#markbar input[name="note"]').value.trim();
  store.set("by", by);
  try {
    const r = await api("/api/mark", { ids, origin: S.markOrigin, by, note, clear: !!clear });
    S.assets = r.assets; S.summary = r.status; S.sel.clear();
    renderHeader();
    const keep = $("#vbody").scrollTop;
    renderAssets(S.tab); $("#vbody").scrollTop = keep; drawRows();
    toast(clear ? `Removed your mark from ${plural(ids.length, "item")}` : `Marked ${plural(ids.length, "item")} ${ORIGIN_LABEL[S.markOrigin]}`);
  } catch (e) { toast("Mark failed: " + e.message, true); }
}

// ---- events (delegated: the views re-render often) --------------------------------------------------------------------
document.addEventListener("click", (ev) => {
  const t = ev.target;
  const tab = t.closest("#tabs [data-tab]");
  if (tab) return setTab(tab.dataset.tab);
  const prog = t.closest("[data-prog]");
  if (prog) { ev.preventDefault(); S.program = prog.dataset.prog; store.set("program", S.program); renderChanges(); $("#pane").scrollTop = 0; return; }
  const more = t.closest("[data-more]");
  if (more) { const k = more.dataset.more; S.expanded.has(k) ? S.expanded.delete(k) : S.expanded.add(k); const sc = $("#pane").scrollTop; renderChanges(); $("#pane").scrollTop = sc; return; }
  const cp = t.closest("[data-copy]");
  if (cp) {
    const txt = $("#" + cp.dataset.copy).textContent;
    (navigator.clipboard ? navigator.clipboard.writeText(txt) : Promise.reject()).then(() => toast("Copied"), () => {
      const r = document.createRange(); r.selectNodeContents($("#" + cp.dataset.copy)); getSelection().removeAllRanges(); getSelection().addRange(r); toast("Selected: press Ctrl+C");
    });
    return;
  }
  const cat = t.closest(".cats [data-cat]");
  if (cat) { const f = S.filt[S.tab]; f.cat = f.cat === cat.dataset.cat ? "" : cat.dataset.cat; renderAssets(S.tab); return; }
  const o = t.closest("#markbar [data-origin]");
  if (o) { S.markOrigin = o.dataset.origin; for (const b of document.querySelectorAll("#markbar [data-origin]")) b.classList.toggle("on", b === o); updateSel(); return; }
  if (t.id === "markGo") return mark(false);
  if (t.id === "markClear") return mark(true);
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
    if (ev.shiftKey && S.lastIdx >= 0) {
      const on = !S.sel.has(id), [a, b] = [Math.min(i, S.lastIdx), Math.max(i, S.lastIdx)];
      for (let k = a; k <= b; k++) on ? S.sel.add(S.rows[k].id) : S.sel.delete(S.rows[k].id);
      getSelection().removeAllRanges();
    } else S.sel.has(id) ? S.sel.delete(id) : S.sel.add(id);
    S.lastIdx = i; drawRows(); updateSel();
  }
});
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

// ---- start ----------------------------------------------------------------------------------------------------------
(function start() {
  S.tab = store.get("tab", "changes");
  if (!VIEWS.includes(S.tab)) S.tab = "changes";
  S.program = progId(store.get("program", "overview"));
  applyHash();                                                          // (a deep link wins over the remembered view)
  for (const b of document.querySelectorAll("#tabs [data-tab]")) b.classList.toggle("on", b.dataset.tab === S.tab);
  loadAll().catch((e) => { $("#view").innerHTML = `<div class="pane"><p class="err">Couldn't load: ${esc(e.message)}. Is dashboard.py still running?</p></div>`; });
})();
