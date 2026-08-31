#include "skud/DepartmentManager.h"
#include "skud/Util.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace skud {

DepartmentManager::DepartmentManager(std::string path):path_(std::move(path)){}

std::string DepartmentManager::normalize(std::string name){
    name=util::trim(std::move(name));
    for(char& c:name)if(c==';'||c=='\n'||c=='\r')c=' ';
    return util::trim(std::move(name));
}

bool DepartmentManager::load(){
    std::lock_guard lk(mu_);
    departments_.clear();
    std::ifstream f(path_);
    if(!f)return false;
    std::string line;
    bool first=true;
    while(std::getline(f,line)){
        if(first){first=false;continue;}
        auto name=normalize(line);
        if(name.empty())continue;
        if(std::find(departments_.begin(),departments_.end(),name)==departments_.end())departments_.push_back(name);
    }
    std::sort(departments_.begin(),departments_.end());
    return true;
}

bool DepartmentManager::save() const{
    std::lock_guard lk(mu_);
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());
    auto tmp=path_+".tmp";
    std::ofstream f(tmp,std::ios::trunc);
    if(!f)return false;
    f<<"name\n";
    for(const auto& name:departments_)f<<name<<"\n";
    f.close();
    std::error_code ec;
    std::filesystem::rename(tmp,path_,ec);
    if(ec){
        std::filesystem::remove(path_,ec);
        ec.clear();
        std::filesystem::rename(tmp,path_,ec);
    }
    return !ec;
}

std::vector<std::string> DepartmentManager::list() const{
    std::lock_guard lk(mu_);
    return departments_;
}

bool DepartmentManager::contains(const std::string& raw_name) const{
    auto name=normalize(raw_name);
    std::lock_guard lk(mu_);
    return std::find(departments_.begin(),departments_.end(),name)!=departments_.end();
}

bool DepartmentManager::add(const std::string& raw_name){
    auto name=normalize(raw_name);
    if(name.empty())return false;
    {
        std::lock_guard lk(mu_);
        if(std::find(departments_.begin(),departments_.end(),name)!=departments_.end())return true;
        departments_.push_back(name);
        std::sort(departments_.begin(),departments_.end());
    }
    return save();
}

bool DepartmentManager::rename(const std::string& raw_old_name,const std::string& raw_new_name){
    auto old_name=normalize(raw_old_name);
    auto new_name=normalize(raw_new_name);
    if(old_name.empty()||new_name.empty())return false;
    if(old_name==new_name)return true;
    {
        std::lock_guard lk(mu_);
        if(std::find(departments_.begin(),departments_.end(),new_name)!=departments_.end())return false;
        auto it=std::find(departments_.begin(),departments_.end(),old_name);
        if(it==departments_.end())return false;
        *it=new_name;
        std::sort(departments_.begin(),departments_.end());
    }
    return save();
}

bool DepartmentManager::erase(const std::string& raw_name){
    auto name=normalize(raw_name);
    if(name.empty())return false;
    {
        std::lock_guard lk(mu_);
        auto before=departments_.size();
        departments_.erase(std::remove(departments_.begin(),departments_.end(),name),departments_.end());
        if(before==departments_.size())return false;
    }
    return save();
}

bool DepartmentManager::ensure(const std::vector<std::string>& names){
    bool changed=false;
    {
        std::lock_guard lk(mu_);
        for(auto raw:names){
            auto name=normalize(std::move(raw));
            if(name.empty())continue;
            if(std::find(departments_.begin(),departments_.end(),name)==departments_.end()){
                departments_.push_back(name);
                changed=true;
            }
        }
        if(changed)std::sort(departments_.begin(),departments_.end());
    }
    return !changed||save();
}

}
