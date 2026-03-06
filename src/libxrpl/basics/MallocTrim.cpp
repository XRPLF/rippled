#include <xrpl/basics/Log.h>
#include <xrpl/basics/MallocTrim.h>

#include <boost/predef.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

#if defined(__GLIBC__) && BOOST_OS_LINUX
#include <sys/resource.h>

#include <malloc.h>
#include <unistd.h>

// Require RUSAGE_THREAD for thread-scoped page fault tracking
#ifndef RUSAGE_THREAD
#error "MallocTrim rusage instrumentation requires RUSAGE_THREAD on Linux/glibc"
#endif

namespace {

bool
getRusageThread(struct rusage& ru)
{
    return ::getrusage(RUSAGE_THREAD, &ru) == 0;  // LCOV_EXCL_LINE
}

}  // namespace
#endif

namespace xrpl {

namespace detail {

// cSpell:ignore statm

#if defined(__GLIBC__) && BOOST_OS_LINUX

inline int
mallocTrimWithPad(std::size_t padBytes)
{
    return ::malloc_trim(padBytes);
}

long
parseStatmRSSkB(std::string const& statm)
{
    // /proc/self/statm format: size resident shared text lib data dt
    // We want the second field (resident) which is in pages
    std::istringstream iss(statm);
    long size, resident;
    if (!(iss >> size >> resident))
        return -1;

    // Convert pages to KB
    long const pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return -1;

    return (resident * pageSize) / 1024;
}

#endif  // __GLIBC__ && BOOST_OS_LINUX

}  // namespace detail

MallocTrimReport
mallocTrim(std::string_view tag, beast::Journal journal)
{
    // LCOV_EXCL_START

    MallocTrimReport report;

#if !(defined(__GLIBC__) && BOOST_OS_LINUX)
    JLOG(journal.debug()) << "malloc_trim not supported on this platform (tag=" << tag << ")";
#else
    // Keep glibc malloc_trim padding at 0 (default): 12h Mainnet tests across 0/256KB/1MB/16MB
    // showed no clear, consistent benefit from custom padding—0 provided the best overall balance
    // of RSS reduction and trim-latency stability without adding a tuning surface.
    constexpr std::size_t TRIM_PAD = 0;

    report.supported = true;

    if (journal.debug())
    {
        auto readFile = [](std::string const& path) -> std::string {
            std::ifstream ifs(path, std::ios::in | std::ios::binary);
            if (!ifs.is_open())
                return {};

            // /proc files are often not seekable; read as a stream.
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        };

        std::string const tagStr{tag};
        std::string const statmPath = "/proc/self/statm";

        auto const statmBefore = readFile(statmPath);
        long const rssBeforeKB = detail::parseStatmRSSkB(statmBefore);

        struct rusage ru0{};
        bool const have_ru0 = getRusageThread(ru0);

        auto const t0 = std::chrono::steady_clock::now();

        report.trimResult = detail::mallocTrimWithPad(TRIM_PAD);

        auto const t1 = std::chrono::steady_clock::now();

        struct rusage ru1{};
        bool const have_ru1 = getRusageThread(ru1);

        auto const statmAfter = readFile(statmPath);
        long const rssAfterKB = detail::parseStatmRSSkB(statmAfter);

        // Populate report fields
        report.rssBeforeKB = rssBeforeKB;
        report.rssAfterKB = rssAfterKB;
        report.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

        if (have_ru0 && have_ru1)
        {
            report.minfltDelta = ru1.ru_minflt - ru0.ru_minflt;
            report.majfltDelta = ru1.ru_majflt - ru0.ru_majflt;
        }

        std::int64_t const deltaKB = (rssBeforeKB < 0 || rssAfterKB < 0)
            ? 0
            : (static_cast<std::int64_t>(rssAfterKB) - static_cast<std::int64_t>(rssBeforeKB));

        JLOG(journal.debug()) << "malloc_trim tag=" << tagStr << " result=" << report.trimResult
                              << " pad=" << TRIM_PAD << " bytes"
                              << " rss_before=" << rssBeforeKB << "kB"
                              << " rss_after=" << rssAfterKB << "kB"
                              << " delta=" << deltaKB << "kB"
                              << " duration_us=" << report.durationUs.count()
                              << " minflt_delta=" << report.minfltDelta
                              << " majflt_delta=" << report.majfltDelta;
    }
    else
    {
        report.trimResult = detail::mallocTrimWithPad(TRIM_PAD);
    }

#endif

    return report;

    // LCOV_EXCL_STOP
}

}  // namespace xrpl
