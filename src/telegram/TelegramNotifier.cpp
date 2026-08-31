#include "skud/TelegramNotifier.h"
#include "skud/Config.h"
#include <chrono>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace skud {
TelegramNotifier::TelegramNotifier(Config&c):cfg_(c){}
TelegramNotifier::~TelegramNotifier(){stop();}
void TelegramNotifier::start(){if(running_.exchange(true))return;thread_=std::thread(&TelegramNotifier::loop,this);}
void TelegramNotifier::stop(){running_=false;cv_.notify_all();if(thread_.joinable())thread_.join();}
void TelegramNotifier::enqueue(const AttendanceEvent&e){if(!cfg_.getBool("telegram.enabled",false))return;std::lock_guard lk(mu_);q_.push_back(e);cv_.notify_one();}
bool TelegramNotifier::sendText(const std::string&text,std::string&err){
    std::lock_guard send_lk(send_mu_);
    auto token=cfg_.get("telegram.bot_token"),chat=cfg_.get("telegram.chat_id");if(token.empty()||chat.empty()){err="bot_token/chat_id not configured";return false;}
    std::string url="https://api.telegram.org/bot"+token+"/sendMessage";
    std::string chatArg="chat_id="+chat,textArg="text="+text;
    pid_t pid=fork(); if(pid<0){err="fork failed";return false;}
    if(pid==0){execlp("curl","curl","-fsS","--max-time","15","-X","POST","--data-urlencode",chatArg.c_str(),"--data-urlencode",textArg.c_str(),url.c_str(),(char*)nullptr);_exit(127);}
    int st=0;if(waitpid(pid,&st,0)<0){err="waitpid failed";return false;}if(WIFEXITED(st)&&WEXITSTATUS(st)==0){err.clear();return true;}err="curl exit code "+std::to_string(WIFEXITED(st)?WEXITSTATUS(st):-1);return false;
}
bool TelegramNotifier::sendDocument(const std::string&path,const std::string&caption,std::string&err){
    std::lock_guard send_lk(send_mu_);
    auto token=cfg_.get("telegram.bot_token"),chat=cfg_.get("telegram.chat_id");if(token.empty()||chat.empty()){err="bot_token/chat_id not configured";return false;}
    if(path.empty()||access(path.c_str(),R_OK)!=0){err="report file is not readable";return false;}
    std::string url="https://api.telegram.org/bot"+token+"/sendDocument";
    std::string chatArg="chat_id="+chat,docArg="document=@"+path,captionArg="caption="+caption;
    pid_t pid=fork(); if(pid<0){err="fork failed";return false;}
    if(pid==0){execlp("curl","curl","-fsS","--max-time","30","-X","POST","-F",chatArg.c_str(),"-F",docArg.c_str(),"-F",captionArg.c_str(),url.c_str(),(char*)nullptr);_exit(127);}
    int st=0;if(waitpid(pid,&st,0)<0){err="waitpid failed";return false;}if(WIFEXITED(st)&&WEXITSTATUS(st)==0){err.clear();return true;}err="curl exit code "+std::to_string(WIFEXITED(st)?WEXITSTATUS(st):-1);return false;
}
bool TelegramNotifier::sendTest(std::string&err){return sendText("Monk_SKUD_UNEX: тестовое сообщение",err);}
void TelegramNotifier::loop(){while(running_){AttendanceEvent e;{std::unique_lock lk(mu_);cv_.wait_for(lk,std::chrono::seconds(1),[&]{return !q_.empty()||!running_;});if(!running_)break;if(q_.empty())continue;e=q_.front();q_.pop_front();}bool arrival=e.type==AttendanceEventType::Arrival;if(arrival&&!cfg_.getBool("telegram.notify_arrival",true))continue;if(!arrival&&!cfg_.getBool("telegram.notify_departure",true))continue;std::ostringstream m;m<<(arrival?"🟢 Приход":"🔴 Уход")<<"\n"<<e.user_name<<"\nОтдел: "<<e.department<<"\nВремя: "<<e.timestamp<<"\nКонтроллер: "<<e.controller_name;std::string err;for(int i=0;i<cfg_.getInt("telegram.retry_count",3);++i){if(sendText(m.str(),err))break;std::this_thread::sleep_for(std::chrono::seconds(2));}}
}
}
