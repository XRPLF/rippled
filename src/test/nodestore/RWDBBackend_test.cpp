/**
 * @file
 * @brief Tests for RWDB null-backend flag and mode helpers.
 */

#include <xrpl/basics/NullBackendFlag.h>
#include <xrpl/beast/unit_test.h>

namespace xrpl::test {

/**
 * Test null-backend flag used by RWDBBackend / SHAMap / Config.
 *
 * Replaces the former XRPL_RWDB_NULL env-var path with the thread-safe
 * process-wide atomic flag.
 */
class RWDBBackend_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testNullBackendFlag();
        testNullBackendCleanupRegression();
    }

    void
    testNullBackendFlag()
    {
        testcase("nullBackend flag");

        bool const original = isNullBackend();

        setNullBackend(false);
        BEAST_EXPECT(!isNullBackend());

        setNullBackend(true);
        BEAST_EXPECT(isNullBackend());

        setNullBackend(false);
        BEAST_EXPECT(!isNullBackend());

        setNullBackend(original);
    }

    void
    testNullBackendCleanupRegression()
    {
        testcase("nullBackend cleanup regression");

        bool const original = isNullBackend();

        setNullBackend(true);
        BEAST_EXPECT(isNullBackend());

        // Simulate destructor / test cleanup
        setNullBackend(false);
        BEAST_EXPECT(!isNullBackend());

        setNullBackend(true);
        BEAST_EXPECT(isNullBackend());

        setNullBackend(original);
    }
};

BEAST_DEFINE_TESTSUITE(RWDBBackend, nodestore, xrpl);

}  // namespace xrpl::test
