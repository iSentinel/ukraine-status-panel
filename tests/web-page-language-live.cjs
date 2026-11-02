/** Read live page language; authentication arrives only on standard input. */
const fs=require("node:fs"),path=require("node:path"),assert=require("node:assert/strict");
let config;

/** Enforce local GET-only traffic before the browser can issue a request. */
async function restrict(page,url,counts) {
  const origin=new URL(url).origin;
  await page.route("**/*",route=>{
    const request=route.request();
    if(!["GET","HEAD"].includes(request.method())){
      counts.writes++;return route.abort();
    }
    if(new URL(request.url()).origin!==origin){
      counts.external++;return route.abort();
    }
    counts.reads++;return route.continue();
  });
}

/** Inspect a fresh browser context without clicking or submitting controls. */
async function main() {
  config=JSON.parse(fs.readFileSync(0,"utf8"));
  const {chromium}=require(path.join(config.root,"tools/web-ui/node_modules/playwright"));
  const browser=await chromium.launch({headless:true});
  const counts={reads:0,writes:0,external:0,errors:0};
  let result;
  try {
    const context=await browser.newContext({viewport:{width:390,height:844},
      isMobile:true,httpCredentials:{username:config.username,password:config.password}});
    const page=await context.newPage();
    page.on("pageerror",()=>counts.errors++);
    await restrict(page,config.url,counts);
    await page.addInitScript(denied=>{
      if(denied)Object.defineProperty(window,"localStorage",{
        get(){throw new DOMException("Storage denied for isolated test","SecurityError")}});
      else localStorage.setItem("usp-active-tab","setup");
    },config.deny_storage);
    await page.goto(config.url,{waitUntil:"domcontentloaded",timeout:20000});
    await page.waitForFunction(code=>document.documentElement.lang===code&&
      document.querySelectorAll('[role="tab"]').length===9,config.expected_lang,{timeout:20000});
    await page.waitForTimeout(250);
    result=await page.evaluate(()=>({page_language:document.documentElement.lang,
      tabs:document.querySelectorAll('[role="tab"]').length,
      viewport_width:document.documentElement.clientWidth}));
  } finally {await browser.close()}
  assert.equal(result.page_language,config.expected_lang);
  assert.equal(counts.writes,0);assert.equal(counts.external,0);assert.equal(counts.errors,0);
  Object.assign(result,{status:"PASS",read_requests:counts.reads,
    blocked_writes:counts.writes,blocked_external_requests:counts.external,
    runtime_errors:counts.errors,storage_denied:!!config.deny_storage,
    device_mutations_performed:false});
  process.stdout.write(JSON.stringify(result)+"\n");
}
main().catch(error=>{
  let message=String(error.message||error);
  if(config?.password)message=message.split(config.password).join("[redacted]");
  process.stderr.write(message+"\n");process.exitCode=1;
});
