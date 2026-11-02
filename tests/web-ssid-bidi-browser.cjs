/** Keep untrusted SSID bidi controls out of visible text without changing values. */
const fs=require("node:fs"),assert=require("node:assert/strict"),path=require("node:path");
const {chromium}=require(path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const args=process.argv.slice(2),opt=n=>{const i=args.indexOf(n);return i<0?undefined:args[i+1]};
const BAD=/[\u061C\u200E\u200F\u202A-\u202E\u2066-\u2069]/u;
const SSID="Cafe\u202E123\u2066RTL\u2069";
const LANGUAGES=[
  {value:"English",setup:"Setup",overview:"Overview",network:"Network"},
  {value:"Русский",setup:"Старт",overview:"Обзор",network:"Сеть"},
  {value:"Українська",setup:"Старт",overview:"Огляд",network:"Мережа"},
];

async function install(page,script){
  await page.addInitScript(()=>{window.EventSource=class extends EventTarget{
    constructor(){super();window.fixtureSource=this}
    emit(v){this.dispatchEvent(new MessageEvent("state",{data:JSON.stringify(v)}))}
  }});
  const posts=[];page.on("request",r=>{if(r.method()==="POST")posts.push(r.url())});
  await page.route("**/*",r=>{const u=new URL(r.request().url());
    if(u.pathname==="/")return r.fulfill({contentType:"text/html",body:"<!doctype html><esp-app></esp-app><script type=module src=/app.js></script>"});
    if(u.pathname==="/app.js")return r.fulfill({contentType:"text/javascript",body:script});
    if(u.pathname==="/usp/network/scan")return r.fulfill({contentType:"application/json",body:JSON.stringify({state:"ready",networks:[{ssid:SSID,rssi:-42,secure:true}]})});
    if(r.request().method()==="POST")return r.fulfill({status:200,body:""});
    throw Error("unexpected request "+u.href);
  });
  await page.goto("http://fw10.test/");return posts;
}

async function scenario(browser,script,language){
  const page=await browser.newPage(),errors=[];page.on("pageerror",e=>errors.push(String(e)));
  const posts=await install(page,script);
  await page.evaluate(([ssid,lang])=>{
    fixtureSource.emit({id:"lang",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:lang,option:["English","Русский","Українська"]});
    fixtureSource.emit({id:"wifi-info",name:"Wi-Fi SSID",domain:"text_sensor",sorting_group:"Status & diagnostics",state:ssid});
    fixtureSource.emit({id:"network-ssid",name:"Network SSID",domain:"text",sorting_group:"Network",state:"Current",min_length:1,max_length:32});
  },[SSID,language.value]);
  await page.waitForTimeout(150);
  await page.getByRole("tab",{name:language.setup,exact:true}).click();await page.waitForTimeout(40);
  const setup=await page.locator("#usp-tabpanel").textContent();
  await page.getByRole("tab",{name:language.overview,exact:true}).click();await page.waitForTimeout(40);
  const status=await page.locator("[data-entity-id=wifi-info] .usp-value").textContent();
  await page.getByRole("tab",{name:language.network,exact:true}).click();await page.waitForTimeout(40);
  await page.locator("[data-entity-id=network-ssid] button.usp-primary").click();await page.waitForTimeout(40);
  const select=page.locator("[data-entity-id=network-ssid] select"),option=select.locator("option").nth(1);
  const optionText=await option.textContent(),raw=await option.getAttribute("value");
  await select.selectOption({value:SSID});await page.waitForTimeout(40);
  const posted=posts.find(x=>x.includes("Network%20SSID")||x.includes("Network+SSID"))||"";
  const postValue=posted?new URL(posted).searchParams.get("value"):null;
  const visible=[setup||"",status||"",optionText||""];
  const result={language:language.value,visible_has_bidi:visible.some(v=>BAD.test(v)),display_changed:visible.every(v=>!v.includes(SSID)),raw_value_exact:raw===SSID,post_value_exact:postValue===SSID,page_errors:errors};
  result.pass=!result.visible_has_bidi&&result.display_changed&&result.raw_value_exact&&result.post_value_exact&&!errors.length;
  await page.close();return result;
}

async function main(){
  if(args.includes("--help")){console.log("node tests/web-ssid-bidi-browser.cjs --script FILE [--output FILE] [--expect-fail]");return}
  assert.ok(opt("--script"),"--script is required");
  const script=fs.readFileSync(opt("--script"),"utf8"),browser=await chromium.launch({headless:true}),rows=[];
  try{for(const language of LANGUAGES)rows.push(await scenario(browser,script,language))}finally{await browser.close()}
  const report={status:rows.every(r=>r.pass)?"PASS":"FAIL",rows,ssid_codepoints:[...SSID].map(c=>c.codePointAt(0).toString(16)),device_contact:false,settings_changed:false};
  if(opt("--output"))fs.writeFileSync(opt("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  if(args.includes("--expect-fail")){assert.equal(report.status,"FAIL");console.log("FW10_SSID_BIDI_BEFORE=CONFIRMED languages=3");return}
  assert.equal(report.status,"PASS","SSID bidi controls remain in visible UI");
  console.log("FW10_SSID_BIDI=PASS languages=3 raw_value_preserved=1 post_value_preserved=1");
}
main().catch(e=>{console.error(e);process.exitCode=1});
