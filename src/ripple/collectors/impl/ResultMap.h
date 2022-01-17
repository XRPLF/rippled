#pragma once

#include <map>
#include <string>

namespace ripple {
namespace collectors {

/**
 * @todo Make the key an enumerator?
 * @brief Defines the map, containing the resource usage statistics.
 * Contains the following keys:
 * Idle_perc: Percentage of total cpu idle time. Source: /proc/stat.
 * Cpu_rippled_perc, num_threads_rippled: Percentage of total cpu time, number of threads, used by the current process. Source: /proc/[pid]/stat.
 * LoadAvg_1min, LoadAvg_5min, LoadAvg_15min: The system's loadavg statistics. Source: /proc/loadavg.
 * MemTotal_kb, MemFree_kb, MemAvailable_kb, SwapTotal_kb, SwapFree_kb: The system's memory statistics. Source: /proc/meminfo.
 * VmSize_rippled_kb, VmSwap_rippled_kb, FDSize_rippled: Process statistics. Source: /proc/[pid]/stat.
 * Uptime_h: System uptime. Source: /proc/uptime.
 * Uptime_rippled_h: Process uptime.
 * @note See also https://man7.org/linux/man-pages/man5/proc.5.html.
 */
using ResultMap = std::map<std::string, float>;

}  // namespace collectors
}  // namespace ripple
