const {chromium}=require('C:/Users/Asus/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const{pathToFileURL}=require('url');
const root=path.resolve(__dirname,'../..'),qa=path.join(root,'tmp/read-aloud-qa');
fs.mkdirSync(qa,{recursive:true});
const mockSpeech=({missing=false,unsupported=false}={})=>{
  if(unsupported){Object.defineProperty(window,'speechSynthesis',{value:undefined});return;}
  const events=new EventTarget();
  const tr={name:'Microsoft Tolga',voiceURI:'test-tr',lang:'tr-TR',localService:true};
  window.__spoken=[];
  window.SpeechSynthesisUtterance=function(text){this.text=text;};
  const synth={speaking:false,paused:false,pending:false,current:null,voices:missing?[]:[tr],
    getVoices(){return this.voices;},addEventListener(...args){events.addEventListener(...args)},
    speak(u){this.current=u;this.speaking=true;window.__spoken.push(u);u.onstart?.({});},
    pause(){this.paused=true;},resume(){this.paused=false;},
    cancel(){const old=this.current;this.current=null;this.speaking=false;setTimeout(()=>old?.onerror?.({error:'canceled'}),0)},
    finish(){const old=this.current;this.current=null;this.speaking=false;old?.onend?.({});},
    fail(){this.current?.onerror?.({error:'synthesis-failed'});},
    addVoice(){this.voices=[tr];events.dispatchEvent(new Event('voiceschanged'));}
  };
  Object.defineProperty(window,'speechSynthesis',{value:synth});
};
(async()=>{
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
  const errors=[],report={};
  try{
    // Inspect the actual browser and exercise its voice engine at volume zero.
    const real=await browser.newPage();
    await real.goto(pathToFileURL(path.join(root,'output/rehber/rtos_yedi_adim_detayli.html')).href);
    await real.waitForFunction(()=>speechSynthesis.getVoices().length>0,{},{timeout:8000}).catch(()=>{});
    report.actualVoices=await real.evaluate(()=>speechSynthesis.getVoices().map(v=>({name:v.name,lang:v.lang,local:v.localService})));
    if(report.actualVoices.some(v=>v.lang.toLowerCase().startsWith('tr'))){
      await real.evaluate(()=>{
        window.__realEvents=[];
        const b=document.createElement('button');b.id='test-real';b.textContent='test';b.style.cssText='position:fixed;right:20px;top:90px;z-index:99';
        b.onclick=()=>{const u=new SpeechSynthesisUtterance('Sesli okuma hazır.');u.lang='tr-TR';u.volume=0;u.voice=speechSynthesis.getVoices().find(v=>v.lang.toLowerCase().startsWith('tr'));u.onstart=()=>window.__realEvents.push('start');u.onend=()=>window.__realEvents.push('end');u.onerror=e=>window.__realEvents.push(e.error);window.__realUtterance=u;speechSynthesis.speak(u);};document.body.append(b);
      });
      await real.locator('#test-real').click();
      await real.waitForFunction(()=>window.__realEvents.includes('end'),{},{timeout:15000}).catch(()=>{});
      report.actualEngineEvents=await real.evaluate(()=>window.__realEvents);
    }
    await real.close();
    for(const name of ['rtos_yedi_adim_detayli','rtos_kod_rehberi']){
      const page=await browser.newPage({viewport:{width:1480,height:1100}});page.on('pageerror',e=>errors.push(e.message));
      await page.addInitScript(mockSpeech);
      await page.goto(pathToFileURL(path.join(root,'output/rehber',name+'.html')).href);
      assert.equal(await page.locator('#listen-play').isEnabled(),true);
      await page.locator('#listen-settings-toggle').click();
      await page.locator('#listen-follow').uncheck();
      await page.locator('#listen-play').click();
      await page.waitForFunction(()=>window.__spoken.length===1);
      assert.equal(await page.locator('#listen-play').textContent(),'❚❚ Duraklat');
      const first=await page.evaluate(()=>window.__spoken[0].text);
      assert.ok(first.length>10);
      assert.equal(await page.locator('.listen-active').count(),1);
      await page.locator('#listen-play').click();
      assert.equal(await page.evaluate(()=>speechSynthesis.paused),true);
      assert.equal(await page.locator('#listen-play').textContent(),'▶ Sürdür');
      await page.locator('#listen-play').click();
      assert.equal(await page.evaluate(()=>speechSynthesis.paused),false);
      assert.equal(await page.evaluate(()=>window.__spoken.length),1);
      await page.evaluate(()=>speechSynthesis.finish());
      await page.waitForFunction(()=>window.__spoken.length===2);
      await page.locator('#listen-next').click();
      await page.waitForFunction(()=>window.__spoken.length===3);
      // Stale cancellation events must not stop the next utterance.
      await page.waitForTimeout(50);
      assert.equal(await page.locator('#listen-play').textContent(),'❚❚ Duraklat');
      await page.locator('#listen-prev').click();
      await page.locator('#listen-rate').selectOption('1.3');
      await page.locator('#listen-next').click();
      assert.equal(await page.evaluate(()=>window.__spoken.at(-1).rate),1.3);
      await page.locator('#listen-play').click();
      await page.locator('#listen-next').click();
      assert.equal(await page.locator('#listen-play').textContent(),'▶ Sürdür');
      await page.locator('#listen-play').click();
      await page.locator('#listen-stop').click();
      assert.equal(await page.locator('.listen-active').count(),0);
      assert.equal(await page.locator('#listen-next').isDisabled(),true);
      await page.locator('#listen-scope').selectOption('all');
      await page.locator('#listen-play').click();
      // Drive the real reader to completion using controllable engine events.
      // setTimeout(60) is accelerated, without replacing the reader's logic.
      await page.evaluate(()=>{window.__timer=window.setTimeout;window.setTimeout=(fn,ms,...args)=>window.__timer(fn,ms===60?0:ms,...args);});
      const allTranscript=await page.evaluate(async()=>{
        const start=window.__spoken.length-1;
        for(let i=0;i<20000;i++){
          if(document.querySelector('#listen-status').textContent==='Okuma tamamlandı.')return window.__spoken.slice(start).map(u=>u.text);
          if(speechSynthesis.current)speechSynthesis.finish();
          await new Promise(r=>window.__timer(r,0));
        }
        throw Error('Reader failed to reach end');
      });
      assert.ok(allTranscript.length>100);
      assert.ok(allTranscript.every(t=>t.length<=190));
      assert.ok(allTranscript.some(t=>t.includes('DUMP')));
      assert.ok(!allTranscript.some(t=>t.startsWith('Kod bloğu.')));
      assert.equal(await page.locator('[role="progressbar"]').getAttribute('aria-valuenow'),'100');
      await page.locator('#listen-code').check();
      await page.locator('#listen-play').click();
      const codeRead=await page.evaluate(async()=>{
        for(let i=0;i<2000;i++){
          if(window.__spoken.at(-1).text.startsWith('Kod bloğu.'))return true;
          speechSynthesis.finish();await new Promise(r=>window.__timer(r,0));
        }return false;
      });
      assert.equal(codeRead,true);
      await page.locator('#listen-stop').click();
      await page.locator('#listen-code').uncheck();
      await page.locator('#listen-scope').selectOption('current');
      await page.locator('#listen-play').click();
      await page.evaluate(()=>speechSynthesis.fail());
      assert.ok((await page.locator('#listen-status').textContent()).includes('synthesis-failed'));
      await page.locator('#listen-play').click();
      await page.locator(name.includes('yedi')?'.side-nav a[href="#step-2"]':'.nav a[href="#ch02"]').click();
      assert.equal(await page.locator('#listen-play').textContent(),'▶ Dinle');
      await page.locator('#listen-play').click();
      assert.equal(await page.locator('.listen-active').evaluate(n=>n.closest('.step-panel,.chapter').id),name.includes('yedi')?'step-2':'ch02');
      await page.locator('#listen-play').click();
      await page.screenshot({path:path.join(qa,name+'-desktop.png')});
      await page.locator('#theme').click();
      await page.screenshot({path:path.join(qa,name+'-dark.png')});
      await page.locator('#theme').click();
      await page.setViewportSize({width:390,height:844});
      await page.waitForFunction(()=>document.querySelector('.sidebar').getBoundingClientRect().right<=1);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      await page.screenshot({path:path.join(qa,name+'-mobile.png'),animations:'disabled'});
      await page.locator('#listen-settings-toggle').click();
      await page.screenshot({path:path.join(qa,name+'-mobile-compact.png')});
      report[name]={paragraphFragments:allTranscript.length,first,sourceCodeOptional:codeRead,complete:true,mobileOverflow:false};
      fs.writeFileSync(path.join(qa,name+'-transcript.txt'),allTranscript.join('\n'),'utf8');
      await page.close();
    }
    for(const option of [{missing:true},{unsupported:true}]){
      const page=await browser.newPage();await page.addInitScript(mockSpeech,option);
      await page.goto(pathToFileURL(path.join(root,'output/rehber/rtos_yedi_adim_detayli.html')).href);
      assert.equal(await page.locator('#listen-play').isDisabled(),true);
      if(option.missing){await page.evaluate(()=>speechSynthesis.addVoice());assert.equal(await page.locator('#listen-play').isEnabled(),true);}
      await page.close();
    }
    assert.deepEqual(errors,[]);report.errors=errors;report.fallbacks=true;
    fs.writeFileSync(path.join(qa,'checks.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
