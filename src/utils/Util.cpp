#include "skud/Util.h"
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace skud::util {

namespace {
bool parseUnsignedBase(const std::string& text,int base,std::uint64_t max,std::uint64_t& out){
    auto s=skud::util::trim(text);
    if(s.empty())return false;
    std::size_t start=0;
    if(base==16&&s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))start=2;
    if(start==s.size())return false;
    for(std::size_t i=start;i<s.size();++i){
        unsigned char c=static_cast<unsigned char>(s[i]);
        if(base==10){if(!std::isdigit(c))return false;}
        else if(!std::isxdigit(c))return false;
    }
    try{
        std::size_t used=0;
        auto v=std::stoull(s,&used,base);
        if(used!=s.size()||v>max)return false;
        out=v;return true;
    }catch(...){return false;}
}
}
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

bool parseCardParts(const std::string& series_text,const std::string& number_text,std::uint16_t& series,std::uint16_t& number,std::string* error){
    std::uint64_t a=0,b=0;
    auto st=trim(series_text), nt=trim(number_text);
    if(st.empty()||nt.empty()){
        if(error)*error="Нужно указать серию карты и номер карты";
        return false;
    }
    if(!parseUnsignedBase(st,10,65535,a)){
        if(error)*error="Серия карты должна быть десятичным числом 0..65535";
        return false;
    }
    if(!parseUnsignedBase(nt,10,65535,b)){
        if(error)*error="Номер карты должен быть десятичным числом 0..65535";
        return false;
    }
    series=static_cast<std::uint16_t>(a);number=static_cast<std::uint16_t>(b);return true;
}

bool parseCardId(const std::string& text,std::uint16_t& series,std::uint16_t& number,std::string* error){
    auto s=trim(text);
    if(s.empty()){if(error)*error="Пустой номер карты";return false;}
    const auto sep=s.find_first_of(":/,");
    if(sep!=std::string::npos){
        auto left=trim(s.substr(0,sep)), right=trim(s.substr(sep+1));
        std::uint64_t a=0,b=0;
        if(!parseUnsignedBase(left,10,65535,a)||!parseUnsignedBase(right,10,65535,b)){
            if(error)*error="Карта должна быть записана как СЕРИЯ:НОМЕР, оба значения десятичные 0..65535";
            return false;
        }
        series=static_cast<std::uint16_t>(a);number=static_cast<std::uint16_t>(b);return true;
    }
    // Backward compatibility for old single *decimal* card values only.
    std::uint64_t v=0;
    if(!parseUnsignedBase(s,10,0xFFFFFFFFULL,v)){
        if(error)*error="Неверный формат карты: серия и номер должны быть десятичными";
        return false;
    }
    if(v<=65535){series=static_cast<std::uint16_t>(v);number=0;}
    else{series=static_cast<std::uint16_t>((v>>16)&0xFFFF);number=static_cast<std::uint16_t>(v&0xFFFF);}
    return true;
}

std::string formatCardSeries(std::uint16_t series){return std::to_string(series);}
std::string formatCardId(std::uint16_t series,std::uint16_t number){return std::to_string(series)+":"+std::to_string(number);}
}
