#!/usr/bin/env node
"use strict";
const fs = require("node:fs");
const path = require("node:path");
const assert = require("node:assert/strict");
const ROOT = path.resolve(__dirname, "..");
const cache = path.resolve(ROOT, "../.qa-tools/playwright");
if (!process.env.PLAYWRIGHT_BROWSERS_PATH && fs.existsSync(cache)) {
  process.env.PLAYWRIGHT_BROWSERS_PATH = cache;
}
const { chromium } = require(path.join(ROOT, "tools/web-ui/node_modules/playwright"));

async function verifyPortal(page, html) {
  let saved = null;
  await page.route("**/*", async route => {
    const url = new URL(route.request().url());
    assert.equal(url.hostname, "portal.test");
    if (url.pathname === "/") {
      return route.fulfill({ contentType: "text/html", body: html });
    }
    if (url.pathname === "/config.json") {
      return route.fulfill({ contentType: "application/json", body: JSON.stringify({
        name: "Fixture panel", mac: "00:11:22:33:44:55",
        aps: [{}, { ssid: "Fixture WiFi", rssi: -35, lock: 1 }]
      }) });
    }
    if (url.pathname === "/wifisave") {
      saved = url.searchParams;
      return route.fulfill({ contentType: "text/plain", body: "Saved. Connecting..." });
    }
    return route.abort();
  });
  await page.goto("http://portal.test/");
  assert.equal(await page.locator('input[type="file"]').count(), 0);
  assert.equal(await page.locator('form[action="/update"]').count(), 0);
  await page.locator("#ssid").fill("Fixture WiFi");
  await page.locator("#psk").fill("fixture-password");
  await page.locator('form[action="/wifisave"] button').click();
  await page.waitForURL(/\/wifisave\?/);
  assert.equal(saved.get("ssid"), "Fixture WiFi");
  assert.equal(saved.get("psk"), "fixture-password");
}

async function main() {
  const html = fs.readFileSync(path.join(ROOT, "components/captive_portal/portal.html"), "utf8");
  const browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
    await verifyPortal(page, html);
  } finally {
    await browser.close();
  }
  console.log("CAPTIVE_PORTAL_BROWSER=PASS WiFi-only DOM/save synthetic fixture; no device access");
}
main().catch(error => { console.error(error); process.exitCode = 1; });
