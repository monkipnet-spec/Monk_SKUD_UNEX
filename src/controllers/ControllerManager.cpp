#include "skud/ControllerManager.h"
#include "skud/AttendanceEngine.h"
#include "skud/Config.h"
#include "skud/SerialPort.h"
#include "skud/Unex721Protocol.h"
#include "skud/UserManager.h"
#include "skud/Util.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace skud {
ControllerManager::ControllerManager(Config&c,AttendanceEngine&a,UserManager&u,std::string p):cfg_(c),attendance_(a),users_(u),path_(std::move(p)){}
ControllerManager::~ControllerManager(){stop();}
bool ControllerManager::loadControllers(){std::lock_guard lk(mu_);controllers_.clear();std::ifstream f(path_);if(!f)return false;std::string l;bool first=true;while(std::getline(f,l)){if(first){first=false;continue;}auto c=util::split(l,';');if(c.size()<4)continue;try{Controller x;x.node=std::stoi(c[0]);x.name=c[1];x.model=c[2];x.enabled=c[3]!="0";controllers_.push_back(x);}catch(...){}}return true;}
bool ControllerManager::saveControllers()const{std::lock_guard lk(mu_);std::filesystem::create_directories(std::filesystem::path(path_).parent_path());auto tmp=path_+".tmp";std::ofstream f(tmp);if(!f)return false;f<<"node;name;model;enabled\n";for(auto&x:controllers_)f<<x.node<<';'<<x.name<<';'<<x.model<<';'<<(x.enabled?1:0)<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec){std::filesystem::remove(path_,ec);ec.clear();std::filesystem::rename(tmp,path_,ec);}return !ec;}
void ControllerManager::start(){if(running_.exchange(true))return;thread_=std::thread(&ControllerManager::loop,this);}
void ControllerManager::stop(){running_=false;if(thread_.joinable())thread_.join();}
std::vector<Controller>ControllerManager::controllers()const{std::lock_guard lk(mu_);return controllers_;}
bool ControllerManager::renameController(int node,const std::string&name){{std::lock_guard lk(mu_);bool ok=false;for(auto&c:controllers_)if(c.node==node){c.name=name;ok=true;}if(!ok)return false;}return saveControllers();}
std::string ControllerManager::serialStatus()const{std::lock_guard lk(mu_);return serial_status_;}
std::string ControllerManager::serialDevice()const{std::lock_guard lk(mu_);return serial_device_;}
void ControllerManager::setRawEventCallback(RawEventFn fn){std::lock_guard lk(mu_);raw_cb_=std::move(fn);}

bool ControllerManager::userUploadProtocolReady()const{return Unex721Protocol::userWriteSupported();}
std::string ControllerManager::userUploadProtocolMessage()const{return Unex721Protocol::userWriteSupportMessage();}

void ControllerManager::finishBlockedUserUpload(ControllerUserUploadJob&job,const std::vector<User>&users,const std::vector<int>&controller_nodes)const{
    job.state="blocked";
    for(const auto&u:users){
        for(int node:controller_nodes){
            ControllerUserUploadResult r;r.user_id=u.id;r.controller_node=node;
            if(!u.enabled){r.status="skipped";r.message="Пользователь отключен";++job.skipped;}
            else if(u.card.empty()){r.status="skipped";r.message="У пользователя не задан номер карты";++job.skipped;}
            else if(u.controller_port<=0){r.status="skipped";r.message="Не задан порт/адрес пользователя в контроллере";++job.skipped;}
            else{r.status="blocked_protocol";r.message=Unex721Protocol::userWriteSupportMessage();++job.failed;}
            ++job.completed;job.results.push_back(std::move(r));
        }
    }
}

std::uint64_t ControllerManager::queueUserUpload(std::vector<User>users,std::vector<int>controller_nodes){
    std::lock_guard lk(upload_mu_);
    const auto id=next_upload_id_++;
    ControllerUserUploadJob job;job.id=id;job.created_at=util::nowLocal();job.state="queued";
    job.total=static_cast<int>(users.size()*controller_nodes.size());
    if(!Unex721Protocol::userWriteSupported()){
        finishBlockedUserUpload(job,users,controller_nodes);
    }else{
        upload_queue_.push_back(PendingUserUpload{id,std::move(users),std::move(controller_nodes)});
    }
    upload_jobs_[id]=std::move(job);
    while(upload_jobs_.size()>20)upload_jobs_.erase(upload_jobs_.begin());
    return id;
}

std::optional<ControllerUserUploadJob> ControllerManager::userUploadJob(std::uint64_t id)const{
    std::lock_guard lk(upload_mu_);auto it=upload_jobs_.find(id);if(it==upload_jobs_.end())return std::nullopt;return it->second;
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

void ControllerManager::processOneUserUpload(Unex721Protocol&proto){
    PendingUserUpload pending;
    {
        std::lock_guard lk(upload_mu_);
        if(upload_queue_.empty())return;
        pending=std::move(upload_queue_.front());upload_queue_.pop_front();
        auto it=upload_jobs_.find(pending.id);if(it!=upload_jobs_.end())it->second.state="running";
    }
    for(const auto&u:pending.users){
        for(int node:pending.controller_nodes){
            if(!running_)return;
            auto out=proto.writeUser(static_cast<std::uint8_t>(node),u);
            ControllerUserUploadResult r{u.id,node,out.status,out.message};
            std::lock_guard lk(upload_mu_);auto it=upload_jobs_.find(pending.id);if(it==upload_jobs_.end())continue;
            auto&job=it->second;++job.completed;
            if(out.ok)++job.success;else if(out.status=="skipped")++job.skipped;else ++job.failed;
            job.results.push_back(std::move(r));
        }
    }
    std::lock_guard lk(upload_mu_);auto it=upload_jobs_.find(pending.id);if(it!=upload_jobs_.end())it->second.state="completed";
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
        Unex721Protocol proto(port); processOneUserDelete(proto); processOneUserUpload(proto); std::vector<int> nodes; {std::lock_guard lk(mu_);for(auto&c:controllers_)if(c.enabled)nodes.push_back(c.node);}
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
            if(evt){ if(cb)cb(*evt); if(!evt->card.empty()){attendance_.onCardRead(evt->card,node,cname,evt->raw_hex);proto.removeOldestEvent((std::uint8_t)node);} /* if card undecoded, do NOT delete the controller event */ }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.getInt("controllers.poll_interval_ms",200)));
        }
    }
    port.closePort(); {std::lock_guard lk(mu_);serial_status_="OFFLINE";}
}
}
