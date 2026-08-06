/**
 * @file
 * @brief Tests for SHAMapSync FullBelowCache disable in null mode.
 */

#include <xrpl/basics/NullBackendFlag.h>

#include <gtest/gtest.h>

namespace xrpl::tests {

/**
 * When null mode is enabled, the shared FullBelowCache should be disabled
 * because nodes are not persisted and cache hits would skip unpinned subtrees.
 */
TEST(SHAMapSyncFullBelowCache, nullModeDisablesSharedCache)
{
    bool const original = isNullBackend();
    setNullBackend(true);

    bool const nullMode = isNullBackend();
    bool const useFullBelowCache = !nullMode;

    EXPECT_TRUE(nullMode);
    EXPECT_FALSE(useFullBelowCache);

    setNullBackend(original);
}

TEST(SHAMapSyncFullBelowCache, normalModeEnablesSharedCache)
{
    bool const original = isNullBackend();
    setNullBackend(false);

    bool const nullMode = isNullBackend();
    bool const useFullBelowCache = !nullMode;

    EXPECT_FALSE(nullMode);
    EXPECT_TRUE(useFullBelowCache);

    setNullBackend(original);
}

}  // namespace xrpl::tests
