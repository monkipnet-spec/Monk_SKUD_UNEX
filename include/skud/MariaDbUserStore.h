#pragma once
#include "skud/Types.h"
#include <mutex>
#include <string>
#include <vector>

namespace skud {
class Config;
class MariaDbUserStore {
public:
    explicit MariaDbUserStore(const Config& cfg);
    ~MariaDbUserStore();
    bool init(std::string& error);
    bool load(std::vector<User>& users,std::string& error);
    bool save(const std::vector<User>& users,std::string& error);
    std::string status() const;
private:
    const Config& cfg_;
    mutable std::mutex mu_;
    void* conn_{nullptr};
    std::string status_{"DISABLED"};
    bool connectLocked(std::string& error);
    bool execLocked(const std::string& sql,std::string& error);
    std::string escLocked(const std::string& value) const;
};
}
