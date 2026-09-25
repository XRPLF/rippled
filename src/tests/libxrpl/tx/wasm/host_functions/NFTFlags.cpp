
#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/NFTFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct NFTFlagsImpl : NFTTest
{
};

TEST_F(NFTFlagsImpl, FlagsDecodeFromId)
{
    auto const issuer = Account{"issuer"};
    expectValue(makeHost()->getNFTFlags(makeNftId(issuer.id())), std::int32_t{kFlags});
}

TEST_F(NFTFlagsImpl, FlagsShouldBeZeroWithZeroNftId)
{
    expectValue(makeHost()->getNFTFlags(uint256{}), std::int32_t{});
}

}  // namespace xrpl::test
