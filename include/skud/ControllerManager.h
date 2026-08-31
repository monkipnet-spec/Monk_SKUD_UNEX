#pragma once
#include "skud/Types.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace skud { class Config; class AttendanceEngine; class FileStore; class Unex721Protocol;
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

    // User uploads are queued so the web thread never touches the serial port.
    std::uint64_t queueUserUpload(std::vector<User> users,std::vector<int> controller_nodes);
    std::optional<ControllerUserUploadJob> userUploadJob(std::uint64_t id) const;
    bool userUploadProtocolReady() const;
    std::string userUploadProtocolMessage() const;

private:
    struct PendingUserUpload {
        std::uint64_t id{};
        std::vector<User> users;
        std::vector<int> controller_nodes;
    };

    void loop();
    void processOneUserUpload(Unex721Protocol& proto);
    void finishBlockedUserUpload(ControllerUserUploadJob& job,const std::vector<User>&users,const std::vector<int>&controller_nodes) const;

    Config& cfg_; AttendanceEngine& attendance_; std::string path_;
    mutable std::mutex mu_; std::vector<Controller> controllers_; std::string serial_status_{"OFFLINE"}; std::string serial_device_;
    std::atomic<bool> running_{false}; std::thread thread_; RawEventFn raw_cb_;

    mutable std::mutex upload_mu_;
    std::deque<PendingUserUpload> upload_queue_;
    std::map<std::uint64_t,ControllerUserUploadJob> upload_jobs_;
    std::uint64_t next_upload_id_{1};
}; }
