#pragma once
#include "skud/Types.h"
#include <cstdint>
#include <optional>
#include <vector>
namespace skud { class SerialPort;
class Unex721Protocol {
public:
    explicit Unex721Protocol(SerialPort& port):port_(port){}
    static std::vector<std::uint8_t> frame(std::uint8_t node,std::uint8_t command,const std::vector<std::uint8_t>&data={});
    static bool validFrame(const std::vector<std::uint8_t>&f);
    bool ping(std::uint8_t node);
    std::optional<RawUnexEvent> getOldestEvent(std::uint8_t node);
    bool removeOldestEvent(std::uint8_t node);
    bool setSystemTime(std::uint8_t node);
    // Hardware write operations are intentionally kept out until UNEX 721 field capture confirms mapping.
private:
    std::optional<std::vector<std::uint8_t>> transact(std::uint8_t node,std::uint8_t cmd,const std::vector<std::uint8_t>&data,int timeout_ms=120);
    RawUnexEvent decodeEvent(std::uint8_t node,const std::vector<std::uint8_t>&f) const;
    SerialPort& port_;
}; }
