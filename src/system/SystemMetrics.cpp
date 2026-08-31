#include "skud/SystemMetrics.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace skud {

bool SystemMetrics::readCpu(std::uint64_t& idle, std::uint64_t& total) {
    std::ifstream file("/proc/stat");
    if (!file) return false;

    std::string line;
    if (!std::getline(file, line)) return false;

    std::istringstream in(line);
    std::string cpu;
    in >> cpu;
    if (cpu != "cpu") return false;

    std::uint64_t user = 0, nice = 0, system = 0, idle_ticks = 0, iowait = 0;
    std::uint64_t irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
    in >> user >> nice >> system >> idle_ticks >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;
    if (!in && in.eof()) {
        // Older kernels may expose fewer fields; the fields read before EOF remain valid.
    }

    idle = idle_ticks + iowait;
    total = user + nice + system + idle_ticks + iowait + irq + softirq + steal + guest + guest_nice;
    return total > 0;
}

bool SystemMetrics::readMemory(std::uint64_t& total_kb, std::uint64_t& available_kb) {
    std::ifstream file("/proc/meminfo");
    if (!file) return false;

    total_kb = 0;
    available_kb = 0;
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    while (file >> key >> value >> unit) {
        if (key == "MemTotal:") total_kb = value;
        else if (key == "MemAvailable:") available_kb = value;
        if (total_kb && available_kb) break;
    }
    return total_kb > 0;
}

SystemMetricsSnapshot SystemMetrics::snapshot() {
    std::lock_guard lock(mu_);
    SystemMetricsSnapshot out;
    out.uptime_seconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started_at_).count());

    std::uint64_t idle = 0, total = 0;
    if (readCpu(idle, total)) {
        if (have_cpu_sample_ && total > previous_total_) {
            const auto total_delta = total - previous_total_;
            const auto idle_delta = idle >= previous_idle_ ? idle - previous_idle_ : 0;
            const auto busy_delta = total_delta > idle_delta ? total_delta - idle_delta : 0;
            out.cpu_percent = 100.0 * static_cast<double>(busy_delta) / static_cast<double>(total_delta);
            out.cpu_percent = std::clamp(out.cpu_percent, 0.0, 100.0);
        }
        previous_idle_ = idle;
        previous_total_ = total;
        have_cpu_sample_ = true;
    }

    std::uint64_t total_kb = 0, available_kb = 0;
    if (readMemory(total_kb, available_kb)) {
        if (available_kb > total_kb) available_kb = total_kb;
        const auto used_kb = total_kb - available_kb;
        out.ram_total_mb = total_kb / 1024;
        out.ram_used_mb = used_kb / 1024;
        out.ram_percent = 100.0 * static_cast<double>(used_kb) / static_cast<double>(total_kb);
        out.ram_percent = std::clamp(out.ram_percent, 0.0, 100.0);
    }

    return out;
}

} // namespace skud
