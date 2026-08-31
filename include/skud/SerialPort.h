#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace skud {
class SerialPort {
public:
    SerialPort()=default; ~SerialPort();
    bool openPort(const std::string& device,int baud);
    void closePort();
    bool isOpen()const{return fd_>=0;}
    bool writeAll(const std::vector<std::uint8_t>& data);
    bool readExact(std::uint8_t* buf,std::size_t n,int timeout_ms);
    std::vector<std::uint8_t> readFrame(int timeout_ms);
    std::vector<std::uint8_t> readExtendedFrame(int timeout_ms);
    static std::string autoDetect();
    std::string device()const{return device_;}
private:int fd_{-1};std::string device_;};
}
