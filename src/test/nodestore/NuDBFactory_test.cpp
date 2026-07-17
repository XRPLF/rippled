#include <test/nodestore/TestBase.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Types.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

class NuDBFactory_test : public TestBase
{
private:
    // Helper function to create a Section with specified parameters
    static Section
    createSection(std::string const& path, std::string const& blockSize = "")
    {
        Section params;
        params.set(Keys::kType, "nudb");
        params.set(Keys::kPath, path);
        if (!blockSize.empty())
            params.set(Keys::kNudbBlockSize, blockSize);
        return params;
    }

    // Helper function to create a backend and test basic functionality
    bool
    testBackendFunctionality(Section const& params, std::size_t expectedBlocksize)
    {
        try
        {
            DummyScheduler scheduler;
            test::SuiteJournal journal("NuDBFactory_test", *this);

            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

            if (!BEAST_EXPECT(backend))
                return false;

            if (!BEAST_EXPECT(backend->getBlockSize() == expectedBlocksize))
                return false;

            backend->open();

            if (!BEAST_EXPECT(backend->isOpen()))
                return false;

            // Test basic store/fetch functionality
            auto batch = createPredictableBatch(10, 12345);
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

    // Helper function to test log messages
    void
    testLogMessage(Section const& params, beast::Severity level, std::string const& expectedMessage)
    {
        test::StreamSink sink(level);
        beast::Journal const journal(sink);

        DummyScheduler scheduler;
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

        std::string const logOutput = sink.messages().str();
        BEAST_EXPECT(logOutput.contains(expectedMessage));
    }

    // Helper function to test power of two validation
    void
    testPowerOfTwoValidation(std::string const& size, bool shouldWork)
    {
        beast::TempDir const tempDir;
        auto params = createSection(tempDir.path(), size);

        test::StreamSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);

        DummyScheduler scheduler;
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

        std::string const logOutput = sink.messages().str();
        bool const hasWarning = logOutput.contains("Invalid nudb_block_size");

        BEAST_EXPECT(hasWarning == !shouldWork);
    }

public:
    void
    testDefaultBlockSize()
    {
        testcase("Default block size (no nudb_block_size specified)");

        beast::TempDir const tempDir;
        auto params = createSection(tempDir.path());

        // Should work with default 4096 block size
        BEAST_EXPECT(testBackendFunctionality(params, 4096));
    }

    void
    testValidBlockSizes()
    {
        testcase("Valid block sizes");

        std::vector<std::size_t> const validSizes = {4096, 8192, 16384, 32768};

        for (auto const& size : validSizes)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), to_string(size));

            BEAST_EXPECT(testBackendFunctionality(params, size));
        }
        // Empty value is ignored by the config parser, so uses the
        // default
        beast::TempDir const tempDir;
        auto params = createSection(tempDir.path(), "");

        BEAST_EXPECT(testBackendFunctionality(params, 4096));
    }

    void
    testInvalidBlockSizes()
    {
        testcase("Invalid block sizes");

        std::vector<std::string> const invalidSizes = {
            "2048",    // Too small
            "1024",    // Too small
            "65536",   // Too large
            "131072",  // Too large
            "5000",    // Not power of 2
            "6000",    // Not power of 2
            "10000",   // Not power of 2
            "0",       // Zero
            "-1",      // Negative
            "abc",     // Non-numeric
            "4k",      // Invalid format
            "4096.5"   // Decimal
        };

        for (auto const& size : invalidSizes)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), size);

            // Fails
            BEAST_EXPECT(!testBackendFunctionality(params, 4096));
        }

        // Test whitespace cases separately since lexical_cast may handle them
        std::vector<std::string> const whitespaceInvalidSizes = {
            "4096 ",  // Trailing space - might be handled by lexical_cast
            " 4096"   // Leading space - might be handled by lexical_cast
        };

        for (auto const& size : whitespaceInvalidSizes)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), size);

            // Fails
            BEAST_EXPECT(!testBackendFunctionality(params, 4096));
        }
    }

    void
    testLogMessages()
    {
        testcase("Log message verification");

        // Test valid custom block size logging
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), "8192");

            testLogMessage(params, beast::Severity::Info, "Using custom NuDB block size: 8192");
        }

        // Test invalid block size failure
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), "5000");

            test::StreamSink sink(beast::Severity::Warning);
            beast::Journal const journal(sink);

            DummyScheduler scheduler;
            try
            {
                auto backend =
                    Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
                fail();
            }
            catch (std::exception const& e)
            {
                std::string const logOutput{e.what()};
                BEAST_EXPECT(logOutput.contains("Invalid nudb_block_size: 5000"));
                BEAST_EXPECT(logOutput.contains("Must be power of 2 between 4096 and 32768"));
            }
        }

        // Test non-numeric value failure
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), "invalid");

            test::StreamSink sink(beast::Severity::Warning);
            beast::Journal const journal(sink);

            DummyScheduler scheduler;
            try
            {
                auto backend =
                    Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

                fail();
            }
            catch (std::exception const& e)
            {
                std::string const logOutput{e.what()};
                BEAST_EXPECT(logOutput.contains("Invalid nudb_block_size value: invalid"));
            }
        }
    }

    void
    testPowerOfTwoValidation()
    {
        testcase("Power of 2 validation logic");

        // Test edge cases around valid range
        std::vector<std::pair<std::string, bool>> const testCases = {
            {"4095", false},   // Just below minimum
            {"4096", true},    // Minimum valid
            {"4097", false},   // Just above minimum, not power of 2
            {"8192", true},    // Valid power of 2
            {"8193", false},   // Just above valid power of 2
            {"16384", true},   // Valid power of 2
            {"32768", true},   // Maximum valid
            {"32769", false},  // Just above maximum
            {"65536", false}   // Power of 2 but too large
        };

        for (auto const& [size, shouldWork] : testCases)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), size);

            // We test the validation logic by catching exceptions for invalid
            // values
            test::StreamSink sink(beast::Severity::Warning);
            beast::Journal const journal(sink);

            DummyScheduler scheduler;
            try
            {
                auto backend =
                    Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
                BEAST_EXPECT(shouldWork);
            }
            catch (std::exception const& e)
            {
                std::string const logOutput{e.what()};
                BEAST_EXPECT(logOutput.contains("Invalid nudb_block_size"));
            }
        }
    }

    void
    testBothConstructorVariants()
    {
        testcase("Both constructor variants work with custom block size");

        beast::TempDir const tempDir;
        auto params = createSection(tempDir.path(), "16384");

        DummyScheduler scheduler;
        test::SuiteJournal journal("NuDBFactory_test", *this);

        // Test first constructor (without nudb::context)
        {
            auto backend1 =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            BEAST_EXPECT(backend1 != nullptr);
            BEAST_EXPECT(testBackendFunctionality(params, 16384));
        }

        // Test second constructor (with nudb::context)
        // Note: This would require access to nudb::context, which might not be
        // easily testable without more complex setup. For now, we test that
        // the factory can create backends with the first constructor.
    }

    void
    testConfigurationParsing()
    {
        testcase("Configuration parsing edge cases");

        // Test that whitespace is handled correctly
        std::vector<std::string> const validFormats = {
            "8192"  // Basic valid format
        };

        // Test whitespace handling separately since lexical_cast behavior may
        // vary
        std::vector<std::string> const whitespaceFormats = {
            " 8192",  // Leading space - may or may not be handled by
                      // lexical_cast
            "8192 "   // Trailing space - may or may not be handled by
                      // lexical_cast
        };

        // Test basic valid format
        for (auto const& format : validFormats)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), format);

            test::StreamSink sink(beast::Severity::Info);
            beast::Journal const journal(sink);

            DummyScheduler scheduler;
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

            // Should log success message for valid values
            std::string const logOutput = sink.messages().str();
            bool const hasSuccessMessage = logOutput.contains("Using custom NuDB block size");
            BEAST_EXPECT(hasSuccessMessage);
        }

        // Test whitespace formats - these should work if lexical_cast handles
        // them
        for (auto const& format : whitespaceFormats)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), format);

            // Use a lower threshold to capture both info and warning messages
            test::StreamSink sink(beast::Severity::Debug);
            beast::Journal const journal(sink);

            DummyScheduler scheduler;
            try
            {
                auto backend =
                    Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
                fail();
            }
            catch (...)
            {
                // Fails
                BEAST_EXPECT(!testBackendFunctionality(params, 8192));
            }
        }
    }

    void
    testDataPersistence()
    {
        testcase("Data persistence with different block sizes");

        std::vector<std::string> const blockSizes = {"4096", "8192", "16384", "32768"};

        for (auto const& size : blockSizes)
        {
            beast::TempDir const tempDir;
            auto params = createSection(tempDir.path(), size);

            DummyScheduler scheduler;
            test::SuiteJournal journal("NuDBFactory_test", *this);

            // Create test data
            auto batch = createPredictableBatch(50, 54321);

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

                Batch copy;
                fetchCopyOfBatch(*backend, &copy, batch);

                BEAST_EXPECT(areBatchesEqual(batch, copy));
                backend->close();
            }
        }
    }

    void
    run() override
    {
        testDefaultBlockSize();
        testValidBlockSizes();
        testInvalidBlockSizes();
        testLogMessages();
        testPowerOfTwoValidation();
        testBothConstructorVariants();
        testConfigurationParsing();
        testDataPersistence();
    }
};

BEAST_DEFINE_TESTSUITE(NuDBFactory, xrpl_core, xrpl);

}  // namespace xrpl::NodeStore
