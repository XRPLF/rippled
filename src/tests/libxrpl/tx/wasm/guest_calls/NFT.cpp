#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// nft_uri — an account region and an nft id region in, the token's URI out.
struct NFTGuest : GuestCallTest
{
    static constexpr std::int32_t kAccountAt = 0;
    static constexpr std::int32_t kNftIdAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kNftIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::string_view kUri = "ipfs://QmNftUri";
    static constexpr std::int32_t kUriLen = static_cast<std::int32_t>(kUri.size());

    static constexpr Arg kAccount = Arg::region(kAccountAt, kAccountLen);
    static constexpr Arg kNftId = Arg::region(kNftIdAt, kNftIdLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kUriLen);

    // Distinct byte patterns: two regions filled alike would pass even if they were swapped
    // on their way to the host.
    Bytes const accountBytes = Bytes(AccountID::size(), 0x14);
    Bytes const nftIdBytes = Bytes(uint256::size(), 0x2f);
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
    Bytes const uri = Bytes(kUri.begin(), kUri.end());

    [[nodiscard]] std::string
    watFor(Arg accountArg, Arg nftIdArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "nft_uri",
            {accountArg, nftIdArg, outArg},
            {{.at = kAccountAt, .bytes = accountBytes}, {.at = kNftIdAt, .bytes = nftIdBytes}},
            answer);
    }
};

TEST_F(NFTGuest, AccountAndNftIdReachHostAndUriComesBack)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId))).WillOnce(Return(uri));

    auto const wat = watFor(kAccount, kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x73667069) << "the URI's first four bytes ('ipfs'), little-endian";
}

TEST_F(NFTGuest, StatusIsTheUriLength)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId))).WillOnce(Return(uri));

    auto const wat = watFor(kAccount, kNftId, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kUriLen);
}

TEST_F(NFTGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft uri came apart"}));

    auto const outcome = run(watFor(kAccount, kNftId, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getNFT"));
}

TEST_F(NFTGuest, AccountOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFT).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTGuest, NftIdOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFT).Times(0);

    auto const wat = watFor(kAccount, Arg::region(kNftIdAt, kNftIdLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFT).Times(0);

    auto const wat = watFor(kAccount, Arg::region(kOnePage, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFT).Times(0);

    auto const wat = watFor(kAccount, Arg::region(-1, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId))).WillOnce(Return(uri));

    auto const wat = watFor(kAccount, kNftId, Arg::outRegion(kOutAt, kUriLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(NFTGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId))).WillOnce(Return(uri));

    auto const wat = watFor(kAccount, kNftId, Arg::outRegion(kOnePage, kUriLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId))).WillOnce(Return(uri));

    auto const wat = watFor(kAccount, kNftId, Arg::outRegion(-1, kUriLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
