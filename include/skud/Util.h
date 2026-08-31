#pragma once
#include <map>
#include <string>
#include <vector>

namespace skud::util {
std::string nowLocal();
std::string todayLocal();
std::string jsonEscape(const std::string& s);
std::string htmlEscape(const std::string& s);
std::string urlDecode(const std::string& s);
std::map<std::string,std::string> parseForm(const std::string& body);
std::vector<std::string> split(const std::string& s, char delim);
std::string trim(std::string s);
std::string hex(const std::vector<unsigned char>& data);
std::string sha256Hex(const std::string& s);
std::string randomToken(std::size_t bytes = 24);
bool constantTimeEqual(const std::string& a, const std::string& b);
}
