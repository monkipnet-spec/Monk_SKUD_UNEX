#include "skud/AttendanceEngine.h"
#include "skud/UserManager.h"
#include "skud/Util.h"
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>

namespace skud {
namespace {
std::string userStateKey(int id){return "U:"+std::to_string(id);}
std::string cardStateKey(const std::string& card){return "C:"+card;}
}

AttendanceEngine::AttendanceEngine(UserManager& users, FileStore& store, int repeat_seconds):users_(users),store_(store),repeat_seconds_(repeat_seconds){
    // v0.3.3 keeps attendance state per USER, not per physical card.  This is
    // required when one employee owns several cards: arrival with card A and
    // departure with card B must toggle the same presence state.
    for(auto&[persisted_key,p]:store_.loadCardStates()){
        State s;s.presence=p.state;s.last_read_text=p.last_read;s.last_read=parseTime(p.last_read,s.has_last);
        std::string key=persisted_key;
        std::string card;
        if(key.rfind("U:",0)==0){
            // already v0.3.3 format
        }else{
            if(key.rfind("C:",0)==0)card=key.substr(2);else card=key; // legacy file used raw card as key
            auto user=users_.byCard(card);
            key=user?userStateKey(user->id):cardStateKey(card);
        }
        auto it=states_.find(key);
        if(it==states_.end()||(!it->second.has_last&&s.has_last)||(it->second.has_last&&s.has_last&&s.last_read>it->second.last_read))states_[key]=s;
    }
    for(auto&a:store_.loadActivities())activities_[a.card]=a;
}
void AttendanceEngine::setNotifier(NotifyFn fn){std::lock_guard lk(mu_);notifier_=std::move(fn);}
std::chrono::system_clock::time_point AttendanceEngine::parseTime(const std::string&s,bool&ok){std::tm tm{};std::istringstream in(s);in>>std::get_time(&tm,"%Y-%m-%d %H:%M:%S");ok=!in.fail();if(!ok)return{};return std::chrono::system_clock::from_time_t(std::mktime(&tm));}
AttendanceEvent AttendanceEngine::onCardRead(const std::string&card,int node,const std::string&controller,const std::string&raw){
    const auto now=std::chrono::system_clock::now();const auto ts=util::nowLocal();AttendanceEvent e;e.timestamp=ts;e.card=card;e.controller_node=node;e.controller_name=controller;e.raw_hex=raw;
    NotifyFn notify;
    {
        std::lock_guard lk(mu_);
        auto user=users_.byCard(card);
        if(user){e.user_id=user->id;e.user_name=user->last_name+" "+user->first_name;e.department=user->department;}else e.type=AttendanceEventType::UnknownCard;
        const std::string state_key=user?userStateKey(user->id):cardStateKey(card);
        auto& st=states_[state_key];bool accidental=false;
        if(st.has_last){auto sec=std::chrono::duration_cast<std::chrono::seconds>(now-st.last_read).count();accidental=sec<=repeat_seconds_;}
        // Every physical read moves last_read. Thus only > repeat_seconds from
        // the immediately previous read changes the state of the USER.
        st.last_read=now;st.has_last=true;st.last_read_text=ts;
        if(user&&user->enabled){
            if(accidental)e.type=AttendanceEventType::Accidental;
            else if(st.presence==PresenceState::Absent){st.presence=PresenceState::Present;e.type=AttendanceEventType::Arrival;}
            else{st.presence=PresenceState::Absent;e.type=AttendanceEventType::Departure;}
        }
        auto& a=activities_[card];a.card=card;a.user_id=e.user_id;a.user_name=e.user_name;a.department=e.department;a.last_read=ts;a.controller_node=node;
        if(e.type==AttendanceEventType::Arrival)a.last_event="Приход";else if(e.type==AttendanceEventType::Departure)a.last_event="Уход";else if(e.type==AttendanceEventType::Accidental)a.last_event="Случайное повторное";else a.last_event="Неизвестная карта";
        persistLocked();notify=notifier_;
    }
    store_.appendEvent(e);
    if(notify&&(e.type==AttendanceEventType::Arrival||e.type==AttendanceEventType::Departure))notify(e);
    return e;
}
void AttendanceEngine::recordRawControllerEvent(int node,const std::string&controller,const std::string&raw){
    AttendanceEvent e;e.timestamp=util::nowLocal();e.type=AttendanceEventType::RawControllerEvent;e.controller_node=node;e.controller_name=controller;e.raw_hex=raw;store_.appendEvent(e);
}
void AttendanceEngine::persistLocked(){std::map<std::string,PersistedCardState> p;for(auto&[key,s]:states_)p[key]={s.presence,s.last_read_text};store_.saveCardStates(p);std::vector<CardActivity>a;for(auto&[_,x]:activities_)a.push_back(x);store_.saveActivities(a);}
std::vector<CardActivity> AttendanceEngine::activities()const{std::lock_guard lk(mu_);std::vector<CardActivity>v;for(auto it=activities_.rbegin();it!=activities_.rend();++it)v.push_back(it->second);return v;}
std::vector<User> AttendanceEngine::presentUsers()const{
    std::lock_guard lk(mu_);std::vector<User>r;std::set<int> seen;
    for(const auto&[key,s]:states_){
        if(s.presence!=PresenceState::Present)continue;
        std::optional<User> u;
        if(key.rfind("U:",0)==0){try{u=users_.byId(std::stoi(key.substr(2)));}catch(...){}}
        else{auto card=key.rfind("C:",0)==0?key.substr(2):key;u=users_.byCard(card);}
        if(u&&seen.insert(u->id).second)r.push_back(*u);
    }
    return r;
}
std::vector<DailyAttendance> AttendanceEngine::todayAttendance()const{
    auto rows=store_.loadDailyAttendance(util::todayLocal());
    for(auto& row:rows){
        auto u=users_.byId(row.user_id);if(!u)continue;
        row.user_name=u->last_name+" "+u->first_name;if(!u->middle_name.empty())row.user_name+=" "+u->middle_name;row.position=u->position;row.department=u->department;
    }
    return rows;
}

bool AttendanceEngine::resetSiteActivity(){
    std::lock_guard lk(mu_);
    const auto old_states=states_;
    const auto old_activities=activities_;

    states_.clear();
    activities_.clear();

    if(!store_.saveCardStates({})){
        states_=old_states;
        activities_=old_activities;
        return false;
    }
    if(!store_.saveActivities({})){
        states_=old_states;
        activities_=old_activities;
        std::map<std::string,PersistedCardState> restored_states;
        for(const auto&[key,state]:states_)restored_states[key]={state.presence,state.last_read_text};
        std::vector<CardActivity> restored_activities;
        restored_activities.reserve(activities_.size());
        for(const auto&[_,activity]:activities_)restored_activities.push_back(activity);
        store_.saveCardStates(restored_states);
        store_.saveActivities(restored_activities);
        return false;
    }
    return true;
}
void AttendanceEngine::refreshUserMetadata(){
    std::lock_guard lk(mu_);
    for(auto&[card,a]:activities_){auto u=users_.byCard(card);if(u){a.user_id=u->id;a.user_name=u->last_name+" "+u->first_name;a.department=u->department;}else{a.user_id=0;a.user_name.clear();a.department.clear();}}

    // If a previously unknown card has just been attached to a user, migrate
    // its persisted state to that user's shared state key.
    std::vector<std::pair<std::string,std::string>> migrations;
    for(const auto&[key,_]:states_){
        if(key.rfind("U:",0)==0)continue;
        const auto card=key.rfind("C:",0)==0?key.substr(2):key;
        auto u=users_.byCard(card);if(u)migrations.push_back({key,userStateKey(u->id)});
    }
    for(const auto&[from,to]:migrations){
        auto src_it=states_.find(from);if(src_it==states_.end())continue;
        auto src=src_it->second;auto dst_it=states_.find(to);
        if(dst_it==states_.end()||(!dst_it->second.has_last&&src.has_last)||(dst_it->second.has_last&&src.has_last&&src.last_read>dst_it->second.last_read))states_[to]=src;
        states_.erase(from);
    }
    persistLocked();
}
}
