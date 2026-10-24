'use strict';
// Runs (sandboxed, isolated world) in every tab and in the tab bar.
// Tabs get window.quarkShell = { open(tabId, hash), retry(), setBadge(n), version } (setBadge: project tab only).
// The tab bar (started with --quark-tabbar) gets window.quarkTabbar instead.
const { contextBridge, ipcRenderer } = require('electron');

const arg = (name) => {
  const pre = '--' + name + '=';
  const a = (process.argv || []).find((x) => x.startsWith(pre));
  return a ? a.slice(pre.length) : '';
};
const version = arg('quark-shell-version');

if ((process.argv || []).includes('--quark-tabbar')) {
  contextBridge.exposeInMainWorld('quarkTabbar', {
    version,
    open: (id) => ipcRenderer.send('shell:open', String(id), ''),
    command: (cmd) => ipcRenderer.send('shell:tabbar', String(cmd)),
    tabs: () => ipcRenderer.invoke('shell:tabs'),
    onTabs: (fn) => { ipcRenderer.on('shell:tabs', (e, data) => fn(data)); },
  });
} else {
  contextBridge.exposeInMainWorld('quarkShell', {
    version,
    // switch to tab <tabId>; for a tab that is already open, set its location.hash (e.g. "#pr=12")
    open: (tabId, hash) => ipcRenderer.send('shell:open', String(tabId), hash == null ? '' : String(hash)),
    // the "Start" / "Try again" button of the shell's own "not running" page
    retry: () => ipcRenderer.send('shell:retry'),
    // the Supervisor Board's halting count: a red badge on its tab and the taskbar (ignored from any other tab)
    setBadge: (n) => ipcRenderer.send('shell:badge', Number(n) || 0),
  });
}
