#include "skud/FileStore.h"
#include "skud/Util.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace skud {
namespace { std::string safe(std::string s){for(char&c:s)if(c==';'||c=='\n'||c=='\r')c=' ';return s;} }
FileStore::FileStore(std::string root):root_(std::move(root)){std::filesystem::create_directories(root_+"/data/events");std::filesystem::create_directories(root_+"/backup");}
bool FileStore::appendEvent(const AttendanceEvent&e){std::lock_guard lk(mu_);auto p=root_+"/data/events/"+util::todayLocal()+".csv";bool exists=std::filesystem::exists(p);std::ofstream f(p,std::ios::app);if(!f)return false;if(!exists)f<<"timestamp;type;card;user_id;user_name;department;controller_node;controller_name;raw_hex\n";std::string t="raw";if(e.type==AttendanceEventType::Arrival)t="arrival";else if(e.type==AttendanceEventType::Departure)t="departure";else if(e.type==AttendanceEventType::Accidental)t="accidental";else if(e.type==AttendanceEventType::UnknownCard)t="unknown_card";f<<safe(e.timestamp)<<';'<<t<<';'<<safe(e.card)<<';'<<e.user_id<<';'<<safe(e.user_name)<<';'<<safe(e.department)<<';'<<e.controller_node<<';'<<safe(e.controller_name)<<';'<<safe(e.raw_hex)<<"\n";return true;}
bool FileStore::saveCardStates(const std::map<std::string,PersistedCardState>&s){std::lock_guard lk(mu_);auto p=root_+"/data/card_state.csv",tmp=p+".tmp";std::ofstream f(tmp);if(!f)return false;f<<"state_key;state;last_read\n";for(auto&[card,st]:s)f<<safe(card)<<';'<<(st.state==PresenceState::Present?"present":"absent")<<';'<<safe(st.last_read)<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,p,ec);if(ec){std::filesystem::remove(p,ec);ec.clear();std::filesystem::rename(tmp,p,ec);}return !ec;}
std::map<std::string,PersistedCardState> FileStore::loadCardStates()const{std::lock_guard lk(mu_);std::map<std::string,PersistedCardState>r;std::ifstream f(root_+"/data/card_state.csv");std::string l;bool first=true;while(std::getline(f,l)){if(first){first=false;continue;}auto c=util::split(l,';');if(c.size()>=3)r[c[0]]={c[1]=="present"?PresenceState::Present:PresenceState::Absent,c[2]};}return r;}
bool FileStore::saveActivities(const std::vector<CardActivity>&a){std::lock_guard lk(mu_);auto p=root_+"/data/active_cards.csv",tmp=p+".tmp";std::ofstream f(tmp);if(!f)return false;f<<"card;user_id;user_name;department;last_read;last_event;controller_node\n";for(auto&x:a)f<<safe(x.card)<<';'<<x.user_id<<';'<<safe(x.user_name)<<';'<<safe(x.department)<<';'<<safe(x.last_read)<<';'<<safe(x.last_event)<<';'<<x.controller_node<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,p,ec);if(ec){std::filesystem::remove(p,ec);ec.clear();std::filesystem::rename(tmp,p,ec);}return !ec;}
std::vector<CardActivity> FileStore::loadActivities()const{std::lock_guard lk(mu_);std::vector<CardActivity>r;std::ifstream f(root_+"/data/active_cards.csv");std::string l;bool first=true;while(std::getline(f,l)){if(first){first=false;continue;}auto c=util::split(l,';');if(c.size()>=7){CardActivity x;x.card=c[0];try{x.user_id=std::stoi(c[1]);x.controller_node=std::stoi(c[6]);}catch(...){}x.user_name=c[2];x.department=c[3];x.last_read=c[4];x.last_event=c[5];r.push_back(x);}}return r;}
std::vector<DailyAttendance> FileStore::loadDailyAttendance(const std::string&date)const{
    std::lock_guard lk(mu_);
    std::map<int,DailyAttendance> by_user;
    std::ifstream f(root_+"/data/events/"+date+".csv");
    std::string line;
    bool first=true;
    while(std::getline(f,line)){
        if(first){first=false;continue;}
        auto c=util::split(line,';');
        if(c.size()<6)continue;
        if(c[1]!="arrival"&&c[1]!="departure")continue;
        int user_id=0;
        try{user_id=std::stoi(c[3]);}catch(...){continue;}
        if(user_id<=0)continue;
        auto& x=by_user[user_id];
        x.user_id=user_id;
        x.user_name=c[4];
        x.department=c[5];
        x.card=c[2];
        x.last_event_time=c[0];
        if(c[1]=="arrival"){
            if(x.arrival_time.empty())x.arrival_time=c[0];
            x.presence=PresenceState::Present;
        }else{
            x.departure_time=c[0];
            x.presence=PresenceState::Absent;
        }
    }
    std::vector<DailyAttendance> out;
    out.reserve(by_user.size());
    for(auto&[_,x]:by_user)out.push_back(std::move(x));
    std::sort(out.begin(),out.end(),[](const DailyAttendance&a,const DailyAttendance&b){
        if(a.presence!=b.presence)return a.presence==PresenceState::Present;
        if(a.arrival_time!=b.arrival_time){
            if(a.arrival_time.empty())return false;
            if(b.arrival_time.empty())return true;
            return a.arrival_time<b.arrival_time;
        }
        return a.user_name<b.user_name;
    });
    return out;
}
bool FileStore::backupFile(const std::string&path)const{if(!std::filesystem::exists(path))return true;std::filesystem::create_directories(root_+"/backup");auto name=std::filesystem::path(path).filename().string();std::string stamp=util::nowLocal();for(char&c:stamp)if(c==':'||c==' ')c='_';std::error_code ec;std::filesystem::copy_file(path,root_+"/backup/"+stamp+"_"+name,std::filesystem::copy_options::overwrite_existing,ec);return !ec;}
}
