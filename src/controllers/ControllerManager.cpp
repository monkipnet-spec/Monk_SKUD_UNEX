#include "skud/ControllerManager.h"
#include "skud/AttendanceEngine.h"
#include "skud/Config.h"
#include "skud/SerialPort.h"
#include "skud/Unex721Protocol.h"
#include "skud/MariaDbUserStore.h"
#include "skud/UserManager.h"
#include "skud/Util.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace skud {
namespace {
std::string traceTimestamp(){
    using namespace std::chrono;
    const auto now=system_clock::now();
    const auto ms=duration_cast<milliseconds>(now.time_since_epoch())%1000;
    const auto t=system_clock::to_time_t(now);
    std::tm tm{};localtime_r(&t,&tm);
    std::ostringstream o;o<<std::put_time(&tm,"%H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<ms.count();
    return o.str();
}
std::string userFullName(const User& u){
    std::string n=u.last_name;
    if(!u.first_name.empty()){if(!n.empty())n+=' ';n+=u.first_name;}
    if(!u.middle_name.empty()){if(!n.empty())n+=' ';n+=u.middle_name;}
    return n.empty()?("Пользователь №"+std::to_string(u.id)):n;
}
std::uint32_t localPinValue(const User& u){
    if(u.pin_code.empty())return 0;
    try{return static_cast<std::uint32_t>(std::stoul(u.pin_code));}catch(...){return 0;}
}
struct EepromPattern { std::string name; std::vector<std::uint8_t> bytes; bool exact{true}; };
std::vector<std::uint8_t> bcdDigits(std::string digits){
    if(digits.size()%2)digits="0"+digits;
    std::vector<std::uint8_t> out;out.reserve(digits.size()/2);
    for(std::size_t i=0;i<digits.size();i+=2)out.push_back(static_cast<std::uint8_t>((digits[i]-'0')*16+(digits[i+1]-'0')));
    return out;
}
std::vector<EepromPattern> eepromPatterns(int series,int number){
    std::vector<EepromPattern> p;
    auto add=[&](std::string n,std::vector<std::uint8_t> b,bool exact=true){
        if(b.empty())return;
        for(const auto&x:p)if(x.bytes==b&&x.exact==exact)return;
        p.push_back({std::move(n),std::move(b),exact});
    };
    const auto sh=static_cast<std::uint8_t>((series>>8)&0xFF),sl=static_cast<std::uint8_t>(series&0xFF);
    const auto nh=static_cast<std::uint8_t>((number>>8)&0xFF),nl=static_cast<std::uint8_t>(number&0xFF);
    add("S16+N16 BE",{sh,sl,nh,nl});
    add("S16+N16 LE",{sl,sh,nl,nh});
    if(series<=255){add("S8+N16 BE",{sl,nh,nl});add("S8+N16 LE",{sl,nl,nh});}
    const auto dec32=static_cast<std::uint64_t>(series)*100000ULL+static_cast<std::uint64_t>(number);
    if(dec32<=0xFFFFFFFFULL){
        const auto v=static_cast<std::uint32_t>(dec32);
        add("DEC(series*100000+number) BE",{static_cast<std::uint8_t>(v>>24),static_cast<std::uint8_t>(v>>16),static_cast<std::uint8_t>(v>>8),static_cast<std::uint8_t>(v)});
        add("DEC(series*100000+number) LE",{static_cast<std::uint8_t>(v),static_cast<std::uint8_t>(v>>8),static_cast<std::uint8_t>(v>>16),static_cast<std::uint8_t>(v>>24)});
    }
    std::ostringstream s3,n5,s5;s3<<std::setw(3)<<std::setfill('0')<<series;n5<<std::setw(5)<<std::setfill('0')<<number;s5<<std::setw(5)<<std::setfill('0')<<series;
    add("BCD 3+5",bcdDigits(s3.str()+n5.str()));
    add("BCD 5+5",bcdDigits(s5.str()+n5.str()));
    const auto ascii=std::to_string(series)+":"+std::to_string(number);
    add("ASCII series:number",std::vector<std::uint8_t>(ascii.begin(),ascii.end()));
    add("N16 BE (частичное)",{nh,nl},false);
    add("N16 LE (частичное)",{nl,nh},false);
    return p;
}
std::string hexSlice(const std::vector<std::uint8_t>& data,std::size_t begin,std::size_t end){
    if(begin>=end||begin>=data.size())return{};end=std::min(end,data.size());
    return util::hex(std::vector<std::uint8_t>(data.begin()+static_cast<std::ptrdiff_t>(begin),data.begin()+static_cast<std::ptrdiff_t>(end)));
}
}
ControllerManager::ControllerManager(Config&c,AttendanceEngine&a,UserManager&u,std::string p):cfg_(c),attendance_(a),users_(u),path_(std::move(p)){
    const auto root=std::filesystem::path(path_).parent_path().parent_path();
    controller_cards_path_=(root/"data"/"controller_cards.csv").string();
    if(usingMariaDb()){db_=std::make_unique<MariaDbUserStore>(cfg_);std::string err;if(!db_->init(err)){std::lock_guard lk(storage_mu_);storage_error_=err;}}
}
ControllerManager::~ControllerManager(){stop();}
bool ControllerManager::usingMariaDb()const{return cfg_.getBool("database.enabled",false);}
bool ControllerManager::backupAndRemove(const std::string& file,const std::string& label,std::string& error)const{
    if(!std::filesystem::exists(file))return true;std::error_code ec;auto backup=std::filesystem::path(file).parent_path().parent_path()/"backup";std::filesystem::create_directories(backup,ec);if(ec){error=ec.message();return false;}
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};localtime_r(&now,&tm);std::ostringstream ts;ts<<std::put_time(&tm,"%Y%m%d-%H%M%S");auto dst=backup/(label+".pre-mariadb-"+ts.str());std::filesystem::copy_file(file,dst,std::filesystem::copy_options::overwrite_existing,ec);if(ec){error=ec.message();return false;}std::filesystem::remove(file,ec);if(ec){error=ec.message();return false;}return true;
}
bool ControllerManager::loadControllers(){
    auto readCsv=[&](std::vector<Controller>&out){std::ifstream f(path_);if(!f)return false;std::string l;bool first=true;while(std::getline(f,l)){if(first){first=false;continue;}auto c=util::split(l,';');if(c.size()<4)continue;try{Controller x;x.node=std::stoi(c[0]);x.name=c[1];x.model=c[2];x.enabled=c[3]!="0";out.push_back(std::move(x));}catch(...){}}return true;};
    if(!usingMariaDb()){std::vector<Controller> tmp;if(!readCsv(tmp))return false;{std::lock_guard lk(mu_);controllers_=std::move(tmp);}loadControllerCards();return true;}
    if(!db_){std::lock_guard lk(storage_mu_);storage_error_="MariaDB backend is not initialized";return false;}std::string err;std::vector<Controller> db_rows;if(!db_->loadControllers(db_rows,err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}
    std::vector<Controller> csv_rows;const bool csv_exists=readCsv(csv_rows);if(csv_exists&&cfg_.getBool("database.migrate_runtime_csv",true)){
        for(const auto&c:csv_rows){auto it=std::find_if(db_rows.begin(),db_rows.end(),[&](const Controller&d){return d.node==c.node;});if(it==db_rows.end())db_rows.push_back(c);}
        if(!db_->saveControllers(db_rows,err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}std::vector<Controller> verify;if(!db_->loadControllers(verify,err)||verify.size()!=db_rows.size()){std::lock_guard lk(storage_mu_);storage_error_="controllers migration verification failed: "+err;return false;}db_rows=std::move(verify);
        if(cfg_.getBool("database.remove_csv_after_migration",true)&&!backupAndRemove(path_,"controllers.csv",err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}
    }
    {std::lock_guard lk(mu_);controllers_=std::move(db_rows);}if(!loadControllerCards())return false;return true;
}
bool ControllerManager::saveControllers()const{
    std::vector<Controller> snapshot;{std::lock_guard lk(mu_);snapshot=controllers_;}
    if(usingMariaDb()){if(!db_)return false;std::string err;const bool ok=db_->saveControllers(snapshot,err);if(!ok){std::lock_guard lk(storage_mu_);storage_error_=err;}return ok;}
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());auto tmp=path_+".tmp";std::ofstream f(tmp);if(!f)return false;f<<"node;name;model;enabled\n";for(auto&x:snapshot)f<<x.node<<';'<<x.name<<';'<<x.model<<';'<<(x.enabled?1:0)<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec){std::filesystem::remove(path_,ec);ec.clear();std::filesystem::rename(tmp,path_,ec);}return !ec;
}
void ControllerManager::start(){if(running_.exchange(true))return;thread_=std::thread(&ControllerManager::loop,this);}
void ControllerManager::stop(){running_=false;if(thread_.joinable())thread_.join();}
std::vector<Controller>ControllerManager::controllers()const{std::lock_guard lk(mu_);return controllers_;}
bool ControllerManager::renameController(int node,const std::string&name){{std::lock_guard lk(mu_);bool ok=false;for(auto&c:controllers_)if(c.node==node){c.name=name;ok=true;}if(!ok)return false;}return saveControllers();}
std::string ControllerManager::serialStatus()const{std::lock_guard lk(mu_);return serial_status_;}
std::string ControllerManager::serialDevice()const{std::lock_guard lk(mu_);return serial_device_;}
void ControllerManager::setRawEventCallback(RawEventFn fn){std::lock_guard lk(mu_);raw_cb_=std::move(fn);}

bool ControllerManager::loadControllerCards(){
    auto readCsv=[&](std::vector<ControllerCardRecord>&out){std::ifstream f(controller_cards_path_);if(!f)return false;std::string line;bool first=true;while(std::getline(f,line)){if(first){first=false;continue;}if(util::trim(line).empty())continue;auto c=util::split(line,';');if(c.size()<7)continue;try{ControllerCardRecord x;x.card=c[0];x.controller_node=std::stoi(c[1]);x.controller_name=c[2];x.first_seen=c[3];x.last_seen=c[4];x.read_count=static_cast<std::uint64_t>(std::stoull(c[5]));x.last_raw_hex=c[6];std::uint16_t series=0,number=0;if(!util::parseCardId(x.card,series,number,nullptr))continue;x.card=util::formatCardId(series,number);out.push_back(std::move(x));}catch(...){}}return true;};
    std::vector<ControllerCardRecord> rows;
    if(usingMariaDb()){
        if(!db_)return false;std::string err;if(!db_->loadControllerCards(rows,err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}std::vector<ControllerCardRecord> csv;const bool csv_exists=readCsv(csv);if(csv_exists&&cfg_.getBool("database.migrate_runtime_csv",true)){
            for(const auto&x:csv){auto it=std::find_if(rows.begin(),rows.end(),[&](const ControllerCardRecord&y){return y.controller_node==x.controller_node&&y.card==x.card;});if(it==rows.end())rows.push_back(x);else if(it->last_seen<x.last_seen)*it=x;}
            if(!db_->saveControllerCards(rows,err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}std::vector<ControllerCardRecord> verify;if(!db_->loadControllerCards(verify,err)||verify.size()!=rows.size()){std::lock_guard lk(storage_mu_);storage_error_="controller cards migration verification failed: "+err;return false;}rows=std::move(verify);
            if(cfg_.getBool("database.remove_csv_after_migration",true)&&!backupAndRemove(controller_cards_path_,"controller_cards.csv",err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}
        }
    }else if(!readCsv(rows))return false;
    std::lock_guard lk(card_mu_);controller_cards_.clear();for(auto&x:rows)controller_cards_[std::to_string(x.controller_node)+"|"+x.card]=std::move(x);return true;
}

bool ControllerManager::saveControllerCards()const{
    std::vector<ControllerCardRecord> snapshot;{std::lock_guard lk(card_mu_);snapshot.reserve(controller_cards_.size());for(const auto&[_,x]:controller_cards_)snapshot.push_back(x);}
    if(usingMariaDb()){if(!db_)return false;std::string err;const bool ok=db_->saveControllerCards(snapshot,err);if(!ok){std::lock_guard lk(storage_mu_);storage_error_=err;}return ok;}
    const auto path=std::filesystem::path(controller_cards_path_);std::filesystem::create_directories(path.parent_path());const auto tmp=controller_cards_path_+".tmp";std::ofstream f(tmp,std::ios::trunc);if(!f)return false;auto safe=[](std::string v){for(char&c:v)if(c==';'||c=='\n'||c=='\r')c=' ';return v;};f<<"card;controller_node;controller_name;first_seen;last_seen;read_count;last_raw_hex\n";for(const auto&x:snapshot)f<<safe(x.card)<<';'<<x.controller_node<<';'<<safe(x.controller_name)<<';'<<safe(x.first_seen)<<';'<<safe(x.last_seen)<<';'<<x.read_count<<';'<<safe(x.last_raw_hex)<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,controller_cards_path_,ec);if(ec){std::filesystem::remove(controller_cards_path_,ec);ec.clear();std::filesystem::rename(tmp,controller_cards_path_,ec);}return !ec;
}

std::vector<ControllerCardRecord> ControllerManager::controllerCards()const{
    std::lock_guard lk(card_mu_);std::vector<ControllerCardRecord> out;out.reserve(controller_cards_.size());for(const auto&[_,x]:controller_cards_)out.push_back(x);
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){if(a.last_seen!=b.last_seen)return a.last_seen>b.last_seen;if(a.controller_node!=b.controller_node)return a.controller_node<b.controller_node;return a.card<b.card;});return out;
}

void ControllerManager::clearControllerCards(){
    {std::lock_guard lk(card_mu_);controller_cards_.clear();}
    saveControllerCards();
}

void ControllerManager::rememberControllerCard(const std::string& card,int node,const std::string& controller_name,const std::string& raw_hex){
    std::uint16_t series=0,number=0;if(!util::parseCardId(card,series,number,nullptr))return;const auto canonical=util::formatCardId(series,number);const auto now=util::nowLocal();
    {
        std::lock_guard lk(card_mu_);const auto key=std::to_string(node)+"|"+canonical;auto&x=controller_cards_[key];if(x.card.empty()){x.card=canonical;x.controller_node=node;x.first_seen=now;}x.controller_name=controller_name;x.last_seen=now;++x.read_count;x.last_raw_hex=raw_hex;
    }
    saveControllerCards();
}
void ControllerManager::appendProtocolTrace(std::string direction,int node,int command,std::string protocol,const std::vector<std::uint8_t>&frame,std::string message,std::string card,int user_address){
    ProtocolTraceEntry e;e.timestamp=traceTimestamp();e.direction=std::move(direction);e.node=node;e.command=command;e.protocol=std::move(protocol);e.raw_hex=util::hex(frame);e.message=std::move(message);e.card=std::move(card);e.user_address=user_address;
    std::lock_guard lk(trace_mu_);e.id=next_trace_id_++;trace_entries_.push_back(std::move(e));while(trace_entries_.size()>1000)trace_entries_.pop_front();
}
std::vector<ProtocolTraceEntry> ControllerManager::protocolTrace(std::uint64_t after_id,std::size_t limit)const{
    limit=std::clamp<std::size_t>(limit,1,500);std::vector<ProtocolTraceEntry> out;std::lock_guard lk(trace_mu_);
    for(const auto&e:trace_entries_)if(e.id>after_id){out.push_back(e);if(out.size()>=limit)break;}return out;
}
void ControllerManager::clearProtocolTrace(){std::lock_guard lk(trace_mu_);trace_entries_.clear();last_event_trace_raw_.clear();}

bool ControllerManager::userUploadProtocolReady()const{return Unex721Protocol::userWriteSupported();}
std::string ControllerManager::userUploadProtocolMessage()const{return Unex721Protocol::userWriteSupportMessage();}

void ControllerManager::finishBlockedUserUpload(ControllerUserUploadJob&job,const std::vector<User>&users,const std::vector<int>&controller_nodes)const{
    job.state="blocked";
    for(const auto&u:users){
        for(int node:controller_nodes){
            ControllerUserUploadResult r;r.user_id=u.id;r.controller_node=node;
            if(!u.enabled){r.status="skipped";r.message="Пользователь отключен";++job.skipped;}
            else if(u.card.empty()){r.status="skipped";r.message="У пользователя не задан номер карты";++job.skipped;}
            else if(u.controller_port<0||u.controller_port>1023){r.status="skipped";r.message="Адрес пользователя вне диапазона 0..1023";++job.skipped;}
            else{r.status="blocked_protocol";r.message=Unex721Protocol::userWriteSupportMessage();++job.failed;}
            ++job.completed;job.results.push_back(std::move(r));
        }
    }
}

std::uint64_t ControllerManager::queueUserUpload(std::vector<User>users,std::vector<int>controller_nodes,bool full_sync){
    std::lock_guard lk(upload_mu_);
    const auto id=next_upload_id_++;
    ControllerUserUploadJob job;job.id=id;job.created_at=util::nowLocal();job.state="queued";job.full_sync=full_sync;
    job.total=full_sync
        ? static_cast<int>(1025*controller_nodes.size())
        : static_cast<int>(users.size()*controller_nodes.size());
    if(!Unex721Protocol::userWriteSupported()){
        finishBlockedUserUpload(job,users,controller_nodes);
    }else{
        upload_queue_.push_back(PendingUserUpload{id,std::move(users),std::move(controller_nodes),full_sync});
    }
    upload_jobs_[id]=std::move(job);
    while(upload_jobs_.size()>20)upload_jobs_.erase(upload_jobs_.begin());
    return id;
}

std::optional<ControllerUserUploadJob> ControllerManager::userUploadJob(std::uint64_t id)const{
    std::lock_guard lk(upload_mu_);auto it=upload_jobs_.find(id);if(it==upload_jobs_.end())return std::nullopt;return it->second;
}

std::uint64_t ControllerManager::queueDisablePassAnyCards(int controller_node){
    std::lock_guard lk(action_mu_);
    const auto id=next_action_id_++;
    ControllerActionJob job;job.id=id;job.created_at=util::nowLocal();job.state="queued";job.controller_node=controller_node;
    action_queue_.push_back(PendingControllerAction{id,controller_node});
    action_jobs_[id]=std::move(job);
    while(action_jobs_.size()>20)action_jobs_.erase(action_jobs_.begin());
    return id;
}

std::optional<ControllerActionJob> ControllerManager::controllerActionJob(std::uint64_t id)const{
    std::lock_guard lk(action_mu_);auto it=action_jobs_.find(id);if(it==action_jobs_.end())return std::nullopt;return it->second;
}

std::uint64_t ControllerManager::queueUserDelete(std::vector<User> users,std::vector<int> controller_nodes,bool delete_from_system){
    std::lock_guard lk(delete_mu_);
    const auto id=next_delete_id_++;
    ControllerUserDeleteJob job;
    job.id=id;
    job.created_at=util::nowLocal();
    job.state="queued";
    job.delete_from_system=delete_from_system;
    job.total=static_cast<int>(users.size()*controller_nodes.size());
    delete_queue_.push_back(PendingUserDelete{id,std::move(users),std::move(controller_nodes),delete_from_system});
    delete_jobs_[id]=std::move(job);
    while(delete_jobs_.size()>20)delete_jobs_.erase(delete_jobs_.begin());
    return id;
}

std::optional<ControllerUserDeleteJob> ControllerManager::userDeleteJob(std::uint64_t id)const{
    std::lock_guard lk(delete_mu_);
    auto it=delete_jobs_.find(id);
    if(it==delete_jobs_.end())return std::nullopt;
    return it->second;
}


std::uint64_t ControllerManager::queueUserRead(std::vector<User> local_users,std::vector<int> controller_nodes,std::vector<int> addresses,bool include_empty){
    std::sort(addresses.begin(),addresses.end());
    addresses.erase(std::remove_if(addresses.begin(),addresses.end(),[](int a){return a<=0||a>16383;}),addresses.end());
    addresses.erase(std::unique(addresses.begin(),addresses.end()),addresses.end());

    std::lock_guard lk(read_mu_);
    const auto id=next_read_id_++;
    ControllerUserReadJob job;
    job.id=id;
    job.created_at=util::nowLocal();
    job.state=(controller_nodes.empty()||addresses.empty())?"completed":"queued";
    job.total=static_cast<int>(controller_nodes.size()*addresses.size());
    read_jobs_[id]=job;
    if(job.state=="queued"){
        PendingUserRead p;
        p.id=id;p.local_users=std::move(local_users);p.controller_nodes=std::move(controller_nodes);
        p.addresses=std::move(addresses);p.include_empty=include_empty;
        read_queue_.push_back(std::move(p));
    }
    while(read_jobs_.size()>20)read_jobs_.erase(read_jobs_.begin());
    return id;
}

std::optional<ControllerUserReadJob> ControllerManager::userReadJob(std::uint64_t id)const{
    std::lock_guard lk(read_mu_);
    auto it=read_jobs_.find(id);
    if(it==read_jobs_.end())return std::nullopt;
    return it->second;
}

std::uint64_t ControllerManager::queueEepromSearch(int card_series,int card_number,std::vector<int> controller_nodes,int start_address,int end_address,int block_size,std::vector<int> compact_user_addresses){
    std::sort(compact_user_addresses.begin(),compact_user_addresses.end());
    compact_user_addresses.erase(std::remove_if(compact_user_addresses.begin(),compact_user_addresses.end(),[](int a){return a<1||a>16383;}),compact_user_addresses.end());
    compact_user_addresses.erase(std::unique(compact_user_addresses.begin(),compact_user_addresses.end()),compact_user_addresses.end());
    std::lock_guard lk(eeprom_mu_);
    const auto id=next_eeprom_id_++;
    ControllerEepromSearchJob job;job.id=id;job.created_at=util::nowLocal();job.card_series=card_series;job.card_number=card_number;
    job.compact_user_addresses=compact_user_addresses;
    job.start_address=start_address;job.end_address=end_address;job.block_size=block_size;
    const int blocks=(end_address-start_address+block_size)/block_size;
    job.total=static_cast<int>(controller_nodes.size())*blocks;
    job.state=(controller_nodes.empty()||start_address>end_address)?"completed":"queued";
    eeprom_jobs_[id]=job;
    if(job.state=="queued"){
        PendingEepromSearch p;p.id=id;p.card_series=card_series;p.card_number=card_number;p.controller_nodes=std::move(controller_nodes);
        p.start_address=start_address;p.end_address=end_address;p.block_size=block_size;p.next_address=start_address;p.compact_user_addresses=std::move(compact_user_addresses);eeprom_queue_.push_back(std::move(p));
    }
    while(eeprom_jobs_.size()>12)eeprom_jobs_.erase(eeprom_jobs_.begin());
    return id;
}

std::optional<ControllerEepromSearchJob> ControllerManager::eepromSearchJob(std::uint64_t id)const{
    std::lock_guard lk(eeprom_mu_);auto it=eeprom_jobs_.find(id);if(it==eeprom_jobs_.end())return std::nullopt;return it->second;
}

void ControllerManager::processEepromSearchBatch(Unex721Protocol& proto){
    constexpr int batch_size=4;
    constexpr std::size_t max_results=500;
    for(int batch=0;batch<batch_size&&running_;++batch){
        std::uint64_t job_id=0;int node=0,start=0,end=0,block_size=64,series=0,number=0;bool last_task=false,need_compact_probe=false;
        std::vector<int> probe_addresses;
        std::vector<std::pair<int,std::vector<std::uint8_t>>> compact_records;
        {
            std::lock_guard lk(eeprom_mu_);
            if(eeprom_queue_.empty())return;
            auto& p=eeprom_queue_.front();auto it=eeprom_jobs_.find(p.id);
            if(it==eeprom_jobs_.end()){eeprom_queue_.pop_front();continue;}
            it->second.state="running";
            if(p.controller_index>=p.controller_nodes.size()){it->second.state="completed";eeprom_queue_.pop_front();continue;}
            job_id=p.id;node=p.controller_nodes[p.controller_index];start=p.next_address;end=p.end_address;block_size=p.block_size;series=p.card_series;number=p.card_number;
            if(!p.compact_user_addresses.empty()&&!p.compact_probed_nodes.count(node)){
                need_compact_probe=true;probe_addresses=p.compact_user_addresses;p.compact_probed_nodes.insert(node);
            }else{
                auto rit=p.compact_records.find(node);if(rit!=p.compact_records.end())compact_records=rit->second;
            }
            p.next_address+=block_size;
            if(p.next_address>end){++p.controller_index;p.next_address=p.start_address;}
            last_task=p.controller_index>=p.controller_nodes.size();
        }

        if(need_compact_probe){
            std::vector<std::string> notes;
            for(int address:probe_addresses){
                const auto u=proto.readUser(static_cast<std::uint8_t>(node),address);
                std::ostringstream note;note<<"Node "<<node<<", user "<<address<<": ";
                if(!u.ok){note<<"87H ERROR — "<<u.message;}
                else if(!u.present){note<<"87H empty — RAW="<<u.raw_record_hex;}
                else if(u.raw_record.size()!=8){note<<"87H record "<<u.raw_record.size()<<"B; compact exact search skipped — RAW="<<u.raw_record_hex;}
                else{
                    compact_records.emplace_back(address,u.raw_record);
                    note<<"compact 8B RAW="<<u.raw_record_hex<<"; exact EEPROM search enabled";
                }
                notes.push_back(note.str());
            }
            std::lock_guard lk(eeprom_mu_);
            if(!eeprom_queue_.empty()&&eeprom_queue_.front().id==job_id)eeprom_queue_.front().compact_records[node]=compact_records;
            auto jit=eeprom_jobs_.find(job_id);if(jit!=eeprom_jobs_.end())for(auto&n:notes)jit->second.compact_probes.push_back(std::move(n));
        }

        const int core_len=std::min(block_size,end-start+1);
        const int read_len=std::min(core_len+12,0x10000-start); // overlap catches patterns crossing a block boundary and gives context
        auto got=proto.readEeprom(static_cast<std::uint8_t>(node),start,read_len);
        std::vector<ControllerEepromSearchMatch> found;
        ControllerEepromSearchError error;
        if(got.ok){
            auto patterns=eepromPatterns(series,number);
            for(const auto& rec:compact_records){
                if(rec.second.empty())continue;
                EepromPattern p;p.name="87H compact user "+std::to_string(rec.first)+" (8B exact)";p.bytes=rec.second;p.exact=true;patterns.push_back(std::move(p));
            }
            for(const auto& pat:patterns){
                if(pat.bytes.empty()||got.data.size()<pat.bytes.size())continue;
                for(std::size_t pos=0;pos+pat.bytes.size()<=got.data.size();++pos){
                    if(static_cast<int>(pos)>=core_len)break;
                    if(!std::equal(pat.bytes.begin(),pat.bytes.end(),got.data.begin()+static_cast<std::ptrdiff_t>(pos)))continue;
                    ControllerEepromSearchMatch m;m.controller_node=node;m.eeprom_address=start+static_cast<int>(pos);m.pattern=pat.name;m.exact=pat.exact;
                    m.matched_hex=util::hex(pat.bytes);
                    const auto cb=pos>8?pos-8:0;const auto ce=std::min(got.data.size(),pos+pat.bytes.size()+8);m.context_hex=hexSlice(got.data,cb,ce);found.push_back(std::move(m));
                }
            }
        }else{error.controller_node=node;error.eeprom_address=start;error.message=got.message;}
        {
            std::lock_guard lk(eeprom_mu_);auto it=eeprom_jobs_.find(job_id);
            if(it!=eeprom_jobs_.end()){
                auto& job=it->second;++job.completed;
                if(!got.ok){++job.failed;if(job.errors.size()<50)job.errors.push_back(std::move(error));}
                for(auto&m:found){if(job.matches.size()<max_results)job.matches.push_back(std::move(m));else job.truncated=true;}
                if(last_task)job.state="completed";
            }
            if(last_task&&!eeprom_queue_.empty()&&eeprom_queue_.front().id==job_id)eeprom_queue_.pop_front();
        }
        if(last_task)return;
    }
}

void ControllerManager::processUserReadBatch(Unex721Protocol& proto){
    constexpr int batch_size=6;
    for(int batch=0;batch<batch_size&&running_;++batch){
        std::uint64_t job_id=0;
        int node=0,address=0;
        bool include_empty=false,last_task=false;
        std::optional<User> local;

        {
            std::lock_guard lk(read_mu_);
            if(read_queue_.empty())return;
            auto& p=read_queue_.front();
            auto jit=read_jobs_.find(p.id);
            if(jit==read_jobs_.end()){read_queue_.pop_front();continue;}
            jit->second.state="running";

            if(p.controller_index>=p.controller_nodes.size()){
                jit->second.state="completed";read_queue_.pop_front();continue;
            }

            job_id=p.id;
            node=p.controller_nodes[p.controller_index];
            address=p.addresses[p.address_index];
            include_empty=p.include_empty;
            for(const auto& u:p.local_users)if(u.controller_port==address){local=u;break;}

            ++p.address_index;
            if(p.address_index>=p.addresses.size()){
                p.address_index=0;
                ++p.controller_index;
            }
            last_task=p.controller_index>=p.controller_nodes.size();
        }

        auto got=proto.readUser(static_cast<std::uint8_t>(node),address);
        ControllerUserReadResult result;
        result.controller_node=node;
        result.address=address;
        if(local){
            result.local_user_id=local->id;
            result.local_user_name=userFullName(*local);
        }

        bool keep_result=true;
        if(!got.ok){
            result.status="error";
            result.message=got.message;
        }else if(!got.present){
            if(local&&local->enabled&&!local->card.empty()){
                result.status="missing";
                result.message="В системе пользователь назначен на этот адрес, но запись в контроллере отсутствует";
            }else if(local){
                result.status="match";
                result.message="Локальный пользователь отключён/без карты, запись контроллера пуста";
            }else{
                result.status="empty";
                result.message="Пустой адрес";
                keep_result=include_empty;
            }
        }else{
            result.card_known=got.card_known;
            if(got.card_known)result.controller_card=util::formatCardId(got.uid1,got.uid2);
            result.controller_enabled=got.enabled;
            result.pin_set=got.pin!=0;
            result.access_mode=got.access_mode;
            result.details_known=got.details_known;
            result.raw_record_hex=got.raw_record_hex;
            if(!local){
                result.status="unknown";
                result.message="Запись есть в контроллере, но в системе нет пользователя с таким адресом";
            }else if(!got.card_known){
                result.status="unverified";
                result.message="Compact 87H не содержит подтверждённого series:number; сравнение карты отключено. Используйте «Поиск карты в EEPROM». "+got.message;
            }else{
                bool card_ok=false;
                const auto& expected_cards=local->cards.empty()?std::vector<std::string>{local->card}:local->cards;
                for(const auto&expected_card:expected_cards){
                    std::uint16_t expect1=0,expect2=0;std::string err;
                    if(util::parseCardId(expected_card,expect1,expect2,&err)&&expect1==got.uid1&&expect2==got.uid2){card_ok=true;break;}
                }
                const bool enabled_ok=!got.details_known||local->enabled==got.enabled;
                const bool pin_ok=!got.details_known||localPinValue(*local)==got.pin;
                const bool mode_ok=!got.details_known||(local->access_mode.empty()?"card":local->access_mode)==got.access_mode;
                if(card_ok&&enabled_ok&&pin_ok&&mode_ok){result.status="match";result.message="Запись контроллера полностью совпадает с системой";}
                else{result.status="diff";std::ostringstream m;m<<"Отличия:";if(!card_ok)m<<" карта";if(got.details_known&&!enabled_ok)m<<" активность";if(got.details_known&&!pin_ok)m<<" PIN";if(got.details_known&&!mode_ok)m<<" режим";result.message=m.str();}
            }
        }

        {
            std::lock_guard lk(read_mu_);
            auto it=read_jobs_.find(job_id);
            if(it!=read_jobs_.end()){
                auto& job=it->second;
                ++job.completed;
                if(result.status=="match")++job.matches;
                else if(result.status=="diff")++job.differences;
                else if(result.status=="missing")++job.missing;
                else if(result.status=="unknown")++job.unknown;
                else if(result.status=="unverified")++job.unverified;
                else if(result.status=="empty")++job.empty;
                else ++job.failed;
                if(keep_result)job.results.push_back(std::move(result));
                if(last_task)job.state="completed";
            }
            if(last_task&&!read_queue_.empty()&&read_queue_.front().id==job_id)read_queue_.pop_front();
        }
        if(last_task)return;
    }
}

void ControllerManager::processOneUserUpload(Unex721Protocol&proto){
    PendingUserUpload pending;
    {
        std::lock_guard lk(upload_mu_);
        if(upload_queue_.empty())return;
        pending=std::move(upload_queue_.front());upload_queue_.pop_front();
        auto it=upload_jobs_.find(pending.id);if(it!=upload_jobs_.end())it->second.state="running";
    }
    auto append_result=[this,&pending](ControllerUserUploadResult r,bool ok,bool skipped=false,bool keep=true){
        std::lock_guard lk(upload_mu_);
        auto it=upload_jobs_.find(pending.id);if(it==upload_jobs_.end())return;
        auto&job=it->second;++job.completed;
        if(ok&&!skipped)++job.success;else if(skipped)++job.skipped;else ++job.failed;
        if(keep)job.results.push_back(std::move(r));
    };

    if(pending.full_sync){
        // Verified full synchronization for real UNEX 721 hardware.
        // Do NOT rely on 85H: the tested controller can ACK 85H while stale
        // user slots still remain active.  Instead reconcile every official
        // H-series address 0..1023.  Addresses absent from the local active
        // dataset are zeroed with 83H and verified by 87H; desired addresses
        // are written with writeUser(), which also performs 87H verification.
        std::map<int,const User*> desired;
        for(const auto&u:pending.users)desired[u.controller_port]=&u;

        for(int node:pending.controller_nodes){
            if(!running_)return;
            const auto n=static_cast<std::uint8_t>(node);

            // Phase 0: disable the H-series global "Pass Any Cards" option.
            // When enabled the controller grants any compatible tag and logs
            // it as Normal Access user=1023 regardless of the 83H user table.
            auto pass_any=proto.disablePassAnyCards(n);
            append_result(ControllerUserUploadResult{0,node,pass_any.status,pass_any.message},pass_any.ok,false,true);
            if(!pass_any.ok){
                std::lock_guard lk(upload_mu_);
                auto it=upload_jobs_.find(pending.id);
                if(it!=upload_jobs_.end()){it->second.completed+=1024;it->second.skipped+=1024;}
                continue;
            }

            // Phase 1: remove every slot that is not part of this full export.
            for(int address=0;address<=1023;++address){
                if(desired.count(address))continue;
                if(!running_)return;
                auto out=proto.clearUserSlot(n,address);
                append_result(
                    ControllerUserUploadResult{0,node,out.status,out.message},
                    out.ok,
                    false,
                    !out.ok);
            }

            // Phase 2: write only the desired local users into their exact slots.
            for(const auto&u:pending.users){
                if(!running_)return;
                auto out=proto.writeUser(n,u);
                append_result(ControllerUserUploadResult{u.id,node,out.status,out.message},out.ok,out.status=="skipped");
            }
        }
    }else{
        for(const auto&u:pending.users){
            for(int node:pending.controller_nodes){
                if(!running_)return;
                auto out=proto.writeUser(static_cast<std::uint8_t>(node),u);
                append_result(ControllerUserUploadResult{u.id,node,out.status,out.message},out.ok,out.status=="skipped");
            }
        }
    }
    std::lock_guard lk(upload_mu_);auto it=upload_jobs_.find(pending.id);if(it!=upload_jobs_.end())it->second.state="completed";
}

void ControllerManager::processOneControllerAction(Unex721Protocol& proto){
    PendingControllerAction pending;
    {
        std::lock_guard lk(action_mu_);
        if(action_queue_.empty())return;
        pending=action_queue_.front();action_queue_.pop_front();
        auto it=action_jobs_.find(pending.id);if(it!=action_jobs_.end())it->second.state="running";
    }

    auto out=proto.disablePassAnyCards(static_cast<std::uint8_t>(pending.controller_node));
    {
        std::lock_guard lk(action_mu_);
        auto it=action_jobs_.find(pending.id);
        if(it!=action_jobs_.end()){
            it->second.state="completed";
            it->second.ok=out.ok;
            it->second.status=out.status;
            it->second.message=out.message;
        }
    }
}

void ControllerManager::processOneUserDelete(Unex721Protocol& proto){
    PendingUserDelete pending;
    {
        std::lock_guard lk(delete_mu_);
        if(delete_queue_.empty())return;
        pending=std::move(delete_queue_.front());
        delete_queue_.pop_front();
        auto it=delete_jobs_.find(pending.id);
        if(it!=delete_jobs_.end())it->second.state="running";
    }

    bool metadata_changed=false;
    for(const auto&u:pending.users){
        bool all_controller_deletes_ok=true;
        for(int node:pending.controller_nodes){
            if(!running_)return;
            auto out=proto.deleteUser(static_cast<std::uint8_t>(node),u);
            ControllerUserDeleteResult r{u.id,node,out.status,out.message};
            if(!out.ok)all_controller_deletes_ok=false;
            std::lock_guard lk(delete_mu_);
            auto it=delete_jobs_.find(pending.id);
            if(it==delete_jobs_.end())continue;
            auto&job=it->second;
            ++job.completed;
            if(out.ok)++job.success;else ++job.failed;
            job.results.push_back(std::move(r));
        }

        if(pending.delete_from_system){
            bool erased=false;
            if(all_controller_deletes_ok)erased=users_.erase(u.id);
            std::lock_guard lk(delete_mu_);
            auto it=delete_jobs_.find(pending.id);
            if(it!=delete_jobs_.end()){
                if(erased){++it->second.local_deleted;metadata_changed=true;}
                else ++it->second.local_retained;
            }
        }
    }
    if(metadata_changed)attendance_.refreshUserMetadata();

    std::lock_guard lk(delete_mu_);
    auto it=delete_jobs_.find(pending.id);
    if(it!=delete_jobs_.end())it->second.state="completed";
}
void ControllerManager::loop(){
    using namespace std::chrono_literals; SerialPort port; std::map<int,std::chrono::steady_clock::time_point> last_sync;
    while(running_){
        if(!cfg_.getBool("serial.enabled",true)){{std::lock_guard lk(mu_);serial_status_="DISABLED";}std::this_thread::sleep_for(1s);continue;}
        if(!port.isOpen()){
            auto dev=cfg_.get("serial.device","auto");if(dev=="auto")dev=SerialPort::autoDetect();
            if(dev.empty()||!port.openPort(dev,cfg_.getInt("serial.baudrate",9600))){ {std::lock_guard lk(mu_);serial_status_="OFFLINE";serial_device_=dev;} std::this_thread::sleep_for(2s);continue; }
            {std::lock_guard lk(mu_);serial_status_="ONLINE";serial_device_=dev;}
        }
        Unex721Protocol proto(port,[this](const std::string&direction,int node,int command,const std::string&protocol,const std::vector<std::uint8_t>&frame,const std::string&message){appendProtocolTrace(direction,node,command,protocol,frame,message);}); processEepromSearchBatch(proto); processUserReadBatch(proto); processOneControllerAction(proto); processOneUserDelete(proto); processOneUserUpload(proto); std::vector<int> nodes; {std::lock_guard lk(mu_);for(auto&c:controllers_)if(c.enabled)nodes.push_back(c.node);}
        if(nodes.empty()){
            int from=cfg_.getInt("controllers.scan_from",1),to=cfg_.getInt("controllers.scan_to",16);
            for(int n=from;n<=to&&running_;++n)if(proto.ping((std::uint8_t)n)){
                std::lock_guard lk(mu_);if(std::none_of(controllers_.begin(),controllers_.end(),[&](auto&c){return c.node==n;})){Controller c;c.node=n;c.name="UNEX 721 #"+std::to_string(n);c.online=true;c.last_seen=util::nowLocal();controllers_.push_back(c);}
            }
            saveControllers(); std::this_thread::sleep_for(300ms); continue;
        }
        for(int node:nodes){ if(!running_)break;
            if(cfg_.getBool("time_sync.enabled",true)){auto now=std::chrono::steady_clock::now();auto it=last_sync.find(node);auto mins=cfg_.getInt("time_sync.interval_minutes",60);if(it==last_sync.end()||now-it->second>std::chrono::minutes(mins)){if(proto.setSystemTime((std::uint8_t)node))last_sync[node]=now;}}
            auto evt=proto.getOldestEvent((std::uint8_t)node); bool online=port.isOpen();
            RawEventFn cb; std::string cname;
            {std::lock_guard lk(mu_);for(auto&c:controllers_)if(c.node==node){c.online=online;if(online)c.last_seen=util::nowLocal();if(evt)c.last_raw_hex=evt->raw_hex;cname=c.name;}cb=raw_cb_;}
            if(evt){
                bool log_semantic=false;{std::lock_guard lk(trace_mu_);if(last_event_trace_raw_!=evt->raw_hex){last_event_trace_raw_=evt->raw_hex;log_semantic=true;}}
                if(log_semantic){
                    std::string msg="Событие контроллера";
                    if(evt->user_address>=0)msg+=", адрес пользователя="+std::to_string(evt->user_address);
                    if(evt->card.empty())msg+=", карта пока не декодирована";
                    appendProtocolTrace("EVENT",node,0x25,"semantic",evt->frame,msg,evt->card,evt->user_address);

                    // Never let an undecoded record block the controller FIFO forever.
                    // Preserve the full RAW frame locally before acknowledging/removing it.
                    if(evt->card.empty())attendance_.recordRawControllerEvent(node,cname,evt->raw_hex);
                }
                if(cb)cb(*evt);
                if(!evt->card.empty()){
                    // v0.3.4 card onboarding: every confirmed real 25H card is kept in
                    // a persistent controller inventory, independent of attendance.
                    rememberControllerCard(evt->card,node,cname,evt->raw_hex);
                    if(cfg_.getBool("cards.auto_create_unknown",false)&&!users_.byCard(evt->card)){
                        if(auto created=users_.ensureUserForCard(evt->card)){
                            attendance_.refreshUserMetadata();
                            appendProtocolTrace("INFO",node,0x25,"semantic",{},"Новая карта автоматически добавлена как пользователь №"+std::to_string(created->id),evt->card);
                        }
                    }
                    attendance_.onCardRead(evt->card,node,cname,evt->raw_hex);
                }

                // 25H always returns the oldest FIFO record.  It must be removed after
                // being copied locally, otherwise the same record hides every newer card
                // event behind it (observed on the real UNEX 721).
                const bool removed=proto.removeOldestEvent((std::uint8_t)node);
                if(log_semantic){
                    appendProtocolTrace("INFO",node,0x37,"semantic",{},
                        removed?(evt->card.empty()?"RAW сохранён; старое нераспознанное событие удалено из FIFO":"Событие обработано и удалено из FIFO")
                               :"Не удалось удалить старейшее событие из FIFO; оно будет прочитано повторно");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.getInt("controllers.poll_interval_ms",200)));
        }
    }
    port.closePort(); {std::lock_guard lk(mu_);serial_status_="OFFLINE";}
}
}
