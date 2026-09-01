#pragma once
#include <string>
#include <vector>

namespace skud {

struct SoyalImportRecord {
    int address{};
    int card_series{};
    int card_number{};
    std::string card;
    std::string pin_code;
    std::string full_name;
    std::string last_name;
    std::string first_name;
    std::string middle_name;
    std::string department;
    std::string position;
    std::string source;
};

struct SoyalImportResult {
    bool ok{false};
    std::string format;
    std::string error;
    int total_slots{};
    int empty_slots{};
    std::vector<SoyalImportRecord> records;
};

class SoyalImport {
public:
    // SOYAL 701Client .usr user database. Confirmed export layout used by the
    // supplied base.usr: fixed 328-byte records, record index = user address.
    static SoyalImportResult parseUsr(const std::string& data);

    // Text export headed by "Addres Card # Name PIN Dep.(1) ...".
    static SoyalImportResult parseUserCardText(const std::string& data);
};

}
