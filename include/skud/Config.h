#pragma once
#include <map>
#include <mutex>
#include <string>

namespace skud {
class Config {
public:
    explicit Config(std::string path);
    bool load();
    bool save() const;
    std::string get(const std::string& key, const std::string& def="") const;
    int getInt(const std::string& key, int def) const;
    bool getBool(const std::string& key, bool def) const;
    void set(const std::string& key, const std::string& value);
    std::string raw() const;
    bool replaceRaw(const std::string& text);
    const std::string& path() const { return path_; }
private:
    std::string path_;
    mutable std::mutex mu_;
    std::map<std::string,std::string> kv_;
};
}
