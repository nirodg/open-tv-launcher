// Renders every screen of both mockups to PNG at 1920x1080.
// Usage (from this folder's parent): node scripts/render.cjs
const { chromium } = require('playwright');
const path = require('path');
const root = path.join(__dirname, '..');
const sets = [
  ['tv-launcher.html', 'launcher', [['home','01-home'],['library','02-app-library'],['settings','03-settings-picture'],['quick','04-quick-panel'],['foundations','05-foundations']]],
  ['store-tv.html', 'store-tv', ['01-keyboard-search','02-store-discover','03-store-categories','04-store-app-details','05-store-library','06-store-settings','07-tv-guide','08-tv-now-playing'].map((n,i)=>['s'+(i+1), n])],
];
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1920, height: 1080 } });
  for (const [file, dir, shots] of sets) {
    await p.goto('file://' + path.join(root, file));
    for (const [id, name] of shots) await p.locator('#' + id).screenshot({ path: path.join(root, 'renders', dir, name + '.png') });
  }
  await b.close();
})();
