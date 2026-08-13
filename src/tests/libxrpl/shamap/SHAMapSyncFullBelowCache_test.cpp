/**
 * @file
 * @brief Tests for SHAMapSync FullBelowCache disable in null mode.
 */

#include <xrpl/basics/NullBackendFlag.h>

#include <gtest/gtest.h>

namespace xrpl::tests {

/**
 * Shared FullBelowCache is unsafe in null-backend mode: a cache hit in
 * one SHAMap skips subtrees that were never pinned into another. The
 * production gate lives in useSharedFullBelowCache().
 */
TEST(SHAMapSyncFullBelowCache, nullModeDisablesSharedCache)
{
    bool const original = isNullBackend();
    setNullBackend(true);

    EXPECT_TRUE(isNullBackend());
    EXPECT_FALSE(useSharedFullBelowCache());

    setNullBackend(original);
}

TEST(SHAMapSyncFullBelowCache, normalModeEnablesSharedCache)
{
    bool const original = isNullBackend();
    setNullBackend(false);

    EXPECT_FALSE(isNullBackend());
    EXPECT_TRUE(useSharedFullBelowCache());

    setNullBackend(original);
}

}  // namespace xrpl::tests
