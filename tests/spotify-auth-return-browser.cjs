/* Actual Chromium popup messaging; every URL is intercepted with synthetic data. */
const fs = require("node:fs"), path = require("node:path"), assert = require("node:assert/strict");
const {chromium} = require(path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const args = process.argv.slice(2);
const opt = key => {const i=args.indexOf(key);return i<0?undefined:args[i+1];};
const ROOT = path.resolve(__dirname,"..");
const CHANNEL = opt("--channel") || path.join(ROOT,"firmware/webui/usp-spotify-auth-return.js");
const RELAY_DIR = opt("--relay-dir") || path.join(ROOT,"web-installer/spotify-callback");
const STATE = "MTExMTExMTExMTExMTExMQ";
const PANEL = "http://192.168.50.22", RELAY = "https://return.test";
const read = p => fs.readFileSync(p === "usp-spotify-auth-return.js" ? CHANNEL : path.join(RELAY_DIR,path.basename(p)), "utf8");

async function fixture(browser, locale = "en-US", panelOrigin = PANEL) {
  const context = await browser.newContext({locale, viewport:{width:320,height:640}});
  const errors = [], unexpected = [];
  context.on("page", page => page.on("pageerror", e => errors.push(String(e))));
  await context.route("**/*", route => {
    const u = new URL(route.request().url());
    if (u.origin === panelOrigin && u.pathname === "/")
      return route.fulfill({contentType:"text/html", body:'<!doctype html><meta charset=utf-8><script type="module" src="/channel.js"></script><script>window.results=[];window.failures=[];</script>'});
    if (u.origin === panelOrigin && u.pathname === "/channel.js")
      return route.fulfill({contentType:"text/javascript",body:read("usp-spotify-auth-return.js")});
    if (u.origin === RELAY && u.pathname === "/cb")
      return route.fulfill({contentType:"text/html",body:read("spotify-callback/index.html")});
    if (u.origin === RELAY && ["/relay.js","/relay.css"].includes(u.pathname))
      return route.fulfill({contentType:u.pathname.endsWith(".js")?"text/javascript":"text/css",body:read("spotify-callback"+u.pathname)});
    if ((u.origin === RELAY && u.pathname === "/fake") || u.origin === "https://other.test")
      return route.fulfill({contentType:"text/html",body:"<!doctype html><title>attack fixture</title>"});
    unexpected.push(u.origin + u.pathname);
    return route.abort();
  });
  const page = await context.newPage(); await page.goto(panelOrigin);
  return {context, page, errors, unexpected};
}

/** Install the real local channel before navigating the actual popup. */
async function open(f, query, withChannel = true, targetOrigin = RELAY, targetPath = "/cb") {
  const ready = f.context.waitForEvent("page");
  await f.page.evaluate(({state, relay, query, withChannel, targetOrigin, targetPath}) => {
    window.popup = window.open("about:blank", "spotify-auth-fixture");
    if (withChannel) window.channel = new UspSpotifyAuthReturn({
      popup, state, redirectUri:relay+"/cb",
      onResult:r => results.push(r), onError:r => failures.push(r)
    });
    popup.location = targetOrigin + targetPath + query;
  }, {state:STATE,relay:RELAY,query,withChannel,targetOrigin,targetPath});
  const popup = await ready; await popup.waitForURL(u => u.origin === targetOrigin && u.pathname === targetPath); await popup.waitForLoadState("load");
  return popup;
}

async function success(browser, locale, denied = false) {
  const f = await fixture(browser, locale);
  try {
    const field = denied ? "error=access_denied" : "code=opaque%2B%2F%25%26%3D";
    const popup = await open(f, "?state="+STATE+"&"+field);
    await f.page.waitForFunction(() => results.length === 1, null, {timeout:7000});
    const result = await f.page.evaluate(() => results[0]);
    assert.deepEqual(result, denied ? {state:STATE,error:"access_denied"} : {state:STATE,code:"opaque+/%&="});
    assert.equal(new URL(popup.url()).search, "");
    assert.equal(await popup.locator("html").getAttribute("lang"), locale.slice(0,2));
    assert.equal(await popup.locator("#result").inputValue(), "");
    await popup.evaluate(({state,panel}) => opener.postMessage({type:"usp-spotify-callback",state,code:"replay"},panel), {state:STATE,panel:PANEL});
    await f.page.waitForTimeout(40);
    assert.equal(await f.page.evaluate(() => results.length), 1);
    assert.deepEqual(f.errors, []); assert.deepEqual(f.unexpected, []);
  } catch (error) {
    const snapshots=[];
    for (const p of f.context.pages()) snapshots.push(await p.evaluate(() => ({
      url:location.href,opener:!!window.opener,status:document.getElementById("status")?.textContent,
      results:window.results,failures:window.failures
    })).catch(()=>({unavailable:true})));
    console.error(JSON.stringify({errors:f.errors,unexpected:f.unexpected,snapshots}));
    throw error;
  } finally {await f.context.close();}
}

async function invalid(browser) {
  const queries = [
    "?state="+STATE+"&state="+STATE+"&code=x",
    "?state="+STATE+"&code=x&error=access_denied",
    "?state="+STATE+"&code=%0A",
    "?state="+STATE+"&code="+ "x".repeat(1025),
    "?state="+STATE+"&code=x#bad",
    "?state="+STATE+"&code=x&unknown=y",
    "?code=x", "?state="+STATE+"&code="
  ];
  for (const query of queries) {
    const f = await fixture(browser);
    try {
      const popup = await open(f, query);
      await f.page.waitForTimeout(40);
      assert.equal(await f.page.evaluate(() => results.length), 0);
      assert.equal(await popup.locator("#manual").isVisible(), false);
      assert.equal(new URL(popup.url()).search, "");
      assert.equal(new URL(popup.url()).hash, "");
      assert.deepEqual(f.errors, []); assert.deepEqual(f.unexpected, []);
    } finally {await f.context.close();}
  }
  return queries.length;
}

/** Missing opener has a bounded manual fallback, with explicit clipboard action. */
async function manual(browser, locale) {
  const f = await fixture(browser, locale);
  try {
    const popup = await f.context.newPage();
    await popup.addInitScript(() => {
      window.copyCalls = [];
      Object.defineProperty(navigator, "clipboard", {value:{writeText:async text => copyCalls.push(text)}});
      const original = window.setTimeout.bind(window);
      window.setTimeout = (fn, delay, ...rest) => {
        if (delay === 60000) window.expireReturn = fn;
        return original(fn, delay, ...rest);
      };
    });
    await popup.goto(RELAY+"/cb?state="+STATE+"&code=manual-code");
    assert.equal(await popup.locator("#manual").isVisible(), true);
    assert.equal(await popup.locator("#result").getAttribute("readonly"), "");
    await popup.locator("#copy").click();
    const copied = await popup.evaluate(() => copyCalls);
    assert.equal(copied.length, 1);
    assert.deepEqual(JSON.parse(copied[0]), {type:"usp-spotify-callback",state:STATE,code:"manual-code"});
    const box = await popup.locator("#copy").boundingBox(); assert.ok(box.height >= 48);
    assert.ok(await popup.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await popup.evaluate(() => expireReturn());
    assert.equal(await popup.locator("#result").inputValue(), "");
    assert.equal(await popup.locator("#manual").isVisible(), false);
    assert.deepEqual(f.errors, []); assert.deepEqual(f.unexpected, []);
  } finally {await f.context.close();}
}

/** Genuine browser origins and window references reject forged return messages. */
async function attacks(browser) {
  const f = await fixture(browser);
  try {
    let popup = await open(f, "", true, "https://other.test", "/");
    await popup.evaluate(({panel,state}) => opener.postMessage({type:"usp-spotify-callback",state,code:"wrong-origin"},panel), {panel:PANEL,state:STATE});
    await f.page.evaluate(() => channel.close()); await popup.close();
    const candidate = f.context.waitForEvent("page");
    await f.page.evaluate(({relay,state}) => {
      window.popup = window.open("about:blank", "candidate2");
      window.channel = new UspSpotifyAuthReturn({popup,state,redirectUri:relay+"/cb",onResult:r=>results.push(r)});
      popup.location = relay+"/fake";
    }, {relay:RELAY,state:STATE});
    popup = await candidate; await popup.waitForURL(u => u.origin === RELAY && u.pathname === "/fake"); await popup.waitForLoadState("load");
    await popup.evaluate(panel => opener.postMessage({type:"usp-spotify-callback",state:"wrong",code:"wrong-state"},panel), PANEL);
    const another = f.context.waitForEvent("page");
    await f.page.evaluate(relay => {window.attacker=window.open(relay+"/fake","other-window");}, RELAY);
    const other = await another; await other.waitForURL(u => u.origin === RELAY && u.pathname === "/fake"); await other.waitForLoadState("load");
    await other.evaluate(({panel,state}) => opener.postMessage({type:"usp-spotify-callback",state,code:"wrong-window"},panel), {panel:PANEL,state:STATE});
    await f.page.waitForTimeout(40); assert.equal(await f.page.evaluate(() => results.length), 0);
    await popup.evaluate(({panel,state}) => opener.postMessage({type:"usp-spotify-callback",state,code:"valid"},panel), {panel:PANEL,state:STATE});
    await f.page.waitForFunction(() => results.length === 1, null, {timeout:7000});
    assert.equal(await f.page.evaluate(() => results[0].code), "valid");
    assert.deepEqual(f.errors, []); assert.deepEqual(f.unexpected, []);
  } finally {await f.context.close();}
}

/** Real relay ignores a wrong binding state, then sends exactly once. */
async function bindGuard(browser) {
  const f = await fixture(browser);
  try {
    await f.page.evaluate(() => addEventListener("message", e => {if(e.data?.type==="usp-spotify-callback") results.push(e.data);}));
    const popup = await open(f, "?state="+STATE+"&code=bound", false);
    await f.page.evaluate(relay => popup.postMessage({type:"usp-spotify-bind",state:"wrong"},relay), RELAY);
    await f.page.waitForTimeout(40); assert.equal(await f.page.evaluate(() => results.length), 0);
    await f.page.evaluate(({relay,state}) => popup.postMessage({type:"usp-spotify-bind",state},relay), {relay:RELAY,state:STATE});
    await f.page.waitForFunction(() => results.length === 1, null, {timeout:7000});
    await f.page.evaluate(({relay,state}) => popup.postMessage({type:"usp-spotify-bind",state},relay), {relay:RELAY,state:STATE});
    await f.page.waitForTimeout(40); assert.equal(await f.page.evaluate(() => results.length), 1);
    assert.deepEqual(f.errors, []); assert.deepEqual(f.unexpected, []);
  } finally {await f.context.close();}
}

/** Public HTTP origins cannot bind a callback even with the correct state. */
async function publicOrigin(browser) {
  const f = await fixture(browser,"en-US","http://public.test");
  try {
    await f.page.evaluate(() => {
      window.readyCount=0;
      addEventListener("message", e => {if(e.data?.type==="usp-spotify-relay-ready") ++readyCount;});
    });
    const popup = await open(f, "?state="+STATE+"&code=guarded");
    await f.page.waitForFunction(() => readyCount > 0);
    await f.page.waitForTimeout(80);
    assert.equal(await f.page.evaluate(() => results.length),0);
    assert.equal(await popup.locator("#result").inputValue(),"");
    assert.deepEqual(f.errors,[]);assert.deepEqual(f.unexpected,[]);
  } finally {await f.context.close();}
}

/** When the opener disappears, the real relay keeps a manual return path. */
async function closedOpener(browser) {
  const f = await fixture(browser);
  try {
    const popup = await open(f,"?state="+STATE+"&code=closed-parent",false);
    await f.page.close();
    await popup.locator("#manual").waitFor({state:"visible"});
    assert.equal(JSON.parse(await popup.locator("#result").inputValue()).code,"closed-parent");
    assert.deepEqual(f.errors,[]);assert.deepEqual(f.unexpected,[]);
  } finally {await f.context.close();}
}

async function main() {
  if (args.includes("--help")) {console.log("node tests/spotify-auth-return-browser.cjs [--channel JS] [--relay-dir DIR] [--output JSON]");return;}
  assert.ok(fs.existsSync(CHANNEL) && fs.existsSync(RELAY_DIR),"Missing return assets");
  const browser = await chromium.launch({headless:true});
  let count;
  try {
    for (const locale of ["en-US","ru-RU","uk-UA"]) {
      await success(browser, locale); await manual(browser, locale);
    }
    await success(browser,"en-US",true);
    count = await invalid(browser); await attacks(browser); await bindGuard(browser);
    await publicOrigin(browser); await closedOpener(browser);
  } finally {await browser.close();}
  const report = {status:"PASS",languages:3,invalid_cases:count,actual_chromium_popup:true,
    origin_source_state_guards:true,replay_blocked:true,manual_fallback:true,manual_ttl_ms:60000,public_http_origin_blocked:true,closed_opener_fallback:true,
    clipboard:"fixture only",unexpected_requests:0,spotify_requests:0,device_contact:false};
  if(args.includes("--output")) fs.writeFileSync(opt("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  console.log("SPOTIFY_AUTH_RETURN_BROWSER=PASS languages3 actual_popup origin_source_state replay manual expiry invalid"+count+" no_network");
}
main().catch(e=>{console.error(e);process.exitCode=1;});
