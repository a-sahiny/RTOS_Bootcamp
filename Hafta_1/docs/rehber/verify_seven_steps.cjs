const {chromium}=require('C:/Users/Asus/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const fs=require('fs'),path=require('path');const{pathToFileURL}=require('url');
(async()=>{
 const root=path.resolve(__dirname,'../..'),qa=path.join(root,'tmp/seven-steps-qa');fs.mkdirSync(qa,{recursive:true});
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1480,height:1100},deviceScaleFactor:1});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 const file=path.join(root,'output/rehber/rtos_yedi_adim_detayli.html');await page.goto(pathToFileURL(file).href);
 const expect=async(test,message)=>{if(!await test)throw new Error(message)};
 await expect(page.locator('.step-panel:visible').count().then(n=>n===1),'Initial single step failed');
 await expect(page.locator('#step-1').isVisible(),'Initial step was not 1');
 await page.screenshot({path:path.join(qa,'desktop.png')});
 const overflow=[];
 for(let i=0;i<=7;i++){
   await page.locator('.side-nav [data-nav="'+i+'"]').click();
   await expect(page.locator('#step-'+i).isVisible(),'Step '+i+' navigation failed');
   const bad=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);if(bad)overflow.push(i);
   await page.screenshot({path:path.join(qa,'step-'+i+'.png')});
 }
 await page.locator('.side-nav [data-nav="3"]').click();await page.locator('[data-byte="62"]').click();
 await expect(page.locator('#byte-info').textContent().then(x=>x.includes('0x8B')&&x.includes('CRC-8')),'CRC byte details failed');
 await page.locator('[data-byte="63"]').click();await expect(page.locator('#byte-info').textContent().then(x=>x.includes('0x0A')&&x.includes('LF')),'LF details failed');
 await page.locator('[data-byte="23"]').click();await expect(page.locator('#byte-info').textContent().then(x=>x.includes('0x20')),'Padding details failed');
 await page.locator('.frame-widget').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(qa,'frame.png')});
 await page.locator('.side-nav [data-nav="4"]').click();
 const removed=[];for(let i=0;i<3;i++){await page.locator('#fifo-get').click();removed.push((await page.locator('#fifo-info').textContent()).split(' ')[0])}
 if(removed.join(',')!=='TEL1,TEL2,BTN7')throw Error('FIFO ordering failed');await expect(page.locator('#fifo-get').isDisabled(),'Empty queue button not disabled');
 await page.locator('#fifo-reset').click();await expect(page.locator('.fifo-item').count().then(n=>n===3),'FIFO reset failed');
 await page.locator('#all').click();await expect(page.locator('.step-panel:visible').count().then(n=>n===8),'Read all failed');
 await page.locator('#all').click();await expect(page.locator('.step-panel:visible').count().then(n=>n===1),'Step mode restore failed');
 await page.locator('#search').fill('RingBuffer_DumpNext');await expect(page.locator('.search-results button').count().then(n=>n>0),'Search yielded no matching function');
 await page.locator('.search-results button').first().click();await expect(page.locator('#step-7').isVisible(),'Search did not reveal hidden destination');
 await page.locator('#search').fill('kelime_olmayan_0197');await expect(page.locator('.search-results').textContent().then(x=>x.includes('Eşleşme yok')),'Empty search failed');await page.locator('#search').fill('');
 await page.locator('#theme').click();await expect(page.locator('body').evaluate(e=>e.classList.contains('dark')),'Theme failed');
 await page.locator('.side-nav [data-nav="6"]').click();await page.screenshot({path:path.join(qa,'dark.png')});await page.locator('#theme').click();
 await page.locator('#wrap').click();await expect(page.locator('body').evaluate(e=>!e.classList.contains('wrap-code')),'Code wrapping control failed');await page.locator('#wrap').click();
 const links=await page.evaluate(()=>({internal:[...document.querySelectorAll('a[href^="#"]')].filter(a=>!document.getElementById(a.hash.slice(1))).map(a=>a.hash),local:[...document.querySelectorAll('a[href]')].map(a=>a.getAttribute('href')).filter(h=>!h.startsWith('#')&&!h.startsWith('http'))}));
 const missing=links.local.filter(h=>!fs.existsSync(path.resolve(path.dirname(file),h)));if(links.internal.length||missing.length)throw Error('Broken links: '+JSON.stringify({links:links.internal,missing}));
 await page.setViewportSize({width:390,height:844});await page.evaluate(()=>{document.documentElement.style.scrollBehavior='auto';scrollTo(0,0)});await page.waitForFunction(()=>document.querySelector('.sidebar').getBoundingClientRect().right<=0);
 await page.screenshot({path:path.join(qa,'mobile.png'),animations:'disabled'});
 const mobileOverflow=[];
 for(let i=0;i<=7;i++){
   await page.locator('.mobile-menu').click();await page.locator('.side-nav [data-nav="'+i+'"]').click();
   if(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth))mobileOverflow.push(i);
 }
 await page.locator('.mobile-menu').click();await page.locator('.side-nav [data-nav="3"]').click();await page.locator('[data-byte="62"]').click();await page.locator('.frame-widget').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(qa,'mobile-frame.png'),animations:'disabled'});
 await page.reload();await expect(page.locator('#step-3').isVisible(),'Deep-link restore after reload failed');
 const checks={panels:8,main_steps:7,source_blocks:await page.locator('.source').count(),bytes:await page.locator('[data-byte]').count(),search:true,crc:'8B',fifo_order:removed,links_checked:links.local.length,desktop_overflow:overflow,mobile_overflow:mobileOverflow,errors};
 await browser.close();if(errors.length||overflow.length||mobileOverflow.length)throw Error(JSON.stringify(checks));fs.writeFileSync(path.join(qa,'checks.json'),JSON.stringify(checks,null,2));console.log(JSON.stringify(checks));
})().catch(e=>{console.error(e);process.exit(1)});
