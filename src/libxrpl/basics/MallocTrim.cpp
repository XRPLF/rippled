/** @file
 *  Implements `mallocTrim()`, which requests glibc to return fragmented free
 *  heap pages to the OS via `::malloc_trim(0)`.
 *
 *  The entire implementation is compiled only on Linux/glibc
 *  (`__GLIBC__ && BOOST_OS_LINUX`). On all other platforms `mallocTrim()`
 *  is a documented no-op that returns a `MallocTrimReport` with
 *  `supported = false`. A compile-time `#error` guards against Linux/glibc
 *  builds that somehow lack `RUSAGE_THREAD`, enforcing the invariant that
 *  thread-scoped page-fault measurement is always available when the active
 *  code path is compiled.
 *
 *  The public entry point is called from `Application::doSweep()` after
 *  cache evictions and other operations that free significant amounts of
 *  memory, ensuring that the OS RSS reflects actual live heap usage rather
 *  than ptmalloc arena fragmentation.
 */
#include <xrpl/basics/MallocTrim.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/predef.h>

#include <string_view>

#if defined(__GLIBC__) && BOOST_OS_LINUX
#include <sys/resource.h>

#include <malloc.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

// Require RUSAGE_THREAD for thread-scoped page fault tracking
#ifndef RUSAGE_THREAD
#error "MallocTrim rusage instrumentation requires RUSAGE_THREAD on Linux/glibc"
#endif

namespace {

/** Capture a per-thread resource-usage snapshot via `RUSAGE_THREAD`.
 *
 *  Wraps `::getrusage(RUSAGE_THREAD, &ru)` so callers can gate page-fault
 *  delta computation on success without propagating the raw return code.
 *
 *  @param ru Output parameter filled with the current thread's `rusage` data
 *      on success; left in an indeterminate state on failure.
 *  @return `true` if `getrusage` succeeded, `false` otherwise.
 */
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

/** Invoke `::malloc_trim` with the given arena-padding hint.
 *
 *  Requests that glibc release all free heap pages whose addresses lie above
 *  the current break minus `padBytes`. Passing `0` asks for maximum
 *  reclamation. The return value mirrors `::malloc_trim`: `1` if memory was
 *  actually returned to the OS, `0` if the call succeeded but had nothing to
 *  release.
 *
 *  @param padBytes Bytes of free space to retain in the arena after trimming.
 *  @return `1` if memory was released to the OS, `0` if nothing was released.
 */
inline int
mallocTrimWithPad(std::size_t padBytes)
{
    return ::malloc_trim(padBytes);
}

/** Parse the RSS field from a `/proc/self/statm` snapshot and convert to KB.
 *
 *  `/proc/self/statm` exposes seven space-delimited fields (all in pages):
 *  `size resident shared text lib data dt`. This function extracts the second
 *  field (`resident`) and multiplies by the system page size to produce a KB
 *  value. Parsing is done with `std::istringstream`; any extraction failure
 *  (empty input, non-numeric data, or fewer than two fields) sets `failbit`
 *  and causes an early return of `-1`. A non-positive page size from
 *  `sysconf(_SC_PAGESIZE)` also yields `-1`.
 *
 *  @param statm Raw content of `/proc/self/statm`, e.g. `"25365 1000 2377 0 0 5623 0"`.
 *  @return Resident set size in kilobytes, or `-1` if parsing or page-size
 *      lookup failed.
 *
 *  @note The sentinel value `-1` is propagated through `MallocTrimReport`
 *      fields; `MallocTrimReport::deltaKB()` returns `0` rather than a
 *      misleading negative number when either RSS reading carries this sentinel.
 */
long
parseStatmRSSkB(std::string const& statm)
{
    std::istringstream iss(statm);
    long size = 0, resident = 0;
    if (!(iss >> size >> resident))
        return -1;

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
    constexpr static std::size_t kTRIM_PAD = 0;

    report.supported = true;

    if (journal.debug())
    {
        auto readFile = [](std::string const& path) -> std::string {
            std::ifstream ifs(path, std::ios::in | std::ios::binary);
            if (!ifs.is_open())
                return {};

            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        };

        std::string const tagStr{tag};
        std::string const statmPath = "/proc/self/statm";

        auto const statmBefore = readFile(statmPath);
        long const rssBeforeKB = detail::parseStatmRSSkB(statmBefore);

        struct rusage ru0{};
        bool const haveRu0 = getRusageThread(ru0);

        auto const t0 = std::chrono::steady_clock::now();

        report.trimResult = detail::mallocTrimWithPad(kTRIM_PAD);

        auto const t1 = std::chrono::steady_clock::now();

        struct rusage ru1{};
        bool const haveRu1 = getRusageThread(ru1);

        auto const statmAfter = readFile(statmPath);
        long const rssAfterKB = detail::parseStatmRSSkB(statmAfter);

        report.rssBeforeKB = rssBeforeKB;
        report.rssAfterKB = rssAfterKB;
        report.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

        if (haveRu0 && haveRu1)
        {
            report.minfltDelta = ru1.ru_minflt - ru0.ru_minflt;
            report.majfltDelta = ru1.ru_majflt - ru0.ru_majflt;
        }

        std::int64_t const deltaKB = (rssBeforeKB < 0 || rssAfterKB < 0)
            ? 0
            : (static_cast<std::int64_t>(rssAfterKB) - static_cast<std::int64_t>(rssBeforeKB));

        JLOG(journal.debug()) << "malloc_trim tag=" << tagStr << " result=" << report.trimResult
                              << " pad=" << kTRIM_PAD << " bytes"
                              << " rss_before=" << rssBeforeKB << "kB"
                              << " rss_after=" << rssAfterKB << "kB"
                              << " delta=" << deltaKB << "kB"
                              << " duration_us=" << report.durationUs.count()
                              << " minflt_delta=" << report.minfltDelta
                              << " majflt_delta=" << report.majfltDelta;
    }
    else
    {
        report.trimResult = detail::mallocTrimWithPad(kTRIM_PAD);
    }

#endif

    return report;

    // LCOV_EXCL_STOP
}

}  // namespace xrpl
