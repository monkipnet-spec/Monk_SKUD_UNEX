#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace skud {

struct SystemMetricsSnapshot {
    double cpu_percent{0.0};
    double ram_percent{0.0};
    std::uint64_t ram_used_mb{0};
    std::uint64_t ram_total_mb{0};
    std::uint64_t uptime_seconds{0};
};

class SystemMetrics {
public:
    SystemMetricsSnapshot snapshot();

private:
    static bool readCpu(std::uint64_t& idle, std::uint64_t& total);
    static bool readMemory(std::uint64_t& total_kb, std::uint64_t& available_kb);

    std::mutex mu_;
    bool have_cpu_sample_{false};
    std::uint64_t previous_idle_{0};
    std::uint64_t previous_total_{0};
    const std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
};

} // namespace skud
