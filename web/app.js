
// Guaranteed visual feedback for every current and future button/link styled as a button.
function showButtonPress(target){
    const b=target&&target.closest?target.closest('button,.button'):null;
    if(!b||b.disabled||b.classList.contains('disabled'))return;
    clearTimeout(b._skudPressTimer);
    b.classList.remove('skud-pressed');
    void b.offsetWidth;
    b.classList.add('skud-pressed');
    b._skudPressTimer=setTimeout(()=>b.classList.remove('skud-pressed'),240);
}
document.addEventListener('pointerdown',e=>showButtonPress(e.target),true);
document.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' ')showButtonPress(e.target)},true);

let USERS=[];
let CONTROLLERS=[];
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
    todayAttendanceBody.innerHTML=a.map(x=>`<tr><td><b>${esc(x.user_name||('Пользователь №'+x.user_id))}</b></td><td>${esc(x.department||'—')}</td><td>${esc(cardTextFromRaw(x.card||''))}</td><td>${timeOnly(x.arrival_time)}</td><td>${timeOnly(x.departure_time)}</td><td><span class="status-pill ${x.status==='at_work'?'status-present':'status-left'}">${x.status==='at_work'?'На работе':'Ушёл'}</span></td></tr>`).join('');
}

function cardDisplay(u){if(u&&u.card_series&&u.card_number!==undefined&&u.card_number!==null&&u.card_number!=='')return `${u.card_series} / ${u.card_number}`;return u&&u.card?cardTextFromRaw(u.card):'—';}
function cardTextFromRaw(card){const s=String(card||'');let m=s.match(/^0x([0-9A-Fa-f]{1,4}):(\d+)$/);if(m)return m[1].toUpperCase().padStart(4,'0')+' / '+m[2];m=s.match(/^([0-9A-Fa-f]{1,4}):(\d+)$/);if(m&&/[A-Fa-f]/.test(m[1]))return m[1].toUpperCase().padStart(4,'0')+' / '+m[2];return s||'—';}
function accessModeText(mode){return ({card:'Только карта',card_or_pin:'Карта ИЛИ PIN',card_and_pin:'Карта + PIN'})[mode]||'Только карта';}
async function loadUsers(){USERS=await (await api('/api/users')).json();USERS_LOADED=true;usersBody.innerHTML=USERS.map(u=>`<tr><td>${u.id}</td><td>${esc(u.last_name+' '+u.first_name+' '+u.middle_name)}</td><td>${esc(u.department||'—')}</td><td>${esc(u.position)}</td><td><b>${esc(cardDisplay(u))}</b></td><td>${u.pin_code?'<span class="status-pill status-present">PIN задан</span>':'—'}<small class="table-subtext">${esc(accessModeText(u.access_mode))}</small></td><td>${u.controller_port||'—'}</td><td><button class="mini" onclick="editUser(${u.id})">Изменить</button> <button class="mini danger" onclick="deleteUser(${u.id})">Удалить</button></td></tr>`).join('');}

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

async function loadCards(){await loadUsers();let a=await (await api('/api/cards/active')).json();cardsBody.innerHTML=a.map(x=>`<tr><td><b>${esc(cardTextFromRaw(x.card))}</b></td><td>${x.user_id||'—'}</td><td>${esc(x.user_name||'Не привязана')}</td><td>${esc(x.department||'')}</td><td>${esc(x.last_read)}</td><td>${esc(x.last_event)}</td><td>${x.user_id?`<button class="mini" onclick="editUser(${x.user_id})">Пользователь</button> <button class="mini danger" onclick="removeCard('${js(x.card)}')">Отвязать</button>`:`<button class="mini" onclick="openAssign('${js(x.card)}')">Добавить / привязать</button>`}</td></tr>`).join('');}
async function loadControllers(){CONTROLLERS=await (await api('/api/controllers')).json();if(window.controllersBody)controllersBody.innerHTML=CONTROLLERS.map(c=>`<tr><td>${c.node}</td><td><input value="${attr(c.name)}" onchange="renameController(${c.node},this.value)"></td><td>${esc(c.model)}</td><td class="${c.online?'ok':'bad'}">${c.online?'ONLINE':'OFFLINE'}</td><td>${esc(c.last_seen||'')}</td><td><code>${esc(c.last_raw_hex||'')}</code></td></tr>`).join('');return CONTROLLERS;}

function userDisplayName(u){return [u.last_name,u.first_name,u.middle_name].filter(Boolean).join(' ')||('Пользователь №'+u.id);}
function controllerDisplayName(c){return c.name||('Контроллер '+c.node);}

async function openUserUpload(){
    if(!USERS_LOADED)await loadUsers();
    await loadControllers();
    uploadAllUsers.checked=true;uploadAllControllers.checked=true;
    uploadUsersList.innerHTML=USERS.length?USERS.map(u=>`<label class="check selection-item"><input type="checkbox" class="upload-user" value="${u.id}" checked> <span><b>${u.id} — ${esc(userDisplayName(u))}</b><small>${esc(u.department||'Без отдела')} · карта ${esc(cardDisplay(u))} · ${u.pin_code?'PIN задан · ':''}${esc(accessModeText(u.access_mode))} · порт ${u.controller_port||'не задан'}</small></span></label>`).join(''):'<div class="muted">Пользователей нет</div>';
    uploadControllersList.innerHTML=CONTROLLERS.length?CONTROLLERS.map(c=>`<label class="check selection-item"><input type="checkbox" class="upload-controller" value="${c.node}" checked> <span><b>${c.node} — ${esc(controllerDisplayName(c))}</b><small>${c.online?'ONLINE':'OFFLINE'} · ${esc(c.model||'UNEX 721')}</small></span></label>`).join(''):'<div class="muted">Контроллеры ещё не обнаружены</div>';
    uploadResult.innerHTML='';
    toggleUploadSelection('users');toggleUploadSelection('controllers');updateUploadSummary();
    uploadDialog.showModal();
}

function toggleUploadSelection(kind){
    const all=kind==='users'?uploadAllUsers:uploadAllControllers;
    const selector=kind==='users'?'.upload-user':'.upload-controller';
    document.querySelectorAll(selector).forEach(x=>{x.disabled=all.checked;if(all.checked)x.checked=true;});
    updateUploadSummary();
}

function selectedUploadValues(selector){return [...document.querySelectorAll(selector+':checked')].map(x=>Number(x.value)).filter(Boolean);}
function updateUploadSummary(){
    if(!window.uploadSelectionSummary)return;
    const userCount=uploadAllUsers.checked?USERS.length:selectedUploadValues('.upload-user').length;
    const controllerCount=uploadAllControllers.checked?CONTROLLERS.length:selectedUploadValues('.upload-controller').length;
    uploadSelectionSummary.textContent=`Будет подготовлено записей: ${userCount} × ${controllerCount} = ${userCount*controllerCount}`;
}
document.addEventListener('change',e=>{if(e.target.matches&&e.target.matches('.upload-user,.upload-controller'))updateUploadSummary();});

function uploadStatusText(status){return ({ok:'Записан',skipped:'Пропущен',blocked_protocol:'Заблокировано',error:'Ошибка'})[status]||status;}
function renderUserUploadJob(job){
    const state=({queued:'В очереди',running:'Выполняется',completed:'Завершено',blocked:'Аппаратная запись заблокирована'})[job.state]||job.state;
    const rows=(job.results||[]).map(r=>{const u=USERS.find(x=>x.id===r.user_id);const c=CONTROLLERS.find(x=>x.node===r.controller_node);const cls=r.status==='ok'?'ok':r.status==='skipped'?'muted':'bad';return `<tr><td>${r.user_id} — ${esc(u?userDisplayName(u):'')}</td><td>${r.controller_node} — ${esc(c?controllerDisplayName(c):'')}</td><td class="${cls}">${esc(uploadStatusText(r.status))}</td><td>${esc(r.message||'')}</td></tr>`;}).join('');
    uploadResult.innerHTML=`<div class="upload-job-state"><b>${esc(state)}</b> · ${job.completed}/${job.total} · успешно ${job.success} · пропущено ${job.skipped} · ошибок/блокировок ${job.failed}</div>${rows?`<div class="upload-result-table"><table><thead><tr><th>Пользователь</th><th>Контроллер</th><th>Результат</th><th>Комментарий</th></tr></thead><tbody>${rows}</tbody></table></div>`:''}`;
}

async function pollUserUpload(jobId){
    for(let n=0;n<120;n++){
        const r=await api('/api/controllers/upload-users/status?job_id='+encodeURIComponent(jobId));
        if(!r.ok)return;
        const job=await r.json();renderUserUploadJob(job);
        if(job.state!=='queued'&&job.state!=='running')return;
        await new Promise(resolve=>setTimeout(resolve,700));
    }
}

async function startUserUpload(){
    const userIds=selectedUploadValues('.upload-user');const nodes=selectedUploadValues('.upload-controller');
    if(!uploadAllUsers.checked&&!userIds.length)return alert('Выберите хотя бы одного пользователя');
    if(!uploadAllControllers.checked&&!nodes.length)return alert('Выберите хотя бы один контроллер');
    if(uploadAllUsers.checked&&!USERS.length)return alert('Нет пользователей для выгрузки');
    if(uploadAllControllers.checked&&!CONTROLLERS.length)return alert('Нет обнаруженных контроллеров');
    if(!confirm(`Выгрузить ${uploadAllUsers.checked?'всех':'выбранных'} пользователей в ${uploadAllControllers.checked?'все':'выбранные'} контроллеры?`))return;
    startUploadButton.disabled=true;uploadResult.innerHTML='<div class="muted">Создание задания...</div>';
    try{
        const r=await api('/api/controllers/upload-users',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc({all_users:uploadAllUsers.checked?'1':'0',user_ids:userIds.join(','),all_controllers:uploadAllControllers.checked?'1':'0',controller_nodes:nodes.join(',')})});
        const j=await r.json();
        if(!r.ok||!j.ok){uploadResult.innerHTML='<div class="bad">Ошибка: '+esc(j.error||'не удалось создать задание')+'</div>';return;}
        if(!j.protocol_ready)uploadResult.innerHTML='<div class="protocol-warning"><b>Аппаратная запись недоступна.</b><br>'+esc(j.protocol_message||'Протокол записи не готов')+'</div>';else uploadResult.innerHTML='<div class="upload-job-state"><b>SOYAL Extended Protocol активен.</b><br>'+esc(j.protocol_message||'')+'</div>';
        await pollUserUpload(j.job_id);
    }finally{startUploadButton.disabled=false;}
}

async function openUserDelete(preselectedId=0){
    if(!USERS_LOADED)await loadUsers();
    await loadControllers();
    deleteFromSystem.checked=true;
    deleteFromControllers.checked=true;
    deleteAllUsers.checked=false;
    deleteAllControllers.checked=true;
    deleteUsersList.innerHTML=USERS.length?USERS.map(u=>`<label class="check selection-item"><input type="checkbox" class="delete-user" value="${u.id}" ${preselectedId===u.id?'checked':''}> <span><b>${u.id} — ${esc(userDisplayName(u))}</b><small>${esc(u.department||'Без отдела')} · карта ${esc(cardDisplay(u))} · ${u.pin_code?'PIN задан · ':''}${esc(accessModeText(u.access_mode))} · порт ${u.controller_port||'не задан'}</small></span></label>`).join(''):'<div class="muted">Пользователей нет</div>';
    deleteControllersList.innerHTML=CONTROLLERS.length?CONTROLLERS.map(c=>`<label class="check selection-item"><input type="checkbox" class="delete-controller" value="${c.node}" checked> <span><b>${c.node} — ${esc(controllerDisplayName(c))}</b><small>${c.online?'ONLINE':'OFFLINE'} · ${esc(c.model||'UNEX 721')}</small></span></label>`).join(''):'<div class="muted">Контроллеры ещё не обнаружены</div>';
    deleteResult.innerHTML='';
    toggleDeleteSelection('controllers');
    updateDeleteTargetUI();
    updateDeleteSummary();
    deleteUsersDialog.showModal();
}

function toggleDeleteSelection(kind){
    const all=kind==='users'?deleteAllUsers:deleteAllControllers;
    const selector=kind==='users'?'.delete-user':'.delete-controller';
    document.querySelectorAll(selector).forEach(x=>{x.disabled=all.checked;if(all.checked)x.checked=true;});
    updateDeleteSummary();
}

function updateDeleteTargetUI(){
    if(!window.deleteControllersPanel)return;
    const useControllers=deleteFromControllers.checked;
    deleteControllersPanel.classList.toggle('disabled-panel',!useControllers);
    deleteAllControllers.disabled=!useControllers;
    document.querySelectorAll('.delete-controller').forEach(x=>x.disabled=!useControllers||deleteAllControllers.checked);
    updateDeleteSummary();
}

function updateDeleteSummary(){
    if(!window.deleteSelectionSummary)return;
    const userCount=deleteAllUsers.checked?USERS.length:selectedUploadValues('.delete-user').length;
    const deleteSystem=deleteFromSystem.checked;
    const deleteControllers=deleteFromControllers.checked;
    const controllerCount=deleteControllers?(deleteAllControllers.checked?CONTROLLERS.length:selectedUploadValues('.delete-controller').length):0;
    let text=`Выбрано пользователей: ${userCount}. `;
    if(deleteSystem&&deleteControllers)text+=`Удаление из системы после подтверждения ${userCount} × ${controllerCount} = ${userCount*controllerCount} операций в контроллерах.`;
    else if(deleteControllers)text+=`Удаление только из контроллеров: ${userCount} × ${controllerCount} = ${userCount*controllerCount} операций.`;
    else if(deleteSystem)text+=`Удаление только из системы: ${userCount} пользователей.`;
    else text+='Не выбрано место удаления.';
    deleteSelectionSummary.textContent=text;
}

document.addEventListener('change',e=>{
    if(e.target.matches&&e.target.matches('.delete-user,.delete-controller'))updateDeleteSummary();
});

function deleteStatusText(status){return ({ok:'Удалён',error:'Ошибка'})[status]||status;}
function renderUserDeleteJob(job){
    const state=({queued:'В очереди',running:'Удаление из контроллеров',completed:'Завершено'})[job.state]||job.state;
    const rows=(job.results||[]).map(r=>{
        const u=USERS.find(x=>x.id===r.user_id);
        const c=CONTROLLERS.find(x=>x.node===r.controller_node);
        const cls=r.status==='ok'?'ok':'bad';
        return `<tr><td>${r.user_id} — ${esc(u?userDisplayName(u):'')}</td><td>${r.controller_node} — ${esc(c?controllerDisplayName(c):'')}</td><td class="${cls}">${esc(deleteStatusText(r.status))}</td><td>${esc(r.message||'')}</td></tr>`;
    }).join('');
    const local=job.delete_from_system?`<div class="delete-local-note ${job.local_retained?'bad':'ok'}">Из системы удалено: <b>${job.local_deleted}</b>. Оставлено из-за ошибок/неподтверждённого удаления в контроллерах: <b>${job.local_retained}</b>.</div>`:'';
    deleteResult.innerHTML=`<div class="upload-job-state"><b>${esc(state)}</b> · ${job.completed}/${job.total} · успешно ${job.success} · ошибок ${job.failed}</div>${local}${rows?`<div class="upload-result-table"><table><thead><tr><th>Пользователь</th><th>Контроллер</th><th>Результат</th><th>Комментарий</th></tr></thead><tbody>${rows}</tbody></table></div>`:''}`;
}

async function pollUserDelete(jobId){
    for(let n=0;n<180;n++){
        const r=await api('/api/users/delete-selected/status?job_id='+encodeURIComponent(jobId));
        if(!r.ok)return null;
        const job=await r.json();
        renderUserDeleteJob(job);
        if(job.state!=='queued'&&job.state!=='running')return job;
        await new Promise(resolve=>setTimeout(resolve,700));
    }
    return null;
}

async function startUserDelete(){
    const userIds=selectedUploadValues('.delete-user');
    const nodes=selectedUploadValues('.delete-controller');
    const deleteSystem=deleteFromSystem.checked;
    const deleteControllers=deleteFromControllers.checked;
    if(!deleteSystem&&!deleteControllers)return alert('Выберите: удалить из системы и/или из контроллеров');
    if(!deleteAllUsers.checked&&!userIds.length)return alert('Выберите хотя бы одного пользователя');
    if(deleteAllUsers.checked&&!USERS.length)return alert('Нет пользователей для удаления');
    if(deleteControllers&&!deleteAllControllers.checked&&!nodes.length)return alert('Выберите хотя бы один контроллер');
    if(deleteControllers&&deleteAllControllers.checked&&!CONTROLLERS.length)return alert('Нет обнаруженных контроллеров');

    const userCount=deleteAllUsers.checked?USERS.length:userIds.length;
    const target=deleteSystem&&deleteControllers?'из системы И контроллеров':deleteSystem?'из системы':'из контроллеров';
    const extra=deleteControllers?`\nКонтроллеры: ${deleteAllControllers.checked?'все':nodes.join(', ')}`:'';
    if(!confirm(`Удалить ${userCount} пользователь(ей) ${target}?${extra}\n\nОперация необратима.`))return;

    startDeleteButton.disabled=true;
    deleteResult.innerHTML='<div class="muted">Подготовка удаления...</div>';
    try{
        const r=await api('/api/users/delete-selected',{
            method:'POST',
            headers:{'Content-Type':'application/x-www-form-urlencoded'},
            body:enc({
                all_users:deleteAllUsers.checked?'1':'0',
                user_ids:userIds.join(','),
                delete_system:deleteSystem?'1':'0',
                delete_controllers:deleteControllers?'1':'0',
                all_controllers:deleteAllControllers.checked?'1':'0',
                controller_nodes:nodes.join(',')
            })
        });
        const j=await r.json();
        if(!r.ok||!j.ok){
            deleteResult.innerHTML='<div class="bad">Ошибка: '+esc(j.error||'не удалось начать удаление')+'</div>';
            return;
        }
        if(j.immediate){
            deleteResult.innerHTML=`<div class="upload-job-state ok"><b>Удаление завершено.</b> Из системы удалено: ${j.local_deleted}.</div>`;
        }else{
            await pollUserDelete(j.job_id);
        }
        await loadUsers();
        await loadDepartments(false);
        await loadCards();
        await loadTodayAttendance();
        refreshStatus();
    }finally{
        startDeleteButton.disabled=false;
    }
}


async function editUser(id=0){
    if(!USERS_LOADED)await loadUsers();
    await loadDepartments(false);
    let u=USERS.find(x=>x.id===id)||{id:0,enabled:true,last_name:'',first_name:'',middle_name:'',department:'',position:'',card:'',card_series:'',card_number:'',pin_code:'',access_mode:'card',controller_port:0};
    userDepartment.innerHTML=departmentOptions(u.department||'');
    for(let [k,v] of Object.entries(u)){let e=userForm.elements[k];if(!e)continue;if(e.type==='checkbox')e.checked=!!v;else e.value=v??'';}
    userDepartment.value=u.department||'';
    userForm.elements.pin_code.value=u.pin_code||'';
    userForm.elements.access_mode.value=u.access_mode||'card';
    userDialog.showModal();
}

async function saveUser(){
    let f=new FormData(userForm),o=Object.fromEntries(f.entries());o.enabled=userForm.elements.enabled.checked?'1':'0';
    o.card_series=(o.card_series||'').trim().toUpperCase();o.card_number=(o.card_number||'').trim();o.pin_code=(o.pin_code||'').trim();
    if((o.card_series&&!o.card_number)||(!o.card_series&&o.card_number))return alert('Укажите одновременно серию и номер карты');
    if(o.card_series&&!/^[0-9A-F]{1,4}$/.test(o.card_series))return alert('Серия карты: 1–4 HEX символа, например B112');
    if(o.card_number&&!/^\d{1,5}$/.test(o.card_number))return alert('Номер карты должен быть десятичным числом 0..65535');
    if(o.card_number&&Number(o.card_number)>65535)return alert('Номер карты не может быть больше 65535');
    if(o.pin_code&&(!/^\d{4}$/.test(o.pin_code)||Number(o.pin_code)<1))return alert('PIN должен содержать 4 цифры: 0001..9999');
    if((o.access_mode==='card_or_pin'||o.access_mode==='card_and_pin')&&!o.pin_code)return alert('Для выбранного режима доступа укажите PIN');
    let r=await api('/api/users/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(o)});let j=await r.json();if(!r.ok||!j.ok)return alert('Ошибка: '+(j.error||'не удалось сохранить пользователя'));
    userDialog.close();await loadUsers();await loadDepartments(false);await loadCards();refreshStatus();
}
async function deleteUser(id){await openUserDelete(id);}
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
