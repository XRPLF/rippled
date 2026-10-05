#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
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

TEST_F(NFTTransferFeeGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getNFTTransferFee(Eq(nftId)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kNftId);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTTransferFeeGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getNFTTransferFee(Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft transfer fee came apart"}));

    auto const outcome = run(watFor(kNftId));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTTransferFee"));
}

TEST_F(NFTTransferFeeGuest, nft_id_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTTransferFee).Times(0);

    auto const wat = watFor(Arg::region(kNftIdAt, kNftIdLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTTransferFeeGuest, input_region_past_memory_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTTransferFee).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kNftIdLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTTransferFeeGuest, negative_input_pointer_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTTransferFee).Times(0);

    auto const wat = watFor(Arg::region(-1, kNftIdLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
