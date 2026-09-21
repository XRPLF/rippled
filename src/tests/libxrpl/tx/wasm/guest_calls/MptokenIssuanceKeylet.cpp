#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// mpt_issuance_id — an issuer region and a four-byte `seq` region in, a keylet region out.
struct MptokenIssuanceKeyletGuest : GuestCallTest
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

TEST_F(MptokenIssuanceKeyletGuest, IssuerAndSeqReachHostAndKeyletComesBack)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kIssuer, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(MptokenIssuanceKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kIssuer, kSeq, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(MptokenIssuanceKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kIssuer, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(MptokenIssuanceKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue))
        .WillOnce(testing::Throw(std::runtime_error{"mptoken issuance keylet came apart"}));

    auto const outcome = run(watFor(kIssuer, kSeq, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("mptokenIssuanceKeylet"));
}

TEST_F(MptokenIssuanceKeyletGuest, IssuerOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    auto const wat = watFor(Arg::region(kIssuerAt, kIssuerLen - 1), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenIssuanceKeyletGuest, SeqRegionOfAnyWidthButFourIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    for (std::int32_t const len : {kSeqLen - 1, kSeqLen + 1})
    {
        auto const wat = watFor(kIssuer, Arg::region(kSeqAt, len), kOut);
        EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams))
            << len << " bytes";
    }
}

TEST_F(MptokenIssuanceKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kIssuerLen), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(MptokenIssuanceKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kIssuerLen), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenIssuanceKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kIssuer, kSeq, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(MptokenIssuanceKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kIssuer, kSeq, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(MptokenIssuanceKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kIssuer, kSeq, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
