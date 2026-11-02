/** R10 real-browser coverage: bounds, keyboard traversal and semantic contrast. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require(process.env.USP_PLAYWRIGHT_MODULE ||
  path.resolve(__dirname, "../tools/web-ui/node_modules/playwright"));

const VIEWPORTS = [320, 375, 768, 1024];
const LANGUAGES = ["English", "Русский", "Українська"];
const TAB_LABELS = {
  English:["Setup","Overview","Location","Hardware","Power","Network","Shelly","Starlink","Spotify","Maintenance"],
  "Русский":["Старт","Обзор","Локация","Оборудование","Свет","Сеть","Shelly","Starlink","Spotify","Обслуживание"],
  "Українська":["Старт","Огляд","Локація","Обладнання","Світло","Мережа","Shelly","Starlink","Spotify","Обслуговування"],
};

async function installEvents(page) {
  await page.addInitScript(() => {
    localStorage.setItem("usp-active-tab", "overview");
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.fixtureSource = this; }
      emit(type, value) { this.dispatchEvent(new MessageEvent(type,{data:JSON.stringify(value)})); }
    };
  });
}

async function routes(page, script) {
  await page.route("**/*", async route => {
    const url = new URL(route.request().url());
    if (url.pathname === "/") return route.fulfill({contentType:"text/html",
      body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/app.css"><esp-app></esp-app><script src="/app.js"></script>'});
    if (url.pathname === "/app.css") return route.fulfill({contentType:"text/css",
      body:fs.readFileSync(path.resolve(__dirname,"../firmware/webui/usp-web-ui.css"),"utf8")});
    if (url.pathname === "/app.js") return route.fulfill({contentType:"text/javascript",body:script});
    if (["/api/streets","/api/houses"].includes(url.pathname)) return route.fulfill({json:[]});
    if (url.pathname === "/api/group") return route.fulfill({json:{ok:false}});
    if (url.pathname === "/usp/spotify/auth/status") return route.fulfill({json:{ok:true,state:"connected",exchange_available:true,language:"en",session_storage:"ready"}});
    if (/^\/(text|button|select|switch)\//.test(url.pathname)) return route.fulfill({status:200,body:"{}"});
    throw new Error("Unexpected R10 fixture request: "+url.href);
  });
}

async function seed(page) {
  await page.waitForFunction(() => Boolean(window.fixtureSource));
  await page.evaluate(() => {
    fixtureSource.emit("ping", {title:"R10 panel", uptime:10});
    const entities = [
      {id:"setup-status",name:"Setup Status",domain:"text_sensor",sorting_group:"Setup",state:"INCOMPLETE"},
      {id:"language",name:"UI Language",domain:"select",sorting_group:"Panel settings",value:"English",option:["English","Русский","Українська"]},
      {id:"firmware",name:"Firmware Version",domain:"text_sensor",sorting_group:"Status & diagnostics",state:"0.115.293-dev"},
      {id:"panel-ip",name:"Panel IP",domain:"text_sensor",sorting_group:"Status & diagnostics",state:"192.0.2.208"},
      {id:"weather-location",name:"Weather Location",domain:"text_sensor",sorting_group:"Location",state:"Kyiv, UA"},
      {id:"weather-search",name:"Weather City Search",domain:"text",sorting_group:"Location",state:"",min_length:1,max_length:64},
      {id:"refresh",name:"Refresh Weather",domain:"button",sorting_group:"Location"},
      {id:"sht-status",name:"SHT4x Status",domain:"text_sensor",sorting_group:"Hardware",state:"NOT DETECTED"},
      {id:"temp",name:"Environment Temperature",domain:"sensor",sorting_group:"Hardware",state:"—"},
      {id:"buzzer",name:"Buzzer Test",domain:"button",sorting_group:"Hardware"},
      {id:"sv-enabled",name:"SvitloBot Enabled",domain:"switch",sorting_group:"SvitloBot",state:"OFF"},
      {id:"group",name:"Outage Group Code",domain:"text",sorting_group:"SvitloBot",state:"10.1",min_length:1,max_length:8},
      {id:"sv-status",name:"SvitloBot Status",domain:"text_sensor",sorting_group:"SvitloBot",state:"NO DATA"},
      {id:"ipv4",name:"Network IPv4 Mode",domain:"select",sorting_group:"Network",value:"DHCP",option:["DHCP","Static"]},
      {id:"ssid",name:"Network SSID",domain:"text",sorting_group:"Network",state:"Example network with a long readable SSID",min_length:1,max_length:64},
      {id:"net-status",name:"Network Profile Status",domain:"text_sensor",sorting_group:"Network",state:"LIVE"},
      {id:"apply-net",name:"Apply Network Settings",domain:"button",sorting_group:"Network"},
      {id:"sh-enabled",name:"Shelly Enabled",domain:"switch",sorting_group:"Shelly",state:"ON"},
      {id:"sh-host",name:"Shelly Host",domain:"text",sorting_group:"Shelly",state:"192.0.2.179",min_length:1,max_length:255},
      {id:"sh-status",name:"Shelly Status",domain:"text_sensor",sorting_group:"Shelly",state:"CONNECTED"},
      {id:"sh-v0",name:"Shelly CH0 Voltage",domain:"sensor",sorting_group:"Shelly",state:"230.1",uom:"V"},
      {id:"sl-enabled",name:"Starlink Enabled",domain:"switch",sorting_group:"Starlink",state:"OFF"},
      {id:"sl-host",name:"Starlink Host",domain:"text",sorting_group:"Starlink",state:"192.168.100.1",min_length:1,max_length:255},
      {id:"sl-status",name:"Starlink Status",domain:"text_sensor",sorting_group:"Starlink",state:"DISCONNECTED"},
      {id:"sl-probe",name:"Starlink Probe Now",domain:"button",sorting_group:"Starlink"},
      {id:"security",name:"Web Security Status",domain:"text_sensor",sorting_group:"Maintenance",state:"FACTORY"},
      {id:"password",name:"New Web Password",domain:"text",sorting_group:"Maintenance",mode:1,state:"",min_length:0,max_length:64},
      {id:"restart",name:"Restart",domain:"button",sorting_group:"Maintenance"},
    ];
    for (const entity of entities) fixtureSource.emit("state", entity);
  });
  await page.locator('[data-entity-id="firmware"]').waitFor();
}

async function setLanguage(page, language) {
  await page.evaluate(value => fixtureSource.emit("state", {
    id:"language",name:"UI Language",domain:"select",sorting_group:"Panel settings",
    value,option:["English","Русский","Українська"],
  }), language);
  await page.getByRole("tab", {name:TAB_LABELS[language][0],exact:true}).waitFor();
}

async function checkGeometry(page, language, width, tabLabel) {
  await page.setViewportSize({width,height:900});
  const tab = page.getByRole("tab", {name:tabLabel,exact:true});
  await tab.click();
  await page.waitForFunction(label => {
    const active=document.querySelector('[role="tab"][aria-selected="true"]');
    return active?.textContent === label && document.querySelector('[role="tabpanel"]')?.getAttribute('aria-labelledby') === active.id;
  }, tabLabel);
  const result = await page.evaluate(() => {
    const visible = el => {
      const s=getComputedStyle(el), r=el.getBoundingClientRect();
      return s.display!=="none" && s.visibility!=="hidden" && r.width>0 && r.height>0;
    };
    const panel=document.querySelector('[role="tabpanel"]');
    const active=document.querySelector('[role="tab"][aria-selected="true"]');
    const nav=document.querySelector('[role="tablist"]');
    if(!panel||!active||!nav) return {fatal:"missing tab structure"};
    const vr={left:0,right:innerWidth};
    const badBounds=[];
    for(const el of panel.querySelectorAll('.usp-card,.usp-note,.usp-finder-card,[data-entity-id],button,input,select,summary')) {
      if(!visible(el)) continue;
      const r=el.getBoundingClientRect();
      if(r.left < vr.left-1 || r.right > vr.right+1) badBounds.push(`${el.tagName}.${el.className}:${r.left.toFixed(1)}..${r.right.toFixed(1)}`);
    }
    const badTargets=[];
    for(const el of panel.querySelectorAll('button,input,select,summary')) {
      if(!visible(el)) continue;
      const r=el.getBoundingClientRect();
      if(r.height < 43.5) badTargets.push(`${el.tagName}.${el.className}:${r.height.toFixed(1)}`);
    }
    const badClip=[];
    for(const el of panel.querySelectorAll('.usp-card,.usp-note,.usp-name,.usp-value,.usp-muted,.usp-pill,h2')) {
      if(!visible(el)) continue;
      if(el.scrollWidth > el.clientWidth+1) badClip.push(`${el.tagName}.${el.className}:${el.scrollWidth}>${el.clientWidth}`);
    }
    const ar=active.getBoundingClientRect(), nr=nav.getBoundingClientRect();
    return {
      docOverflow:document.documentElement.scrollWidth>document.documentElement.clientWidth+1,
      panelRight:panel.getBoundingClientRect().right,
      badBounds,badTargets,badClip,
      activeRevealed:ar.left>=nr.left-1 && ar.right<=nr.right+1,
    };
  });
  assert.equal(result.fatal, undefined, `${language}/${width}/${tabLabel}: ${result.fatal}`);
  assert.equal(result.docOverflow,false,`${language}/${width}/${tabLabel}: document overflow`);
  assert.ok(result.panelRight<=width+1,`${language}/${width}/${tabLabel}: panel outside viewport`);
  assert.deepEqual(result.badBounds,[],`${language}/${width}/${tabLabel}: bounds ${result.badBounds.join(" | ")}`);
  assert.deepEqual(result.badTargets,[],`${language}/${width}/${tabLabel}: targets ${result.badTargets.join(" | ")}`);
  assert.deepEqual(result.badClip,[],`${language}/${width}/${tabLabel}: clipping ${result.badClip.join(" | ")}`);
  assert.equal(result.activeRevealed,true,`${language}/${width}/${tabLabel}: active tab not revealed`);
}

async function checkKeyboardTraversal(page, language, tabLabel) {
  await page.setViewportSize({width:375,height:900});
  const tab=page.getByRole("tab",{name:tabLabel,exact:true});
  await tab.click();
  await page.waitForFunction(label => {
    const active=document.querySelector('[role="tab"][aria-selected="true"]');
    return active?.textContent === label && document.querySelector('[role="tabpanel"]')?.getAttribute('aria-labelledby') === active.id;
  }, tabLabel);
  await page.getByRole("tab",{name:tabLabel,exact:true}).focus();
  const hasControl=await page.evaluate(() => [...document.querySelectorAll('[role="tabpanel"] button,[role="tabpanel"] input,[role="tabpanel"] select,[role="tabpanel"] summary')]
    .some(el => el.tabIndex>=0 && getComputedStyle(el).display!=="none" && el.getBoundingClientRect().height>0));
  if(!hasControl) return;
  await page.keyboard.press("Tab");
  const state=await page.evaluate(() => {
    const el=document.activeElement, panel=document.querySelector('[role="tabpanel"]');
    if(!el||!panel) return {inside:false,visible:false,height:0};
    const r=el.getBoundingClientRect(), s=getComputedStyle(el);
    return {inside:panel.contains(el),visible:s.display!=="none"&&s.visibility!=="hidden"&&r.width>0&&r.height>0,height:r.height};
  });
  assert.equal(state.inside,true,`${language}/${tabLabel}: Tab did not enter active panel`);
  assert.equal(state.visible,true,`${language}/${tabLabel}: Tab landed on hidden control`);
  assert.ok(state.height>=43.5,`${language}/${tabLabel}: focused target below 44px`);
}

async function checkContrastMatrix(page) {
  const report=await page.evaluate(() => {
    const host=document.createElement('div'); host.className='usp-card';
    host.style.cssText='position:absolute;left:0;top:0;width:300px;visibility:hidden;z-index:-1';
    const specs=[
      ['name','div','usp-name'],['value','div','usp-value'],['muted','div','usp-muted'],
      ['pill','span','usp-pill'],['good','span','usp-pill good'],['warn','span','usp-pill warn'],
      ['bad','span','usp-pill bad'],['quiet','span','usp-pill quiet'],['note','div','usp-note'],
      ['button','button',''],['primary','button','usp-primary'],['tab','button','usp-tab'],
      ['activeTab','button','usp-tab active'],['error','div','usp-error show'],
      ['suggestion','button','usp-suggestion'],
    ];
    const nodes=[];
    for(const [name,tag,cls] of specs){const el=document.createElement(tag);el.className=cls;el.textContent='Readable sample';host.append(el);nodes.push([name,el]);}
    document.body.append(host);
    const rgba=value=>{const m=value.match(/rgba?\(([^)]+)\)/);if(!m)return null;const p=m[1].split(',').map(Number);return [p[0],p[1],p[2],p.length>3?p[3]:1];};
    const blend=(fg,bg)=>{const a=fg[3],ia=1-a;return [fg[0]*a+bg[0]*ia,fg[1]*a+bg[1]*ia,fg[2]*a+bg[2]*ia,1];};
    const bg=el=>{let out=[0,0,0,0];for(let cur=el;cur;cur=cur.parentElement){const c=rgba(getComputedStyle(cur).backgroundColor);if(c&&c[3]>0){out=out[3]===0?c:blend(out,c);if(out[3]>=.999)break;}}return out[3]===0?[15,23,42,1]:out;};
    const lum=c=>{const v=c.slice(0,3).map(x=>x/255).map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4);return .2126*v[0]+.7152*v[1]+.0722*v[2];};
    const ratio=(a,b)=>{const [hi,lo]=[lum(a),lum(b)].sort((x,y)=>y-x);return (hi+.05)/(lo+.05);};
    const values=nodes.map(([name,el])=>{const fg=rgba(getComputedStyle(el).color),back=bg(el);return {name,ratio:ratio(fg,back)};});
    host.remove(); return values;
  });
  const bad=report.filter(item=>item.ratio<4.5).map(item=>`${item.name}:${item.ratio.toFixed(2)}`);
  assert.deepEqual(bad,[],`semantic contrast below WCAG AA: ${bad.join(" | ")}`);
}

async function main(){
  if(process.argv.includes("--help")){console.log("Usage: node web-ui-r10-browser.cjs --script path/to/Web-UI.js");return;}
  const index=process.argv.indexOf("--script");
  if(index<0||!process.argv[index+1]) throw new Error("--script is required");
  const script=fs.readFileSync(process.argv[index+1],"utf8");
  const browser=await chromium.launch({headless:true});
  try{
    const page=await browser.newPage(); page.setDefaultTimeout(10000);
    const errors=[]; page.on("pageerror",e=>errors.push(e.message));
    await installEvents(page); await routes(page,script); await page.goto("http://usp.test/"); await seed(page);
    let cases=0;
    for(const language of LANGUAGES){
      await setLanguage(page,language);
      for(const width of VIEWPORTS) for(const tab of TAB_LABELS[language]){await checkGeometry(page,language,width,tab);cases++;}
      for(const tab of TAB_LABELS[language]) await checkKeyboardTraversal(page,language,tab);
    }
    await checkContrastMatrix(page);
    assert.equal(cases,120); assert.deepEqual(errors,[]);
    console.log(`R10_BROWSER=PASS cases=${cases} tabs=10 languages=3 viewports=4 bounds=1 touch44=1 keyboard=1 contrast=AA`);
  }finally{await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
