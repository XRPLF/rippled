#include <xrpl/basics/Log.h>
#include <xrpl/basics/MallocTrim.h>

#include <boost/predef.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
// #include <thread>

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
    return ::getrusage(RUSAGE_THREAD, &ru) == 0;
}

}  // namespace
#endif

namespace xrpl {

namespace detail {

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
mallocTrim([[maybe_unused]] std::optional<std::string> const& tag, beast::Journal journal)
{
    MallocTrimReport report;

#if !(defined(__GLIBC__) && BOOST_OS_LINUX)
    JLOG(journal.debug()) << "malloc_trim not supported on this platform";
#else

    constexpr std::size_t KB = 1024;
    constexpr std::size_t MB = 1024 * KB;

    constexpr std::size_t TRIM_PAD = 256 * KB;
    // constexpr std::size_t TRIM_PAD = 1 * MB;
    // constexpr std::size_t TRIM_PAD = 16 * MB;
    // constexpr std::size_t TRIM_PAD = 0;

    report.supported = true;

    if (journal.debug())
    {
        auto readFile = [](std::string const& path) -> std::string {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open())
                return {};

            ifs.seekg(0, std::ios::end);
            auto const size = ifs.tellg();
            if (size < 0)
                return {};

            ifs.seekg(0, std::ios::beg);
            std::string result(static_cast<std::size_t>(size), '\0');
            ifs.read(result.data(), size);

            return result;
        };

        std::string const tagStr = tag.value_or("default");
        std::string const statmPath = "/proc/self/statm";

        auto const statmBefore = readFile(statmPath);
        long const rssBeforeKB = detail::parseStatmRSSkB(statmBefore);

        struct rusage ru0
        {
        };
        bool const have_ru0 = getRusageThread(ru0);

        auto const t0 = std::chrono::steady_clock::now();

        report.trimResult = detail::mallocTrimWithPad(TRIM_PAD);

        auto const t1 = std::chrono::steady_clock::now();

        struct rusage ru1
        {
        };
        bool const have_ru1 = getRusageThread(ru1);

        auto const statmAfter = readFile(statmPath);
        long const rssAfterKB = detail::parseStatmRSSkB(statmAfter);

        // Populate report fields
        report.rssBeforeKB = rssBeforeKB;
        report.rssAfterKB = rssAfterKB;

        long long const durationUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        long minfltDelta = -1;
        long majfltDelta = -1;
        if (have_ru0 && have_ru1)
        {
            minfltDelta = ru1.ru_minflt - ru0.ru_minflt;
            majfltDelta = ru1.ru_majflt - ru0.ru_majflt;
        }

        long const deltaKB = (rssBeforeKB < 0 || rssAfterKB < 0) ? 0 : (rssAfterKB - rssBeforeKB);

        JLOG(journal.debug()) << "malloc_trim tag=" << tagStr << " result=" << report.trimResult << " pad=" << TRIM_PAD
                              << " bytes"
                              << " rss_before=" << rssBeforeKB << "kB"
                              << " rss_after=" << rssAfterKB << "kB"
                              << " delta=" << deltaKB << "kB"
                              << " duration_us=" << durationUs << " minflt_delta=" << minfltDelta
                              << " majflt_delta=" << majfltDelta;
    }
    else
    {
        report.trimResult = detail::mallocTrimWithPad(TRIM_PAD);
    }

#endif

    return report;
}

}  // namespace xrpl
