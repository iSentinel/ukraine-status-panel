/** Exercise optional browser storage failures without any panel access. */
const fs=require("node:fs"),assert=require("node:assert/strict");
const path=require("node:path");
const {chromium}=require(process.env.USP_PLAYWRIGHT_MODULE||path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const args=process.argv.slice(2),option=n=>{const index=args.indexOf(n);return index<0?undefined:args[index+1]};

/** Apply browser-policy failures before the real application starts. */
async function install(page,script,mode) {
  await page.addInitScript(mode=>{
    localStorage.removeItem("usp-active-tab");
    if(["get","both"].includes(mode))
      Storage.prototype.getItem=()=>{throw new DOMException("Storage access denied","SecurityError")};
    if(["set","both"].includes(mode))
      Storage.prototype.setItem=()=>{throw new DOMException("Storage quota exceeded","QuotaExceededError")};
    if(mode==="property")Object.defineProperty(window,"localStorage",{
      get(){throw new DOMException("Storage property denied","SecurityError")}});
    window.EventSource=class extends EventTarget {
      constructor(){super();window.fixtureSource=this}
      emit(value){this.dispatchEvent(new MessageEvent("state",{data:JSON.stringify(value)}))}
    };
  },mode);
  await page.route("**/*",r=>{
    const u=new URL(r.request().url());
    if(u.pathname==="/")return r.fulfill({contentType:"text/html",body:"<!doctype html><esp-app></esp-app><script type=module src=/app.js></script>"});
    if(u.pathname==="/app.js")return r.fulfill({contentType:"text/javascript",body:script});
    throw Error("Unexpected fixture request; no writes permitted: "+u.href);
  });
  await page.goto("http://storage-fixture.test/");
}

/** Navigate locally and make sure another Setup event cannot undo the choice. */
async function scenario(browser,script,mode,language) {
  const page=await browser.newPage(),errors=[];
  page.on("pageerror",e=>errors.push(e.name));
  await install(page,script,mode);
  const started=await page.evaluate(language=>{
    if(!window.fixtureSource)return false;
    fixtureSource.emit({id:"lang",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:language,option:["English","Русский","Українська"]});
    fixtureSource.emit({id:"setup",name:"Setup Status",domain:"text_sensor",sorting_group:"Setup",state:"INCOMPLETE"});
    return true;
  },language);
  await page.waitForTimeout(200);
  const tabs=await page.getByRole("tab").count();
  const network=page.locator("#usp-tab-network");
  const networkPresent=await network.count()===1;
  let selected=false,after=false;
  if(networkPresent){
    await network.click();await page.waitForTimeout(100);
    selected=await network.getAttribute("aria-selected")==="true";
    await page.evaluate(()=>fixtureSource.emit({id:"setup",name:"Setup Status",state:"INCOMPLETE"}));
    await page.waitForTimeout(100);
    after=await network.getAttribute("aria-selected")==="true";
  }
  const result={mode,language,source_started:started,tabs,network_present:networkPresent,
    selected_after_click:selected,selected_after_setup_event:after,page_errors:errors,
    pass:started&&tabs>0&&networkPresent&&selected&&after&&errors.length===0};
  await page.close();return result;
}

/** Keep the failing baseline and the normal-storage control case together. */
async function main() {
  if(args.includes("--help")){console.log("node tests/web-storage-browser.cjs --script FILE [--output NEW.json]");return}
  assert.ok(option("--script"),"--script is required");
  const script=fs.readFileSync(option("--script"),"utf8"),rows=[];
  const browser=await chromium.launch({headless:true});
  try {
    for(const language of ["English","Русский","Українська"])
      for(const mode of ["normal","get","property","set","both"])
        rows.push(await scenario(browser,script,mode,language));
  }finally{await browser.close()}
  const report={status:rows.every(r=>r.pass)?"PASS":"FAIL",rows,
    device_contact:false,settings_changed:false,fixture_only:true};
  if(option("--output"))fs.writeFileSync(option("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  assert.equal(report.status,"PASS","F-W9 optional storage failure breaks app or navigation");
  console.log("FW9_STORAGE_BROWSER=PASS");
}
main().catch(e=>{console.error(e);process.exitCode=1;});
