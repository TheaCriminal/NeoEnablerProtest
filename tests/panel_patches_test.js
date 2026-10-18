// Patch checkboxes: after a change the panel shows the state the manager returns, and alerts only when an entry
// outside the ticked executable's resolutions changed (the eboot.bin resolution a collection needs).
const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const listeners={};const alerts=[];let reply='';
const box=(hash,res,checked)=>({dataset:{tid:'CUSA00001',hash,res},checked,disabled:false});
const boxes=[box('a1','eboot.bin',false),box('a2','eboot.bin',true),box('b1','b.elf',false),box('b2','b.elf',true),box('x',undefined,true)];
const document={addEventListener:(n,f)=>{listeners[n]=f;},querySelectorAll:()=>[],getElementById:()=>null,
    querySelector:q=>boxes.find(b=>q.includes('"'+b.dataset.hash+'"'))||null};
vm.runInNewContext(fs.readFileSync('assets/panel.js','utf8'),{document,window:{},URLSearchParams,alert:t=>alerts.push(t),
    fetch:async()=>({ok:true,text:async()=>reply})});
const tick=async(b,on,text)=>{b.checked=on;reply=text;listeners.change({target:b});await new Promise(r=>setTimeout(r,0));};
listeners.DOMContentLoaded();
(async()=>{
    // same executable: the other resolution turned off, no alert
    await tick(boxes[2],true,'CUSA00001: 1 enabled; the other resolution was disabled\nstate a1=0 a2=1 b1=1 b2=0 x=1');
    assert(boxes[2].checked&&!boxes[3].checked&&boxes[1].checked&&alerts.length===0&&!boxes[2].disabled);
    // last eboot.bin resolution off: the other executable's resolution turned off too, with an alert
    await tick(boxes[1],false,'CUSA00001: 2 disabled. A PS4 Pro patch for A is required for B\nstate a1=0 a2=0 b1=0 b2=0 x=1');
    assert(!boxes[2].checked&&alerts.length===1&&alerts[0].includes('is required for B'));
    // a resolution of another executable on: the eboot.bin one turned on, with an alert
    await tick(boxes[3],true,'CUSA00001: 2 enabled. A PS4 Pro patch for A is required for B, so 2 was enabled too\nstate a1=0 a2=1 b1=0 b2=1 x=1');
    assert(boxes[1].checked&&boxes[3].checked&&alerts.length===2);
    console.log('PASS patch checkboxes follow the manager state, alert only for changes outside the executable');
})().catch(e=>{console.error(e);process.exit(1);});
