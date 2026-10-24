'use strict';
// Runs (sandboxed, isolated world) in every tab and in the tab bar.
// Tabs get window.quarkShell = { open(tabId, hash), retry(), setBadge(n), playChanged(), statsChanged(), version }
// (setBadge: project tab only; playChanged: Play tab only; statsChanged: Editor Manager tab only).
// The tab bar (started with --quark-tabbar) gets window.quarkTabbar instead.
// In a tab, a Ctrl+F the page did not use itself (not defaultPrevented) opens the shell's find box.
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
    // cmd: reload, reload-tabs, ready, relaunch, zoom-reset, tab-menu <id>, more-menu [ids], reorder <id> <beforeId|null>,
    // find {text, forward, again} | {close: true}
    command: (cmd, arg, arg2) => ipcRenderer.send('shell:tabbar', String(cmd), arg === undefined ? null : arg, arg2 === undefined ? null : arg2),
    tabs: () => ipcRenderer.invoke('shell:tabs'),
    onTabs: (fn) => { ipcRenderer.on('shell:tabs', (e, data) => fn(data)); },
    onCtrl: (fn) => { ipcRenderer.on('shell:ctrl', (e, down) => fn(down === true)); },
    onFind: (fn) => { ipcRenderer.on('shell:find', (e, data) => fn(data)); },
    onFindResult: (fn) => { ipcRenderer.on('shell:find-result', (e, data) => fn(data)); },
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
    // the Play tab started a game or saved a note: the shell checks for a waiting play note now
    playChanged: () => ipcRenderer.send('shell:play'),
    // the Editor Manager refreshed (or marked): the shell re-reads its summary for the tab bar's facts now
    statsChanged: () => ipcRenderer.send('shell:stats'),
  });
  // (checked after the event has gone through the page's own handlers, wherever they are registered)
  window.addEventListener('keydown', (e) => {
    if (!(e.ctrlKey || e.metaKey) || e.altKey || e.shiftKey || e.repeat || (e.key !== 'f' && e.key !== 'F')) return;
    setTimeout(() => { if (!e.defaultPrevented) ipcRenderer.send('shell:find'); }, 0);
  }, true);
}
