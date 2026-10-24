// Shared nav: dims the tools that aren't running (checked once at load, no-cors with a short timeout).
const TOOLS = { dashboard: 'http://127.0.0.1:8765/', docs: 'http://127.0.0.1:8766/' };
if (!document.documentElement.classList.contains('embed')) {
  for (const [tool, url] of Object.entries(TOOLS)) {
    const a = document.querySelector(`.qt-nav a[data-tool="${tool}"]`);
    if (!a) continue;
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 1500);
    fetch(url, { mode: 'no-cors', cache: 'no-store', signal: ctl.signal })
      .catch(() => { a.classList.add('down'); a.title = `${a.textContent} isn't running`; })
      .finally(() => clearTimeout(timer));
  }
}
