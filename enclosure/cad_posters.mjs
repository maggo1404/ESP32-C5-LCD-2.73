#!/usr/bin/env node
// Erzeugt Vorschaubilder (Poster) fuer den 3D-Viewer: laedt web/ + export/glb headless
// (Playwright/Chromium, WebGL via SwiftShader) und speichert export/glb/posters/<modell>.png.
//   node cad_posters.mjs          (benoetigt: npm i playwright && npx playwright install chromium)
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
const glb = path.resolve(here, 'export/glb');
const out = path.join(glb, 'posters');
const models = ['assembly', 'body', 'front_bezel', 'battery_door', 'vent_clip', 'assembly_exploded'];

// temporaeres Site-Layout wie in pages.yml: web/ + gehaeuse/3d/
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'cad-posters-'));
fs.cpSync(web, root, { recursive: true });
fs.cpSync(glb, path.join(root, 'gehaeuse/3d'), { recursive: true });
fs.rmSync(path.join(root, 'gehaeuse/3d/posters'), { recursive: true, force: true });

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.glb': 'model/gltf-binary', '.json': 'application/json', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  const p = path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'content-type': types[path.extname(p)] || 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

fs.mkdirSync(out, { recursive: true });
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
try {
  const page = await browser.newPage({ viewport: { width: 800, height: 600 }, colorScheme: 'light' });
  page.on('pageerror', (e) => console.error('pageerror', e.message));
  await page.route(/esp-web-tools|unpkg\.com/, (r) => r.abort());
  await page.goto(url);
  await page.evaluate(() => { document.getElementById('cad-open').hidden = false; });
  await page.click('#cad-open');
  await page.addStyleTag({ content: '.cad-ui{visibility:hidden!important}.cad-stage{inset:0!important}' });
  await page.waitForTimeout(200);
  for (const m of models) {
    await page.waitForSelector('.cad-dialog[open]');
    if (m !== 'assembly') await page.dispatchEvent(`.cad-tab[data-model="${m}"]`, 'click');
    await page.waitForSelector(`.cad-dialog[data-ready="${m}"]`, { timeout: 60000 });
    await page.waitForTimeout(300);
    await page.screenshot({ path: path.join(out, `${m}.png`) });
    console.log('poster', m);
  }
} finally {
  await browser.close();
  server.close();
  fs.rmSync(root, { recursive: true, force: true });
}
