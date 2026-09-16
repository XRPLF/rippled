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

// loan_broker_id — an owner region and a four-byte `seq` region in, a keylet region out.
struct LoanBrokerKeyletGuest : GuestCallTest
{
    static constexpr std::int32_t kOwnerAt = 0;
    static constexpr std::int32_t kSeqAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOwnerLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kSeqLen = 4;
    static constexpr std::int32_t kKeyletLen = 32;
    static constexpr std::uint32_t kSeqValue = 0x72839405;

    static constexpr Arg kOwner = Arg::region(kOwnerAt, kOwnerLen);
    static constexpr Arg kSeq = Arg::region(kSeqAt, kSeqLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const ownerBytes = Bytes(AccountID::size(), 0x6b);
    AccountID const owner = AccountID::fromVoid(ownerBytes.data());
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
    watFor(Arg ownerArg, Arg seqArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "loan_broker_id",
            {ownerArg, seqArg, outArg},
            {{.at = kOwnerAt, .bytes = ownerBytes}, {.at = kSeqAt, .bytes = seqBytes}},
            answer);
    }
};

TEST_F(LoanBrokerKeyletGuest, OwnerAndSeqReachHostAndKeyletComesBack)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kOwner, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(LoanBrokerKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kOwner, kSeq, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(LoanBrokerKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOwner, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(LoanBrokerKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue))
        .WillOnce(testing::Throw(std::runtime_error{"loan broker keylet came apart"}));

    auto const outcome = run(watFor(kOwner, kSeq, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("loanBrokerKeylet"));
}

TEST_F(LoanBrokerKeyletGuest, OwnerOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOwnerAt, kOwnerLen - 1), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanBrokerKeyletGuest, SeqRegionOfAnyWidthButFourIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    for (std::int32_t const len : {kSeqLen - 1, kSeqLen + 1})
    {
        auto const wat = watFor(kOwner, Arg::region(kSeqAt, len), kOut);
        EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams))
            << len << " bytes";
    }
}

TEST_F(LoanBrokerKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kOwnerLen), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LoanBrokerKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kOwnerLen), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanBrokerKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kOwner, kSeq, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(LoanBrokerKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kOwner, kSeq, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LoanBrokerKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kOwner, kSeq, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
