// Real Windows/Chrome voice integration. Volume zero avoids playing test audio aloud.
const {chromium}=require('C:/Users/Asus/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const{pathToFileURL}=require('url');
(async()=>{
  const root=path.resolve(__dirname,'../..'),qa=path.join(root,'tmp/read-aloud-qa');
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
  const report=[];
  try{
    for(const name of ['rtos_yedi_adim_detayli','rtos_kod_rehberi']){
      const page=await browser.newPage({viewport:{width:1480,height:1100}});
      await page.goto(pathToFileURL(path.join(root,'output/rehber',name+'.html')).href);
      await page.waitForFunction(()=>!document.getElementById('listen-play').disabled);
      await page.evaluate(()=>{
        document.documentElement.style.scrollBehavior='auto';
        const Native=window.SpeechSynthesisUtterance;window.__voiceLog=[];
        window.SpeechSynthesisUtterance=function(text){
          const u=new Native(text);u.volume=0;
          u.addEventListener('start',()=>window.__voiceLog.push({event:'start',text}));
          u.addEventListener('end',()=>window.__voiceLog.push({event:'end',text}));
          u.addEventListener('error',e=>window.__voiceLog.push({event:e.error,text}));return u;
        };
      });
      await page.locator('#listen-settings-toggle').click();
      await page.locator('#listen-rate').selectOption('1.5');
      await page.locator('#listen-play').click();
      await page.waitForFunction(()=>window.__voiceLog.some(e=>e.event==='start'));
      await page.locator('#listen-play').click();
      assert.equal(await page.locator('#listen-play').textContent(),'▶ Sürdür');
      const pausedCount=await page.evaluate(()=>window.__voiceLog.filter(e=>e.event==='start').length);
      await page.waitForTimeout(300);
      assert.equal(await page.evaluate(()=>window.__voiceLog.filter(e=>e.event==='start').length),pausedCount);
      await page.locator('#listen-play').click();
      await page.waitForFunction(()=>window.__voiceLog.filter(e=>e.event==='start').length>=2,{},{timeout:25000});
      const events=await page.evaluate(()=>window.__voiceLog);
      assert.ok(events.some(e=>e.event==='end'));
      assert.ok(!events.some(e=>e.event==='synthesis-failed'));
      await page.locator('#listen-stop').click();
      await page.locator('#listen-rate').selectOption('0.95');
      await page.locator('#listen-follow').check();
      const chapter=name.includes('yedi')?'#step-2':'#ch02';
      await page.locator(name.includes('yedi')?'.side-nav a[href="#step-2"]':'.nav a[href="#ch02"]').click();
      await page.evaluate(id=>{const e=document.querySelector(id);scrollTo({top:scrollY+e.getBoundingClientRect().top-105,behavior:'instant'})},chapter);
      await page.screenshot({path:path.join(qa,name+'-desktop-final.png'),animations:'disabled'});
      await page.locator('#theme').click();
      await page.screenshot({path:path.join(qa,name+'-dark-final.png'),animations:'disabled'});
      await page.locator('#theme').click();
      await page.setViewportSize({width:390,height:844});
      await page.waitForFunction(()=>document.querySelector('.sidebar').getBoundingClientRect().right<=1);
      await page.evaluate(id=>{const e=document.querySelector(id);scrollTo({top:scrollY+e.getBoundingClientRect().top-105,behavior:'instant'})},chapter);
      await page.screenshot({path:path.join(qa,name+'-mobile-final.png'),animations:'disabled'});
      await page.locator('#listen-settings-toggle').click();
      await page.screenshot({path:path.join(qa,name+'-mobile-compact-final.png'),animations:'disabled'});
      report.push({name,voice:await page.locator('#listen-voice option:checked').textContent(),events,pauseResume:true});await page.close();
    }
    fs.writeFileSync(path.join(qa,'real-playback.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
