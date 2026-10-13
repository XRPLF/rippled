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

}  // namespace xrpl::test
