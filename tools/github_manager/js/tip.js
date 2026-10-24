// One shared hover tooltip for elements with data-tip (chart bars and lanes).
const tip = document.getElementById('tip');
let on = null;
function place(e) {
  const pad = 12, w = tip.offsetWidth, h = tip.offsetHeight;
  let x = e.clientX + pad, y = e.clientY + pad;
  if (x + w > innerWidth - 8) x = Math.max(8, e.clientX - w - pad);
  if (y + h > innerHeight - 8) y = Math.max(8, e.clientY - h - pad);
  tip.style.transform = `translate(${x}px, ${y}px)`;
}
document.addEventListener('pointerover', (e) => {
  const t = e.target.closest && e.target.closest('[data-tip]');
  if (t === on) return;
  on = t;
  if (!t) { tip.hidden = true; return; }
  tip.textContent = t.dataset.tip;
  tip.hidden = false;
  place(e);
});
document.addEventListener('pointermove', (e) => { if (on) place(e); });
document.addEventListener('scroll', () => { tip.hidden = true; on = null; }, { passive: true, capture: true });
