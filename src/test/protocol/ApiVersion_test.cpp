#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ApiVersion.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace ripple {
namespace test {
struct ApiVersion_test : beast::unit_test::suite
{
    void
    run() override
    {
        {
            testcase("API versions invariants");

            static_assert(
                RPC::apiMinimumSupportedVersion <=
                RPC::apiMaximumSupportedVersion);
            static_assert(
                RPC::apiMinimumSupportedVersion <= RPC::apiMaximumValidVersion);
            static_assert(
                RPC::apiMaximumSupportedVersion <= RPC::apiMaximumValidVersion);
            static_assert(RPC::apiBetaVersion <= RPC::apiMaximumValidVersion);

            BEAST_EXPECT(true);
        }

        {
            // Update when we change versions
            testcase("API versions");

            static_assert(RPC::apiMinimumSupportedVersion >= 1);
            static_assert(RPC::apiMinimumSupportedVersion < 2);
            static_assert(RPC::apiMaximumSupportedVersion >= 2);
            static_assert(RPC::apiMaximumSupportedVersion < 3);
            static_assert(RPC::apiMaximumValidVersion >= 3);
            static_assert(RPC::apiMaximumValidVersion < 4);
            static_assert(RPC::apiBetaVersion >= 3);
            static_assert(RPC::apiBetaVersion < 4);

            BEAST_EXPECT(true);
        }
    }
};

BEAST_DEFINE_TESTSUITE(ApiVersion, protocol, ripple);

}  // namespace test
}  // namespace ripple
