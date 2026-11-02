/** Verify the custom module establishes a device-width mobile layout viewport. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {chromium}=require(path.resolve(__dirname,'../tools/web-ui/node_modules/playwright'));

async function main(){
  const i=process.argv.indexOf('--script');
  if(i<0||!process.argv[i+1]) throw Error('--script is required');
  const script=fs.readFileSync(process.argv[i+1],'utf8');
  const browser=await chromium.launch({headless:true});
  try{
    const context=await browser.newContext({viewport:{width:390,height:800},isMobile:true,hasTouch:true,deviceScaleFactor:1});
    const page=await context.newPage();
    await page.addInitScript(()=>{window.EventSource=class extends EventTarget{constructor(){super()}}});
    await page.route('**/*',route=>{
      const url=new URL(route.request().url());
      if(url.pathname==='/') return route.fulfill({contentType:'text/html',body:'<!DOCTYPE html><html><head><meta charset=UTF-8><link rel=icon href=data:></head><body><script type=module src=/0.js></script><esp-app></esp-app></body></html>'});
      if(url.pathname==='/0.js') return route.fulfill({contentType:'text/javascript',body:script});
      throw Error('Unexpected fixture request: '+url.href);
    });
    await page.goto('http://usp.test/');
    await page.waitForTimeout(100);
    const result=await page.evaluate(()=>({width:innerWidth,client:document.documentElement.clientWidth,meta:document.querySelector('meta[name=viewport]')?.content||''}));
    assert.match(result.meta,/width=device-width/);
    assert.ok(result.width<=390,result);
    assert.ok(result.client<=390,result);
    console.log(`WEB_VIEWPORT_BROWSER=PASS width=${result.width} client=${result.client} meta=${result.meta}`);
  } finally {await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1});
