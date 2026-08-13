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
    NullBackendScope const on(true);

    EXPECT_TRUE(isNullBackend());
    EXPECT_FALSE(useSharedFullBelowCache());
}

TEST(SHAMapSyncFullBelowCache, normalModeEnablesSharedCache)
{
    // Do not store an absolute false: that would drop another live
    // RWDB SHAMapStoreImp in this process. If the count is already
    // non-zero, shared cache is correctly disabled.
    if (isNullBackend())
    {
        EXPECT_FALSE(useSharedFullBelowCache());
        return;
    }

    EXPECT_TRUE(useSharedFullBelowCache());
}

}  // namespace xrpl::tests
