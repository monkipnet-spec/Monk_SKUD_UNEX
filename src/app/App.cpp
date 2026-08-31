#include "skud/App.h"
#include "skud/AttendanceEngine.h"
#include "skud/Config.h"
#include "skud/ControllerManager.h"
#include "skud/DepartmentManager.h"
#include "skud/FileStore.h"
#include "skud/RuntimeBootstrap.h"
#include "skud/TelegramNotifier.h"
#include "skud/UserManager.h"
#include "skud/Util.h"
#include "skud/WebServer.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace skud {
App::App(std::string root):root_(std::move(root)){}
App::~App(){stop();}
bool App::init(){
    std::string bootstrap_error;
    if(!ensureRuntimeLayout(root_,bootstrap_error)){
        std::cerr<<"Cannot initialize runtime directory "<<root_<<": "<<bootstrap_error<<"\n";
        return false;
    }
    cfg_=std::make_unique<Config>(root_+"/config/system.conf");
    if(!cfg_->load()){
        cfg_->set("server.port","8080");cfg_->set("serial.enabled","true");cfg_->set("serial.device","auto");cfg_->set("serial.baudrate","9600");cfg_->set("controllers.scan_from","1");cfg_->set("controllers.scan_to","16");cfg_->set("controllers.poll_interval_ms","200");cfg_->set("attendance.accidental_repeat_seconds","60");cfg_->set("time_sync.enabled","true");cfg_->set("time_sync.interval_minutes","60");cfg_->set("telegram.enabled","false");cfg_->set("telegram.bot_token","");cfg_->set("telegram.chat_id","");cfg_->set("telegram.notify_arrival","true");cfg_->set("telegram.notify_departure","true");cfg_->set("telegram.retry_count","3");cfg_->set("auth.username","admin");auto salt=util::randomToken(16);cfg_->set("auth.salt",salt);cfg_->set("auth.password_hash",util::sha256Hex(salt+"admin"));cfg_->save();
    }
    if(cfg_->get("auth.salt").empty()||cfg_->get("auth.password_hash").empty()){auto salt=util::randomToken(16);cfg_->set("auth.salt",salt);cfg_->set("auth.password_hash",util::sha256Hex(salt+"admin"));cfg_->save();}
    store_=std::make_unique<FileStore>(root_);
    users_=std::make_unique<UserManager>(root_+"/config/users.csv");
    if(!users_->load())users_->save();
    departments_=std::make_unique<DepartmentManager>(root_+"/config/departments.csv");
    if(!departments_->load())departments_->save();
    departments_->ensure(users_->usedDepartments());
    attendance_=std::make_unique<AttendanceEngine>(*users_,*store_,cfg_->getInt("attendance.accidental_repeat_seconds",60));
    telegram_=std::make_unique<TelegramNotifier>(*cfg_);attendance_->setNotifier([this](const AttendanceEvent&e){telegram_->enqueue(e);});
    controllers_=std::make_unique<ControllerManager>(*cfg_,*attendance_,root_+"/config/controllers.csv");if(!controllers_->loadControllers())controllers_->saveControllers();
    web_=std::make_unique<WebServer>(*cfg_,*users_,*departments_,*attendance_,*controllers_,*telegram_,root_);
    return true;
}
bool App::run(){if(!cfg_&&!init())return false;telegram_->start();controllers_->start();if(!web_->start()){std::cerr<<"Cannot start web server on port "<<cfg_->getInt("server.port",8080)<<"\n";controllers_->stop();telegram_->stop();return false;}running_=true;std::cout<<"Monk_SKUD_UNEX started. Web: http://0.0.0.0:"<<cfg_->getInt("server.port",8080)<<" default login admin/admin\n";while(running_)std::this_thread::sleep_for(std::chrono::seconds(1));return true;}
void App::stop(){if(!running_&& !web_)return;running_=false;if(web_)web_->stop();if(controllers_)controllers_->stop();if(telegram_)telegram_->stop();}
}
