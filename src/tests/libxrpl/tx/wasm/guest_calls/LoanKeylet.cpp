#include <xrpl/basics/base_uint.h>
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

using testing::Eq;
using testing::Return;

// loan_id — a 32-byte loan broker id and a four-byte sequence region in, a keylet out.
struct LoanKeyletGuest : GuestCallTest
{
    static constexpr std::int32_t kLoanBrokerIdAt = 0;
    static constexpr std::int32_t kSeqAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kLoanBrokerIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kSeqLen = 4;
    static constexpr std::int32_t kKeyletLen = 32;
    static constexpr std::uint32_t kSeqValue = 0x73849506;

    static constexpr Arg kLoanBrokerId = Arg::region(kLoanBrokerIdAt, kLoanBrokerIdLen);
    static constexpr Arg kSeq = Arg::region(kSeqAt, kSeqLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const loanBrokerIdBytes = Bytes(uint256::size(), 0x1b);
    uint256 const loanBrokerId = uint256::fromVoid(loanBrokerIdBytes.data());
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
    watFor(Arg loanBrokerIdArg, Arg seqArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "loan_id",
            {loanBrokerIdArg, seqArg, outArg},
            {{.at = kLoanBrokerIdAt, .bytes = loanBrokerIdBytes},
             {.at = kSeqAt, .bytes = seqBytes}},
            answer);
    }
};

TEST_F(LoanKeyletGuest, LoanBrokerIdAndSeqReachHostAndKeyletComesBack)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kLoanBrokerId, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(LoanKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kLoanBrokerId, kSeq, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(LoanKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kLoanBrokerId, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(LoanKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue))
        .WillOnce(testing::Throw(std::runtime_error{"loan keylet came apart"}));

    auto const outcome = run(watFor(kLoanBrokerId, kSeq, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("loanKeylet"));
}

TEST_F(LoanKeyletGuest, LoanBrokerIdOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanKeylet).Times(0);

    auto const wat = watFor(Arg::region(kLoanBrokerIdAt, kLoanBrokerIdLen - 1), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanKeyletGuest, SeqRegionOfAnyWidthButFourIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanKeylet).Times(0);

    for (std::int32_t const len : {kSeqLen - 1, kSeqLen + 1})
    {
        auto const wat = watFor(kLoanBrokerId, Arg::region(kSeqAt, len), kOut);
        EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams))
            << len << " bytes";
    }
}

TEST_F(LoanKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kLoanBrokerIdLen), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LoanKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kLoanBrokerIdLen), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kLoanBrokerId, kSeq, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(LoanKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kLoanBrokerId, kSeq, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LoanKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kLoanBrokerId, kSeq, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
