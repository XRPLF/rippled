#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Types.h>

#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

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

bool
runRoundTrip(Section const& params, std::size_t expectedBlocksize)
{
    try
    {
        DummyScheduler scheduler;
        beast::Journal const journal(TestSink::instance());
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

        if (!backend || backend->getBlockSize() != expectedBlocksize)
            return false;
        backend->open();
        if (!backend->isOpen())
            return false;

        auto const batch = createPredictableBatch(10, 12345);
        storeBatch(*backend, batch);

        Batch copy;
        fetchCopyOfBatch(*backend, &copy, batch);

        backend->close();
        return areBatchesEqual(batch, copy);
    }
    catch (...)
    {
        return false;
    }
}

}  // namespace

TEST(NuDBFactory, DefaultBlockSize)
{
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    EXPECT_TRUE(runRoundTrip(params, 4096));
}

TEST(NuDBFactory, ValidBlockSizes)
{
    std::vector<std::size_t> const kValidSizes = {4096, 8192, 16384, 32768};
    for (auto const size : kValidSizes)
    {
        SCOPED_TRACE("size=" + std::to_string(size));
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), std::to_string(size));
        EXPECT_TRUE(runRoundTrip(params, size));
    }

    // empty value is ignored by config parser; default (4096) is used
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "");
        EXPECT_TRUE(runRoundTrip(params, 4096));
    }
}

TEST(NuDBFactory, InvalidBlockSizes)
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
        EXPECT_FALSE(runRoundTrip(params, 4096));
    }

    // whitespace handling — lexical_cast may or may not strip; treat as invalid
    std::vector<std::string> const kWhitespaceSizes = {"4096 ", " 4096"};
    for (auto const& size : kWhitespaceSizes)
    {
        SCOPED_TRACE("size='" + size + "'");
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_FALSE(runRoundTrip(params, 4096));
    }
}

TEST(NuDBFactory, LogMessages)
{
    // valid custom block size emits info log
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "8192");
        test::CaptureSink sink(beast::Severity::Info);
        beast::Journal const journal(sink);

        DummyScheduler scheduler;
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

        EXPECT_NE(
            sink.messages().str().find("Using custom NuDB block size: 8192"), std::string::npos);
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
            EXPECT_NE(what.find("Invalid nudb_block_size: 5000"), std::string::npos);
            EXPECT_NE(what.find("Must be power of 2 between 4096 and 32768"), std::string::npos);
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
            EXPECT_NE(what.find("Invalid nudb_block_size value: invalid"), std::string::npos);
        }
    }
}

TEST(NuDBFactory, PowerOfTwoValidation)
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
            std::string const what{e.what()};
            EXPECT_NE(what.find("Invalid nudb_block_size"), std::string::npos);
        }
    }
}

TEST(NuDBFactory, BothConstructorVariants)
{
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path(), "16384");
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend1 = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    EXPECT_NE(backend1, nullptr);
    EXPECT_TRUE(runRoundTrip(params, 16384));

    // second constructor (with nudb::context) requires extra setup; skipped
    // here for the same reason the original beast test skipped it.
}

TEST(NuDBFactory, ConfigurationParsing)
{
    // basic valid format emits success log
    {
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "8192");
        test::CaptureSink sink(beast::Severity::Info);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
        EXPECT_NE(sink.messages().str().find("Using custom NuDB block size"), std::string::npos);
    }

    // whitespace formats fail
    std::vector<std::string> const kWhitespaceFormats = {" 8192", "8192 "};
    for (auto const& format : kWhitespaceFormats)
    {
        SCOPED_TRACE("format='" + format + "'");
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), format);
        test::CaptureSink sink(beast::Severity::Debug);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            FAIL() << "expected exception for whitespace block size '" << format << "'";
        }
        catch (...)
        {
            EXPECT_FALSE(runRoundTrip(params, 8192));
        }
    }
}

TEST(NuDBFactory, DataPersistence)
{
    std::vector<std::string> const kBlockSizes = {"4096", "8192", "16384", "32768"};
    for (auto const& size : kBlockSizes)
    {
        SCOPED_TRACE("size=" + size);
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        DummyScheduler scheduler;
        beast::Journal const journal(TestSink::instance());

        auto const batch = createPredictableBatch(50, 54321);

        // store
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            backend->open();
            storeBatch(*backend, batch);
            backend->close();
        }

        // retrieve from a fresh backend instance
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            backend->open();
            Batch copy;
            fetchCopyOfBatch(*backend, &copy, batch);
            EXPECT_TRUE(areBatchesEqual(batch, copy));
            backend->close();
        }
    }
}

}  // namespace xrpl::NodeStore
