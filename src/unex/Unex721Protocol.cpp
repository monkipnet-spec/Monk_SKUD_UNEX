#include "skud/Unex721Protocol.h"
#include "skud/SerialPort.h"
#include "skud/Util.h"
#include <ctime>
namespace skud {
std::vector<std::uint8_t> Unex721Protocol::frame(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data){std::vector<std::uint8_t>p{node,cmd};p.insert(p.end(),data.begin(),data.end());std::uint8_t x=0xFF;unsigned sum=0;for(auto b:p){x^=b;sum+=b;}p.push_back(x);sum+=x;p.push_back((std::uint8_t)(sum&0xFF));std::vector<std::uint8_t>f{0x7E,(std::uint8_t)p.size()};f.insert(f.end(),p.begin(),p.end());return f;}
bool Unex721Protocol::validFrame(const std::vector<std::uint8_t>&f){if(f.size()<6||f[0]!=0x7E||f[1]+2!=f.size())return false;std::uint8_t x=0xFF;unsigned sum=0;for(size_t i=2;i+2<f.size();++i){x^=f[i];sum+=f[i];}if(x!=f[f.size()-2])return false;sum+=x;return (std::uint8_t)(sum&0xFF)==f.back();}
std::optional<std::vector<std::uint8_t>>Unex721Protocol::transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout){auto q=frame(node,cmd,data);if(!port_.writeAll(q))return std::nullopt;auto r=port_.readFrame(timeout);if(r.empty()||!validFrame(r))return std::nullopt;return r;}
bool Unex721Protocol::ping(std::uint8_t node){return transact(node,0x25,{},80).has_value();}
std::optional<RawUnexEvent>Unex721Protocol::getOldestEvent(std::uint8_t node){auto r=transact(node,0x25,{},120);if(!r)return std::nullopt; // ACK/no-event frames are returned by device too.
    // Long event frames are passed to the decoder. Short ACK frames do not produce a card event.
    if(r->size()<18)return std::nullopt; return decodeEvent(node,*r);
}
bool Unex721Protocol::removeOldestEvent(std::uint8_t node){return transact(node,0x37,{},120).has_value();}
bool Unex721Protocol::setSystemTime(std::uint8_t node){std::time_t t=std::time(nullptr);std::tm tm{};localtime_r(&t,&tm);std::vector<std::uint8_t>d={(std::uint8_t)tm.tm_sec,(std::uint8_t)tm.tm_min,(std::uint8_t)tm.tm_hour,(std::uint8_t)(tm.tm_wday+1),(std::uint8_t)tm.tm_mday,(std::uint8_t)(tm.tm_mon+1),(std::uint8_t)((tm.tm_year+1900)%100)};return transact(node,0x23,d,150).has_value();}
RawUnexEvent Unex721Protocol::decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f)const{RawUnexEvent e;e.node=node;e.frame=f;e.raw_hex=util::hex(f);
    // SOYAL/UNEX 721 event-frame envelope is supported, but exact UNEX card/user field offsets are deliberately not guessed.
    // A captured real UNEX 721 Normal Access frame can be added here without changing any other module.
    // Heuristic event code detection: known normal-access code 0x0B if present in payload.
    for(size_t i=2;i+2<f.size();++i)if(f[i]==0x0B){e.event_code=0x0B;break;}
    return e;}
}
