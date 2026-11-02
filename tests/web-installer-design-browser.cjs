const fs = require('fs');
const http = require('http');
const path = require('path');
const { chromium } = require('../tools/web-ui/node_modules/playwright');

const ROOT = path.resolve(__dirname, '..');
const WEB = path.join(ROOT, 'web-installer');
const widths = [320, 375, 768, 1024];
const types = {
  '.html': 'text/html; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
};

function server() {
  return http.createServer((req, res) => {
    const raw = new URL(req.url, 'http://localhost').pathname;
    const relative = raw === '/' ? 'index.html' : decodeURIComponent(raw).replace(/^\/+/, '');
    const file = path.resolve(WEB, relative);
    if (!file.startsWith(WEB + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
      res.writeHead(404);
      res.end();
      return;
    }
    res.writeHead(200, {'content-type': types[path.extname(file)] || 'application/octet-stream'});
    res.end(fs.readFileSync(file));
  });
}

async function runtimeLoaded(page, failures, label) {
  try {
    await page.waitForFunction(
      () => Boolean(customElements.get('esp-web-install-button')),
      null,
      {timeout: 5000},
    );
  } catch {
    failures.push(`${label}: local esp-web-tools runtime did not register`);
  }
}

(async () => {
  const host = server();
  await new Promise(resolve => host.listen(0, '127.0.0.1', resolve));
  const url = `http://127.0.0.1:${host.address().port}/`;
  const localOrigin = new URL(url).origin;
  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage();
  const failures = [];
  const external = [];

  await page.route('**/*', async route => {
    const requestUrl = route.request().url();
    if (/^https?:/i.test(requestUrl) && new URL(requestUrl).origin !== localOrigin) {
      external.push(requestUrl);
      await route.abort();
      return;
    }
    await route.continue();
  });

  try {
    for (const width of widths) {
      await page.setViewportSize({width, height: 780});
      await page.goto(url, {waitUntil: 'domcontentloaded'});
      await runtimeLoaded(page, failures, `${width}`);
      const result = await page.evaluate(() => {
        const buttons = [...document.querySelectorAll('.langs button')];
        const h1 = document.querySelector('h1');
        const panel = document.querySelector('.panel');
        return {
          overflow: document.documentElement.scrollWidth > document.documentElement.clientWidth,
          buttonHeights: buttons.map(b => b.getBoundingClientRect().height),
          h1Size: parseFloat(getComputedStyle(h1).fontSize),
          panelLeft: panel.getBoundingClientRect().left,
          panelRight: panel.getBoundingClientRect().right,
          viewport: document.documentElement.clientWidth,
          localRuntime: [...document.scripts].some(
            script => script.src.includes('/vendor/esp-web-tools/10.4.0/install-button.js')
          ),
        };
      });
      if (result.overflow) failures.push(`${width}: horizontal overflow`);
      if (result.buttonHeights.some(v => v < 44)) failures.push(`${width}: language target <44`);
      if (result.h1Size > 36.1) failures.push(`${width}: h1 hero-scale ${result.h1Size}`);
      if (!result.localRuntime) failures.push(`${width}: local runtime script missing`);
      if (result.panelLeft < 0 || result.panelRight > result.viewport + 0.5) {
        failures.push(`${width}: panel outside viewport`);
      }
    }

    await page.setViewportSize({width: 375, height: 780});
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await runtimeLoaded(page, failures, 'language');
    for (const [lang, token] of [['uk','перше'], ['ru','первая'], ['en','first installation']]) {
      await page.click(`[data-lang="${lang}"]`);
      const state = await page.evaluate(() => ({
        lang: document.documentElement.lang,
        pressed: document.querySelector('[data-lang][aria-pressed="true"]')?.dataset.lang,
        device: document.querySelector('[data-i18n="device"]')?.textContent || '',
      }));
      if (state.lang !== lang || state.pressed !== lang || !state.device.includes(token)) {
        failures.push(`language ${lang}: state=${JSON.stringify(state)}`);
      }
    }
    if (external.length) failures.push(`external runtime requests: ${[...new Set(external)].join(',')}`);
  } finally {
    await browser.close();
    await new Promise(resolve => host.close(resolve));
  }

  if (failures.length) {
    console.error('WEB_INSTALLER_DESIGN_BROWSER=FAIL ' + failures.join('; '));
    process.exit(1);
  }
  console.log(
    'WEB_INSTALLER_DESIGN_BROWSER=PASS widths=320,375,768,1024 ' +
    'languages=UA,RU,EN overflow=0 touch44=1 local_runtime=10.4.0 external_requests=0'
  );
})();
