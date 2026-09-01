#pragma once
#include "skud/Types.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace skud { class SerialPort;
class Unex721Protocol {
public:
    struct UserWriteOutcome {
        bool ok{false};
        std::string status;
        std::string message;
    };
    struct EepromReadOutcome {
        bool ok{false};
        int address{};
        std::vector<std::uint8_t> data;
        std::string raw_frame_hex;
        std::string message;
    };
    struct UserReadOutcome {
        bool ok{false};
        bool present{false};
        bool enabled{false};
        int address{};
        std::uint16_t uid1{};
        std::uint16_t uid2{};
        std::uint32_t pin{};
        std::uint8_t mode{};
        std::string access_mode;
        bool card_known{true}; // false for real compact H/UNEX 8-byte records: captured bytes are not direct series:number
        bool details_known{true}; // false when PIN/mode layout is not confirmed
        std::string raw_record_hex;
        std::string message;
    };

    explicit Unex721Protocol(SerialPort& port):port_(port){}

    // Legacy compact packet used by the first UNEX implementation.
    static std::vector<std::uint8_t> frame(std::uint8_t node,std::uint8_t command,const std::vector<std::uint8_t>&data={});
    static bool validFrame(const std::vector<std::uint8_t>&f);

    // SOYAL H-series Extended Protocol used by AR-727H and documented for RS485.
    static std::vector<std::uint8_t> extendedFrame(std::uint8_t node,std::uint8_t command,const std::vector<std::uint8_t>&data={});
    static bool validExtendedFrame(const std::vector<std::uint8_t>&f);

    bool ping(std::uint8_t node);
    std::optional<RawUnexEvent> getOldestEvent(std::uint8_t node);
    bool removeOldestEvent(std::uint8_t node);
    bool setSystemTime(std::uint8_t node);

    static bool userWriteSupported();
    static std::string userWriteSupportMessage();
    UserWriteOutcome writeUser(std::uint8_t node,const User& user);
    UserWriteOutcome deleteUser(std::uint8_t node,const User& user);
    UserReadOutcome readUser(std::uint8_t node,int address);
    EepromReadOutcome readEeprom(std::uint8_t node,int address,int length);

private:
    std::optional<std::vector<std::uint8_t>> transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=120);
    std::optional<std::vector<std::uint8_t>> transactExtended(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=180);
    RawUnexEvent decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    RawUnexEvent decodeExtendedEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    SerialPort& port_;
}; }
