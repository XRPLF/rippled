#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// nft_xfer_fee — an nft id region in, the transfer fee answered directly with no out region.
struct NFTTransferFeeGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kNftIdAt = 0;
    static constexpr std::int32_t kNftIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kTransferFee = 9999;

    static constexpr Arg kNftId = Arg::region(kNftIdAt, kNftIdLen);

    Bytes const nftIdBytes = Bytes(uint256::size(), 0xdf);
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());

    [[nodiscard]] std::string
    watFor(Arg nftIdArg) const
    {
        return hostCallWat("nft_xfer_fee", {nftIdArg}, {{.at = kNftIdAt, .bytes = nftIdBytes}});
    }
};

TEST_F(NFTTransferFeeGuest, nft_id_reaches_host_and_transfer_fee_comes_back)
{
    EXPECT_CALL(host, getNFTTransferFee(Eq(nftId))).WillOnce(Return(kTransferFee));

    auto const wat = watFor(kNftId);
    EXPECT_EQ(hostAnswer(wat), kTransferFee);
}

}  // namespace xrpl::test
