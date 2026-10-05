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

// nft_taxon — an nft id region in, the taxon out as four little-endian bytes.
struct NFTTaxonGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kNftIdAt = 0;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kNftIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kTaxonLen = 4;
    static constexpr std::uint32_t kTaxon = 0x12345678;

    static constexpr Arg kNftId = Arg::region(kNftIdAt, kNftIdLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kTaxonLen);

    Bytes const nftIdBytes = Bytes(uint256::size(), 0x7f);
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());

    [[nodiscard]] std::string
    watFor(Arg nftIdArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "nft_taxon", {nftIdArg, outArg}, {{.at = kNftIdAt, .bytes = nftIdBytes}}, answer);
    }
};

TEST_F(NFTTaxonGuest, nft_id_reaches_host_and_taxon_comes_back)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId))).WillOnce(Return(kTaxon));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kTaxon));
}

TEST_F(NFTTaxonGuest, status_is_the_taxon_length)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId))).WillOnce(Return(kTaxon));

    auto const wat = watFor(kNftId, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kTaxonLen);
}

TEST_F(NFTTaxonGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTTaxonGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft taxon came apart"}));

    auto const outcome = run(watFor(kNftId, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTTaxon"));
}

TEST_F(NFTTaxonGuest, nft_id_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTTaxon).Times(0);

    auto const wat = watFor(Arg::region(kNftIdAt, kNftIdLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTTaxonGuest, input_region_past_memory_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTTaxon).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTTaxonGuest, negative_input_pointer_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTTaxon).Times(0);

    auto const wat = watFor(Arg::region(-1, kNftIdLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NFTTaxonGuest, out_region_one_byte_short_is_refused_after_asking_host)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId))).WillOnce(Return(kTaxon));

    auto const wat = watFor(kNftId, Arg::outRegion(kOutAt, kTaxonLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(NFTTaxonGuest, out_region_past_memory_is_refused_after_asking_host)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId))).WillOnce(Return(kTaxon));

    auto const wat = watFor(kNftId, Arg::outRegion(kOnePage, kTaxonLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(NFTTaxonGuest, negative_out_pointer_is_refused_after_asking_host)
{
    EXPECT_CALL(host, getNFTTaxon(Eq(nftId))).WillOnce(Return(kTaxon));

    auto const wat = watFor(kNftId, Arg::outRegion(-1, kTaxonLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
