
#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/NFTFixture.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct NFTTransferFeeImpl : NFTTest
{
};

TEST_F(NFTTransferFeeImpl, TransferFeeDecodesFromId)
{
    auto const issuer = Account{"issuer"};
    expectValue(makeHost()->getNFTTransferFee(makeNftId(issuer.id())), std::int32_t{kFee});
}

TEST_F(NFTTransferFeeImpl, TransferFeeShouldBeZeroWithZeroNftId)
{
    expectValue(makeHost()->getNFTTransferFee(uint256{}), std::int32_t{});
}

}  // namespace xrpl::test
