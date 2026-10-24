// The headless browser the page tests use: playwright-core (PW_CORE, else require, else npm's npx cache: run
// `npx playwright --version` once to put it there) driving an installed Edge or Chrome (BROWSER_EXE to pick one).
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');

function findPlaywright() {
  if (process.env.PW_CORE) return require(process.env.PW_CORE);
  try { return require('playwright-core'); } catch (e) { /* look in the npx cache */ }
  const npx = path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), '.npm'), 'npm-cache', '_npx');
  const npx2 = path.join(os.homedir(), '.npm', '_npx');
  for (const root of [npx, npx2]) {
    for (const d of fs.existsSync(root) ? fs.readdirSync(root) : []) {
      const p = path.join(root, d, 'node_modules', 'playwright-core');
      if (fs.existsSync(p)) return require(p);
    }
  }
  throw new Error('playwright-core not found (run: npx playwright --version, or set PW_CORE)');
}

function findBrowser() {
  const c = [process.env.BROWSER_EXE, 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Google/Chrome/Application/chrome.exe', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge'];
  return c.find((p) => p && fs.existsSync(p));
}

// the page under test: UIC_HTML, else the installed one (Quark/ui-customizer/ui-customizer.html, next to the checkout)
function defaultHtml() {
  return process.env.UIC_HTML || path.resolve(__dirname, '..', '..', '..', '..', 'ui-customizer', 'ui-customizer.html');
}

module.exports = { findPlaywright, findBrowser, defaultHtml };
