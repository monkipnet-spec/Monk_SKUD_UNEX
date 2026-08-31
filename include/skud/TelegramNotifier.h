#pragma once
#include "skud/Types.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace skud { class Config;
class TelegramNotifier {
public:
    explicit TelegramNotifier(Config& cfg);
    ~TelegramNotifier();
    void start();
    void stop();
    void enqueue(const AttendanceEvent& e);
    bool sendTest(std::string& error);
    bool sendDocument(const std::string& path, const std::string& caption, std::string& error);
private:
    bool sendText(const std::string& text,std::string& error);
    void loop();
    Config& cfg_; std::atomic<bool> running_{false}; std::thread thread_; std::mutex mu_; std::mutex send_mu_; std::condition_variable cv_; std::deque<AttendanceEvent> q_;
}; }
