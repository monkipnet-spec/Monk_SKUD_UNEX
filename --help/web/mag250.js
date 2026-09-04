(function(){
var scrollBox=null;
var scrollDir=1;
var scrollPause=18;
var lastRows='';
function el(id){return document.getElementById(id);}
function esc(s){s=String(s==null?'':s);return s.replace(/[&<>\"']/g,function(c){var m={'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',"'":'&#39;'};return m[c];});}
function pad2(n){return n<10?'0'+n:String(n);}
function clockTick(){var d=new Date();el('clock').innerHTML=pad2(d.getHours())+':'+pad2(d.getMinutes())+':'+pad2(d.getSeconds());var wd=['Воскресенье','Понедельник','Вторник','Среда','Четверг','Пятница','Суббота'];el('date').innerHTML=pad2(d.getDate())+'.'+pad2(d.getMonth()+1)+'.'+d.getFullYear()+' · '+wd[d.getDay()];}
function shortTime(v){v=String(v||'');return v.length>=19?v.substring(11,19):(v?v:'—');}
function statusHtml(s){if(s==='at_work')return '<span class="status present"><i class="s-dot"></i>НА РАБОТЕ</span>';if(s==='left')return '<span class="status left"><i class="s-dot"></i>УШЁЛ</span>';if(s==='disabled')return '<span class="status disabled"><i class="s-dot"></i>ОТКЛЮЧЕН</span>';return '<span class="status absent"><i class="s-dot"></i>НЕ ПРИХОДИЛ</span>';}
function parseJson(text){if(window.JSON&&window.JSON.parse)return window.JSON.parse(text);return eval('('+text+')');}
function requestJson(url,done,fail){var x;try{x=new XMLHttpRequest();}catch(e){fail('XMLHttpRequest');return;}x.open('GET',url,true);x.onreadystatechange=function(){if(x.readyState!==4)return;if(x.status>=200&&x.status<300){try{done(parseJson(x.responseText));}catch(e2){fail('JSON');}}else{fail('HTTP '+x.status);}};x.onerror=function(){fail('NETWORK');};try{x.send(null);}catch(e3){fail('SEND');}}
function renderRows(rows){var html='',i,r,cls;for(i=0;i<rows.length;i++){r=rows[i]||{};cls=(i%2?'alt ':'')+(r.status==='at_work'?'present-row':(r.status==='disabled'?'disabled-row':''));html+='<tr class="'+cls+'"><td>'+statusHtml(r.status)+'</td><td class="worker-name">'+esc(r.name||'—')+'</td><td>'+esc(r.position||'—')+'</td><td class="muted">'+esc(r.department||'—')+'</td><td class="time">'+shortTime(r.arrival_time)+'</td><td class="time">'+shortTime(r.departure_time)+'</td></tr>';}return html||'<tr><td colspan="6" class="empty">Нет сотрудников</td></tr>';}
function rowsSig(rows){var s='',i,r;for(i=0;i<rows.length;i++){r=rows[i]||{};s+=String(r.id||'')+'|'+String(r.status||'')+'|'+String(r.name||'')+'|'+String(r.position||'')+'|'+String(r.department||'')+'|'+String(r.arrival_time||'')+'|'+String(r.departure_time||'')+'\n';}return s;}
function updateRows(rows){rows=rows||[];var sig=rowsSig(rows);if(sig===lastRows)return;lastRows=sig;var old=scrollBox?scrollBox.scrollTop:0;el('workers').innerHTML=renderRows(rows);if(scrollBox){var max=scrollBox.scrollHeight-scrollBox.clientHeight;scrollBox.scrollTop=old>max?max:old;}}
function setConnection(ok){var c=el('connection');if(ok){c.className='online';c.innerHTML='<span class="dot"></span><b>ONLINE</b>';}else{c.className='offline';c.innerHTML='<span class="dot"></span><b>НЕТ СВЯЗИ</b>';}}
function refresh(){requestJson('/api/monitor?ts='+(new Date()).getTime(),function(j){var total=parseInt(j.registered_count,10)||0;var present=parseInt(j.present_count,10)||0;el('presentWorkers').innerHTML=present;el('awayWorkers').innerHTML=Math.max(0,total-present);updateRows(j.workers||[]);el('updatedAt').innerHTML='Данные: '+esc(j.generated_at||'только что');setConnection(true);},function(err){el('updatedAt').innerHTML='Ошибка: '+esc(err);setConnection(false);});}
function scrollStep(){if(!scrollBox)return;var max=scrollBox.scrollHeight-scrollBox.clientHeight;if(max<5){scrollBox.scrollTop=0;scrollDir=1;scrollPause=18;return;}if(scrollPause>0){scrollPause--;return;}if(scrollDir>0){scrollBox.scrollTop=scrollBox.scrollTop+2;if(scrollBox.scrollTop>=max-2){scrollBox.scrollTop=max;scrollDir=-1;scrollPause=22;}}else{scrollBox.scrollTop=0;scrollDir=1;scrollPause=16;}}
function keyHandler(e){e=e||window.event;var k=e.keyCode||e.which;if(k===38){scrollBox.scrollTop=Math.max(0,scrollBox.scrollTop-60);scrollPause=40;return false;}if(k===40){scrollBox.scrollTop=Math.min(scrollBox.scrollHeight-scrollBox.clientHeight,scrollBox.scrollTop+60);scrollPause=40;return false;}if(k===13){refresh();return false;}return true;}
function init(){scrollBox=el('listScroll');document.onkeydown=keyHandler;clockTick();refresh();window.setInterval(clockTick,1000);window.setInterval(refresh,3000);window.setInterval(scrollStep,100);}
if(document.readyState==='complete'||document.readyState==='interactive'){window.setTimeout(init,0);}else{window.onload=init;}
})();
