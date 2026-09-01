#include "skud/DepartmentManager.h"
#include "skud/Config.h"
#include "skud/MariaDbUserStore.h"
#include "skud/Util.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace skud {
DepartmentManager::DepartmentManager(std::string path,Config* cfg):path_(std::move(path)),cfg_(cfg){}
DepartmentManager::~DepartmentManager()=default;
std::string DepartmentManager::normalize(std::string name){name=util::trim(std::move(name));for(char&c:name)if(c==';'||c=='\n'||c=='\r')c=' ';return util::trim(std::move(name));}
bool DepartmentManager::usingMariaDb()const{return cfg_&&cfg_->getBool("database.enabled",false);}
std::string DepartmentManager::storageError()const{std::lock_guard lk(storage_mu_);return storage_error_;}

bool DepartmentManager::backupAndRemoveCsv(std::string& error)const{
    if(!std::filesystem::exists(path_))return true;std::error_code ec;auto backup=std::filesystem::path(path_).parent_path().parent_path()/"backup";std::filesystem::create_directories(backup,ec);if(ec){error=ec.message();return false;}const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};localtime_r(&now,&tm);std::ostringstream ts;ts<<std::put_time(&tm,"%Y%m%d-%H%M%S");auto dst=backup/("departments.csv.pre-mariadb-"+ts.str());std::filesystem::copy_file(path_,dst,std::filesystem::copy_options::overwrite_existing,ec);if(ec){error=ec.message();return false;}std::filesystem::remove(path_,ec);if(ec){error=ec.message();return false;}return true;
}

bool DepartmentManager::load(){
    auto readCsv=[&](std::vector<std::string>&out){std::ifstream f(path_);if(!f)return false;std::string line;bool first=true;while(std::getline(f,line)){if(first){first=false;continue;}auto name=normalize(line);if(name.empty())continue;if(std::find(out.begin(),out.end(),name)==out.end())out.push_back(name);}std::sort(out.begin(),out.end());return true;};
    if(!usingMariaDb()){std::vector<std::string> tmp;if(!readCsv(tmp))return false;std::lock_guard lk(mu_);departments_=std::move(tmp);return true;}
    db_=std::make_unique<MariaDbUserStore>(*cfg_);std::string err;if(!db_->init(err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}std::vector<std::string> db_rows;if(!db_->loadDepartments(db_rows,err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}
    std::vector<std::string> csv_rows;const bool csv_exists=readCsv(csv_rows);if(csv_exists&&cfg_->getBool("database.migrate_runtime_csv",true)){
        for(const auto&name:csv_rows)if(std::find(db_rows.begin(),db_rows.end(),name)==db_rows.end())db_rows.push_back(name);std::sort(db_rows.begin(),db_rows.end());
        if(!db_->saveDepartments(db_rows,err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}std::vector<std::string> verify;if(!db_->loadDepartments(verify,err)||verify.size()!=db_rows.size()){std::lock_guard lk(storage_mu_);storage_error_="departments migration verification failed: "+err;return false;}db_rows=std::move(verify);
        if(cfg_->getBool("database.remove_csv_after_migration",true)&&!backupAndRemoveCsv(err)){std::lock_guard lk(storage_mu_);storage_error_=err;return false;}
    }
    {std::lock_guard lk(mu_);departments_=std::move(db_rows);}std::lock_guard lk(storage_mu_);storage_error_.clear();return true;
}

bool DepartmentManager::save()const{
    std::vector<std::string> snapshot;{std::lock_guard lk(mu_);snapshot=departments_;}
    if(usingMariaDb()){if(!db_)return false;std::string err;const bool ok=db_->saveDepartments(snapshot,err);if(!ok){std::lock_guard lk(storage_mu_);storage_error_=err;}return ok;}
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());auto tmp=path_+".tmp";std::ofstream f(tmp,std::ios::trunc);if(!f)return false;f<<"name\n";for(const auto&name:snapshot)f<<name<<"\n";f.close();std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec){std::filesystem::remove(path_,ec);ec.clear();std::filesystem::rename(tmp,path_,ec);}return !ec;
}
std::vector<std::string> DepartmentManager::list()const{std::lock_guard lk(mu_);return departments_;}
bool DepartmentManager::contains(const std::string&raw)const{auto name=normalize(raw);std::lock_guard lk(mu_);return std::find(departments_.begin(),departments_.end(),name)!=departments_.end();}
bool DepartmentManager::add(const std::string&raw){auto name=normalize(raw);if(name.empty())return false;{std::lock_guard lk(mu_);if(std::find(departments_.begin(),departments_.end(),name)!=departments_.end())return true;departments_.push_back(name);std::sort(departments_.begin(),departments_.end());}return save();}
bool DepartmentManager::rename(const std::string&raw_old,const std::string&raw_new){auto old=normalize(raw_old),neu=normalize(raw_new);if(old.empty()||neu.empty())return false;if(old==neu)return true;{std::lock_guard lk(mu_);if(std::find(departments_.begin(),departments_.end(),neu)!=departments_.end())return false;auto it=std::find(departments_.begin(),departments_.end(),old);if(it==departments_.end())return false;*it=neu;std::sort(departments_.begin(),departments_.end());}return save();}
bool DepartmentManager::erase(const std::string&raw){auto name=normalize(raw);if(name.empty())return false;{std::lock_guard lk(mu_);auto before=departments_.size();departments_.erase(std::remove(departments_.begin(),departments_.end(),name),departments_.end());if(before==departments_.size())return false;}return save();}
bool DepartmentManager::ensure(const std::vector<std::string>&names){bool changed=false;{std::lock_guard lk(mu_);for(auto raw:names){auto name=normalize(std::move(raw));if(name.empty())continue;if(std::find(departments_.begin(),departments_.end(),name)==departments_.end()){departments_.push_back(name);changed=true;}}if(changed)std::sort(departments_.begin(),departments_.end());}return !changed||save();}
}
