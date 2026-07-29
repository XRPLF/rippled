#include <xrpl/protocol/ApiVersion.h>

#include <gtest/gtest.h>

using namespace xrpl;

TEST(ApiVersion, invariants)
{
    static_assert(RPC::kApiMinimumSupportedVersion <= RPC::kApiMaximumSupportedVersion);
    static_assert(RPC::kApiMinimumSupportedVersion <= RPC::kApiMaximumValidVersion);
    static_assert(RPC::kApiMaximumSupportedVersion <= RPC::kApiMaximumValidVersion);
    static_assert(RPC::kApiBetaVersion <= RPC::kApiMaximumValidVersion);
}

// Update when we change versions
TEST(ApiVersion, versions)
{
    static_assert(RPC::kApiMinimumSupportedVersion >= 1);
    static_assert(RPC::kApiMinimumSupportedVersion < 2);
    static_assert(RPC::kApiMaximumSupportedVersion >= 2);
    static_assert(RPC::kApiMaximumSupportedVersion < 3);
    static_assert(RPC::kApiMaximumValidVersion >= 3);
    static_assert(RPC::kApiMaximumValidVersion < 4);
    static_assert(RPC::kApiBetaVersion >= 3);
    static_assert(RPC::kApiBetaVersion < 4);
}
