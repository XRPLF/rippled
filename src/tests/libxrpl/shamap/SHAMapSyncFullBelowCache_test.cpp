/**
 * @file
 * @brief Tests for SHAMapSync FullBelowCache disable in null mode.
 */

#include <gtest/gtest.h>
#include <shamap/common.h>

namespace xrpl::tests {

/**
 * Shared FullBelowCache is unsafe in null-backend mode: a cache hit in
 * one SHAMap skips subtrees that were never pinned into another. The
 * production gate lives on Family::isNullBackend() so it is per-instance.
 */
TEST(SHAMapSyncFullBelowCache, nullModeDisablesSharedCache)
{
    TestNodeFamily family{beast::Journal{beast::Journal::getNullSink()}};
    family.setNullBackend(true);
    EXPECT_TRUE(family.isNullBackend());
}

TEST(SHAMapSyncFullBelowCache, normalModeEnablesSharedCache)
{
    TestNodeFamily const family{beast::Journal{beast::Journal::getNullSink()}};
    EXPECT_FALSE(family.isNullBackend());
}

TEST(SHAMapSyncFullBelowCache, familiesAreIndependent)
{
    TestNodeFamily const disk{beast::Journal{beast::Journal::getNullSink()}};
    TestNodeFamily rwdb{beast::Journal{beast::Journal::getNullSink()}};
    rwdb.setNullBackend(true);
    EXPECT_FALSE(disk.isNullBackend());
    EXPECT_TRUE(rwdb.isNullBackend());
}

}  // namespace xrpl::tests
