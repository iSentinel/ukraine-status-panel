/** R10 real-browser geometry, keyboard and semantic-contrast audit. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require(path.resolve(__dirname, "../tools/web-ui/node_modules/playwright"));

const ROOT = path.resolve(__dirname, "..");
const CSS = fs.readFileSync(path.join(ROOT, "firmware/webui/usp-web-ui.css"), "utf8");
const LANGUAGES = ["English", "Русский", "Українська"];
const WIDTHS = [320, 375, 390, 768, 1024, 1440];
const TABS = ["setup", "overview", "location", "hardware", "svitlo", "network", "shelly", "starlink", "spotify", "maintenance"];

async function installEvents(page) {
  await page.addInitScript(() => {
    localStorage.setItem("usp-active-tab", "overview");
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.fixtureSource = this; }
      emit(type, value) { this.dispatchEvent(new MessageEvent(type, {data:JSON.stringify(value)})); }
    };
  });
}

async function routeApp(page, script) {
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.hostname !== "usp.test") throw new Error(`External request: ${url.href}`);
    if (url.pathname === "/") return route.fulfill({contentType:"text/html",
      body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/app.css"><esp-app></esp-app><script src="/app.js"></script>'});
    if (url.pathname === "/app.css") return route.fulfill({contentType:"text/css", body:CSS});
    if (url.pathname === "/app.js") return route.fulfill({contentType:"text/javascript", body:script});
    return route.fulfill({status:200, contentType:"application/json", body:"{}"});
  });
}

function fixtureEntities(language) {
  return [
    {id:"lang", name:"UI Language", domain:"select", sorting_group:"Panel settings", value:language, option:LANGUAGES},
    {id:"wifi-info", name:"Wi-Fi SSID", domain:"text_sensor", sorting_group:"Status & diagnostics", state:"Very-Long-SSID-For-Wrapping-Verification"},
    {id:"region", name:"Selected Region", domain:"select", sorting_group:"Location", state:"Kyiv City", value:"Kyiv City", option:["Kyiv City","Kyiv Oblast"]},
    {id:"weather-city", name:"Weather Location", domain:"text", sorting_group:"Location", state:"Kyiv", min_length:1, max_length:64},
    {id:"refresh", name:"Refresh Weather", domain:"button", sorting_group:"Location"},
    {id:"live", name:"Alert Live State", domain:"text_sensor", sorting_group:"Status & diagnostics", state:"LIVE"},
    {id:"long", name:"Extremely Long Diagnostic Label That Must Wrap Without Clipping Or Hidden Content", domain:"sensor", sorting_group:"Status & diagnostics", state:"1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ-very-long-value-that-must-wrap"},
    {id:"sht", name:"SHT4x Status", domain:"text_sensor", sorting_group:"Hardware", state:"NOT DETECTED"},
    {id:"buzzer", name:"Buzzer Test", domain:"button", sorting_group:"Hardware"},
    {id:"light", name:"Panel Backlight", domain:"light", sorting_group:"Hardware", brightness:200, state:"ON"},
    {id:"outage", name:"Outage Group Code", domain:"text", sorting_group:"SvitloBot", state:"10.1", min_length:1, max_length:8},
    {id:"sv-status", name:"SvitloBot Status", domain:"text_sensor", sorting_group:"SvitloBot", state:"ERROR"},
    {id:"network-ssid", name:"Network SSID", domain:"text", sorting_group:"Network", state:"Example Wi-Fi", min_length:1, max_length:32},
    {id:"network-mode", name:"Network IPv4 Mode", domain:"select", sorting_group:"Network", state:"DHCP", value:"DHCP", option:["DHCP","Static"]},
    {id:"network-apply", name:"Apply Network Settings", domain:"button", sorting_group:"Network"},
    {id:"starlink-enable", name:"Starlink Enabled", domain:"switch", sorting_group:"Starlink", state:"OFF"},
    {id:"starlink-host", name:"Starlink Host", domain:"text", sorting_group:"Starlink", state:"192.168.100.1", min_length:1, max_length:64},
    {id:"starlink-port", name:"Starlink Port", domain:"number", sorting_group:"Starlink", value:9200, min_value:1, max_value:65535, step:1},
    {id:"starlink-probe", name:"Starlink Probe Now", domain:"button", sorting_group:"Starlink"},
    {id:"security", name:"Web Security Status", domain:"text_sensor", sorting_group:"Maintenance", state:"FACTORY"},
    {id:"restart", name:"Restart", domain:"button", sorting_group:"Maintenance"},
    {id:"setup", name:"Setup Status", domain:"text_sensor", sorting_group:"Setup", state:"COMPLETE"},
  ];
}

async function seed(page, language) {
  await page.evaluate(({language, entities}) => {
    fixtureSource.emit("ping", {title:"R10 browser audit", uptime:123});
    for (const entity of entities) fixtureSource.emit("state", entity);
    fixtureSource.emit("state", {id:"lang", name:"UI Language", domain:"select",
      sorting_group:"Panel settings", value:language, option:["English","Русский","Українська"]});
  }, {language, entities:fixtureEntities(language)});
  await page.locator('[role="tablist"]').waitFor();
}

async function auditGeometry(page, label) {
  const result = await page.evaluate(() => {
    const visible = el => {
      const s=getComputedStyle(el), r=el.getBoundingClientRect();
      return s.display!=="none" && s.visibility!=="hidden" && r.width>0 && r.height>0;
    };
    const vp=document.documentElement.clientWidth;
    const panel=document.querySelector('[role="tabpanel"]');
    const controls=[...panel.querySelectorAll('button,input,select,summary')].filter(visible);
    const cards=[...panel.querySelectorAll('.usp-card')].filter(visible);
    const badBounds=controls.filter(el=>{const r=el.getBoundingClientRect();return r.left < -0.6 || r.right > vp+0.6;})
      .map(el=>({tag:el.tagName,text:(el.textContent||el.getAttribute('aria-label')||'').trim().slice(0,80),rect:el.getBoundingClientRect().toJSON()}));
    const small=controls.filter(el=>el.getBoundingClientRect().height < 43.5)
      .map(el=>({tag:el.tagName,text:(el.textContent||el.getAttribute('aria-label')||'').trim().slice(0,80),h:el.getBoundingClientRect().height}));
    const cardOverflow=cards.filter(el=>el.scrollWidth > el.clientWidth+1)
      .map(el=>({text:(el.textContent||'').trim().slice(0,80),scroll:el.scrollWidth,client:el.clientWidth,children:[...el.children].map(c=>({className:c.className,scroll:c.scrollWidth,client:c.clientWidth,rect:c.getBoundingClientRect().toJSON(),children:[...c.children].map(x=>({tag:x.tagName,text:(x.textContent||'').trim().slice(0,40),scroll:x.scrollWidth,client:x.clientWidth,rect:x.getBoundingClientRect().toJSON(),whiteSpace:getComputedStyle(x).whiteSpace,overflowWrap:getComputedStyle(x).overflowWrap,pad:getComputedStyle(x).padding}))}))}));
    const active=document.querySelector('[role="tab"][aria-selected="true"]');
    const nav=document.querySelector('[role="tablist"]');
    const ar=active.getBoundingClientRect(), nr=nav.getBoundingClientRect();
    return {
      viewport:vp,
      pageOverflow:document.documentElement.scrollWidth-document.documentElement.clientWidth,
      bodyOverflow:document.body.scrollWidth-document.body.clientWidth,
      badBounds, small, cardOverflow,
      selected:[...document.querySelectorAll('[role="tab"][aria-selected="true"]')].length,
      tabStops:[...document.querySelectorAll('[role="tab"][tabindex="0"]')].length,
      activeInsideNav:ar.left>=nr.left-1 && ar.right<=nr.right+1,
      panelLabel:panel.getAttribute('aria-labelledby'),
      activeId:active.id,
    };
  });
  assert.ok(result.pageOverflow <= 1, `${label} document overflow ${JSON.stringify(result)}`);
  assert.ok(result.bodyOverflow <= 1, `${label} body overflow ${JSON.stringify(result)}`);
  assert.deepEqual(result.badBounds, [], `${label} clipped control`);
  assert.deepEqual(result.small, [], `${label} touch target <44px`);
  assert.deepEqual(result.cardOverflow, [], `${label} card content overflow`);
  assert.equal(result.selected, 1, `${label} selected tabs`);
  assert.equal(result.tabStops, 1, `${label} tab stops`);
  assert.equal(result.activeInsideNav, true, `${label} active tab not revealed`);
  assert.equal(result.panelLabel, result.activeId, `${label} tabpanel label mismatch`);
}

async function keyboardAudit(page) {
  const active=page.locator('[role="tab"][aria-selected="true"]');
  await active.focus();
  await page.keyboard.press("Tab");
  const next=await page.evaluate(() => ({tag:document.activeElement?.tagName, inPanel:!!document.activeElement?.closest('[role="tabpanel"]')}));
  assert.equal(next.inPanel, true, `Tab from active tab did not enter panel: ${JSON.stringify(next)}`);
  await page.keyboard.press("Shift+Tab");
  assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("role")), "tab");
}

async function contrastAudit(page) {
  const rows=await page.evaluate(() => {
    function rgb(text) {
      const m=text.match(/rgba?\(([^)]+)\)/); if(!m) throw Error(text);
      const p=m[1].split(',').map(Number); return {r:p[0],g:p[1],b:p[2],a:p.length>3?p[3]:1};
    }
    function blend(top,bottom){return {r:top.r*top.a+bottom.r*(1-top.a),g:top.g*top.a+bottom.g*(1-top.a),b:top.b*top.a+bottom.b*(1-top.a),a:1};}
    function lum(c){return [.2126,.7152,.0722].reduce((n,w,i)=>{let v=[c.r,c.g,c.b][i]/255;v=v<=.04045?v/12.92:Math.pow((v+.055)/1.055,2.4);return n+w*v;},0);}
    function ratio(a,b){const x=lum(a),y=lum(b),hi=Math.max(x,y),lo=Math.min(x,y);return (hi+.05)/(lo+.05);}
    return [...document.querySelectorAll('.usp-pill.good,.usp-pill.warn,.usp-pill.bad,.usp-pill.quiet')].filter(el=>el.getClientRects().length).map(el=>{
      const style=getComputedStyle(el), parent=getComputedStyle(el.closest('.usp-card')||document.body);
      const fg=rgb(style.color), raw=rgb(style.backgroundColor), base=rgb(parent.backgroundColor);
      const bg=raw.a<1?blend(raw,base):raw;
      return {className:el.className,text:el.textContent.trim(),ratio:ratio(fg,bg)};
    });
  });
  assert.ok(rows.length > 0, "No semantic pills visible");
  for (const row of rows) assert.ok(row.ratio >= 4.5, `Semantic contrast ${JSON.stringify(row)}`);
  return rows;
}

async function run(scriptPath) {
  const script=fs.readFileSync(scriptPath,"utf8");
  const browser=await chromium.launch({headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1024,height:900}});
    page.setDefaultTimeout(10000);
    const errors=[]; page.on("pageerror",e=>errors.push(e.message));
    await installEvents(page); await routeApp(page,script); await page.goto("http://usp.test/");
    const hardwareLabels={English:"Hardware",Русский:"Оборудование",Українська:"Обладнання"};
    for (const language of LANGUAGES) {
      await seed(page,language);
      await page.waitForFunction(expected => document.querySelector("#usp-tab-hardware")?.textContent === expected, hardwareLabels[language]);
      for (const width of WIDTHS) {
        await page.setViewportSize({width,height:900});
        for (const tab of TABS) {
          await page.locator(`#usp-tab-${tab}`).click();
          await page.locator(`#usp-tab-${tab}[aria-selected="true"]`).waitFor();
          await auditGeometry(page,`${path.basename(scriptPath)} ${language} ${width} ${tab}`);
        }
      }
    }
    await page.setViewportSize({width:390,height:900});
    await page.locator('#usp-tab-hardware').click(); await page.locator('#usp-tab-hardware[aria-selected="true"]').waitFor(); await keyboardAudit(page);
    const contrasts=[];
    for (const tab of ["overview","hardware","svitlo","maintenance"]) {
      await page.locator(`#usp-tab-${tab}`).click(); await page.locator(`#usp-tab-${tab}[aria-selected="true"]`).waitFor(); contrasts.push(...await contrastAudit(page));
    }
    assert.deepEqual(errors,[]);
    console.log(`R10_WEB_BROWSER=PASS script=${path.basename(scriptPath)} languages=3 widths=${WIDTHS.length} tabs=${TABS.length} geometry=${3*WIDTHS.length*TABS.length} touch44=1 keyboard=1 contrast_rows=${contrasts.length}`);
  } finally { await browser.close(); }
}

const idx=process.argv.indexOf("--script");
if(idx<0||!process.argv[idx+1]) throw Error("--script path required");
run(path.resolve(process.argv[idx+1])).catch(e=>{console.error(e);process.exitCode=1;});
