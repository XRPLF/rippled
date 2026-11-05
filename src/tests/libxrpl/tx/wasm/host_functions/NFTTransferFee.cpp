
#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/NFTFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct NFTTransferFeeImpl : NFTTest
{
};

TEST_F(NFTTransferFeeImpl, transfer_fee_decodes_from_id)
{
    auto const issuer = Account{"issuer"};
    expectValue(makeHost()->getNFTTransferFee(makeNftId(issuer.id())), std::int32_t{kFee});
}

TEST_F(NFTTransferFeeImpl, transfer_fee_should_be_zero_with_zero_nft_id)
{
    expectValue(makeHost()->getNFTTransferFee(uint256{}), std::int32_t{});
}

}  // namespace xrpl::test
