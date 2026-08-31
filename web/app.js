let USERS=[];
let DEPARTMENTS=[];
let USERS_LOADED=false;

function enc(o){return new URLSearchParams(o)}
async function api(url,opt={}){let r=await fetch(url,opt);if(r.status===401){location='/login.html';throw new Error('auth');}return r}
function tab(id){document.querySelectorAll('.tab').forEach(x=>x.classList.add('hidden'));document.getElementById(id).classList.remove('hidden');if(id==='dashboard')loadTodayAttendance();if(id==='cards')loadCards();if(id==='users')loadUsers();if(id==='departments'){loadUsers().then(()=>loadDepartments());}if(id==='controllers')loadControllers();}

function formatUptime(total){total=Math.max(0,Math.floor(Number(total)||0));const d=Math.floor(total/86400);total%=86400;const h=Math.floor(total/3600);total%=3600;const m=Math.floor(total/60);const sec=total%60;const clock=[h,m,sec].map(x=>String(x).padStart(2,'0')).join(':');return d>0?d+'д '+clock:clock;}
async function refreshStatus(){let r=await api('/api/status');let s=await r.json();serialStatus.textContent=s.serial_status;serialStatus.className=s.serial_status==='ONLINE'?'ok':'bad';serialDevice.textContent=s.serial_device||'USB-COM не найден';presentCount.textContent=s.present_count;repeatSec.textContent=s.repeat_seconds+' сек';if(window.cpuLoad)cpuLoad.textContent=Number(s.cpu_percent||0).toFixed(1)+'%';if(window.ramLoad)ramLoad.textContent=Number(s.ram_percent||0).toFixed(1)+'%';if(window.ramDetail)ramDetail.textContent=(s.ram_used_mb||0)+' / '+(s.ram_total_mb||0)+' MB';if(window.uptimeValue)uptimeValue.textContent=formatUptime(s.uptime_seconds);}
function timeOnly(value){if(!value)return '—';const s=String(value);return s.length>=19?s.slice(11,19):s;}
async function loadTodayAttendance(){
    let a=await (await api('/api/attendance/today')).json();
    if(!window.todayAttendanceBody)return;
    if(!a.length){todayAttendanceBody.innerHTML='<tr><td colspan="6" class="muted">Нет событий за сегодня</td></tr>';return;}
    todayAttendanceBody.innerHTML=a.map(x=>`<tr><td><b>${esc(x.user_name||('Пользователь №'+x.user_id))}</b></td><td>${esc(x.department||'—')}</td><td>${esc(x.card||'—')}</td><td>${timeOnly(x.arrival_time)}</td><td>${timeOnly(x.departure_time)}</td><td><span class="status-pill ${x.status==='at_work'?'status-present':'status-left'}">${x.status==='at_work'?'На работе':'Ушёл'}</span></td></tr>`).join('');
}

async function loadUsers(){USERS=await (await api('/api/users')).json();USERS_LOADED=true;usersBody.innerHTML=USERS.map(u=>`<tr><td>${u.id}</td><td>${esc(u.last_name+' '+u.first_name+' '+u.middle_name)}</td><td>${esc(u.department||'—')}</td><td>${esc(u.position)}</td><td>${esc(u.card)}</td><td>${u.controller_port||'—'}</td><td><button class="mini" onclick="editUser(${u.id})">Изменить</button> <button class="mini danger" onclick="deleteUser(${u.id})">Удалить</button></td></tr>`).join('');}

function departmentOptions(selected=''){
    let items=[...DEPARTMENTS];
    if(selected&&!items.includes(selected))items.push(selected);
    items.sort((a,b)=>a.localeCompare(b,'ru'));
    return `<option value="">Без отдела</option>`+items.map(name=>`<option value="${attr(name)}"${name===selected?' selected':''}>${esc(name)}</option>`).join('');
}

async function loadDepartments(render=true){
    DEPARTMENTS=await (await api('/api/departments')).json();
    if(userDepartment)userDepartment.innerHTML=departmentOptions(userDepartment.value||'');
    if(!render||!document.getElementById('departmentsBody'))return;
    if(!USERS_LOADED){USERS=await (await api('/api/users')).json();USERS_LOADED=true;}
    departmentsBody.innerHTML=DEPARTMENTS.map((name,i)=>{
        const count=USERS.filter(u=>u.department===name).length;
        return `<tr><td><input id="department-${i}" value="${attr(name)}" maxlength="120"></td><td>${count}</td><td><button class="mini" onclick="saveDepartment('${js(name)}',${i})">Сохранить</button> <button class="mini danger" onclick="deleteDepartment('${js(name)}')">Удалить</button></td></tr>`;
    }).join('');
}

async function addDepartment(){
    const name=newDepartmentName.value.trim();
    if(!name)return;
    let r=await api('/api/departments/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({old_name:'',name})});
    let j=await r.json();
    if(!j.ok){alert('Ошибка: '+(j.error||'не удалось добавить отдел'));return;}
    newDepartmentName.value='';
    await loadDepartments();
}

async function saveDepartment(oldName,index){
    const name=document.getElementById('department-'+index).value.trim();
    if(!name)return alert('Название отдела не может быть пустым');
    let r=await api('/api/departments/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({old_name:oldName,name})});
    let j=await r.json();
    if(!j.ok){alert('Ошибка: '+(j.error||'не удалось сохранить отдел'));return;}
    await loadUsers();
    await loadDepartments();
    await loadCards();
}

async function deleteDepartment(name){
    if(!confirm('Удалить отдел «'+name+'»?'))return;
    let r=await api('/api/departments/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({name})});
    let j=await r.json();
    if(!j.ok){alert(j.error==='department is used by users'?'Нельзя удалить отдел: он назначен одному или нескольким пользователям. Сначала измените отдел у этих пользователей.':'Ошибка: '+(j.error||'не удалось удалить отдел'));return;}
    await loadDepartments();
}

async function loadCards(){await loadUsers();let a=await (await api('/api/cards/active')).json();cardsBody.innerHTML=a.map(x=>`<tr><td><b>${esc(x.card)}</b></td><td>${x.user_id||'—'}</td><td>${esc(x.user_name||'Не привязана')}</td><td>${esc(x.department||'')}</td><td>${esc(x.last_read)}</td><td>${esc(x.last_event)}</td><td>${x.user_id?`<button class="mini" onclick="editUser(${x.user_id})">Пользователь</button> <button class="mini danger" onclick="removeCard('${js(x.card)}')">Отвязать</button>`:`<button class="mini" onclick="openAssign('${js(x.card)}')">Добавить / привязать</button>`}</td></tr>`).join('');}
async function loadControllers(){let a=await (await api('/api/controllers')).json();controllersBody.innerHTML=a.map(c=>`<tr><td>${c.node}</td><td><input value="${attr(c.name)}" onchange="renameController(${c.node},this.value)"></td><td>${esc(c.model)}</td><td class="${c.online?'ok':'bad'}">${c.online?'ONLINE':'OFFLINE'}</td><td>${esc(c.last_seen||'')}</td><td><code>${esc(c.last_raw_hex||'')}</code></td></tr>`).join('');}

async function editUser(id=0){
    if(!USERS_LOADED)await loadUsers();
    await loadDepartments(false);
    let u=USERS.find(x=>x.id===id)||{id:0,enabled:true,last_name:'',first_name:'',middle_name:'',department:'',position:'',card:'',controller_port:0};
    userDepartment.innerHTML=departmentOptions(u.department||'');
    for(let [k,v] of Object.entries(u)){let e=userForm.elements[k];if(!e)continue;if(e.type==='checkbox')e.checked=!!v;else e.value=v??'';}
    userDepartment.value=u.department||'';
    userDialog.showModal();
}

async function saveUser(){let f=new FormData(userForm),o=Object.fromEntries(f.entries());o.enabled=userForm.elements.enabled.checked?'1':'0';await api('/api/users/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(o)});userDialog.close();await loadUsers();await loadDepartments(false);await loadCards();refreshStatus();}
async function deleteUser(id){if(!confirm('Удалить пользователя №'+id+'?'))return;await api('/api/users/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({id})});await loadUsers();await loadCards();refreshStatus();}
function openAssign(card){assignForm.card.value=card;assignUsers.innerHTML=USERS.map(u=>`<option value="${u.id}">${u.id} — ${esc(u.last_name+' '+u.first_name)} (${esc(u.department)})</option>`).join('');assignDialog.showModal();}
async function assignCard(){let o=Object.fromEntries(new FormData(assignForm));await api('/api/cards/assign',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(o)});assignDialog.close();await loadCards();}
async function removeCard(card){if(!confirm('Отвязать карту '+card+' от пользователя?'))return;await api('/api/cards/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({card})});await loadCards();}
async function renameController(node,name){await api('/api/controllers/name',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({node,name})});}
async function simulate(){if(!simCard.value)return;let r=await api('/api/simulate',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({card:simCard.value})});let j=await r.json();alert('Событие: '+j.event);refreshStatus();loadTodayAttendance();}
async function importUsers(file){if(!file)return;let text=await file.text();let r=await api('/api/import/users',{method:'POST',headers:{'Content-Type':'text/csv'},body:text});let j=await r.json();alert(j.ok?'Импорт выполнен':'Ошибка: '+j.error);await loadUsers();await loadDepartments(false);}
async function importSettings(file){if(!file)return;let text=await file.text();let r=await api('/api/import/settings',{method:'POST',headers:{'Content-Type':'text/plain'},body:text});let j=await r.json();alert(j.ok?'Настройки импортированы. Перезапустите службу.':'Ошибка импорта');}
settingsForm.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(settingsForm));o.telegram_enabled=settingsForm.telegram_enabled.checked?'1':'0';let r=await api('/api/settings/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(o)});let j=await r.json();settingsMsg.textContent=j.ok?'Сохранено. Для порта/COM выполните перезапуск службы.':'Ошибка';}
async function testTelegram(){let r=await api('/api/telegram/test',{method:'POST'}),j=await r.json();alert(j.ok?'Сообщение отправлено':'Ошибка: '+j.error)}
async function logout(){await api('/api/logout');location='/login.html'}
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}function attr(s){return esc(s)}function js(s){return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'")}

refreshStatus();loadTodayAttendance();loadUsers();loadDepartments(false);setInterval(refreshStatus,3000);setInterval(loadTodayAttendance,5000);
