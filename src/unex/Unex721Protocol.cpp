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
    auto q=frame(node,cmd,data);
    if(!port_.writeAll(q))return std::nullopt;
    // Some USB-RS485 adapters echo transmitted bytes back to RX.  Do not
    // mistake that echo for a controller reply; wait for the next valid frame.
    for(int attempt=0;attempt<3;++attempt){
        auto r=port_.readFrame(timeout);
        if(r.empty())return std::nullopt;
        if(!validFrame(r))continue;
        if(r==q)continue;
        return r;
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> Unex721Protocol::transactExtended(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout){
    auto q=extendedFrame(node,cmd,data);
    if(q.empty()||!port_.writeAll(q))return std::nullopt;
    // See transact(): extended packets may be locally echoed as well.
    for(int attempt=0;attempt<3;++attempt){
        auto r=port_.readExtendedFrame(timeout);
        if(r.empty())return std::nullopt;
        if(!validExtendedFrame(r))continue;
        if(r==q)continue;
        return r;
    }
    return std::nullopt;
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

bool Unex721Protocol::userWriteSupported(){return false;}
std::string Unex721Protocol::userWriteSupportMessage(){return "UNEX 721 подтверждён как compact H-series: чтение 0x87 работает стандартным 0x7E с 8-байтной записью; аппаратная запись временно заблокирована до подтверждения точного H-series формата записи/PIN";}

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
        auto verify=readUser(node,address);
        if(!verify.ok)return{false,"error","Контроллер подтвердил 0x84, но контрольное чтение 0x87 не удалось: "+verify.message};
        const std::uint16_t got1=verify.uid1;
        const std::uint16_t got2=verify.uid2;
        const std::uint32_t got_pin=verify.pin;
        const std::uint8_t got_mode=verify.mode;
        const bool enabled=verify.enabled;
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
    (void)node;
    (void)user;
    return{false,"blocked_protocol","Удаление из контроллера временно заблокировано: фактический UNEX 721 использует compact H-series 8-byte user record; точный формат безопасной записи ещё уточняется"};
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

    std::optional<std::vector<std::uint8_t>> r;
    bool standard=false;
    std::string standard_diag="таймаут/нет кадра";
    std::string extended_diag="таймаут/нет кадра";

    if(auto sr=transact(node,0x87,read_data,250)){
        // Real UNEX 721 capture (2026-09-01) returns a standard Data Echo:
        // 7E 0D 00 03 SID [8-byte compact H-series record] XOR SUM.
        // Enterprise controllers may return a longer 24-byte record.
        if(sr->size()<7){
            standard_diag="слишком короткий кадр RAW="+util::hex(*sr);
        }else if((*sr)[3]==NACK){
            standard_diag="NACK RAW="+util::hex(*sr);
        }else if((*sr)[3]!=0x03){
            std::ostringstream d;d<<"функция 0x"<<std::hex<<std::uppercase<<static_cast<int>((*sr)[3])<<" RAW="<<util::hex(*sr);standard_diag=d.str();
        }else{
            r=std::move(sr);standard=true;standard_diag="OK";
        }
    }
    if(!r){
        if(auto er=transactExtended(node,0x87,read_data,350)){
            if(er->size()<11){
                extended_diag="слишком короткий кадр RAW="+util::hex(*er);
            }else if((*er)[7]==NACK){
                extended_diag="NACK RAW="+util::hex(*er);
            }else if((*er)[7]!=0x03){
                std::ostringstream d;d<<"функция 0x"<<std::hex<<std::uppercase<<static_cast<int>((*er)[7])<<" RAW="<<util::hex(*er);extended_diag=d.str();
            }else{
                r=std::move(er);standard=false;extended_diag="OK";
            }
        }
    }
    if(!r){
        out.message="Нет корректного ответа на 0x87: 0x7E="+standard_diag+", Extended="+extended_diag;
        return out;
    }

    const std::size_t data_begin=standard?5:9; // after Node=00, Function=03, Source ID
    if(r->size()<data_begin+2){
        out.message="Ответ 0x87 не содержит пользовательских данных RAW="+util::hex(*r);
        return out;
    }
    const std::size_t data_end=r->size()-2; // XOR/SUM
    if(data_end<data_begin){
        out.message="Некорректная длина ответа 0x87 RAW="+util::hex(*r);
        return out;
    }
    const std::size_t data_len=data_end-data_begin;
    std::vector<std::uint8_t> record(r->begin()+static_cast<std::ptrdiff_t>(data_begin),r->begin()+static_cast<std::ptrdiff_t>(data_end));
    out.raw_record_hex=util::hex(record);

    if(data_len==8){
        // Compact Home-series/UNEX record observed on the real UNEX 721.
        // The first four bytes map cleanly to the documented H-series
        // decimal Site Code / Card Code pair (two big-endian 16-bit words).
        // The remaining four bytes are deliberately kept raw until the
        // exact UNEX PIN/access-mode layout is confirmed on known test data.
        out.uid1=(static_cast<std::uint16_t>(record[0])<<8)|record[1];
        out.uid2=(static_cast<std::uint16_t>(record[2])<<8)|record[3];
        const bool all_zero=std::all_of(record.begin(),record.end(),[](std::uint8_t b){return b==0x00;});
        const bool uid_ff=out.uid1==0xFFFF&&out.uid2==0xFFFF;
        out.present=!all_zero&&!uid_ff;
        out.enabled=out.present;
        out.pin=0;
        out.mode=0;
        out.access_mode.clear();
        out.details_known=false;
        out.ok=true;
        std::ostringstream m;
        if(out.present){
            m<<"Адрес "<<address<<": карта "<<util::formatCardSeries(out.uid1)<<" / "<<out.uid2
             <<" [H/UNEX compact 8B; PIN/режим пока не декодированы; RAW="<<out.raw_record_hex<<"]";
        }else{
            m<<"Адрес "<<address<<" пуст [H/UNEX compact 8B; RAW="<<out.raw_record_hex<<"]";
        }
        out.message=m.str();
        return out;
    }

    // Enterprise-style 24-byte user record.  The user record starts with
    // four bytes before UID1, matching the public AR-727H/E-series library.
    if(data_len>=24){
        const std::size_t uid1_hi=data_begin+4;
        const std::size_t pin0=uid1_hi+4;
        const std::size_t mode_i=uid1_hi+8;
        if(mode_i>=data_end){
            out.message="Неполная 24-байтная запись 0x87 RAW="+util::hex(*r);
            return out;
        }
        out.uid1=(static_cast<std::uint16_t>((*r)[uid1_hi])<<8)|(*r)[uid1_hi+1];
        out.uid2=(static_cast<std::uint16_t>((*r)[uid1_hi+2])<<8)|(*r)[uid1_hi+3];
        out.pin=(static_cast<std::uint32_t>((*r)[pin0])<<24)|
                (static_cast<std::uint32_t>((*r)[pin0+1])<<16)|
                (static_cast<std::uint32_t>((*r)[pin0+2])<<8)|(*r)[pin0+3];
        out.mode=(*r)[mode_i];
        out.enabled=out.mode>0;
        out.present=!(out.mode==0&&out.uid1==0xFFFF&&out.uid2==0xFFFF);
        out.access_mode=accessModeFromByte(out.mode);
        out.details_known=true;
        out.ok=true;
        std::ostringstream m;
        if(!out.present)m<<"Адрес "<<address<<" пуст";
        else m<<"Адрес "<<address<<": карта "<<util::formatCardSeries(out.uid1)<<" / "<<out.uid2
              <<", "<<(out.enabled?"активен":"отключен")<<", PIN "<<(out.pin?"задан":"нет");
        m<<" ["<<(standard?"0x7E":"Extended")<<", 24B]";
        out.message=m.str();
        return out;
    }

    std::ostringstream m;
    m<<"Неизвестный формат пользовательской записи 0x87: "<<data_len<<" байт, RAW="<<out.raw_record_hex;
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
