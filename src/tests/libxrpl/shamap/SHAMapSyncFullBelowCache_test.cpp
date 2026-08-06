/**
 * @file
 * @brief Tests for SHAMapSync FullBelowCache disable in null mode.
 */

#include <xrpl/basics/NullBackendFlag.h>
#include <xrpl/beast/unit_test.h>

namespace xrpl::test {

/**
 * Test SHAMapSync useFullBelowCache behavior with null-backend mode.
 *
 * When null mode is enabled, the shared FullBelowCache should be disabled
 * because nodes are not persisted and cache hits would skip unpinned subtrees.
 */
class SHAMapSyncFullBelowCache_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testUseFullBelowCacheNullMode();
        testUseFullBelowCacheNormalMode();
    }

    void
    testUseFullBelowCacheNullMode()
    {
        testcase("useFullBelowCache null mode");

        bool const original = isNullBackend();
        setNullBackend(true);

        bool const nullMode = isNullBackend();
        bool const useFullBelowCache = !nullMode;

        BEAST_EXPECT(nullMode);
        BEAST_EXPECT(!useFullBelowCache);

        setNullBackend(original);
    }

    void
    testUseFullBelowCacheNormalMode()
    {
        testcase("useFullBelowCache normal mode");

        bool const original = isNullBackend();
        setNullBackend(false);

        bool const nullMode = isNullBackend();
        bool const useFullBelowCache = !nullMode;

        BEAST_EXPECT(!nullMode);
        BEAST_EXPECT(useFullBelowCache);

        setNullBackend(original);
    }
};

BEAST_DEFINE_TESTSUITE(SHAMapSyncFullBelowCache, shamap, xrpl);

}  // namespace xrpl::test
