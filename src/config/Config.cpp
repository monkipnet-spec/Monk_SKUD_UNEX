#include "skud/Config.h"
#include "skud/Util.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace skud {
Config::Config(std::string path):path_(std::move(path)){}
bool Config::load(){
    std::lock_guard lk(mu_); kv_.clear(); std::ifstream f(path_); if(!f)return false; std::string line;
    while(std::getline(f,line)){ line=util::trim(line); if(line.empty()||line[0]=='#')continue; auto p=line.find('='); if(p==std::string::npos)continue; kv_[util::trim(line.substr(0,p))]=util::trim(line.substr(p+1)); }
    return true;
}
bool Config::save() const{
    std::lock_guard lk(mu_); std::filesystem::create_directories(std::filesystem::path(path_).parent_path());
    auto tmp=path_+".tmp"; std::ofstream f(tmp,std::ios::trunc); if(!f)return false;
    f<<"# Monk_SKUD_UNEX system configuration\n";
    for(auto&[k,v]:kv_)f<<k<<"="<<v<<"\n"; f.flush(); f.close();
    std::error_code ec; std::filesystem::rename(tmp,path_,ec); if(ec){std::filesystem::remove(path_,ec); ec.clear(); std::filesystem::rename(tmp,path_,ec);} return !ec;
}
std::string Config::get(const std::string&k,const std::string&d)const{std::lock_guard lk(mu_);auto it=kv_.find(k);return it==kv_.end()?d:it->second;}
int Config::getInt(const std::string&k,int d)const{try{return std::stoi(get(k));}catch(...){return d;}}
bool Config::getBool(const std::string&k,bool d)const{auto v=get(k); if(v.empty())return d; for(auto&c:v)c=std::tolower((unsigned char)c); return v=="1"||v=="true"||v=="yes"||v=="on";}
void Config::set(const std::string&k,const std::string&v){std::lock_guard lk(mu_);kv_[k]=v;}
std::string Config::raw()const{std::lock_guard lk(mu_);std::ostringstream o;for(auto&[k,v]:kv_)o<<k<<"="<<v<<"\n";return o.str();}
bool Config::replaceRaw(const std::string&text){
    std::map<std::string,std::string> n; std::istringstream in(text); std::string line; while(std::getline(in,line)){line=util::trim(line);if(line.empty()||line[0]=='#')continue;auto p=line.find('=');if(p==std::string::npos)return false;n[util::trim(line.substr(0,p))]=util::trim(line.substr(p+1));}
    {std::lock_guard lk(mu_);kv_=std::move(n);} return save();
}
}
