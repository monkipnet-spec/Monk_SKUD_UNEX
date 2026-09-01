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

void expiryBytes(const std::string& valid_until,std::uint8_t& yy,std::uint8_t& mm,std::uint8_t& dd){
    yy=99;mm=12;dd=31;
    if(valid_until.size()<10||valid_until[4]!='-'||valid_until[7]!='-')return;
    try{
        int y=std::stoi(valid_until.substr(0,4));int m=std::stoi(valid_until.substr(5,2));int d=std::stoi(valid_until.substr(8,2));
        if(y>=2000&&y<=2099&&m>=1&&m<=12&&d>=1&&d<=31){yy=static_cast<std::uint8_t>(y%100);mm=static_cast<std::uint8_t>(m);dd=static_cast<std::uint8_t>(d);}
    }catch(...){}
}


bool parsePin(const std::string& pin,std::uint32_t& out,std::string& error){
    auto s=util::trim(pin);out=0;
    if(s.empty())return true;
    if(s.size()!=4||!std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isdigit(c);})){
        error="PIN должен состоять ровно из 4 цифр";return false;
    }
    try{
        int v=std::stoi(s);if(v<1||v>9999){error="PIN должен быть в диапазоне 0001..9999";return false;}
        out=static_cast<std::uint32_t>(v);return true;
    }catch(...){error="Неверный PIN";return false;}
}

std::uint8_t accessModeByte(const User& user){
    // Keep the lower six bits from the reference library's 0x58 card mode.
    // Protocol bits 7/6: 01=card/read-only, 10=card OR PIN, 11=card + PIN.
    constexpr std::uint8_t low=0x18;
    if(user.access_mode=="card_or_pin")return static_cast<std::uint8_t>(0x80|low); // 0x98
    if(user.access_mode=="card_and_pin")return static_cast<std::uint8_t>(0xC0|low); // 0xD8
    return static_cast<std::uint8_t>(0x40|low); // 0x58
}

std::string accessModeText(const User& user){
    if(user.access_mode=="card_or_pin")return "карта ИЛИ PIN";
    if(user.access_mode=="card_and_pin")return "карта + PIN";
    return "только карта";
}

std::string accessModeFromByte(std::uint8_t mode){
    switch(mode&0xC0){
        case 0x80:return "card_or_pin";
        case 0xC0:return "card_and_pin";
        default:return "card";
    }
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
std::string Unex721Protocol::userWriteSupportMessage(){return "SOYAL H-series Extended Protocol: 0x84 запись карты/PIN, 0x87 контрольное чтение; адрес пользователя 1..16383";}

Unex721Protocol::UserWriteOutcome Unex721Protocol::writeUser(std::uint8_t node,const User& user){
    if(!user.enabled)return{false,"skipped","Пользователь отключен"};
    if(user.card.empty())return{false,"skipped","У пользователя не задана серия/номер карты"};
    if(user.controller_port<=0||user.controller_port>16383)return{false,"skipped","Порт/адрес пользователя должен быть 1..16383"};
    std::uint16_t uid1=0,uid2=0;std::string parse_error;
    if(!util::parseCardId(user.card,uid1,uid2,&parse_error))return{false,"error",parse_error};
    std::uint32_t pin=0;
    if(!parsePin(user.pin_code,pin,parse_error))return{false,"error",parse_error};
    if((user.access_mode=="card_or_pin"||user.access_mode=="card_and_pin")&&user.pin_code.empty())
        return{false,"error","Для выбранного PIN-режима у пользователя не задан PIN"};
    std::uint8_t yy=99,mm=12,dd=31;expiryBytes(user.valid_until,yy,mm,dd);
    const auto address=static_cast<std::uint16_t>(user.controller_port);
    const std::vector<std::uint8_t> data={
        1,
        static_cast<std::uint8_t>((address>>8)&0xFF),static_cast<std::uint8_t>(address&0xFF),
        0,0,0,0,
        static_cast<std::uint8_t>((uid1>>8)&0xFF),static_cast<std::uint8_t>(uid1&0xFF),
        static_cast<std::uint8_t>((uid2>>8)&0xFF),static_cast<std::uint8_t>(uid2&0xFF),
        static_cast<std::uint8_t>((pin>>24)&0xFF),
        static_cast<std::uint8_t>((pin>>16)&0xFF),
        static_cast<std::uint8_t>((pin>>8)&0xFF),
        static_cast<std::uint8_t>(pin&0xFF),
        accessModeByte(user),
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
        const std::uint32_t got_pin=(static_cast<std::uint32_t>((*verify)[17])<<24)|(static_cast<std::uint32_t>((*verify)[18])<<16)|(static_cast<std::uint32_t>((*verify)[19])<<8)|(*verify)[20];
        const std::uint8_t got_mode=(*verify)[21];
        const bool enabled=got_mode>0;
        if(got1!=uid1||got2!=uid2||got_pin!=pin||!enabled||(got_mode&0xC0)!=(accessModeByte(user)&0xC0)){
            std::ostringstream m;m<<"Контрольное чтение не совпало: ожидалось "<<util::formatCardId(uid1,uid2)<<", получено "<<util::formatCardId(got1,got2)<<", active="<<(enabled?"1":"0")<<", PIN="<<(got_pin==pin?"OK":"DIFF")<<", mode="<<(((got_mode&0xC0)==(accessModeByte(user)&0xC0))?"OK":"DIFF");
            return{false,"error",m.str()};
        }
        std::ostringstream m;m<<"Записан и проверен: адрес "<<user.controller_port<<", карта "<<util::formatCardSeries(uid1)<<" / "<<uid2<<", режим "<<accessModeText(user);if(!user.pin_code.empty())m<<", PIN записан";
        return{true,"ok",m.str()};
    }
    if(code==NACK)return{false,"error","Контроллер вернул NACK на запись пользователя"};
    std::ostringstream m;m<<"Неожиданный код ответа 0x"<<std::hex<<std::uppercase<<static_cast<int>(code);
    return{false,"error",m.str()};
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::deleteUser(std::uint8_t node,const User& user){
    if(user.controller_port<=0||user.controller_port>16383)
        return{false,"error","Порт/адрес пользователя должен быть 1..16383"};
    const auto address=static_cast<std::uint16_t>(user.controller_port);

    // Exact-slot deletion: use H-series Set Card (0x84) for one address,
    // UID1/UID2 = 0xFFFF and mode=0. This mirrors disableCard() in the
    // protocol reference and avoids the range semantics of command 0x85.
    const std::vector<std::uint8_t> data={
        1,
        static_cast<std::uint8_t>((address>>8)&0xFF),static_cast<std::uint8_t>(address&0xFF),
        0,0,0,0,
        0xFF,0xFF,
        0xFF,0xFF,
        0,0,0,0,
        0x00,
        0x00,
        0xFF,0xFF,
        99,12,31,
        0x00,
        0,0,0,0
    };
    auto r=transactExtended(node,0x84,data,350);
    if(!r)return{false,"error","Нет корректного ответа Extended Protocol на удаление пользователя (0x84)"};
    if(r->size()<8)return{false,"error","Короткий ответ контроллера при удалении пользователя"};
    const auto code=(*r)[7];
    if(code==NACK)return{false,"error","Контроллер вернул NACK при удалении пользователя"};
    if(code!=ACK){
        std::ostringstream m;m<<"Неожиданный код ответа 0x"<<std::hex<<std::uppercase<<static_cast<int>(code);
        return{false,"error",m.str()};
    }

    const std::vector<std::uint8_t> read_data={
        static_cast<std::uint8_t>((address>>8)&0xFF),static_cast<std::uint8_t>(address&0xFF),0x01
    };
    auto verify=transactExtended(node,0x87,read_data,350);
    if(!verify||verify->size()<=22)return{false,"error","Контроллер подтвердил удаление, но контрольное чтение 0x87 не удалось"};
    if((*verify)[7]==NACK)return{false,"error","Контроллер вернул NACK при контрольном чтении удалённого пользователя"};

    const std::uint16_t got1=(static_cast<std::uint16_t>((*verify)[13])<<8)|(*verify)[14];
    const std::uint16_t got2=(static_cast<std::uint16_t>((*verify)[15])<<8)|(*verify)[16];
    const bool enabled=(*verify)[21]>0;
    if(enabled||got1!=0xFFFF||got2!=0xFFFF){
        std::ostringstream m;
        m<<"Удаление не подтверждено: адрес "<<address<<", UID "<<util::formatCardId(got1,got2)
         <<", active="<<(enabled?"1":"0");
        return{false,"error",m.str()};
    }
    std::ostringstream m;m<<"Удалён и проверен: адрес "<<address;
    return{true,"ok",m.str()};
}


Unex721Protocol::UserReadOutcome Unex721Protocol::readUser(std::uint8_t node,int address){
    UserReadOutcome out;
    out.address=address;
    if(address<=0||address>16383){
        out.message="Адрес пользователя должен быть 1..16383";
        return out;
    }
    const auto a=static_cast<std::uint16_t>(address);
    const std::vector<std::uint8_t> read_data={
        static_cast<std::uint8_t>((a>>8)&0xFF),static_cast<std::uint8_t>(a&0xFF),0x01
    };
    auto r=transactExtended(node,0x87,read_data,300);
    if(!r){out.message="Нет корректного ответа на 0x87";return out;}
    if(r->size()<22){out.message="Короткий ответ контроллера на 0x87";return out;}
    if((*r)[7]==NACK){out.message="Контроллер вернул NACK на чтение 0x87";return out;}

    out.uid1=(static_cast<std::uint16_t>((*r)[13])<<8)|(*r)[14];
    out.uid2=(static_cast<std::uint16_t>((*r)[15])<<8)|(*r)[16];
    out.pin=(static_cast<std::uint32_t>((*r)[17])<<24)|
            (static_cast<std::uint32_t>((*r)[18])<<16)|
            (static_cast<std::uint32_t>((*r)[19])<<8)|(*r)[20];
    out.mode=(*r)[21];
    out.enabled=out.mode>0;
    out.present=!(out.mode==0&&out.uid1==0xFFFF&&out.uid2==0xFFFF);
    out.access_mode=accessModeFromByte(out.mode);
    out.ok=true;

    std::ostringstream m;
    if(!out.present)m<<"Адрес "<<address<<" пуст";
    else{
        m<<"Адрес "<<address<<": карта "<<util::formatCardSeries(out.uid1)<<" / "<<out.uid2
         <<", "<<(out.enabled?"активен":"отключен")
         <<", PIN "<<(out.pin?"задан":"нет");
    }
    out.message=m.str();
    return out;
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
        e.card=util::formatCardId(uid1,uid2);
        e.event_code=f[7];
    }
    return e;
}
}
