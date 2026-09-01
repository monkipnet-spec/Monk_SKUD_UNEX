#pragma once
#include "skud/Types.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace skud {
class Config;
class MariaDbUserStore;
class UserManager {
public:
    explicit UserManager(std::string path, Config* cfg=nullptr);
    ~UserManager();
    bool load();
    bool save() const;
    std::vector<User> list() const;
    std::optional<User> byCard(const std::string& card) const;
    std::optional<User> byId(int id) const;
    User upsert(User user);
    bool erase(int id);
    int eraseMany(const std::vector<int>& ids);
    bool assignCard(int id,const std::string&card);
    // Return an existing owner for card or create a placeholder local user.
    // The placeholder can be edited normally afterwards.
    std::optional<User> ensureUserForCard(const std::string& card);
    bool removeCard(const std::string&card);
    bool renameDepartment(const std::string& old_name,const std::string& new_name);
    bool departmentInUse(const std::string& name) const;
    std::vector<std::string> usedDepartments() const;
    std::string exportCsv() const;
    bool importCsv(const std::string& csv, std::string& error);
    bool usingMariaDb() const;
    std::string storageStatus() const;
    std::string storageError() const;
private:
    std::string path_;
    Config* cfg_{};
    std::unique_ptr<MariaDbUserStore> db_;
    mutable std::mutex storage_mu_;
    mutable std::string storage_error_;
    mutable std::mutex mu_;
    std::vector<User> users_;
};
}
