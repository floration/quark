// Release testing: docs/RELEASE_CHECKLIST.md (GET /api/checklist) as checkboxes. Every "| ☐ | ... |" row is one box;
// ticks are kept in this browser only (localStorage, keyed by the row's text, so a reworded row starts unticked).
import { esc } from './util.js';

const KEY = 'gm-release-checks';
let ticks = {};
try { ticks = JSON.parse(localStorage.getItem(KEY) || '{}') || {}; } catch { ticks = {}; }
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(ticks)); } catch { /* private window */ } };

// inline markdown: `code`, **bold**, [text](url)
function inline(s) {
  return esc(s)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>')
    .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (m, t, u) => (/^https?:/.test(u) ? `<a href="${u}" target="_blank" rel="noopener">${t}</a>` : t));
}

function render(box, data) {
  if (!data.text) {
    box.innerHTML = `<section class="card offline"><h2>RELEASE_CHECKLIST.md not found</h2><p>Looked in: ${esc((data.looked || []).join(', '))}</p></section>`;
    return;
  }
  const out = [];
  let open = false, n = 0, done = 0;
  for (const line of data.text.split('\n')) {
    const h = line.match(/^(##+)\s+(.*)/);
    const row = line.match(/^\|\s*☐\s*\|(.*)\|\s*$/);
    if (h && h[1] === '##') {
      if (open) out.push('</section>');
      out.push(`<section class="card rc-sec"><h2>${inline(h[2])}</h2>`);
      open = true;
    } else if (h && open) {
      out.push(`<h3>${inline(h[2])}</h3>`);
    } else if (row && open) {
      const text = row[1].trim();
      const on = !!ticks[text];
      n++; if (on) done++;
      out.push(`<label class="rc-item${on ? ' done' : ''}"><input type="checkbox" data-rc="${esc(text)}"${on ? ' checked' : ''}><span>${inline(text)}</span></label>`);
    }
  }
  if (open) out.push('</section>');
  box.innerHTML = `<div class="rc-bar"><b class="rc-count">${done} / ${n} done</b>
    <button type="button" data-rc-reload>Reload checklist</button>
    <button type="button" data-rc-clear>Clear all ticks (new build)</button>
    <span class="muted small">${esc(data.path || '')}</span></div>${out.join('')}`;
}

async function load(box) {
  try {
    const r = await fetch('/api/checklist', { cache: 'no-store' });
    render(box, await r.json());
  } catch (e) {
    box.innerHTML = `<p class="msg bad">Could not load the checklist: ${esc(e.message || e)}</p>`;
  }
}

export const release = {
  title: 'Release testing',
  mount(el) {
    el.innerHTML = '<div class="rc-box"></div>';
    const box = el.firstChild;
    box.addEventListener('change', (e) => {
      const c = e.target.closest('[data-rc]');
      if (!c) return;
      if (c.checked) ticks[c.dataset.rc] = true; else delete ticks[c.dataset.rc];
      save();
      c.closest('.rc-item').classList.toggle('done', c.checked);
      const all = box.querySelectorAll('[data-rc]');
      box.querySelector('.rc-count').textContent = `${box.querySelectorAll('[data-rc]:checked').length} / ${all.length} done`;
    });
    box.addEventListener('click', (e) => {
      if (e.target.closest('[data-rc-reload]')) load(box);
      else if (e.target.closest('[data-rc-clear]') && confirm('Untick every box?')) { ticks = {}; save(); load(box); }
    });
    load(box);
  },
  update() {},
};
