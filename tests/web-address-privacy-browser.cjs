/** Require explicit in-memory consent before any Kyiv address leaves the browser. */
const fs=require("node:fs"),assert=require("node:assert/strict"),path=require("node:path");
const {chromium}=require(process.env.USP_PLAYWRIGHT_MODULE||path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const args=process.argv.slice(2),opt=n=>{const i=args.indexOf(n);return i<0?undefined:args[i+1]};
const LANGUAGES=["English","Русский","Українська"];

async function install(page,script,calls){
  await page.addInitScript(()=>{
    localStorage.setItem("usp-active-tab","svitlo");
    window.EventSource=class extends EventTarget{constructor(){super();window.fixtureSource=this}emit(v){this.dispatchEvent(new MessageEvent("state",{data:JSON.stringify(v)}))}};
  });
  await page.route("**/*",route=>{const u=new URL(route.request().url());
    if(u.pathname==="/")return route.fulfill({contentType:"text/html",body:'<!doctype html><meta charset=utf-8><esp-app></esp-app><script type=module src=/app.js></script>'});
    if(u.pathname==="/app.js")return route.fulfill({contentType:"text/javascript",body:script});
    if(u.hostname==="getnubergroupproxy.onrender.com"){
      calls.push(u.href);
      if(u.pathname==="/api/streets")return route.fulfill({json:[{name:"Main avenue",id:1}]});
      if(u.pathname==="/api/houses")return route.fulfill({json:[{name:"13"}]});
      if(u.pathname==="/api/group")return route.fulfill({json:{ok:true,group:10,subgroup:1}});
    }
    if(route.request().method()==="POST")return route.fulfill({status:200,body:""});
    throw Error("Unexpected request "+u.href);
  });
  await page.goto("http://fw11.test/");
}

async function publish(page,language){
  await page.evaluate(language=>{
    fixtureSource.emit({id:"lang",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:language,option:["English","Русский","Українська"]});
    fixtureSource.emit({id:"group",name:"Outage Group Code",domain:"text",sorting_group:"SvitloBot",state:"10.1"});
  },language);
  await page.locator(".usp-finder").waitFor();
}

async function scenario(browser,script,language){
  const page=await browser.newPage(),calls=[],errors=[];page.on("pageerror",e=>errors.push(String(e)));
  await install(page,script,calls);await publish(page,language);
  let consent=page.locator(".usp-external-consent"),fields=page.locator(".usp-finder input");
  const consentBefore=await consent.count(),fieldsBefore=await fields.count();
  if(fieldsBefore){await fields.first().fill("Main");await page.waitForTimeout(400)}
  const requestsBeforeConsent=calls.length;
  let requestAfterConsent=false,reloadRequiresConsent=false;
  if(consentBefore===1){
    await consent.click();await fields.first().waitFor();await fields.first().fill("Main");
    await page.getByRole("button",{name:"Main avenue",exact:true}).waitFor();
    requestAfterConsent=calls.length>requestsBeforeConsent;
    await page.reload();await publish(page,language);
    reloadRequiresConsent=await page.locator(".usp-external-consent").count()===1&&await page.locator(".usp-finder input").count()===0;
  }
  const result={language,consent_before:consentBefore,fields_before:fieldsBefore,requests_before_consent:requestsBeforeConsent,request_after_consent:requestAfterConsent,reload_requires_consent:reloadRequiresConsent,page_errors:errors};
  result.pass=consentBefore===1&&fieldsBefore===0&&requestsBeforeConsent===0&&requestAfterConsent&&reloadRequiresConsent&&errors.length===0;
  await page.close();return result;
}

async function main(){
  if(args.includes("--help")){console.log("node tests/web-address-privacy-browser.cjs --script FILE [--output FILE] [--expect-fail]");return}
  assert.ok(opt("--script"),"--script is required");const script=fs.readFileSync(opt("--script"),"utf8"),rows=[];
  const browser=await chromium.launch({headless:true});try{for(const language of LANGUAGES)rows.push(await scenario(browser,script,language))}finally{await browser.close()}
  const report={status:rows.every(r=>r.pass)?"PASS":"FAIL",rows,proxy:"getnubergroupproxy.onrender.com",consent_persistence:"page-memory-only",device_contact:false,settings_changed:false};
  if(opt("--output"))fs.writeFileSync(opt("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  if(args.includes("--expect-fail")){assert.equal(report.status,"FAIL");assert.ok(rows.some(r=>r.requests_before_consent>0||r.consent_before===0));console.log("FW11_ADDRESS_PRIVACY_BEFORE=CONFIRMED languages=3");return}
  assert.equal(report.status,"PASS","address left browser before explicit consent");
  console.log("FW11_ADDRESS_PRIVACY=PASS languages=3 no_preconsent_requests=1 reload_reconsent=1");
}
main().catch(e=>{console.error(e);process.exitCode=1});
