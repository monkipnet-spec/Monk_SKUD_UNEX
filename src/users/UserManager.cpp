#include "skud/UserManager.h"
#include "skud/Util.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace skud {
namespace {
std::string clean(std::string s){for(char&c:s)if(c==';'||c=='\n'||c=='\r')c=' ';return s;}

bool validPin(const std::string& pin){
    if(pin.empty())return true;
    if(pin.size()!=4||!std::all_of(pin.begin(),pin.end(),[](unsigned char c){return std::isdigit(c);}))return false;
    try{return std::stoi(pin)>=1&&std::stoi(pin)<=9999;}catch(...){return false;}
}

void normalizeUser(User& u){
    if(u.access_mode!="card"&&u.access_mode!="card_or_pin"&&u.access_mode!="card_and_pin")u.access_mode="card";
    if(!validPin(u.pin_code))u.pin_code.clear();

    std::uint16_t series=0,number=0;
    const auto legacy_card=util::trim(u.card);
    if(!util::trim(u.card_series).empty()||!util::trim(u.card_number).empty()){
        if(util::parseCardParts(u.card_series,u.card_number,series,number,nullptr)){
            u.card_series=util::formatCardSeries(series);
            u.card_number=std::to_string(number);
            u.card=util::formatCardId(series,number);
            return;
        }
    }
    if(!legacy_card.empty()&&util::parseCardId(legacy_card,series,number,nullptr)){
        u.card_series=util::formatCardSeries(series);
        u.card_number=std::to_string(number);
        u.card=util::formatCardId(series,number);
        return;
    }
    if(util::trim(u.card).empty()){
        u.card.clear();u.card_series.clear();u.card_number.clear();
    }
}

std::string row(const User& source){
    User u=source;normalizeUser(u);
    std::ostringstream o;
    o<<u.id<<';'<<(u.enabled?1:0)<<';'<<clean(u.last_name)<<';'<<clean(u.first_name)<<';'<<clean(u.middle_name)<<';'
     <<clean(u.department)<<';'<<clean(u.position)<<';'<<clean(u.card)<<';'<<clean(u.card_series)<<';'<<clean(u.card_number)<<';'
     <<clean(u.pin_code)<<';'<<clean(u.access_mode)<<';'<<u.controller_port<<';'<<clean(u.valid_from)<<';'<<clean(u.valid_until)<<';'
     <<(u.telegram_arrival?1:0)<<';'<<(u.telegram_departure?1:0);
    return o.str();
}

bool decodeRow(const std::vector<std::string>& c,User& u){
    if(c.size()<12)return false;
    try{
        u.id=std::stoi(c[0]);u.enabled=c[1]!="0";u.last_name=c[2];u.first_name=c[3];u.middle_name=c[4];u.department=c[5];u.position=c[6];u.card=c[7];
        if(c.size()>=17){
            u.card_series=c[8];u.card_number=c[9];u.pin_code=c[10];u.access_mode=c[11].empty()?"card":c[11];
            u.controller_port=c[12].empty()?0:std::stoi(c[12]);u.valid_from=c[13];u.valid_until=c[14];u.telegram_arrival=c[15]!="0";u.telegram_departure=c[16]!="0";
        }else if(c.size()>=13){
            // v0.1.2-v0.2.1 layout.
            u.controller_port=c[8].empty()?0:std::stoi(c[8]);u.valid_from=c[9];u.valid_until=c[10];u.telegram_arrival=c[11]!="0";u.telegram_departure=c[12]!="0";
        }else{
            // Original layout without controller user address.
            u.controller_port=0;u.valid_from=c[8];u.valid_until=c[9];u.telegram_arrival=c[10]!="0";u.telegram_departure=c[11]!="0";
        }
        normalizeUser(u);return true;
    }catch(...){return false;}
}

bool sameCard(const std::string& a,const std::string& b){
    if(a==b)return true;
    std::uint16_t as=0,an=0,bs=0,bn=0;
    return util::parseCardId(a,as,an,nullptr)&&util::parseCardId(b,bs,bn,nullptr)&&as==bs&&an==bn;
}
}

UserManager::UserManager(std::string path):path_(std::move(path)){}
bool UserManager::load(){
    std::lock_guard lk(mu_);users_.clear();std::ifstream f(path_);if(!f)return false;
    std::string line;bool first=true;while(std::getline(f,line)){if(first){first=false;continue;}if(util::trim(line).empty())continue;User u;if(decodeRow(util::split(line,';'),u))users_.push_back(std::move(u));}return true;
}
bool UserManager::save()const{
    std::lock_guard lk(mu_);std::filesystem::create_directories(std::filesystem::path(path_).parent_path());auto tmp=path_+".tmp";std::ofstream f(tmp,std::ios::trunc);if(!f)return false;
    f<<"id;enabled;last_name;first_name;middle_name;department;position;card;card_series;card_number;pin_code;access_mode;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure\n";
    for(auto&u:users_)f<<row(u)<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec){std::filesystem::remove(path_,ec);ec.clear();std::filesystem::rename(tmp,path_,ec);}return !ec;
}
std::vector<User>UserManager::list()const{std::lock_guard lk(mu_);return users_;}
std::optional<User>UserManager::byCard(const std::string& card)const{std::lock_guard lk(mu_);for(auto&u:users_)if(sameCard(u.card,card))return u;return std::nullopt;}
std::optional<User>UserManager::byId(int id)const{std::lock_guard lk(mu_);for(auto&u:users_)if(u.id==id)return u;return std::nullopt;}
User UserManager::upsert(User u){
    normalizeUser(u);
    {
        std::lock_guard lk(mu_);if(u.id<=0){int m=0;for(auto&x:users_)m=std::max(m,x.id);u.id=m+1;}
        bool updated=false;for(auto&x:users_)if(x.id==u.id){x=u;updated=true;break;}if(!updated)users_.push_back(u);
    }
    save();return u;
}
bool UserManager::erase(int id){{std::lock_guard lk(mu_);auto n=users_.size();users_.erase(std::remove_if(users_.begin(),users_.end(),[&](auto&u){return u.id==id;}),users_.end());if(n==users_.size())return false;}return save();}
int UserManager::eraseMany(const std::vector<int>& ids){if(ids.empty())return 0;int removed=0;{std::lock_guard lk(mu_);const auto before=users_.size();users_.erase(std::remove_if(users_.begin(),users_.end(),[&](const auto&u){return std::find(ids.begin(),ids.end(),u.id)!=ids.end();}),users_.end());removed=static_cast<int>(before-users_.size());}if(removed>0)save();return removed;}
bool UserManager::assignCard(int id,const std::string& card){
    std::uint16_t series=0,number=0;if(!util::parseCardId(card,series,number,nullptr))return false;const auto canonical=util::formatCardId(series,number);
    {std::lock_guard lk(mu_);for(auto&u:users_)if(sameCard(u.card,canonical)){u.card.clear();u.card_series.clear();u.card_number.clear();}bool ok=false;for(auto&u:users_)if(u.id==id){u.card=canonical;u.card_series=util::formatCardSeries(series);u.card_number=std::to_string(number);ok=true;}if(!ok)return false;}return save();
}
bool UserManager::removeCard(const std::string& card){{std::lock_guard lk(mu_);bool ok=false;for(auto&u:users_)if(sameCard(u.card,card)){u.card.clear();u.card_series.clear();u.card_number.clear();ok=true;}if(!ok)return false;}return save();}
bool UserManager::renameDepartment(const std::string&old_name,const std::string&new_name){if(old_name==new_name)return true;{std::lock_guard lk(mu_);for(auto&u:users_)if(u.department==old_name)u.department=new_name;}return save();}
bool UserManager::departmentInUse(const std::string&name)const{std::lock_guard lk(mu_);return std::any_of(users_.begin(),users_.end(),[&](const auto&u){return u.department==name;});}
std::vector<std::string>UserManager::usedDepartments()const{std::lock_guard lk(mu_);std::vector<std::string> out;for(const auto&u:users_){auto name=util::trim(u.department);if(name.empty())continue;if(std::find(out.begin(),out.end(),name)==out.end())out.push_back(name);}std::sort(out.begin(),out.end());return out;}
std::string UserManager::exportCsv()const{std::lock_guard lk(mu_);std::ostringstream o;o<<"id;enabled;last_name;first_name;middle_name;department;position;card;card_series;card_number;pin_code;access_mode;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure\n";for(auto&u:users_)o<<row(u)<<"\n";return o.str();}
bool UserManager::importCsv(const std::string&csv,std::string&err){
    std::vector<User> n;std::istringstream f(csv);std::string line;bool first=true;int ln=0;while(std::getline(f,line)){++ln;if(first){first=false;continue;}if(util::trim(line).empty())continue;User u;if(!decodeRow(util::split(line,';'),u)){err="bad CSV at line "+std::to_string(ln);return false;}n.push_back(std::move(u));}
    {std::lock_guard lk(mu_);users_=std::move(n);}return save();
}
}
