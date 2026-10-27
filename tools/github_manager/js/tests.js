// Tests: the separate tests editor (http://127.0.0.1:8775/), embedded. The iframe is made only when the tab opens,
// after a quick check that the tests editor answers; polls never touch it (update does nothing).
import { html } from './util.js';
import { send } from './ghedit.js';

export const TESTS_URL = 'http://127.0.0.1:8775/';
let token = 0;

// http://127.0.0.1:8775/#ci/release.yml/linux-tests%20(net)?run=123 (the tests editor drops matrix suffixes itself)
export function testsJobUrl(file, job, run) {
  return `${TESTS_URL}#ci/${encodeURIComponent(file)}/${encodeURIComponent(job)}?run=${encodeURIComponent(run)}`;
}

function fit(frame) {
  if (!frame.isConnected) return;
  frame.style.height = `${Math.max(320, innerHeight - frame.getBoundingClientRect().top - 4)}px`;
}

async function answers() {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), 1500);
  try {
    await fetch(TESTS_URL, { mode: 'no-cors', cache: 'no-store', signal: ctl.signal });
    return true;
  } catch { return false; } finally { clearTimeout(timer); }
}

function down(box, note) {
  box.innerHTML = String(html`<section class="card offline tests-down">
    <h2>The tests editor isn't running</h2>
    <p>This tab shows the tests editor from <a href="${TESTS_URL}" target="quark-tests-editor" rel="noopener"><code>${TESTS_URL}</code></a>, and nothing answered there.
      <b>Start it</b> runs its <code>serve.py</code> in the background (no window, low priority; the Quarkology app's install, or the one in a local copy).</p>
    ${note || ''}
    <p><button type="button" class="primary" data-tests-start>Start it</button> <button type="button" data-tests-retry>Try again</button></p></section>`);
}

async function check(box, my) {
  box.innerHTML = String(html`<p class="muted pad">Looking for the tests editor at <code>${TESTS_URL}</code>…</p>`);
  const up = await answers();
  if (my !== token || !box.isConnected) return;
  if (!up) { down(box); return; }
  embed(box);
}

// Start it on the server's side, then look for it every second for up to 20 s
async function start(box, my) {
  box.innerHTML = String(html`<p class="muted pad"><span class="spin s-run" aria-hidden="true"></span> Starting the tests editor…</p>`);
  let got;
  try {
    got = await send('POST', '/api/tools/tests-editor', {});
  } catch (e) {
    if (my === token && box.isConnected) down(box, html`<p class="msg bad small" role="alert">Could not start it: ${e.message || e}</p>`);
    return;
  }
  for (let i = 0; i < 20; i++) {
    await new Promise((r) => setTimeout(r, 1000));
    if (my !== token || !box.isConnected) return;
    if (await answers()) { if (my === token && box.isConnected) embed(box); return; }
  }
  if (my === token && box.isConnected) down(box, html`<p class="msg warn small" role="alert">Started <code>${got.script || 'serve.py'}</code>, but it hasn't answered after 20 s. Its log is <code>cache/serve.log</code> next to it; try again in a moment.</p>`);
}

function embed(box) {
  box.innerHTML = String(html`<iframe class="tests-frame" src="${TESTS_URL}?embed=1" title="Tests editor"></iframe>`);
  const frame = box.querySelector('iframe');
  fit(frame);
  requestAnimationFrame(() => fit(frame));
}

export const tests = {
  title: 'Tests',
  mount(el) {
    el.innerHTML = '<div id="tests-box" class="tests-box"></div>';
    const box = el.querySelector('#tests-box');
    box.addEventListener('click', (e) => {
      if (e.target.closest('[data-tests-retry]')) check(box, ++token);
      else if (e.target.closest('[data-tests-start]')) start(box, ++token);
    });
    check(box, ++token);
  },
  update() {},
};
window.addEventListener('resize', () => { const f = document.querySelector('.tests-frame'); if (f) fit(f); });
