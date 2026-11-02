/** Stage5 browser contract for the main Spotify tab. No provider route is allowed. */
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require(path.resolve(__dirname,'../tools/web-ui/node_modules/playwright'));
const args=process.argv.slice(2),i=args.indexOf('--script');
if(i<0||!args[i+1])throw Error('--script required');
const SCRIPT=fs.readFileSync(path.resolve(args[i+1]),'utf8');
const CSS=fs.readFileSync(path.resolve(__dirname,'../firmware/webui/usp-web-ui.css'),'utf8');
const LABELS={English:'Spotify','Русский':'Spotify','Українська':'Spotify'};
const LANGS={English:'en',Русский:'ru',Українська:'uk'};
/** All browser traffic is served by the local fixture; no provider host is allowed. */
async function routeFixture(page,language,state,storage,requests,failure){
  await page.route('**/*',route=>{
    const u=new URL(route.request().url());requests.push({url:u.href,method:route.request().method()});
    if(u.origin!=='http://usp.test')throw Error('External request: '+u.href);
    if(u.pathname==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/app.css"><esp-app></esp-app><script src="/app.js"></script>'});
    if(u.pathname==='/app.css')return route.fulfill({contentType:'text/css',body:CSS});
    if(u.pathname==='/app.js')return route.fulfill({contentType:'text/javascript',body:SCRIPT});
    if(u.pathname==='/usp/spotify/auth/status' && failure){
      if(failure==='offline')return route.abort('internetdisconnected');
      if(failure==='unauthorized')return route.fulfill({status:401,body:'Unauthorized'});
      if(failure==='oversize')return route.fulfill({body:'x'.repeat(8193)});
      return route.fulfill({contentType:'application/json',body:'{broken'});
    }
    if(u.pathname==='/usp/spotify/auth/status')return route.fulfill({json:{ok:true,state,exchange_available:true,language:LANGS[language],session_storage:storage,session_key_id:5,application:{client_id:'SHOULD_NOT_RENDER',redirect_uri:'https://secret.invalid/cb'},access_token:'NEVER_RENDER',refresh_token:'NEVER_RENDER'}});
    if(u.pathname==='/usp/spotify/auth/')return route.fulfill({contentType:'text/html',body:'<!doctype html><title>auth</title>'});
    return route.fulfill({status:200,contentType:'application/json',body:'{}'});
  });
}
async function run(language,state,storage,failure=""){
  const browser=await chromium.launch({headless:true});
  const page=await browser.newPage({viewport:{width:390,height:900}});page.setDefaultTimeout(10000);
  const requests=[],errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.addInitScript(()=>{localStorage.setItem('usp-active-tab','overview');window.EventSource=class extends EventTarget{constructor(){super();window.fixtureSource=this;}emit(t,v){this.dispatchEvent(new MessageEvent(t,{data:JSON.stringify(v)}));}}});
  await routeFixture(page,language,state,storage,requests,failure);
  try{
    await page.goto('http://usp.test/');await page.waitForFunction(()=>!!window.fixtureSource);
    await page.evaluate(language=>{fixtureSource.emit('ping',{title:'Stage5',uptime:1});fixtureSource.emit('state',{id:'lang',name:'UI Language',domain:'select',sorting_group:'Panel settings',value:language,option:['English','Русский','Українська']});fixtureSource.emit('state',{id:'setup',name:'Setup Status',domain:'text_sensor',sorting_group:'Setup',state:'COMPLETE'});},language);
    const tab=page.getByRole('tab',{name:LABELS[language],exact:true});await tab.waitFor();await tab.click();
    await page.waitForFunction(expected=>document.querySelector('[data-spotify-state]')?.dataset.spotifyState===expected,failure?'unknown':state);
    if(failure)await page.waitForFunction(label=>document.querySelector('[data-spotify-state]')?.textContent===label,
      {English:'Status unavailable',Русский:'Состояние недоступно',Українська:'Стан недоступний'}[language]);
    const view=await page.evaluate(()=>({text:document.querySelector('[role="tabpanel"]').textContent,state:document.querySelector('[data-spotify-state]').dataset.spotifyState,storage:document.querySelector('[data-spotify-storage]').dataset.spotifyStorage,overflow:document.documentElement.scrollWidth>document.documentElement.clientWidth,small:[...document.querySelectorAll('[role="tabpanel"] button')].filter(b=>b.getBoundingClientRect().height<43.5).length}));
    assert.equal(view.state,failure?'unknown':state);assert.equal(view.storage,failure?'unknown':storage);
    if(failure){
      const label={English:'Status unavailable',Русский:'Состояние недоступно',Українська:'Стан недоступний'}[language];
      assert.ok(view.text.includes(label),'Failed status must not remain Checking');
      assert.equal(view.text.includes({English:'Checking…',Русский:'Проверяем…',Українська:'Перевіряємо…'}[language]),false);
    }assert.equal(view.overflow,false);assert.equal(view.small,0);
    for(const secret of ['SHOULD_NOT_RENDER','secret.invalid','NEVER_RENDER'])assert.equal(view.text.includes(secret),false);
    const localStatus=requests.filter(x=>new URL(x.url).pathname==='/usp/spotify/auth/status');assert.ok(localStatus.length>=1);assert.ok(localStatus.every(x=>x.method==='GET'));
    assert.equal(requests.some(x=>new URL(x.url).hostname.includes('spotify.com')),false);
    const before=localStatus.length;await page.getByRole('button',{name:/Refresh|Обновить|Оновити/}).click();await page.waitForTimeout(50);assert.ok(requests.filter(x=>new URL(x.url).pathname==='/usp/spotify/auth/status').length>before);
    assert.deepEqual(errors,[]);
    await page.getByRole('button',{name:/Manage sign-in|Управлять входом|Керувати входом/}).click();
    await page.waitForURL('http://usp.test/usp/spotify/auth/');
    assert.ok(requests.every(x=>x.method==='GET'),'Main Spotify tab must not submit a setting or OAuth request');
  }finally{await browser.close();}
}
(async()=>{for(const lang of Object.keys(LABELS)){await run(lang,'connected','hardware');await run(lang,'connected','local');await run(lang,'reauth','local');for(const failure of ['unauthorized','malformed','offline','oversize'])await run(lang,'connected','local',failure);}console.log('SPOTIFY_WEB_STATUS_BROWSER=PASS cases=21 languages3 hardware_local states3 errors4 explicit_manage local_status_get no_provider no_secret touch44');})().catch(e=>{console.error(e);process.exitCode=1;});
