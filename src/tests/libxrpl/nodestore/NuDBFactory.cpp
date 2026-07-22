#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>

#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::node_store {

namespace {

Section
makeSection(std::string const& path, std::string const& blockSize = "")
{
    Section params;
    params.set("type", "nudb");
    params.set("path", path);
    if (!blockSize.empty())
        params.set("nudb_block_size", blockSize);
    return params;
}

void
runRoundTrip(Section const& params, std::size_t expectedBlocksize)
{
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());
    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

    ASSERT_TRUE(backend);
    ASSERT_EQ(backend->getBlockSize(), expectedBlocksize);
    backend->open();
    ASSERT_TRUE(backend->isOpen());

    auto const batch = createPredictableBatch(10, 12345);
    storeBatch(*backend, batch);

    auto const copy = fetchCopyOfBatch(*backend, batch);

    backend->close();
    EXPECT_EQ(batch, copy);
}

}  // namespace

TEST(NuDBFactory, default_block_size)
{
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 4096));
}

TEST(NuDBFactory, valid_block_sizes)
{
    auto const kValidSizes = std::to_array<std::size_t>({4096, 8192, 16384, 32768});
    for (auto const size : kValidSizes)
    {
        SCOPED_TRACE("size=" + std::to_string(size));
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), std::to_string(size));
        ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, size));
    }

    // empty value is ignored by config parser; default (4096) is used
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "");
        ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 4096));
    }
}

TEST(NuDBFactory, invalid_block_sizes)
{
    std::vector<std::string> const kInvalidSizes = {
        "2048",     // too small
        "1024",     // too small
        "65536",    // too large
        "131072",   // too large
        "5000",     // not power of 2
        "6000",     // not power of 2
        "10000",    // not power of 2
        "0",        // zero
        "-1",       // negative
        "abc",      // non-numeric
        "4k",       // invalid format
        "4096.5"};  // decimal

    for (auto const& size : kInvalidSizes)
    {
        SCOPED_TRACE("size='" + size + "'");
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_THROW(runRoundTrip(params, 4096), std::exception);
    }

    // whitespace handling — lexical_cast may or may not strip; treat as invalid
    std::vector<std::string> const kWhitespaceSizes = {"4096 ", " 4096"};
    for (auto const& size : kWhitespaceSizes)
    {
        SCOPED_TRACE("size='" + size + "'");
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_THROW(runRoundTrip(params, 4096), std::exception);
    }
}

TEST(NuDBFactory, log_messages)
{
    // valid custom block size emits info log
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "8192");
        test::CaptureSink sink(beast::Severity::Info);
        beast::Journal const journal(sink);

        DummyScheduler scheduler;
        [[maybe_unused]] auto backend =
            Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

        EXPECT_TRUE(sink.messages().contains("Using custom NuDB block size: 8192"));
    }

    // invalid block size throws with informative message
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "5000");
        test::CaptureSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            FAIL() << "expected exception for invalid block size 5000";
        }
        catch (std::exception const& e)
        {
            std::string const what{e.what()};
            EXPECT_TRUE(what.contains("Invalid nudb_block_size: 5000"));
            EXPECT_TRUE(what.contains("Must be power of 2 between 4096 and 32768"));
        }
    }

    // non-numeric value throws
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "invalid");
        test::CaptureSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            FAIL() << "expected exception for non-numeric block size";
        }
        catch (std::exception const& e)
        {
            std::string const what{e.what()};
            EXPECT_TRUE(what.contains("Invalid nudb_block_size value: invalid"));
        }
    }
}

TEST(NuDBFactory, power_of_two_validation)
{
    std::vector<std::pair<std::string, bool>> const kCASES = {
        {"4095", false},    // just below minimum
        {"4096", true},     // minimum valid
        {"4097", false},    // not power of 2
        {"8192", true},     // valid power of 2
        {"8193", false},    // not power of 2
        {"16384", true},    // valid power of 2
        {"32768", true},    // maximum valid
        {"32769", false},   // just above maximum
        {"65536", false}};  // power of 2 but too large

    for (auto const& [size, shouldWork] : kCASES)
    {
        SCOPED_TRACE("size=" + size + " shouldWork=" + (shouldWork ? "true" : "false"));
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        test::CaptureSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            EXPECT_TRUE(shouldWork);
        }
        catch (std::exception const& e)
        {
            // A throw is only expected for sizes that should NOT work; if a
            // valid size throws, fail here instead of silently matching the
            // message below (which would mask the regression).
            EXPECT_FALSE(shouldWork);
            std::string const what{e.what()};
            EXPECT_TRUE(what.contains("Invalid nudb_block_size"));
        }
    }
}

TEST(NuDBFactory, both_constructor_variants)
{
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path(), "16384");
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend1 = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    EXPECT_NE(backend1, nullptr);
    ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 16384));

    // Test second constructor (with nudb::context)
    // Note: This would require access to nudb::context, which might not be
    // easily testable without more complex setup. For now, we test that
    // the factory can create backends with the first constructor.
}

TEST(NuDBFactory, configuration_parsing)
{
    // basic valid format emits success log
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "8192");
        test::CaptureSink sink(beast::Severity::Info);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        [[maybe_unused]] auto backend =
            Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
        EXPECT_TRUE(sink.messages().contains("Using custom NuDB block size"));
    }

    // Test whitespace handling separately since lexical_cast behavior may vary
    std::vector<std::string> const kWhitespaceFormats = {" 8192", "8192 "};
    for (auto const& format : kWhitespaceFormats)
    {
        SCOPED_TRACE("format='" + format + "'");
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), format);
        test::CaptureSink sink(beast::Severity::Debug);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        EXPECT_ANY_THROW(Manager::instance().makeBackend(params, megabytes(4), scheduler, journal));
    }
}

TEST(NuDBFactory, data_persistence)
{
    std::vector<std::string> const kBlockSizes = {"4096", "8192", "16384", "32768"};
    for (auto const& size : kBlockSizes)
    {
        SCOPED_TRACE("size=" + size);
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        DummyScheduler scheduler;
        beast::Journal const journal(TestSink::instance());

        // Create test data
        auto const batch = createPredictableBatch(50, 54321);

        // Store data
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            backend->open();
            storeBatch(*backend, batch);
            backend->close();
        }

        // Retrieve data in new backend instance
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            backend->open();
            auto const copy = fetchCopyOfBatch(*backend, batch);
            EXPECT_EQ(batch, copy);
            backend->close();
        }
    }
}

}  // namespace xrpl::node_store
