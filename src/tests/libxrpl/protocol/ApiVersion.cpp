#include <xrpl/protocol/ApiVersion.h>

#include <gtest/gtest.h>

using namespace xrpl;

TEST(ApiVersion, invariants)
{
    static_assert(rpc::kApiMinimumSupportedVersion <= rpc::kApiMaximumSupportedVersion);
    static_assert(rpc::kApiMinimumSupportedVersion <= rpc::kApiMaximumValidVersion);
    static_assert(rpc::kApiMaximumSupportedVersion <= rpc::kApiMaximumValidVersion);
    static_assert(rpc::kApiBetaVersion <= rpc::kApiMaximumValidVersion);
}

// Update when we change versions
TEST(ApiVersion, versions)
{
    static_assert(rpc::kApiMinimumSupportedVersion >= 1);
    static_assert(rpc::kApiMinimumSupportedVersion < 2);
    static_assert(rpc::kApiMaximumSupportedVersion >= 2);
    static_assert(rpc::kApiMaximumSupportedVersion < 3);
    static_assert(rpc::kApiMaximumValidVersion >= 3);
    static_assert(rpc::kApiMaximumValidVersion < 4);
    static_assert(rpc::kApiBetaVersion >= 3);
    static_assert(rpc::kApiBetaVersion < 4);
}
