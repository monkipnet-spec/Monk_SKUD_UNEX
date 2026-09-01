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

namespace skud { class Config; class AttendanceEngine; class UserManager; class FileStore; class Unex721Protocol;
class ControllerManager {
public:
    using RawEventFn = std::function<void(const RawUnexEvent&)>;
    ControllerManager(Config& cfg, AttendanceEngine& attendance, UserManager& users, std::string controllers_path);
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

    // User deletions use the same serial queue. When delete_from_system=true,
    // the local record is removed only after every selected controller confirms
    // deletion for that user.
    std::uint64_t queueUserDelete(std::vector<User> users,std::vector<int> controller_nodes,bool delete_from_system);
    std::optional<ControllerUserDeleteJob> userDeleteJob(std::uint64_t id) const;

    // Read users back from controllers with 0x87. Jobs are processed in small
    // batches so long scans do not starve normal event polling.
    std::uint64_t queueUserRead(std::vector<User> local_users,std::vector<int> controller_nodes,std::vector<int> addresses,bool include_empty);
    std::optional<ControllerUserReadJob> userReadJob(std::uint64_t id) const;

private:
    struct PendingUserUpload {
        std::uint64_t id{};
        std::vector<User> users;
        std::vector<int> controller_nodes;
    };
    struct PendingUserDelete {
        std::uint64_t id{};
        std::vector<User> users;
        std::vector<int> controller_nodes;
        bool delete_from_system{false};
    };
    struct PendingUserRead {
        std::uint64_t id{};
        std::vector<User> local_users;
        std::vector<int> controller_nodes;
        std::vector<int> addresses;
        bool include_empty{false};
        std::size_t controller_index{};
        std::size_t address_index{};
    };

    void loop();
    void processOneUserUpload(Unex721Protocol& proto);
    void processOneUserDelete(Unex721Protocol& proto);
    void processUserReadBatch(Unex721Protocol& proto);
    void finishBlockedUserUpload(ControllerUserUploadJob& job,const std::vector<User>&users,const std::vector<int>&controller_nodes) const;

    Config& cfg_; AttendanceEngine& attendance_; UserManager& users_; std::string path_;
    mutable std::mutex mu_; std::vector<Controller> controllers_; std::string serial_status_{"OFFLINE"}; std::string serial_device_;
    std::atomic<bool> running_{false}; std::thread thread_; RawEventFn raw_cb_;

    mutable std::mutex upload_mu_;
    std::deque<PendingUserUpload> upload_queue_;
    std::map<std::uint64_t,ControllerUserUploadJob> upload_jobs_;
    std::uint64_t next_upload_id_{1};

    mutable std::mutex delete_mu_;
    std::deque<PendingUserDelete> delete_queue_;
    std::map<std::uint64_t,ControllerUserDeleteJob> delete_jobs_;
    std::uint64_t next_delete_id_{1};

    mutable std::mutex read_mu_;
    std::deque<PendingUserRead> read_queue_;
    std::map<std::uint64_t,ControllerUserReadJob> read_jobs_;
    std::uint64_t next_read_id_{1};
}; }
