#pragma once
#include "skud/Types.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace skud {
struct PersistedCardState { PresenceState state{PresenceState::Absent}; std::string last_read; };
class FileStore {
public:
    explicit FileStore(std::string root);
    bool appendEvent(const AttendanceEvent& e);
    bool saveCardStates(const std::map<std::string,PersistedCardState>& states);
    std::map<std::string,PersistedCardState> loadCardStates() const;
    bool saveActivities(const std::vector<CardActivity>& activities);
    std::vector<CardActivity> loadActivities() const;
    bool backupFile(const std::string& path) const;
private:
    std::string root_;
    mutable std::mutex mu_;
};
}
