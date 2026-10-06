#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// mpt_issuance_id — an issuer region and a four-byte `seq` region in, a keylet region out.
struct MptokenIssuanceKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kIssuerAt = 0;
    static constexpr std::int32_t kSeqAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kIssuerLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kSeqLen = 4;
    static constexpr std::int32_t kKeyletLen = 32;
    static constexpr std::uint32_t kSeqValue = 0x71829304;

    static constexpr Arg kIssuer = Arg::region(kIssuerAt, kIssuerLen);
    static constexpr Arg kSeq = Arg::region(kSeqAt, kSeqLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const issuerBytes = Bytes(AccountID::size(), 0x97);
    AccountID const issuer = AccountID::fromVoid(issuerBytes.data());
    Bytes const seqBytes{
        static_cast<std::uint8_t>(kSeqValue),
        static_cast<std::uint8_t>(kSeqValue >> 8),
        static_cast<std::uint8_t>(kSeqValue >> 16),
        static_cast<std::uint8_t>(kSeqValue >> 24)};

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg issuerArg, Arg seqArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "mpt_issuance_id",
            {issuerArg, seqArg, outArg},
            {{.at = kIssuerAt, .bytes = issuerBytes}, {.at = kSeqAt, .bytes = seqBytes}},
            answer);
    }
};

TEST_F(MptokenIssuanceKeyletGuest, issuer_and_seq_reach_host_and_keylet_comes_back)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kIssuer, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test
