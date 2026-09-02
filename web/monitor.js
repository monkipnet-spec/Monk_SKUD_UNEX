(function(){
'use strict';
function byId(id){return document.getElementById(id);}
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function time(v){v=String(v||'');return v.length>=19?v.slice(11,19):(v||'—');}
function tick(){var d=new Date();byId('clock').textContent=d.toLocaleTimeString('ru-RU',{hour12:false});byId('date').textContent=d.toLocaleDateString('ru-RU',{weekday:'long',day:'2-digit',month:'2-digit',year:'numeric'});}
function statusHtml(s){if(s==='at_work')return '<span class="status present">НА РАБОТЕ</span>';if(s==='left')return '<span class="status left">УШЁЛ</span>';if(s==='disabled')return '<span class="status disabled">ОТКЛЮЧЕН</span>';return '<span class="status absent">НЕ ПРИХОДИЛ</span>';}
function xhrJson(url,ok,fail){var x=new XMLHttpRequest();x.open('GET',url,true);x.onreadystatechange=function(){if(x.readyState!==4)return;if(x.status>=200&&x.status<300){try{ok(JSON.parse(x.responseText));}catch(e){fail(e);}}else fail(new Error('HTTP '+x.status));};x.onerror=function(){fail(new Error('Ошибка сети'));};x.send(null);}

var scrollBox=null;
var scrollRunning=false;
var scrollLast=0;
var scrollPauseUntil=0;
var scrollAtBottom=false;
var userPauseUntil=0;
var rowsSignature='';
var SCROLL_PX_PER_SECOND=20;
var TOP_PAUSE_MS=1600;
var BOTTOM_PAUSE_MS=2200;

function maxScroll(){return scrollBox?Math.max(0,scrollBox.scrollHeight-scrollBox.clientHeight):0;}
function needsScroll(){return maxScroll()>6;}
function markManualPause(){userPauseUntil=(new Date()).getTime()+6000;}
function scrollFrame(ts){
 if(!scrollRunning)return;
 var now=(new Date()).getTime();
 var max=maxScroll();
 if(max<=6){scrollBox.scrollTop=0;scrollLast=ts;scrollAtBottom=false;window.requestAnimationFrame(scrollFrame);return;}
 if(now<userPauseUntil||now<scrollPauseUntil){scrollLast=ts;window.requestAnimationFrame(scrollFrame);return;}
 if(scrollAtBottom){scrollBox.scrollTop=0;scrollAtBottom=false;scrollPauseUntil=now+TOP_PAUSE_MS;scrollLast=ts;window.requestAnimationFrame(scrollFrame);return;}
 if(!scrollLast)scrollLast=ts;
 var dt=Math.min(100,Math.max(0,ts-scrollLast));scrollLast=ts;
 var next=scrollBox.scrollTop+(SCROLL_PX_PER_SECOND*dt/1000);
 if(next>=max-1){scrollBox.scrollTop=max;scrollAtBottom=true;scrollPauseUntil=now+BOTTOM_PAUSE_MS;}else scrollBox.scrollTop=next;
 window.requestAnimationFrame(scrollFrame);
}
function ensureAutoScroll(){
 if(!scrollBox)scrollBox=document.querySelector('.table-scroll');
 if(!scrollBox)return;
 if(!needsScroll()){scrollBox.scrollTop=0;scrollAtBottom=false;return;}
 if(!scrollRunning){scrollRunning=true;scrollPauseUntil=(new Date()).getTime()+TOP_PAUSE_MS;window.requestAnimationFrame(scrollFrame);}
}
function initScrollInteraction(){
 scrollBox=document.querySelector('.table-scroll');if(!scrollBox)return;
 scrollBox.addEventListener('touchstart',markManualPause,false);
 scrollBox.addEventListener('mousedown',markManualPause,false);
 scrollBox.addEventListener('wheel',markManualPause,false);
}

function statusRank(s){return s==='at_work'?0:s==='left'?1:s==='absent'?2:3;}
function sortRowsByStatus(rows){
 var copy=rows.slice(0);
 copy.sort(function(a,b){
  a=a||{};b=b||{};
  var ar=statusRank(a.status),br=statusRank(b.status);
  if(ar!==br)return ar-br;
  var an=String(a.name||'').toLocaleLowerCase();
  var bn=String(b.name||'').toLocaleLowerCase();
  if(an<bn)return -1;if(an>bn)return 1;
  return (Number(a.id)||0)-(Number(b.id)||0);
 });
 return copy;
}
function buildRows(rows){
 var out='';
 for(var i=0;i<rows.length;i++){
  var x=rows[i]||{};
  out+='<tr class="'+(x.status==='at_work'?'present-row':(x.status==='disabled'?'disabled-row':''))+'"><td>'+statusHtml(x.status)+'</td><td class="worker-name">'+esc(x.name)+'</td><td>'+esc(x.position||'—')+'</td><td class="muted">'+esc(x.department||'—')+'</td><td class="time">'+time(x.arrival_time)+'</td><td class="time">'+time(x.departure_time)+'</td></tr>';
 }
 return out;
}
function updateWorkers(rows){
 rows=sortRowsByStatus(rows||[]);
 var sig='';try{sig=JSON.stringify(rows);}catch(e){sig=String((new Date()).getTime());}
 if(sig===rowsSignature){ensureAutoScroll();return;}
 var oldTop=scrollBox?scrollBox.scrollTop:0;
 rowsSignature=sig;
 byId('workers').innerHTML=rows.length?buildRows(rows):'<tr><td colspan="6" class="empty">Нет активных работников в справочнике</td></tr>';
 window.setTimeout(function(){if(scrollBox&&oldTop>0)scrollBox.scrollTop=Math.min(oldTop,maxScroll());ensureAutoScroll();},0);
}
function refreshMonitor(){
 xhrJson('/api/monitor?ts='+(new Date()).getTime(),function(j){
  byId('totalWorkers').textContent=j.registered_count||0;
  byId('presentWorkers').textContent=j.present_count||0;
  byId('awayWorkers').textContent=Math.max(0,(j.registered_count||0)-(j.present_count||0));
  byId('controllersState').textContent=(j.controllers_online||0)+' / '+(j.controllers_total||0);
  updateWorkers(j.workers||[]);
  byId('updatedAt').textContent='Данные: '+(j.generated_at||'только что');
  byId('connection').className='connection ok';byId('connection').innerHTML='<span></span><b>ONLINE</b>';
 },function(e){
  byId('connection').className='connection bad';byId('connection').innerHTML='<span></span><b>НЕТ СВЯЗИ</b>';
  byId('updatedAt').textContent='Ошибка обновления: '+e.message;
 });
}

window.refreshMonitor=refreshMonitor;
initScrollInteraction();tick();window.setInterval(tick,1000);refreshMonitor();window.setInterval(refreshMonitor,3000);
})();
