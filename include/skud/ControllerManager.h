#pragma once
#include "skud/Types.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace skud { class Config; class AttendanceEngine; class FileStore;
class ControllerManager {
public:
    using RawEventFn = std::function<void(const RawUnexEvent&)>;
    ControllerManager(Config& cfg, AttendanceEngine& attendance, std::string controllers_path);
    ~ControllerManager();
    bool loadControllers();
    bool saveControllers() const;
    void start();
    void stop();
    std::vector<Controller> controllers() const;
    bool renameController(int node,const std::string&name);
    std::string serialStatus() const;
    std::string serialDevice() const;
    void setRawEventCallback(RawEventFn fn);
private:
    void loop();
    Config& cfg_; AttendanceEngine& attendance_; std::string path_;
    mutable std::mutex mu_; std::vector<Controller> controllers_; std::string serial_status_{"OFFLINE"}; std::string serial_device_;
    std::atomic<bool> running_{false}; std::thread thread_; RawEventFn raw_cb_;
}; }
