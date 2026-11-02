/** Exercise reset buttons only in an isolated Chromium fixture. No panel access. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require(process.env.USP_PLAYWRIGHT_MODULE || path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const args = process.argv.slice(2);
const value = key => { const index=args.indexOf(key); return index<0 ? undefined : args[index+1]; };

/** Serve a local UI and record synthetic POSTs without contacting any server. */
async function fixture(browser, script, name, hold, language) {
  const page = await browser.newPage();
  let release;
  const pending = new Promise(resolve => { release=resolve; });
  const state = {posts:0,dialogs:0,accept:false,status:200};
  await page.addInitScript(() => {
    localStorage.setItem("usp-active-tab","maintenance");
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.fixtureSource=this; }
      emit(row) { this.dispatchEvent(new MessageEvent("state",{data:JSON.stringify(row)})); }
    };
  });
  await page.route("**/*", async route => {
    const u = new URL(route.request().url());
    if (u.pathname==="/") return route.fulfill({contentType:"text/html",body:"<!doctype html><esp-app></esp-app><script type=module src=/app.js></script>"});
    if (u.pathname==="/app.js") return route.fulfill({contentType:"text/javascript",body:script});
    if (u.pathname.startsWith("/button/")) {
      state.posts++; if(hold) await pending;
      return route.fulfill({status:state.status,body:"{}"});
    }
    throw Error("Unexpected request "+u.href);
  });
  page.on("dialog", async dialog => {
    state.dialogs++;
    if(state.accept) await dialog.accept(); else await dialog.dismiss();
  });
  await page.goto("http://reset-fixture.test/");
  await page.evaluate(({name,language}) => {
    fixtureSource.emit({id:"lang",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:language,option:[language]});
    fixtureSource.emit({id:"reset-fixture",name,domain:"button",sorting_group:"Maintenance"});
  },{name,language});
  await page.locator('[data-entity-id="reset-fixture"] button').waitFor();
  return {page,state,release};
}

/** Reject a confirmation and observe whether any request was sent. */
async function cancelCase(browser,script,name,language) {
  const f=await fixture(browser,script,name,false,language);
  await f.page.locator('[data-entity-id="reset-fixture"] button').click();
  await f.page.waitForTimeout(100);
  const result={name,language,dialogs:f.state.dialogs,posts:f.state.posts};
  await f.page.close();
  return result;
}

/** Hold the first POST to expose duplicate activation while it is pending. */
async function duplicateCase(browser,script,name,language) {
  const f=await fixture(browser,script,name,true,language);
  f.state.accept=true;
  const button=f.page.locator('[data-entity-id="reset-fixture"] button');
  await button.click();
  await f.page.waitForTimeout(100);
  await button.evaluate(b=>b.click());
  await f.page.waitForTimeout(100);
  const result={name,language,dialogs:f.state.dialogs,pending_posts:f.state.posts};
  await f.page.evaluate(name=>fixtureSource.emit({id:"reset-fixture",name,domain:"button",sorting_group:"Maintenance",state:""}),name);
  await f.page.waitForTimeout(100);
  assert.equal(await button.isDisabled(),true,"pending button remains disabled after SSE");
  f.state.status=500;
  f.release();
  await f.page.waitForFunction(()=>!document.querySelector('[data-entity-id="reset-fixture"] button').disabled);
  assert.equal(f.state.posts,1,"SSE must not permit a duplicate pending POST");
  f.state.status=200;
  await button.click(); await f.page.waitForTimeout(100);
  assert.equal(f.state.posts,2,"retry after error must be possible");
  result.retry_after_error=true;
  await f.page.close();
  return result;
}

/** Save failures as evidence before returning a nonzero result. */
async function main() {
  if(args.includes("--help")) { console.log("node tests/web-reset-confirm-browser.cjs --script FILE [--output NEW.json]"); return; }
  assert.ok(value("--script"),"--script is required");
  const script=fs.readFileSync(value("--script"),"utf8");
  const browser=await chromium.launch({headless:true});
  const cancel=[],duplicate=[];
  try {
    for(const language of ["English","Русский","Українська"]) for(const name of ["Reset Network","Reset Integrations","Restart"]) {
      cancel.push(await cancelCase(browser,script,name,language));
      duplicate.push(await duplicateCase(browser,script,name,language));
    }
  } finally { await browser.close(); }
  const report={status:cancel.every(r=>r.dialogs===1&&r.posts===0)&&duplicate.every(r=>r.pending_posts===1)?"PASS":"FAIL",
    cancel,duplicate,device_contact:false,fixture_posts_only:true};
  if(value("--output")) fs.writeFileSync(value("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  assert.equal(report.status,"PASS","F-W5 reset confirmation or pending-request guard missing");
  console.log("FW5_RESET_BROWSER=PASS");
}
main().catch(e=>{console.error(e);process.exitCode=1;});
