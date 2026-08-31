#include "skud/AttendanceEngine.h"
#include "skud/UserManager.h"
#include "skud/Util.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace skud {
AttendanceEngine::AttendanceEngine(UserManager& users, FileStore& store, int repeat_seconds):users_(users),store_(store),repeat_seconds_(repeat_seconds){
    for(auto&[card,p]:store_.loadCardStates()){ State s; s.presence=p.state;s.last_read_text=p.last_read;s.last_read=parseTime(p.last_read,s.has_last);states_[card]=s; }
    for(auto&a:store_.loadActivities())activities_[a.card]=a;
}
void AttendanceEngine::setNotifier(NotifyFn fn){std::lock_guard lk(mu_);notifier_=std::move(fn);}
std::chrono::system_clock::time_point AttendanceEngine::parseTime(const std::string&s,bool&ok){std::tm tm{};std::istringstream in(s);in>>std::get_time(&tm,"%Y-%m-%d %H:%M:%S");ok=!in.fail();if(!ok)return{};return std::chrono::system_clock::from_time_t(std::mktime(&tm));}
AttendanceEvent AttendanceEngine::onCardRead(const std::string&card,int node,const std::string&controller,const std::string&raw){
    const auto now=std::chrono::system_clock::now(); const auto ts=util::nowLocal(); AttendanceEvent e; e.timestamp=ts;e.card=card;e.controller_node=node;e.controller_name=controller;e.raw_hex=raw;
    NotifyFn notify;
    {
        std::lock_guard lk(mu_);
        auto user=users_.byCard(card); if(user){e.user_id=user->id;e.user_name=user->last_name+" "+user->first_name;e.department=user->department;} else e.type=AttendanceEventType::UnknownCard;
        auto& st=states_[card]; bool accidental=false;
        if(st.has_last){auto sec=std::chrono::duration_cast<std::chrono::seconds>(now-st.last_read).count(); accidental=sec<=repeat_seconds_;}
        // Important: every physical read moves last_read. Thus only > repeat_seconds from the immediately previous read changes state.
        st.last_read=now;st.has_last=true;st.last_read_text=ts;
        if(user && user->enabled){
            if(accidental)e.type=AttendanceEventType::Accidental;
            else if(st.presence==PresenceState::Absent){st.presence=PresenceState::Present;e.type=AttendanceEventType::Arrival;}
            else {st.presence=PresenceState::Absent;e.type=AttendanceEventType::Departure;}
        }
        auto& a=activities_[card];a.card=card;a.user_id=e.user_id;a.user_name=e.user_name;a.department=e.department;a.last_read=ts;a.controller_node=node;
        if(e.type==AttendanceEventType::Arrival)a.last_event="Приход";else if(e.type==AttendanceEventType::Departure)a.last_event="Уход";else if(e.type==AttendanceEventType::Accidental)a.last_event="Случайное повторное";else a.last_event="Неизвестная карта";
        persistLocked(); notify=notifier_;
    }
    store_.appendEvent(e);
    if(notify && (e.type==AttendanceEventType::Arrival||e.type==AttendanceEventType::Departure))notify(e);
    return e;
}
void AttendanceEngine::persistLocked(){std::map<std::string,PersistedCardState> p;for(auto&[c,s]:states_)p[c]={s.presence,s.last_read_text};store_.saveCardStates(p);std::vector<CardActivity>a;for(auto&[_,x]:activities_)a.push_back(x);store_.saveActivities(a);}
std::vector<CardActivity> AttendanceEngine::activities()const{std::lock_guard lk(mu_);std::vector<CardActivity>v;for(auto it=activities_.rbegin();it!=activities_.rend();++it)v.push_back(it->second);return v;}
std::vector<User> AttendanceEngine::presentUsers()const{std::lock_guard lk(mu_);std::vector<User>r;for(auto&[card,s]:states_)if(s.presence==PresenceState::Present){auto u=users_.byCard(card);if(u)r.push_back(*u);}return r;}
std::vector<DailyAttendance> AttendanceEngine::todayAttendance()const{
    auto rows=store_.loadDailyAttendance(util::todayLocal());
    for(auto& row:rows){
        auto u=users_.byId(row.user_id);
        if(!u)continue;
        row.user_name=u->last_name+" "+u->first_name;
        if(!u->middle_name.empty())row.user_name+=" "+u->middle_name;
        row.department=u->department;
    }
    return rows;
}
void AttendanceEngine::refreshUserMetadata(){std::lock_guard lk(mu_);for(auto&[card,a]:activities_){auto u=users_.byCard(card);if(u){a.user_id=u->id;a.user_name=u->last_name+" "+u->first_name;a.department=u->department;}else{a.user_id=0;a.user_name.clear();a.department.clear();}}persistLocked();}
}
