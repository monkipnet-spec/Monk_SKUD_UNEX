#include "skud/SerialPort.h"
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <filesystem>

namespace skud {
SerialPort::~SerialPort(){closePort();}
static speed_t baudConst(int b){switch(b){case 19200:return B19200;case 38400:return B38400;case 57600:return B57600;case 115200:return B115200;default:return B9600;}}
bool SerialPort::openPort(const std::string&device,int baud){closePort();fd_=::open(device.c_str(),O_RDWR|O_NOCTTY|O_NONBLOCK);if(fd_<0)return false;termios t{};if(tcgetattr(fd_,&t)!=0){closePort();return false;}cfmakeraw(&t);cfsetispeed(&t,baudConst(baud));cfsetospeed(&t,baudConst(baud));t.c_cflag=(t.c_cflag&~CSIZE)|CS8;t.c_cflag&=~PARENB;t.c_cflag&=~CSTOPB;t.c_cflag|=CLOCAL|CREAD;tcsetattr(fd_,TCSANOW,&t);tcflush(fd_,TCIOFLUSH);device_=device;return true;}
void SerialPort::closePort(){if(fd_>=0)::close(fd_);fd_=-1;device_.clear();}
bool SerialPort::writeAll(const std::vector<std::uint8_t>&d){if(fd_<0)return false;size_t off=0;while(off<d.size()){auto n=::write(fd_,d.data()+off,d.size()-off);if(n<0)return false;off+=(size_t)n;}tcdrain(fd_);return true;}
bool SerialPort::readExact(std::uint8_t*b,std::size_t n,int timeout_ms){size_t off=0;while(off<n){pollfd p{fd_,POLLIN,0};int r=poll(&p,1,timeout_ms);if(r<=0)return false;auto k=::read(fd_,b+off,n-off);if(k<=0)return false;off+=(size_t)k;}return true;}
std::vector<std::uint8_t> SerialPort::readFrame(int timeout_ms){std::vector<std::uint8_t>r;if(fd_<0)return r;std::uint8_t b=0;for(int i=0;i<64;i++){if(!readExact(&b,1,timeout_ms))return{};if(b==0x7E)break;if(i==63)return{};}std::uint8_t len=0;if(!readExact(&len,1,timeout_ms))return{};r={0x7E,len};if(len==0||len>250)return{};std::vector<std::uint8_t>tail(len);if(!readExact(tail.data(),tail.size(),timeout_ms))return{};r.insert(r.end(),tail.begin(),tail.end());return r;}
std::string SerialPort::autoDetect(){std::error_code ec;std::filesystem::path byid("/dev/serial/by-id");if(std::filesystem::exists(byid,ec))for(auto&e:std::filesystem::directory_iterator(byid,ec))return e.path().string();for(auto s:{"/dev/ttyUSB0","/dev/ttyUSB1","/dev/ttyACM0","/dev/ttyACM1"})if(std::filesystem::exists(s,ec))return s;return{};}
}
