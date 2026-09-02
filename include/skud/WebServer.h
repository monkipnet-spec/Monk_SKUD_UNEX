#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "skud/SystemMetrics.h"
#include "skud/Types.h"
namespace skud { class Config; class UserManager; class DepartmentManager; class AttendanceEngine; class ControllerManager; class TelegramNotifier; class ReportManager;
class WebServer {
public:
    WebServer(Config&,UserManager&,DepartmentManager&,AttendanceEngine&,ControllerManager&,TelegramNotifier&,ReportManager&,std::string root);
    ~WebServer();
    bool start(); void stop();
private:
    struct Req{std::string method,path,query,body;std::map<std::string,std::string>headers;};
    struct Res{int code{200};std::string type{"application/json; charset=utf-8"};std::string body;std::vector<std::pair<std::string,std::string>>headers;};
    void loop(); void handleClient(int fd); Res route(const Req& r);
    bool authorized(const Req&r)const; std::string cookie(const Req&r,const std::string&name)const;
    Res file(const std::string&name,const std::string&type); Res jsonUsers(); Res jsonDepartments(); Res jsonCards(); Res jsonControllerCards(); Res jsonTodayAttendance(); Res jsonControllers(); Res jsonStatus(); Res jsonReportSettings(); Res jsonUserUploadJob(const ControllerUserUploadJob& job); Res jsonControllerActionJob(const ControllerActionJob& job); Res jsonAttendanceReadJob(const ControllerAttendanceReadJob& job); Res jsonUserDeleteJob(const ControllerUserDeleteJob& job); Res jsonUserReadJob(const ControllerUserReadJob& job); Res jsonEepromSearchJob(const ControllerEepromSearchJob& job); Res jsonProtocolTrace(std::uint64_t after_id,std::size_t limit);
    Config&cfg_;UserManager&users_;DepartmentManager&departments_;AttendanceEngine&attendance_;ControllerManager&controllers_;TelegramNotifier&telegram_;ReportManager&reports_;SystemMetrics system_metrics_;std::string root_;
    std::atomic<bool>running_{false};int server_fd_{-1};std::thread thread_;mutable std::mutex sessions_mu_;std::map<std::string,std::string>sessions_;
}; }
