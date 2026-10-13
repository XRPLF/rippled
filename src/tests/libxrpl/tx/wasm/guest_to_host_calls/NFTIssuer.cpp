#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// nft_issuer — an nft id region in, the issuer account out.
struct NFTIssuerGuest : GuestToHostCallTest
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

TEST_F(NFTIssuerGuest, nft_id_reaches_host_and_issuer_comes_back)
{
    EXPECT_CALL(host, getNFTIssuer(Eq(nftId))).WillOnce(Return(issuer));

    auto const wat = watFor(kNftId, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x44332211) << "the issuer's first four bytes, little-endian";
}

}  // namespace xrpl::test
