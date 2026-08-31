#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace skud {
class DepartmentManager {
public:
    explicit DepartmentManager(std::string path);
    bool load();
    bool save() const;
    std::vector<std::string> list() const;
    bool add(const std::string& name);
    bool rename(const std::string& old_name, const std::string& new_name);
    bool erase(const std::string& name);
    bool contains(const std::string& name) const;
    bool ensure(const std::vector<std::string>& names);
private:
    static std::string normalize(std::string name);
    std::string path_;
    mutable std::mutex mu_;
    std::vector<std::string> departments_;
};
}
