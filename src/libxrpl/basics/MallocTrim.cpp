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
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

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
    long size = 0, resident = 0;
    if (!(iss >> size >> resident))
        return -1;

    // Convert pages to KB
    long const pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return -1;

    return (resident * pageSize) / 1024;
}

/**
 * Read a whole /proc pseudo-file into a string.
 *
 * /proc files are frequently not seekable, so the contents are streamed rather
 * than sized-then-read.
 *
 * @param path Absolute path of the pseudo-file.
 * @return The file contents, or an empty string if it could not be opened.
 */
[[nodiscard]] std::string
readProcFile(std::string const& path)
{
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open())
        return {};

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/**
 * Run malloc_trim and measure what it cost.
 *
 * Kept separate from mallocTrim() so the always-on measurement is one testable
 * unit and the caller is left with only the logging decision.
 *
 * Measurement order brackets the trim as tightly as possible: the two RSS
 * samples are outermost, the two fault samples inside them, and the clock pair
 * innermost, so the reported duration contains the trim and nothing else.
 *
 * @param padBytes glibc trim padding, passed straight to ::malloc_trim.
 * @return A fully populated report. Fields whose source syscall failed keep
 *         their -1 "not measured" sentinel.
 */
[[nodiscard]] MallocTrimReport
measuredTrim(std::size_t padBytes)
{
    MallocTrimReport report;
    report.supported = true;

    std::string const statmPath = "/proc/self/statm";

    report.rssBeforeKB = detail::parseStatmRSSkB(readProcFile(statmPath));

    struct rusage ru0{};
    bool const haveRu0 = getRusageThread(ru0);

    auto const t0 = std::chrono::steady_clock::now();
    report.trimResult = detail::mallocTrimWithPad(padBytes);
    auto const t1 = std::chrono::steady_clock::now();

    struct rusage ru1{};
    bool const haveRu1 = getRusageThread(ru1);

    report.rssAfterKB = detail::parseStatmRSSkB(readProcFile(statmPath));
    report.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    if (haveRu0 && haveRu1)
    {
        report.minfltDelta = ru1.ru_minflt - ru0.ru_minflt;
        report.majfltDelta = ru1.ru_majflt - ru0.ru_majflt;
    }

    return report;
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
    static constexpr std::size_t kTrimPad = 0;

    // The measurement is unconditional, and deliberately not gated on
    // `journal.debug()`. An ordinary node does not run at debug level, so a
    // severity-gated measurement would leave the caller with no duration to
    // record and the per-sweep trim cost invisible in production, the one
    // place it matters.
    //
    // Cost of measuring on every sweep, measured on this platform: two
    // /proc/self/statm reads at ~2.8 us each and two getrusage(RUSAGE_THREAD)
    // calls at ~0.17 us each, so about 6 us in total. The trim it brackets
    // costs milliseconds on a large heap, and the sweep that calls it runs once
    // per SizedItem::SweepInterval (10 s at the fastest, tiny-node setting).
    // That is a duty cycle under 1e-6 percent, and under 1 percent of the
    // measured operation, so nothing here is worth making conditional -- a
    // debug-only RSS read would buy that blind spot back for no saving.
    report = detail::measuredTrim(kTrimPad);

    // Only the LOG stays gated: the string formatting is what an ordinary node
    // genuinely should not pay for, and the numbers now reach the metrics
    // pipeline through the return value instead.
    if (journal.debug())
    {
        JLOG(journal.debug()) << "malloc_trim tag=" << tag << " result=" << report.trimResult
                              << " pad=" << kTrimPad << " bytes"
                              << " rss_before=" << report.rssBeforeKB << "kB"
                              << " rss_after=" << report.rssAfterKB << "kB"
                              << " delta=" << report.deltaKB() << "kB"
                              << " duration_us=" << report.durationUs.count()
                              << " minflt_delta=" << report.minfltDelta
                              << " majflt_delta=" << report.majfltDelta;
    }

#endif

    return report;

    // LCOV_EXCL_STOP
}

}  // namespace xrpl
