#include "skud/Unex721Protocol.h"
#include "skud/SerialPort.h"
#include "skud/Util.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <thread>
#include <limits>
#include <sstream>

namespace skud {
namespace {
constexpr std::uint8_t ACK=0x04;
constexpr std::uint8_t NACK=0x05;

bool parsePin(const std::string& pin,std::uint32_t& out,std::string& error){
    const auto s=util::trim(pin);out=0;
    if(s.empty())return true;
    if(s.size()!=4||!std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isdigit(c);})){error="PIN должен состоять ровно из 4 цифр";return false;}
    try{
        const int v=std::stoi(s);
        if(v<1||v>9999){error="PIN должен быть в диапазоне 0001..9999";return false;}
        out=static_cast<std::uint32_t>(v);return true;
    }catch(...){error="Неверный PIN";return false;}
}

// AR-721H/727H Protocol v1.2, command 83H/87H:
// 00 = Invalid, 01 = Card Only (manual says "Read Only" on 83H page),
// 02 = Card OR PIN, 03 = Card + PIN.
// Section 3.1 has a conflicting 02/03 comment; the command pages 35/36 are
// internally consistent, so the wire implementation follows 83H/87H.
std::uint8_t hAccessModeByte(const User& user){
    if(!user.enabled)return 0x00;
    if(user.access_mode=="card_or_pin")return 0x02;
    if(user.access_mode=="card_and_pin")return 0x03;
    return 0x01;
}

std::string hAccessModeFromByte(std::uint8_t mode){
    switch(mode){
        case 0x02:return "card_or_pin";
        case 0x03:return "card_and_pin";
        case 0x00:
        case 0x01:return "card";
        default:return {};
    }
}

std::string bytesHex(const std::vector<std::uint8_t>& bytes){
    return util::hex(bytes);
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

bool Unex721Protocol::userWriteSupported(){return true;}
std::string Unex721Protocol::userWriteSupportMessage(){
    return "H-series user upload: official 83H Set User Data over standard 0x7E, User Address 0..1023, with mandatory 87H byte-for-byte read-back verification. 84H is not used for writing.";
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::writeUser(std::uint8_t node,const User& user){
    const int address=user.controller_port;
    if(address<0||address>1023)
        return{false,"skipped","Для AR-721H/727H допустимый User Address 0..1023 (по Memory Layout протокола)"};

    std::string card=user.card;
    if(card.empty()&&!user.cards.empty())card=user.cards.front();
    std::uint16_t site=0,card_code=0;
    std::string error;
    if(!util::parseCardId(card,site,card_code,&error))
        return{false,"invalid_user","Некорректная основная карта: "+error};

    std::uint32_t pin32=0;
    if(!parsePin(user.pin_code,pin32,error))return{false,"invalid_user",error};
    if((user.access_mode=="card_or_pin"||user.access_mode=="card_and_pin")&&pin32==0)
        return{false,"invalid_user","Для режима доступа с PIN необходимо задать PIN пользователя"};
    const auto pin=static_cast<std::uint16_t>(pin32);

    // Safety pre-read. For an existing slot preserve its valid time-zone.
    // A new/empty slot defaults to time zone 1, matching the 83H manual example.
    auto before=readUser(node,address);
    if(!before.ok)
        return{false,"precheck_failed","83H не отправлен: не удалось выполнить контрольный 87H до записи: "+before.message};
    std::uint8_t zone=0x01;
    if(before.present&&before.raw_record.size()==8&&before.raw_record[7]<=0x0B)zone=before.raw_record[7];

    const auto mode=hAccessModeByte(user);
    std::vector<std::uint8_t> expected={
        static_cast<std::uint8_t>((site>>8)&0xFF),static_cast<std::uint8_t>(site&0xFF),
        static_cast<std::uint8_t>((card_code>>8)&0xFF),static_cast<std::uint8_t>(card_code&0xFF),
        static_cast<std::uint8_t>((pin>>8)&0xFF),static_cast<std::uint8_t>(pin&0xFF),
        mode,zone
    };
    const auto a=static_cast<std::uint16_t>(address);
    std::vector<std::uint8_t> payload={
        static_cast<std::uint8_t>((a>>8)&0xFF),static_cast<std::uint8_t>(a&0xFF)
    };
    payload.insert(payload.end(),expected.begin(),expected.end());

    auto ack=transact(node,0x83,payload,500);
    if(!ack)return{false,"write_timeout","Нет ответа на 83H Set User Data"};
    if(ack->size()<6)return{false,"write_error","Короткий ответ на 83H: "+util::hex(*ack)};
    if((*ack)[3]==NACK)return{false,"write_nack","Контроллер вернул NACK на 83H: "+util::hex(*ack)};
    if((*ack)[3]!=ACK){
        std::ostringstream m;m<<"Неожиданный ответ 83H code=0x"<<std::hex<<std::uppercase<<static_cast<int>((*ack)[3])<<": "<<util::hex(*ack);
        return{false,"write_error",m.str()};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    auto after=readUser(node,address);
    if(!after.ok)
        return{false,"verify_failed","83H получил ACK, но контрольный 87H не удался: "+after.message};
    if(after.raw_record.size()!=8)
        return{false,"verify_failed","83H получил ACK, но 87H вернул неожиданный формат "+std::to_string(after.raw_record.size())+"B: "+after.raw_record_hex};
    if(after.raw_record!=expected){
        return{false,"verify_failed","83H получил ACK, но данные НЕ записались. Ожидалось 87H RAW="+bytesHex(expected)+", получено="+after.raw_record_hex};
    }

    std::ostringstream m;
    m<<"Записано и подтверждено 87H: address="<<address
     <<", карта="<<util::formatCardId(site,card_code)
     <<", PIN="<<(pin?user.pin_code:"0000")
     <<", mode="<<static_cast<int>(mode)
     <<", zone="<<static_cast<int>(zone)
     <<", RAW="<<after.raw_record_hex;
    if(user.cards.size()>1)m<<"; в контроллер записана основная карта (у пользователя локально карт: "<<user.cards.size()<<")";
    return{true,"written_verified",m.str()};
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::disablePassAnyCards(std::uint8_t node){
    // AR-721H H-series compound command 24* is stored in EEPROM address 0x16
    // ("Assigned function setting").  Current SOYAL H-series documentation
    // assigns weighted value 032 / bit 5 to "swipe any tags to release door open".
    // Preserve every other option and clear only bit 0x20.  Always rewrite the
    // byte even when the bit already reads as zero: some UNEX-compatible
    // firmware keeps an access index/cache that is refreshed only after a write.
    auto before=readEeprom(node,0x0016,1);
    if(!before.ok||before.data.size()!=1)
        return{false,"pass_any_read_failed","Не удалось прочитать EEPROM 0x0016 (параметр 24*): "+before.message};

    const std::uint8_t old_value=before.data[0];
    const std::uint8_t new_value=static_cast<std::uint8_t>(old_value & static_cast<std::uint8_t>(~0x20u));
    auto wr=writeEeprom(node,0x0016,{new_value});
    if(!wr.ok)
        return{false,"pass_any_write_failed","Не удалось отключить 'любая карта': было 24*="+std::to_string(old_value)+", нужно "+std::to_string(new_value)+". "+wr.message};

    std::ostringstream m;
    m<<"Pass Any Cards отключён: EEPROM 0x0016 / 24* "
     <<static_cast<int>(old_value)<<" -> "<<static_cast<int>(new_value)
     <<" (снят bit 0x20), подтверждено 12H read-back";
    return{true, old_value==new_value?"pass_any_rewritten_disabled":"pass_any_disabled", m.str()};
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::clearAllUsers(std::uint8_t node){
    // AR-721H/727H Protocol v1.2, command 85H: Clearing All Card Content.
    // The manual notes that processing can take about 10 seconds, so use a
    // deliberately long timeout. This command is used only for an explicit
    // full synchronization requested by the operator.
    auto ack=transact(node,0x85,{},12000);
    if(!ack)return{false,"clear_timeout","Нет ответа на 85H Clear All Users (ожидание 12 с)"};
    if(ack->size()<6)return{false,"clear_error","Короткий ответ на 85H: "+util::hex(*ack)};
    if((*ack)[3]==NACK)return{false,"clear_nack","Контроллер вернул NACK на 85H: "+util::hex(*ack)};
    if((*ack)[3]!=ACK)return{false,"clear_error","Неожиданный ответ 85H: "+util::hex(*ack)};
    return{true,"cleared_all","Все пользовательские ячейки очищены командой 85H; далее будут записаны только пользователи полной выгрузки"};
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::clearUserSlot(std::uint8_t node,int address){
    if(address<0||address>1023)
        return{false,"clear_slot_invalid","Для AR-721H/727H допустимый User Address 0..1023"};

    // Full synchronization must actively rewrite every absent address, even if
    // 87H already returns eight zero bytes.  Real UNEX 721 hardware has been
    // observed to report 87H(1023)=zeros while still granting an old card as
    // Normal Access user=1023.  A real 83H write is therefore required to make
    // the controller rebuild/refresh its internal access entry for the slot.
    const std::vector<std::uint8_t> zeros(8,0x00);
    const auto a=static_cast<std::uint16_t>(address);
    std::vector<std::uint8_t> payload={
        static_cast<std::uint8_t>((a>>8)&0xFF),static_cast<std::uint8_t>(a&0xFF)
    };
    payload.insert(payload.end(),zeros.begin(),zeros.end());

    auto ack=transact(node,0x83,payload,500);
    if(!ack)return{false,"clear_slot_timeout","Нет ответа на 83H при принудительной очистке User Address "+std::to_string(address)};
    if(ack->size()<6)return{false,"clear_slot_error","Короткий ответ на 83H: "+util::hex(*ack)};
    if((*ack)[3]==NACK)return{false,"clear_slot_nack","Контроллер вернул NACK на 83H при очистке User Address "+std::to_string(address)+": "+util::hex(*ack)};
    if((*ack)[3]!=ACK)return{false,"clear_slot_error","Неожиданный ответ 83H при очистке User Address "+std::to_string(address)+": "+util::hex(*ack)};

    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    auto after=readUser(node,address);
    if(!after.ok||after.raw_record.size()!=8)
        return{false,"clear_slot_verify_failed","83H ACK получен, но контрольный 87H после очистки не удался для User Address "+std::to_string(address)};
    if(after.raw_record!=zeros)
        return{false,"clear_slot_verify_failed","83H ACK получен, но User Address "+std::to_string(address)+" не очищен. Ожидалось 00 00 00 00 00 00 00 00, получено="+after.raw_record_hex};

    return{true,"slot_cleared_verified","User Address "+std::to_string(address)+" принудительно записан нулями через 83H и подтверждён 87H"};
}

Unex721Protocol::UserWriteOutcome Unex721Protocol::deleteUser(std::uint8_t node,const User& user){
    const int address=user.controller_port;
    if(address<0||address>1023)
        return{false,"skipped","Для AR-721H/727H допустимый User Address 0..1023"};

    // Official 83H defines Mode=00 as Invalid.  Disable the slot with the
    // smallest possible change: preserve Site/Card/PIN/Zone and change only
    // Access Mode to 00.  This is reversible and avoids 85H (clear all users).
    auto before=readUser(node,address);
    if(!before.ok)
        return{false,"precheck_failed","Удаление не выполнено: 87H до записи не удался: "+before.message};
    if(before.raw_record.size()!=8)
        return{false,"unsupported_record","Удаление требует официальную H-series 8B запись 87H; получено: "+before.raw_record_hex};
    if(before.raw_record[6]==0)
        return{true,"already_invalid","User Address "+std::to_string(address)+" уже имеет Mode=0 (Invalid), RAW="+before.raw_record_hex};

    auto expected=before.raw_record;
    expected[6]=0x00;
    const auto a=static_cast<std::uint16_t>(address);
    std::vector<std::uint8_t> payload={
        static_cast<std::uint8_t>((a>>8)&0xFF),static_cast<std::uint8_t>(a&0xFF)
    };
    payload.insert(payload.end(),expected.begin(),expected.end());

    auto ack=transact(node,0x83,payload,500);
    if(!ack)return{false,"delete_timeout","Нет ответа на 83H при отключении User Address "+std::to_string(address)};
    if(ack->size()<6)return{false,"delete_error","Короткий ответ на 83H: "+util::hex(*ack)};
    if((*ack)[3]==NACK)return{false,"delete_nack","Контроллер вернул NACK на 83H: "+util::hex(*ack)};
    if((*ack)[3]!=ACK)return{false,"delete_error","Неожиданный ответ 83H: "+util::hex(*ack)};

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    auto after=readUser(node,address);
    if(!after.ok||after.raw_record.size()!=8)
        return{false,"verify_failed","83H ACK получен, но контрольный 87H после отключения не удался"};
    if(after.raw_record!=expected)
        return{false,"verify_failed","83H ACK получен, но Mode=0 не подтверждён. Ожидалось RAW="+bytesHex(expected)+", получено="+after.raw_record_hex};

    std::ostringstream m;
    m<<"User Address "<<address<<" отключён и подтверждён 87H (Mode=0 Invalid): карта "
     <<util::formatCardId(before.uid1,before.uid2)<<", RAW="<<after.raw_record_hex;
    return{true,"invalidated_verified",m.str()};
}

Unex721Protocol::UserReadOutcome Unex721Protocol::readUser(std::uint8_t node,int address){
    UserReadOutcome out;
    out.address=address;
    if(address<0||address>1023){
        out.message="Адрес пользователя должен быть 0..1023 для AR-721H/727H";
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
    out.raw_record=record;
    out.raw_record_hex=util::hex(record);

    if(data_len==8){
        // Official AR-721H/727H 87H record (Protocol v1.2, section 2.20):
        // SiteH SiteL CardH CardL PINH PINL Mode Zone.
        const bool all_zero=std::all_of(record.begin(),record.end(),[](std::uint8_t b){return b==0x00;});
        out.uid1=(static_cast<std::uint16_t>(record[0])<<8)|record[1];
        out.uid2=(static_cast<std::uint16_t>(record[2])<<8)|record[3];
        out.pin=(static_cast<std::uint32_t>(record[4])<<8)|record[5];
        out.mode=record[6];
        out.access_mode=hAccessModeFromByte(out.mode);
        out.present=!all_zero;
        out.enabled=out.mode!=0;
        out.card_known=true;
        out.details_known=out.mode<=3;
        out.ok=true;
        std::ostringstream m;
        if(!out.present)m<<"Адрес "<<address<<" пуст [87H RAW="<<out.raw_record_hex<<"]";
        else{
            m<<"Адрес "<<address<<": карта "<<util::formatCardId(out.uid1,out.uid2)
             <<", PIN "<<out.pin<<", mode "<<static_cast<int>(out.mode)
             <<", zone "<<static_cast<int>(record[7])<<", RAW="<<out.raw_record_hex;
            if(!out.details_known)m<<" [неизвестный mode]";
        }
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
        out.access_mode=hAccessModeFromByte(out.mode);
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
    if(f.size()>=31 && f[0]==0x7E && (f[3]==0x0B || f[3]==0x03)){
        // Protocol v1.2, 25H event packet:
        // frame[3]  = event (0Bh valid / 03h invalid)
        // frame[13] = Data8  = User Address Hi
        // frame[14] = Data9  = User Address Lo
        // frame[19..20]      = Site Code
        // frame[23..24]      = Card Code
        e.event_code=f[3];
        e.user_address=(static_cast<int>(f[13])<<8)|f[14];
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
