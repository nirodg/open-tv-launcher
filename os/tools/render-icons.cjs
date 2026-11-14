// Renders every icon symbol from prototype/index.html to a white PNG for the OS UI.
//   node os/tools/render-icons.cjs
// Output: os/assets/icons/<id>.png (96×96, transparent, white strokes — tinted at runtime)
const fs = require('fs');
const path = require('path');
const { chromium } = require(path.join(__dirname, '..', '..', 'node_modules', 'playwright'));

const ROOT = path.join(__dirname, '..', '..');
const SRC = fs.readFileSync(path.join(ROOT, 'prototype', 'index.html'), 'utf8');
const OUT = path.join(ROOT, 'os', 'assets', 'icons');

const syms = [...SRC.matchAll(/<symbol id="([^"]+)" viewBox="([^"]+)">([\s\S]*?)<\/symbol>/g)]
  .map(m => ({ id: m[1], vb: m[2], inner: m[3] }));
if (!syms.length) { console.error('no symbols found'); process.exit(1); }

const svgDoc = s => `<!doctype html><html><body style="margin:0;background:transparent">
<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96" viewBox="${s.vb}"
 style="color:#fff;fill:none;stroke:#fff;stroke-width:2;stroke-linecap:round;stroke-linejoin:round">${s.inner}</svg>
</body></html>`;

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 96, height: 96 } });
  for (const s of syms) {
    await page.setContent(svgDoc(s));
    const el = page.locator('svg');
    await el.screenshot({ path: path.join(OUT, `${s.id}.png`), omitBackground: true });
  }
  await browser.close();
  console.log(`rendered ${syms.length} icons → ${OUT}`);
})();
