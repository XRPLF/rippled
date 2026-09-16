#include <xrpl/basics/base_uint.h>
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

// nft_serial — an nft id region in, the sequence out as four little-endian bytes.
struct NFTSequenceGuest : HostCallTest
{
    static constexpr std::int32_t kNftIdAt = 0;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kNftIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kSequenceLen = 4;
    static constexpr std::uint32_t kSequence = 0x1a2b3c4d;

    static constexpr Arg kNftId = Arg::region(kNftIdAt, kNftIdLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kSequenceLen);

    Bytes const nftIdBytes = Bytes(uint256::size(), 0x9f);
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());

    [[nodiscard]] std::string
    watFor(Arg nftIdArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "nft_serial", {nftIdArg, outArg}, {{.at = kNftIdAt, .bytes = nftIdBytes}}, answer);
    }
};

TEST_F(NFTSequenceGuest, NftIdReachesHostAndSequenceComesBack)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId))).WillOnce(Return(kSequence));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kSequence));
}

TEST_F(NFTSequenceGuest, StatusIsTheSequenceLength)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId))).WillOnce(Return(kSequence));

    auto const wat = watFor(kNftId, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kSequenceLen);
}

TEST_F(NFTSequenceGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTSequenceGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft sequence came apart"}));

    auto const outcome = callHost(watFor(kNftId, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTSequence"));
}

TEST_F(NFTSequenceGuest, NftIdOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFTSequence).Times(0);

    auto const wat = watFor(Arg::region(kNftIdAt, kNftIdLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTSequenceGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFTSequence).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTSequenceGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getNFTSequence).Times(0);

    auto const wat = watFor(Arg::region(-1, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTSequenceGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId))).WillOnce(Return(kSequence));

    auto const wat = watFor(kNftId, Arg::outRegion(kOutAt, kSequenceLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(NFTSequenceGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId))).WillOnce(Return(kSequence));

    auto const wat = watFor(kNftId, Arg::outRegion(kOnePage, kSequenceLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTSequenceGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId))).WillOnce(Return(kSequence));

    auto const wat = watFor(kNftId, Arg::outRegion(-1, kSequenceLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
