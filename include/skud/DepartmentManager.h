#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace skud {
class Config;
class MariaDbUserStore;
class DepartmentManager {
public:
    explicit DepartmentManager(std::string path, Config* cfg=nullptr);
    ~DepartmentManager();
    bool load();
    bool save() const;
    std::vector<std::string> list() const;
    bool add(const std::string& name);
    bool rename(const std::string& old_name, const std::string& new_name);
    bool erase(const std::string& name);
    bool contains(const std::string& name) const;
    bool ensure(const std::vector<std::string>& names);
    std::string storageError() const;
private:
    static std::string normalize(std::string name);
    bool usingMariaDb() const;
    bool backupAndRemoveCsv(std::string& error) const;
    std::string path_;
    Config* cfg_{};
    std::unique_ptr<MariaDbUserStore> db_;
    mutable std::mutex storage_mu_;
    mutable std::string storage_error_;
    mutable std::mutex mu_;
    std::vector<std::string> departments_;
};
}
