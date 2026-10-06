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

// nft_serial — an nft id region in, the sequence out as four little-endian bytes.
struct NFTSequenceGuest : GuestToHostCallTest
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

TEST_F(NFTSequenceGuest, nft_id_reaches_host_and_sequence_comes_back)
{
    EXPECT_CALL(host, getNFTSequence(Eq(nftId))).WillOnce(Return(kSequence));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kSequence));
}

}  // namespace xrpl::test
