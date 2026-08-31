#include "skud/Unex721Protocol.h"
#include "skud/SerialPort.h"
#include "skud/Util.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <limits>
#include <sstream>

namespace skud {
namespace {
constexpr std::uint8_t ACK=0x04;
constexpr std::uint8_t NACK=0x05;

bool parseUnsigned(const std::string& text,std::uint64_t max,std::uint64_t& out){
    auto s=util::trim(text);
    if(s.empty())return false;
    int base=10;
    std::size_t start=0;
    if(s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;start=2;if(start==s.size())return false;}
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

bool parseCardWords(const std::string& card,std::uint16_t& uid1,std::uint16_t& uid2,std::string& error){
    auto s=util::trim(card);
    if(s.empty()){error="Пустой номер карты";return false;}
    const auto sep=s.find_first_of(":/,");
    if(sep!=std::string::npos){
        std::uint64_t a=0,b=0;
        if(!parseUnsigned(s.substr(0,sep),65535,a)||!parseUnsigned(s.substr(sep+1),65535,b)){
            error="Номер карты должен быть UID1:UID2, оба значения 0..65535";return false;
        }
        uid1=static_cast<std::uint16_t>(a);uid2=static_cast<std::uint16_t>(b);return true;
    }
    std::uint64_t v=0;
    if(!parseUnsigned(s,0xFFFFFFFFULL,v)){error="Номер карты должен быть десятичным/0xHEX 32-битным числом или UID1:UID2";return false;}
    if(v<=65535){uid1=static_cast<std::uint16_t>(v);uid2=0;}
    else{uid1=static_cast<std::uint16_t>((v>>16)&0xFFFF);uid2=static_cast<std::uint16_t>(v&0xFFFF);}
    return true;
}

void expiryBytes(const std::string& valid_until,std::uint8_t& yy,std::uint8_t& mm,std::uint8_t& dd){
    yy=99;mm=12;dd=31;
    if(valid_until.size()<10||valid_until[4]!='-'||valid_until[7]!='-')return;
    try{
        int y=std::stoi(valid_until.substr(0,4));int m=std::stoi(valid_until.substr(5,2));int d=std::stoi(valid_until.substr(8,2));
        if(y>=2000&&y<=2099&&m>=1&&m<=12&&d>=1&&d<=31){yy=static_cast<std::uint8_t>(y%100);mm=static_cast<std::uint8_t>(m);dd=static_cast<std::uint8_t>(d);}
    }catch(...){}
}

std::string cardWordsText(std::uint16_t uid1,std::uint16_t uid2){
    if(uid2==0)return std::to_string(uid1);
    return std::to_string(uid1)+":"+std::to_string(uid2);
}
}

std::vector<std::uint8_t> Unex721Protocol::frame(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data){
    std::vector<std::uint8_t>p{node,cmd};p.insert(p.end(),data.begin(),data.end());std::uint8_t x=0xFF;unsigned sum=0;for(auto b:p){x^=b;sum+=b;}p.push_back(x);sum+=x;p.push_back(static_cast<std::uint8_t>(sum&0xFF));std::vector<std::uint8_t>f{0x7E,static_cast<std::uint8_t>(p.size())};f.insert(f.end(),p.begin(),p.end());return f;
}

bool Unex721Protocol::validFrame(const std::vector<std::uint8_t>&f){
    if(f.size()<6||f[0]!=0x7E||static_cast<std::size_t>(f[1])+2!=f.size())return false;std::uint8_t x=0xFF;unsigned sum=0;for(std::size_t i=2;i+2<f.size();++i){x^=f[i];sum+=f[i];}if(x!=f[f.size()-2])return false;sum+=x;return static_cast<std::uint8_t>(sum&0xFF)==f.back();
}

std::vector<std::uint8_t> Unex721Protocol::extendedFrame(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data){
    const std::size_t len=data.size()+4; // node + command + XOR + SUM
    if(len>1018)return{};
    std::uint8_t x=static_cast<std::uint8_t>(0xFF^node^cmd);
    unsigned sum=static_cast<unsigned>(node)+cmd;
    for(auto b:data){x^=b;sum+=b;}
    sum+=x;
    std::vector<std::uint8_t> f={0xFF,0x00,0x5A,0xA5,static_cast<std::uint8_t>((len>>8)&0xFF),static_cast<std::uint8_t>(len&0xFF),node,cmd};
    f.insert(f.end(),data.begin(),data.end());f.push_back(x);f.push_back(static_cast<std::uint8_t>(sum&0xFF));return f;
}

bool Unex721Protocol::validExtendedFrame(const std::vector<std::uint8_t>&f){
    if(f.size()<10||f[0]!=0xFF||f[1]!=0x00||f[2]!=0x5A||f[3]!=0xA5)return false;
    const std::size_t len=(static_cast<std::size_t>(f[4])<<8)|f[5];
    if(len<4||len>1018||f.size()!=len+6)return false;
    std::uint8_t x=0xFF;unsigned sum=0;
    for(std::size_t i=6;i+2<f.size();++i){x^=f[i];sum+=f[i];}
    if(x!=f[f.size()-2])return false;sum+=x;return static_cast<std::uint8_t>(sum&0xFF)==f.back();
}

std::optional<std::vector<std::uint8_t>> Unex721Protocol::transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout){
    auto q=frame(node,cmd,data);if(!port_.writeAll(q))return std::nullopt;auto r=port_.readFrame(timeout);if(r.empty()||!validFrame(r))return std::nullopt;return r;
}

std::optional<std::vector<std::uint8_t>> Unex721Protocol::transactExtended(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout){
    auto q=extendedFrame(node,cmd,data);if(q.empty()||!port_.writeAll(q))return std::nullopt;auto r=port_.readExtendedFrame(timeout);if(r.empty()||!validExtendedFrame(r))return std::nullopt;return r;
}

bool Unex721Protocol::ping(std::uint8_t node){
    // AR-727H-compatible Extended Protocol status command. Fall back to the
    // legacy compact transaction so existing UNEX experiments keep working.
    if(transactExtended(node,0x18,{},120))return true;
    return transact(node,0x25,{},80).has_value();
}

std::optional<RawUnexEvent> Unex721Protocol::getOldestEvent(std::uint8_t node){
    if(auto r=transactExtended(node,0x25,{},180)){
        if(r->size()>=8&&(*r)[7]==ACK)return std::nullopt; // no event in controller
        if(r->size()>=30)return decodeExtendedEvent(node,*r);
        return std::nullopt;
    }
    auto r=transact(node,0x25,{},120);if(!r)return std::nullopt;if(r->size()<18)return std::nullopt;return decodeEvent(node,*r);
}

bool Unex721Protocol::removeOldestEvent(std::uint8_t node){
    if(auto r=transactExtended(node,0x37,{},180))return r->size()>=8&&(*r)[7]!=NACK;
    return transact(node,0x37,{},120).has_value();
}

bool Unex721Protocol::setSystemTime(std::uint8_t node){
    std::time_t t=std::time(nullptr);std::tm tm{};localtime_r(&t,&tm);std::vector<std::uint8_t>d={static_cast<std::uint8_t>(tm.tm_sec),static_cast<std::uint8_t>(tm.tm_min),static_cast<std::uint8_t>(tm.tm_hour),static_cast<std::uint8_t>(tm.tm_wday+1),static_cast<std::uint8_t>(tm.tm_mday),static_cast<std::uint8_t>(tm.tm_mon+1),static_cast<std::uint8_t>((tm.tm_year+1900)%100)};
    if(auto r=transactExtended(node,0x23,d,180))return r->size()>=8&&(*r)[7]!=NACK;
    return transact(node,0x23,d,150).has_value();
}

bool Unex721Protocol::userWriteSupported(){return true;}
std::string Unex721Protocol::userWriteSupportMessage(){return "SOYAL H-series Extended Protocol: команда 0x84 записи карты включена; адрес пользователя 1..16383";}

Unex721Protocol::UserWriteOutcome Unex721Protocol::writeUser(std::uint8_t node,const User& user){
    if(!user.enabled)return{false,"skipped","Пользователь отключен"};
    if(user.card.empty())return{false,"skipped","У пользователя не задан номер карты"};
    if(user.controller_port<=0||user.controller_port>16383)return{false,"skipped","Порт/адрес пользователя должен быть 1..16383"};
    std::uint16_t uid1=0,uid2=0;std::string parse_error;
    if(!parseCardWords(user.card,uid1,uid2,parse_error))return{false,"error",parse_error};
    std::uint8_t yy=99,mm=12,dd=31;expiryBytes(user.valid_until,yy,mm,dd);
    const auto address=static_cast<std::uint16_t>(user.controller_port);
    const std::vector<std::uint8_t> data={
        1,
        static_cast<std::uint8_t>((address>>8)&0xFF),static_cast<std::uint8_t>(address&0xFF),
        0,0,0,0,
        static_cast<std::uint8_t>((uid1>>8)&0xFF),static_cast<std::uint8_t>(uid1&0xFF),
        static_cast<std::uint8_t>((uid2>>8)&0xFF),static_cast<std::uint8_t>(uid2&0xFF),
        0,0,0,0,
        0x58, // enabled mode, matching H-series user record
        0x00, // zone
        0xFF,0xFF, // groups
        yy,mm,dd,
        0x00, // level
        0,0,0,0
    };
    auto r=transactExtended(node,0x84,data,350);
    if(!r)return{false,"error","Нет корректного ответа Extended Protocol на команду 0x84"};
    if(r->size()<8)return{false,"error","Короткий ответ контроллера"};
    const auto code=(*r)[7];
    if(code==ACK){
        // Read back the same user address (0x87) before reporting success.
        // This catches wiring/echo/protocol mismatches and confirms that the
        // controller actually stored the UID pair rather than only accepting a frame.
        const std::vector<std::uint8_t> read_data={
            static_cast<std::uint8_t>((address>>8)&0xFF),static_cast<std::uint8_t>(address&0xFF),0x01
        };
        auto verify=transactExtended(node,0x87,read_data,350);
        if(!verify||verify->size()<=22)return{false,"error","Контроллер подтвердил 0x84, но чтение 0x87 для проверки не удалось"};
        if((*verify)[7]==NACK)return{false,"error","Контроллер вернул NACK при контрольном чтении 0x87"};
        const std::uint16_t got1=(static_cast<std::uint16_t>((*verify)[13])<<8)|(*verify)[14];
        const std::uint16_t got2=(static_cast<std::uint16_t>((*verify)[15])<<8)|(*verify)[16];
        const bool enabled=(*verify)[21]>0;
        if(got1!=uid1||got2!=uid2||!enabled){
            std::ostringstream m;m<<"Контрольное чтение не совпало: ожидалось "<<cardWordsText(uid1,uid2)<<", получено "<<cardWordsText(got1,got2)<<", active="<<(enabled?"1":"0");
            return{false,"error",m.str()};
        }
        std::ostringstream m;m<<"Записан и проверен: адрес "<<user.controller_port<<", UID "<<cardWordsText(uid1,uid2);
        return{true,"ok",m.str()};
    }
    if(code==NACK)return{false,"error","Контроллер вернул NACK на запись пользователя"};
    std::ostringstream m;m<<"Неожиданный код ответа 0x"<<std::hex<<std::uppercase<<static_cast<int>(code);
    return{false,"error",m.str()};
}

RawUnexEvent Unex721Protocol::decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f)const{
    RawUnexEvent e;e.node=node;e.frame=f;e.raw_hex=util::hex(f);for(std::size_t i=2;i+2<f.size();++i)if(f[i]==0x0B){e.event_code=0x0B;break;}return e;
}

RawUnexEvent Unex721Protocol::decodeExtendedEvent(std::uint8_t node,const std::vector<std::uint8_t>&f)const{
    RawUnexEvent e;e.node=node;e.frame=f;e.raw_hex=util::hex(f);
    // Indices below are the zero-based equivalent of the AR-727H library's
    // 1-based response positions: address 18/19, UID1 24/25, door 26, UID2 28/29.
    if(f.size()>28){
        e.user_address=(static_cast<int>(f[17])<<8)|f[18];
        const std::uint16_t uid1=(static_cast<std::uint16_t>(f[23])<<8)|f[24];
        const std::uint16_t uid2=(static_cast<std::uint16_t>(f[27])<<8)|f[28];
        e.card=cardWordsText(uid1,uid2);
        e.event_code=f[7];
    }
    return e;
}
}
