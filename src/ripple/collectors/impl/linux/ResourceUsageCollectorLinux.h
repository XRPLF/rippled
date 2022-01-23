#pragma once

#include <ripple/beast/utility/Journal.h>
#include <ripple/collectors/ResourceUsageCollectorBase.h>
#include <pfs/procfs.hpp>
#include <chrono>

namespace ripple {
namespace collectors {

/**
 * @brief Retrieves and calculates system and process resource usage statistics on linux.
 */
class ResourceUsageCollectorLinux : public ResourceUsageCollectorBase
{
public:
    explicit ResourceUsageCollectorLinux(
        beast::Journal journal,
        boost::asio::io_service& io_service);
    ~ResourceUsageCollectorLinux() override;

    ResourceUsageCollectorLinux(const ResourceUsageCollectorLinux&) = delete;
    ResourceUsageCollectorLinux& operator=(const ResourceUsageCollectorLinux&) = delete;

    /**
     * @brief Contains the following keys:
     * Idle_perc: Percentage of total cpu idle time. Source: /proc/stat.
     * Cpu_rippled_perc, num_threads_rippled: Percentage of total cpu time, number of threads, used by the current process. Source: /proc/[pid]/stat.
     * LoadAvg_1min, LoadAvg_5min, LoadAvg_15min: The system's loadavg statistics. Source: /proc/loadavg.
     * MemTotal_kb, MemFree_kb, MemAvailable_kb, SwapTotal_kb, SwapFree_kb: The system's memory statistics. Source: /proc/meminfo.
     * VmSize_rippled_kb, VmSwap_rippled_kb, FDSize_rippled: Process statistics. Source: /proc/[pid]/stat.
     * Uptime_h: System uptime. Source: /proc/uptime.
     * Uptime_rippled_h: Process uptime.
     * @note See also https://man7.org/linux/man-pages/man5/proc.5.html.
     */
    void
    collect() override;

private:
    /**
     * @brief Get the Stat Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void
    getStatMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Load Avg Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void
    getLoadAvgMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Mem Info Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void
    getMemInfoMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Status Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void
    getStatusMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Uptime Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void
    getUptimeMetrics(ResultMap& resultMap);

    /// Represents (approximately) the process start time.
    std::chrono::system_clock::time_point m_startTime;
    /// The procfs instance, used to retrieve system resource usage statistics.
    pfs::procfs m_pfs;
    /// The most recently retrieved system resource usage statistics.
    pfs::proc_stat m_procStat;
    /// The task instance, used to retrieve task resource usage statistics.
    pfs::task m_task;
    /// The most recently retrieved task resource usage statistics.
    pfs::task_stat m_taskStat;
};

}  // namespace collectors
}  // namespace ripple
