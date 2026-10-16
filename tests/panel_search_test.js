// Game search: filters by name or title ID, ignoring case and accents; shows the no-match notice.
const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const listeners={};const games=[{dataset:{search:'Bloodborne™ CUSA00900'},hidden:false},{dataset:{search:'Pokémon Café CUSA99999'},hidden:false}];
const none={hidden:true};
const document={addEventListener:(n,f)=>{listeners[n]=f;},querySelectorAll:q=>q==='article.game'?games:[],getElementById:id=>id==='no-match'?none:null};
vm.runInNewContext(fs.readFileSync('assets/panel.js','utf8'),{document,window:{},URLSearchParams,fetch:()=>{},alert:()=>{}});
listeners.DOMContentLoaded();
const type=v=>listeners.input({target:{id:'game-search',value:v}});
type('blood');assert(!games[0].hidden&&games[1].hidden&&none.hidden);
type('cusa999');assert(games[0].hidden&&!games[1].hidden);
type('pokemon cafe');assert(!games[1].hidden);
type('zzz');assert(games[0].hidden&&games[1].hidden&&!none.hidden);
type('');assert(!games[0].hidden&&!games[1].hidden&&none.hidden);
console.log('PASS game search by name or title ID, case and accent insensitive, no-match notice');
