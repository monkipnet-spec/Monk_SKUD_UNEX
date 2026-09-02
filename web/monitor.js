(function(){
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const time=v=>{v=String(v||'');return v.length>=19?v.slice(11,19):(v||'—')};
function tick(){const d=new Date();$('clock').textContent=d.toLocaleTimeString('ru-RU',{hour12:false});$('date').textContent=d.toLocaleDateString('ru-RU',{weekday:'long',day:'2-digit',month:'2-digit',year:'numeric'});}
function statusHtml(s){if(s==='at_work')return '<span class="status present">НА РАБОТЕ</span>';if(s==='left')return '<span class="status left">УШЁЛ</span>';return '<span class="status absent">НЕ ПРИХОДИЛ</span>'}
async function refreshMonitor(){
 try{
  const r=await fetch('/api/monitor?ts='+Date.now(),{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);const j=await r.json();
  $('totalWorkers').textContent=j.registered_count||0;$('presentWorkers').textContent=j.present_count||0;$('awayWorkers').textContent=Math.max(0,(j.registered_count||0)-(j.present_count||0));$('controllersState').textContent=(j.controllers_online||0)+' / '+(j.controllers_total||0);
  const rows=j.workers||[];$('workers').innerHTML=rows.length?rows.map(x=>`<tr class="${x.status==='at_work'?'present-row':''}"><td>${statusHtml(x.status)}</td><td class="worker-name">${esc(x.name)}</td><td>${esc(x.position||'—')}</td><td class="muted">${esc(x.department||'—')}</td><td class="time">${time(x.arrival_time)}</td><td class="time">${time(x.departure_time)}</td></tr>`).join(''):'<tr><td colspan="6" class="empty">Нет активных работников в справочнике</td></tr>';
  $('updatedAt').textContent='Данные: '+(j.generated_at||'только что');$('connection').className='connection ok';$('connection').innerHTML='<span></span><b>ONLINE</b>';
 }catch(e){$('connection').className='connection bad';$('connection').innerHTML='<span></span><b>НЕТ СВЯЗИ</b>';$('updatedAt').textContent='Ошибка обновления: '+e.message;}
}
window.refreshMonitor=refreshMonitor;tick();setInterval(tick,1000);refreshMonitor();setInterval(refreshMonitor,3000);
})();
