#include "skud/UserManager.h"
#include "skud/Util.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace skud {
namespace {
std::string clean(std::string s){ for(char&c:s)if(c==';'||c=='\n'||c=='\r')c=' '; return s; }
std::string row(const User&u){ std::ostringstream o; o<<u.id<<';'<<(u.enabled?1:0)<<';'<<clean(u.last_name)<<';'<<clean(u.first_name)<<';'<<clean(u.middle_name)<<';'<<clean(u.department)<<';'<<clean(u.position)<<';'<<clean(u.card)<<';'<<clean(u.valid_from)<<';'<<clean(u.valid_until)<<';'<<(u.telegram_arrival?1:0)<<';'<<(u.telegram_departure?1:0); return o.str(); }
}
UserManager::UserManager(std::string path):path_(std::move(path)){}
bool UserManager::load(){std::lock_guard lk(mu_);users_.clear();std::ifstream f(path_);if(!f)return false;std::string line;bool first=true;while(std::getline(f,line)){if(first){first=false;continue;}auto c=util::split(line,';');if(c.size()<12)continue;try{User u;u.id=std::stoi(c[0]);u.enabled=c[1]!="0";u.last_name=c[2];u.first_name=c[3];u.middle_name=c[4];u.department=c[5];u.position=c[6];u.card=c[7];u.valid_from=c[8];u.valid_until=c[9];u.telegram_arrival=c[10]!="0";u.telegram_departure=c[11]!="0";users_.push_back(u);}catch(...){}}return true;}
bool UserManager::save()const{std::lock_guard lk(mu_);std::filesystem::create_directories(std::filesystem::path(path_).parent_path());auto tmp=path_+".tmp";std::ofstream f(tmp,std::ios::trunc);if(!f)return false;f<<"id;enabled;last_name;first_name;middle_name;department;position;card;valid_from;valid_until;telegram_arrival;telegram_departure\n";for(auto&u:users_)f<<row(u)<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec){std::filesystem::remove(path_,ec);ec.clear();std::filesystem::rename(tmp,path_,ec);}return !ec;}
std::vector<User>UserManager::list()const{std::lock_guard lk(mu_);return users_;}
std::optional<User>UserManager::byCard(const std::string&card)const{std::lock_guard lk(mu_);for(auto&u:users_)if(u.card==card)return u;return std::nullopt;}
std::optional<User>UserManager::byId(int id)const{std::lock_guard lk(mu_);for(auto&u:users_)if(u.id==id)return u;return std::nullopt;}
User UserManager::upsert(User u){{std::lock_guard lk(mu_);if(u.id<=0){int m=0;for(auto&x:users_)m=std::max(m,x.id);u.id=m+1;}for(auto&x:users_)if(x.id==u.id){x=u;goto done;}users_.push_back(u);}done: save();return u;}
bool UserManager::erase(int id){{std::lock_guard lk(mu_);auto n=users_.size();users_.erase(std::remove_if(users_.begin(),users_.end(),[&](auto&u){return u.id==id;}),users_.end());if(n==users_.size())return false;}return save();}
bool UserManager::assignCard(int id,const std::string&card){{std::lock_guard lk(mu_);for(auto&u:users_)if(u.card==card)u.card.clear();bool ok=false;for(auto&u:users_)if(u.id==id){u.card=card;ok=true;}if(!ok)return false;}return save();}
bool UserManager::removeCard(const std::string&card){{std::lock_guard lk(mu_);bool ok=false;for(auto&u:users_)if(u.card==card){u.card.clear();ok=true;}if(!ok)return false;}return save();}
std::string UserManager::exportCsv()const{std::lock_guard lk(mu_);std::ostringstream o;o<<"id;enabled;last_name;first_name;middle_name;department;position;card;valid_from;valid_until;telegram_arrival;telegram_departure\n";for(auto&u:users_)o<<row(u)<<"\n";return o.str();}
bool UserManager::importCsv(const std::string&csv,std::string&err){std::vector<User>n;std::istringstream f(csv);std::string line;bool first=true;int ln=0;while(std::getline(f,line)){++ln;if(first){first=false;continue;}if(util::trim(line).empty())continue;auto c=util::split(line,';');if(c.size()<12){err="bad CSV at line "+std::to_string(ln);return false;}try{User u;u.id=std::stoi(c[0]);u.enabled=c[1]!="0";u.last_name=c[2];u.first_name=c[3];u.middle_name=c[4];u.department=c[5];u.position=c[6];u.card=c[7];u.valid_from=c[8];u.valid_until=c[9];u.telegram_arrival=c[10]!="0";u.telegram_departure=c[11]!="0";n.push_back(u);}catch(...){err="bad value at line "+std::to_string(ln);return false;}}{std::lock_guard lk(mu_);users_=std::move(n);}return save();}
}
