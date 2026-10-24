// Tests: the separate tests editor (http://127.0.0.1:8775/), embedded. The iframe is made only when the tab opens,
// after a quick check that the tests editor answers; polls never touch it (update does nothing).
import { html } from './util.js';

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

async function check(box, my) {
  box.innerHTML = String(html`<p class="muted pad">Looking for the tests editor at <code>${TESTS_URL}</code>…</p>`);
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), 1500);
  let up = false;
  try {
    await fetch(TESTS_URL, { mode: 'no-cors', cache: 'no-store', signal: ctl.signal });
    up = true;
  } catch { /* not running */ } finally { clearTimeout(timer); }
  if (my !== token || !box.isConnected) return;
  if (!up) {
    box.innerHTML = String(html`<section class="card offline tests-down">
      <h2>The tests editor isn't running</h2>
      <p>This tab shows the tests editor from <a href="${TESTS_URL}" target="quark-tests-editor" rel="noopener"><code>${TESTS_URL}</code></a>, and nothing answered there.</p>
      <p><button type="button" class="primary" data-tests-retry>Try again</button></p></section>`);
    return;
  }
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
    box.addEventListener('click', (e) => { if (e.target.closest('[data-tests-retry]')) check(box, ++token); });
    check(box, ++token);
  },
  update() {},
};
window.addEventListener('resize', () => { const f = document.querySelector('.tests-frame'); if (f) fit(f); });
