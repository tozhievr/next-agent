const {spawn}=require('node:child_process');const {chromium}=require('@playwright/test');const path=require('node:path');const net=require('node:net');
(async()=>{
 const executable=process.env.NEXTAGENT_PORTABLE_EXECUTABLE;if(!executable)throw new Error('Set NEXTAGENT_PORTABLE_EXECUTABLE');
 const server=net.createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;await new Promise(r=>server.close(r));
 const child=spawn(executable,[`--remote-debugging-port=${port}`],{env:{...process.env,OPENROUTER_API_KEY:'',APPIMAGE_EXTRACT_AND_RUN:'1'},stdio:'ignore'});let browser;
 try{let ready=false;for(let i=0;i<240;i++){try{const result=await fetch(`http://127.0.0.1:${port}/json/version`);if(result.ok){ready=true;break;}}catch{}if(child.exitCode!==null)throw new Error('Portable launcher exited early: '+child.exitCode);await new Promise(r=>setTimeout(r,500));}if(!ready)throw new Error('Portable app did not expose its test debug port');
 browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`);const page=browser.contexts()[0].pages()[0];await page.getByRole('heading',{name:'Что будем создавать?'}).waitFor();await page.getByRole('button',{name:'Проверить ядро'}).click();await page.getByText('Завершено',{exact:true}).waitFor({timeout:30000});const state=await page.evaluate(()=>window.harness.snapshot());if(state.error||state.tool_calls!==1)throw new Error(JSON.stringify(state));await page.evaluate(()=>window.harness.window('close')).catch(()=>{});console.log('Portable launcher and embedded native transaction passed: '+path.basename(executable));
 }finally{await browser?.close().catch(()=>{});if(child.exitCode===null)child.kill();}
})().catch(e=>{console.error(e);process.exitCode=1;});
