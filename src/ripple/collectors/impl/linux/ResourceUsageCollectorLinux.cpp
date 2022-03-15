#include <ripple/collectors/impl/linux/ResourceUsageCollectorLinux.h>
#include <exception>

using namespace ripple::collectors;

ResourceUsageCollectorLinux::ResourceUsageCollectorLinux(
    beast::Journal journal,
    boost::asio::io_service& io_service)
    : ResourceUsageCollectorBase(journal, io_service)
    , m_startTime(std::chrono::system_clock::now())
    , m_pfs()
    , m_procStat(m_pfs.get_stat())
    , m_task(m_pfs.get_task())
    , m_taskStat(m_task.get_stat())
{
    JLOG(this->journal().info())
        << "Constructed ResourceUsageCollector for linux.";
}

ResourceUsageCollectorLinux::~ResourceUsageCollectorLinux() = default;

void
ResourceUsageCollectorLinux::collect()
{
    JLOG(journal.debug())
        << "Collecting system and process resource usage metrics.";
    ResultMap resultMap;
    this->getStatMetrics(resultMap);
    this->getLoadAvgMetrics(resultMap);
    this->getMemInfoMetrics(resultMap);
    this->getStatusMetrics(resultMap);
    this->getUptimeMetrics(resultMap);
    this->setResultMap(resultMap);
}

void
ResourceUsageCollectorLinux::getStatMetrics(ResultMap& resultMap)
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
        JLOG(this->journal().error())
            << "Error during ResourceUsageCollectorLinux::getStatMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(this->journal().error())
            << "Unknown error during ResourceUsageCollectorLinux::getStatMetrics()";
    }
}

void
ResourceUsageCollectorLinux::getLoadAvgMetrics(ResultMap& resultMap)
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
        JLOG(this->journal().error())
            << "Error during ResourceUsageCollectorLinux::getLoadAvgMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(this->journal().error())
            << "Unknown error during ResourceUsageCollectorLinux::getLoadAvgMetrics()";
    }
}

void
ResourceUsageCollectorLinux::getMemInfoMetrics(ResultMap& resultMap)
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
        JLOG(this->journal().error())
            << "Error during ResourceUsageCollectorLinux::getMemInfoMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(this->journal().error())
            << "Unknown error during ResourceUsageCollectorLinux::getMemInfoMetrics()";
    }
}

void
ResourceUsageCollectorLinux::getStatusMetrics(ResultMap& resultMap)
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
        JLOG(this->journal().error())
            << "Error during ResourceUsageCollectorLinux::getStatusMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(this->journal().error())
            << "Unknown error during ResourceUsageCollectorLinux::getStatusMetrics()";
    }
}

void
ResourceUsageCollectorLinux::getUptimeMetrics(ResultMap& resultMap)
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
        JLOG(this->journal().error())
            << "Error during ResourceUsageCollectorLinux::getUptimeMetrics(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(this->journal().error())
            << "Unknown error during ResourceUsageCollectorLinux::getUptimeMetrics()";
    }
}
