'use strict';
// Quark Project is now Quarkology (2026-10-27): the old app's hand-over (tools/quarkology/SPEC.md section 4, "Moving
// from Quark Project"). The old app's self-update puts this folder in Quark\project-manager\shell\. When the old app
// restarts (by itself right after that update, or with its Restart button) or the old "Quark Project" shortcut is
// used, its Electron runs this file: it starts Quark\quarkology\ and quits. If the update did not install Quarkology
// (its setup.ps1 failed), this runs that setup first, hidden, with a small "Moving" window.
// preload.js, tabbar.html, tabbar.css and tabbar.js are the old shell's, unchanged: the old app keeps loading them
// (each new tab's preload, the tab bar) until it restarts. This folder goes one release later.
const { app, dialog, BrowserWindow } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const { spawn } = require('child_process');

const IS_WIN = process.platform === 'win32';
const SETUP_TIMEOUT_MS = 30 * 60000;
function argValue(name) {
  const pre = '--' + name + '=';
  const a = process.argv.find((x) => x.startsWith(pre));
  return a ? a.slice(pre.length).replace(/^"(.*)"$/, '$1') : null;
}
const QUARK = path.resolve(argValue('quark') || path.join(__dirname, '..', '..'));
const NEW = path.join(QUARK, 'quarkology');
const EXE = path.join(NEW, 'electron', IS_WIN ? 'electron.exe' : 'electron');
const SHELL = path.join(NEW, 'shell');
const VBS = path.join(NEW, 'quarkology-hidden.vbs');
const SETUP = path.join(QUARK, 'repository', 'tools', 'quarkology', 'setup.ps1');
const LOG_DIR = IS_WIN && process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA, 'Quarkology', 'logs') : path.join(NEW, 'logs');
const LOG = path.join(LOG_DIR, 'handover.log');
const isFile = (p) => { try { return fs.statSync(p).isFile(); } catch { return false; } };
function log(line) {
  try { fs.mkdirSync(LOG_DIR, { recursive: true }); fs.appendFileSync(LOG, '[' + new Date().toISOString() + '] ' + line + '\n'); } catch { /* nowhere to log */ }
}

// (never the old app's profile, %LOCALAPPDATA%\QuarkProject\electron-data: Quarkology copies it once this has quit)
app.setPath('userData', path.join(os.tmpdir(), 'quarkology-handover'));
app.on('window-all-closed', () => { /* (the "Moving" window closing must not quit before Quarkology starts) */ });

const installed = () => isFile(path.join(SHELL, 'main.js')) && (isFile(EXE) || isFile(VBS));

function startQuarkology() {
  // -> a promise that settles once Quarkology is started (detached: it outlives this process)
  const tab = argValue('tab');
  return new Promise((resolve) => {
    let child;
    if (isFile(EXE)) {
      const args = ['"' + SHELL + '"'].concat(argValue('quark') ? ['"--quark=' + QUARK + '"'] : [], tab ? ['"--tab=' + tab + '"'] : []);
      log('starting ' + EXE + ' ' + args.join(' '));
      // (through cmd.exe's start, like the old shell's electron-swap.ps1: a direct child would die with this process)
      child = IS_WIN ? spawn('start "" "' + EXE + '" ' + args.join(' '), { shell: true, cwd: NEW, windowsHide: true, stdio: 'ignore' })
        : spawn(EXE, args.map((a) => a.replace(/"/g, '')), { cwd: NEW, detached: true, stdio: 'ignore' });
    } else {
      log('starting ' + VBS + ' (no Electron in ' + NEW + ')');
      child = spawn('wscript.exe', [VBS], { cwd: NEW, windowsHide: true, detached: true, stdio: 'ignore' });
    }
    child.on('error', (err) => { log('could not start Quarkology: ' + err.message); resolve(false); });
    if (IS_WIN && isFile(EXE)) child.on('exit', () => resolve(true));
    else { child.unref(); setTimeout(() => resolve(true), 500); }
  });
}

function runSetup() {
  // tools\quarkology\setup.ps1 from the repository, hidden; -> its exit code
  return new Promise((resolve) => {
    log('Quarkology is not installed in ' + NEW + ': running ' + SETUP);
    let out;
    try { fs.mkdirSync(LOG_DIR, { recursive: true }); out = fs.openSync(path.join(LOG_DIR, 'handover-setup.log'), 'w'); } catch { out = 'ignore'; }
    const child = spawn('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', SETUP, '-Quark', QUARK],
      { cwd: path.dirname(SETUP), windowsHide: true, stdio: ['ignore', out, out] });
    const timer = setTimeout(() => { try { child.kill(); } catch { /* gone */ } }, SETUP_TIMEOUT_MS);
    child.on('error', (err) => { clearTimeout(timer); log('setup did not start: ' + err.message); resolve(-1); });
    child.on('exit', (code) => { clearTimeout(timer); log('setup exited with ' + code); resolve(code); });
  });
}

function movingWindow() {
  const html = '<!doctype html><meta charset="utf-8"><title>Quarkology</title><body style="margin:0;display:flex;align-items:center;'
    + 'justify-content:center;height:100vh;font:15px system-ui,Segoe UI,sans-serif;background:#16181c;color:#d5d8de">'
    + '<div><b>Quark Project is now Quarkology</b><br><span style="color:#8a909a">Moving your tabs and settings over: Quarkology opens by itself.</span></div>';
  const w = new BrowserWindow({ width: 520, height: 140, resizable: false, minimizable: false, maximizable: false, title: 'Quarkology',
    autoHideMenuBar: true, backgroundColor: '#16181c', webPreferences: { sandbox: true, javascript: false } });
  w.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  return w;
}

app.whenReady().then(async () => {
  if (!installed() && IS_WIN && isFile(SETUP)) {
    const w = movingWindow();
    await runSetup();
    try { w.destroy(); } catch { /* closed */ }
  }
  if (installed() && await startQuarkology()) { app.exit(0); return; }
  log('Quarkology could not be started from ' + NEW);
  dialog.showMessageBoxSync({ type: 'error', title: 'Quarkology', noLink: true, buttons: ['OK'],
    message: 'Quarkology could not be started',
    detail: 'Quark Project is now called Quarkology, in ' + NEW + ', but it is not installed there yet. The details are in '
      + LOG + '. Run repository\\tools\\quarkology\\setup.cmd to install it.' });
  app.exit(1);
});
