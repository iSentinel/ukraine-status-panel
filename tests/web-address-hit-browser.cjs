/** Check address suggestions against the real UI; all responses are local fixtures. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require(process.env.USP_PLAYWRIGHT_MODULE ||
  path.resolve(__dirname, "../tools/web-ui/node_modules/playwright"));
const args = process.argv.slice(2);
function option(name) { const index = args.indexOf(name); return index < 0 ? undefined : args[index + 1]; }

/** Serve the actual assets and isolate every request from the panel and providers. */
async function routes(page, script, css, calls) {
  await page.route("**/*", route => {
    const u = new URL(route.request().url());
    if (u.pathname === "/") return route.fulfill({contentType:"text/html",
      body:'<!doctype html><meta charset=utf-8><link rel=stylesheet href=/app.css><esp-app></esp-app><script type=module src=/app.js></script>'});
    if (u.pathname === "/app.js") return route.fulfill({contentType:"text/javascript",body:script});
    if (u.pathname === "/app.css") return route.fulfill({contentType:"text/css",body:css});
    if (u.pathname === "/api/streets") return route.fulfill({json:[{name:"Main avenue",id:1}]});
    if (u.pathname === "/api/houses") return route.fulfill({json:[{name:"13"},{name:"15"}]});
    if (u.pathname === "/api/group") { calls.push(u.href); return route.fulfill({json:{ok:true,group:10,subgroup:1}}); }
    throw Error("Unexpected request " + u.href);
  });
}

/** Publish the minimum ordinary source state without mutating a real device. */
async function initialize(page, language) {
  await page.addInitScript(() => {
    localStorage.setItem("usp-active-tab", "svitlo");
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.fixtureSource = this; }
      emit(value) { this.dispatchEvent(new MessageEvent("state",{data:JSON.stringify(value)})); }
    };
  });
  await page.goto("http://usp.test/");
  await page.evaluate(language => {
    fixtureSource.emit({id:"lang",name:"UI Language",domain:"select",
      sorting_group:"Panel settings",value:language,option:[language]});
    fixtureSource.emit({id:"group",name:"Outage Group Code",domain:"text",
      sorting_group:"SvitloBot",state:"10.1"});
  }, language);
  const consent = page.locator(".usp-external-consent");
  if (await consent.count()) await consent.click();
  await page.locator(".usp-finder input").first().waitFor();
}

/** Measure hit testing while house suggestions are open, then click the action. */
async function scenario(browser, script, css, width, language) {
  const page = await browser.newPage({viewport:{width,height:900}});
  page.setDefaultTimeout(10000);
  const calls = [];
  await routes(page, script, css, calls);
  await initialize(page, language);
  await page.locator(".usp-finder input").first().fill("Main");
  await page.getByRole("button",{name:"Main avenue",exact:true}).click();
  const house = page.locator(".usp-finder input").nth(1);
  await page.getByRole("button",{name:"13",exact:true}).waitFor();
  await house.fill("13");
  await page.getByRole("button",{name:"15",exact:true}).waitFor();
  const find = page.locator(".usp-finder .usp-actions button").first();
  assert.equal(await find.isEnabled(), true);
  await find.scrollIntoViewIfNeeded();
  const hit = await find.evaluate(button => {
    const r = button.getBoundingClientRect();
    return [.25,.5,.75].map(x => {
      const el = document.elementFromPoint(r.x+r.width*x,r.y+r.height/2);
      return {fraction:x,clear:el===button||button.contains(el),cover:el?.className};
    });
  });
  const result = {width,language,hit,clear:hit.every(p=>p.clear)};
  if (result.clear) { await find.click(); await page.waitForFunction(() => !!document.querySelector(".usp-finder .usp-primary:not([hidden])")); assert.equal(calls.length,1); }
  await page.close();
  return result;
}

/** Save evidence before asserting, including the failing baseline geometry. */
async function main() {
  if (args.includes("--help")) { console.log("node probe.cjs --script FILE --css FILE --output NEW.json"); return; }
  const script = fs.readFileSync(option("--script"),"utf8");
  const css = fs.readFileSync(option("--css") || path.resolve(__dirname,"../firmware/webui/usp-web-ui.css"),"utf8");
  const browser = await chromium.launch({headless:true});
  const rows = [];
  try {
    for (const width of [390,1024,320,768])
      for (const language of ["English","Русский","Українська"])
        rows.push(await scenario(browser,script,css,width,language));
  } finally { await browser.close(); }
  const report = {status:rows.every(r=>r.clear)?"PASS":"FAIL",cases:rows,
    device_contact:false,synthetic_provider_responses:true};
  if (args.includes("--output")) fs.writeFileSync(option("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  assert.equal(report.status,"PASS","F-W4 suggestions intercept the Find group action");
  console.log("FW4_ADDRESS_HIT=PASS cases=12");
}
main().catch(e=>{console.error(e);process.exitCode=1;});
