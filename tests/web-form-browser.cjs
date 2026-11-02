/**
 * Check the real browser behavior of text forms and address search.
 * All HTTP responses and source events are fixtures; no device is contacted.
 * Usage: node web-form-browser.cjs --script path/to/usp-web-ui.js
 * USP_PLAYWRIGHT_MODULE may select a locally installed Playwright package.
 */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require(process.env.USP_PLAYWRIGHT_MODULE ||
  path.resolve(__dirname, "../tools/web-ui/node_modules/playwright"));

/** A controllable response lets requests complete in reverse order. */
function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}

/** Bound fixture waits so a broken request cannot leave this test running. */
async function waitForFixture(predicate) {
  const deadline = Date.now() + 10000;
  while (!predicate()) {
    assert.ok(Date.now() < deadline, "Fixture request did not start");
    await new Promise(resolve => setTimeout(resolve, 20));
  }
}

/** Install a source-event substitute before the app opens its connection. */
async function installEvents(page) {
  await page.addInitScript(() => {
    localStorage.setItem("usp-active-tab", "starlink");
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.fixtureSource = this; }
      emit(type, value) {
        this.dispatchEvent(new MessageEvent(type, {data:JSON.stringify(value)}));
      }
    };
  });
}

/** Serve only fixtures, including errors and delayed search responses. */
async function routes(page, script, state) {
  await page.route("**/*", async route => {
    const url = new URL(route.request().url());
    if (url.pathname === "/") return route.fulfill({contentType:"text/html",
      body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/app.css"><esp-app></esp-app><script src="/app.js"></script>' });
    if (url.pathname === "/app.css") return route.fulfill({contentType:"text/css",
      body:fs.readFileSync(path.resolve(__dirname,"../firmware/webui/usp-web-ui.css"),"utf8")});
    if (url.pathname === "/app.js") return route.fulfill({contentType:"text/javascript", body:script});
    if (url.pathname.startsWith("/switch/") || url.pathname.startsWith("/select/")) {
      (state.actions ||= []).push({path:url.pathname, option:url.searchParams.get("option")});
      return route.fulfill({status:200, body:"{}"});
    }
    if (url.pathname.startsWith("/button/")) {
      (state.buttonPosts ||= []).push(decodeURIComponent(url.pathname.split("/")[2] || ""));
      return route.fulfill({status:200, body:"{}"});
    }
    if (url.pathname.startsWith("/text/")) {
      state.posts.push(url.searchParams.get("value"));
      if (state.holdPost) await state.holdPost.promise;
      if (state.disconnect) return route.abort("failed");
      return route.fulfill({status:state.postStatus, body:"{}"});
    }
    if (url.pathname === "/api/streets") {
      const query = url.searchParams.get("q");
      if (query === "Old") { state.oldStarted = true; await state.old.promise; }
      return route.fulfill({json:[{name:query + " avenue", id:query}]});
    }
    if (url.pathname === "/api/houses") return route.fulfill({json:[{name:"13"}]});
    if (url.pathname === "/api/group") {
      if (state.holdGroup) { state.groupStarted = true; await state.holdGroup.promise; }
      return route.fulfill({json:{ok:true, group:3, subgroup:1}});
    }
    throw new Error("Unexpected fixture request: " + url.href);
  });
}

/** Publish representative entities through the app's normal source listener. */
async function entities(page) {
  await page.evaluate(() => {
    fixtureSource.emit("ping", {title:"Test panel", uptime:10});
    for (const entity of [
      {id:"host", name:"Starlink Host", domain:"text", sorting_group:"Starlink", mode:0, state:"initial.invalid", min_length:1, max_length:255},
      {id:"group", name:"Outage Group Code", domain:"text", sorting_group:"SvitloBot", state:"12.1", min_length:1, max_length:8},
      {id:"language", name:"UI Language", domain:"select", sorting_group:"Panel settings", value:"English", option:["English", "Русский"]},
      {id:"optional", name:"Optional secret", domain:"text", sorting_group:"Starlink", mode:1, state:"", min_length:0, max_length:64},
      {id:"security", name:"Web Security Status", domain:"text_sensor", sorting_group:"Maintenance", state:"FACTORY"},
      {id:"refresh", name:"Refresh Weather", domain:"button", sorting_group:"Location"},
      {id:"starlink-enabled", name:"Starlink Enabled", domain:"switch", sorting_group:"Starlink", state:"OFF"},
      {id:"buzzer", name:"Buzzer Test", domain:"button", sorting_group:"Hardware"},
      {id:"restart", name:"Restart", domain:"button", sorting_group:"Maintenance"},
    ]) fixtureSource.emit("state", entity);
  });
  await page.locator('[data-entity-id="host"]').waitFor();
}

/** Check error preservation, a real click from a focused field and duplicates. */
async function textForms(page, state) {
  const card = page.locator('[data-entity-id="host"]');
  const input = card.locator("input"), save = card.getByRole("button", {name:"Save", exact:true});
  await input.fill("first.invalid");
  await save.click();
  await card.getByRole("status").filter({hasText:"Unconfirmed"}).waitFor();
  assert.equal(await input.inputValue(), "first.invalid");
  assert.deepEqual(state.posts, ["first.invalid"]);
  state.postStatus = 200; state.holdPost = deferred();
  await save.click();
  await page.waitForFunction(() => document.querySelector('[data-entity-id="host"] button').disabled);
  await card.locator("button").evaluate(button => button.click());
  await input.fill("newer.invalid");
  state.holdPost.resolve();
  await card.getByRole("status").filter({hasText:"Request accepted"}).waitFor();
  assert.equal(await input.inputValue(), "newer.invalid");
  assert.equal(state.posts.length, 2);
  state.holdPost = null;
  await save.click();
  await page.waitForFunction(() => document.querySelector('[data-entity-id="host"] input').value === "");
  assert.equal(state.posts.at(-1), "newer.invalid");
  state.disconnect = true;
  await input.fill("offline.invalid"); await save.click();
  await card.getByRole("status").filter({hasText:"Unconfirmed"}).waitFor();
  assert.equal(await input.inputValue(), "offline.invalid");
  state.disconnect = false;
  console.log("PASS R01: HTTP/network failure, click, duplicate and later edit");
}

/** Keep clearing an optional secret explicit while rejecting required empties. */
async function emptyForms(page, state) {
  const before = state.posts.length;
  const host = page.locator('[data-entity-id="host"]');
  await host.locator("input").fill("");
  await host.getByRole("button", {name:"Save", exact:true}).click();
  assert.equal(state.posts.length, before);
  const optional = page.locator('[data-entity-id="optional"]');
  await optional.getByRole("button", {name:"Save", exact:true}).click();
  await optional.getByRole("status").filter({hasText:"Request accepted"}).waitFor();
  assert.equal(state.posts.at(-1), "");
  console.log("PASS empty fields: required rejected, optional secret may be cleared");
}

/** A discovery event must preserve both the actual focus and selected text. */
async function focusDuringUpdates(page) {
  const input = page.locator('[data-entity-id="host"] input');
  await input.fill("keep-this-draft.invalid");
  await input.evaluate(element => element.setSelectionRange(2, 8));
  await page.evaluate(() => fixtureSource.emit("state", {
    id:"extra", name:"Additional diagnostic", domain:"sensor", sorting_group:"Starlink", state:1,
  }));
  await page.locator('[data-entity-id="extra"]').waitFor();
  assert.equal(await input.inputValue(), "keep-this-draft.invalid");
  assert.equal(await input.evaluate(element => document.activeElement === element), true);
  assert.deepEqual(await input.evaluate(element => [element.selectionStart, element.selectionEnd]), [2,8]);
  await page.evaluate(() => fixtureSource.emit("state", {
    id:"language", name:"UI Language", value:"Русский",
  }));
  await page.locator(".usp-tab").filter({hasText:"Обзор"}).waitFor();
  assert.equal(await input.inputValue(), "keep-this-draft.invalid");
  assert.equal(await input.evaluate(element => document.activeElement === element), true);
  await page.evaluate(() => fixtureSource.emit("state", {id:"language", name:"UI Language", value:"English"}));
  await page.locator(".usp-tab").filter({hasText:"Overview"}).waitFor();
  console.log("PASS focus: discovery and language update preserve draft/selection");
}

/** Record the browser event order when focus leaves a text draft for Save. */
async function interactionJournal(page) {
  await page.evaluate(() => {
    window.__uspR06Events = [];
    const card = document.querySelector('[data-entity-id="host"]');
    const input = card.querySelector("input"), save = card.querySelector("button");
    save.addEventListener("pointerdown", () => window.__uspR06Events.push("pointerdown"), {once:true});
    input.addEventListener("focusout", () => window.__uspR06Events.push("focusout"), {once:true});
    save.addEventListener("click", () => window.__uspR06Events.push("click"), {once:true});
  });
}

/** Mouse and keyboard submit exactly once while the focused draft survives blur. */
async function mouseAndKeyboardSubmit(page, state) {
  const card = page.locator('[data-entity-id="host"]');
  const input = card.locator("input"), save = card.getByRole("button", {name:"Save", exact:true});
  state.postStatus = 200; state.disconnect = false; state.holdPost = null;
  await input.fill("mouse.invalid"); await input.focus(); await interactionJournal(page);
  const mouseBefore = state.posts.length;
  await save.click(); await waitForFixture(() => state.posts.length === mouseBefore + 1);
  const events = await page.evaluate(() => window.__uspR06Events);
  assert.deepEqual(events, ["pointerdown", "focusout", "click"]);
  assert.equal(state.posts.at(-1), "mouse.invalid");
  await page.waitForFunction(() => document.querySelector('[data-entity-id="host"] input').value === "");
  await input.fill("keyboard.invalid"); await input.focus();
  const keyBefore = state.posts.length;
  await input.press("Tab");
  assert.equal(await save.evaluate(button => document.activeElement === button), true);
  await save.press("Enter"); await waitForFixture(() => state.posts.length === keyBefore + 1);
  await page.waitForTimeout(80); assert.equal(state.posts.length, keyBefore + 1);
  assert.equal(state.posts.at(-1), "keyboard.invalid");
  console.log("PASS R05/R06 mouse+Tab/Enter: event order and single POST");
}

/** A real touchscreen tap must cross blur and still issue exactly one request. */
async function touchSubmit(browser, script) {
  const state = {posts:[], postStatus:200, old:deferred()};
  const context = await browser.newContext({hasTouch:true, viewport:{width:390, height:800}});
  try {
    const page = await context.newPage(); page.setDefaultTimeout(10000);
    await installEvents(page); await routes(page, script, state);
    await page.goto("http://usp.test/"); await entities(page);
    const card = page.locator('[data-entity-id="host"]');
    const input = card.locator("input"), save = card.getByRole("button", {name:"Save", exact:true});
    await input.fill("touch.invalid"); await input.focus(); await interactionJournal(page);
    const box = await save.boundingBox(); assert.ok(box);
    await page.touchscreen.tap(box.x + box.width/2, box.y + box.height/2);
    await waitForFixture(() => state.posts.length === 1); await page.waitForTimeout(80);
    const events = await page.evaluate(() => window.__uspR06Events);
    assert.ok(events.includes("pointerdown") && events.includes("focusout") && events.includes("click"), events);
    assert.ok(events.indexOf("pointerdown") < events.indexOf("focusout"), events);
    assert.ok(events.indexOf("focusout") < events.indexOf("click"), events);
    assert.deepEqual(state.posts, ["touch.invalid"]);
    console.log("PASS R06 touch: pointerdown/focusout/click and single POST");
  } finally { await context.close(); }
}

/** A focused switch must consume SSE state and keep keyboard focus on its new action. */
async function switchFocusUpdates(page, state) {
  const card = page.locator('[data-entity-id="starlink-enabled"]');
  const button = card.locator("button"), pill = card.locator(".usp-pill");
  await card.waitFor();
  assert.equal(await pill.innerText(), "Off");
  assert.equal(await button.innerText(), "On");
  const before = state.actions?.length || 0;
  await button.click();
  assert.equal(await button.evaluate(element => document.activeElement === element), true);
  await page.evaluate(() => fixtureSource.emit("state", {
    id:"starlink-enabled", name:"Starlink Enabled", domain:"switch",
    sorting_group:"Starlink", state:"ON",
  }));
  await page.waitForFunction(() =>
    document.querySelector('[data-entity-id="starlink-enabled"] .usp-pill')?.textContent === "On");
  assert.equal(await button.innerText(), "Off");
  assert.equal(await button.evaluate(element => document.activeElement === element), true);
  await button.press("Enter");
  await waitForFixture(() => (state.actions?.length || 0) === before + 2);
  assert.deepEqual(
    state.actions.slice(before).map(action => action.path.split("/").at(-1)),
    ["turn_on", "turn_off"],
  );
  console.log("PASS F-W1: focused switch follows SSE, keeps focus and reverses action");
}

/** Use real typing and suggestion clicks to exercise dependent buttons. */
async function addressSelection(page, state) {
  await page.locator(".usp-tab").filter({hasText:/^Power$/}).click();
  // Address lookup stays disabled until the user allows the external service.
  await page.locator(".usp-external-consent").click();
  const street = page.locator(".usp-finder input").nth(0);
  const house = page.locator(".usp-finder input").nth(1);
  const find = page.getByRole("button", {name:"Find group", exact:true});
  await street.fill("Main");
  await page.getByRole("button", {name:"Main avenue", exact:true}).click();
  await page.getByRole("button", {name:"13", exact:true}).click();
  assert.equal(await find.isEnabled(), true);
  await find.click();
  const apply = page.getByRole("button", {name:"Apply group", exact:true});
  await apply.waitFor({state:"visible"});
  const old = await apply.elementHandle();
  const before = state.posts.length;
  await house.fill("99");
  assert.equal(await apply.isVisible(), false);
  await old.evaluate(button => button.onclick());
  assert.equal(state.posts.length, before);
  assert.equal(await find.isEnabled(), true);
  console.log("PASS R02/R03: button enables and changed address invalidates Apply");
}

/** Stale street or group responses must never revive an earlier address. */
async function staleResponses(page, state) {
  const street = page.locator(".usp-finder input").nth(0);
  const house = page.locator(".usp-finder input").nth(1);
  await street.fill("Old");
  await waitForFixture(() => state.oldStarted);
  await street.fill("New");
  await page.getByRole("button", {name:"New avenue", exact:true}).waitFor();
  const oldFinished = page.waitForResponse(response => response.url().includes("streets?q=Old"));
  state.old.resolve(); await oldFinished;
  await page.getByRole("button", {name:"New avenue", exact:true}).click();
  assert.equal(await page.getByRole("button", {name:"Old avenue", exact:true}).count(), 0);
  await page.getByRole("button", {name:"13", exact:true}).click();
  state.holdGroup = deferred();
  await page.getByRole("button", {name:"Find group", exact:true}).click();
  await waitForFixture(() => state.groupStarted);
  await house.fill("100");
  const groupFinished = page.waitForResponse(response => response.url().includes("/api/group"));
  state.holdGroup.resolve(); await groupFinished;
  assert.equal(await page.getByRole("button", {name:"Apply group", exact:true}).isVisible(), false);
  console.log("PASS R04: late street/group responses are ignored");
}


/** Publish only disabled fixture schedules through the normal event stream. */
async function seedAlarms(page, language) {
  await page.evaluate(value => {
    fixtureSource.emit("state", {id:"language", name:"UI Language", domain:"select",
      sorting_group:"Panel settings", value, option:["English","Русский","Українська"]});
    for (let index=1;index<=3;index++) {
      const common={sorting_group:"Hardware", sorting_weight:60+index*4};
      fixtureSource.emit("state", {...common,id:"alarm-time-"+index,
        name:"Alarm "+index+" Time",domain:"text",state:"07:00",min_length:5,max_length:5});
      fixtureSource.emit("state", {...common,id:"alarm-enabled-"+index,
        name:"Alarm "+index+" Enabled",domain:"switch",state:"OFF"});
      fixtureSource.emit("state", {...common,id:"alarm-repeat-"+index,
        name:"Alarm "+index+" Repeat",domain:"select",state:"Daily",
        option:["Daily","Weekdays","Weekends"]});
    }
  }, language);
}

/** Check localized labels, strict HH:MM validation and unchanged wire values. */
async function alarmForms(page, state) {
  state.postStatus=200;
  for (const language of ["English","Русский","Українська"]) {
    await seedAlarms(page,language);
    await page.getByRole("tab",{name:/^(Hardware|Оборудование|Обладнання)$/}).click();
    const card=page.locator('[data-entity-id="alarm-time-1"]');
    await card.waitFor();
    assert.match(await card.locator(".usp-name").innerText(), /Alarm 1 Time|Будильник 1/);
    assert.equal(await card.locator("input").getAttribute("placeholder"),"07:00");
    const before=state.posts.length;
    await card.locator("input").fill("24:99");
    await card.locator("button").click();
    assert.equal(state.posts.length,before,"Invalid HH:MM must not be posted");
    await card.locator("input").fill("08:15");
    await card.locator("button").click();
    await waitForFixture(()=>state.posts.length===before+1);
    assert.equal(state.posts.at(-1),"08:15");
    const select=page.locator('[data-entity-id="alarm-repeat-1"] select');
    await select.selectOption("Weekdays");
    await waitForFixture(()=>state.actions?.at(-1)?.option==="Weekdays");
    assert.equal(await select.locator('option[value="Weekdays"]').innerText(),language==="English"?"Weekdays":"Пн–Пт");
    for(const width of [320,390,1024,1440]) {
      await page.setViewportSize({width,height:900});
      const bad=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
      assert.equal(bad,false);
    }
  }
  console.log("PASS alarms: UA/RU/EN HH:MM rejection, generic controls and 12 widths");
}

/** Check R07 security semantics, action verbs and the complete tab pattern. */
async function webSemantics(page, state) {
  await page.evaluate(() => fixtureSource.emit("state", {
    id:"language", name:"UI Language", value:"English",
    option:["English","Русский","Українська"],
  }));
  const tablist = page.getByRole("tablist");
  await tablist.waitFor();
  const hardware = tablist.getByRole("tab", {name:"Hardware", exact:true});
  await hardware.waitFor();
  assert.equal(await hardware.getAttribute("aria-selected"), "true");
  assert.equal(await hardware.getAttribute("tabindex"), "0");
  await hardware.focus();
  await hardware.press("ArrowRight");
  const power = page.getByRole("tab", {name:"Power", exact:true});
  await page.waitForFunction(() => document.activeElement?.textContent === "Power");
  assert.equal(await power.getAttribute("aria-selected"), "true");
  assert.equal(await power.getAttribute("tabindex"), "0");
  await power.press("End");
  const maintenance = page.getByRole("tab", {name:"Maintenance", exact:true});
  await page.waitForFunction(() => document.activeElement?.textContent === "Maintenance");
  assert.equal(await maintenance.getAttribute("aria-selected"), "true");
  await maintenance.press("Home");
  const setup = page.getByRole("tab", {name:"Setup", exact:true});
  await page.waitForFunction(() => document.activeElement?.textContent === "Setup");
  assert.equal(await setup.getAttribute("aria-selected"), "true");

  await maintenance.click();
  const security = page.locator('[data-entity-id="security"] .usp-pill');
  await security.waitFor();
  assert.match(await security.getAttribute("class"), /\bwarn\b/);
  assert.equal(await security.innerText(), "Factory credentials");
  assert.equal(await page.locator('[data-entity-id="restart"]').getByRole("button").innerText(), "Restart");

  await page.getByRole("tab", {name:"Location", exact:true}).click();
  const refresh = page.locator('[data-entity-id="refresh"]').getByRole("button");
  assert.equal(await refresh.innerText(), "Refresh Weather");
  await refresh.click();
  await waitForFixture(() => state.buttonPosts?.includes("Refresh Weather"));

  await page.getByRole("tab", {name:"Hardware", exact:true}).click();
  assert.equal(await page.locator('[data-entity-id="buzzer"]').getByRole("button").innerText(), "Buzzer Test");

  await page.evaluate(() => fixtureSource.emit("state", {
    id:"language", name:"UI Language", value:"Русский",
  }));
  await page.getByRole("tab", {name:"Обслуживание", exact:true}).click();
  assert.equal(await page.locator('[data-entity-id="security"] .usp-pill').innerText(), "Заводские учётные данные");
  assert.equal(await page.locator('[data-entity-id="restart"]').getByRole("button").innerText(), "Перезагрузка");

  await page.evaluate(() => fixtureSource.emit("state", {
    id:"language", name:"UI Language", value:"Українська",
  }));
  await page.getByRole("tab", {name:"Локація", exact:true}).click();
  assert.equal(await page.locator('[data-entity-id="refresh"]').getByRole("button").innerText(), "Оновити погоду");
  await page.getByRole("tab", {name:"Обладнання", exact:true}).click();
  assert.equal(await page.locator('[data-entity-id="buzzer"]').getByRole("button").innerText(), "Тест бузера");

  await page.evaluate(() => fixtureSource.emit("state", {
    id:"security", name:"Web Security Status", domain:"text_sensor",
    sorting_group:"Maintenance", state:"DEFAULT",
  }));
  await page.getByRole("tab", {name:"Обслуговування", exact:true}).click();
  const defaultSecurity = page.locator('[data-entity-id="security"] .usp-pill');
  assert.match(await defaultSecurity.getAttribute("class"), /\bbad\b/);
  assert.equal(await defaultSecurity.innerText(), "Потрібно змінити стандартний пароль");

  await page.evaluate(() => fixtureSource.emit("state", {
    id:"security", name:"Web Security Status", domain:"text_sensor",
    sorting_group:"Maintenance", state:"CUSTOM",
  }));
  const customSecurity = page.locator('[data-entity-id="security"] .usp-pill');
  await page.waitForFunction(() =>
    document.querySelector('[data-entity-id="security"] .usp-pill')?.textContent === "Пароль змінено");
  assert.match(await customSecurity.getAttribute("class"), /\bgood\b/);
  assert.equal(await customSecurity.innerText(), "Пароль змінено");
  assert.equal(await page.locator('[role="tabpanel"]').getAttribute("aria-labelledby"), "usp-tab-maintenance");
  console.log("PASS R07: security semantics, action labels, tab ARIA and keyboard navigation");
}

/** Run deterministic fixtures in Chromium and always close the browser. */
async function main() {
  if (process.argv.includes("--help")) {
    console.log("Usage: node web-form-browser.cjs --script path/to/Web-UI.js");
    return;
  }
  const index = process.argv.indexOf("--script");
  if (index < 0 || !process.argv[index+1]) throw new Error("--script is required");
  const script = fs.readFileSync(process.argv[index+1], "utf8");
  const state = {posts:[], postStatus:500, old:deferred()};
  const browser = await chromium.launch({headless:true});
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await installEvents(page); await routes(page, script, state);
    await page.goto("http://usp.test/"); await entities(page);
    await textForms(page, state); await emptyForms(page, state);
    await focusDuringUpdates(page); await mouseAndKeyboardSubmit(page, state);
    await touchSubmit(browser, script); await switchFocusUpdates(page, state);
    await addressSelection(page, state); await staleResponses(page, state);
    await alarmForms(page, state);
    await webSemantics(page, state);
    assert.deepEqual(errors, []);
    console.log("WEB_FORM_BROWSER=PASS synthetic fixtures; no device access");
  } finally { await browser.close(); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
