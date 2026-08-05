#include <xrpl/basics/MallocTrim.h>

#include <xrpl/beast/utility/Journal.h>

#include <boost/predef.h>

#include <gtest/gtest.h>

#include <string>

using namespace xrpl;

// cSpell:ignore statm

#if defined(__GLIBC__) && BOOST_OS_LINUX
namespace xrpl::detail {
long
parseStatmRSSkB(std::string const& statm);
}  // namespace xrpl::detail
#endif

TEST(MallocTrimReport, structure)
{
    // Test default construction
    MallocTrimReport report;
    EXPECT_EQ(report.supported, false);
    EXPECT_EQ(report.trimResult, -1);
    EXPECT_EQ(report.rssBeforeKB, -1);
    EXPECT_EQ(report.rssAfterKB, -1);
    EXPECT_EQ(report.durationUs, std::chrono::microseconds{-1});
    EXPECT_EQ(report.minfltDelta, -1);
    EXPECT_EQ(report.majfltDelta, -1);
    EXPECT_EQ(report.deltaKB(), 0);

    // Test deltaKB calculation - memory freed
    report.rssBeforeKB = 1000;
    report.rssAfterKB = 800;
    EXPECT_EQ(report.deltaKB(), -200);

    // Test deltaKB calculation - memory increased
    report.rssBeforeKB = 500;
    report.rssAfterKB = 600;
    EXPECT_EQ(report.deltaKB(), 100);

    // Test deltaKB calculation - no change
    report.rssBeforeKB = 1234;
    report.rssAfterKB = 1234;
    EXPECT_EQ(report.deltaKB(), 0);
}

#if defined(__GLIBC__) && BOOST_OS_LINUX
TEST(parseStatmRSSkB, standard_format)
{
    using xrpl::detail::parseStatmRSSkB;

    // Test standard format: size resident shared text lib data dt
    // Assuming 4KB page size: resident=1000 pages = 4000 KB
    {
        std::string const statm = "25365 1000 2377 0 0 5623 0";
        long const result = parseStatmRSSkB(statm);
        // Note: actual result depends on system page size
        // On most systems it's 4KB, so 1000 pages = 4000 KB
        EXPECT_GT(result, 0);
    }

    // Test with newline
    {
        std::string const statm = "12345 2000 1234 0 0 3456 0\n";
        long const result = parseStatmRSSkB(statm);
        EXPECT_GT(result, 0);
    }

    // Test with tabs
    {
        std::string const statm = "12345\t2000\t1234\t0\t0\t3456\t0";
        long const result = parseStatmRSSkB(statm);
        EXPECT_GT(result, 0);
    }

    // Test zero resident pages
    {
        std::string const statm = "25365 0 2377 0 0 5623 0";
        long const result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, 0);
    }

    // Test with extra whitespace
    {
        std::string const statm = "  25365   1000   2377  ";
        long const result = parseStatmRSSkB(statm);
        EXPECT_GT(result, 0);
    }

    // Test empty string
    {
        std::string const statm;
        long const result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }

    // Test malformed data (only one field)
    {
        std::string const statm = "25365";
        long const result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }

    // Test malformed data (non-numeric)
    {
        std::string const statm = "abc def ghi";
        long const result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }

    // Test malformed data (second field non-numeric)
    {
        std::string const statm = "25365 abc 2377";
        long const result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }
}
#endif

// The measurement must NOT depend on the journal's severity. It used to sit
// inside `if (journal.debug())`, so an ordinary node -- which does not run at
// debug level -- measured nothing and the caller had no duration to record.
// This is the regression test for that: with a null sink (nothing is even
// loggable) every field must still be populated.
TEST(mallocTrim, measures_without_debug_logging)
{
    beast::Journal const journal{beast::Journal::getNullSink()};

    MallocTrimReport const report = mallocTrim("without_debug", journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
    EXPECT_EQ(report.supported, true);
    EXPECT_GE(report.trimResult, 0);

    // The three measured fields are populated, NOT left at their -1
    // "not measured" sentinel. Asserting >= 0 rather than == a fixed number
    // because these are real timings; the sentinel is what the test excludes.
    EXPECT_GE(report.durationUs.count(), 0);
    EXPECT_GE(report.minfltDelta, 0);
    EXPECT_GE(report.majfltDelta, 0);

    // RSS is read on both sides of the trim, so both are real page counts.
    // A live process always has resident pages, so these are strictly > 0.
    EXPECT_GT(report.rssBeforeKB, 0);
    EXPECT_GT(report.rssAfterKB, 0);

    // deltaKB() is now derived from two real readings rather than from the
    // sentinel pair, so it is the genuine change: a trim never grows RSS by
    // more than another thread could allocate concurrently, and this test is
    // single-threaded, so the reading cannot be positive.
    EXPECT_LE(report.deltaKB(), 0);
    EXPECT_EQ(report.deltaKB(), report.rssAfterKB - report.rssBeforeKB);
#else
    // NEGATIVE PLATFORM PATH: not Linux/glibc, so there is no trim at all and
    // every field must keep its sentinel. A zero here would falsely claim a
    // free trim happened.
    EXPECT_EQ(report.supported, false);
    EXPECT_EQ(report.trimResult, -1);
    EXPECT_EQ(report.rssBeforeKB, -1);
    EXPECT_EQ(report.rssAfterKB, -1);
    EXPECT_EQ(report.durationUs, std::chrono::microseconds{-1});
    EXPECT_EQ(report.minfltDelta, -1);
    EXPECT_EQ(report.majfltDelta, -1);
    EXPECT_EQ(report.deltaKB(), 0);
#endif
}

TEST(mallocTrim, empty_tag)
{
    beast::Journal const journal{beast::Journal::getNullSink()};
    MallocTrimReport const report = mallocTrim("", journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
    EXPECT_EQ(report.supported, true);
    EXPECT_GE(report.trimResult, 0);
#else
    EXPECT_EQ(report.supported, false);
#endif
}

TEST(mallocTrim, with_debug_logging)
{
    struct DebugSink : public beast::Journal::Sink
    {
        DebugSink() : Sink(beast::Severity::Debug, false)
        {
        }
        void
        write(beast::Severity, std::string const&) override
        {
        }
        void
        writeAlways(beast::Severity, std::string const&) override
        {
        }
    };

    DebugSink sink;
    beast::Journal const journal{sink};

    MallocTrimReport const report = mallocTrim("debug_test", journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
    EXPECT_EQ(report.supported, true);
    EXPECT_GE(report.trimResult, 0);
    EXPECT_GE(report.durationUs.count(), 0);
    EXPECT_GE(report.minfltDelta, 0);
    EXPECT_GE(report.majfltDelta, 0);

    // Same fields as the null-sink case above: raising the severity adds the
    // log line and changes nothing about what is measured. The two tests
    // together are what prove the severity no longer gates the measurement.
    EXPECT_GT(report.rssBeforeKB, 0);
    EXPECT_GT(report.rssAfterKB, 0);
#else
    EXPECT_EQ(report.supported, false);
    EXPECT_EQ(report.trimResult, -1);
    EXPECT_EQ(report.durationUs, std::chrono::microseconds{-1});
    EXPECT_EQ(report.minfltDelta, -1);
    EXPECT_EQ(report.majfltDelta, -1);
#endif
}

TEST(mallocTrim, repeated_calls)
{
    beast::Journal const journal{beast::Journal::getNullSink()};

    // Call malloc_trim multiple times to ensure it's safe
    for (auto i = 0uz; i < 5; ++i)
    {
        MallocTrimReport const report = mallocTrim("iteration_" + std::to_string(i), journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
        EXPECT_EQ(report.supported, true);
        EXPECT_GE(report.trimResult, 0);
#else
        EXPECT_EQ(report.supported, false);
#endif
    }
}
