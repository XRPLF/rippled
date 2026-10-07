#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// nft_uri — an account region and an nft id region in, the token's URI out.
struct NFTGuest : GuestToHostCallTest
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

TEST_F(NFTGuest, account_and_nft_id_reach_host_and_uri_comes_back)
{
    EXPECT_CALL(host, getNFT(Eq(account), Eq(nftId))).WillOnce(Return(uri));

    auto const wat = watFor(kAccount, kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x73667069) << "the URI's first four bytes ('ipfs'), little-endian";
}

}  // namespace xrpl::test
