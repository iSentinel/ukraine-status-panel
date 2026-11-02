/** F-W3: Setup summaries must patch in place under ordinary SSE updates. */
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const {chromium}=require(process.env.USP_PLAYWRIGHT_MODULE||path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));

async function install(page){
  await page.addInitScript(()=>{
    localStorage.setItem("usp-active-tab","setup");
    window.EventSource=class extends EventTarget{
      constructor(){super();window.fixtureSource=this}
      emit(type,value){this.dispatchEvent(new MessageEvent(type,{data:JSON.stringify(value)}))}
    };
  });
}
async function route(page,script){
  await page.route("**/*",r=>{
    const u=new URL(r.request().url());
    if(u.pathname==="/")return r.fulfill({contentType:"text/html",body:"<!doctype html><meta charset=utf-8><esp-app></esp-app><script type=module src=/app.js></script>"});
    if(u.pathname==="/app.js")return r.fulfill({contentType:"text/javascript",body:script});
    if(u.pathname==="/events")return r.fulfill({status:204,body:""});
    throw Error("Unexpected request: "+u.href);
  });
}
async function seed(page){
  await page.evaluate(()=>{
    fixtureSource.emit("ping",{title:"Test panel",uptime:10});
    const rows=[
      {id:"ssid",name:"Wi-Fi SSID",domain:"text_sensor",sorting_group:"Status & diagnostics",state:"Lab"},
      {id:"language",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:"English",option:["English","Русский","Українська"]},
      {id:"region",name:"Selected Region",domain:"select",sorting_group:"Location",value:"Kyiv City",option:["Kyiv City"]},
      {id:"weather",name:"Weather Location",domain:"text",sorting_group:"Location",state:"Kyiv"},
      {id:"group",name:"Outage Group Code",domain:"text",sorting_group:"SvitloBot",state:"10.1"},
      {id:"diag",name:"Free Heap",domain:"sensor",sorting_group:"Status & diagnostics",state:60000},
    ];
    for(const e of rows) fixtureSource.emit("state",e);
  });
  await page.getByRole("tab",{name:"Setup",exact:true}).waitFor();
  await page.getByText("Lab",{exact:true}).waitFor();
}
async function main(){
  const i=process.argv.indexOf("--script"); assert.ok(i>=0&&process.argv[i+1],"--script is required");
  const script=fs.readFileSync(process.argv[i+1],"utf8");
  const browser=await chromium.launch({headless:true});
  try{
    const page=await browser.newPage(); page.setDefaultTimeout(10000);
    await install(page); await route(page,script); await page.goto("http://usp.test/"); await seed(page);
    const firstOpen=page.getByRole("button",{name:"Open",exact:true}).first();
    await firstOpen.focus();
    const identity=await firstOpen.evaluate(el=>{el.dataset.fw3="same";return el.isConnected&&document.activeElement===el});
    assert.equal(identity,true,"Setup Open button must be focused before SSE");
    await page.evaluate(()=>fixtureSource.emit("state",{id:"ssid",name:"Wi-Fi SSID",state:"Lab-2"}));
    await page.waitForTimeout(80);
    const result=await page.evaluate(()=>({
      activeTag:document.activeElement?.tagName,
      activeText:document.activeElement?.textContent,
      sameNode:document.querySelector("[data-fw3=same]")===document.activeElement,
      oldNodeConnected:!!document.querySelector("[data-fw3=same]"),
    }));
    assert.equal(result.sameNode,true,"F-W3: ordinary SSE replaced focused Setup action: "+JSON.stringify(result));
    assert.equal(await page.getByText("Lab",{exact:true}).count(),1,"focused Setup render must be deferred");
    await page.keyboard.press("Enter");
    const network=page.getByRole("tab",{name:"Network",exact:true});
    await network.waitFor(); assert.equal(await network.getAttribute("aria-selected"),"true");
    await page.getByRole("tab",{name:"Setup",exact:true}).click();
    await page.getByText("Lab-2",{exact:true}).waitFor();
    await page.evaluate(()=>document.activeElement?.blur());
    await page.evaluate(()=>fixtureSource.emit("state",{id:"ssid",name:"Wi-Fi SSID",state:"Lab-3"}));
    await page.getByText("Lab-3",{exact:true}).waitFor();
    console.log("F_W3_SETUP_SSE_BROWSER=PASS focus_preserved=1 deferred_state=1 eventual_refresh=1 action_survives=1");
  }finally{await browser.close()}
}
main().catch(e=>{console.error(e.stack||e);process.exit(1)});
