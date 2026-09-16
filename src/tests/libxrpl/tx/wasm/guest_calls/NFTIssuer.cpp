#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// nft_issuer — an nft id region in, the issuer account out.
struct NFTIssuerGuest : HostCallTest
{
    static constexpr std::int32_t kNftIdAt = 0;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kNftIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kIssuerLen = static_cast<std::int32_t>(AccountID::size());

    static constexpr Arg kNftId = Arg::region(kNftIdAt, kNftIdLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kIssuerLen);

    Bytes const nftIdBytes = Bytes(uint256::size(), 0x5f);
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());

    // An issuer whose first four bytes are distinctive, so the guest's `i32.load` of them
    // cannot pass by accident.
    Bytes const issuer = [] {
        Bytes bytes(kIssuerLen, 0xab);
        bytes[0] = 0x11;
        bytes[1] = 0x22;
        bytes[2] = 0x33;
        bytes[3] = 0x44;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg nftIdArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "nft_issuer", {nftIdArg, outArg}, {{.at = kNftIdAt, .bytes = nftIdBytes}}, answer);
    }
};

TEST_F(NFTIssuerGuest, NftIdReachesHostAndIssuerComesBack)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId))).WillOnce(Return(issuer));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x44332211) << "the issuer's first four bytes, little-endian";
}

TEST_F(NFTIssuerGuest, StatusIsTheIssuerLength)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId))).WillOnce(Return(issuer));

    auto const wat = watFor(kNftId, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kIssuerLen);
}

TEST_F(NFTIssuerGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTIssuerGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft issuer came apart"}));

    auto const outcome = callHost(watFor(kNftId, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTIssuer"));
}

TEST_F(NFTIssuerGuest, NftIdOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFTIssuer).Times(0);

    auto const wat = watFor(Arg::region(kNftIdAt, kNftIdLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTIssuerGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFTIssuer).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTIssuerGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFTIssuer).Times(0);

    auto const wat = watFor(Arg::region(-1, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTIssuerGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId))).WillOnce(Return(issuer));

    auto const wat = watFor(kNftId, Arg::outRegion(kOutAt, kIssuerLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(NFTIssuerGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId))).WillOnce(Return(issuer));

    auto const wat = watFor(kNftId, Arg::outRegion(kOnePage, kIssuerLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTIssuerGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId))).WillOnce(Return(issuer));

    auto const wat = watFor(kNftId, Arg::outRegion(-1, kIssuerLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
