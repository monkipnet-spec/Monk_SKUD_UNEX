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

void Unex721Protocol::trace(const std::string& direction,int node,int command,const std::string& protocol,const std::vector<std::uint8_t>& frame,const std::string& message) const{
    if(trace_)trace_(direction,node,command,protocol,frame,message);
}

std::optional<std::vector<std::uint8_t>> Unex721Protocol::transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout){
    auto q=frame(node,cmd,data);
    trace("TX",node,cmd,"0x7E",q);
    if(!port_.writeAll(q)){trace("INFO",node,cmd,"0x7E",q,"ошибка записи в COM");return std::nullopt;}
    // Some USB-RS485 adapters echo transmitted bytes back to RX. Do not
    // mistake that echo for a controller reply; keep it visible in Live protocol.
    for(int attempt=0;attempt<3;++attempt){
        auto r=port_.readFrame(timeout);
        if(r.empty())return std::nullopt;
        const bool valid=validFrame(r);
        trace("RX",node,cmd,"0x7E",r,!valid?"неверный XOR/SUM":(r==q?"TX echo — проигнорировано":""));
        if(!valid)continue;
        if(r==q)continue;
        return r;
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> Unex721Protocol::transactExtended(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout){
    auto q=extendedFrame(node,cmd,data);
    if(q.empty())return std::nullopt;
    trace("TX",node,cmd,"Extended",q);
    if(!port_.writeAll(q)){trace("INFO",node,cmd,"Extended",q,"ошибка записи в COM");return std::nullopt;}
    // See transact(): extended packets may be locally echoed as well.
    for(int attempt=0;attempt<3;++attempt){
        auto r=port_.readExtendedFrame(timeout);
        if(r.empty())return std::nullopt;
        const bool valid=validExtendedFrame(r);
        trace("RX",node,cmd,"Extended",r,!valid?"неверный XOR/SUM":(r==q?"TX echo — проигнорировано":""));
        if(!valid)continue;
        if(r==q)continue;
        return r;
    }
    return std::nullopt;
}

bool Unex721Protocol::ping(std::uint8_t node){
    // Real UNEX 721 hardware is confirmed to answer standard 0x7E packets.
    // Avoid an Extended timeout on every availability check.
    if(transact(node,0x25,{},100))return true;
    return transactExtended(node,0x18,{},120).has_value();
}

std::optional<RawUnexEvent> Unex721Protocol::getOldestEvent(std::uint8_t node){
    // The real UNEX 721 answers 25H through the standard 0x7E protocol and
    // ignores Extended frames. Use the proven path first so every poll does
    // not waste ~180 ms waiting for an Extended timeout.
    if(auto r=transact(node,0x25,{},120)){
        if(r->size()<18)return std::nullopt; // short ACK/no-event response
        return decodeEvent(node,*r);
    }
    if(auto r=transactExtended(node,0x25,{},180)){
        if(r->size()>=8&&(*r)[7]==ACK)return std::nullopt;
        if(r->size()>=30)return decodeExtendedEvent(node,*r);
    }
    return std::nullopt;
}

bool Unex721Protocol::removeOldestEvent(std::uint8_t node){
    // Same as 25H: standard 0x7E is the confirmed UNEX 721 transport.
    if(auto r=transact(node,0x37,{},120))return true;
    if(auto r=transactExtended(node,0x37,{},180))return r->size()>=8&&(*r)[7]!=NACK;
    return false;
}

bool Unex721Protocol::setSystemTime(std::uint8_t node){
    std::time_t t=std::time(nullptr);std::tm tm{};localtime_r(&t,&tm);std::vector<std::uint8_t>d={static_cast<std::uint8_t>(tm.tm_sec),static_cast<std::uint8_t>(tm.tm_min),static_cast<std::uint8_t>(tm.tm_hour),static_cast<std::uint8_t>(tm.tm_wday+1),static_cast<std::uint8_t>(tm.tm_mday),static_cast<std::uint8_t>(tm.tm_mon+1),static_cast<std::uint8_t>((tm.tm_year+1900)%100)};
    if(auto r=transact(node,0x23,d,180))return r->size()>=4&&(*r)[3]!=NACK;
    if(auto r=transactExtended(node,0x23,d,180))return r->size()>=8&&(*r)[7]!=NACK;
    return false;
}

bool Unex721Protocol::userWriteSupported(){return false;}
std::string Unex721Protocol::userWriteSupportMessage(){return "Транспорт записи UNEX 721 исправлен на H-series standard 0x7E / 20H Write EEPROM с контрольным 12H read-back. Старый ошибочный Extended 0x84 отключён. Выгрузка пользовательских карт пока защищённо заблокирована до подтверждения адресов H-series user EEPROM, чтобы не повредить действующую базу контроллера.";}

Unex721Protocol::UserWriteOutcome Unex721Protocol::writeUser(std::uint8_t node,const User& user){
    (void)node;
    (void)user;
    return{false,"blocked_protocol",userWriteSupportMessage()};
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::deleteUser(std::uint8_t node,const User& user){
    (void)node;
    (void)user;
    return{false,"blocked_protocol","Удаление пользователя защищённо заблокировано: старый Extended 0x84/0x85 для этого UNEX 721 отключён; сначала требуется подтвердить H-series user EEPROM layout через 12H, после чего запись будет выполняться 20H с read-back"};
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
        // Real UNEX 721 capture. Known control samples disproved the old
        // assumption that bytes 0..3 are direct series:number:
        // user 7 = card 112:53910, PIN 0031 -> RAW 00 A4 31 F1 00 00 01 01;
        // user 5 = card 40:32010, PIN 1234 -> RAW 00 00 00 00 04 D2 00 00.
        // Keep the whole record raw and do not invent card/PIN semantics.
        const bool all_zero=std::all_of(record.begin(),record.end(),[](std::uint8_t b){return b==0x00;});
        out.present=!all_zero;
        out.enabled=out.present;
        out.uid1=0;out.uid2=0;out.pin=0;out.mode=0;out.access_mode.clear();
        out.card_known=false;out.details_known=false;out.ok=true;
        std::ostringstream m;
        if(out.present)m<<"Адрес "<<address<<": compact H/UNEX 8B, карта/PIN не декодированы; RAW="<<out.raw_record_hex;
        else m<<"Адрес "<<address<<" пуст [H/UNEX compact 8B; RAW="<<out.raw_record_hex<<"]";
        out.message=m.str();return out;
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


Unex721Protocol::EepromReadOutcome Unex721Protocol::readEeprom(std::uint8_t node,int address,int length){
    EepromReadOutcome out;out.address=address;
    if(address<0||address>0xFFFF){out.message="Адрес EEPROM должен быть 0000..FFFF";return out;}
    if(length<1||length>240||address+length-1>0xFFFF){out.message="Длина чтения EEPROM должна быть 1..240 и не выходить за FFFF";return out;}
    const std::vector<std::uint8_t> data={
        static_cast<std::uint8_t>((address>>8)&0xFF),
        static_cast<std::uint8_t>(address&0xFF),
        static_cast<std::uint8_t>(length)
    };
    // H-series CommView command: 12H + EEPROM address H/L + byte count.
    auto r=transact(node,0x12,data,450);
    if(!r){out.message="Нет корректного ответа на 12H Read EEPROM";return out;}
    out.raw_frame_hex=util::hex(*r);
    if(r->size()<6){out.message="Короткий ответ на 12H: "+out.raw_frame_hex;return out;}
    const auto code=(*r)[3];
    if(code==NACK){out.message="Контроллер вернул NACK на 12H: "+out.raw_frame_hex;return out;}
    if(code!=0x03){
        std::ostringstream m;m<<"Неожиданный ответ 12H code=0x"<<std::hex<<std::uppercase<<static_cast<int>(code)<<": "<<out.raw_frame_hex;
        out.message=m.str();return out;
    }
    // Standard SOYAL Data Echo observed on the real UNEX 721:
    // 7E LEN 00 03 SOURCE <data...> XOR SUM.
    if(r->size()<7){out.message="Короткий Data Echo на 12H: "+out.raw_frame_hex;return out;}
    const std::size_t data_begin=5;
    const std::size_t data_end=r->size()-2;
    if(data_end<data_begin||data_end-data_begin<static_cast<std::size_t>(length)){
        std::ostringstream m;m<<"12H вернул "<<(data_end>=data_begin?data_end-data_begin:0)<<" байт вместо "<<length<<": "<<out.raw_frame_hex;
        out.message=m.str();return out;
    }
    out.data.assign(r->begin()+static_cast<std::ptrdiff_t>(data_begin),r->begin()+static_cast<std::ptrdiff_t>(data_begin+length));
    out.ok=true;
    std::ostringstream m;m<<"12H EEPROM 0x"<<std::hex<<std::uppercase<<address<<" +"<<std::dec<<length<<" байт";
    out.message=m.str();return out;
}


Unex721Protocol::EepromWriteOutcome Unex721Protocol::writeEeprom(std::uint8_t node,int address,const std::vector<std::uint8_t>& bytes){
    EepromWriteOutcome out;out.address=address;out.length=static_cast<int>(bytes.size());
    if(address<0||address>0xFFFF){out.message="Адрес EEPROM должен быть 0000..FFFF";return out;}
    if(bytes.empty()||bytes.size()>240||static_cast<std::size_t>(address)+bytes.size()-1>0xFFFF){
        out.message="Длина записи EEPROM должна быть 1..240 и не выходить за FFFF";return out;
    }
    std::vector<std::uint8_t> data={
        static_cast<std::uint8_t>((address>>8)&0xFF),
        static_cast<std::uint8_t>(address&0xFF),
        static_cast<std::uint8_t>(bytes.size())
    };
    data.insert(data.end(),bytes.begin(),bytes.end());
    // H-series documented transport: 20H Write EEPROM over the standard 0x7E frame.
    // Never report success on ACK alone: read the same bytes back via 12H and compare.
    auto r=transact(node,0x20,data,500);
    if(!r){out.message="Нет корректного ответа на 20H Write EEPROM";return out;}
    out.raw_frame_hex=util::hex(*r);
    if(r->size()<6){out.message="Короткий ответ на 20H: "+out.raw_frame_hex;return out;}
    const auto code=(*r)[3];
    if(code==NACK){out.message="Контроллер вернул NACK на 20H: "+out.raw_frame_hex;return out;}
    if(code!=ACK){
        std::ostringstream m;m<<"Неожиданный ответ 20H code=0x"<<std::hex<<std::uppercase<<static_cast<int>(code)<<": "<<out.raw_frame_hex;
        out.message=m.str();return out;
    }
    auto verify=readEeprom(node,address,static_cast<int>(bytes.size()));
    if(!verify.ok){out.message="20H ACK получен, но 12H read-back не удался: "+verify.message;return out;}
    if(verify.data!=bytes){
        out.message="20H ACK получен, но контрольное 12H чтение не совпало: expected="+util::hex(bytes)+" got="+util::hex(verify.data);
        return out;
    }
    out.ok=true;
    std::ostringstream m;m<<"20H EEPROM 0x"<<std::hex<<std::uppercase<<address<<" +"<<std::dec<<bytes.size()<<" байт записано и подтверждено 12H read-back";
    out.message=m.str();return out;
}


RawUnexEvent Unex721Protocol::decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f)const{
    RawUnexEvent e;
    e.node=node;
    e.frame=f;
    e.raw_hex=util::hex(f);

    // Real UNEX 721 / H-series 25H event captured on hardware:
    // 7E 1D 00 0B SRC SS MM HH WD DD MO YY ... SH SL ... NH NL ... XOR SUM
    // For the known card 112:53910 the controller returned:
    // ... 00 70 02 10 D2 96 ...
    // Hence card series is bytes 19..20 and card number is bytes 23..24
    // (zero-based frame indexes). This mapping is now confirmed by a real
    // successful green-light access event, not inferred from 87H user data.
    if(f.size()>=31 && f[0]==0x7E && f[3]==0x0B){
        e.event_code=0x0B;
        const std::uint16_t series=(static_cast<std::uint16_t>(f[19])<<8)|f[20];
        const std::uint16_t number=(static_cast<std::uint16_t>(f[23])<<8)|f[24];
        if(!(series==0&&number==0) && !(series==0xFFFF&&number==0xFFFF))
            e.card=util::formatCardId(series,number);
        return e;
    }

    // Keep unknown/short event formats visible as RAW without guessing fields.
    for(std::size_t i=2;i+2<f.size();++i)if(f[i]==0x0B){e.event_code=0x0B;break;}
    return e;
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
