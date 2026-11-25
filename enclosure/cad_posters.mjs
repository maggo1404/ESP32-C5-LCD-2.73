#!/usr/bin/env node
// Erzeugt Vorschaubilder (Poster) fuer den 3D-Viewer: laedt web/ + export/glb headless
// (Playwright/Chromium, WebGL via SwiftShader) und speichert export/<variante>/glb/posters/<modell>.png.
//   node cad_posters.mjs [lipo|lifepo4|all]   (benoetigt: npm i playwright && npx playwright install chromium)
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.resolve(here, '../web');
const arg = process.argv[2] || 'all';
const variants = {
  lipo: ['assembly', 'body', 'front_bezel', 'battery_door', 'vent_clip', 'assembly_exploded'],
  lifepo4: ['assembly', 'body', 'front_bezel', 'battery_door', 'charger_lid', 'vent_clip', 'assembly_exploded'],
};
const todo = arg === 'all' ? Object.keys(variants) : [arg];

// temporaeres Site-Layout wie in pages.yml: web/ + gehaeuse/3d/<variante>/
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'cad-posters-'));
fs.cpSync(web, root, { recursive: true });
for (const v of Object.keys(variants)) {
  const glb = path.resolve(here, 'export', v, 'glb');
  if (!fs.existsSync(glb)) continue;
  fs.cpSync(glb, path.join(root, 'gehaeuse/3d', v), { recursive: true });
  fs.rmSync(path.join(root, 'gehaeuse/3d', v, 'posters'), { recursive: true, force: true });
}

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.glb': 'model/gltf-binary', '.json': 'application/json', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  const p = path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'content-type': types[path.extname(p)] || 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
try {
  const page = await browser.newPage({ viewport: { width: 800, height: 600 }, colorScheme: 'light' });
  page.on('pageerror', (e) => console.error('pageerror', e.message));
  await page.route(/esp-web-tools|unpkg\.com/, (r) => r.abort());
  await page.goto(url);
  await page.evaluate(() => { document.getElementById('cad-open').hidden = false; });
  await page.click('#cad-open');
  await page.addStyleTag({ content: '.cad-bar,.cad-close{display:none!important}' });
  await page.waitForTimeout(200);
  let first = true;
  for (const v of todo) {
    const out = path.resolve(here, 'export', v, 'glb', 'posters');
    fs.mkdirSync(out, { recursive: true });
    for (const m of variants[v]) {
      await page.waitForSelector('.cad-dialog[open]');
      // Variante und Modell ueber die (ausgeblendete) Leiste schalten
      await page.evaluate((vv) => document.querySelector(`.cad-variant[data-variant="${vv}"]`).click(), v);
      if (!first || m !== 'assembly' || v !== 'lipo') await page.evaluate((mm) => document.querySelector(`.cad-tab[data-model="${mm}"]`).click(), m);
      first = false;
      await page.waitForSelector(`.cad-dialog[data-variant="${v}"][data-ready="${m}"]`, { timeout: 60000 });
      await page.waitForTimeout(300);
      await page.screenshot({ path: path.join(out, `${m}.png`) });
      console.log('poster', v, m);
    }
  }
} finally {
  await browser.close();
  server.close();
  fs.rmSync(root, { recursive: true, force: true });
}
