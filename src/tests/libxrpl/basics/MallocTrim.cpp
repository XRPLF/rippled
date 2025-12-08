#include <xrpl/basics/MallocTrim.h>

#include <boost/predef.h>

#include <doctest/doctest.h>

using namespace ripple;

#if defined(__GLIBC__) && BOOST_OS_LINUX
namespace ripple::detail {
long
parseVmRSSkB(std::string const& status);
}  // namespace ripple::detail
#endif

TEST_CASE("MallocTrimReport structure")
{
    // Test default construction
    MallocTrimReport report;
    CHECK(report.supported == false);
    CHECK(report.trimResult == -1);
    CHECK(report.rssBeforeKB == -1);
    CHECK(report.rssAfterKB == -1);
    CHECK(report.deltaKB() == 0);

    // Test deltaKB calculation - memory freed
    report.rssBeforeKB = 1000;
    report.rssAfterKB = 800;
    CHECK(report.deltaKB() == -200);

    // Test deltaKB calculation - memory increased
    report.rssBeforeKB = 500;
    report.rssAfterKB = 600;
    CHECK(report.deltaKB() == 100);

    // Test deltaKB calculation - no change
    report.rssBeforeKB = 1234;
    report.rssAfterKB = 1234;
    CHECK(report.deltaKB() == 0);
}

#if defined(__GLIBC__) && BOOST_OS_LINUX
TEST_CASE("parseVmRSSkB")
{
    using ripple::detail::parseVmRSSkB;

    // Test standard format
    {
        std::string status = "VmRSS:      123456 kB\n";
        long result = parseVmRSSkB(status);
        CHECK(result == 123456);
    }

    // Test with multiple lines
    {
        std::string status =
            "Name:   rippled\n"
            "VmPeak:  1234567 kB\n"
            "VmSize:  1234567 kB\n"
            "VmRSS:      987654 kB\n"
            "VmData:   123456 kB\n";
        long result = parseVmRSSkB(status);
        CHECK(result == 987654);
    }

    // Test with minimal whitespace
    {
        std::string status = "VmRSS: 42 kB";
        long result = parseVmRSSkB(status);
        CHECK(result == 42);
    }

    // Test with extra whitespace
    {
        std::string status = "VmRSS:          999999 kB";
        long result = parseVmRSSkB(status);
        CHECK(result == 999999);
    }

    // Test with tabs
    {
        std::string status = "VmRSS:\t\t12345 kB";
        long result = parseVmRSSkB(status);
        // Note: tabs are not explicitly handled as spaces, this documents
        // current behavior
        CHECK(result == 12345);
    }

    // Test zero value
    {
        std::string status = "VmRSS:      0 kB\n";
        long result = parseVmRSSkB(status);
        CHECK(result == 0);
    }

    // Test missing VmRSS
    {
        std::string status =
            "Name:   rippled\n"
            "VmPeak:  1234567 kB\n"
            "VmSize:  1234567 kB\n";
        long result = parseVmRSSkB(status);
        CHECK(result == -1);
    }

    // Test empty string
    {
        std::string status = "";
        long result = parseVmRSSkB(status);
        CHECK(result == -1);
    }

    // Test malformed data (VmRSS but no number)
    {
        std::string status = "VmRSS:      \n";
        long result = parseVmRSSkB(status);
        // sscanf should fail to parse and return -1 unchanged
        CHECK(result == -1);
    }

    // Test malformed data (VmRSS but invalid number)
    {
        std::string status = "VmRSS:      abc kB\n";
        long result = parseVmRSSkB(status);
        // sscanf should fail and return -1 unchanged
        CHECK(result == -1);
    }

    // Test partial match (should not match "NotVmRSS:")
    {
        std::string status = "NotVmRSS:      123456 kB\n";
        long result = parseVmRSSkB(status);
        CHECK(result == -1);
    }
}
#endif

TEST_CASE("mallocTrim basic functionality")
{
    beast::Journal journal{beast::Journal::getNullSink()};

    // Test with no tag
    {
        MallocTrimReport report = mallocTrim(std::nullopt, journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
        // On Linux with glibc, should be supported
        CHECK(report.supported == true);
        // trimResult should be 0 or 1 (success indicators)
        CHECK(report.trimResult >= 0);
#else
        // On other platforms, should be unsupported
        CHECK(report.supported == false);
        CHECK(report.trimResult == -1);
        CHECK(report.rssBeforeKB == -1);
        CHECK(report.rssAfterKB == -1);
#endif
    }

    // Test with tag
    {
        MallocTrimReport report =
            mallocTrim(std::optional<std::string>("test_tag"), journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
        CHECK(report.supported == true);
        CHECK(report.trimResult >= 0);
#else
        CHECK(report.supported == false);
#endif
    }
}

TEST_CASE("mallocTrim with debug logging")
{
    beast::Journal journal{beast::Journal::getNullSink()};

    MallocTrimReport report =
        mallocTrim(std::optional<std::string>("debug_test"), journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
    CHECK(report.supported == true);
    // The function should complete without crashing
#else
    CHECK(report.supported == false);
#endif
}

TEST_CASE("mallocTrim repeated calls")
{
    beast::Journal journal{beast::Journal::getNullSink()};

    // Call malloc_trim multiple times to ensure it's safe
    for (int i = 0; i < 5; ++i)
    {
        MallocTrimReport report = mallocTrim(
            std::optional<std::string>("iteration_" + std::to_string(i)),
            journal);

#if defined(__GLIBC__) && BOOST_OS_LINUX
        CHECK(report.supported == true);
        CHECK(report.trimResult >= 0);
#else
        CHECK(report.supported == false);
#endif
    }
}
