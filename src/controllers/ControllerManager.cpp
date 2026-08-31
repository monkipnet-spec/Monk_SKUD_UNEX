#include "skud/ControllerManager.h"
#include "skud/AttendanceEngine.h"
#include "skud/Config.h"
#include "skud/SerialPort.h"
#include "skud/Unex721Protocol.h"
#include "skud/Util.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace skud {
ControllerManager::ControllerManager(Config&c,AttendanceEngine&a,std::string p):cfg_(c),attendance_(a),path_(std::move(p)){}
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
void ControllerManager::loop(){
    using namespace std::chrono_literals; SerialPort port; std::map<int,std::chrono::steady_clock::time_point> last_sync;
    while(running_){
        if(!cfg_.getBool("serial.enabled",true)){std::lock_guard lk(mu_);serial_status_="DISABLED";std::this_thread::sleep_for(1s);continue;}
        if(!port.isOpen()){
            auto dev=cfg_.get("serial.device","auto");if(dev=="auto")dev=SerialPort::autoDetect();
            if(dev.empty()||!port.openPort(dev,cfg_.getInt("serial.baudrate",9600))){ {std::lock_guard lk(mu_);serial_status_="OFFLINE";serial_device_=dev;} std::this_thread::sleep_for(2s);continue; }
            {std::lock_guard lk(mu_);serial_status_="ONLINE";serial_device_=dev;}
        }
        Unex721Protocol proto(port); std::vector<int> nodes; {std::lock_guard lk(mu_);for(auto&c:controllers_)if(c.enabled)nodes.push_back(c.node);}
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
