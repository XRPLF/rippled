#include <ripple/basics/Log.h>
#include <ripple/collectors/impl/ResourceUsageImpl.h>
#include <exception>

namespace ripple {
namespace collectors {

ResourceUsageImpl::ResourceUsageImpl(beast::Journal journal)
    : m_journal(journal)
    , m_pfs()
    , m_procStat(m_pfs.get_stat())
    , m_task(m_pfs.get_task())
    , m_taskStat(m_task.get_stat())
    , m_startTime(std::chrono::system_clock::now())
{
}

ResourceUsageImpl::~ResourceUsageImpl() = default;

ResultMap
ResourceUsageImpl::getResourceUsage()
{
    ResultMap resultMap;
    this->getStatMetrics(resultMap);
    this->getLoadAvgMetrics(resultMap);
    this->getMemInfoMetrics(resultMap);
    this->getStatusMetrics(resultMap);
    this->getUptimeMetrics(resultMap);
    return resultMap;
}

void
ResourceUsageImpl::getStatMetrics(ResultMap& resultMap)
{
    try
    {
        auto idleTime1 = static_cast<float>(m_procStat.cpus.total.idle);
        auto totalTime1 = static_cast<float>(
            m_procStat.cpus.total.user + m_procStat.cpus.total.nice +
            m_procStat.cpus.total.system + m_procStat.cpus.total.idle +
            m_procStat.cpus.total.iowait + m_procStat.cpus.total.irq +
            m_procStat.cpus.total.softirq);
        m_procStat = m_pfs.get_stat();
        auto idleTime2 = static_cast<float>(m_procStat.cpus.total.idle);
        auto totalTime2 = static_cast<float>(
            m_procStat.cpus.total.user + m_procStat.cpus.total.nice +
            m_procStat.cpus.total.system + m_procStat.cpus.total.idle +
            m_procStat.cpus.total.iowait + m_procStat.cpus.total.irq +
            m_procStat.cpus.total.softirq);
        auto idlePerc =
            ((idleTime2 - idleTime1) * 100) / (totalTime2 - totalTime1);
        resultMap["Idle_perc"] = idlePerc;

        auto totalTaskTime1 =
            static_cast<float>(m_taskStat.utime + m_taskStat.stime);
        m_taskStat = m_task.get_stat();
        auto totalTaskTime2 =
            static_cast<float>(m_taskStat.utime + m_taskStat.stime);
        resultMap["Cpu_rippled_perc"] = m_procStat.cpus.per_item.size() *
            (totalTaskTime2 - totalTaskTime1) * 100 / (totalTime2 - totalTime1);
        resultMap["num_threads_rippled"] = m_taskStat.num_threads;
    }
    catch (const std::exception& exc)
    {
        JLOG(m_journal.error())
            << "Error during ResourceUsageImpl::getStatMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(m_journal.error())
            << "Unknown error during ResourceUsageImpl::getStatMetrics()";
    }
}

void
ResourceUsageImpl::getLoadAvgMetrics(ResultMap& resultMap)
{
    try
    {
        auto loadavg = m_pfs.get_loadavg();
        resultMap["LoadAvg_1min"] = loadavg.last_1min;
        resultMap["LoadAvg_5min"] = loadavg.last_5min;
        resultMap["LoadAvg_15min"] = loadavg.last_15min;
    }
    catch (const std::exception& exc)
    {
        JLOG(m_journal.error())
            << "Error during ResourceUsageImpl::getLoadAvgMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(m_journal.error())
            << "Unknown error during ResourceUsageImpl::getLoadAvgMetrics()";
    }
}

void
ResourceUsageImpl::getMemInfoMetrics(ResultMap& resultMap)
{
    try
    {
        auto meminfo = m_pfs.get_meminfo();
        resultMap["MemTotal_kb"] = meminfo["MemTotal"];
        resultMap["MemFree_kb"] = meminfo["MemFree"];
        resultMap["MemAvailable_kb"] = meminfo["MemAvailable"];
        resultMap["SwapTotal_kb"] = meminfo["SwapTotal"];
        resultMap["SwapFree_kb"] = meminfo["SwapFree"];
    }
    catch (const std::exception& exc)
    {
        JLOG(m_journal.error())
            << "Error during ResourceUsageImpl::getMemInfoMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(m_journal.error())
            << "Unknown error during ResourceUsageImpl::getMemInfoMetrics()";
    }
}

void
ResourceUsageImpl::getStatusMetrics(ResultMap& resultMap)
{
    try
    {
        auto status = m_task.get_status();
        resultMap["VmSize_rippled_kb"] = status.vm_size;
        resultMap["VmSwap_rippled_kb"] = status.vm_swap;
        resultMap["FDSize_rippled"] = status.fd_size;
    }
    catch (const std::exception& exc)
    {
        JLOG(m_journal.error())
            << "Error during ResourceUsageImpl::getStatusMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(m_journal.error())
            << "Unknown error during ResourceUsageImpl::getStatusMetrics()";
    }
}

void
ResourceUsageImpl::getUptimeMetrics(ResultMap& resultMap)
{
    try
    {
        auto uptime = m_pfs.get_uptime();
        resultMap["Uptime_h"] =
            static_cast<float>(std::chrono::duration_cast<std::chrono::minutes>(
                                   uptime.system_time)
                                   .count()) /
            60.0;
        resultMap["Uptime_rippled_h"] = static_cast<float>(
            std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::system_clock::now() - m_startTime)
                .count() /
            60.0);
    }
    catch (const std::exception& exc)
    {
        JLOG(m_journal.error())
            << "Error during ResourceUsageImpl::getUptimeMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(m_journal.error())
            << "Unknown error during ResourceUsageImpl::getUptimeMetrics()";
    }
}

}  // namespace collectors
}  // namespace ripple
