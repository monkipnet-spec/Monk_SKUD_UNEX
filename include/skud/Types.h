#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace skud {

enum class PresenceState { Absent, Present };
enum class AttendanceEventType { Arrival, Departure, Accidental, UnknownCard, RawControllerEvent };

struct User {
    int id{};
    bool enabled{true};
    std::string last_name;
    std::string first_name;
    std::string middle_name;
    std::string department;
    std::string position;
    std::string card; // primary/legacy canonical card id, e.g. 112:12345
    std::vector<std::string> cards; // all cards assigned to this user; first card is mirrored in card/card_series/card_number
    std::string card_series; // primary printed decimal series, e.g. 112
    std::string card_number; // printed decimal card number
    std::string pin_code; // optional 4-digit individual PIN; empty = not configured
    std::string access_mode{"card"}; // card, card_or_pin, card_and_pin
    int controller_port{}; // UNEX controller user address; 0 = not specified
    std::string valid_from;
    std::string valid_until;
    bool telegram_arrival{true};
    bool telegram_departure{true};
};

struct Controller {
    int node{};
    std::string name;
    std::string model{"UNEX 721"};
    bool enabled{true};
    bool online{false};
    std::string last_seen;
    std::string last_raw_hex;
};

struct PersistedCardState { PresenceState state{PresenceState::Absent}; std::string last_read; };

struct CardActivity {
    std::string card;
    int user_id{};
    std::string user_name;
    std::string department;
    std::string last_read;
    std::string last_event;
    int controller_node{};
};

// Persistent inventory of real cards observed in confirmed standard 25H
// events from UNEX 721 controllers. This is independent of attendance state:
// it is used to onboard cards into the local user database.
struct ControllerCardRecord {
    std::string card;
    int controller_node{};
    std::string controller_name;
    std::string first_seen;
    std::string last_seen;
    std::uint64_t read_count{};
    std::string last_raw_hex;
};

struct DailyAttendance {
    int user_id{};
    std::string user_name;
    std::string department;
    std::string card;
    std::string arrival_time;
    std::string departure_time;
    PresenceState presence{PresenceState::Absent};
    std::string last_event_time;
};

struct AttendanceEvent {
    std::string timestamp;
    AttendanceEventType type{AttendanceEventType::RawControllerEvent};
    std::string card;
    int user_id{};
    std::string user_name;
    std::string department;
    int controller_node{};
    std::string controller_name;
    std::string raw_hex;
};


struct ControllerUserUploadResult {
    int user_id{};
    int controller_node{};
    std::string status;
    std::string message;
};

struct ControllerUserUploadJob {
    std::uint64_t id{};
    std::string created_at;
    std::string state; // queued, running, completed, blocked
    int total{};
    int completed{};
    int success{};
    int failed{};
    int skipped{};
    bool full_sync{false};
    std::vector<ControllerUserUploadResult> results;
};

struct ControllerActionJob {
    std::uint64_t id{};
    std::string created_at;
    std::string state; // queued, running, completed
    int controller_node{};
    bool ok{false};
    std::string status;
    std::string message;
};


struct ControllerUserDeleteResult {
    int user_id{};
    int controller_node{};
    std::string status;
    std::string message;
};

struct ControllerUserDeleteJob {
    std::uint64_t id{};
    std::string created_at;
    std::string state; // queued, running, completed
    bool delete_from_system{false};
    int total{};
    int completed{};
    int success{};
    int failed{};
    int local_deleted{};
    int local_retained{};
    std::vector<ControllerUserDeleteResult> results;
};


struct ControllerUserReadResult {
    int controller_node{};
    int address{};
    int local_user_id{};
    std::string local_user_name;
    std::string controller_card;
    bool controller_enabled{false};
    bool pin_set{false};
    std::string access_mode;
    bool card_known{true};
    bool details_known{true};
    std::string raw_record_hex;
    std::string status; // match, diff, missing, unknown, empty, error
    std::string message;
};

struct ControllerUserReadJob {
    std::uint64_t id{};
    std::string created_at;
    std::string state; // queued, running, completed
    int total{};
    int completed{};
    int matches{};
    int differences{};
    int missing{};
    int unknown{};
    int unverified{};
    int empty{};
    int failed{};
    std::vector<ControllerUserReadResult> results;
};

struct ControllerEepromSearchMatch {
    int controller_node{};
    int eeprom_address{};
    std::string pattern;
    bool exact{true};
    std::string matched_hex;
    std::string context_hex;
};

struct ControllerEepromSearchError {
    int controller_node{};
    int eeprom_address{};
    std::string message;
};

struct ControllerEepromSearchJob {
    std::uint64_t id{};
    std::string created_at;
    std::string state; // queued, running, completed
    int card_series{};
    int card_number{};
    std::vector<int> compact_user_addresses;
    std::vector<std::string> compact_probes;
    int start_address{};
    int end_address{0xFFFF};
    int block_size{64};
    int total{};
    int completed{};
    int failed{};
    bool truncated{false};
    std::vector<ControllerEepromSearchMatch> matches;
    std::vector<ControllerEepromSearchError> errors;
};

struct ProtocolTraceEntry {
    std::uint64_t id{};
    std::string timestamp;
    std::string direction; // TX, RX, EVENT, INFO
    int node{};
    int command{-1};
    std::string protocol; // 0x7E, Extended, semantic
    std::string raw_hex;
    std::string message;
    std::string card;
    int user_address{-1};
};

struct RawUnexEvent {
    int node{};
    std::vector<std::uint8_t> frame;
    std::string raw_hex;
    std::string card; // filled when the event decoder can determine the card
    int user_address{-1};
    int event_code{-1};
};

} // namespace skud
