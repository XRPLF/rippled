#include <xrpl/basics/Log.h>
#include <xrpl/basics/MallocTrim.h>

#include <boost/predef.h>

#include <chrono>
#include <cstdio>
#include <fstream>
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
pid_t const cachedPid = ::getpid();

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
parseVmRSSkB(std::string const& status)
{
    std::istringstream iss(status);
    std::string line;

    while (std::getline(iss, line))
    {
        // Allow leading spaces/tabs before the key.
        auto const firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs == std::string::npos)
            continue;

        constexpr char key[] = "VmRSS:";
        constexpr auto keyLen = sizeof(key) - 1;

        // Require the line (after leading whitespace) to start with "VmRSS:".
        // Check if we have enough characters and the substring matches.
        if (firstNonWs + keyLen > line.size() || line.substr(firstNonWs, keyLen) != key)
            continue;

        // Move past "VmRSS:" and any following whitespace.
        auto pos = firstNonWs + keyLen;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }

        long value = -1;
        if (std::sscanf(line.c_str() + pos, "%ld", &value) == 1)
            return value;

        // Found the key but couldn't parse a number.
        return -1;
    }

    // No VmRSS line found.
    return -1;
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
            std::ifstream ifs(path);
            if (!ifs.is_open())
                return {};
            return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        };

        std::string const tagStr = tag.value_or("default");
        std::string const statusPath = "/proc/" + std::to_string(cachedPid) + "/status";

        auto const statusBefore = readFile(statusPath);
        long const rssBeforeKB = detail::parseVmRSSkB(statusBefore);

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

        auto const statusAfter = readFile(statusPath);
        long const rssAfterKB = detail::parseVmRSSkB(statusAfter);

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
