#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ApiVersion.h>

namespace xrpl::test {
struct ApiVersion_test : beast::unit_test::Suite
{
    void
    run() override
    {
        {
            testcase("API versions invariants");

            static_assert(RPC::kApiMinimumSupportedVersion <= RPC::kApiMaximumSupportedVersion);
            static_assert(RPC::kApiMinimumSupportedVersion <= RPC::kApiMaximumValidVersion);
            static_assert(RPC::kApiMaximumSupportedVersion <= RPC::kApiMaximumValidVersion);
            static_assert(RPC::kApiBetaVersion <= RPC::kApiMaximumValidVersion);

            BEAST_EXPECT(true);
        }

        {
            // Update when we change versions
            testcase("API versions");

            static_assert(RPC::kApiMinimumSupportedVersion >= 1);
            static_assert(RPC::kApiMinimumSupportedVersion < 2);
            static_assert(RPC::kApiMaximumSupportedVersion >= 2);
            static_assert(RPC::kApiMaximumSupportedVersion < 3);
            static_assert(RPC::kApiMaximumValidVersion >= 3);
            static_assert(RPC::kApiMaximumValidVersion < 4);
            static_assert(RPC::kApiBetaVersion >= 3);
            static_assert(RPC::kApiBetaVersion < 4);

            BEAST_EXPECT(true);
        }
    }
};

BEAST_DEFINE_TESTSUITE(ApiVersion, protocol, xrpl);

}  // namespace xrpl::test
