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

            static_assert(
                RPC::kAPI_MINIMUM_SUPPORTED_VERSION <= RPC::kAPI_MAXIMUM_SUPPORTED_VERSION);
            static_assert(RPC::kAPI_MINIMUM_SUPPORTED_VERSION <= RPC::kAPI_MAXIMUM_VALID_VERSION);
            static_assert(RPC::kAPI_MAXIMUM_SUPPORTED_VERSION <= RPC::kAPI_MAXIMUM_VALID_VERSION);
            static_assert(RPC::kAPI_BETA_VERSION <= RPC::kAPI_MAXIMUM_VALID_VERSION);

            BEAST_EXPECT(true);
        }

        {
            // Update when we change versions
            testcase("API versions");

            static_assert(RPC::kAPI_MINIMUM_SUPPORTED_VERSION >= 1);
            static_assert(RPC::kAPI_MINIMUM_SUPPORTED_VERSION < 2);
            static_assert(RPC::kAPI_MAXIMUM_SUPPORTED_VERSION >= 2);
            static_assert(RPC::kAPI_MAXIMUM_SUPPORTED_VERSION < 3);
            static_assert(RPC::kAPI_MAXIMUM_VALID_VERSION >= 3);
            static_assert(RPC::kAPI_MAXIMUM_VALID_VERSION < 4);
            static_assert(RPC::kAPI_BETA_VERSION >= 3);
            static_assert(RPC::kAPI_BETA_VERSION < 4);

            BEAST_EXPECT(true);
        }
    }
};

BEAST_DEFINE_TESTSUITE(ApiVersion, protocol, xrpl);

}  // namespace xrpl::test
