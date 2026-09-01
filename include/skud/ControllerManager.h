#pragma once
#include "skud/Types.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace skud { class Config; class AttendanceEngine; class UserManager; class FileStore; class Unex721Protocol; class MariaDbUserStore;
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

    // Persistent inventory of real cards decoded from standard 25H events.
    // Existing local users are matched dynamically by exact series:number.
    std::vector<ControllerCardRecord> controllerCards() const;
    void clearControllerCards();

    // User uploads are queued so the web thread never touches the serial port.
    std::uint64_t queueUserUpload(std::vector<User> users,std::vector<int> controller_nodes,bool full_sync=false);
    std::optional<ControllerUserUploadJob> userUploadJob(std::uint64_t id) const;
    bool userUploadProtocolReady() const;
    std::string userUploadProtocolMessage() const;

    // Queue a focused controller setting action through the same serialized
    // COM-port worker. This disables only H-series Pass Any Cards (24* bit 0x20)
    // and performs 12H -> 20H -> 12H verified read-back.
    std::uint64_t queueDisablePassAnyCards(int controller_node);
    std::optional<ControllerActionJob> controllerActionJob(std::uint64_t id) const;

    // User deletions use the same serial queue. When delete_from_system=true,
    // the local record is removed only after every selected controller confirms
    // deletion for that user.
    std::uint64_t queueUserDelete(std::vector<User> users,std::vector<int> controller_nodes,bool delete_from_system);
    std::optional<ControllerUserDeleteJob> userDeleteJob(std::uint64_t id) const;

    // Read users back from controllers with 0x87. Jobs are processed in small
    // batches so long scans do not starve normal event polling.
    std::uint64_t queueUserRead(std::vector<User> local_users,std::vector<int> controller_nodes,std::vector<int> addresses,bool include_empty);
    std::optional<ControllerUserReadJob> userReadJob(std::uint64_t id) const;

    // Read-only H-series EEPROM diagnostic. Searches several common binary/BCD
    // representations of a known series:number card without writing anything.
    std::uint64_t queueEepromSearch(int card_series,int card_number,std::vector<int> controller_nodes,int start_address,int end_address,int block_size,std::vector<int> compact_user_addresses={});
    std::optional<ControllerEepromSearchJob> eepromSearchJob(std::uint64_t id) const;

    // In-memory live protocol ring buffer for web diagnostics.
    std::vector<ProtocolTraceEntry> protocolTrace(std::uint64_t after_id=0,std::size_t limit=250) const;
    void clearProtocolTrace();

private:
    struct PendingUserUpload {
        std::uint64_t id{};
        std::vector<User> users;
        std::vector<int> controller_nodes;
        bool full_sync{false};
    };
    struct PendingUserDelete {
        std::uint64_t id{};
        std::vector<User> users;
        std::vector<int> controller_nodes;
        bool delete_from_system{false};
    };
    struct PendingControllerAction {
        std::uint64_t id{};
        int controller_node{};
    };
    struct PendingEepromSearch {
        std::uint64_t id{};
        int card_series{};
        int card_number{};
        std::vector<int> controller_nodes;
        int start_address{};
        int end_address{0xFFFF};
        int block_size{64};
        std::vector<int> compact_user_addresses;
        std::set<int> compact_probed_nodes;
        std::map<int,std::vector<std::pair<int,std::vector<std::uint8_t>>>> compact_records;
        std::size_t controller_index{};
        int next_address{};
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
    void processOneControllerAction(Unex721Protocol& proto);
    void processUserReadBatch(Unex721Protocol& proto);
    void processEepromSearchBatch(Unex721Protocol& proto);
    void finishBlockedUserUpload(ControllerUserUploadJob& job,const std::vector<User>&users,const std::vector<int>&controller_nodes) const;
    void appendProtocolTrace(std::string direction,int node,int command,std::string protocol,const std::vector<std::uint8_t>& frame,std::string message={},std::string card={},int user_address=-1);
    void rememberControllerCard(const std::string& card,int node,const std::string& controller_name,const std::string& raw_hex);
    bool loadControllerCards();
    bool saveControllerCards() const;
    bool usingMariaDb() const;
    bool backupAndRemove(const std::string& path,const std::string& label,std::string& error) const;

    Config& cfg_; AttendanceEngine& attendance_; UserManager& users_; std::string path_;
    std::unique_ptr<MariaDbUserStore> db_;
    mutable std::mutex storage_mu_;
    mutable std::string storage_error_;
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

    mutable std::mutex action_mu_;
    std::deque<PendingControllerAction> action_queue_;
    std::map<std::uint64_t,ControllerActionJob> action_jobs_;
    std::uint64_t next_action_id_{1};

    mutable std::mutex read_mu_;
    std::deque<PendingUserRead> read_queue_;
    std::map<std::uint64_t,ControllerUserReadJob> read_jobs_;
    std::uint64_t next_read_id_{1};

    mutable std::mutex eeprom_mu_;
    std::deque<PendingEepromSearch> eeprom_queue_;
    std::map<std::uint64_t,ControllerEepromSearchJob> eeprom_jobs_;
    std::uint64_t next_eeprom_id_{1};

    mutable std::mutex card_mu_;
    std::map<std::string,ControllerCardRecord> controller_cards_;
    std::string controller_cards_path_;

    mutable std::mutex trace_mu_;
    std::deque<ProtocolTraceEntry> trace_entries_;
    std::uint64_t next_trace_id_{1};
    std::string last_event_trace_raw_;
}; }
