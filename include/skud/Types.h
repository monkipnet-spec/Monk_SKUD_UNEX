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
    std::string card;
    int controller_port{}; // UNEX controller port assigned to this user; 0 = not specified
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

struct CardActivity {
    std::string card;
    int user_id{};
    std::string user_name;
    std::string department;
    std::string last_read;
    std::string last_event;
    int controller_node{};
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

struct RawUnexEvent {
    int node{};
    std::vector<std::uint8_t> frame;
    std::string raw_hex;
    std::string card; // filled when the event decoder can determine the card
    int user_address{-1};
    int event_code{-1};
};

} // namespace skud
