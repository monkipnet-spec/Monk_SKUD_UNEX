#include "skud/Util.h"
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace skud::util {
std::string nowLocal() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{}; localtime_r(&t, &tm);
    std::ostringstream o; o << std::put_time(&tm, "%Y-%m-%d %H:%M:%S"); return o.str();
}
std::string todayLocal() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{}; localtime_r(&t, &tm);
    std::ostringstream o; o << std::put_time(&tm, "%Y-%m-%d"); return o.str();
}
std::string jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c: s) {
        switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default: if(c<0x20)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<(int)c; else o<<c;}
    }
    return o.str();
}
std::string htmlEscape(const std::string& s){ std::string r; for(char c:s){ if(c=='&')r+="&amp;"; else if(c=='<')r+="&lt;"; else if(c=='>')r+="&gt;"; else if(c=='\"')r+="&quot;"; else r+=c;} return r; }
std::string urlDecode(const std::string& s){ std::string r; for(size_t i=0;i<s.size();++i){ if(s[i]=='+')r+=' '; else if(s[i]=='%'&&i+2<s.size()){ try{r+=(char)std::stoi(s.substr(i+1,2),nullptr,16); i+=2;}catch(...){r+=s[i];}} else r+=s[i]; } return r; }
std::map<std::string,std::string> parseForm(const std::string& body){ std::map<std::string,std::string> m; for(auto& part: split(body,'&')){ auto p=part.find('='); if(p==std::string::npos)m[urlDecode(part)]=""; else m[urlDecode(part.substr(0,p))]=urlDecode(part.substr(p+1)); } return m; }
std::vector<std::string> split(const std::string& s,char d){ std::vector<std::string> v; std::string x; std::stringstream ss(s); while(std::getline(ss,x,d))v.push_back(x); if(!s.empty()&&s.back()==d)v.emplace_back(); return v; }
std::string trim(std::string s){ auto ws=[](unsigned char c){return std::isspace(c);}; s.erase(s.begin(),std::find_if(s.begin(),s.end(),[&](char c){return !ws(c);})); s.erase(std::find_if(s.rbegin(),s.rend(),[&](char c){return !ws(c);}).base(),s.end()); return s; }
std::string hex(const std::vector<unsigned char>& data){ std::ostringstream o; for(size_t i=0;i<data.size();++i){ if(i)o<<' '; o<<std::hex<<std::uppercase<<std::setw(2)<<std::setfill('0')<<(int)data[i]; } return o.str(); }
std::string sha256Hex(const std::string& s){ unsigned char out[SHA256_DIGEST_LENGTH]; SHA256(reinterpret_cast<const unsigned char*>(s.data()),s.size(),out); std::ostringstream o; for(auto c:out)o<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)c; return o.str(); }
std::string randomToken(std::size_t bytes){ std::random_device rd; std::ostringstream o; for(size_t i=0;i<bytes;i++)o<<std::hex<<std::setw(2)<<std::setfill('0')<<(rd()&0xff); return o.str(); }
bool constantTimeEqual(const std::string&a,const std::string&b){ if(a.size()!=b.size())return false; unsigned char x=0; for(size_t i=0;i<a.size();++i)x|=a[i]^b[i]; return x==0; }
}
