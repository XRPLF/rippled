#pragma once

#include <ripple/beast/utility/Journal.h>
#include <ripple/collectors/impl/ResultMap.h>
#include <pfs/procfs.hpp>
#include <chrono>

namespace ripple {
namespace collectors {

/**
 * @brief Retrieves and calculates system and process resource usage statistics.
 */
class ResourceUsageImpl
{
public:
    ResourceUsageImpl(beast::Journal journal);
    ~ResourceUsageImpl();

    ResourceUsageImpl(const ResourceUsageImpl&) = delete;
    ResourceUsageImpl& operator=(const ResourceUsageImpl&) = delete;

    /**
     * @brief Get the ResultMap, containing system and process resource usage.
     * @return ResultMap Contains resource usage key/value pairs.
     */
    ResultMap getResourceUsage();

private:
    /**
     * @brief Get the Stat Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void getStatMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Load Avg Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void getLoadAvgMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Mem Info Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void getMemInfoMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Status Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void getStatusMetrics(ResultMap& resultMap);

    /**
     * @brief Get the Uptime Metrics object
     * @param resultMap The resultMap, to which the metrics are added.
     * @note Does not raise exceptions. In case of an exception, no values will be added to the resultMap.
     */
    void getUptimeMetrics(ResultMap& resultMap);

    beast::Journal m_journal;
    /// The procfs instance, used to retrieve system resource usage statistics.
    pfs::procfs m_pfs;
    /// The most recently retrieved system resource usage statistics.
    pfs::proc_stat m_procStat;
    /// The task instance, used to retrieve task resource usage statistics.
    pfs::task m_task;
    /// The most recently retrieved task resource usage statistics.
    pfs::task_stat m_taskStat;
    /// Represents (approximately) the process start time.
    std::chrono::system_clock::time_point m_startTime;
};

}  // namespace collectors
}  // namespace ripple
