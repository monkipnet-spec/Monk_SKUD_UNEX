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

private:
    std::optional<std::vector<std::uint8_t>> transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=120);
    std::optional<std::vector<std::uint8_t>> transactExtended(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=180);
    RawUnexEvent decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    RawUnexEvent decodeExtendedEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    SerialPort& port_;
}; }
