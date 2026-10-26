// Quarkology Editors: the Quark editors in one window (built by tools/editors_app/export.py + electron-builder). No Supervisor Board, Claude,
// GitHub or Tests, no updates, no git: each editor server runs from the bundle with the bundled Python, on a free port,
// and saves into Documents\Quarkology Editors (QUARK_DATA_DIR), which people zip and send to q.
const { app, BrowserWindow, ipcMain, shell } = require('electron');
const { spawn, spawnSync } = require('child_process');
const fs = require('fs');
const net = require('net');
const path = require('path');
const { pathToFileURL } = require('url');

const IS_WIN = process.platform === 'win32';
const BUNDLE = app.isPackaged ? path.join(process.resourcesPath, 'bundle')
                              : path.resolve(process.env.QUARK_EDITORS_BUNDLE || path.join(__dirname, 'bundle'));
const PYTHON = IS_WIN ? path.join(BUNDLE, 'python', 'python.exe') : (process.env.QUARK_EDITORS_PYTHON || 'python3');
const SAVES = path.join(app.getPath('documents'), 'Quarkology Editors');
const TABS = JSON.parse(fs.readFileSync(path.join(__dirname, 'tabs.json'), 'utf8')).tabs;
const children = [];
let tabs = null;

function freePort(want) {
  // the usual port when it's free (q's own Quarkology may hold it), else any free one
  const tryPort = (p) => new Promise((res) => {
    const s = net.createServer();
    s.once('error', () => res(0));
    s.listen(p, '127.0.0.1', () => { const got = s.address().port; s.close(() => res(got)); });
  });
  return tryPort(want).then((p) => p || tryPort(0));
}

function serverEnv() {
  const env = Object.assign({}, process.env, { QUARK_SHELL: '1', QUARK_DATA_DIR: SAVES, PYTHONUNBUFFERED: '1',
                                               PYTHONNOUSERSITE: '1', PYTHONDONTWRITEBYTECODE: '1' });
  delete env.PYTHONHOME; delete env.PYTHONPATH;   // (a Python installed on the PC must not change the bundled one)
  return env;
}

async function startServers() {
  fs.mkdirSync(SAVES, { recursive: true });
  const log = fs.openSync(path.join(SAVES, 'editors.log'), 'a');
  const out = [];
  for (const t of TABS) {
    if (t.page) { out.push({ id: t.id, name: t.name, url: pathToFileURL(path.join(BUNDLE, t.page)).href }); continue; }
    const port = await freePort(t.port);
    const child = spawn(PYTHON, ['-B', path.join(BUNDLE, t.serve), '--port', String(port), ...(t.args || [])],
                        { cwd: BUNDLE, env: serverEnv(), windowsHide: true, stdio: ['ignore', log, log] });
    child.on('error', (e) => fs.writeSync(log, t.id + ': could not start: ' + e.message + '\n'));
    children.push(child);
    out.push({ id: t.id, name: t.name, url: 'http://127.0.0.1:' + port + '/?embed=1', probe: 'http://127.0.0.1:' + port + '/' });
  }
  return out;
}

function stopServers() {
  for (const c of children) {
    if (c.exitCode !== null || !c.pid) continue;
    if (IS_WIN) spawnSync('taskkill', ['/T', '/F', '/PID', String(c.pid)], { windowsHide: true, stdio: 'ignore', timeout: 5000 });
    else try { c.kill(); } catch {}
  }
}

ipcMain.handle('tabs', () => ({ tabs, saves: SAVES }));
ipcMain.handle('open-saves', () => shell.openPath(SAVES));

app.whenReady().then(async () => {
  tabs = await startServers();
  const win = new BrowserWindow({ width: 1600, height: 950, title: 'Quarkology Editors', backgroundColor: '#15171c',
                                  autoHideMenuBar: true,
                                  webPreferences: { preload: path.join(__dirname, 'preload.js'), webSecurity: false } });
  win.webContents.setWindowOpenHandler(({ url }) => { shell.openExternal(url); return { action: 'deny' }; });
  win.loadFile(path.join(__dirname, 'index.html'));
});
app.on('window-all-closed', () => app.quit());
app.on('will-quit', stopServers);
process.on('exit', stopServers);
