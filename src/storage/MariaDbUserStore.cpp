#include "skud/MariaDbUserStore.h"
#include "skud/Config.h"
#include "skud/Util.h"
#include <mariadb/mysql.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace skud {
namespace {
bool validDbName(const std::string& s){
    return !s.empty()&&std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});
}
std::string nullable(const char* p){return p?p:"";}
int toInt(const char* p,int d=0){try{return p?std::stoi(p):d;}catch(...){return d;}}
}

MariaDbUserStore::MariaDbUserStore(const Config& cfg):cfg_(cfg){}
MariaDbUserStore::~MariaDbUserStore(){std::lock_guard lk(mu_);if(conn_)mysql_close(static_cast<MYSQL*>(conn_));}

bool MariaDbUserStore::connectLocked(std::string& error){
    if(conn_&&mysql_ping(static_cast<MYSQL*>(conn_))==0)return true;
    if(conn_){mysql_close(static_cast<MYSQL*>(conn_));conn_=nullptr;}
    const auto host=cfg_.get("database.host","127.0.0.1");
    const auto user=cfg_.get("database.user","monk_skud");
    const auto pass=cfg_.get("database.password","");
    const auto db=cfg_.get("database.name","monk_skud_unex");
    const auto port=static_cast<unsigned>(std::max(1,cfg_.getInt("database.port",3306)));
    if(!validDbName(db)){error="database.name должен содержать только A-Z, a-z, 0-9 и _";status_="ERROR: invalid database.name";return false;}
    MYSQL* c=mysql_init(nullptr);if(!c){error="mysql_init failed";status_="ERROR: mysql_init";return false;}
    unsigned timeout=5;mysql_options(c,MYSQL_OPT_CONNECT_TIMEOUT,&timeout);
    if(!mysql_real_connect(c,host.c_str(),user.c_str(),pass.c_str(),nullptr,port,nullptr,0)){
        error=mysql_error(c);status_="OFFLINE: "+error;mysql_close(c);return false;
    }
    mysql_set_character_set(c,"utf8mb4");
    if(cfg_.getBool("database.auto_create",true)){
        const std::string q="CREATE DATABASE IF NOT EXISTS `"+db+"` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
        if(mysql_query(c,q.c_str())!=0){error=mysql_error(c);status_="ERROR create database: "+error;mysql_close(c);return false;}
    }
    if(mysql_select_db(c,db.c_str())!=0){error=mysql_error(c);status_="ERROR select database: "+error;mysql_close(c);return false;}
    conn_=c;status_="ONLINE "+host+":"+std::to_string(port)+"/"+db;return true;
}

bool MariaDbUserStore::execLocked(const std::string& sql,std::string& error){
    if(!connectLocked(error))return false;
    auto*c=static_cast<MYSQL*>(conn_);
    if(mysql_query(c,sql.c_str())==0)return true;
    error=mysql_error(c);status_="ERROR: "+error;return false;
}

std::string MariaDbUserStore::escLocked(const std::string& value)const{
    auto*c=static_cast<MYSQL*>(conn_);std::string out(value.size()*2+1,'\0');
    const auto n=mysql_real_escape_string(c,out.data(),value.data(),static_cast<unsigned long>(value.size()));out.resize(n);return out;
}

bool MariaDbUserStore::init(std::string& error){
    std::lock_guard lk(mu_);if(!connectLocked(error))return false;
    const char* schema[]={
        "CREATE TABLE IF NOT EXISTS skud_users ("
        "id INT NOT NULL PRIMARY KEY, enabled TINYINT(1) NOT NULL DEFAULT 1,"
        "last_name VARCHAR(255) NOT NULL DEFAULT '', first_name VARCHAR(255) NOT NULL DEFAULT '', middle_name VARCHAR(255) NOT NULL DEFAULT '',"
        "department VARCHAR(255) NOT NULL DEFAULT '', position VARCHAR(255) NOT NULL DEFAULT '', pin_code VARCHAR(16) NOT NULL DEFAULT '',"
        "access_mode VARCHAR(32) NOT NULL DEFAULT 'card', controller_port INT NOT NULL DEFAULT 0,"
        "valid_from VARCHAR(32) NOT NULL DEFAULT '', valid_until VARCHAR(32) NOT NULL DEFAULT '',"
        "telegram_arrival TINYINT(1) NOT NULL DEFAULT 1, telegram_departure TINYINT(1) NOT NULL DEFAULT 1,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
        "CREATE TABLE IF NOT EXISTS skud_user_cards ("
        "user_id INT NOT NULL, card_order INT NOT NULL DEFAULT 0, card VARCHAR(32) NOT NULL, series INT UNSIGNED NOT NULL, number INT UNSIGNED NOT NULL,"
        "PRIMARY KEY(user_id,card_order), UNIQUE KEY uq_skud_card(card), KEY idx_skud_cards_user(user_id),"
        "CONSTRAINT fk_skud_cards_user FOREIGN KEY(user_id) REFERENCES skud_users(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
        "CREATE TABLE IF NOT EXISTS skud_meta (meta_key VARCHAR(100) NOT NULL PRIMARY KEY, meta_value TEXT NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
        "INSERT INTO skud_meta(meta_key,meta_value) VALUES('schema_version','1') ON DUPLICATE KEY UPDATE meta_value=VALUES(meta_value)"
    };
    for(const auto*q:schema)if(mysql_query(static_cast<MYSQL*>(conn_),q)!=0){error=mysql_error(static_cast<MYSQL*>(conn_));status_="ERROR schema: "+error;return false;}
    return true;
}

bool MariaDbUserStore::load(std::vector<User>& users,std::string& error){
    std::lock_guard lk(mu_);if(!connectLocked(error))return false;auto*c=static_cast<MYSQL*>(conn_);
    if(mysql_query(c,"SELECT id,enabled,last_name,first_name,middle_name,department,position,pin_code,access_mode,controller_port,valid_from,valid_until,telegram_arrival,telegram_departure FROM skud_users ORDER BY id")!=0){error=mysql_error(c);status_="ERROR load users: "+error;return false;}
    MYSQL_RES* res=mysql_store_result(c);if(!res){error=mysql_error(c);return false;}
    users.clear();std::map<int,std::size_t> index;MYSQL_ROW r;
    while((r=mysql_fetch_row(res))){
        User u;u.id=toInt(r[0]);u.enabled=toInt(r[1],1)!=0;u.last_name=nullable(r[2]);u.first_name=nullable(r[3]);u.middle_name=nullable(r[4]);u.department=nullable(r[5]);u.position=nullable(r[6]);u.pin_code=nullable(r[7]);u.access_mode=nullable(r[8]);u.controller_port=toInt(r[9]);u.valid_from=nullable(r[10]);u.valid_until=nullable(r[11]);u.telegram_arrival=toInt(r[12],1)!=0;u.telegram_departure=toInt(r[13],1)!=0;index[u.id]=users.size();users.push_back(std::move(u));
    }
    mysql_free_result(res);
    if(mysql_query(c,"SELECT user_id,card FROM skud_user_cards ORDER BY user_id,card_order")!=0){error=mysql_error(c);status_="ERROR load cards: "+error;return false;}
    res=mysql_store_result(c);if(!res){error=mysql_error(c);return false;}
    while((r=mysql_fetch_row(res))){const int id=toInt(r[0]);auto it=index.find(id);if(it!=index.end())users[it->second].cards.push_back(nullable(r[1]));}
    mysql_free_result(res);status_="ONLINE; users="+std::to_string(users.size());return true;
}

bool MariaDbUserStore::save(const std::vector<User>& users,std::string& error){
    std::lock_guard lk(mu_);if(!connectLocked(error))return false;auto*c=static_cast<MYSQL*>(conn_);
    if(mysql_query(c,"START TRANSACTION")!=0){error=mysql_error(c);return false;}
    auto rollback=[&](){mysql_query(c,"ROLLBACK");};
    if(mysql_query(c,"DELETE FROM skud_user_cards")!=0||mysql_query(c,"DELETE FROM skud_users")!=0){error=mysql_error(c);rollback();return false;}
    for(const auto&u:users){
        std::ostringstream q;q<<"INSERT INTO skud_users(id,enabled,last_name,first_name,middle_name,department,position,pin_code,access_mode,controller_port,valid_from,valid_until,telegram_arrival,telegram_departure) VALUES("
            <<u.id<<','<<(u.enabled?1:0)<<",'"<<escLocked(u.last_name)<<"','"<<escLocked(u.first_name)<<"','"<<escLocked(u.middle_name)<<"','"<<escLocked(u.department)<<"','"<<escLocked(u.position)<<"','"<<escLocked(u.pin_code)<<"','"<<escLocked(u.access_mode)<<"',"<<u.controller_port<<",'"<<escLocked(u.valid_from)<<"','"<<escLocked(u.valid_until)<<"',"<<(u.telegram_arrival?1:0)<<','<<(u.telegram_departure?1:0)<<')';
        if(mysql_query(c,q.str().c_str())!=0){error=mysql_error(c);rollback();status_="ERROR save user: "+error;return false;}
        for(std::size_t i=0;i<u.cards.size();++i){std::uint16_t series=0,number=0;if(!util::parseCardId(u.cards[i],series,number,nullptr))continue;std::ostringstream qc;qc<<"INSERT INTO skud_user_cards(user_id,card_order,card,series,number) VALUES("<<u.id<<','<<i<<",'"<<escLocked(util::formatCardId(series,number))<<"',"<<series<<','<<number<<')';if(mysql_query(c,qc.str().c_str())!=0){error=mysql_error(c);rollback();status_="ERROR save card: "+error;return false;}}
    }
    if(mysql_query(c,"COMMIT")!=0){error=mysql_error(c);rollback();return false;}status_="ONLINE; users="+std::to_string(users.size());return true;
}

std::string MariaDbUserStore::status()const{std::lock_guard lk(mu_);return status_;}
}
