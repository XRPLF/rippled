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

// nft_flags — an nft id region in, the flags answered directly with no out region.
struct NFTFlagsGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kNftIdAt = 0;
    static constexpr std::int32_t kNftIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kFlags = 0x0b;

    static constexpr Arg kNftId = Arg::region(kNftIdAt, kNftIdLen);

    Bytes const nftIdBytes = Bytes(uint256::size(), 0xbf);
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());

    [[nodiscard]] std::string
    watFor(Arg nftIdArg) const
    {
        return hostCallWat("nft_flags", {nftIdArg}, {{.at = kNftIdAt, .bytes = nftIdBytes}});
    }
};

TEST_F(NFTFlagsGuest, nft_id_reaches_host_and_flags_come_back)
{
    EXPECT_CALL(host, getNFTFlags(Eq(nftId))).WillOnce(Return(kFlags));

    auto const wat = watFor(kNftId);
    EXPECT_EQ(hostAnswer(wat), kFlags);
}

TEST_F(NFTFlagsGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getNFTFlags(Eq(nftId)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kNftId);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTFlagsGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getNFTFlags(Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft flags came apart"}));

    auto const outcome = run(watFor(kNftId));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTFlags"));
}

TEST_F(NFTFlagsGuest, nft_id_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getNFTFlags).Times(0);

    auto const wat = watFor(Arg::region(kNftIdAt, kNftIdLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
