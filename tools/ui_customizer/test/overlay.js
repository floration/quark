// node test/overlay.js [ui-customizer.html] [out dir] [page id substring ...] - how close the customizer draws a page to
// the game: for each page (default: a few typical ones) writes <out>/<page>.png with three rows (the game's screenshot,
// the customizer's drawing, their difference) and prints the share of pixels that differ (the customizer draws over the screenshot, so only
// what its widgets draw differently counts) by more than 48 levels.
// Needs the page's screenshots (extract/shots next to the HTML: install.py --extract). Headless (playwright-core + an
// installed Edge / Chrome, like e2e.js).
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { findPlaywright, findBrowser, defaultHtml } = require('./browser.js');

(async () => {
  const html = path.resolve(process.argv[2] || defaultHtml());
  const out = path.resolve(process.argv[3] || path.join(os.tmpdir(), 'quark-ui-overlay'));
  const want = process.argv.slice(4);
  const picks = want.length ? want : ['main', 'settings', 'settings/video', 'settings/controls', 'ingame/pause', 'editor', 'host'];
  fs.mkdirSync(out, { recursive: true });
  const { chromium } = findPlaywright();
  const browser = await chromium.launch({ executablePath: findBrowser(), headless: true });
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto('file:///' + html.replace(/\\/g, '/'));
  await page.waitForTimeout(500);
  await page.evaluate(() => document.fonts.ready);
  const ids = await page.evaluate((picks) => {
    const all = QApp.S.doc.pages;
    const out = [];
    for (const p of picks) {
      const exact = all.find((x) => x.id === p && x.screenshot);
      const sub = exact || all.find((x) => x.id.includes(p) && x.screenshot);
      if (sub && !out.includes(sub.id)) out.push(sub.id);
    }
    return out;
  }, picks);
  let worst = 0;
  for (const id of ids) {
    const shot = await page.evaluate((id) => QApp.baseline.pages.find((x) => x.id === id).screenshot, id);
    const data = fs.readFileSync(path.join(path.dirname(html), /^extract\//.test(shot) ? '' : 'extract', shot)).toString('base64');
    const r = await page.evaluate(async ([id, data]) => {   // (the shot as a data URL: a file:// image would taint the canvas)
      const p = QApp.baseline.pages.find((x) => x.id === id);
      const img = new Image();
      img.src = 'data:image/png;base64,' + data;
      await img.decode();
      const W = 1280, H = 720;
      const c = document.createElement('canvas'); c.width = W; c.height = H * 3;
      const g = c.getContext('2d');
      g.drawImage(img, 0, 0, W, H);
      const d = document.createElement('canvas'); d.width = W; d.height = H;
      const dg = d.getContext('2d');
      // the game's frame as the backdrop (it has the 3D view / menu map the customizer only suggests), the page over it
      dg.drawImage(img, 0, 0, W, H);
      const veil = (e) => e.kind === 'view' || (e.kind === 'rect' && e.x <= 1 && e.y <= 1 && e.w >= W * 0.95 && e.h >= H * 0.95);   // (in the shot already / a 3D view placeholder)
      for (const e of p.elements) if (QRender.box(dg, e) && !veil(e)) { dg.save(); QRender.drawElement(dg, e, false); dg.restore(); }
      g.drawImage(d, 0, H);
      const a = g.getImageData(0, 0, W, H).data, b = dg.getImageData(0, 0, W, H).data;
      const diff = g.createImageData(W, H);
      let n = 0, m = 0;
      for (let i = 0; i < a.length; i += 4) {
        const e = Math.max(Math.abs(a[i] - b[i]), Math.abs(a[i + 1] - b[i + 1]), Math.abs(a[i + 2] - b[i + 2]));
        m++;
        if (e > 48) n++;
        diff.data[i] = diff.data[i + 1] = diff.data[i + 2] = Math.min(255, e * 2); diff.data[i + 3] = 255;
      }
      g.putImageData(diff, 0, H * 2);
      return { png: c.toDataURL('image/png'), share: m ? n / m : 0 };
    }, [id, data]);
    const file = path.join(out, id.replace(/[^A-Za-z0-9._-]+/g, '_') + '.png');
    fs.writeFileSync(file, Buffer.from(r.png.split(',')[1], 'base64'));
    worst = Math.max(worst, r.share);
    console.log(`${id}: ${(r.share * 100).toFixed(1)}% of the pixels differ -> ${file}`);
  }
  await browser.close();
  if (errors.length) { console.error('page errors:\n' + errors.join('\n')); process.exit(1); }
  if (!ids.length) console.log('no page with a screenshot matched (run install.py --extract for the screenshots)');
  console.log(`overlay: ${ids.length} pages, worst ${(worst * 100).toFixed(1)}%`);
})().catch((e) => { console.error(e); process.exit(1); });
