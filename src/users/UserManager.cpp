#include "skud/UserManager.h"
#include "skud/Config.h"
#include "skud/MariaDbUserStore.h"
#include "skud/Util.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace skud {
namespace {
std::string clean(std::string s){for(char&c:s)if(c==';'||c=='\n'||c=='\r')c=' ';return s;}

bool validPin(const std::string& pin){
    if(pin.empty())return true;
    if(pin.size()!=4||!std::all_of(pin.begin(),pin.end(),[](unsigned char c){return std::isdigit(c);}))return false;
    try{return std::stoi(pin)>=1&&std::stoi(pin)<=9999;}catch(...){return false;}
}

bool sameCard(const std::string& a,const std::string& b){
    if(a==b)return true;
    std::uint16_t as=0,an=0,bs=0,bn=0;
    return util::parseCardId(a,as,an,nullptr)&&util::parseCardId(b,bs,bn,nullptr)&&as==bs&&an==bn;
}

std::string canonicalCard(const std::string& value){
    std::uint16_t series=0,number=0;
    if(!util::parseCardId(util::trim(value),series,number,nullptr))return{};
    return util::formatCardId(series,number);
}

void appendUniqueCard(std::vector<std::string>& cards,const std::string& value){
    const auto c=canonicalCard(value);
    if(c.empty())return;
    if(std::none_of(cards.begin(),cards.end(),[&](const auto&x){return sameCard(x,c);}))cards.push_back(c);
}

void syncPrimary(User& u){
    if(u.cards.empty()){
        u.card.clear();u.card_series.clear();u.card_number.clear();
        return;
    }
    u.card=u.cards.front();
    std::uint16_t series=0,number=0;
    if(util::parseCardId(u.card,series,number,nullptr)){
        u.card_series=util::formatCardSeries(series);
        u.card_number=std::to_string(number);
    }else{
        u.card.clear();u.card_series.clear();u.card_number.clear();
    }
}

void normalizeUser(User& u){
    if(u.access_mode!="card"&&u.access_mode!="card_or_pin"&&u.access_mode!="card_and_pin")u.access_mode="card";
    if(!validPin(u.pin_code))u.pin_code.clear();

    std::vector<std::string> normalized;
    normalized.reserve(u.cards.size()+1);
    for(const auto&card:u.cards)appendUniqueCard(normalized,card);

    // Backward compatibility: older CSV/UI versions stored exactly one card in
    // card + card_series/card_number.  Merge it into the new multi-card list.
    std::uint16_t series=0,number=0;
    if(!util::trim(u.card_series).empty()||!util::trim(u.card_number).empty()){
        if(util::parseCardParts(u.card_series,u.card_number,series,number,nullptr))
            appendUniqueCard(normalized,util::formatCardId(series,number));
    }
    if(!util::trim(u.card).empty())appendUniqueCard(normalized,u.card);

    u.cards=std::move(normalized);
    syncPrimary(u);
}

std::string encodeCards(const User& source){
    User u=source;normalizeUser(u);
    std::ostringstream o;
    for(std::size_t i=0;i<u.cards.size();++i){if(i)o<<'|';o<<clean(u.cards[i]);}
    return o.str();
}

void decodeCards(const std::string& text,User& u){
    for(const auto&part:util::split(text,'|'))appendUniqueCard(u.cards,part);
}

std::string row(const User& source){
    User u=source;normalizeUser(u);
    std::ostringstream o;
    o<<u.id<<';'<<(u.enabled?1:0)<<';'<<clean(u.last_name)<<';'<<clean(u.first_name)<<';'<<clean(u.middle_name)<<';'
     <<clean(u.department)<<';'<<clean(u.position)<<';'<<clean(u.card)<<';'<<clean(u.card_series)<<';'<<clean(u.card_number)<<';'
     <<clean(u.pin_code)<<';'<<clean(u.access_mode)<<';'<<u.controller_port<<';'<<clean(u.valid_from)<<';'<<clean(u.valid_until)<<';'
     <<(u.telegram_arrival?1:0)<<';'<<(u.telegram_departure?1:0)<<';'<<encodeCards(u);
    return o.str();
}

bool decodeRow(const std::vector<std::string>& c,User& u){
    if(c.size()<12)return false;
    try{
        u.id=std::stoi(c[0]);u.enabled=c[1]!="0";u.last_name=c[2];u.first_name=c[3];u.middle_name=c[4];u.department=c[5];u.position=c[6];u.card=c[7];
        if(c.size()>=17){
            u.card_series=c[8];u.card_number=c[9];u.pin_code=c[10];u.access_mode=c[11].empty()?"card":c[11];
            u.controller_port=c[12].empty()?0:std::stoi(c[12]);u.valid_from=c[13];u.valid_until=c[14];u.telegram_arrival=c[15]!="0";u.telegram_departure=c[16]!="0";
            if(c.size()>=18)decodeCards(c[17],u);
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

bool userHasCard(const User& u,const std::string& card){
    return std::any_of(u.cards.begin(),u.cards.end(),[&](const auto&x){return sameCard(x,card);});
}

bool removeCardFromUser(User& u,const std::string& card){
    const auto before=u.cards.size();
    u.cards.erase(std::remove_if(u.cards.begin(),u.cards.end(),[&](const auto&x){return sameCard(x,card);}),u.cards.end());
    if(before==u.cards.size())return false;
    syncPrimary(u);return true;
}
}

UserManager::UserManager(std::string path,Config* cfg):path_(std::move(path)),cfg_(cfg){}
UserManager::~UserManager()=default;

bool UserManager::load(){
    auto readCsv=[&](std::vector<User>& out)->bool{
        std::ifstream f(path_);if(!f)return false;std::string line;bool first=true;
        while(std::getline(f,line)){if(first){first=false;continue;}if(util::trim(line).empty())continue;User u;if(decodeRow(util::split(line,';'),u))out.push_back(std::move(u));}
        return true;
    };
    const bool db_enabled=cfg_&&cfg_->getBool("database.enabled",false);
    if(!db_enabled){std::vector<User> tmp;if(!readCsv(tmp))return false;std::lock_guard lk(mu_);users_=std::move(tmp);return true;}

    db_=std::make_unique<MariaDbUserStore>(*cfg_);std::string err;
    if(!db_->init(err)){std::lock_guard sl(storage_mu_);storage_error_=err;return false;}
    std::vector<User> db_users;
    if(!db_->load(db_users,err)){std::lock_guard sl(storage_mu_);storage_error_=err;return false;}

    std::vector<User> csv_users;const bool csv_exists=readCsv(csv_users);
    if(csv_exists&&cfg_->getBool("database.migrate_users_csv",true)){
        bool migrate=false;
        if(db_users.empty()&&!csv_users.empty())migrate=true;
        else if(!csv_users.empty()){
            // Only remove an old CSV automatically when every CSV user/card is already represented in MariaDB.
            migrate=std::all_of(csv_users.begin(),csv_users.end(),[&](const User& cu){
                auto it=std::find_if(db_users.begin(),db_users.end(),[&](const User&du){return du.id==cu.id;});if(it==db_users.end())return false;
                return std::all_of(cu.cards.begin(),cu.cards.end(),[&](const std::string&card){return userHasCard(*it,card);});
            });
        }else migrate=true;

        if(db_users.empty()&&!csv_users.empty()){
            if(!db_->save(csv_users,err)){std::lock_guard sl(storage_mu_);storage_error_="CSV->MariaDB migration failed: "+err;return false;}
            std::vector<User> verify;if(!db_->load(verify,err)||verify.size()!=csv_users.size()){
                std::lock_guard sl(storage_mu_);storage_error_="CSV->MariaDB verification failed: "+err;return false;
            }
            db_users=std::move(verify);migrate=true;
        }
        if(migrate&&cfg_->getBool("database.remove_csv_after_migration",true)){
            std::error_code ec;auto backup_dir=std::filesystem::path(path_).parent_path().parent_path()/"backup";std::filesystem::create_directories(backup_dir,ec);
            if(!ec&&std::filesystem::exists(path_)){
                const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};localtime_r(&now,&tm);std::ostringstream ts;ts<<std::put_time(&tm,"%Y%m%d-%H%M%S");
                auto backup=backup_dir/("users.csv.pre-mariadb-"+ts.str());std::filesystem::copy_file(path_,backup,std::filesystem::copy_options::overwrite_existing,ec);
                if(!ec)std::filesystem::remove(path_,ec);
            }
        }
    }
    {std::lock_guard lk(mu_);users_=std::move(db_users);} {std::lock_guard sl(storage_mu_);storage_error_.clear();}return true;
}

bool UserManager::save()const{
    std::vector<User> snapshot;{std::lock_guard lk(mu_);snapshot=users_;}
    if(cfg_&&cfg_->getBool("database.enabled",false)){
        if(!db_){std::lock_guard sl(storage_mu_);storage_error_="MariaDB backend is not initialized";return false;}
        std::string err;if(!db_->save(snapshot,err)){std::lock_guard sl(storage_mu_);storage_error_=err;return false;}{std::lock_guard sl(storage_mu_);storage_error_.clear();}return true;
    }
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());auto tmp=path_+".tmp";std::ofstream f(tmp,std::ios::trunc);if(!f)return false;
    f<<"id;enabled;last_name;first_name;middle_name;department;position;card;card_series;card_number;pin_code;access_mode;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure;cards\n";
    for(const auto&u:snapshot)f<<row(u)<<"\n";
    f.close();std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec){std::filesystem::remove(path_,ec);ec.clear();std::filesystem::rename(tmp,path_,ec);}return !ec;
}
std::vector<User>UserManager::list()const{std::lock_guard lk(mu_);return users_;}
std::optional<User>UserManager::byCard(const std::string& card)const{
    const auto canonical=canonicalCard(card);if(canonical.empty())return std::nullopt;
    std::lock_guard lk(mu_);for(const auto&u:users_)if(userHasCard(u,canonical))return u;return std::nullopt;
}
std::optional<User>UserManager::byId(int id)const{std::lock_guard lk(mu_);for(const auto&u:users_)if(u.id==id)return u;return std::nullopt;}
User UserManager::upsert(User u){
    normalizeUser(u);
    {
        std::lock_guard lk(mu_);
        if(u.id<=0){int m=0;for(const auto&x:users_)m=std::max(m,x.id);u.id=m+1;}
        // A physical card can belong to only one local user.  If a card is added
        // here, detach the same card from another user instead of allowing an
        // ambiguous attendance lookup.
        for(auto&x:users_){
            if(x.id==u.id)continue;
            bool changed=false;
            for(const auto&card:u.cards)changed=removeCardFromUser(x,card)||changed;
            if(changed)normalizeUser(x);
        }
        bool updated=false;for(auto&x:users_)if(x.id==u.id){x=u;updated=true;break;}if(!updated)users_.push_back(u);
    }
    save();return u;
}
bool UserManager::erase(int id){{std::lock_guard lk(mu_);auto n=users_.size();users_.erase(std::remove_if(users_.begin(),users_.end(),[&](const auto&u){return u.id==id;}),users_.end());if(n==users_.size())return false;}return save();}
int UserManager::eraseMany(const std::vector<int>& ids){if(ids.empty())return 0;int removed=0;{std::lock_guard lk(mu_);const auto before=users_.size();users_.erase(std::remove_if(users_.begin(),users_.end(),[&](const auto&u){return std::find(ids.begin(),ids.end(),u.id)!=ids.end();}),users_.end());removed=static_cast<int>(before-users_.size());}if(removed>0)save();return removed;}
bool UserManager::assignCard(int id,const std::string& card){
    const auto canonical=canonicalCard(card);if(canonical.empty())return false;
    bool found=false;
    {
        std::lock_guard lk(mu_);
        for(auto&u:users_){
            if(u.id==id){found=true;continue;}
            removeCardFromUser(u,canonical);
        }
        if(!found)return false;
        for(auto&u:users_)if(u.id==id){appendUniqueCard(u.cards,canonical);normalizeUser(u);break;}
    }
    return save();
}

std::optional<User> UserManager::ensureUserForCard(const std::string& card){
    const auto canonical=canonicalCard(card);
    if(canonical.empty())return std::nullopt;
    User result;
    bool created=false;
    {
        std::lock_guard lk(mu_);
        for(const auto&u:users_)if(userHasCard(u,canonical))return u;
        int max_id=0;for(const auto&u:users_)max_id=std::max(max_id,u.id);
        result.id=max_id+1;
        result.enabled=true;
        // A visible placeholder makes bulk controller imports understandable.
        // The operator can immediately open and edit the created user.
        result.last_name="Карта";
        result.first_name=canonical;
        result.cards.push_back(canonical);
        normalizeUser(result);
        users_.push_back(result);
        created=true;
    }
    if(created&&!save())return std::nullopt;
    return result;
}
bool UserManager::removeCard(const std::string& card){
    const auto canonical=canonicalCard(card);if(canonical.empty())return false;
    bool ok=false;{
        std::lock_guard lk(mu_);
        for(auto&u:users_)ok=removeCardFromUser(u,canonical)||ok;
    }
    return ok?save():false;
}
bool UserManager::renameDepartment(const std::string&old_name,const std::string&new_name){if(old_name==new_name)return true;{std::lock_guard lk(mu_);for(auto&u:users_)if(u.department==old_name)u.department=new_name;}return save();}
bool UserManager::departmentInUse(const std::string&name)const{std::lock_guard lk(mu_);return std::any_of(users_.begin(),users_.end(),[&](const auto&u){return u.department==name;});}
std::vector<std::string>UserManager::usedDepartments()const{std::lock_guard lk(mu_);std::vector<std::string> out;for(const auto&u:users_){auto name=util::trim(u.department);if(name.empty())continue;if(std::find(out.begin(),out.end(),name)==out.end())out.push_back(name);}std::sort(out.begin(),out.end());return out;}
std::string UserManager::exportCsv()const{std::lock_guard lk(mu_);std::ostringstream o;o<<"id;enabled;last_name;first_name;middle_name;department;position;card;card_series;card_number;pin_code;access_mode;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure;cards\n";for(const auto&u:users_)o<<row(u)<<"\n";return o.str();}
bool UserManager::importCsv(const std::string&csv,std::string&err){
    std::vector<User> n;std::istringstream f(csv);std::string line;bool first=true;int ln=0;while(std::getline(f,line)){++ln;if(first){first=false;continue;}if(util::trim(line).empty())continue;User u;if(!decodeRow(util::split(line,';'),u)){err="bad CSV at line "+std::to_string(ln);return false;}n.push_back(std::move(u));}
    {std::lock_guard lk(mu_);users_=std::move(n);}return save();
}


bool UserManager::usingMariaDb()const{return cfg_&&cfg_->getBool("database.enabled",false);}
std::string UserManager::storageStatus()const{
    if(!usingMariaDb())return "FILE CSV";
    if(db_)return db_->status();
    std::lock_guard sl(storage_mu_);return storage_error_.empty()?"MariaDB not initialized":"ERROR: "+storage_error_;
}
std::string UserManager::storageError()const{std::lock_guard sl(storage_mu_);return storage_error_;}
}
