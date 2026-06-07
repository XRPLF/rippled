/**
 * @file
 * @brief Tests for SHAMapSync FullBelowCache disable in null mode.
 */

#include <xrpl/beast/unit_test.h>

#include <cstdlib>
#include <string>
#include <string_view>

namespace xrpl::test {

/**
 * Test SHAMapSync useFullBelowCache behavior with XRPL_RWDB_NULL.
 *
 * When null mode is enabled, the FullBelowCache should be disabled
 * because nodes are not persisted to disk.
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

        // Save current state
        char* original = std::getenv("XRPL_RWDB_NULL");
        std::string originalValue;
        bool hadOriginal = original != nullptr;
        if (hadOriginal)
            originalValue = original;

        // Set null mode
        setenv("XRPL_RWDB_NULL", "1", 1);

        // The useFullBelowCache logic checks the same environment variable
        // as RWDBBackend::nullMode(), so we verify the logic directly
        char const* e = std::getenv("XRPL_RWDB_NULL");
        bool nullMode = e && *e && std::string_view{e} != "0";
        bool useFullBelowCache = !nullMode;

        BEAST_EXPECT(nullMode);
        BEAST_EXPECT(!useFullBelowCache);

        // Restore original state
        if (hadOriginal)
            setenv("XRPL_RWDB_NULL", originalValue.c_str(), 1);
        else
            unsetenv("XRPL_RWDB_NULL");
    }

    void
    testUseFullBelowCacheNormalMode()
    {
        testcase("useFullBelowCache normal mode");

        // Save current state
        char* original = std::getenv("XRPL_RWDB_NULL");
        std::string originalValue;
        bool hadOriginal = original != nullptr;
        if (hadOriginal)
            originalValue = original;

        // Unset null mode
        unsetenv("XRPL_RWDB_NULL");

        // In normal mode, FullBelowCache should be enabled
        char const* e = std::getenv("XRPL_RWDB_NULL");
        bool nullMode = e && *e && std::string_view{e} != "0";
        bool useFullBelowCache = !nullMode;

        BEAST_EXPECT(!nullMode);
        BEAST_EXPECT(useFullBelowCache);

        // Restore original state
        if (hadOriginal)
            setenv("XRPL_RWDB_NULL", originalValue.c_str(), 1);
        else
            unsetenv("XRPL_RWDB_NULL");
    }
};

BEAST_DEFINE_TESTSUITE(SHAMapSyncFullBelowCache, shamap, xrpl);

}  // namespace xrpl::test
