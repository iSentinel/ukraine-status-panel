/** Browser model: only loopback HTTP and intercepted provider/relay fixtures. */
const fs=require("node:fs"),path=require("node:path"),assert=require("node:assert/strict");
const {chromium}=require(path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const ROOT=path.resolve(__dirname,"..");
const args=process.argv.slice(2),outIndex=args.indexOf("--output");
const OUTPUT=outIndex<0?null:args[outIndex+1];
const zlib=require("node:zlib"),http=require("node:http");
const BASE="/usp/spotify/auth/";
const STATE="MTExMTExMTExMTExMTExMQ",REDIRECT="https://return.test/cb";
const result={type:"usp-spotify-callback",state:STATE,code:"fixture-code"};
function authorize(client,redirect) {
  const p=new URLSearchParams({response_type:"code",client_id:client,redirect_uri:redirect,
    state:STATE,code_challenge:"a".repeat(43),code_challenge_method:"S256"});
  return "https://accounts.spotify.com/authorize?"+p;
}
async function fixture(browser,{language="en",width=390,available=true,state="idle",application=null}={}) {
  const context=await browser.newContext({viewport:{width,height:800},locale:language+"-UA"});
  const f={context,language,state,available,application,posts:[],errors:[],unexpected:[],mode:"normal"};
  await startServer(f);
  context.on("page",page=>page.on("pageerror",e=>f.errors.push(String(e))));
  await context.route("**/*",r=>new URL(r.request().url()).origin===f.origin?r.continue():serve(f,r));
  f.page=await context.newPage();
  await f.page.clock.install();
  await f.page.goto(f.origin+BASE);
  await f.page.waitForFunction(expected=>document.getElementById("status").dataset.state===expected,state);
  return f;
}
/** Real loopback HTTP lets Chromium decode actual gzip transfer bytes. */
async function startServer(f) {
  f.server=http.createServer((req,res)=>{
    const chunks=[];
    req.on("data",chunk=>chunks.push(chunk));
    req.on("end",()=>{
      const route={
        request:()=>({url:()=>f.origin+req.url,method:()=>req.method,headers:()=>req.headers,
          postData:()=>Buffer.concat(chunks).toString("utf8")}),
        abort:async()=>{
          // A processed request loses its response body after headers arrive.
          res.writeHead(200,{"Content-Type":"application/json","Content-Length":"64"});
          res.flushHeaders();res.write("{",()=>res.destroy());
        },
        fulfill:async options=>{
          const headers={...options.headers};
          if(options.contentType)headers["Content-Type"]=options.contentType;
          if(options.json)headers["Content-Type"]="application/json";
          const body=options.json?JSON.stringify(options.json):options.body;
          res.writeHead(options.status||200,headers);res.end(body);
        }
      };
      Promise.resolve(serve(f,route)).catch(error=>{
        f.errors.push(String(error));res.destroy();
      });
    });
  });
  await new Promise(resolve=>f.server.listen(0,"127.0.0.1",resolve));
  f.origin="http://127.0.0.1:"+f.server.address().port;
}

async function serve(f,route) {
  const req=route.request(),u=new URL(req.url());
  if(u.origin===f.origin && u.pathname.startsWith(BASE)) {
    const file=u.pathname.slice(BASE.length)||"index.html";
    if(["status","begin","callback","cancel"].includes(file))return api(f,route,file);
    if(["index.html","page.js","page.css","return.js"].includes(file))
      return route.fulfill({contentType:file.endsWith(".js")?"text/javascript":file.endsWith(".css")?"text/css":"text/html",
        headers:{"Content-Encoding":"gzip","Cache-Control":"no-store","Referrer-Policy":"no-referrer",
          "Content-Security-Policy":"default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"},
        body:zlib.gzipSync(fs.readFileSync(file==="return.js"?
          path.join(ROOT,"firmware/webui/usp-spotify-auth-return.js"):path.join(ROOT,"firmware/webui/spotify-auth",file)))});
  }
  if(u.origin==="https://accounts.spotify.com" && u.pathname==="/authorize")
    return route.fulfill({contentType:"text/html",body:"<!doctype html><title>Fixture provider</title>"});
  if(u.origin==="https://return.test") {
    const file=u.pathname==="/cb"?"index.html":path.basename(u.pathname);
    if(["index.html","relay.js","relay.css"].includes(file))
      return route.fulfill({contentType:file.endsWith(".js")?"text/javascript":file.endsWith(".css")?"text/css":"text/html",
        body:fs.readFileSync(path.join(ROOT,"web-installer/spotify-callback",file),"utf8")});
  }
  if(u.origin===f.origin && u.pathname==="/")
    return route.fulfill({contentType:"text/html",body:"<!doctype html><title>Panel fixture</title>"});
  f.unexpected.push(u.origin+u.pathname);return route.abort();
}
async function api(f,route,name) {
  const req=route.request(),send=data=>route.fulfill({json:data});
  if(name==="status") {
    if(f.holdStatus) {
      f.held=()=>send({ok:true,state:f.state,exchange_available:f.available,language:f.language,application:f.application});
      return;
    }
    return send({ok:true,state:f.state,exchange_available:f.available,language:f.language,application:f.application});
  }
  assert.equal(req.method(),"POST");
  assert.match(req.headers()["content-type"],/^text\/plain/);
  const fields=Object.fromEntries(new URLSearchParams(req.postData()||""));
  f.posts.push({name,fields});
  if(name==="begin") {
    f.state="awaiting_user";
    if(f.mode==="lost-begin")return route.abort();
    return send({ok:true,authorize_url:f.mode==="bad-url"?"https://foreign.test/authorize":authorize(fields.client_id,fields.redirect_uri)});
  }
  if(name==="cancel") {f.state="idle";return send({ok:true,state:"idle"});}
  if(fields.state!==STATE)return route.fulfill({status:400,json:{ok:false,error:"callback_rejected"}});
  f.state=fields.error?"idle":"code_pending";
  if(f.mode==="lost-callback")return route.abort();
  return send({ok:true,state:fields.error?"denied":"code_pending"});
}
async function fill(f) {
  await f.page.locator("#client").fill("fixtureClient");
  await f.page.locator("#redirect").fill(REDIRECT);
}
async function navigatePopup(popup,url) {
  try {await popup.evaluate(value=>{location.href=value;},url);}
  catch(error) {
    if(!String(error).includes("Execution context was destroyed"))throw error;
  }
}
async function begin(f) {
  await fill(f);
  const pending=f.context.waitForEvent("page");
  await f.page.locator("#connect").click();
  const popup=await pending;
  await popup.waitForURL(u=>u.origin==="https://accounts.spotify.com");
  await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="awaiting_user");
  return popup;
}
async function poll(f) {
  await f.page.clock.runFor(3100);
  await f.page.waitForFunction(state=>document.getElementById("status").dataset.state===state,f.state);
}
async function finish(f) {
  assert.deepEqual(f.errors,[]);assert.deepEqual(f.unexpected,[]);
  assert.deepEqual(await f.page.evaluate(()=>Object.keys(localStorage)),[]);
  assert.deepEqual(await f.page.evaluate(()=>Object.keys(sessionStorage)),[]);
  await f.context.close();
  f.server.closeAllConnections();await new Promise(resolve=>f.server.close(resolve));
}
async function unavailable(browser) {
  const f=await fixture(browser,{available:false});
  try {
    await fill(f);assert.equal(await f.page.locator("#connect").isDisabled(),true);
    await f.page.locator("#configuration").evaluate(el=>el.dispatchEvent(new Event("submit",{bubbles:true,cancelable:true})));
    await f.page.clock.runFor(100);
    assert.equal(f.posts.length,0);
  } finally {await finish(f);}
}
async function popupSuccess(browser,denied=false) {
  const f=await fixture(browser);
  try {
    const popup=await begin(f);
    // Navigate through the original popup so its opener relationship is retained.
    await navigatePopup(popup,REDIRECT+"?state="+STATE+(denied?"&error=access_denied":"&code=fixture-code"));
    await f.page.waitForFunction(state=>document.getElementById("status").dataset.state===state,denied?"idle":"code_pending");
    assert.equal(f.posts.filter(p=>p.name==="callback").length,1);
    assert.equal(new URL(popup.url()).search,"");
    assert.equal(await f.page.locator("#status").getAttribute("data-state")==="connected",false);
    assert.equal(await f.page.locator("#open-login").isVisible(),false);
  } finally {await finish(f);}
}
async function manual(browser) {
  const f=await fixture(browser,{state:"awaiting_user"});
  try {
    for(const value of [null,[],{...result,error:3},{...result,code:3,error:"denied"},
      {...result,state:3},{...result,extra:"x"},{...result,code:""},
      {...result,code:"x".repeat(1025)},{...result,code:"bad\nvalue"}]) {
      await f.page.locator("#manual").fill(JSON.stringify(value));
      await f.page.locator("#submit-result").click();
      await f.page.locator("#error").waitFor({state:"visible"});
      assert.equal(f.posts.length,0);
    }
    await f.page.locator("#manual").fill(JSON.stringify({...result,state:"x".repeat(22)}));
    await f.page.locator("#submit-result").click();
    await f.page.locator("#error").waitFor({state:"visible"});
    assert.equal(f.posts.length,1);
    assert.equal(f.state,"awaiting_user");
    await f.page.locator("#manual").fill(JSON.stringify({...result,code:""}));
    await f.page.locator("#submit-result").click();
    assert.equal(f.posts.length,1);
    await f.page.locator("#manual").fill(JSON.stringify(result));
    await f.page.locator("#submit-result").click();
    await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="code_pending");
    assert.equal(f.posts.length,2);
  } finally {await finish(f);}
}
async function lost(browser,kind) {
  const f=await fixture(browser,{state:kind==="callback"?"awaiting_user":"idle"});
  try {
    f.mode="lost-"+kind;
    if(kind==="begin") {
      await fill(f);await f.page.locator("#connect").click();
    } else {
      await f.page.locator("#manual").fill(JSON.stringify(result));
      await f.page.locator("#submit-result").click();
    }
    await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="unknown");
    assert.equal(await f.page.locator("#connect").isDisabled(),true);
    await poll(f);
    assert.equal(f.posts.filter(p=>p.name===kind).length,1);
    if(kind==="callback")assert.equal(await f.page.locator("#error").isVisible(),false);
    else assert.equal(await f.page.locator("#error").isVisible(),true);
  } finally {await finish(f);}
}
async function staleStatus(browser) {
  const f=await fixture(browser);
  try {
    f.holdStatus=true;
    await f.page.clock.runFor(3100);
    await f.page.waitForTimeout(30);assert.ok(f.held);
    await begin(f);
    // Release a status response issued before begin; it cannot overwrite the mutation.
    f.state="idle";await f.held();f.holdStatus=false;f.state="awaiting_user";
    await f.page.waitForTimeout(30);
    assert.equal(await f.page.locator("#status").getAttribute("data-state"),"awaiting_user");
    await f.page.locator("#cancel").click();
    await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="idle");
    assert.equal(f.posts.filter(p=>p.name==="cancel").length,1);
    await f.page.goto(f.origin+"/");
    assert.equal(f.posts.length,2);
  } finally {await finish(f);}
}
async function badUrl(browser) {
  const f=await fixture(browser);
  try {
    f.mode="bad-url";await fill(f);await f.page.locator("#connect").click();
    await f.page.locator("#error").waitFor({state:"visible"});
    assert.equal(f.posts.length,1);assert.equal(await f.page.locator("#open-login").isVisible(),false);
  } finally {await finish(f);}
}
async function layout(browser,language,width) {
  const f=await fixture(browser,{language,width,available:false});
  try {
    await f.page.locator("#client").fill("keepFocus");
    await f.page.locator("#client").focus();
    f.language=language==="uk"?"ru":"uk";await poll(f);
    await f.page.waitForFunction(language=>document.documentElement.lang===language,f.language);
    assert.equal(await f.page.locator("html").getAttribute("lang"),f.language);
    assert.equal(await f.page.locator("#client").inputValue(),"keepFocus");
    assert.equal(await f.page.locator("#client").evaluate(e=>e===document.activeElement),true);
    const bounds=await f.page.evaluate(()=>({
      overflow:document.documentElement.scrollWidth>innerWidth,
      small:[...document.querySelectorAll("button,input,textarea")].filter(e=>e.getBoundingClientRect().height<48).map(e=>e.id),
      labels:[...document.querySelectorAll("input,textarea")].every(e=>!!document.querySelector('label[for="'+e.id+'"]'))
    }));
    assert.deepEqual(bounds,{overflow:false,small:[],labels:true});
  } finally {await finish(f);}
}
/** A living opener may have navigated away; explicit manual return still works. */
async function navigatedOpener(browser) {
  const f=await fixture(browser);
  try {
    const popup=await begin(f);
    await f.page.goto(f.origin+"/");
    await navigatePopup(popup,REDIRECT+"?state="+STATE+"&code=fixture-code");
    await popup.locator("#manual-return").waitFor({state:"visible"});
    assert.equal(await popup.locator("#manual").isVisible(),false);
    await popup.locator("#manual-return").click();
    const copied=await popup.locator("#result").inputValue();
    assert.deepEqual(JSON.parse(copied),result);
    assert.equal(await popup.locator("#manual-return").isVisible(),false);
    await f.page.goto(f.origin+BASE);
    await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="awaiting_user");
    await f.page.locator("#manual").fill(copied);
    await f.page.locator("#submit-result").click();
    await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="code_pending");
    assert.equal(f.posts.filter(p=>p.name==="callback").length,1);
  } finally {await finish(f);}
}


async function registeredLogin(browser) {
  for (const language of ["uk","ru","en"]) {
    const application={client_id:"registeredPublicApp123",redirect_uri:REDIRECT};
    const f=await fixture(browser,{language,application});
    try {
      assert.equal(await f.page.locator("#application-fields").isVisible(),false);
      assert.equal(await f.page.locator("#client").inputValue(),application.client_id);
      assert.equal(f.posts.length,0);
      const popupWait=f.page.waitForEvent("popup");
      await f.page.locator("#connect").click();
      const popup=await popupWait;
      await popup.waitForURL(u=>u.origin==="https://accounts.spotify.com");
      await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="awaiting_user");
      assert.equal(f.posts.filter(p=>p.name==="begin").length,1);
      assert.deepEqual(f.posts[0].fields,{client_id:application.client_id,redirect_uri:REDIRECT});
      await navigatePopup(popup,REDIRECT+"?state="+STATE+"&code=fixture-code");
      await f.page.waitForFunction(()=>document.getElementById("status").dataset.state==="code_pending");
      assert.equal(f.posts.filter(p=>p.name==="callback").length,1);
    } finally {await finish(f);}
  }
}
async function registeredDraft(browser) {
  const f=await fixture(browser);
  try {
    await f.page.locator("#client").fill("keepDraft");
    f.application={client_id:"registeredPublicApp123",redirect_uri:REDIRECT};
    await poll(f);
    assert.equal(await f.page.locator("#client").inputValue(),"keepDraft");
    assert.equal(await f.page.locator("#client").evaluate(e=>e===document.activeElement),true);
    assert.equal(await f.page.locator("#application-fields").isVisible(),true);
    assert.equal(f.posts.length,0);
  } finally {await finish(f);}
}
async function malformedApplication(browser) {
  for(const application of [{client_id:"bad id",redirect_uri:REDIRECT},
    {client_id:"valid",redirect_uri:"http://192.168.1.2/callback"},
    {client_id:"valid",redirect_uri:"javascript:alert(1)"}]) {
    const f=await fixture(browser,{application});
    try {
      assert.equal(await f.page.locator("#application-fields").isVisible(),true);
      assert.equal(await f.page.locator("#client").inputValue(),"");
      assert.equal(f.posts.length,0);
    } finally {await finish(f);}
  }
}

async function main() {
  const browser=await chromium.launch({headless:true,args:["--no-sandbox"]});
  const completed=[];
  try {
    for(const [name,run] of [["registered-login",()=>registeredLogin(browser)],["registered-draft",()=>registeredDraft(browser)],["malformed-application",()=>malformedApplication(browser)],["unavailable",()=>unavailable(browser)],["popup-code",()=>popupSuccess(browser)],
      ["popup-denial",()=>popupSuccess(browser,true)],["manual",()=>manual(browser)],
      ["lost-begin",()=>lost(browser,"begin")],["lost-callback",()=>lost(browser,"callback")],
      ["stale-status",()=>staleStatus(browser)],["bad-url",()=>badUrl(browser)],["navigated-opener",()=>navigatedOpener(browser)]]) {
      await run();completed.push(name);console.log(name+" PASS");
    }
    for(const language of ["uk","ru","en"])for(const width of [320,390,1024]) {
      await layout(browser,language,width);completed.push(language+"-"+width);
    }
    if(OUTPUT)fs.writeFileSync(OUTPUT,JSON.stringify({status:"PASS",completed,provider_io:0,device_io:0,gzip_assets:true},null,2)+"\n");
    console.log("SPOTIFY_AUTH_PAGE_BROWSER=PASS");
  } finally {await browser.close();}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
