#pragma once
#include "skud/Types.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace skud { class SerialPort;
class Unex721Protocol {
public:
    using TraceFn = std::function<void(const std::string&,int,int,const std::string&,const std::vector<std::uint8_t>&,const std::string&)>;
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
    struct EepromWriteOutcome {
        bool ok{false};
        int address{};
        int length{};
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
        bool card_known{true}; // official H-series 87H 8-byte record exposes Site/Card directly
        bool details_known{true}; // true for official H-series 87H mode 0..3
        std::string raw_record_hex;
        std::vector<std::uint8_t> raw_record;
        std::string message;
    };

    explicit Unex721Protocol(SerialPort& port,TraceFn trace={}):port_(port),trace_(std::move(trace)){}

    // Legacy compact packet used by the first UNEX implementation.
    static std::vector<std::uint8_t> frame(std::uint8_t node,std::uint8_t command,const std::vector<std::uint8_t>&data={});
    static bool validFrame(const std::vector<std::uint8_t>&f);

    // Legacy/Enterprise extended transport retained only as a compatibility fallback.
    // The real UNEX 721 H-series path is the standard 0x7E protocol.
    static std::vector<std::uint8_t> extendedFrame(std::uint8_t node,std::uint8_t command,const std::vector<std::uint8_t>&data={});
    static bool validExtendedFrame(const std::vector<std::uint8_t>&f);

    bool ping(std::uint8_t node);
    std::optional<RawUnexEvent> getOldestEvent(std::uint8_t node);
    bool removeOldestEvent(std::uint8_t node);
    bool setSystemTime(std::uint8_t node);

    static bool userWriteSupported();
    static std::string userWriteSupportMessage();
    UserWriteOutcome writeUser(std::uint8_t node,const User& user);
    UserWriteOutcome clearAllUsers(std::uint8_t node);
    UserWriteOutcome disablePassAnyCards(std::uint8_t node);
    UserWriteOutcome clearUserSlot(std::uint8_t node,int address);
    UserWriteOutcome deleteUser(std::uint8_t node,const User& user);
    UserReadOutcome readUser(std::uint8_t node,int address);
    EepromReadOutcome readEeprom(std::uint8_t node,int address,int length);
    EepromWriteOutcome writeEeprom(std::uint8_t node,int address,const std::vector<std::uint8_t>& data);

private:
    std::optional<std::vector<std::uint8_t>> transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=120);
    std::optional<std::vector<std::uint8_t>> transactExtended(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=180);
    RawUnexEvent decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    RawUnexEvent decodeExtendedEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    void trace(const std::string& direction,int node,int command,const std::string& protocol,const std::vector<std::uint8_t>& frame,const std::string& message={}) const;
    SerialPort& port_;
    TraceFn trace_;
}; }
