#pragma once
#include <memory>
#include <string>
namespace skud { class Config; class FileStore; class UserManager; class DepartmentManager; class AttendanceEngine; class ControllerManager; class TelegramNotifier; class ReportManager; class WebServer;
class App {
public:
    explicit App(std::string root); ~App();
    bool init(); bool run(); void stop();
private:
    std::string root_; bool running_{false};
    std::unique_ptr<Config> cfg_; std::unique_ptr<FileStore> store_; std::unique_ptr<UserManager> users_; std::unique_ptr<DepartmentManager> departments_; std::unique_ptr<AttendanceEngine> attendance_; std::unique_ptr<ControllerManager> controllers_; std::unique_ptr<TelegramNotifier> telegram_; std::unique_ptr<ReportManager> reports_; std::unique_ptr<WebServer> web_;
}; }
