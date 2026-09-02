#include "skud/TelegramNotifier.h"
#include "skud/Config.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace skud {
namespace {
std::string htmlEscape(const std::string& value){
    std::string out;out.reserve(value.size()+16);
    for(char c:value){
        if(c=='&')out+="&amp;";
        else if(c=='<')out+="&lt;";
        else if(c=='>')out+="&gt;";
        else if(c=='\"')out+="&quot;";
        else out+=c;
    }
    return out;
}
}

TelegramNotifier::TelegramNotifier(Config&c):cfg_(c){}
TelegramNotifier::~TelegramNotifier(){stop();}
void TelegramNotifier::start(){if(running_.exchange(true))return;thread_=std::thread(&TelegramNotifier::loop,this);}
void TelegramNotifier::stop(){running_=false;cv_.notify_all();if(thread_.joinable())thread_.join();}
void TelegramNotifier::enqueue(const AttendanceEvent&e){
    if(!cfg_.getBool("telegram.enabled",false))return;
    std::lock_guard lk(mu_);q_.push_back(e);cv_.notify_one();
}

bool TelegramNotifier::sendMessage(const std::string&text,const std::string&parse_mode,std::string&err){
    std::lock_guard send_lk(send_mu_);
    auto token=cfg_.get("telegram.bot_token"),chat=cfg_.get("telegram.chat_id");
    if(token.empty()||chat.empty()){err="bot_token/chat_id not configured";return false;}
    std::string url="https://api.telegram.org/bot"+token+"/sendMessage";
    std::string chatArg="chat_id="+chat,textArg="text="+text,previewArg="disable_web_page_preview=true";
    std::string modeArg="parse_mode="+parse_mode;
    pid_t pid=fork(); if(pid<0){err="fork failed";return false;}
    if(pid==0){
        if(parse_mode.empty())
            execlp("curl","curl","-fsS","--max-time","15","-X","POST","--data-urlencode",chatArg.c_str(),"--data-urlencode",textArg.c_str(),"--data-urlencode",previewArg.c_str(),url.c_str(),(char*)nullptr);
        else
            execlp("curl","curl","-fsS","--max-time","15","-X","POST","--data-urlencode",chatArg.c_str(),"--data-urlencode",textArg.c_str(),"--data-urlencode",modeArg.c_str(),"--data-urlencode",previewArg.c_str(),url.c_str(),(char*)nullptr);
        _exit(127);
    }
    int st=0;if(waitpid(pid,&st,0)<0){err="waitpid failed";return false;}
    if(WIFEXITED(st)&&WEXITSTATUS(st)==0){err.clear();return true;}
    err="curl exit code "+std::to_string(WIFEXITED(st)?WEXITSTATUS(st):-1);return false;
}

bool TelegramNotifier::sendText(const std::string&text,std::string&err){return sendMessage(text,"",err);}
bool TelegramNotifier::sendHtml(const std::string&html,std::string&err){return sendMessage(html,"HTML",err);}

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

bool TelegramNotifier::sendTest(std::string&err){
    return sendHtml("<b>Monk SKUD UNEX</b>\n✅ Telegram подключен. Автоматические уведомления о приходе/уходе активны при включенных настройках.",err);
}

void TelegramNotifier::loop(){
    while(running_){
        AttendanceEvent e;
        {
            std::unique_lock lk(mu_);
            cv_.wait_for(lk,std::chrono::seconds(1),[&]{return !q_.empty()||!running_;});
            if(!running_)break;if(q_.empty())continue;e=q_.front();q_.pop_front();
        }
        const bool arrival=e.type==AttendanceEventType::Arrival;
        if(arrival&&(!cfg_.getBool("telegram.notify_arrival",true)||!e.telegram_arrival))continue;
        if(!arrival&&(!cfg_.getBool("telegram.notify_departure",true)||!e.telegram_departure))continue;

        // FIFO can contain historical access events after a long controller/server
        // outage.  Store all of them in attendance, but do not flood Telegram with
        // old arrivals/departures when the backlog is drained.
        {
            std::tm tm{}; std::istringstream in(e.timestamp); in>>std::get_time(&tm,"%Y-%m-%d %H:%M:%S");
            if(!in.fail()){
                tm.tm_isdst=-1; const auto event_time=std::mktime(&tm); const auto now=std::time(nullptr);
                const int max_age_minutes=std::max(1,cfg_.getInt("telegram.notify_recent_minutes",10));
                if(event_time!=static_cast<std::time_t>(-1)&&now>event_time&&now-event_time>max_age_minutes*60)continue;
            }
        }

        std::ostringstream m;
        m<<(arrival?"🟢 <b>ПРИХОД</b>":"🔴 <b>УХОД</b>")
         <<"\n<b>"<<htmlEscape(e.user_name.empty()?"Пользователь":e.user_name)<<"</b>";
        if(!e.position.empty())m<<"\nДолжность: "<<htmlEscape(e.position);
        if(!e.department.empty())m<<"\nОтдел: "<<htmlEscape(e.department);
        m<<"\nВремя: <code>"<<htmlEscape(e.timestamp)<<"</code>";
        if(!e.controller_name.empty())m<<"\nКонтроллер: "<<htmlEscape(e.controller_name)<<" (Node "<<e.controller_node<<")";

        std::string err;
        const int retries=std::max(1,cfg_.getInt("telegram.retry_count",3));
        for(int i=0;i<retries;++i){
            if(sendHtml(m.str(),err))break;
            if(i+1<retries)std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}
}
