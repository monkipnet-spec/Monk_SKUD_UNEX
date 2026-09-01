#include "skud/WebServer.h"
#include "skud/AttendanceEngine.h"
#include "skud/Config.h"
#include "skud/ControllerManager.h"
#include "skud/DepartmentManager.h"
#include "skud/ReportManager.h"
#include "skud/TelegramNotifier.h"
#include "skud/UserManager.h"
#include "skud/Util.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace skud {
WebServer::WebServer(Config&c,UserManager&u,DepartmentManager&d,AttendanceEngine&a,ControllerManager&cm,TelegramNotifier&t,ReportManager&r,std::string root):cfg_(c),users_(u),departments_(d),attendance_(a),controllers_(cm),telegram_(t),reports_(r),root_(std::move(root)){}
WebServer::~WebServer(){stop();}
bool WebServer::start(){if(running_)return true;server_fd_=::socket(AF_INET,SOCK_STREAM,0);if(server_fd_<0)return false;int one=1;setsockopt(server_fd_,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=INADDR_ANY;addr.sin_port=htons(cfg_.getInt("server.port",8080));if(bind(server_fd_,(sockaddr*)&addr,sizeof(addr))<0||listen(server_fd_,32)<0){::close(server_fd_);server_fd_=-1;return false;}running_=true;thread_=std::thread(&WebServer::loop,this);return true;}
void WebServer::stop(){running_=false;if(server_fd_>=0){shutdown(server_fd_,SHUT_RDWR);::close(server_fd_);server_fd_=-1;}if(thread_.joinable())thread_.join();}
void WebServer::loop(){while(running_){int c=accept(server_fd_,nullptr,nullptr);if(c<0){if(!running_)break;continue;}std::thread(&WebServer::handleClient,this,c).detach();}}
static std::string lower(std::string s){for(char&c:s)c=std::tolower((unsigned char)c);return s;}
static std::vector<int> parseIntList(const std::string&s){std::vector<int> out;for(auto&part:util::split(s,',')){try{int v=std::stoi(util::trim(part));if(v>0&&std::find(out.begin(),out.end(),v)==out.end())out.push_back(v);}catch(...){}}return out;}
void WebServer::handleClient(int fd){std::string data;char buf[4096];while(data.find("\r\n\r\n")==std::string::npos&&data.size()<65536){auto n=recv(fd,buf,sizeof(buf),0);if(n<=0){close(fd);return;}data.append(buf,n);}auto hp=data.find("\r\n\r\n");std::string head=data.substr(0,hp),body=data.substr(hp+4);std::istringstream hs(head);std::string line;Req r;if(!std::getline(hs,line)){close(fd);return;}if(!line.empty()&&line.back()=='\r')line.pop_back();std::istringstream l1(line);std::string target,ver;l1>>r.method>>target>>ver;auto q=target.find('?');r.path=q==std::string::npos?target:target.substr(0,q);r.query=q==std::string::npos?"":target.substr(q+1);while(std::getline(hs,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();auto p=line.find(':');if(p!=std::string::npos)r.headers[lower(util::trim(line.substr(0,p)))]=util::trim(line.substr(p+1));}size_t cl=0;try{cl=std::stoul(r.headers["content-length"]);}catch(...){}while(body.size()<cl){auto n=recv(fd,buf,sizeof(buf),0);if(n<=0)break;body.append(buf,n);}r.body=body.substr(0,cl);auto res=route(r);std::ostringstream out;std::string reason=res.code==200?"OK":res.code==302?"Found":res.code==401?"Unauthorized":res.code==404?"Not Found":"Bad Request";out<<"HTTP/1.1 "<<res.code<<' '<<reason<<"\r\nContent-Type: "<<res.type<<"\r\nContent-Length: "<<res.body.size()<<"\r\nConnection: close\r\n";for(auto&h:res.headers)out<<h.first<<": "<<h.second<<"\r\n";out<<"\r\n"<<res.body;auto s=out.str();send(fd,s.data(),s.size(),MSG_NOSIGNAL);close(fd);}
std::string WebServer::cookie(const Req&r,const std::string&name)const{auto it=r.headers.find("cookie");if(it==r.headers.end())return{};for(auto&p:util::split(it->second,';')){auto x=p.find('=');if(x!=std::string::npos&&util::trim(p.substr(0,x))==name)return util::trim(p.substr(x+1));}return{};}
bool WebServer::authorized(const Req&r)const{auto sid=cookie(r,"SKUDSID");if(sid.empty())return false;std::lock_guard lk(sessions_mu_);return sessions_.count(sid)>0;}
WebServer::Res WebServer::file(const std::string&name,const std::string&type){const auto path=std::filesystem::path(root_)/"web"/name;std::ifstream f(path,std::ios::binary);if(!f){return{404,"text/plain; charset=utf-8","UI file not found: "+path.string()};}std::ostringstream o;o<<f.rdbuf();Res x{200,type,o.str()};x.headers.push_back({"Cache-Control","no-store, no-cache, must-revalidate, max-age=0"});x.headers.push_back({"Pragma","no-cache"});return x;}
WebServer::Res WebServer::jsonUsers(){auto v=users_.list();std::ostringstream o;o<<"[";bool first=true;for(auto&u:v){if(!first)o<<',';first=false;o<<"{\"id\":"<<u.id<<",\"enabled\":"<<(u.enabled?"true":"false")<<",\"last_name\":\""<<util::jsonEscape(u.last_name)<<"\",\"first_name\":\""<<util::jsonEscape(u.first_name)<<"\",\"middle_name\":\""<<util::jsonEscape(u.middle_name)<<"\",\"department\":\""<<util::jsonEscape(u.department)<<"\",\"position\":\""<<util::jsonEscape(u.position)<<"\",\"card\":\""<<util::jsonEscape(u.card)<<"\",\"card_series\":\""<<util::jsonEscape(u.card_series)<<"\",\"card_number\":\""<<util::jsonEscape(u.card_number)<<"\",\"pin_code\":\""<<util::jsonEscape(u.pin_code)<<"\",\"access_mode\":\""<<util::jsonEscape(u.access_mode)<<"\",\"controller_port\":"<<u.controller_port<<"}";}o<<"]";return{200,"application/json; charset=utf-8",o.str()};}
WebServer::Res WebServer::jsonDepartments(){auto v=departments_.list();std::ostringstream o;o<<"[";bool first=true;for(const auto&name:v){if(!first)o<<',';first=false;o<<"\""<<util::jsonEscape(name)<<"\"";}o<<"]";return{200,"application/json; charset=utf-8",o.str()};}
WebServer::Res WebServer::jsonCards(){auto v=attendance_.activities();std::ostringstream o;o<<"[";bool first=true;for(auto&a:v){if(!first)o<<',';first=false;o<<"{\"card\":\""<<util::jsonEscape(a.card)<<"\",\"user_id\":"<<a.user_id<<",\"user_name\":\""<<util::jsonEscape(a.user_name)<<"\",\"department\":\""<<util::jsonEscape(a.department)<<"\",\"last_read\":\""<<a.last_read<<"\",\"last_event\":\""<<util::jsonEscape(a.last_event)<<"\",\"controller_node\":"<<a.controller_node<<"}";}o<<"]";return{200,"application/json; charset=utf-8",o.str()};}
WebServer::Res WebServer::jsonTodayAttendance(){
    auto v=attendance_.todayAttendance();
    std::ostringstream o;o<<"[";bool first=true;
    for(const auto&a:v){
        if(!first)o<<',';first=false;
        o<<"{\"user_id\":"<<a.user_id
         <<",\"user_name\":\""<<util::jsonEscape(a.user_name)<<"\""
         <<",\"department\":\""<<util::jsonEscape(a.department)<<"\""
         <<",\"card\":\""<<util::jsonEscape(a.card)<<"\""
         <<",\"arrival_time\":\""<<util::jsonEscape(a.arrival_time)<<"\""
         <<",\"departure_time\":\""<<util::jsonEscape(a.departure_time)<<"\""
         <<",\"status\":\""<<(a.presence==PresenceState::Present?"at_work":"left")<<"\""
         <<"}";
    }
    o<<"]";
    return{200,"application/json; charset=utf-8",o.str()};
}
WebServer::Res WebServer::jsonControllers(){auto v=controllers_.controllers();std::ostringstream o;o<<"[";bool first=true;for(auto&c:v){if(!first)o<<',';first=false;o<<"{\"node\":"<<c.node<<",\"name\":\""<<util::jsonEscape(c.name)<<"\",\"model\":\""<<util::jsonEscape(c.model)<<"\",\"online\":"<<(c.online?"true":"false")<<",\"last_seen\":\""<<c.last_seen<<"\",\"last_raw_hex\":\""<<util::jsonEscape(c.last_raw_hex)<<"\"}";}o<<"]";return{200,"application/json; charset=utf-8",o.str()};}
WebServer::Res WebServer::jsonUserUploadJob(const ControllerUserUploadJob&job){
    std::ostringstream o;o<<"{\"id\":"<<job.id<<",\"created_at\":\""<<util::jsonEscape(job.created_at)<<"\",\"state\":\""<<util::jsonEscape(job.state)<<"\",\"total\":"<<job.total<<",\"completed\":"<<job.completed<<",\"success\":"<<job.success<<",\"failed\":"<<job.failed<<",\"skipped\":"<<job.skipped<<",\"results\":[";
    bool first=true;for(const auto&r:job.results){if(!first)o<<',';first=false;o<<"{\"user_id\":"<<r.user_id<<",\"controller_node\":"<<r.controller_node<<",\"status\":\""<<util::jsonEscape(r.status)<<"\",\"message\":\""<<util::jsonEscape(r.message)<<"\"}";}o<<"]}";
    return{200,"application/json; charset=utf-8",o.str()};
}
WebServer::Res WebServer::jsonUserDeleteJob(const ControllerUserDeleteJob&job){
    std::ostringstream o;
    o<<"{\"id\":"<<job.id
     <<",\"created_at\":\""<<util::jsonEscape(job.created_at)<<"\""
     <<",\"state\":\""<<util::jsonEscape(job.state)<<"\""
     <<",\"delete_from_system\":"<<(job.delete_from_system?"true":"false")
     <<",\"total\":"<<job.total
     <<",\"completed\":"<<job.completed
     <<",\"success\":"<<job.success
     <<",\"failed\":"<<job.failed
     <<",\"local_deleted\":"<<job.local_deleted
     <<",\"local_retained\":"<<job.local_retained
     <<",\"results\":[";
    bool first=true;
    for(const auto&r:job.results){
        if(!first)o<<',';first=false;
        o<<"{\"user_id\":"<<r.user_id
         <<",\"controller_node\":"<<r.controller_node
         <<",\"status\":\""<<util::jsonEscape(r.status)<<"\""
         <<",\"message\":\""<<util::jsonEscape(r.message)<<"\"}";
    }
    o<<"]}";
    return{200,"application/json; charset=utf-8",o.str()};
}
WebServer::Res WebServer::jsonUserReadJob(const ControllerUserReadJob&job){
    std::ostringstream o;
    o<<"{\"id\":"<<job.id
     <<",\"created_at\":\""<<util::jsonEscape(job.created_at)<<"\""
     <<",\"state\":\""<<util::jsonEscape(job.state)<<"\""
     <<",\"total\":"<<job.total
     <<",\"completed\":"<<job.completed
     <<",\"matches\":"<<job.matches
     <<",\"differences\":"<<job.differences
     <<",\"missing\":"<<job.missing
     <<",\"unknown\":"<<job.unknown
     <<",\"empty\":"<<job.empty
     <<",\"failed\":"<<job.failed
     <<",\"results\":[";
    bool first=true;
    for(const auto&r:job.results){
        if(!first)o<<',';first=false;
        o<<"{\"controller_node\":"<<r.controller_node
         <<",\"address\":"<<r.address
         <<",\"local_user_id\":"<<r.local_user_id
         <<",\"local_user_name\":\""<<util::jsonEscape(r.local_user_name)<<"\""
         <<",\"controller_card\":\""<<util::jsonEscape(r.controller_card)<<"\""
         <<",\"controller_enabled\":"<<(r.controller_enabled?"true":"false")
         <<",\"pin_set\":"<<(r.pin_set?"true":"false")
         <<",\"access_mode\":\""<<util::jsonEscape(r.access_mode)<<"\""
         <<",\"details_known\":"<<(r.details_known?"true":"false")
         <<",\"raw_record_hex\":\""<<util::jsonEscape(r.raw_record_hex)<<"\""
         <<",\"status\":\""<<util::jsonEscape(r.status)<<"\""
         <<",\"message\":\""<<util::jsonEscape(r.message)<<"\"}";
    }
    o<<"]}";
    return{200,"application/json; charset=utf-8",o.str()};
}

WebServer::Res WebServer::jsonStatus(){auto p=attendance_.presentUsers();auto m=system_metrics_.snapshot();std::ostringstream o;o<<"{\"serial_status\":\""<<controllers_.serialStatus()<<"\",\"serial_device\":\""<<util::jsonEscape(controllers_.serialDevice())<<"\",\"present_count\":"<<p.size()<<",\"repeat_seconds\":"<<cfg_.getInt("attendance.accidental_repeat_seconds",60)<<",\"cpu_percent\":"<<m.cpu_percent<<",\"ram_percent\":"<<m.ram_percent<<",\"ram_used_mb\":"<<m.ram_used_mb<<",\"ram_total_mb\":"<<m.ram_total_mb<<",\"uptime_seconds\":"<<m.uptime_seconds<<"}";return{200,"application/json; charset=utf-8",o.str()};}
WebServer::Res WebServer::jsonReportSettings(){
    const auto today=reports_.todayRange(),week=reports_.currentWeekRange(),month=reports_.currentMonthRange();
    const auto s=reports_.schedule();
    std::ostringstream o;
    o<<"{\"today\":{\"from\":\""<<today.from<<"\",\"to\":\""<<today.to<<"\"}"
     <<",\"week\":{\"from\":\""<<week.from<<"\",\"to\":\""<<week.to<<"\"}"
     <<",\"month\":{\"from\":\""<<month.from<<"\",\"to\":\""<<month.to<<"\"}"
     <<",\"schedule\":{\"enabled\":"<<(s.enabled?"true":"false")
     <<",\"period\":\""<<util::jsonEscape(s.period)<<"\""
     <<",\"time\":\""<<util::jsonEscape(s.time)<<"\""
     <<",\"weekday\":"<<s.weekday
     <<",\"month_day\":"<<s.month_day
     <<",\"last_sent_at\":\""<<util::jsonEscape(s.last_sent_at)<<"\""
     <<",\"last_period\":\""<<util::jsonEscape(s.last_period)<<"\""
     <<",\"last_status\":\""<<util::jsonEscape(s.last_status)<<"\""
     <<",\"last_error\":\""<<util::jsonEscape(s.last_error)<<"\"}}";
    return{200,"application/json; charset=utf-8",o.str()};
}
WebServer::Res WebServer::route(const Req&r){
    if(r.path=="/api/login"&&r.method=="POST"){auto f=util::parseForm(r.body);auto salt=cfg_.get("auth.salt");auto hash=util::sha256Hex(salt+f["password"]);if(f["username"]==cfg_.get("auth.username","admin")&&util::constantTimeEqual(hash,cfg_.get("auth.password_hash"))){auto sid=util::randomToken();{std::lock_guard lk(sessions_mu_);sessions_[sid]=f["username"];}Res x{200,"application/json","{\"ok\":true}"};x.headers.push_back({"Set-Cookie","SKUDSID="+sid+"; Path=/; HttpOnly; SameSite=Strict"});return x;}return{401,"application/json","{\"ok\":false}"};}
    if(r.path=="/login.html")return file("login.html","text/html; charset=utf-8");
    if(r.path=="/style.css")return file("style.css","text/css; charset=utf-8");
    if(r.path=="/app.js")return file("app.js","application/javascript; charset=utf-8");
    if(r.path=="/"&&!authorized(r)){Res x; x.code=302;x.type="text/plain";x.headers.push_back({"Location","/login.html"});return x;}
    if(r.path=="/")return file("index.html","text/html; charset=utf-8");
    if(!authorized(r))return{401,"application/json","{\"error\":\"auth required\"}"};
    if(r.path=="/api/logout"){auto sid=cookie(r,"SKUDSID");{std::lock_guard lk(sessions_mu_);sessions_.erase(sid);}Res x{200,"application/json","{}"};x.headers.push_back({"Set-Cookie","SKUDSID=; Path=/; Max-Age=0"});return x;}
    if(r.path=="/api/status")return jsonStatus();
    if(r.path=="/api/users"&&r.method=="GET")return jsonUsers();
    if(r.path=="/api/departments"&&r.method=="GET")return jsonDepartments();
    if(r.path=="/api/cards/active")return jsonCards();
    if(r.path=="/api/attendance/today"&&r.method=="GET")return jsonTodayAttendance();
    if(r.path=="/api/reports/settings"&&r.method=="GET")return jsonReportSettings();
    if(r.path=="/api/reports/settings"&&r.method=="POST"){
        auto f=util::parseForm(r.body);
        int weekday=1,month_day=1;try{weekday=std::stoi(f["weekday"]);}catch(...){}try{month_day=std::stoi(f["month_day"]);}catch(...){}
        std::string err;bool ok=reports_.saveSchedule(f["enabled"]=="1",f["period"],f["time"],weekday,month_day,err);
        return{ok?200:400,"application/json",ok?"{\"ok\":true}":("{\"ok\":false,\"error\":\""+util::jsonEscape(err)+"\"}")};
    }
    if(r.path=="/api/reports/preview"&&r.method=="GET"){
        auto q=util::parseForm(r.query);AttendanceReport report;std::string err;
        if(!reports_.build(q["from"],q["to"],report,err))return{400,"application/json","{\"ok\":false,\"error\":\""+util::jsonEscape(err)+"\"}"};
        std::ostringstream o;o<<"{\"ok\":true,\"filename\":\""<<util::jsonEscape(report.filename)<<"\",\"days\":"<<report.days<<",\"users\":"<<report.users<<",\"rows\":"<<report.rows<<",\"content\":\""<<util::jsonEscape(report.content)<<"\"}";
        return{200,"application/json; charset=utf-8",o.str()};
    }
    if(r.path=="/api/reports/download"&&r.method=="GET"){
        auto q=util::parseForm(r.query);AttendanceReport report;std::string err;
        if(!reports_.build(q["from"],q["to"],report,err))return{400,"text/plain; charset=utf-8",err};
        Res x{200,"text/plain; charset=utf-8",report.content};x.headers.push_back({"Content-Disposition","attachment; filename=\""+report.filename+"\""});return x;
    }
    if(r.path=="/api/reports/send"&&r.method=="POST"){
        auto f=util::parseForm(r.body);AttendanceReport report;std::string err;bool ok=reports_.sendToTelegram(f["from"],f["to"],report,err);
        std::ostringstream o;o<<"{\"ok\":"<<(ok?"true":"false")<<",\"filename\":\""<<util::jsonEscape(report.filename)<<"\",\"error\":\""<<util::jsonEscape(err)<<"\"}";
        return{ok?200:400,"application/json; charset=utf-8",o.str()};
    }
    if(r.path=="/api/controllers"&&r.method=="GET")return jsonControllers();
    if(r.path=="/api/controllers/read-users"&&r.method=="POST"){
        auto f=util::parseForm(r.body);
        std::vector<int> selected_nodes;
        auto allc=controllers_.controllers();
        if(f["all_controllers"]=="1"){
            for(const auto&c:allc)if(c.enabled)selected_nodes.push_back(c.node);
        }else{
            for(int node:parseIntList(f["controller_nodes"])){
                auto it=std::find_if(allc.begin(),allc.end(),[&](const Controller&c){return c.node==node&&c.enabled;});
                if(it!=allc.end())selected_nodes.push_back(node);
            }
        }
        if(selected_nodes.empty())return{400,"application/json","{\"ok\":false,\"error\":\"no controllers selected\"}"};

        auto local_users=users_.list();
        std::vector<int> addresses;
        const auto mode=f["address_mode"].empty()?"local":f["address_mode"];
        if(mode=="local"){
            for(const auto&u:local_users)if(u.controller_port>0&&u.controller_port<=16383)addresses.push_back(u.controller_port);
        }else if(mode=="range"){
            int from=0,to=0;try{from=std::stoi(f["range_from"]);to=std::stoi(f["range_to"]);}catch(...){}
            if(from<1||to>16383||from>to)return{400,"application/json","{\"ok\":false,\"error\":\"range must be 1..16383\"}"};
            addresses.reserve(static_cast<std::size_t>(to-from+1));
            for(int a=from;a<=to;++a)addresses.push_back(a);
        }else{
            return{400,"application/json","{\"ok\":false,\"error\":\"bad address mode\"}"};
        }
        std::sort(addresses.begin(),addresses.end());
        addresses.erase(std::unique(addresses.begin(),addresses.end()),addresses.end());
        if(addresses.empty())return{400,"application/json","{\"ok\":false,\"error\":\"no user addresses to read\"}"};

        const bool include_empty=f["include_empty"]=="1";
        const auto count=addresses.size()*selected_nodes.size();
        auto id=controllers_.queueUserRead(std::move(local_users),std::move(selected_nodes),std::move(addresses),include_empty);
        std::ostringstream o;o<<"{\"ok\":true,\"job_id\":"<<id<<",\"total\":"<<count<<"}";
        return{200,"application/json",o.str()};
    }
    if(r.path=="/api/controllers/read-users/status"&&r.method=="GET"){
        auto q=util::parseForm(r.query);std::uint64_t id=0;try{id=std::stoull(q["job_id"]);}catch(...){}
        auto job=controllers_.userReadJob(id);
        if(!job)return{404,"application/json","{\"error\":\"read job not found\"}"};
        return jsonUserReadJob(*job);
    }

    if(r.path=="/api/controllers/upload-users"&&r.method=="POST"){
        auto f=util::parseForm(r.body);std::vector<User> selected_users;std::vector<int> selected_nodes;
        const auto all_users=f["all_users"]=="1";const auto all_controllers=f["all_controllers"]=="1";
        auto allu=users_.list();
        if(all_users)selected_users=allu;else{auto ids=parseIntList(f["user_ids"]);for(int id:ids){auto u=users_.byId(id);if(u)selected_users.push_back(*u);}}
        auto allc=controllers_.controllers();
        if(all_controllers){for(const auto&c:allc)if(c.enabled)selected_nodes.push_back(c.node);}else{auto nodes=parseIntList(f["controller_nodes"]);for(int node:nodes){auto it=std::find_if(allc.begin(),allc.end(),[&](const Controller&c){return c.node==node&&c.enabled;});if(it!=allc.end())selected_nodes.push_back(node);}}
        if(selected_users.empty())return{400,"application/json","{\"ok\":false,\"error\":\"no users selected\"}"};
        if(selected_nodes.empty())return{400,"application/json","{\"ok\":false,\"error\":\"no controllers selected\"}"};
        auto id=controllers_.queueUserUpload(std::move(selected_users),std::move(selected_nodes));
        std::ostringstream o;o<<"{\"ok\":true,\"job_id\":"<<id<<",\"protocol_ready\":"<<(controllers_.userUploadProtocolReady()?"true":"false")<<",\"protocol_message\":\""<<util::jsonEscape(controllers_.userUploadProtocolMessage())<<"\"}";
        return{200,"application/json",o.str()};
    }
    if(r.path=="/api/controllers/upload-users/status"&&r.method=="GET"){
        auto q=util::parseForm(r.query);std::uint64_t id=0;try{id=std::stoull(q["job_id"]);}catch(...){}
        auto job=controllers_.userUploadJob(id);if(!job)return{404,"application/json","{\"error\":\"upload job not found\"}"};return jsonUserUploadJob(*job);
    }
    if(r.path=="/api/users/delete-selected"&&r.method=="POST"){
        auto f=util::parseForm(r.body);
        const bool delete_system=f["delete_system"]=="1";
        const bool delete_controllers=f["delete_controllers"]=="1";
        if(!delete_system&&!delete_controllers)
            return{400,"application/json","{\"ok\":false,\"error\":\"delete target not selected\"}"};

        std::vector<User> selected_users;
        const auto all_users=f["all_users"]=="1";
        if(all_users)selected_users=users_.list();
        else{
            for(int id:parseIntList(f["user_ids"])){
                auto u=users_.byId(id);
                if(u)selected_users.push_back(*u);
            }
        }
        if(selected_users.empty())
            return{400,"application/json","{\"ok\":false,\"error\":\"no users selected\"}"};

        if(!delete_controllers){
            std::vector<int> ids;ids.reserve(selected_users.size());
            for(const auto&u:selected_users)ids.push_back(u.id);
            const int removed=users_.eraseMany(ids);
            if(removed>0)attendance_.refreshUserMetadata();
            std::ostringstream o;o<<"{\"ok\":true,\"immediate\":true,\"local_deleted\":"<<removed<<"}";
            return{200,"application/json",o.str()};
        }

        std::vector<int> selected_nodes;
        const auto all_controllers=f["all_controllers"]=="1";
        auto allc=controllers_.controllers();
        if(all_controllers){
            for(const auto&c:allc)if(c.enabled)selected_nodes.push_back(c.node);
        }else{
            for(int node:parseIntList(f["controller_nodes"])){
                auto it=std::find_if(allc.begin(),allc.end(),[&](const Controller&c){return c.node==node&&c.enabled;});
                if(it!=allc.end())selected_nodes.push_back(node);
            }
        }
        if(selected_nodes.empty())
            return{400,"application/json","{\"ok\":false,\"error\":\"no controllers selected\"}"};

        auto id=controllers_.queueUserDelete(std::move(selected_users),std::move(selected_nodes),delete_system);
        std::ostringstream o;
        o<<"{\"ok\":true,\"immediate\":false,\"job_id\":"<<id
         <<",\"delete_from_system\":"<<(delete_system?"true":"false")<<"}";
        return{200,"application/json",o.str()};
    }
    if(r.path=="/api/users/delete-selected/status"&&r.method=="GET"){
        auto q=util::parseForm(r.query);std::uint64_t id=0;try{id=std::stoull(q["job_id"]);}catch(...){}
        auto job=controllers_.userDeleteJob(id);
        if(!job)return{404,"application/json","{\"error\":\"delete job not found\"}"};
        return jsonUserDeleteJob(*job);
    }
    if(r.path=="/api/users/save"&&r.method=="POST"){
        auto f=util::parseForm(r.body);User u;try{u.id=std::stoi(f["id"]);}catch(...){}
        u.enabled=f["enabled"]!="0";u.last_name=f["last_name"];u.first_name=f["first_name"];u.middle_name=f["middle_name"];u.department=f["department"];u.position=f["position"];
        u.card_series=util::trim(f["card_series"]);u.card_number=util::trim(f["card_number"]);u.pin_code=util::trim(f["pin_code"]);u.access_mode=f["access_mode"].empty()?"card":f["access_mode"];
        std::uint16_t series=0,number=0;std::string validation_error;
        const bool any_card=!u.card_series.empty()||!u.card_number.empty();
        if(any_card){
            if(!util::parseCardParts(u.card_series,u.card_number,series,number,&validation_error))return{400,"application/json","{\"ok\":false,\"error\":\""+util::jsonEscape(validation_error)+"\"}"};
            u.card_series=util::formatCardSeries(series);u.card_number=std::to_string(number);u.card=util::formatCardId(series,number);
        }
        if(!u.pin_code.empty()){
            bool digits=u.pin_code.size()==4&&std::all_of(u.pin_code.begin(),u.pin_code.end(),[](unsigned char c){return std::isdigit(c);});
            int pv=0;try{pv=std::stoi(u.pin_code);}catch(...){}
            if(!digits||pv<1||pv>9999)return{400,"application/json","{\"ok\":false,\"error\":\"PIN должен состоять из 4 цифр и быть в диапазоне 0001..9999\"}"};
        }
        if(u.access_mode!="card"&&u.access_mode!="card_or_pin"&&u.access_mode!="card_and_pin")u.access_mode="card";
        if((u.access_mode=="card_or_pin"||u.access_mode=="card_and_pin")&&u.pin_code.empty())return{400,"application/json","{\"ok\":false,\"error\":\"Для режима с PIN необходимо указать PIN пользователя\"}"};
        try{u.controller_port=std::stoi(f["controller_port"]);}catch(...){u.controller_port=0;}if(u.controller_port<0)u.controller_port=0;if(u.controller_port>16383)u.controller_port=16383;
        if(u.id>0){auto old=users_.byId(u.id);if(old){u.valid_from=old->valid_from;u.valid_until=old->valid_until;u.telegram_arrival=old->telegram_arrival;u.telegram_departure=old->telegram_departure;}}
        auto saved=users_.upsert(u);if(!saved.department.empty())departments_.add(saved.department);attendance_.refreshUserMetadata();return{200,"application/json","{\"ok\":true,\"id\":"+std::to_string(saved.id)+"}"};
    }
    if(r.path=="/api/users/delete"&&r.method=="POST"){auto f=util::parseForm(r.body);bool ok=false;try{ok=users_.erase(std::stoi(f["id"]));}catch(...){}attendance_.refreshUserMetadata();return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}"};}
    if(r.path=="/api/departments/save"&&r.method=="POST"){
        auto f=util::parseForm(r.body);
        auto old_name=util::trim(f["old_name"]),name=util::trim(f["name"]);
        if(name.empty())return{400,"application/json","{\"ok\":false,\"error\":\"empty department\"}"};
        bool ok=false;
        if(old_name.empty())ok=departments_.add(name);
        else if(old_name==name)ok=true;
        else{
            ok=departments_.rename(old_name,name);
            if(ok){users_.renameDepartment(old_name,name);attendance_.refreshUserMetadata();}
        }
        return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"department already exists or was not found\"}"};
    }
    if(r.path=="/api/departments/delete"&&r.method=="POST"){
        auto f=util::parseForm(r.body);auto name=util::trim(f["name"]);
        if(users_.departmentInUse(name))return{200,"application/json","{\"ok\":false,\"error\":\"department is used by users\"}"};
        bool ok=departments_.erase(name);
        return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"department not found\"}"};
    }
    if(r.path=="/api/cards/assign"&&r.method=="POST"){auto f=util::parseForm(r.body);bool ok=false;try{ok=users_.assignCard(std::stoi(f["user_id"]),f["card"]);}catch(...){}attendance_.refreshUserMetadata();return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}"};}
    if(r.path=="/api/cards/delete"&&r.method=="POST"){auto f=util::parseForm(r.body);bool ok=users_.removeCard(f["card"]);attendance_.refreshUserMetadata();return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}"};}
    if(r.path=="/api/controllers/name"&&r.method=="POST"){auto f=util::parseForm(r.body);bool ok=false;try{ok=controllers_.renameController(std::stoi(f["node"]),f["name"]);}catch(...){}return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}"};}
    if(r.path=="/api/telegram/test"&&r.method=="POST"){std::string err;bool ok=telegram_.sendTest(err);return{200,"application/json",ok?"{\"ok\":true}":("{\"ok\":false,\"error\":\""+util::jsonEscape(err)+"\"}")};}
    if(r.path=="/api/export/users"){Res x{200,"text/csv; charset=utf-8",users_.exportCsv()};x.headers.push_back({"Content-Disposition","attachment; filename=users.csv"});return x;}
    if(r.path=="/api/import/users"&&r.method=="POST"){std::string err;bool ok=users_.importCsv(r.body,err);if(ok)departments_.ensure(users_.usedDepartments());attendance_.refreshUserMetadata();return{200,"application/json",ok?"{\"ok\":true}":("{\"ok\":false,\"error\":\""+util::jsonEscape(err)+"\"}")};}
    if(r.path=="/api/export/settings"){Res x{200,"text/plain; charset=utf-8",cfg_.raw()};x.headers.push_back({"Content-Disposition","attachment; filename=system.conf"});return x;}
    if(r.path=="/api/import/settings"&&r.method=="POST"){bool ok=cfg_.replaceRaw(r.body);return{200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}"};}
    if(r.path=="/api/settings/save"&&r.method=="POST"){auto f=util::parseForm(r.body);if(!f["username"].empty())cfg_.set("auth.username",f["username"]);if(!f["password"].empty()){auto salt=util::randomToken(16);cfg_.set("auth.salt",salt);cfg_.set("auth.password_hash",util::sha256Hex(salt+f["password"]));}if(!f["port"].empty())cfg_.set("server.port",f["port"]);if(!f["serial_device"].empty())cfg_.set("serial.device",f["serial_device"]);cfg_.set("telegram.enabled",f["telegram_enabled"]=="1"?"true":"false");cfg_.set("telegram.bot_token",f["bot_token"]);cfg_.set("telegram.chat_id",f["chat_id"]);cfg_.save();return{200,"application/json","{\"ok\":true,\"restart_required\":true}"};}
    return{404,"application/json","{\"error\":\"not found\"}"};
}
}
