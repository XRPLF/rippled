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

}  // namespace xrpl::test
