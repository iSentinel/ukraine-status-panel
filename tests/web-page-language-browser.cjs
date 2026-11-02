/** Check actual UI field labels and page language with isolated source events. */
const fs=require("node:fs"),assert=require("node:assert/strict");
const path=require("node:path");
const {chromium}=require(process.env.USP_PLAYWRIGHT_MODULE||path.resolve(__dirname,"../tools/web-ui/node_modules/playwright"));
const args=process.argv.slice(2),option=n=>{const index=args.indexOf(n);return index<0?undefined:args[index+1]};

/** Serve only the actual app source and prevent requests to any panel. */
async function install(page,script,language) {
  await page.addInitScript(()=>{
    localStorage.setItem("usp-active-tab","hardware");
    window.EventSource=class extends EventTarget {
      constructor(){super();window.fixtureSource=this}
      emit(value){this.dispatchEvent(new MessageEvent("state",{data:JSON.stringify(value)}))}
    };
  });
  await page.route("**/*",r=>{
    const u=new URL(r.request().url());
    if(u.pathname==="/")return r.fulfill({contentType:"text/html",body:"<!doctype html><esp-app></esp-app><script type=module src=/app.js></script>"});
    if(u.pathname==="/app.js")return r.fulfill({contentType:"text/javascript",body:script});
    throw Error("Unexpected request; no writes permitted: "+u.href);
  });
  await page.goto("http://accessibility-fixture.test/");
  await page.evaluate(language=>{
    fixtureSource.emit({id:"lang",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:language,option:["English","Русский","Українська"]});
    const group={sorting_group:"Hardware"};
    for(const row of [
      {id:"host",name:"Starlink Host",domain:"text",state:"fixture.invalid",min_length:1,max_length:255},
      {id:"port",name:"Starlink Port",domain:"number",value:9200,min_value:1,max_value:65535,step:1},
      {id:"secret",name:"Optional secret",domain:"text",mode:1,state:"",min_length:0,max_length:64},
      {id:"light",name:"LCD Backlight",domain:"light",brightness:128},
      {id:"palette",name:"Alert Palette",domain:"select",value:"Standard",option:["Standard","Accessible","Monochrome"]},
    ])fixtureSource.emit({...group,...row});
  },language);
}

/** Ask Chromium to resolve the accessible name from the visible field heading. */
async function scenario(browser,script,language,expectedLang) {
  const page=await browser.newPage();
  await install(page,script,language);
  const rows=[];
  for(const id of ["host","port","secret","light","palette"]){
    const card=page.locator('[data-entity-id="'+id+'"]');
    await card.waitFor();
    const label=await card.locator(".usp-name").innerText();
    rows.push({id,label,associated_controls:await card.getByLabel(label,{exact:true}).count()});
  }
  const htmlLang=await page.locator("html").getAttribute("lang")||"";
  await page.close();
  return {language,expected_language:expectedLang,html_language:htmlLang,fields:rows,
    pass:htmlLang===expectedLang&&rows.every(r=>r.associated_controls===1)};
}

/** Follow source-language updates on the same open page without submitting settings. */
async function languageChanges(browser,script) {
  const page=await browser.newPage(),rows=[];
  await install(page,script,"English");
  for(const [language,code] of [["English","en"],["Русский","ru"],["Українська","uk"],["English","en"]]){
    await page.evaluate(language=>fixtureSource.emit({id:"lang",name:"UI Language",value:language}),language);
    await page.waitForFunction(code=>document.documentElement.lang===code,code);
    rows.push({language,html_language:await page.locator("html").getAttribute("lang")});
  }
  await page.close();
  return rows;
}

/** Save the baseline deficiencies before failing this read-only browser check. */
async function main() {
  if(args.includes("--help")){console.log("node tests/web-page-language-browser.cjs --script FILE [--output NEW.json]");return}
  assert.ok(option("--script"),"--script is required");
  const script=fs.readFileSync(option("--script"),"utf8"),rows=[];
  const browser=await chromium.launch({headless:true});
  let changes=[];
  try {
    for(const [language,code] of [["English","en"],["Русский","ru"],["Українська","uk"]])
      rows.push(await scenario(browser,script,language,code));
    changes=await languageChanges(browser,script);
  }finally{await browser.close()}
  const report={status:rows.every(r=>r.pass)?"PASS":"FAIL",rows,
    changes,device_contact:false,settings_changed:false,fixture_only:true};
  if(option("--output"))fs.writeFileSync(option("--output"),JSON.stringify(report,null,2)+"\n",{flag:"wx"});
  assert.equal(report.status,"PASS","F-W7 field accessible names or page language missing");
  console.log("FW7_PAGE_LANGUAGE_BROWSER=PASS");
}
main().catch(e=>{console.error(e);process.exitCode=1;});
