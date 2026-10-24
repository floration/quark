'use strict';
// Quark Project: the Electron shell (tools/project_manager/SPEC.md, section 1).
// One window: a 34 px tab bar view on top and one WebContentsView per tab, created on first open and kept alive.
// Runs on the plain prebuilt electron.exe (no npm packages): `electron.exe <this folder> [--quark=<dir>] [--tab=<id>]`.
//
// Layout once installed:  Quark\project-manager\shell\main.js   (this file)
//                         Quark\project-manager\tabs.json        (q's tab list; shell\tabs.default.json if missing)
//                         Quark\project-manager\quark.ico
// Data folder (Windows: %LOCALAPPDATA%\QuarkProject, off the Desktop so OneDrive never syncs a live Chromium
// profile; elsewhere: Quark\project-manager):
//                         <data>\state.json       (window bounds, last tab, last download folder)
//                         <data>\electron-data\   (Chromium profile: one persist:<id> partition per tab)
//                         <data>\logs\<id>.log    (output of servers the shell started, and the watchdog's notes)
// Automatic updates (pm/updater.py): the Supervisor Board server installs the tools; this shell polls its
// /api/updates, restarts the servers whose tool was reinstalled, and offers a restart when the app itself was updated.

const { app, BaseWindow, WebContentsView, Menu, session, shell, ipcMain, dialog, nativeTheme, nativeImage, screen } = require('electron');
const path = require('path');
const fs = require('fs');
const http = require('http');
const os = require('os');
const { spawn, spawnSync, execFile } = require('child_process');
const { pathToFileURL, fileURLToPath } = require('url');

const IS_WIN = process.platform === 'win32';
const TABBAR_H = 34;
const WINDOW_BUTTONS_W = process.platform === 'darwin' ? 0 : 140; // (the window buttons drawn over the tab bar)
const START_TIMEOUT_MS = 20000;
const PROBE_TIMEOUT_MS = 500;
const STATUS_POLL_MS = 5000;
// keepAlive servers (supervisor): started at app start, probed every WATCHDOG_MS, restarted when down. The wait before a
// restart grows with the restarts in the last FAIL_WINDOW_MS: 10 s, 30 s, 60 s, then every 2 min.
const WATCHDOG_MS = 10000;
const RESTART_DELAYS_MS = [10000, 30000, 60000, 120000];
const FAIL_WINDOW_MS = 5 * 60000;
const WATCH_PROBE_TIMEOUT_MS = 1500; // (the watchdog is slower to call a busy server dead than the tab probe)
const LOG_MAX = 4e6;
const LOCAL_PERMISSIONS = new Set(['fileSystem', 'clipboard-sanitized-write', 'media', 'pointerLock']);
// automatic updates: GET <Supervisor Board>/api/updates every UPDATES_POLL_MS (the first one a few seconds after start)
const UPDATES_POLL_MS = 15000;
const UPDATES_FIRST_MS = 3000;
const PM_PORT_DEFAULT = 8768;

// ---------- paths ----------
function argValue(name, argv = process.argv) {
  const pre = '--' + name + '=';
  const a = argv.find((x) => x.startsWith(pre));
  return a ? a.slice(pre.length).replace(/^"(.*)"$/, '$1') : null;
}
const SHELL_DIR = __dirname;
const PM_DIR = path.resolve(SHELL_DIR, '..');
const QUARK_DIR = path.resolve(argValue('quark') || path.join(PM_DIR, '..'));
const DATA_DIR = IS_WIN && process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA, 'QuarkProject') : PM_DIR;
const STATE_FILE = path.join(DATA_DIR, 'state.json');
const LOG_DIR = path.join(DATA_DIR, 'logs');
const SELFTEST_SHOT = argValue('selftest-shot');
const SELFTEST_SCRIPT = process.env.QUARK_SHELL_SELFTEST === '1' ? argValue('selftest') : null;

// The Chromium profile lives next to state.json so everything the app keeps is in Quark\project-manager\.
// Must be set before the single-instance lock (the lock file is in userData).
try { fs.mkdirSync(DATA_DIR, { recursive: true }); } catch { /* setPath below still works or fails loudly */ }
app.setPath('userData', path.join(DATA_DIR, 'electron-data'));
app.setName('Quark Project');
if (IS_WIN) app.setAppUserModelId('Quark.ProjectManager');

if (!app.requestSingleInstanceLock({ tab: argValue('tab') })) {
  app.quit();
  process.exit(0);
}

// ---------- small helpers ----------
const readJSON = (file, fallback) => {
  try { return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '')); } catch { return fallback; }
};
const isDir = (p) => { try { return fs.statSync(p).isDirectory(); } catch { return false; } };
const isFile = (p) => { try { return fs.statSync(p).isFile(); } catch { return false; } };
const inside = (child, parent) => {
  const rel = path.relative(parent, child);
  return rel === '' || (!!rel && !rel.startsWith('..') && !path.isAbsolute(rel));
};
const expandVars = (s) => String(s)
  .replace(/%PM%/gi, PM_DIR)
  .replace(/%QUARK%/gi, QUARK_DIR);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const parseURL = (u) => { try { return new URL(u); } catch { return null; } };
const isLoopbackHost = (h) => h === '127.0.0.1' || h === 'localhost' || h === '[::1]';
function isLocalURL(u) {
  const p = parseURL(u);
  return !!p && (p.protocol === 'file:' || ((p.protocol === 'http:' || p.protocol === 'https:') && isLoopbackHost(p.hostname)));
}

// ---------- tabs.json ----------
// Same resolution as pm/tabs.py (SPEC "tabs.json" rules).
function firstHtml(folder) {
  try {
    return fs.readdirSync(folder).filter((f) => /\.html?$/i.test(f) && isFile(path.join(folder, f))).sort()[0] || null;
  } catch { return null; }
}
function resolveStart(t, folder) {
  if (t.start) {
    // a relative program name ("run.cmd") is the file in the tab's folder
    const cmd = expandVars(t.start);
    const m = /^\s*(?:"([^"]+)"|(\S+))(.*)$/.exec(cmd);
    const prog = m && (m[1] || m[2]);
    if (prog && folder && !/[\\/]/.test(prog) && isFile(path.join(folder, prog))) return '"' + path.join(folder, prog) + '"' + m[3];
    return cmd;
  }
  if (!folder || !isDir(folder)) return null;
  const base = path.basename(folder).toLowerCase();
  let cmds = [];
  try { cmds = fs.readdirSync(folder).filter((f) => /\.cmd$/i.test(f)).sort(); } catch { /* none */ }
  const named = cmds.find((f) => f.toLowerCase().includes(base));
  if (named) return '"' + path.join(folder, named) + '"';
  if (isFile(path.join(folder, 'start.cmd'))) return '"' + path.join(folder, 'start.cmd') + '"';
  return null;
}
function resolveTab(t, i) {
  const id = String(t.id || 'tab' + i).replace(/[^A-Za-z0-9_-]/g, '-');
  const name = String(t.name || t.id || 'Tab ' + (i + 1));
  const folder = t.folder ? path.resolve(QUARK_DIR, expandVars(t.folder)) : null;
  const tab = { id, name, folder, installed: true, kind: 'file', url: null, port: null, start: null, missing: '',
    parent: t.parent ? String(t.parent).replace(/[^A-Za-z0-9_-]/g, '-') : null,
    childLabel: t.childLabel ? String(t.childLabel) : '', depth: 0,
    keepAlive: t.keepAlive === true, lowPriority: t.lowPriority === true };
  if (t.url) {
    const url = expandVars(t.url);
    const p = parseURL(url);
    tab.url = url;
    if (p && (p.protocol === 'http:' || p.protocol === 'https:')) {
      tab.kind = 'server';
      tab.port = Number(t.port) || Number(p.port) || (p.protocol === 'https:' ? 443 : 80);
      tab.origin = p.origin;
      tab.probeHost = isLoopbackHost(p.hostname) ? p.hostname.replace(/^\[|\]$/g, '') : null; // only local servers are probed/started
      tab.start = tab.probeHost ? resolveStart(t, folder) : null;
      tab.cwd = folder && isDir(folder) ? folder : PM_DIR;
      if (folder && !isDir(folder)) { tab.installed = false; tab.missing = 'folder ' + folder; }
    } else if (p && p.protocol === 'file:') {
      tab.file = fileURLToPath(p);
    } else {
      tab.installed = false; tab.missing = 'unsupported url ' + url;
    }
  }
  if (tab.kind === 'file') {
    if (!tab.file) {
      if (!folder) { tab.installed = false; tab.missing = 'no folder, file or url'; }
      else if (!isDir(folder)) { tab.installed = false; tab.missing = 'folder ' + folder; }
      else if (t.file) tab.file = path.resolve(folder, expandVars(t.file));
      else {
        const named = path.basename(folder) + '.html';
        tab.file = path.join(folder, isFile(path.join(folder, named)) ? named
          : isFile(path.join(folder, 'index.html')) ? 'index.html' : (firstHtml(folder) || named));
      }
    }
    if (tab.installed && !isFile(tab.file)) { tab.installed = false; tab.missing = 'file ' + tab.file; }
    tab.fileDir = tab.file ? path.dirname(tab.file) : folder;
    if (tab.file) tab.url = pathToFileURL(tab.file).href;
  }
  return tab;
}
// Tabs form a tree through "parent" (e.g. managers under "project", editors under "dashboard"). The list is put in
// depth-first order (siblings keep their tabs.json order); Ctrl+1..9 and Ctrl+Tab follow that order.
function treeOrder(list) {
  const byId = new Map(list.map((t) => [t.id, t]));
  const kids = new Map();
  for (const t of list) {
    if (!t.parent || t.parent === t.id || !byId.has(t.parent)) { t.parent = null; continue; }
    if (!kids.has(t.parent)) kids.set(t.parent, []);
    kids.get(t.parent).push(t);
  }
  const out = [];
  const seen = new Set();
  const visit = (t, depth) => {
    if (seen.has(t.id)) return;
    seen.add(t.id); t.depth = depth; out.push(t);
    for (const c of kids.get(t.id) || []) visit(c, depth + 1);
  };
  list.filter((t) => !t.parent).forEach((t) => visit(t, 0));
  list.forEach((t) => { if (!seen.has(t.id)) { t.parent = null; visit(t, 0); } }); // a parent cycle: show them as roots
  return out;
}
function loadTabs() {
  let file = path.join(PM_DIR, 'tabs.json');
  let data = readJSON(file, null);
  let error = '';
  if (!data && fs.existsSync(file)) error = 'tabs.json could not be read (bad JSON?); using the default tabs';
  if (!data) { file = path.join(SHELL_DIR, 'tabs.default.json'); data = readJSON(file, { tabs: [] }); }
  const list = Array.isArray(data) ? data : (Array.isArray(data.tabs) ? data.tabs : []);
  const seen = new Set();
  const tabs = [];
  list.forEach((t, i) => {
    if (!t || typeof t !== 'object') return;
    const r = resolveTab(t, i);
    if (seen.has(r.id)) return;
    seen.add(r.id);
    tabs.push(r);
  });
  return { tabs: treeOrder(tabs), file, error };
}

// ---------- state.json ----------
// (state.json used to live in Quark\project-manager on Windows too: read that once if the new one is missing)
let state = readJSON(STATE_FILE, null) || (DATA_DIR !== PM_DIR && readJSON(path.join(PM_DIR, 'state.json'), null)) || {};
let saveTimer = null;
function saveState(now) {
  clearTimeout(saveTimer);
  const write = () => {
    try { fs.writeFileSync(STATE_FILE, JSON.stringify(state, null, 2)); } catch { /* read-only folder: ignore */ }
  };
  if (now) write(); else saveTimer = setTimeout(write, 800);
}
function rememberBounds() {
  if (!win || win.isDestroyed()) return;
  state.maximized = win.isMaximized();
  if (!state.maximized && !win.isMinimized() && !win.isFullScreen()) state.bounds = win.getBounds();
  saveState();
}
function initialBounds() {
  const b = state.bounds;
  const def = { width: 1440, height: 900 };
  if (!b || !(b.width > 300) || !(b.height > 200)) return def;
  // only reuse the position if it is still on a connected display
  const onScreen = screen.getAllDisplays().some((d) => {
    const a = d.workArea;
    return b.x < a.x + a.width - 80 && b.x + b.width > a.x + 80 && b.y >= a.y - 10 && b.y < a.y + a.height - 40;
  });
  return onScreen ? { x: b.x, y: b.y, width: b.width, height: b.height } : { width: b.width, height: b.height };
}

// ---------- servers ----------
const started = new Map(); // tab id -> { child, pid, port, exited, code, log: string[] }
let pyMissing = null;
function fixPython(cmd) {
  // "py -3 ..." needs the Windows Python launcher; fall back to python (or python3 off Windows) if it is missing.
  if (!/^\s*py(\.exe)?\s/i.test(cmd)) return cmd;
  if (!IS_WIN) return cmd.replace(/^\s*py(\.exe)?\s+(-3\s+)?/i, 'python3 ');
  if (pyMissing === null) pyMissing = spawnSync('where', ['py'], { windowsHide: true }).status !== 0;
  return pyMissing ? cmd.replace(/^\s*py(\.exe)?\s+(-3(\.\d+)?\s+)?/i, 'python ') : cmd;
}
function probe(tab, timeout = PROBE_TIMEOUT_MS) {
  return new Promise((resolve) => {
    if (!tab.probeHost) return resolve(true);
    const req = http.get({ host: tab.probeHost, port: tab.port, path: '/', timeout, headers: { 'User-Agent': 'quark-project-shell' } }, (res) => {
      res.resume();
      resolve(true);
    });
    req.on('timeout', () => { req.destroy(); resolve(false); });
    req.on('error', () => resolve(false));
  });
}
function splitCommand(cmd) {
  // 'py -3 serve.py --docs "C:\a b"' -> ['py', '-3', 'serve.py', '--docs', 'C:\a b'] when the command is one program
  // with plain arguments (no cmd.exe syntax, balanced quotes, not a .cmd / .bat); else null (it runs through the shell).
  if (/[&|<>^%]/.test(cmd) || (cmd.split('"').length - 1) % 2) return null;
  const parts = cmd.match(/(?:"[^"]*"|[^\s"])+/g);
  if (!parts) return null;
  const argv = parts.map((p) => p.replace(/"/g, ''));
  return argv[0] && !/\.(cmd|bat)$/i.test(argv[0]) ? argv : null;
}
function lowerPriority(pid) {
  // PRIORITY_LOW = IDLE_PRIORITY_CLASS on Windows (what `start /low` gives); processes it starts inherit it
  if (!pid) return;
  try { os.setPriority(pid, os.constants.priority.PRIORITY_LOW); } catch { /* gone, or not allowed */ }
}
const logOpened = new Set(); // tab ids whose log this session already started (later starts append to it)
function logNote(id, line) {
  try {
    fs.mkdirSync(LOG_DIR, { recursive: true });
    fs.appendFileSync(path.join(LOG_DIR, id + '.log'), '[' + new Date().toISOString() + '] ' + line + '\n');
  } catch { /* no log */ }
}
function startServer(tab, why) {
  const prev = started.get(tab.id);
  if (prev && !prev.exited) return prev;
  const cmd = fixPython(tab.start);
  const rec = { cmd, child: null, pid: 0, port: tab.port, exited: false, code: null, log: [], at: Date.now() };
  let logStream = null;
  let logged = 0;
  try {
    fs.mkdirSync(LOG_DIR, { recursive: true });
    const file = path.join(LOG_DIR, tab.id + '.log');
    let size = 0;
    if (logOpened.has(tab.id)) { try { size = fs.statSync(file).size; } catch { /* new file */ } }
    const append = size > 0 && size < LOG_MAX / 2; // first start of the session (or a big log): rewrite it
    logStream = fs.createWriteStream(file, { flags: append ? 'a' : 'w' });
    logStream.on('error', () => { logStream = null; });
    logged = append ? size : 0;
    logOpened.add(tab.id);
    logStream.write('[' + new Date().toISOString() + '] start' + (why ? ' (' + why + ')' : '') + ': ' + cmd + '\n');
  } catch { logStream = null; }
  const keep = (buf) => {
    if (logStream && logged < LOG_MAX) { logged += buf.length; logStream.write(logged < LOG_MAX ? buf : '\n[log cut at 4 MB]\n'); }
    const lines = (rec.log.join('\n') + buf.toString()).split(/\r?\n/);
    rec.log = lines.slice(-40);
  };
  const finish = (code, note) => {
    if (rec.exited) return;
    rec.exited = true; rec.code = code;
    if (logStream) { const s = logStream; logStream = null; s.end('[' + new Date().toISOString() + '] ' + note + '\n'); }
    updateTabbar();
    onServerExit(tab.id, rec);
  };
  try {
    // Windows: never `detached` and never `start` -- with DETACHED_PROCESS Windows ignores CREATE_NO_WINDOW and
    // console grandchildren (py.exe -> python.exe) open a visible console. Hidden + piped output keeps every window
    // hidden; the shell kills the tree with taskkill on close. Elsewhere detached gives a process group to kill.
    // A lowPriority tab with a plain "program args" command is spawned directly (no cmd.exe), so its priority is
    // lowered on the program itself before it starts children (py.exe -> python.exe inherits it). Everything else
    // runs as cmd.exe /d /s /c "<cmd>" so .cmd files, quoting and relative names (run.cmd) work in the tab's folder.
    const opts = {
      cwd: tab.cwd, windowsHide: true, detached: !IS_WIN, stdio: ['ignore', 'pipe', 'pipe'],
      env: Object.assign({}, process.env, { QUARK_SHELL: '1', PYTHONUNBUFFERED: '1' }),
    };
    const argv = tab.lowPriority ? splitCommand(cmd) : null;
    const child = argv ? spawn(argv[0], argv.slice(1), opts) : spawn(cmd, Object.assign({ shell: true }, opts));
    rec.child = child; rec.pid = child.pid;
    if (tab.lowPriority) lowerPriority(child.pid);
    child.stdout.on('data', keep);
    child.stderr.on('data', keep);
    child.on('error', (e) => { keep(Buffer.from('\n' + e.message + '\n')); finish(-1, 'failed: ' + e.message); });
    child.on('exit', (code, signal) => finish(code === null ? (signal || -1) : code, 'exited (code ' + (code === null ? signal : code) + ')'));
  } catch (e) {
    rec.exited = true; rec.code = -1; rec.log = [String(e.message || e)];
    if (logStream) { logStream.end(String(e.message || e) + '\n'); logStream = null; }
  }
  started.set(tab.id, rec);
  return rec;
}
function parseListeningPid(out, port) {
  for (const line of String(out || '').split(/\r?\n/)) {
    const m = line.trim().split(/\s+/);
    if (m.length >= 5 && /LISTEN/i.test(m[3]) && /[:.](\d+)$/.test(m[1]) && Number(RegExp.$1) === port) return Number(m[4]) || 0;
  }
  return 0;
}
function listeningPid(port) {
  // Windows only: the pid that listens on <port> (for servers whose start script handed off and exited).
  const r = spawnSync('netstat', ['-ano', '-p', 'TCP'], { windowsHide: true, encoding: 'utf8' });
  return r.status === 0 ? parseListeningPid(r.stdout, port) : 0;
}
function listeningPidAsync(port) {
  return new Promise((resolve) => {
    if (!IS_WIN) return resolve(0);
    execFile('netstat', ['-ano', '-p', 'TCP'], { windowsHide: true, timeout: 5000 }, (err, out) => resolve(err ? 0 : parseListeningPid(out, port)));
  });
}
function killTree(pid) {
  if (!pid) return;
  if (IS_WIN) spawnSync('taskkill', ['/T', '/F', '/PID', String(pid)], { windowsHide: true, stdio: 'ignore', timeout: 5000 });
  else {
    try { process.kill(-pid, 'SIGTERM'); } catch { try { process.kill(pid, 'SIGTERM'); } catch { /* gone */ } }
  }
}
let stopped = false;
function stopStartedServers() {
  if (stopped) return;
  stopped = true;
  clearTimeout(watchTimer);
  clearTimeout(updatesTimer);
  for (const s of sup.values()) clearTimeout(s.timer);
  for (const rec of started.values()) {
    if (!rec.exited) killTree(rec.pid);
    else if (IS_WIN && rec.code === 0 && rec.cameUp && rec.port) {
      // the start command exited cleanly: it probably launched the real server with `start`; stop that one
      const pid = listeningPid(rec.port);
      if (pid && pid !== process.pid) killTree(pid);
    }
  }
}

// ---------- starting a server, and the supervisor (keepAlive tabs) ----------
// One start at a time per tab: ensureStarted() is shared by the tab view (bringUp), the app-start pass and the
// watchdog. When it ends, a view showing a placeholder ("starting" / "not running") loads the page or shows why not;
// a loaded page is left alone (the tools' pages poll their server and reconnect by themselves).
const waiting = new Map(); // tab id -> Promise<{ up, rec }>
const sup = new Map();     // tab id -> { timer, busy, nextAt, why, attempts: [ms] }  (keepAlive tabs)
let watchTimer = null;
const supFor = (id) => { if (!sup.has(id)) sup.set(id, { timer: null, busy: false, nextAt: 0, why: '', attempts: [] }); return sup.get(id); };
const supervised = (t) => !!(t && t.keepAlive && t.kind === 'server' && t.installed && t.probeHost && t.start);
const restartPending = (id) => { const s = sup.get(id); return !!(s && (s.timer || s.busy)); };

function ensureStarted(tab, why) {
  if (waiting.has(tab.id)) return waiting.get(tab.id);
  const s = sup.get(tab.id);
  if (s && s.timer) { clearTimeout(s.timer); s.timer = null; s.nextAt = 0; }
  startingNow.add(tab.id);
  const e0 = views.get(tab.id);
  if (e0 && e0.mode === 'down') showStarting(e0);
  updateTabbar();
  const p = (async () => {
    let up = false;
    let rec = null;
    try {
      if (await probe(tab)) up = true; // (came up meanwhile: never start a second copy)
      else {
        rec = startServer(tab, why);
        const t0 = Date.now();
        while (Date.now() - t0 < START_TIMEOUT_MS && !stopped) {
          await new Promise((r) => setTimeout(r, 250));
          if (await probe(tab)) { up = true; break; }
          if (rec.exited && rec.code !== 0) break; // failed; exit code 0 = handed off to another process, keep waiting
        }
        if (up) rec.cameUp = true; // the port was down before the start, so whoever listens now is the server the shell started
      }
    } finally {
      waiting.delete(tab.id);
      startingNow.delete(tab.id);
    }
    if (stopped) return { up, rec };
    status.set(tab.id, up ? 'up' : 'down');
    const reason = up ? '' : !rec ? 'It did not start.' : rec.exited ? 'Its start command exited (code ' + rec.code + ').'
      : 'It did not answer within ' + START_TIMEOUT_MS / 1000 + ' s.';
    if (!up && rec) logNote(tab.id, 'not up: ' + reason);
    if (!up && supervised(tab)) {
      scheduleRestart(tab, !rec ? 'start failed' : rec.exited ? 'start exited, code ' + rec.code : 'no answer within ' + START_TIMEOUT_MS / 1000 + ' s');
    }
    if (up && rec && rec.cameUp && tab.lowPriority && IS_WIN) {
      // (a start that went through cmd.exe: also lower the process that actually listens)
      listeningPidAsync(tab.port).then((pid) => { if (pid && pid !== process.pid) lowerPriority(pid); });
    }
    const e = views.get(tab.id);
    if (e && !e.view.webContents.isDestroyed() && (e.mode === 'starting' || e.mode === 'down')) {
      if (up) loadPage(e, e.pendingHash); else showDown(e, reason);
    }
    updateTabbar();
    return { up, rec };
  })();
  waiting.set(tab.id, p);
  return p;
}

function scheduleRestart(tab, why) {
  if (stopped || !supervised(tab) || waiting.has(tab.id) || updateRestarting.has(tab.id)) return;
  const s = supFor(tab.id);
  if (s.timer || s.busy) return;
  const now = Date.now();
  s.attempts = s.attempts.filter((t) => now - t < FAIL_WINDOW_MS);
  const delay = RESTART_DELAYS_MS[Math.min(s.attempts.length, RESTART_DELAYS_MS.length - 1)];
  s.nextAt = now + delay; s.why = why;
  logNote(tab.id, 'watchdog: down (' + why + '); restarting in ' + delay / 1000 + ' s');
  s.timer = setTimeout(() => restartNow(tab.id), delay);
  const e = views.get(tab.id);
  if (e && e.mode === 'down') showDown(e, e.downReason);
  updateTabbar();
}
async function restartNow(id) {
  const s = supFor(id);
  s.timer = null; s.nextAt = 0;
  const tab = tabById(id);
  if (stopped || !supervised(tab) || waiting.has(id)) return;
  s.busy = true;
  try {
    if (await probe(tab, WATCH_PROBE_TIMEOUT_MS)) { status.set(id, 'up'); logNote(id, 'watchdog: it is answering again; no restart'); return; }
    const rec = started.get(id);
    if (rec && !rec.exited) {
      // ours, alive, but not answering (hung): stop it first so the new one can take the port
      logNote(id, 'watchdog: pid ' + rec.pid + ' is not answering; stopping it');
      killTree(rec.pid);
      for (let i = 0; i < 20 && !rec.exited; i++) await new Promise((r) => setTimeout(r, 150));
      if (!rec.exited) { rec.exited = true; rec.code = -1; }
    }
  } finally { s.busy = false; }
  if (stopped || waiting.has(id)) return;
  s.attempts.push(Date.now());
  ensureStarted(tab, 'watchdog restart ' + s.attempts.length + ' in 5 min (' + s.why + ')');
}
async function onServerExit(id, rec) {
  // a supervised server the shell started has exited: restart it (after the backoff) unless something else answers
  if (stopped || waiting.has(id) || restartPending(id) || updateRestarting.has(id)) return; // (a start being waited for handles its own failure)
  const tab = tabById(id);
  if (!supervised(tab) || started.get(id) !== rec) return;
  if (await probe(tab)) return; // handed off to another process that is serving
  status.set(id, 'down');
  scheduleRestart(tab, 'its process exited (code ' + rec.code + ')');
}
async function watchdog() {
  clearTimeout(watchTimer);
  if (stopped) return;
  try {
    const list = tabs.filter((t) => supervised(t) && !waiting.has(t.id) && !restartPending(t.id) && !updateRestarting.has(t.id));
    // (a miss is probed again at once: one slow answer is not a dead server)
    const res = await Promise.all(list.map(async (t) => (await probe(t, WATCH_PROBE_TIMEOUT_MS)) || probe(t, WATCH_PROBE_TIMEOUT_MS)));
    let changed = false;
    list.forEach((t, i) => {
      if (waiting.has(t.id) || restartPending(t.id)) return;
      const st = res[i] ? 'up' : 'down';
      if (status.get(t.id) !== st) { status.set(t.id, st); changed = true; }
      if (!res[i]) scheduleRestart(t, 'not answering on port ' + t.port);
      else { const e = views.get(t.id); if (e && e.mode === 'down') loadPage(e, e.pendingHash); }
    });
    if (changed) updateTabbar();
  } finally {
    if (!stopped) watchTimer = setTimeout(watchdog, WATCHDOG_MS);
  }
}
async function startKeepAlive() {
  // at app start: every keepAlive server that is down starts now (in parallel, hidden), not when its tab is shown
  const list = tabs.filter((t) => supervised(t) && !waiting.has(t.id));
  await Promise.all(list.map(async (t) => {
    if (await probe(t)) { status.set(t.id, 'up'); return; }
    if (!stopped && !waiting.has(t.id)) ensureStarted(t, 'app start');
  }));
  updateTabbar();
  clearTimeout(watchTimer);
  if (!stopped) watchTimer = setTimeout(watchdog, WATCHDOG_MS);
}

// ---------- automatic updates (pm/updater.py, installs.json; shell/README.md "Automatic installs and updates") ----------
// The Supervisor Board server pulls testing into Quark\repository and reinstalls the tools whose sources changed.
// Here: ask for that pass once per app open, restart each server whose tool was reinstalled (its `restart` requests,
// acknowledged so a relaunch never repeats them) and show "updated, reopen" when the app's own files changed.
const updateRestarting = new Set(); // tab ids being restarted for an update (the watchdog keeps out of the way)
const handledRestarts = new Set();  // restart request ids already done this session
let selfUpdated = false;
let updatesTimer = null;
let askedUpdate = false;
let relaunching = false;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function pmPort() {
  const t = tabById('project');
  return (t && t.probeHost && t.port) || PM_PORT_DEFAULT;
}
function pmRequest(method, pathname, timeout = 5000) {
  // -> the JSON answer, or null (server down, not JSON)
  return new Promise((resolve) => {
    const req = http.request({ host: '127.0.0.1', port: pmPort(), path: pathname, method, timeout,
      headers: { 'User-Agent': 'quark-project-shell', 'Content-Length': 0 } }, (res) => {
      const chunks = [];
      res.on('data', (c) => { if (chunks.length < 256) chunks.push(c); });
      res.on('end', () => { try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); } catch { resolve(null); } });
      res.on('error', () => resolve(null));
    });
    req.on('timeout', () => { req.destroy(); resolve(null); });
    req.on('error', () => resolve(null));
    req.end();
  });
}
function commandLine(pid) {
  // the command line of process <pid> ('' when unknown): Get-CimInstance, else wmic (gone from new Windows 11)
  return new Promise((resolve) => {
    pid = Number(pid) || 0;
    if (!pid) return resolve('');
    if (!IS_WIN) {
      try { return resolve(fs.readFileSync('/proc/' + pid + '/cmdline', 'utf8').replace(/\0/g, ' ').trim()); } catch { return resolve(''); }
    }
    execFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
      '(Get-CimInstance Win32_Process -Filter "ProcessId=' + pid + '").CommandLine'],
    { windowsHide: true, timeout: 15000 }, (err, out) => {
      const s = String(out || '').trim();
      if (!err && s) return resolve(s);
      execFile('wmic', ['process', 'where', 'ProcessId=' + pid, 'get', 'CommandLine', '/value'], { windowsHide: true, timeout: 10000 }, (e2, o2) => {
        const m = /CommandLine=(.*)/.exec(String(o2 || ''));
        resolve(!e2 && m ? m[1].trim() : '');
      });
    });
  });
}
async function stopForeignListener(port, folderName, logId) {
  // a server this shell did not start: stop whatever listens on <port> only if its command line names the tool's
  // folder (never some other program that happens to use the port). -> true when it was stopped.
  const pid = await listeningPidAsync(port);
  if (!pid || pid === process.pid) { logNote(logId, 'update: no process found listening on port ' + port + ' (left alone)'); return false; }
  const cl = await commandLine(pid);
  if (!folderName || !cl.toLowerCase().includes(String(folderName).toLowerCase())) {
    logNote(logId, 'update: pid ' + pid + ' on port ' + port + ' does not look like ' + (folderName || 'the tool') + ' ('
      + (cl ? 'command line: ' + cl.slice(0, 240) : 'command line unknown') + '); left alone, restart it by hand');
    return false;
  }
  logNote(logId, 'update: stopping pid ' + pid + ' (' + cl.slice(0, 240) + ')');
  killTree(pid);
  return true;
}
async function restartForUpdate(r) {
  const port = Number(r.port);
  const tab = tabs.find((t) => t.kind === 'server' && t.probeHost && t.port === port && t.installed);
  if (!tab || tab.id === 'project') return; // (no tab on that port; the Supervisor Board itself restarts with the app)
  const id = tab.id;
  const what = String(r.name || r.entry || 'its tool');
  const folderName = tab.folder ? path.basename(tab.folder) : String(r.folder || '');
  let start = false;
  updateRestarting.add(id);
  try {
    if (waiting.has(id)) await waiting.get(id); // (a start already under way finishes first)
    const rec = started.get(id);
    const up = await probe(tab, WATCH_PROBE_TIMEOUT_MS);
    if (rec && !rec.exited) {
      logNote(id, 'update: ' + what + ' was reinstalled; restarting pid ' + rec.pid);
      killTree(rec.pid);
      for (let i = 0; i < 40 && !rec.exited; i++) await sleep(150);
      if (!rec.exited) { rec.exited = true; rec.code = -1; }
      start = true;
    } else if (up) {
      start = await stopForeignListener(port, folderName, id); // (Windows only: elsewhere it is left alone)
    } else {
      // down: a keepAlive server starts now with the new code; any other starts when its tab is opened
      start = supervised(tab);
      logNote(id, 'update: ' + what + ' was reinstalled; it is not running' + (start ? ', starting it' : ' (it starts with the new code when opened)'));
    }
    if (start) for (let i = 0; i < 40 && await probe(tab, 300); i++) await sleep(250); // (until the port is free)
  } finally { updateRestarting.delete(id); }
  if (!start || stopped || !tab.start) return;
  const s = sup.get(id);
  if (s && s.timer) { clearTimeout(s.timer); s.timer = null; s.nextAt = 0; }
  status.set(id, 'down');
  const res = await ensureStarted(tab, 'update: ' + what + ' reinstalled');
  // a loaded page gets the new page files (a page with unsaved changes asks first)
  const e = views.get(id);
  if (res.up && e && e.mode === 'page' && !e.view.webContents.isDestroyed()) e.view.webContents.reload();
}
async function pollUpdates() {
  clearTimeout(updatesTimer);
  if (stopped) return;
  try {
    const u = await pmRequest('GET', '/api/updates');
    if (u && typeof u === 'object') {
      // once per app open: an install pass (the server's own one at its start makes this a no-op within a minute)
      if (!askedUpdate) { askedUpdate = true; pmRequest('POST', '/api/updates/run'); }
      if (u.selfUpdated === true && !selfUpdated) { selfUpdated = true; updateTabbar(); }
      for (const r of Array.isArray(u.restart) ? u.restart : []) {
        if (!r || typeof r.id !== 'string' || handledRestarts.has(r.id)) continue;
        handledRestarts.add(r.id);
        restartForUpdate(r)
          .catch((err) => logNote('updates', 'restart for port ' + r.port + ' failed: ' + (err && err.message)))
          .finally(() => pmRequest('POST', '/api/updates/ack?id=' + encodeURIComponent(r.id)));
      }
    }
  } finally {
    if (!stopped) updatesTimer = setTimeout(pollUpdates, win && win.isMinimized() ? UPDATES_POLL_MS * 2 : UPDATES_POLL_MS);
  }
}
async function relaunchApp() {
  // the tab bar's Restart button after a self-update: a new shell with the new files; the old Supervisor Board server
  // goes too (stopped here when this shell started it, else when its command line is Quark\project-manager's), and
  // the new shell's supervisor starts the new one
  if (relaunching || !win) return;
  relaunching = true;
  const r = await dialog.showMessageBox(win, {
    type: 'question', title: 'Quark Project', noLink: true, defaultId: 0, cancelId: 1, buttons: ['Restart', 'Later'],
    message: 'Restart Quark Project with the new version?',
    detail: 'Every tab closes and opens again. Save open work in the editors first (their autosaves are kept).',
  });
  if (r.response !== 0) { relaunching = false; updateTabbar(); return; }
  saveState(true);
  const pm = tabById('project');
  const own = pm && started.get(pm.id);
  if (pm && pm.probeHost && !(own && !own.exited) && IS_WIN && await probe(pm)) {
    await stopForeignListener(pm.port, path.basename(PM_DIR), pm.id);
  }
  stopStartedServers();
  app.relaunch();
  app.exit(0);
}

// ---------- placeholder page (shown in the tab's own view) ----------
function placeholderURL(tab, mode, info = {}) {
  const dark = nativeTheme.shouldUseDarkColors;
  const title = { starting: 'Starting ' + tab.name + '…', down: tab.name + ' is not running', missing: tab.name + ' is not installed', crashed: tab.name + ' stopped' }[mode];
  const lines = [];
  if (mode === 'starting') lines.push('Waiting for ' + tab.url.replace(/\?.*$/, '') + ' (up to ' + START_TIMEOUT_MS / 1000 + ' s).');
  if (mode === 'down') {
    lines.push('Nothing answers on ' + tab.url.replace(/\?.*$/, '') + '.');
    if (info.reason) lines.push(info.reason);
    const s = sup.get(tab.id);
    if (supervised(tab) && s && s.nextAt) lines.push('It restarts by itself in about ' + Math.max(1, Math.round((s.nextAt - Date.now()) / 1000)) + ' s (log: ' + path.join(LOG_DIR, tab.id + '.log') + ').');
    else if (supervised(tab)) lines.push('The shell keeps it running: it is checked every ' + WATCHDOG_MS / 1000 + ' s and restarted when down.');
  }
  if (mode === 'missing') lines.push('Not found: ' + tab.missing + '.', 'Run setup.cmd again, or fix this tab in ' + tabsFile + ' (Ctrl+Shift+R reloads the tab list).');
  if (mode === 'crashed') lines.push('The page stopped (' + (info.reason || 'renderer gone') + ').');
  const canStart = mode === 'down' && !!tab.start;
  const btn = mode === 'down' ? (canStart ? 'Start' : 'Try again') : mode === 'crashed' ? 'Reload' : '';
  const cmd = (mode === 'down' || mode === 'starting') && tab.start ? '<p class="cmd"><span>start</span> <code>' + esc(fixPython(tab.start)) + '</code> <span>in</span> <code>' + esc(tab.cwd) + '</code></p>' : '';
  const nostart = mode === 'down' && !tab.start ? '<p>No start command: add <code>"start"</code> to this tab in tabs.json, or put a <code>' + esc(tab.folder ? path.basename(tab.folder) : tab.id) + '.cmd</code> / <code>start.cmd</code> in its folder.</p>' : '';
  const log = info.log && info.log.filter((l) => l.trim()).length ? '<pre>' + esc(info.log.filter((l) => l.trim()).slice(-14).join('\n')) + '</pre>' : '';
  const html = `<!doctype html><html><head><meta charset="utf-8"><title>${esc(tab.name)}</title>
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'">
<style>
:root{--bg:${dark ? '#16181c' : '#f4f5f7'};--card:${dark ? '#1e2126' : '#ffffff'};--line:${dark ? '#2c3037' : '#dde0e5'};--text:${dark ? '#d5d8de' : '#22252a'};--muted:${dark ? '#8a909a' : '#6a707a'};--accent:#4c8dff;--amber:#e0a43a}
html,body{margin:0;height:100%;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
body{display:flex;align-items:center;justify-content:center}
.card{max-width:640px;width:calc(100% - 32px);background:var(--card);border:1px solid var(--line);border-radius:10px;padding:24px 28px}
h1{font-size:18px;font-weight:600;margin:0 0 8px;display:flex;align-items:center;gap:10px}
.dot{width:10px;height:10px;border-radius:50%;background:${mode === 'starting' ? 'var(--amber)' : 'var(--muted)'}}
.starting .dot{animation:p 1s ease-in-out infinite alternate}@keyframes p{to{opacity:.3}}
p{margin:6px 0;color:var(--muted)} code{font:12px ui-monospace,Consolas,monospace;color:var(--text);word-break:break-all}
.cmd span{color:var(--muted)} pre{background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:10px;font:12px/1.4 ui-monospace,Consolas,monospace;max-height:220px;overflow:auto;white-space:pre-wrap;color:var(--text)}
button{margin-top:12px;background:var(--accent);color:#fff;border:0;border-radius:6px;padding:7px 18px;font:inherit;cursor:pointer}
button:hover{filter:brightness(1.1)} #t{font-variant-numeric:tabular-nums}
</style></head><body class="${mode}"><div class="card">
<h1><span class="dot"></span>${esc(title)}${mode === 'starting' ? ' <span id="t"></span>' : ''}</h1>
${lines.map((l) => '<p>' + esc(l) + '</p>').join('')}${cmd}${nostart}${log}
${btn ? '<button id="b" autofocus>' + btn + '</button>' : ''}
</div><script>
var b=document.getElementById('b');if(b)b.onclick=function(){b.disabled=true;window.quarkShell&&window.quarkShell.retry();};
var t=document.getElementById('t'),s=Date.now()-${Number(info.elapsed) || 0};if(t)setInterval(function(){t.textContent=Math.floor((Date.now()-s)/1000)+' s';},250);
</script></body></html>`;
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

// ---------- window, views ----------
let win = null;
let tabbar = null;
let tabs = [];
let tabsFile = '';
let tabsError = '';
let activeId = null;
const views = new Map(); // tab id -> { view, tab, mode: 'page'|'starting'|'down'|'missing'|'crashed', loadedURL, pendingHash }
const status = new Map(); // tab id -> 'up'|'down'|'starting'|'missing'
let startingNow = new Set();

const tabById = (id) => tabs.find((t) => t.id === id);
const entryFor = (wc) => { for (const e of views.values()) if (e.view.webContents === wc) return e; return null; };

function tabStatus(tab) {
  if (!tab.installed) return 'missing';
  if (startingNow.has(tab.id)) return 'starting';
  if (tab.kind === 'file') return 'up';
  const st = status.get(tab.id) || 'unknown';
  return st !== 'up' && restartPending(tab.id) ? 'restarting' : st; // (down; the watchdog restarts it soon)
}
const tabbarTabs = () => tabs.map((t) => ({
  id: t.id, name: t.name, kind: t.kind, status: tabStatus(t), missing: t.missing, parent: t.parent, depth: t.depth, childLabel: t.childLabel,
  badge: t.id === 'project' ? haltCount : 0,
}));

// ---------- halting badge ----------
// The Supervisor Board page (tab "project" only) reports how many things q must do that block a Claude agent:
// a red count on its tab button, and on Windows a red dot over the taskbar icon (elsewhere the app badge count).
let haltCount = 0;
let haltDot = null;
function redDot() {
  if (haltDot) return haltDot;
  const n = 16, buf = Buffer.alloc(n * n * 4);
  for (let y = 0; y < n; y++) {
    for (let x = 0; x < n; x++) {
      const d = Math.hypot(x + 0.5 - n / 2, y + 0.5 - n / 2);
      const a = Math.max(0, Math.min(1, n / 2 - 0.5 - d)); // (anti-aliased edge)
      const ring = d > n / 2 - 2.5 ? 1 : 0; // (a white rim so it reads on a red or dark icon)
      const [r, g, b] = ring ? [255, 255, 255] : [0xd9, 0x2d, 0x20];
      const i = (y * n + x) * 4; // BGRA, premultiplied
      buf[i] = Math.round(b * a); buf[i + 1] = Math.round(g * a); buf[i + 2] = Math.round(r * a); buf[i + 3] = Math.round(255 * a);
    }
  }
  haltDot = nativeImage.createFromBitmap(buf, { width: n, height: n });
  return haltDot;
}
function setHalting(n) {
  n = Math.max(0, Math.min(999, Math.floor(Number(n)) || 0));
  if (n === haltCount) return;
  haltCount = n;
  updateTabbar();
  if (!win || win.isDestroyed()) return;
  try {
    if (IS_WIN && typeof win.setOverlayIcon === 'function') win.setOverlayIcon(n ? redDot() : null, n ? n + ' halting: Claude is blocked on you' : '');
    else if (typeof app.setBadgeCount === 'function') app.setBadgeCount(n);
  } catch (err) { /* (cosmetic) */ }
  win.setTitle(n ? '(' + n + ') Quark Project' : 'Quark Project');
}
function updateTabbar() {
  if (!tabbar || tabbar.webContents.isDestroyed()) return;
  tabbar.webContents.send('shell:tabs', {
    active: activeId,
    error: tabsError,
    updated: selfUpdated,
    tabs: tabbarTabs(),
  });
}

function layout() {
  if (!win || win.isDestroyed()) return;
  const { width, height } = win.getContentBounds();
  tabbar.setBounds({ x: 0, y: 0, width, height: TABBAR_H });
  const e = views.get(activeId);
  if (e) e.view.setBounds({ x: 0, y: TABBAR_H, width, height: Math.max(0, height - TABBAR_H) });
}

function navTarget(url) {
  // where a link that leaves its tab should go: another tab (by origin) or the default browser (https only)
  const p = parseURL(url);
  if (!p) return null;
  if (p.protocol === 'http:' || p.protocol === 'https:') {
    const t = tabs.find((x) => x.kind === 'server' && x.origin === p.origin);
    if (t) return { tab: t, hash: p.hash };
  }
  if (p.protocol === 'https:') return { external: p.href };
  return null;
}
function allowedNav(tab, url) {
  const p = parseURL(url);
  if (!p) return false;
  if (tab.kind === 'server') return p.origin === tab.origin;
  if (p.protocol !== 'file:') return false;
  try { return inside(path.resolve(fileURLToPath(p)), tab.fileDir); } catch { return false; }
}
function routeAway(url) {
  const t = navTarget(url);
  if (!t) return;
  if (t.tab) openTab(t.tab.id, t.hash);
  else if (t.external) shell.openExternal(t.external).catch(() => {});
}

function sessionFor(tab) {
  const ses = session.fromPartition('persist:' + tab.id);
  if (ses.__quarkReady) return ses;
  ses.__quarkReady = true;
  const fromLocal = (wc, url) => isLocalURL(url || (wc && !wc.isDestroyed() ? wc.getURL() : ''));
  ses.setPermissionRequestHandler((wc, permission, cb, details) => {
    cb(LOCAL_PERMISSIONS.has(permission) && fromLocal(wc, details && details.requestingUrl));
  });
  ses.setPermissionCheckHandler((wc, permission, origin, details) => {
    const url = (details && (details.requestingUrl || details.embeddingOrigin)) || origin;
    return LOCAL_PERMISSIONS.has(permission) && fromLocal(wc, url);
  });
  ses.setDevicePermissionHandler(() => false);
  ses.on('select-hid-device', (e, d, cb) => { e.preventDefault(); cb(''); });
  ses.on('select-serial-port', (e, d, cb) => { e.preventDefault(); cb(''); });
  ses.on('select-usb-device', (e, d, cb) => { e.preventDefault(); cb(''); });
  // File System Access: Chromium refuses some folders (the home / Desktop / Documents / Downloads folder itself,
  // AppData, Program Files, system folders). Without a listener Electron just denies; ask q instead.
  ses.on('file-system-access-restricted', async (e, details, cb) => {
    const origin = details.origin || '';
    if (!(origin.startsWith('file:') || isLocalURL(origin)) || !win) return cb('deny');
    const r = await dialog.showMessageBox(win, {
      type: 'warning', title: 'Quark Project', noLink: true, defaultId: 1, cancelId: 2,
      buttons: ['Allow this folder', 'Choose another', 'Cancel'],
      message: 'This ' + (details.isDirectory ? 'folder' : 'file') + ' is normally off limits to web pages',
      detail: details.path + '\n\nIt is a system folder or a whole user folder (Desktop, Documents, Downloads, your home folder). '
        + 'Picking a subfolder (for example Desktop\\Quark\\...) is safer.',
    });
    cb(['allow', 'tryAgain', 'deny'][r.response] || 'deny');
  });
  ses.on('will-download', (e, item) => {
    const dir = state.downloadDir && isDir(state.downloadDir) ? state.downloadDir : app.getPath('downloads');
    item.setSaveDialogOptions({ title: 'Save ' + item.getFilename(), defaultPath: path.join(dir, item.getFilename()) });
    item.once('done', (ev, st) => {
      if (st === 'completed' && item.getSavePath()) { state.downloadDir = path.dirname(item.getSavePath()); saveState(); }
    });
  });
  return ses;
}

function createView(tab) {
  const view = new WebContentsView({
    webPreferences: {
      session: sessionFor(tab),
      preload: path.join(SHELL_DIR, 'preload.js'),
      contextIsolation: true, sandbox: true, nodeIntegration: false, nodeIntegrationInSubFrames: false,
      webviewTag: false, spellcheck: false, autoplayPolicy: 'no-user-gesture-required',
      additionalArguments: ['--quark-shell-version=' + app.getVersion()],
    },
  });
  view.setBackgroundColor(nativeTheme.shouldUseDarkColors ? '#16181c' : '#f4f5f7');
  const wc = view.webContents;
  const entry = { view, tab, mode: 'page', pendingHash: '' };
  wc.setWindowOpenHandler(({ url }) => { routeAway(url); return { action: 'deny' }; });
  const lock = (e, url) => {
    if (entry.mode !== 'page' || !allowedNav(entry.tab, url)) { e.preventDefault(); routeAway(url); }
  };
  wc.on('will-navigate', (e) => lock(e, e.url));
  wc.on('will-redirect', (e) => { if (e.isMainFrame) lock(e, e.url); });
  wc.on('will-frame-navigate', (e) => {
    // subframes may show local or inline content only (no remote pages inside a tool)
    if (!e.isMainFrame && /^https?:/i.test(e.url) && !isLocalURL(e.url)) { e.preventDefault(); routeAway(e.url); }
  });
  wc.on('before-input-event', onKey);
  wc.on('will-prevent-unload', (e) => {
    const r = dialog.showMessageBoxSync(win, {
      type: 'question', buttons: ['Leave', 'Stay'], defaultId: 1, cancelId: 1, noLink: true, title: 'Quark Project',
      message: entry.tab.name + ' has unsaved changes', detail: 'Leave the page anyway? Unsaved work is lost.',
    });
    if (r === 0) e.preventDefault();
  });
  wc.on('did-fail-load', (e, code, desc, url, isMainFrame) => {
    if (!isMainFrame || code === -3 /* aborted (e.g. a new load) */ || entry.mode !== 'page') return;
    if (entry.tab.kind === 'server') { status.set(entry.tab.id, 'down'); showDown(entry, desc + ' (' + code + ')'); updateTabbar(); }
  });
  wc.on('did-finish-load', () => {
    if (entry.mode === 'page' && entry.pendingHash) { const h = entry.pendingHash; entry.pendingHash = ''; setHash(entry, h); }
  });
  wc.on('render-process-gone', (e, d) => {
    if (d.reason === 'clean-exit') return;
    entry.mode = 'crashed';
    wc.loadURL(placeholderURL(entry.tab, 'crashed', { reason: d.reason })).catch(() => {});
  });
  win.contentView.addChildView(view);
  view.setVisible(false);
  views.set(tab.id, entry);
  return entry;
}

function loadPage(entry, hash) {
  entry.mode = 'page';
  let url = entry.tab.url;
  if (hash) { url = url.replace(/#.*$/, '') + hash; entry.pendingHash = ''; }
  entry.view.webContents.loadURL(url).catch(() => { /* did-fail-load handles it */ });
}
function showDown(entry, reason) {
  entry.mode = 'down';
  entry.downReason = reason || '';
  const rec = started.get(entry.tab.id);
  entry.view.webContents.loadURL(placeholderURL(entry.tab, 'down', { reason, log: rec && rec.log })).catch(() => {});
}
function showStarting(entry) {
  if (entry.mode === 'starting') return;
  entry.mode = 'starting';
  const rec = started.get(entry.tab.id);
  const elapsed = rec && !rec.exited ? Date.now() - rec.at : 0;
  entry.view.webContents.loadURL(placeholderURL(entry.tab, 'starting', { elapsed })).catch(() => {});
}

async function bringUp(entry, hash, forceStart) {
  const tab = entry.tab;
  if (!tab.installed) {
    entry.mode = 'missing';
    entry.view.webContents.loadURL(placeholderURL(tab, 'missing')).catch(() => {});
    return;
  }
  if (tab.kind !== 'server' || !tab.probeHost) return loadPage(entry, hash);
  if (hash) entry.pendingHash = hash;
  // a start is already being waited for (app start, watchdog, another click): ensureStarted loads this view when it ends
  if (waiting.has(tab.id)) { showStarting(entry); updateTabbar(); return; }
  if (await probe(tab)) { status.set(tab.id, 'up'); updateTabbar(); return loadPage(entry, entry.pendingHash); }
  if (waiting.has(tab.id)) { showStarting(entry); return; }
  status.set(tab.id, 'down');
  if (!tab.start) { updateTabbar(); return showDown(entry, 'No start command was found for it.'); }
  const prev = started.get(tab.id);
  if (prev && prev.exited && !forceStart && prev.code !== 0) { updateTabbar(); return showDown(entry, 'Its start command exited (code ' + prev.code + ').'); }
  showStarting(entry);
  ensureStarted(tab, forceStart ? 'Start pressed' : 'tab opened');
}

function setHash(entry, hash) {
  if (!hash) return;
  const h = hash.startsWith('#') ? hash : '#' + hash;
  entry.view.webContents.executeJavaScript('location.hash = ' + JSON.stringify(h) + '; void 0').catch(() => {});
}

function openTab(id, hash) {
  const tab = tabById(id);
  if (!tab || !win) return false;
  hash = hash ? String(hash) : '';
  if (hash && !hash.startsWith('#')) hash = '#' + hash;
  let entry = views.get(id);
  const fresh = !entry;
  if (fresh) entry = createView(tab);
  if (activeId !== id) {
    const old = views.get(activeId);
    activeId = id;
    layout();
    entry.view.setVisible(true);
    if (old) old.view.setVisible(false);
    state.lastTab = id;
    saveState();
  }
  entry.view.webContents.focus();
  if (fresh) bringUp(entry, hash);
  else if (hash) {
    if (entry.mode === 'page' && !entry.view.webContents.isLoading()) setHash(entry, hash);
    else entry.pendingHash = hash;
  }
  updateTabbar();
  return true;
}

function reloadActive(hard) {
  const e = views.get(activeId);
  if (!e) return;
  if (e.mode !== 'page' || (e.tab.kind === 'server' && status.get(e.tab.id) !== 'up')) return bringUp(e, '', true);
  if (hard) e.view.webContents.reloadIgnoringCache(); else e.view.webContents.reload();
}

function reloadTabList() {
  const r = loadTabs();
  const keepActive = activeId;
  for (const [id, e] of views) {
    const nt = r.tabs.find((t) => t.id === id);
    if (nt && nt.url === e.tab.url && nt.installed === e.tab.installed) { e.tab = nt; continue; }
    // changed or removed: drop the view (it is re-created on next open)
    win.contentView.removeChildView(e.view);
    if (!e.view.webContents.isDestroyed()) e.view.webContents.close();
    views.delete(id);
    if (activeId === id) activeId = null;
  }
  tabs = r.tabs; tabsFile = r.file; tabsError = r.error;
  const next = tabById(keepActive) ? keepActive : (tabs[0] && tabs[0].id);
  if (next) openTab(next); else updateTabbar();
  pollStatus();
  startKeepAlive(); // (a keepAlive tab that is new or changed starts now)
}

function cycle(dir) {
  if (!tabs.length) return;
  const i = Math.max(0, tabs.findIndex((t) => t.id === activeId));
  openTab(tabs[(i + dir + tabs.length) % tabs.length].id);
}

function onKey(event, input) {
  if (input.type !== 'keyDown') return;
  const ctrl = input.control || (process.platform === 'darwin' && input.meta);
  const key = input.key;
  let handled = true;
  if (ctrl && !input.alt && !input.shift && /^[1-9]$/.test(key)) {
    // Ctrl+1..8 = that tab in tree order, Ctrl+9 = the last tab (browser convention)
    const t = key === '9' ? tabs[tabs.length - 1] : tabs[Number(key) - 1];
    if (t) openTab(t.id);
  }
  else if (ctrl && key === 'Tab') cycle(input.shift ? -1 : 1);
  else if (ctrl && !input.alt && (key === 'r' || key === 'R') && input.shift) reloadTabList();
  else if (ctrl && !input.alt && (key === 'r' || key === 'R')) reloadActive(false);
  else if (key === 'F5' && !input.alt) reloadActive(ctrl);
  else if (key === 'F11') { if (win) win.setFullScreen(!win.isFullScreen()); }
  else if (key === 'F12') { const e = views.get(activeId); if (e) e.view.webContents.toggleDevTools(); }
  else if (ctrl && key === 'PageDown') cycle(1);
  else if (ctrl && key === 'PageUp') cycle(-1);
  else handled = false;
  if (handled) event.preventDefault();
}

let pollTimer = null;
let polling = false;
async function pollStatus() {
  clearTimeout(pollTimer);
  if (polling || stopped) return;
  polling = true;
  try {
    const servers = tabs.filter((t) => t.kind === 'server' && t.installed && t.probeHost && !startingNow.has(t.id));
    const res = await Promise.all(servers.map((t) => probe(t)));
    let changed = false;
    servers.forEach((t, i) => {
      const s = res[i] ? 'up' : 'down';
      if (status.get(t.id) !== s) { status.set(t.id, s); changed = true; }
      // a "not running" placeholder whose server has come up (started elsewhere): load it
      const e = views.get(t.id);
      if (res[i] && e && e.mode === 'down') loadPage(e, e.pendingHash);
    });
    if (changed) updateTabbar();
  } finally {
    polling = false;
    if (!stopped) pollTimer = setTimeout(pollStatus, win && win.isMinimized() ? STATUS_POLL_MS * 4 : STATUS_POLL_MS);
  }
}

function createWindow() {
  const r = loadTabs();
  tabs = r.tabs; tabsFile = r.file; tabsError = r.error;
  const icon = path.join(PM_DIR, 'quark.ico');
  const dark = nativeTheme.shouldUseDarkColors;
  win = new BaseWindow(Object.assign(initialBounds(), {
    title: 'Quark Project', show: false, minWidth: 480, minHeight: 300,
    backgroundColor: dark ? '#16181c' : '#f4f5f7',
    // no title bar (q, 2026-10-24): the tab bar is the top of the window; Windows/Linux draw their window buttons
    // over its right end (WINDOW_BUTTONS_W keeps the clock clear of them), macOS keeps its traffic lights at the left
    titleBarStyle: 'hidden',
    titleBarOverlay: process.platform === 'darwin' ? undefined : { color: '#16181c', symbolColor: '#c9cdd4', height: TABBAR_H },
    icon: isFile(icon) ? nativeImage.createFromPath(icon) : undefined,
  }));
  win.setMenu && win.setMenu(null);

  tabbar = new WebContentsView({
    webPreferences: {
      session: session.fromPartition('quark-shell-tabbar'),
      preload: path.join(SHELL_DIR, 'preload.js'),
      contextIsolation: true, sandbox: true, nodeIntegration: false, webviewTag: false, spellcheck: false,
      additionalArguments: ['--quark-tabbar', '--quark-shell-version=' + app.getVersion()],
    },
  });
  tabbar.setBackgroundColor('#16181c');
  const tses = tabbar.webContents.session;
  tses.setPermissionRequestHandler((wc, p, cb) => cb(false));
  tses.setPermissionCheckHandler(() => false);
  tabbar.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  tabbar.webContents.on('will-navigate', (e) => e.preventDefault());
  tabbar.webContents.on('before-input-event', onKey);
  tabbar.webContents.on('did-finish-load', () => {
    const css = process.platform === 'darwin' ? '#bar{padding-left:78px}' : '#bar{padding-right:' + WINDOW_BUTTONS_W + 'px}';
    tabbar.webContents.insertCSS(css).catch(() => {});
  });
  win.contentView.addChildView(tabbar);

  win.on('resize', layout);
  win.on('maximize', layout);
  win.on('unmaximize', layout);
  win.on('enter-full-screen', layout);
  win.on('leave-full-screen', layout);
  win.on('resized', rememberBounds);
  win.on('moved', rememberBounds);
  win.on('maximize', rememberBounds);
  win.on('unmaximize', rememberBounds);
  win.on('focus', () => { const e = views.get(activeId); if (e) e.view.webContents.focus(); });
  win.on('close', () => { rememberBounds(); saveState(true); });
  win.on('closed', () => {
    stopStartedServers();
    for (const e of views.values()) if (!e.view.webContents.isDestroyed()) e.view.webContents.close();
    if (!tabbar.webContents.isDestroyed()) tabbar.webContents.close();
    win = null;
    app.quit();
  });

  layout();
  tabbar.webContents.once('did-finish-load', () => {
    if (state.maximized || state.maximized === undefined) win.maximize(); // (first run: fill the screen)
    win.show();
    const want = argValue('tab');
    const first = (want && tabById(want)) ? want : (tabById(state.lastTab) ? state.lastTab : (tabs[0] && tabs[0].id));
    if (first) openTab(first);
    updateTabbar();
    pollStatus();
    if (SELFTEST_SHOT) selftestShot(SELFTEST_SHOT);
    if (SELFTEST_SCRIPT) runSelftest(SELFTEST_SCRIPT);
  });
  tabbar.webContents.loadFile(path.join(SHELL_DIR, 'tabbar.html'));
  startKeepAlive();
  updatesTimer = setTimeout(pollUpdates, UPDATES_FIRST_MS);
}

// ---------- IPC ----------
ipcMain.on('shell:open', (e, id, hash) => {
  if (!entryFor(e.sender) && (!tabbar || e.sender !== tabbar.webContents)) return;
  if (typeof id === 'string') openTab(id, typeof hash === 'string' ? hash : '');
});
ipcMain.on('shell:badge', (e, n) => {
  const entry = entryFor(e.sender);
  if (!entry || entry.tab.id !== 'project' || entry.mode !== 'page' || typeof n !== 'number') return;
  setHalting(n);
});
ipcMain.on('shell:retry', (e) => {
  const entry = entryFor(e.sender);
  if (entry && entry.mode !== 'page') bringUp(entry, entry.pendingHash, true);
});
ipcMain.on('shell:tabbar', (e, cmd) => {
  if (!tabbar || e.sender !== tabbar.webContents) return;
  if (cmd === 'reload') reloadActive(false);
  else if (cmd === 'reload-tabs') reloadTabList();
  else if (cmd === 'ready') updateTabbar();
  else if (cmd === 'relaunch' && selfUpdated) relaunchApp();
});
ipcMain.handle('shell:tabs', (e) => {
  if (!tabbar || e.sender !== tabbar.webContents) return null;
  return { active: activeId, error: tabsError, updated: selfUpdated, tabs: tabbarTabs() };
});

// ---------- selftest (local verification only) ----------
async function selftestShot(file) {
  await new Promise((r) => setTimeout(r, Number(argValue('selftest-wait')) || 2500));
  try {
    const top = await tabbar.webContents.capturePage();
    const e = views.get(activeId);
    const body = e ? await e.view.webContents.capturePage() : null;
    const a = top.getSize();
    const parts = [top.toBitmap()];
    let h = a.height;
    if (body && body.getSize().width === a.width) { parts.push(body.toBitmap()); h += body.getSize().height; }
    const img = nativeImage.createFromBitmap(Buffer.concat(parts), { width: a.width, height: h });
    fs.writeFileSync(file, img.toPNG());
    console.log('selftest-shot: wrote ' + file + ' (' + a.width + 'x' + h + ')');
  } catch (err) {
    console.error('selftest-shot failed: ' + err.message);
  }
  if (!SELFTEST_SCRIPT) app.quit();
}
function runSelftest(file) {
  try {
    require(path.resolve(file))({ app, win: () => win, tabbar: () => tabbar, views, tabs: () => tabs, openTab, reloadTabList, reloadActive, status, started, activeId: () => activeId, onKey,
      sup, waiting, ensureStarted, watchdog, splitCommand, pollUpdates, restartForUpdate, handledRestarts, selfUpdated: () => selfUpdated,
      updateRestarting, commandLine });
  } catch (err) { console.error('selftest failed: ' + err.stack); app.exit(1); }
}

// ---------- app ----------
app.on('web-contents-created', (e, wc) => {
  wc.on('will-attach-webview', (ev) => ev.preventDefault());
});
app.on('second-instance', (e, argv, cwd, extra) => {
  if (!win) return;
  if (win.isMinimized()) win.restore();
  win.show();
  win.focus();
  const want = (extra && extra.tab) || argValue('tab', argv);
  if (want) openTab(want);
});
app.on('window-all-closed', () => app.quit());
app.on('before-quit', () => { saveState(true); });
app.on('will-quit', stopStartedServers);
process.on('exit', stopStartedServers);

app.whenReady().then(() => {
  if (process.platform === 'darwin') {
    // macOS needs the Edit menu for copy / paste shortcuts; it is in the system bar, not in the window
    Menu.setApplicationMenu(Menu.buildFromTemplate([{ role: 'appMenu' }, { role: 'editMenu' }]));
  } else {
    Menu.setApplicationMenu(null);
  }
  createWindow();
});
