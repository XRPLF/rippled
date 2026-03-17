#include <xrpl/basics/MallocTrim.h>

#include <boost/predef.h>

#include <gtest/gtest.h>

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
        std::string statm = "25365 1000 2377 0 0 5623 0";
        long result = parseStatmRSSkB(statm);
        // Note: actual result depends on system page size
        // On most systems it's 4KB, so 1000 pages = 4000 KB
        EXPECT_GT(result, 0);
    }

    // Test with newline
    {
        std::string statm = "12345 2000 1234 0 0 3456 0\n";
        long result = parseStatmRSSkB(statm);
        EXPECT_GT(result, 0);
    }

    // Test with tabs
    {
        std::string statm = "12345\t2000\t1234\t0\t0\t3456\t0";
        long result = parseStatmRSSkB(statm);
        EXPECT_GT(result, 0);
    }

    // Test zero resident pages
    {
        std::string statm = "25365 0 2377 0 0 5623 0";
        long result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, 0);
    }

    // Test with extra whitespace
    {
        std::string statm = "  25365   1000   2377  ";
        long result = parseStatmRSSkB(statm);
        EXPECT_GT(result, 0);
    }

    // Test empty string
    {
        std::string statm = "";
        long result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }

    // Test malformed data (only one field)
    {
        std::string statm = "25365";
        long result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }

    // Test malformed data (non-numeric)
    {
        std::string statm = "abc def ghi";
        long result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }

    // Test malformed data (second field non-numeric)
    {
        std::string statm = "25365 abc 2377";
        long result = parseStatmRSSkB(statm);
        EXPECT_EQ(result, -1);
    }
}
#endif

TEST(mallocTrim, without_debug_logging)
{
    beast::Journal journal{beast::Journal::getNullSink()};

    MallocTrimReport report = mallocTrim("without_debug", journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
    EXPECT_EQ(report.supported, true);
    EXPECT_GE(report.trimResult, 0);
    EXPECT_EQ(report.durationUs, std::chrono::microseconds{-1});
    EXPECT_EQ(report.minfltDelta, -1);
    EXPECT_EQ(report.majfltDelta, -1);
#else
    EXPECT_EQ(report.supported, false);
    EXPECT_EQ(report.trimResult, -1);
    EXPECT_EQ(report.rssBeforeKB, -1);
    EXPECT_EQ(report.rssAfterKB, -1);
    EXPECT_EQ(report.durationUs, std::chrono::microseconds{-1});
    EXPECT_EQ(report.minfltDelta, -1);
    EXPECT_EQ(report.majfltDelta, -1);
#endif
}

TEST(mallocTrim, empty_tag)
{
    beast::Journal journal{beast::Journal::getNullSink()};
    MallocTrimReport report = mallocTrim("", journal);

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
        DebugSink() : Sink(beast::severities::kDebug, false)
        {
        }
        void
        write(beast::severities::Severity, std::string const&) override
        {
        }
        void
        writeAlways(beast::severities::Severity, std::string const&) override
        {
        }
    };

    DebugSink sink;
    beast::Journal journal{sink};

    MallocTrimReport report = mallocTrim("debug_test", journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
    EXPECT_EQ(report.supported, true);
    EXPECT_GE(report.trimResult, 0);
    EXPECT_GE(report.durationUs.count(), 0);
    EXPECT_GE(report.minfltDelta, 0);
    EXPECT_GE(report.majfltDelta, 0);
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
    beast::Journal journal{beast::Journal::getNullSink()};

    // Call malloc_trim multiple times to ensure it's safe
    for (int i = 0; i < 5; ++i)
    {
        MallocTrimReport report = mallocTrim("iteration_" + std::to_string(i), journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
        EXPECT_EQ(report.supported, true);
        EXPECT_GE(report.trimResult, 0);
#else
        EXPECT_EQ(report.supported, false);
#endif
    }
}
