// Game cards: Patches opens the game's window inside the page (Close, a click outside it or Escape closes it, Escape
// closes the tab only with no window open), the patch search filters that window, and Play posts the title ID.
const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const listeners={};const alerts=[];const posts=[];const calls=[];
const item=search=>({dataset:{search},hidden:false});
const items=[item('1440p (PS4 Pro mode) 2560x1440 output'),item('Native 4K (PS4 Pro mode, requires 8 GiB enabled) Café'),item('60 FPS Unlock')];
const none={hidden:true},search={classList:{contains:c=>c==='patch-search'},value:'',focused:0,focus(){this.focused++;}};
const win={hidden:true,querySelectorAll:q=>q==='.patches li'?items:[],querySelector:q=>q==='.none'?none:q==='.patch-search'?search:null};
search.closest=s=>s==='.window'?win:null;
const el=attrs=>({closest:s=>attrs[s]||null});
const document={addEventListener:(n,f)=>{listeners[n]=f;},querySelectorAll:()=>[],getElementById:id=>id==='w-CUSA00900'?win:null};
vm.runInNewContext(fs.readFileSync('assets/panel.js','utf8'),{document,window:{close:()=>calls.push('window.close')},URLSearchParams,
    alert:t=>alerts.push(t),fetch:async(url,o)=>{posts.push([url,o.body]);return {text:async()=>'Starting Bloodborne'};}});
listeners.DOMContentLoaded();
(async()=>{
    const patches={dataset:{open:'w-CUSA00900'}};
    listeners.click({target:el({'[data-open]':patches})});
    assert(!win.hidden&&search.focused===1);
    const type=v=>{search.value=v;listeners.input({target:search});};
    type('4k cafe');assert(items[0].hidden&&!items[1].hidden&&items[2].hidden&&none.hidden);
    type('zzz');assert(items.every(i=>i.hidden)&&!none.hidden);
    type('');assert(items.every(i=>!i.hidden)&&none.hidden);
    // Escape closes the window, not the tab; a second Escape closes the tab
    let prevented=0;
    listeners.keydown({key:'Escape',preventDefault:()=>prevented++});
    assert(win.hidden&&prevented===1&&!calls.length);
    listeners.keydown({key:'Escape',preventDefault:()=>prevented++});
    assert(calls.includes('window.close'));
    // Close button and a click on the backdrop
    listeners.click({target:el({'[data-open]':patches})});assert(!win.hidden);
    listeners.click({target:el({'[data-close]':{}})});assert(win.hidden);
    listeners.click({target:el({'[data-open]':patches})});
    win.closest=()=>null;listeners.click({target:win});assert(win.hidden);
    // Play posts the title ID and shows the manager's answer
    const play={dataset:{play:'CUSA00900'},disabled:false};
    listeners.click({target:el({'[data-play]':play})});
    assert(play.disabled);
    await new Promise(r=>setTimeout(r,0));
    assert.deepEqual(posts,[['/games/play','tid=CUSA00900']]);
    assert(alerts[0]==='Starting Bloodborne'&&!play.disabled);
    console.log('PASS game cards: patch window open/close, patch search, Escape order, Play request');
})().catch(e=>{console.error(e);process.exit(1);});
