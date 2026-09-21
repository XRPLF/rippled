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

// paychan_id — two account regions and a four-byte `seq` region in, a keylet region out.
//
// The declared `u32` is not a wasm scalar: it arrives as a region holding the number
// little-endian (`args.rs`'s `InU32`), which is why `seq` below is spelled as bytes.
struct PaychannelKeyletGuest : GuestCallTest
{
    static constexpr std::int32_t kAccountAt = 0;
    static constexpr std::int32_t kDestinationAt = 32;
    static constexpr std::int32_t kSeqAt = 64;
    static constexpr std::int32_t kOutAt = 96;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kSeqLen = 4;
    static constexpr std::int32_t kKeyletLen = 32;
    static constexpr std::uint32_t kSeqValue = 0x12345678;

    static constexpr Arg kAccount = Arg::region(kAccountAt, kAccountLen);
    static constexpr Arg kDestination = Arg::region(kDestinationAt, kAccountLen);
    static constexpr Arg kSeq = Arg::region(kSeqAt, kSeqLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const accountBytes = Bytes(AccountID::size(), 0x71);
    Bytes const destinationBytes = Bytes(AccountID::size(), 0x84);
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    AccountID const destination = AccountID::fromVoid(destinationBytes.data());
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
    watFor(
        Arg accountArg,
        Arg destinationArg,
        Arg seqArg,
        Arg outArg,
        Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "paychan_id",
            {accountArg, destinationArg, seqArg, outArg},
            {{.at = kAccountAt, .bytes = accountBytes},
             {.at = kDestinationAt, .bytes = destinationBytes},
             {.at = kSeqAt, .bytes = seqBytes}},
            answer);
    }
};

TEST_F(PaychannelKeyletGuest, AccountsAndSeqReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDestination, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(PaychannelKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDestination, kSeq, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(PaychannelKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kDestination, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(PaychannelKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue))
        .WillOnce(testing::Throw(std::runtime_error{"paychannel keylet came apart"}));

    auto const outcome = run(watFor(kAccount, kDestination, kSeq, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("paychannelKeylet"));
}

TEST_F(PaychannelKeyletGuest, AccountOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kDestination, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PaychannelKeyletGuest, DestinationOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    auto const wat = watFor(kAccount, Arg::region(kDestinationAt, kAccountLen + 1), kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PaychannelKeyletGuest, SeqRegionOfAnyWidthButFourIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    for (std::int32_t const len : {kSeqLen - 1, kSeqLen + 1})
    {
        auto const wat = watFor(kAccount, kDestination, Arg::region(kSeqAt, len), kOut);
        EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams))
            << len << " bytes";
    }
}

TEST_F(PaychannelKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kDestination, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(PaychannelKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kDestination, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PaychannelKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDestination, kSeq, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(PaychannelKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDestination, kSeq, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(PaychannelKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDestination, kSeq, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
