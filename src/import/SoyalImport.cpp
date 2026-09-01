#include "skud/SoyalImport.h"
#include "skud/Util.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace skud {
namespace {
constexpr std::size_t kUsrRecordSize=328;

std::uint16_t le16(const unsigned char* p){
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1])<<8);
}

void appendUtf8(std::string& out,std::uint32_t cp){
    if(cp<=0x7F)out.push_back(static_cast<char>(cp));
    else if(cp<=0x7FF){out.push_back(static_cast<char>(0xC0|(cp>>6)));out.push_back(static_cast<char>(0x80|(cp&0x3F)));}
    else{out.push_back(static_cast<char>(0xE0|(cp>>12)));out.push_back(static_cast<char>(0x80|((cp>>6)&0x3F)));out.push_back(static_cast<char>(0x80|(cp&0x3F)));}
}

std::string cp1251ToUtf8(const std::string& in){
    std::string out;out.reserve(in.size()*2);
    for(unsigned char c:in){
        if(c<0x80){out.push_back(static_cast<char>(c));continue;}
        if(c>=0xC0){appendUtf8(out,0x0410+(c-0xC0));continue;}
        if(c==0xA8){appendUtf8(out,0x0401);continue;}
        if(c==0xB8){appendUtf8(out,0x0451);continue;}
        // Common Windows-1251 punctuation used in names/positions.
        switch(c){
            case 0x82: appendUtf8(out,0x201A); break;
            case 0x84: appendUtf8(out,0x201E); break;
            case 0x85: appendUtf8(out,0x2026); break;
            case 0x86: appendUtf8(out,0x2020); break;
            case 0x87: appendUtf8(out,0x2021); break;
            case 0x89: appendUtf8(out,0x2030); break;
            case 0x8B: appendUtf8(out,0x2039); break;
            case 0x91: case 0x92: appendUtf8(out,0x2019); break;
            case 0x93: case 0x94: appendUtf8(out,0x201D); break;
            case 0x95: appendUtf8(out,0x2022); break;
            case 0x96: appendUtf8(out,0x2013); break;
            case 0x97: appendUtf8(out,0x2014); break;
            case 0x99: appendUtf8(out,0x2122); break;
            case 0xAB: appendUtf8(out,0x00AB); break;
            case 0xBB: appendUtf8(out,0x00BB); break;
            default: out.push_back('?'); break;
        }
    }
    return out;
}

std::string fixedAnsi(const unsigned char* record,std::size_t begin,std::size_t end){
    std::string raw;
    for(std::size_t i=begin;i<end;++i){if(record[i]==0)break;raw.push_back(static_cast<char>(record[i]));}
    return util::trim(cp1251ToUtf8(raw));
}

std::string fixedAnsi(const std::string& line,std::size_t begin,std::size_t end){
    if(begin>=line.size())return{};end=std::min(end,line.size());
    return util::trim(cp1251ToUtf8(line.substr(begin,end-begin)));
}

void splitName(SoyalImportRecord& r){
    r.full_name=util::trim(r.full_name);
    if(r.full_name.empty())return;
    std::istringstream s(r.full_name);std::vector<std::string> parts;std::string p;
    while(s>>p)parts.push_back(p);
    if(parts.empty())return;
    r.last_name=parts[0];
    if(parts.size()>1)r.first_name=parts[1];
    if(parts.size()>2){for(std::size_t i=2;i<parts.size();++i){if(!r.middle_name.empty())r.middle_name+=' ';r.middle_name+=parts[i];}}
}

std::string pinText(std::uint16_t pin){
    if(pin==0||pin>9999)return{};
    std::ostringstream o;o<<std::setw(4)<<std::setfill('0')<<pin;return o.str();
}

bool parseUnsigned(const std::string& text,int& value){
    auto s=util::trim(text);if(s.empty())return false;
    if(!std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isdigit(c);}))return false;
    try{value=std::stoi(s);return true;}catch(...){return false;}
}
}

SoyalImportResult SoyalImport::parseUsr(const std::string& data){
    SoyalImportResult out;out.format="SOYAL .usr (328B records)";
    if(data.empty()){out.error="Файл .usr пуст";return out;}
    if(data.size()%kUsrRecordSize!=0){
        out.error="Размер .usr не кратен 328 байтам: "+std::to_string(data.size());return out;
    }
    const auto count=data.size()/kUsrRecordSize;
    if(count>100000){out.error="Слишком много записей в .usr";return out;}
    out.total_slots=static_cast<int>(count);
    for(std::size_t i=0;i<count;++i){
        const auto* r=reinterpret_cast<const unsigned char*>(data.data()+i*kUsrRecordSize);
        const auto pin=le16(r+0);
        const auto number=le16(r+2);
        const auto series=le16(r+12);
        const auto name=fixedAnsi(r,36,132);
        const auto position=fixedAnsi(r,132,228);
        // Empty 701Client slots are all-zero in the useful identity fields.
        if(pin==0&&number==0&&series==0&&name.empty()&&position.empty()){++out.empty_slots;continue;}
        SoyalImportRecord x;x.address=static_cast<int>(i);x.card_series=series;x.card_number=number;
        if(series!=0||number!=0)x.card=util::formatCardId(series,number);
        x.pin_code=pinText(pin);x.full_name=name;x.position=position;x.source="usr";splitName(x);
        out.records.push_back(std::move(x));
    }
    out.ok=true;return out;
}

SoyalImportResult SoyalImport::parseUserCardText(const std::string& data){
    SoyalImportResult out;out.format="SOYAL UserCard.txt";
    if(data.empty()){out.error="Текстовый экспорт пуст";return out;}
    std::vector<std::string> lines;std::string line;std::istringstream s(data);
    while(std::getline(s,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();lines.push_back(line);}
    if(lines.empty()||lines[0].find("Addres")!=0||lines[0].find("Card #")==std::string::npos){out.error="Не найден заголовок SOYAL UserCard.txt";return out;}
    out.total_slots=static_cast<int>(lines.size()>0?lines.size()-1:0);
    for(std::size_t i=1;i<lines.size();++i){
        const auto& l=lines[i];if(util::trim(l).empty()){++out.empty_slots;continue;}
        int address=0;if(!parseUnsigned(l.substr(0,std::min<std::size_t>(7,l.size())),address)){++out.empty_slots;continue;}
        const auto card_field=fixedAnsi(l,7,19);int series=0,number=0;
        auto colon=card_field.find(':');
        bool card_ok=false;
        if(colon!=std::string::npos){card_ok=parseUnsigned(card_field.substr(0,colon),series)&&parseUnsigned(card_field.substr(colon+1),number)&&series<=65535&&number<=65535;}
        const auto name=fixedAnsi(l,19,50);
        int pin=0;parseUnsigned(fixedAnsi(l,50,57),pin);
        auto dep=fixedAnsi(l,57,74);if(dep=="Dep_00")dep.clear();
        // Ignore default empty SOYAL slots such as 00000:00000 / PIN 0000.
        if((!card_ok||(series==0&&number==0))&&name.empty()&&pin==0){++out.empty_slots;continue;}
        SoyalImportRecord x;x.address=address;x.card_series=series;x.card_number=number;
        if(card_ok&&(series!=0||number!=0))x.card=util::formatCardId(static_cast<std::uint16_t>(series),static_cast<std::uint16_t>(number));
        x.pin_code=pin>0&&pin<=9999?pinText(static_cast<std::uint16_t>(pin)):std::string{};
        x.full_name=name;x.department=dep;x.source="txt";splitName(x);out.records.push_back(std::move(x));
    }
    out.ok=true;return out;
}

}
